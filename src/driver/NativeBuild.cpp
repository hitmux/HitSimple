#include "driver/NativeBuild.h"

#include "driver/Diagnostics.h"
#include "driver/FrontendPipeline.h"
#include "hitsimple/codegen/LlvmCompatibility.h"
#include "hitsimple/codegen/NativeTarget.h"
#include "hitsimple/codegen/ObjectEmitter.h"
#include "hitsimple/codegen/OptimizationPipeline.h"
#include "hitsimple/support/Cargo.h"
#include "hitsimple/support/CompilationMetrics.h"
#include "hitsimple/support/Path.h"
#include "hitsimple/support/Process.h"
#include "hitsimple/support/ResourcePaths.h"
#include "hitsimple/support/Toolchain.h"

#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

#ifndef HITSIMPLE_BOOST_INCLUDE_DIR
#define HITSIMPLE_BOOST_INCLUDE_DIR ""
#endif

namespace hitsimple::driver {

namespace {

std::string serializeLlvmModule(const llvm::Module &module) {
  std::string llvmIr;
  llvm::raw_string_ostream output(llvmIr);
  module.print(output, nullptr);
  output.flush();
  return llvmIr;
}

void appendSanitizerArguments(std::vector<std::string> &arguments,
                              const NativeBackendOptions &backendOptions) {
  switch (backendOptions.sanitizer) {
  case Sanitizer::None:
    return;
  case Sanitizer::Address:
    arguments.emplace_back("-fsanitize=address");
    break;
  case Sanitizer::Undefined:
    arguments.emplace_back("-fsanitize=undefined");
    arguments.emplace_back("-fno-sanitize-recover=undefined");
    break;
  }
  arguments.emplace_back("-fno-omit-frame-pointer");
}

std::optional<std::vector<std::filesystem::path>>
sanitizerRuntimeSources(const NativeBackendOptions &backendOptions,
                        std::string &error) {
  if (backendOptions.sanitizer == Sanitizer::None) {
    return std::vector<std::filesystem::path>{};
  }
#if !defined(__linux__) && !defined(__APPLE__)
  error = "--sanitize native executable builds currently support only Linux "
          "and macOS; Windows is rejected until its COFF runtime packaging "
          "and CI coverage are implemented";
  return std::nullopt;
#elif defined(__linux__) && !defined(__x86_64__)
  error = "--sanitize native executable builds currently support only x86_64 "
          "Linux and macOS; Linux targets without Clang fp128 runtime support "
          "are rejected";
  return std::nullopt;
#else
  const auto runtimeSource = hitsimple::support::runtimeSourcePath();
  std::vector<std::filesystem::path> sources{runtimeSource};
#if defined(__APPLE__)
  sources.push_back(runtimeSource.parent_path() / "hitsimple_f128.cpp");
#else
  sources.push_back(runtimeSource.parent_path() / "hitsimple_f128_native.c");
#endif
  for (const auto &source : sources) {
    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(source, filesystemError)) {
      error = "sanitizer runtime source is unavailable '" +
              hitsimple::support::pathToUtf8(source) + "'";
      if (filesystemError) {
        error += ": " + filesystemError.message();
      }
      return std::nullopt;
    }
  }
  return sources;
#endif
}

bool isCxxSanitizerRuntimeSource(const std::filesystem::path &source) {
  return source.extension() == ".cpp";
}

void appendCxxSanitizerRuntimeArguments(
    std::vector<std::string> &arguments) {
  arguments.emplace_back("-std=c++20");
#if defined(__APPLE__)
  constexpr std::string_view boostIncludeDirectory =
      HITSIMPLE_BOOST_INCLUDE_DIR;
  if (!boostIncludeDirectory.empty()) {
    arguments.emplace_back("-I" + std::string(boostIncludeDirectory));
  }
#endif
}

void appendSanitizerRuntimePlatformArguments(
    std::vector<std::string> &arguments) {
#if defined(__APPLE__)
  // Match the installed runtime library: the C source delegates f128 to the
  // software C++ backend instead of relying on Linux's native _Float128.
  arguments.emplace_back("-DHITSIMPLE_SOFTWARE_F128=1");
#else
  static_cast<void>(arguments);
#endif
}

#if defined(__APPLE__)
bool emitMacDsym(const std::string &executablePath, std::string &error) {
  auto dsymPath = hitsimple::support::pathFromUtf8(executablePath);
  dsymPath += ".dSYM";
  std::error_code removeError;
  std::filesystem::remove_all(dsymPath, removeError);
  if (removeError) {
    error = "cannot remove stale dSYM '" +
            hitsimple::support::pathToUtf8(dsymPath) + "': " +
            removeError.message();
    return false;
  }
  const auto process =
      hitsimple::support::runProcess("dsymutil", {executablePath});
  if (!process.launched) {
    error = "cannot start dsymutil: " + process.error;
    return false;
  }
  if (process.exitCode != 0) {
    error = "dsymutil failed for '" + executablePath + "' (exit code " +
            std::to_string(process.exitCode) + ")";
    return false;
  }
  return true;
}
#endif

int reportMissingLinkerDriver(
    const hitsimple::support::ClangSelection &selection,
    hitsimple::support::CompilationMetrics &metrics) {
  std::cerr << "hsc: object emission succeeded; no compatible linker driver "
               "found";
  if (!selection.error.empty()) {
    std::cerr << ": " << selection.error;
  }
  std::cerr << '\n';
  const auto linkStarted = metrics.now();
  metrics.fail(metrics.clangBackendLink(), linkStarted);
  metrics.fail("clang_backend_link");
  return EXIT_FAILURE;
}

} // namespace

std::string_view clangOptimizationFlag(OptimizationLevel level) {
  switch (level) {
  case OptimizationLevel::O0:
    return "-O0";
  case OptimizationLevel::O1:
    return "-O1";
  case OptimizationLevel::O2:
    return "-O2";
  case OptimizationLevel::O3:
    return "-O3";
  case OptimizationLevel::Os:
    return "-Os";
  }
  return "-O2";
}

void appendClangCodegenArguments(std::vector<std::string> &arguments,
                                 const NativeBackendOptions &backendOptions) {
  arguments.emplace_back(clangOptimizationFlag(backendOptions.optimization));
  appendSanitizerArguments(arguments, backendOptions);
  switch (backendOptions.pgoMode) {
  case PgoMode::None:
    return;
  case PgoMode::Instrument:
    arguments.push_back(
        "-fprofile-instr-generate=" +
        hitsimple::support::pathToUtf8(backendOptions.profilePath));
    return;
  case PgoMode::Use:
    arguments.push_back(
        "-fprofile-instr-use=" +
        hitsimple::support::pathToUtf8(backendOptions.profilePath));
    return;
  }
}

void appendClangLinkArguments(std::vector<std::string> &arguments,
                              const NativeBackendOptions &backendOptions) {
  arguments.emplace_back(clangOptimizationFlag(backendOptions.optimization));
  appendSanitizerArguments(arguments, backendOptions);
  if (backendOptions.pgoMode == PgoMode::Instrument) {
    arguments.push_back(
        "-fprofile-instr-generate=" +
        hitsimple::support::pathToUtf8(backendOptions.profilePath));
  }
}

std::string targetTriple() {
#ifdef _WIN32
  return "x86_64-w64-windows-gnu";
#else
  return llvm::sys::getDefaultTargetTriple();
#endif
}

bool usesWindowsCodeView(
    const hitsimple::codegen::CodegenOptions &codegenOptions) {
  const auto triple = codegenOptions.targetTriple.empty()
                          ? targetTriple()
                          : codegenOptions.targetTriple;
  return llvm::Triple(triple).isOSWindows();
}

std::filesystem::path pdbPathForExecutable(const std::string &executablePath) {
  auto pdbPath = hitsimple::support::pathFromUtf8(executablePath);
  pdbPath.replace_extension(".pdb");
  return pdbPath;
}

class TemporaryDirectory final {
public:
  explicit TemporaryDirectory(std::filesystem::path path)
      : path_(std::move(path)) {}

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

bool emitOptimizedObject(llvm::Module &module,
                         const std::filesystem::path &objectPath,
                         const NativeBackendOptions &backendOptions) {
  hitsimple::codegen::OptimizationPipelineOptions pipelineOptions;
  pipelineOptions.optimization = backendOptions.optimization;
  pipelineOptions.pgoMode = backendOptions.pgoMode;
  if (backendOptions.sanitizer == Sanitizer::Address) {
    pipelineOptions.sanitizer =
        hitsimple::codegen::SanitizerInstrumentation::Address;
  }
  pipelineOptions.profilePath =
      hitsimple::support::pathToUtf8(backendOptions.profilePath);
  pipelineOptions.emitRemarks =
      backendOptions.optimizationRemarksPath.has_value();

  hitsimple::codegen::NativeTargetOptions targetOptions;
  targetOptions.triple = hitsimple::codegen::moduleTargetTriple(module);
  targetOptions.optimization = backendOptions.optimization;
  std::string targetError;
  const auto nativeTarget =
      hitsimple::codegen::createNativeTarget(targetOptions, targetError);
  if (!nativeTarget) {
    std::cerr << "hsc: cannot create native target for object emission: "
              << targetError << '\n';
    return false;
  }
  module.setDataLayout(nativeTarget->machine->createDataLayout());

  hitsimple::codegen::OptimizationPipelineResult optimized;
  std::string pipelineError;
  if (!hitsimple::codegen::runOptimizationPipeline(
          module, *nativeTarget->machine, pipelineOptions, optimized,
          pipelineError)) {
    std::cerr << "hsc: optimization pipeline failed: " << pipelineError << '\n';
    return false;
  }
  if (backendOptions.optimizationRemarksPath &&
      !appendOptimizationRemarks(*backendOptions.optimizationRemarksPath,
                                 optimized.remarks)) {
    return false;
  }
  hitsimple::codegen::ObjectEmissionOptions emissionOptions;
  std::string emissionError;
  if (!hitsimple::codegen::emitObjectFile(
          module, *nativeTarget->machine, objectPath, emissionOptions,
          emissionError)) {
    std::cerr << "hsc: object emission failed: " << emissionError << '\n';
    return false;
  }
  return true;
}

std::string defaultObjectOutputPath(const std::string &inputPath) {
  auto output = hitsimple::support::pathFromUtf8(inputPath);
#ifdef _WIN32
  output.replace_extension(".obj");
#else
  output.replace_extension(".o");
#endif
  return hitsimple::support::pathToUtf8(output);
}

int compileObject(const std::vector<std::string> &inputPaths,
                  const std::optional<std::string> &outputPath,
                  hitsimple::codegen::CodegenOptions codegenOptions,
                  bool cCompatibilityMode,
                  hitsimple::stdlib::BuiltinProviderSelection providerSelection,
                  const NativeBackendOptions &backendOptions,
                  hitsimple::support::CompilationMetrics &metrics) {
  if (inputPaths.size() != 1U) {
    std::cerr << "hsc: --crate-type=object supports exactly one input file\n";
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  const auto objectPath =
      outputPath.value_or(defaultObjectOutputPath(inputPaths.front()));
  if (!validateOutputParent(objectPath)) {
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  if (!compileObjectTranslationUnit(
          inputPaths.front(), hitsimple::support::pathFromUtf8(objectPath),
          codegenOptions, cCompatibilityMode, providerSelection, true,
          backendOptions, metrics)) {
    return EXIT_FAILURE;
  }
  metrics.markSkipped(metrics.llvmIrWrite());
  metrics.markSkipped(metrics.clangBackendLink());
  return EXIT_SUCCESS;
}

int compileStaticLibrary(
    const std::vector<std::string> &inputPaths,
    const std::optional<std::string> &outputPath,
    hitsimple::codegen::CodegenOptions codegenOptions, bool cCompatibilityMode,
    hitsimple::stdlib::BuiltinProviderSelection providerSelection,
    const hitsimple::support::LlvmArSelection &llvmAr,
    const NativeBackendOptions &backendOptions,
    hitsimple::support::CompilationMetrics &metrics) {
  if (!llvmAr.path) {
    std::cerr << "hsc: " << llvmAr.error << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  const auto archivePath = outputPath.value_or("libhitsimple.a");
  if (!validateOutputParent(archivePath)) {
    metrics.fail("cli");
    return EXIT_FAILURE;
  }

  const auto temporaryPath =
      std::filesystem::temp_directory_path() /
      ("hitsimple-staticlib-" +
       std::to_string(hitsimple::support::currentProcessId()));
  std::error_code directoryError;
  std::filesystem::remove_all(temporaryPath, directoryError);
  directoryError.clear();
  if (!std::filesystem::create_directories(temporaryPath, directoryError)) {
    std::cerr << "hsc: cannot create static library temporary directory '"
              << hitsimple::support::pathToUtf8(temporaryPath)
              << "': " << directoryError.message() << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  TemporaryDirectory temporaryDirectory(temporaryPath);

  std::vector<std::filesystem::path> objectPaths;
  objectPaths.reserve(inputPaths.size());
  std::vector<std::string> sourceModuleIds;
  std::unordered_set<std::string> seenSourceModules;
  for (std::size_t index = 0; index < inputPaths.size(); ++index) {
    const auto objectPath =
        temporaryPath / ("unit-" + std::to_string(index) + ".o");
    auto compiled = compileObjectTranslationUnit(
        inputPaths[index], objectPath, codegenOptions, cCompatibilityMode,
        providerSelection, false, backendOptions, metrics);
    if (!compiled) {
      return EXIT_FAILURE;
    }
    for (const auto &module : compiled->sourceModules) {
      if (seenSourceModules.insert(module).second) {
        sourceModuleIds.push_back(module);
      }
    }
    objectPaths.push_back(objectPath);
  }
  auto sourceModules =
      compileSourceModules(sourceModuleIds, codegenOptions, metrics);
  if (!sourceModules) {
    return EXIT_FAILURE;
  }
  for (std::size_t index = 0; index < sourceModules->size(); ++index) {
    const auto objectPath =
        temporaryPath / ("stdlib-" + std::to_string(index) + ".o");
    metrics.translationUnits()
        .at((*sourceModules)[index].metricsIndex)
        .llvmIrBytes =
        serializeLlvmModule(*(*sourceModules)[index].emission.module).size();
    if (!emitOptimizedObject(*(*sourceModules)[index].emission.module,
                             objectPath, backendOptions)) {
      metrics.fail("llvm_emission");
      return EXIT_FAILURE;
    }
    objectPaths.push_back(objectPath);
  }

  const auto runtimePath = hitsimple::support::runtimeLibraryPath();
  std::error_code runtimeError;
  if (!std::filesystem::is_regular_file(runtimePath, runtimeError)) {
    std::cerr << "hsc: runtime static library is unavailable '"
              << hitsimple::support::pathToUtf8(runtimePath) << "'";
    if (runtimeError) {
      std::cerr << ": " << runtimeError.message();
    }
    std::cerr << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }

  const auto originalDirectory = std::filesystem::current_path(directoryError);
  if (directoryError) {
    std::cerr
        << "hsc: cannot read current directory while creating static library: "
        << directoryError.message() << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  std::filesystem::current_path(temporaryPath, directoryError);
  if (directoryError) {
    std::cerr << "hsc: cannot enter static library temporary directory: "
              << directoryError.message() << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  const auto extractResult = hitsimple::support::runProcess(
      *llvmAr.path, {"x", hitsimple::support::pathToUtf8(runtimePath)});
  std::error_code restoreError;
  std::filesystem::current_path(originalDirectory, restoreError);
  if (restoreError) {
    std::cerr << "hsc: cannot restore current directory after llvm-ar: "
              << restoreError.message() << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  if (!extractResult.launched) {
    std::cerr << "hsc: cannot start llvm-ar '"
              << hitsimple::support::pathToUtf8(*llvmAr.path)
              << "': " << extractResult.error << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  if (extractResult.exitCode != 0) {
    std::cerr << "hsc: llvm-ar failed while extracting the runtime archive "
              << "(exit code " << extractResult.exitCode << ")\n";
    metrics.fail("cli");
    return EXIT_FAILURE;
  }

  for (const auto &entry : std::filesystem::directory_iterator(temporaryPath)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto extension = entry.path().extension();
    if (extension == ".o" || extension == ".obj") {
      if (std::find(objectPaths.begin(), objectPaths.end(), entry.path()) ==
          objectPaths.end()) {
        objectPaths.push_back(entry.path());
      }
    }
  }
  if (objectPaths.size() == inputPaths.size()) {
    std::cerr << "hsc: llvm-ar extracted no runtime object members\n";
    metrics.fail("cli");
    return EXIT_FAILURE;
  }

  const auto temporaryArchive = temporaryPath / "libhitsimple.a";
  std::vector<std::string> archiveArguments{
      "rcs", hitsimple::support::pathToUtf8(temporaryArchive)};
  for (const auto &objectPath : objectPaths) {
    archiveArguments.push_back(hitsimple::support::pathToUtf8(objectPath));
  }
  const auto archiveResult =
      hitsimple::support::runProcess(*llvmAr.path, archiveArguments);
  if (!archiveResult.launched) {
    std::cerr << "hsc: cannot start llvm-ar '"
              << hitsimple::support::pathToUtf8(*llvmAr.path)
              << "': " << archiveResult.error << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  if (archiveResult.exitCode != 0) {
    std::cerr << "hsc: llvm-ar failed while creating static library (exit code "
              << archiveResult.exitCode << ")\n";
    metrics.fail("cli");
    return EXIT_FAILURE;
  }

  std::error_code copyError;
  std::filesystem::copy_file(
      temporaryArchive, hitsimple::support::pathFromUtf8(archivePath),
      std::filesystem::copy_options::overwrite_existing, copyError);
  if (copyError) {
    std::cerr << "hsc: cannot write static library '" << archivePath
              << "': " << copyError.message() << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  metrics.markSkipped(metrics.llvmIrWrite());
  metrics.markSkipped(metrics.clangBackendLink());
  return EXIT_SUCCESS;
}

int compileExecutable(
    const std::vector<std::string> &inputPaths,
    const std::optional<std::string> &outputPath,
    hitsimple::codegen::CodegenOptions codegenOptions, bool cCompatibilityMode,
    hitsimple::stdlib::BuiltinProviderSelection providerSelection,
    const std::optional<std::filesystem::path> &clangOverride,
    const NativeBackendOptions &backendOptions,
    hitsimple::support::CompilationMetrics &metrics) {
  std::vector<CompiledTranslationUnit> units;
  units.reserve(inputPaths.size());
  std::size_t mainCount = 0;
  for (const auto &inputPath : inputPaths) {
    auto compiled =
        compileTranslationUnit(inputPath, codegenOptions, cCompatibilityMode,
                               providerSelection, false, metrics);
    if (!compiled) {
      return EXIT_FAILURE;
    }
    mainCount += compiled->mainDefinitionCount;
    units.push_back(std::move(*compiled));
  }

  if (mainCount == 0) {
    printDiagnostic(fileLevelDiagnostic(hitsimple::diagnostic::Stage::Sema,
                                        "program must define a main function",
                                        inputPaths.front()));
    metrics.fail("sema_hir");
    return EXIT_FAILURE;
  }
  if (mainCount > 1) {
    printDiagnostic(fileLevelDiagnostic(
        hitsimple::diagnostic::Stage::Sema,
        "program must define only one main function", inputPaths.front()));
    metrics.fail("sema_hir");
    return EXIT_FAILURE;
  }
  if (!validateCCompatibilityExternalAbi(units)) {
    metrics.fail("sema_hir");
    return EXIT_FAILURE;
  }
  const auto sourceModuleIds = collectRequiredSourceModules(units);
  auto sourceModules =
      compileSourceModules(sourceModuleIds, codegenOptions, metrics);
  if (!sourceModules) {
    return EXIT_FAILURE;
  }
  units.insert(units.end(), std::make_move_iterator(sourceModules->begin()),
               std::make_move_iterator(sourceModules->end()));

#ifdef _WIN32
  const std::string executablePath = outputPath.value_or("a.exe");
#else
  const std::string executablePath = outputPath.value_or("a.out");
#endif
  const auto writeStarted = metrics.now();
  if (!validateOutputParent(executablePath)) {
    metrics.fail(metrics.llvmIrWrite(), writeStarted);
    metrics.fail("llvm_ir_write");
    return EXIT_FAILURE;
  }
  std::string sanitizerRuntimeError;
  const auto sanitizedRuntimeSourcePaths =
      sanitizerRuntimeSources(backendOptions, sanitizerRuntimeError);
  if (!sanitizedRuntimeSourcePaths) {
    std::cerr << "hsc: " << sanitizerRuntimeError << '\n';
    metrics.fail(metrics.llvmIrWrite(), writeStarted);
    metrics.fail("llvm_ir_write");
    return EXIT_FAILURE;
  }
  const bool emitWindowsPdb =
      codegenOptions.emitDebugInfo && usesWindowsCodeView(codegenOptions);
  std::optional<std::filesystem::path> pdbPath;
  if (emitWindowsPdb) {
    pdbPath = pdbPathForExecutable(executablePath);
    std::error_code removeError;
    std::filesystem::remove(*pdbPath, removeError);
    if (removeError) {
      std::cerr << "hsc: cannot remove stale PDB '"
                << hitsimple::support::pathToUtf8(*pdbPath)
                << "': " << removeError.message() << '\n';
      metrics.fail("clang_backend_link");
      return EXIT_FAILURE;
    }
  }
  const auto temporaryPath =
      std::filesystem::temp_directory_path() /
      ("hitsimple-link-" +
       std::to_string(hitsimple::support::currentProcessId()));
  std::error_code directoryError;
  std::filesystem::remove_all(temporaryPath, directoryError);
  directoryError.clear();
  if (!std::filesystem::create_directories(temporaryPath, directoryError)) {
    std::cerr << "hsc: cannot create link temporary directory '"
              << hitsimple::support::pathToUtf8(temporaryPath)
              << "': " << directoryError.message() << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  TemporaryDirectory temporaryDirectory(temporaryPath);
  std::vector<std::filesystem::path> objectPaths;
  objectPaths.reserve(units.size());
  for (std::size_t index = 0; index < units.size(); ++index) {
    const auto objectPath =
        temporaryPath / ("hitsimple-" + std::to_string(index) + ".o");
    metrics.translationUnits().at(units[index].metricsIndex).llvmIrBytes =
        serializeLlvmModule(*units[index].emission.module).size();
    if (!emitOptimizedObject(*units[index].emission.module, objectPath,
                             backendOptions)) {
      metrics.fail("llvm_emission");
      return EXIT_FAILURE;
    }
    objectPaths.push_back(objectPath);
  }
  metrics.markSkipped(metrics.llvmIrWrite());

  const auto clang = hitsimple::support::resolveClang(clangOverride);
  if (!clang.path) {
    return reportMissingLinkerDriver(clang, metrics);
  }

  const auto linkStarted = metrics.now();
  std::vector<std::string> arguments;
  arguments.reserve(objectPaths.size() + sanitizedRuntimeSourcePaths->size());
  for (const auto &objectPath : objectPaths) {
    arguments.push_back(hitsimple::support::pathToUtf8(objectPath));
  }
  if (!sanitizedRuntimeSourcePaths->empty()) {
    for (std::size_t index = 0; index < sanitizedRuntimeSourcePaths->size();
         ++index) {
      const auto &runtimeSource = (*sanitizedRuntimeSourcePaths)[index];
      const auto runtimeObject =
          temporaryPath / ("hitsimple-sanitizer-runtime-" +
                           std::to_string(index) + ".o");
      const bool runtimeIsCxx = isCxxSanitizerRuntimeSource(runtimeSource);
      std::vector<std::string> runtimeArguments{"-c"};
      if (runtimeIsCxx) {
        runtimeArguments.insert(runtimeArguments.end(), {"-x", "c++"});
        appendCxxSanitizerRuntimeArguments(runtimeArguments);
      }
      appendSanitizerRuntimePlatformArguments(runtimeArguments);
      runtimeArguments.push_back(hitsimple::support::pathToUtf8(runtimeSource));
      runtimeArguments.insert(runtimeArguments.end(),
                              {"-o", hitsimple::support::pathToUtf8(runtimeObject)});
      appendClangCodegenArguments(runtimeArguments, backendOptions);
      if (codegenOptions.emitDebugInfo) {
        runtimeArguments.push_back("-g");
      }
      const auto runtimeProcess =
          hitsimple::support::runProcess(*clang.path, runtimeArguments);
      if (!runtimeProcess.launched) {
        std::cerr << "hsc: cannot start sanitizer runtime compiler '"
                  << hitsimple::support::pathToUtf8(*clang.path)
                  << "': " << runtimeProcess.error << '\n';
        metrics.fail(metrics.clangBackendLink(), linkStarted);
        metrics.fail("clang_backend_link");
        return EXIT_FAILURE;
      }
      if (runtimeProcess.exitCode != 0) {
        std::cerr << "hsc: sanitizer runtime compiler failed for '"
                  << hitsimple::support::pathToUtf8(runtimeSource)
                  << "' (exit code " << runtimeProcess.exitCode << ")\n";
        metrics.fail(metrics.clangBackendLink(), linkStarted);
        metrics.fail("clang_backend_link");
        return EXIT_FAILURE;
      }
      arguments.push_back(hitsimple::support::pathToUtf8(runtimeObject));
    }
  } else if (const auto runtimeSource =
                 hitsimple::support::pathEnvironmentVariable(
                     "HITSIMPLE_RUNTIME_SOURCE")) {
    arguments.push_back(hitsimple::support::pathToUtf8(*runtimeSource));
  } else {
    arguments.insert(arguments.end(),
                     {"-x", "none",
                      hitsimple::support::pathToUtf8(
                          hitsimple::support::runtimeLibraryPath())});
  }
  appendClangLinkArguments(arguments, backendOptions);
#ifdef _WIN32
  arguments.push_back("-static-libgcc");
  arguments.push_back("-static-libstdc++");
#endif
#if defined(__APPLE__)
  arguments.push_back("-lc++");
#endif
  if (codegenOptions.emitDebugInfo) {
    arguments.push_back("-g");
  }
  if (emitWindowsPdb) {
    arguments.push_back("-gcodeview");
    arguments.push_back("-fuse-ld=lld");
    arguments.push_back("-Xlinker");
    arguments.push_back("--pdb=" + hitsimple::support::pathToUtf8(*pdbPath));
  }
  arguments.insert(arguments.end(), {"-lm", "-o", executablePath});
  const auto process = hitsimple::support::runProcess(*clang.path, arguments);
  if (!process.launched) {
    std::cerr << "hsc: cannot start Clang '"
              << hitsimple::support::pathToUtf8(*clang.path)
              << "': " << process.error << '\n';
    metrics.fail(metrics.clangBackendLink(), linkStarted);
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
  if (process.exitCode != 0) {
    std::cerr << "hsc: Clang failed while linking executable (exit code "
              << process.exitCode << ")\n";
    metrics.fail(metrics.clangBackendLink(), linkStarted);
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
#if defined(__APPLE__)
  if (codegenOptions.emitDebugInfo) {
    std::string dsymError;
    if (!emitMacDsym(executablePath, dsymError)) {
      std::cerr << "hsc: " << dsymError << '\n';
      metrics.fail(metrics.clangBackendLink(), linkStarted);
      metrics.fail("clang_backend_link");
      return EXIT_FAILURE;
    }
  }
#endif
  if (pdbPath) {
    std::error_code pdbError;
    if (!std::filesystem::is_regular_file(*pdbPath, pdbError)) {
      std::cerr << "hsc: debug build did not generate PDB '"
                << hitsimple::support::pathToUtf8(*pdbPath) << "'";
      if (pdbError) {
        std::cerr << ": " << pdbError.message();
      }
      std::cerr << '\n';
      metrics.fail(metrics.clangBackendLink(), linkStarted);
      metrics.fail("clang_backend_link");
      return EXIT_FAILURE;
    }
  }
  metrics.complete(metrics.clangBackendLink(), linkStarted);

  return EXIT_SUCCESS;
}

int compileMixedExecutable(
    const std::vector<std::string> &inputPaths,
    const std::optional<std::string> &outputPath,
    hitsimple::codegen::CodegenOptions codegenOptions, bool cCompatibilityMode,
    hitsimple::stdlib::BuiltinProviderSelection providerSelection,
    const std::optional<std::filesystem::path> &clangOverride,
    const std::optional<std::filesystem::path> &clangxxOverride,
    const ExternalBuildInputs &externalInputs,
    const NativeBackendOptions &backendOptions,
    hitsimple::support::CompilationMetrics &metrics) {
  const bool linkOnlyExternalInputs =
      externalInputs.cSources.empty() && externalInputs.cxxSources.empty() &&
      (!externalInputs.linkInputs.empty() || !externalInputs.libraries.empty());
  if (linkOnlyExternalInputs && !externalInputs.linkerLanguage) {
    std::cerr << "hsc: archive or object link inputs require "
                 "--linker-language=c|cxx\n";
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  if (externalInputs.entryMode == EntryMode::Native &&
      externalInputs.cSources.empty() && externalInputs.cxxSources.empty()) {
    std::cerr << "hsc: --entry=native requires a --c-source or --cxx-source "
                 "that defines main\n";
    metrics.fail("cli");
    return EXIT_FAILURE;
  }

  const bool useCxxLinker =
      externalInputs.linkerLanguage
          ? *externalInputs.linkerLanguage == LinkerLanguage::Cxx
          : !externalInputs.cxxSources.empty();
  const auto temporaryPath =
      std::filesystem::temp_directory_path() /
      ("hitsimple-link-" +
       std::to_string(hitsimple::support::currentProcessId()));
  std::error_code directoryError;
  std::filesystem::remove_all(temporaryPath, directoryError);
  directoryError.clear();
  if (!std::filesystem::create_directories(temporaryPath, directoryError)) {
    std::cerr << "hsc: cannot create link temporary directory '"
              << hitsimple::support::pathToUtf8(temporaryPath)
              << "': " << directoryError.message() << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  TemporaryDirectory temporaryDirectory(temporaryPath);

  std::vector<CompiledObjectTranslationUnit> units;
  units.reserve(inputPaths.size());
  std::vector<std::filesystem::path> hitSimpleObjects;
  hitSimpleObjects.reserve(inputPaths.size());
  std::vector<std::string> sourceModuleIds;
  std::unordered_set<std::string> seenSourceModules;
  std::size_t mainCount = 0;
  for (std::size_t index = 0; index < inputPaths.size(); ++index) {
    const auto objectPath =
        temporaryPath / ("hitsimple-" + std::to_string(index) + ".o");
    auto compiled = compileObjectTranslationUnit(
        inputPaths[index], objectPath, codegenOptions, cCompatibilityMode,
        providerSelection, false, backendOptions, metrics);
    if (!compiled) {
      return EXIT_FAILURE;
    }
    mainCount += compiled->mainDefinitionCount;
    units.push_back(std::move(*compiled));
    for (const auto &module : units.back().sourceModules) {
      if (seenSourceModules.insert(module).second) {
        sourceModuleIds.push_back(module);
      }
    }
    hitSimpleObjects.push_back(objectPath);
  }
  if (externalInputs.entryMode == EntryMode::HitSimple && mainCount == 0) {
    printDiagnostic(fileLevelDiagnostic(hitsimple::diagnostic::Stage::Sema,
                                        "program must define a main function",
                                        inputPaths.front()));
    metrics.fail("sema_hir");
    return EXIT_FAILURE;
  }
  if (externalInputs.entryMode == EntryMode::HitSimple && mainCount > 1) {
    printDiagnostic(fileLevelDiagnostic(
        hitsimple::diagnostic::Stage::Sema,
        "program must define only one main function", inputPaths.front()));
    metrics.fail("sema_hir");
    return EXIT_FAILURE;
  }
  if (externalInputs.entryMode == EntryMode::Native && mainCount != 0) {
    printDiagnostic(
        fileLevelDiagnostic(hitsimple::diagnostic::Stage::Sema,
                            "--entry=native forbids a HitSimple main function",
                            inputPaths.front()));
    metrics.fail("sema_hir");
    return EXIT_FAILURE;
  }
  std::vector<CompiledTranslationUnit> compatibilityUnits;
  compatibilityUnits.reserve(units.size());
  for (const auto &unit : units) {
    compatibilityUnits.push_back({inputPaths[compatibilityUnits.size()],
                                  {},
                                  unit.mainDefinitionCount,
                                  unit.compatibilityLinkage,
                                  {},
                                  0});
  }
  if (!validateCCompatibilityExternalAbi(compatibilityUnits)) {
    metrics.fail("sema_hir");
    return EXIT_FAILURE;
  }
  auto sourceModules =
      compileSourceModules(sourceModuleIds, codegenOptions, metrics);
  if (!sourceModules) {
    return EXIT_FAILURE;
  }
  for (std::size_t index = 0; index < sourceModules->size(); ++index) {
    const auto objectPath =
        temporaryPath / ("hitsimple-stdlib-" + std::to_string(index) + ".o");
    metrics.translationUnits()
        .at((*sourceModules)[index].metricsIndex)
        .llvmIrBytes =
        serializeLlvmModule(*(*sourceModules)[index].emission.module).size();
    if (!emitOptimizedObject(*(*sourceModules)[index].emission.module,
                             objectPath, backendOptions)) {
      metrics.fail("llvm_emission");
      return EXIT_FAILURE;
    }
    hitSimpleObjects.push_back(objectPath);
  }

#ifdef _WIN32
  const std::string executablePath = outputPath.value_or("a.exe");
#else
  const std::string executablePath = outputPath.value_or("a.out");
#endif
  const auto writeStarted = metrics.now();
  if (!validateOutputParent(executablePath)) {
    metrics.fail(metrics.llvmIrWrite(), writeStarted);
    metrics.fail("llvm_ir_write");
    return EXIT_FAILURE;
  }
  metrics.markSkipped(metrics.llvmIrWrite());

  std::string sanitizerRuntimeError;
  const auto sanitizedRuntimeSourcePaths =
      sanitizerRuntimeSources(backendOptions, sanitizerRuntimeError);
  if (!sanitizedRuntimeSourcePaths) {
    std::cerr << "hsc: " << sanitizerRuntimeError << '\n';
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
  const auto runtimeSource =
      hitsimple::support::pathEnvironmentVariable("HITSIMPLE_RUNTIME_SOURCE");
  const bool needsCCompiler =
      !externalInputs.cSources.empty() || !useCxxLinker ||
      !sanitizedRuntimeSourcePaths->empty() || runtimeSource.has_value();
  const bool needsCxxCompiler =
      !externalInputs.cxxSources.empty() || useCxxLinker;
  const auto clang = needsCCompiler
                         ? hitsimple::support::resolveClang(clangOverride)
                         : hitsimple::support::ClangSelection{};
  const auto clangxx =
      needsCxxCompiler
          ? hitsimple::support::resolveClangxx(clangxxOverride)
          : hitsimple::support::ClangSelection{};
  if (needsCCompiler && !clang.path) {
    return reportMissingLinkerDriver(clang, metrics);
  }
  if (needsCxxCompiler && !clangxx.path) {
    return reportMissingLinkerDriver(clangxx, metrics);
  }

  std::vector<std::filesystem::path> externalObjects;
  externalObjects.reserve(externalInputs.cSources.size() +
                          externalInputs.cxxSources.size() +
                          sanitizedRuntimeSourcePaths->size() + 1U);
  const auto compileExternalSource =
      [&](const std::string &source, std::string_view label,
          const hitsimple::support::ClangSelection &compiler,
          std::size_t index, bool requiresCxx20 = false,
          bool isSanitizerRuntime = false) -> bool {
    const auto objectPath = temporaryPath / (std::string(label) + "-" +
                                             std::to_string(index) + ".o");
    std::vector<std::string> arguments{
        "-c", source, "-o", hitsimple::support::pathToUtf8(objectPath)};
    if (requiresCxx20) {
      arguments.insert(arguments.end(), {"-x", "c++"});
      appendCxxSanitizerRuntimeArguments(arguments);
    }
    if (isSanitizerRuntime) {
      appendSanitizerRuntimePlatformArguments(arguments);
    }
    appendClangCodegenArguments(arguments, backendOptions);
    if (codegenOptions.emitDebugInfo) {
      arguments.push_back("-g");
    }
    const auto process =
        hitsimple::support::runProcess(*compiler.path, arguments);
    if (!process.launched) {
      std::cerr << "hsc: cannot start " << label << " compiler '"
                << hitsimple::support::pathToUtf8(*compiler.path)
                << "': " << process.error << '\n';
      return false;
    }
    if (process.exitCode != 0) {
      std::cerr << "hsc: " << label << " compiler failed for '" << source
                << "' (exit code " << process.exitCode << ")\n";
      return false;
    }
    externalObjects.push_back(objectPath);
    return true;
  };
  for (std::size_t index = 0; index < externalInputs.cSources.size(); ++index) {
    if (!compileExternalSource(externalInputs.cSources[index], "C", clang,
                               index)) {
      metrics.fail("clang_backend_link");
      return EXIT_FAILURE;
    }
  }
  for (std::size_t index = 0; index < externalInputs.cxxSources.size();
       ++index) {
    if (!compileExternalSource(externalInputs.cxxSources[index], "C++", clangxx,
                               index)) {
      metrics.fail("clang_backend_link");
      return EXIT_FAILURE;
    }
  }
  std::vector<std::filesystem::path> runtimeObjects;
  if (!sanitizedRuntimeSourcePaths->empty()) {
    for (std::size_t index = 0; index < sanitizedRuntimeSourcePaths->size();
         ++index) {
      const auto &runtimeSource = (*sanitizedRuntimeSourcePaths)[index];
      const bool runtimeIsCxx = isCxxSanitizerRuntimeSource(runtimeSource);
      if (!compileExternalSource(
              hitsimple::support::pathToUtf8(runtimeSource),
              runtimeIsCxx ? "runtime C++" : "runtime C", clang, index,
              runtimeIsCxx, true)) {
        metrics.fail("clang_backend_link");
        return EXIT_FAILURE;
      }
      runtimeObjects.push_back(externalObjects.back());
      externalObjects.pop_back();
    }
  } else if (runtimeSource) {
    if (!compileExternalSource(hitsimple::support::pathToUtf8(*runtimeSource),
                               "runtime C", clang, 0U)) {
      metrics.fail("clang_backend_link");
      return EXIT_FAILURE;
    }
    runtimeObjects.push_back(externalObjects.back());
    externalObjects.pop_back();
  }

  const bool emitWindowsPdb =
      codegenOptions.emitDebugInfo && usesWindowsCodeView(codegenOptions);
  std::optional<std::filesystem::path> pdbPath;
  if (emitWindowsPdb) {
    pdbPath = pdbPathForExecutable(executablePath);
    std::error_code removeError;
    std::filesystem::remove(*pdbPath, removeError);
    if (removeError) {
      std::cerr << "hsc: cannot remove stale PDB '"
                << hitsimple::support::pathToUtf8(*pdbPath)
                << "': " << removeError.message() << '\n';
      metrics.fail("clang_backend_link");
      return EXIT_FAILURE;
    }
  }

  std::vector<std::string> linkArguments;
  for (const auto &objectPath : hitSimpleObjects) {
    linkArguments.push_back(hitsimple::support::pathToUtf8(objectPath));
  }
  for (const auto &objectPath : externalObjects) {
    linkArguments.push_back(hitsimple::support::pathToUtf8(objectPath));
  }
  linkArguments.insert(linkArguments.end(), externalInputs.linkInputs.begin(),
                       externalInputs.linkInputs.end());
  for (const auto &directory : externalInputs.libraryDirectories) {
    linkArguments.insert(linkArguments.end(), {"-L", directory});
  }
  for (const auto &library : externalInputs.libraries) {
    linkArguments.insert(linkArguments.end(), {"-l", library});
  }
  linkArguments.insert(linkArguments.end(),
                       externalInputs.linkArguments.begin(),
                       externalInputs.linkArguments.end());
  if (!runtimeObjects.empty()) {
    for (const auto &runtimeObject : runtimeObjects) {
      linkArguments.push_back(hitsimple::support::pathToUtf8(runtimeObject));
    }
  } else {
    linkArguments.push_back(hitsimple::support::pathToUtf8(
        hitsimple::support::runtimeLibraryPath()));
  }
  appendClangLinkArguments(linkArguments, backendOptions);
#ifdef _WIN32
  linkArguments.push_back("--target=x86_64-w64-windows-gnu");
  linkArguments.push_back("-static-libgcc");
  linkArguments.push_back("-static-libstdc++");
#endif
#if defined(__APPLE__)
  linkArguments.push_back("-lc++");
#endif
  if (codegenOptions.emitDebugInfo) {
    linkArguments.push_back("-g");
  }
  if (emitWindowsPdb) {
    linkArguments.push_back("-gcodeview");
    linkArguments.push_back("-fuse-ld=lld");
    linkArguments.push_back("-Xlinker");
    linkArguments.push_back("--pdb=" +
                            hitsimple::support::pathToUtf8(*pdbPath));
  }
  linkArguments.insert(linkArguments.end(), {"-lm", "-o", executablePath});
  const auto linkStarted = metrics.now();
  const auto &linker = useCxxLinker ? clangxx : clang;
  const auto linkResult =
      hitsimple::support::runProcess(*linker.path, linkArguments);
  if (!linkResult.launched) {
    std::cerr << "hsc: cannot start linker '"
              << hitsimple::support::pathToUtf8(*linker.path)
              << "': " << linkResult.error << '\n';
    metrics.fail(metrics.clangBackendLink(), linkStarted);
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
  if (linkResult.exitCode != 0) {
    std::cerr << "hsc: linker failed while linking executable (exit code "
              << linkResult.exitCode << ")\n";
    metrics.fail(metrics.clangBackendLink(), linkStarted);
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
#if defined(__APPLE__)
  if (codegenOptions.emitDebugInfo) {
    std::string dsymError;
    if (!emitMacDsym(executablePath, dsymError)) {
      std::cerr << "hsc: " << dsymError << '\n';
      metrics.fail(metrics.clangBackendLink(), linkStarted);
      metrics.fail("clang_backend_link");
      return EXIT_FAILURE;
    }
  }
#endif
  if (pdbPath) {
    std::error_code pdbError;
    if (!std::filesystem::is_regular_file(*pdbPath, pdbError)) {
      std::cerr << "hsc: debug build did not generate PDB '"
                << hitsimple::support::pathToUtf8(*pdbPath) << "'";
      if (pdbError) {
        std::cerr << ": " << pdbError.message();
      }
      std::cerr << '\n';
      metrics.fail(metrics.clangBackendLink(), linkStarted);
      metrics.fail("clang_backend_link");
      return EXIT_FAILURE;
    }
  }
  metrics.complete(metrics.clangBackendLink(), linkStarted);
  return EXIT_SUCCESS;
}

} // namespace hitsimple::driver
