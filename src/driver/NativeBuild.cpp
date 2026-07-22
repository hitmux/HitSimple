#include "driver/NativeBuild.h"

#include "driver/Diagnostics.h"
#include "driver/FrontendPipeline.h"
#include "hitsimple/codegen/OptimizationPipeline.h"
#include "hitsimple/codegen/TargetCapabilities.h"
#include "hitsimple/support/Cargo.h"
#include "hitsimple/support/CompilationMetrics.h"
#include "hitsimple/support/Path.h"
#include "hitsimple/support/Process.h"
#include "hitsimple/support/ResourcePaths.h"
#include "hitsimple/support/Toolchain.h"

#include <llvm/Config/llvm-config.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace hitsimple::driver {

namespace {

void appendSanitizerArguments(
    std::vector<std::string>& arguments,
    const NativeBackendOptions& backendOptions) {
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

std::optional<std::vector<std::filesystem::path>> sanitizerRuntimeSources(
    const NativeBackendOptions& backendOptions, std::string& error) {
  if (backendOptions.sanitizer == Sanitizer::None) {
    return std::vector<std::filesystem::path>{};
  }
#if !defined(__linux__)
  error = "--sanitize is currently supported only on Linux hosts";
  return std::nullopt;
#else
  const auto runtimeSource = hitsimple::support::runtimeSourcePath();
  const std::vector<std::filesystem::path> sources{
      runtimeSource, runtimeSource.parent_path() / "hitsimple_f128_native.c"};
  for (const auto& source : sources) {
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

void appendClangCodegenArguments(
    std::vector<std::string>& arguments,
    const NativeBackendOptions& backendOptions) {
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

void appendHitSimpleIrCodegenArguments(
    std::vector<std::string>& arguments,
    const NativeBackendOptions& backendOptions) {
#if defined(__APPLE__)
  // Clang normalizes LLVM's native Darwin host triple to its macOS deployment
  // triple for the same ABI. Suppress only that expected driver diagnostic.
  arguments.emplace_back("-Wno-override-module");
#endif
  // The embedded New Pass Manager already applied the requested optimization
  // pipeline. Keep Clang in backend-only mode so it cannot silently select a
  // second, potentially different IR optimization pipeline.
  arguments.emplace_back("-O0");
  appendSanitizerArguments(arguments, backendOptions);
  if (backendOptions.pgoMode == PgoMode::Instrument) {
    arguments.push_back(
        "-fprofile-instr-generate=" +
        hitsimple::support::pathToUtf8(backendOptions.profilePath));
  }
}

void appendClangLinkArguments(
    std::vector<std::string>& arguments,
    const NativeBackendOptions& backendOptions) {
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
    const hitsimple::codegen::CodegenOptions& codegenOptions) {
  const auto triple = codegenOptions.targetTriple.empty()
      ? targetTriple()
      : codegenOptions.targetTriple;
  return llvm::Triple(triple).isOSWindows();
}

std::filesystem::path pdbPathForExecutable(
    const std::string& executablePath) {
  auto pdbPath = hitsimple::support::pathFromUtf8(executablePath);
  pdbPath.replace_extension(".pdb");
  return pdbPath;
}

std::optional<unsigned> backendClangMajorVersion(
    const hitsimple::support::ClangSelection& clang, std::string& error) {
  if (!clang.path) {
    error = clang.error;
    return std::nullopt;
  }
  std::error_code filesystemError;
  const auto outputPath = std::filesystem::temp_directory_path(filesystemError) /
      ("hitsimple-clang-version-" +
       std::to_string(hitsimple::support::currentProcessId()) + ".txt");
  if (filesystemError) {
    error = "cannot determine a temporary path for Clang version detection: " +
            filesystemError.message();
    return std::nullopt;
  }
  const auto process =
      hitsimple::support::runProcess(*clang.path, {"--version"}, outputPath);
  const auto output = readFile(hitsimple::support::pathToUtf8(outputPath));
  std::filesystem::remove(outputPath, filesystemError);
  if (!process.launched) {
    error = "cannot start backend Clang '" +
            hitsimple::support::pathToUtf8(*clang.path) + "': " +
            process.error;
    return std::nullopt;
  }
  if (process.exitCode != 0 || !output) {
    error = "cannot read backend Clang version from '" +
            hitsimple::support::pathToUtf8(*clang.path) + "'";
    return std::nullopt;
  }
  const auto marker = output->find("clang version ");
  if (marker == std::string::npos) {
    error = "backend compiler does not report a Clang version: '" +
            hitsimple::support::pathToUtf8(*clang.path) + "'";
    return std::nullopt;
  }
  auto cursor = marker + std::string_view("clang version ").size();
  const auto firstDigit = cursor;
  while (cursor < output->size() &&
         std::isdigit(static_cast<unsigned char>((*output)[cursor]))) {
    ++cursor;
  }
  if (cursor == firstDigit) {
    error = "backend Clang has an invalid version string: '" +
            hitsimple::support::pathToUtf8(*clang.path) + "'";
    return std::nullopt;
  }
  try {
    return static_cast<unsigned>(std::stoul(output->substr(
        firstDigit, cursor - firstDigit)));
  } catch (const std::exception&) {
    error = "backend Clang has an unsupported version string: '" +
            hitsimple::support::pathToUtf8(*clang.path) + "'";
    return std::nullopt;
  }
}

bool validateBackendClangCompatibility(
    const hitsimple::support::ClangSelection& clang) {
  std::string error;
  const auto major = backendClangMajorVersion(clang, error);
  if (!major) {
    std::cerr << "hsc: " << error << '\n';
    return false;
  }
  if (*major != LLVM_VERSION_MAJOR) {
    std::cerr << "hsc: embedded LLVM major version " << LLVM_VERSION_MAJOR
              << " is incompatible with backend Clang major version " << *major
              << "; select a matching --clang executable\n";
    return false;
  }
  return true;
}

class TemporaryDirectory final {
public:
  explicit TemporaryDirectory(std::filesystem::path path) : path_(std::move(path)) {}

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path& path() const { return path_; }

private:
  std::filesystem::path path_;
};

bool writeOptimizedLlvmIr(std::string_view llvmIr,
                          const std::filesystem::path& llvmIrPath,
                          const NativeBackendOptions& backendOptions) {
  hitsimple::codegen::OptimizationPipelineOptions pipelineOptions;
  pipelineOptions.optimization = backendOptions.optimization;
  pipelineOptions.pgoMode = backendOptions.pgoMode;
  if (backendOptions.sanitizer == Sanitizer::Address) {
    pipelineOptions.sanitizer =
        hitsimple::codegen::SanitizerInstrumentation::Address;
  }
  pipelineOptions.profilePath =
      hitsimple::support::pathToUtf8(backendOptions.profilePath);
  pipelineOptions.emitRemarks = backendOptions.optimizationRemarksPath.has_value();

  std::string pipelineError;
  const auto optimized = hitsimple::codegen::runOptimizationPipeline(
      llvmIr, pipelineOptions, pipelineError);
  if (!optimized) {
    std::cerr << "hsc: optimization pipeline failed: " << pipelineError << '\n';
    return false;
  }
  if (backendOptions.optimizationRemarksPath &&
      !appendOptimizationRemarks(*backendOptions.optimizationRemarksPath,
                                 optimized->remarks)) {
    return false;
  }

  const auto llvmIrPathText = hitsimple::support::pathToUtf8(llvmIrPath);
  if (!writeFile(llvmIrPathText, optimized->llvmIr)) {
    std::cerr << "hsc: cannot write temporary LLVM IR '" << llvmIrPathText
              << "'\n";
    return false;
  }
  return true;
}

bool emitObjectWithClang(
    std::string_view llvmIr, const std::filesystem::path& llvmIrPath,
    const std::filesystem::path& objectPath,
    const hitsimple::support::ClangSelection& clang,
    const NativeBackendOptions& backendOptions) {
  if (!validateBackendClangCompatibility(clang) ||
      !writeOptimizedLlvmIr(llvmIr, llvmIrPath, backendOptions)) {
    return false;
  }
  std::vector<std::string> arguments{
      "-x", "ir", hitsimple::support::pathToUtf8(llvmIrPath), "-c", "-o",
      hitsimple::support::pathToUtf8(objectPath)};
  appendHitSimpleIrCodegenArguments(arguments, backendOptions);
  const auto process = hitsimple::support::runProcess(*clang.path, arguments);
  if (!process.launched) {
    std::cerr << "hsc: cannot start Clang '"
              << hitsimple::support::pathToUtf8(*clang.path)
              << "': " << process.error << '\n';
    return false;
  }
  if (process.exitCode != 0) {
    std::cerr << "hsc: Clang failed while compiling LLVM IR to object (exit code "
              << process.exitCode << ")\n";
    return false;
  }
  return true;
}

std::string defaultObjectOutputPath(const std::string& inputPath) {
  auto output = hitsimple::support::pathFromUtf8(inputPath);
  output.replace_extension(".o");
  return hitsimple::support::pathToUtf8(output);
}

int compileObject(const std::vector<std::string>& inputPaths,
                  const std::optional<std::string>& outputPath,
                  hitsimple::codegen::CodegenOptions codegenOptions,
                  bool cCompatibilityMode,
                  hitsimple::stdlib::BuiltinProviderSelection providerSelection,
                  const hitsimple::support::ClangSelection& clang,
                  const NativeBackendOptions& backendOptions,
                  hitsimple::support::CompilationMetrics& metrics) {
  if (inputPaths.size() != 1U) {
    std::cerr << "hsc: --crate-type=object supports exactly one input file\n";
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  if (!clang.path) {
    std::cerr << "hsc: " << clang.error << '\n';
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
  const auto objectPath = outputPath.value_or(defaultObjectOutputPath(
      inputPaths.front()));
  if (!validateOutputParent(objectPath)) {
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  const auto temporaryPath = std::filesystem::temp_directory_path() /
      ("hitsimple-object-" +
       std::to_string(hitsimple::support::currentProcessId()));
  std::error_code directoryError;
  std::filesystem::remove_all(temporaryPath, directoryError);
  directoryError.clear();
  if (!std::filesystem::create_directories(temporaryPath, directoryError)) {
    std::cerr << "hsc: cannot create object temporary directory '"
              << hitsimple::support::pathToUtf8(temporaryPath) << "': "
              << directoryError.message() << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  TemporaryDirectory temporaryDirectory(temporaryPath);
  const auto writeStarted = metrics.now();
  if (!compileObjectTranslationUnit(
          inputPaths.front(), hitsimple::support::pathFromUtf8(objectPath),
          temporaryPath / "unit.ll", codegenOptions, cCompatibilityMode,
          providerSelection, true, clang, backendOptions, metrics)) {
    return EXIT_FAILURE;
  }
  metrics.complete(metrics.llvmIrWrite(), writeStarted);
  metrics.markSkipped(metrics.clangBackendLink());
  return EXIT_SUCCESS;
}

int compileStaticLibrary(
    const std::vector<std::string>& inputPaths,
    const std::optional<std::string>& outputPath,
    hitsimple::codegen::CodegenOptions codegenOptions, bool cCompatibilityMode,
    hitsimple::stdlib::BuiltinProviderSelection providerSelection,
    const hitsimple::support::LlvmArSelection& llvmAr,
    const hitsimple::support::ClangSelection& clang,
    const NativeBackendOptions& backendOptions,
    hitsimple::support::CompilationMetrics& metrics) {
  if (!llvmAr.path) {
    std::cerr << "hsc: " << llvmAr.error << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  if (!clang.path) {
    std::cerr << "hsc: " << clang.error << '\n';
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
  const auto archivePath = outputPath.value_or("libhitsimple.a");
  if (!validateOutputParent(archivePath)) {
    metrics.fail("cli");
    return EXIT_FAILURE;
  }

  const auto temporaryPath = std::filesystem::temp_directory_path() /
      ("hitsimple-staticlib-" +
       std::to_string(hitsimple::support::currentProcessId()));
  std::error_code directoryError;
  std::filesystem::remove_all(temporaryPath, directoryError);
  directoryError.clear();
  if (!std::filesystem::create_directories(temporaryPath, directoryError)) {
    std::cerr << "hsc: cannot create static library temporary directory '"
              << hitsimple::support::pathToUtf8(temporaryPath) << "': "
              << directoryError.message() << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  TemporaryDirectory temporaryDirectory(temporaryPath);

  std::vector<std::filesystem::path> objectPaths;
  objectPaths.reserve(inputPaths.size());
  std::vector<std::string> sourceModuleIds;
  std::unordered_set<std::string> seenSourceModules;
  for (std::size_t index = 0; index < inputPaths.size(); ++index) {
    const auto objectPath = temporaryPath / ("unit-" +
        std::to_string(index) + ".o");
    auto compiled = compileObjectTranslationUnit(
        inputPaths[index], objectPath,
        temporaryPath / ("unit-" + std::to_string(index) + ".ll"),
        codegenOptions, cCompatibilityMode, providerSelection, false, clang,
        backendOptions, metrics);
    if (!compiled) {
      return EXIT_FAILURE;
    }
    for (const auto& module : compiled->sourceModules) {
      if (seenSourceModules.insert(module).second) {
        sourceModuleIds.push_back(module);
      }
    }
    objectPaths.push_back(objectPath);
  }
  auto sourceModules = compileSourceModules(sourceModuleIds, codegenOptions, metrics);
  if (!sourceModules) {
    return EXIT_FAILURE;
  }
  for (std::size_t index = 0; index < sourceModules->size(); ++index) {
    const auto objectPath = temporaryPath /
        ("stdlib-" + std::to_string(index) + ".o");
    if (!emitObjectWithClang(
            (*sourceModules)[index].llvmIr,
            temporaryPath / ("stdlib-" + std::to_string(index) + ".ll"),
            objectPath, clang, backendOptions)) {
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
    std::cerr << "hsc: cannot read current directory while creating static library: "
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
              << hitsimple::support::pathToUtf8(*llvmAr.path) << "': "
              << extractResult.error << '\n';
    metrics.fail("cli");
    return EXIT_FAILURE;
  }
  if (extractResult.exitCode != 0) {
    std::cerr << "hsc: llvm-ar failed while extracting the runtime archive "
              << "(exit code " << extractResult.exitCode << ")\n";
    metrics.fail("cli");
    return EXIT_FAILURE;
  }

  for (const auto& entry : std::filesystem::directory_iterator(temporaryPath)) {
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
  std::vector<std::string> archiveArguments{"rcs",
      hitsimple::support::pathToUtf8(temporaryArchive)};
  for (const auto& objectPath : objectPaths) {
    archiveArguments.push_back(hitsimple::support::pathToUtf8(objectPath));
  }
  const auto archiveResult = hitsimple::support::runProcess(*llvmAr.path,
                                                             archiveArguments);
  if (!archiveResult.launched) {
    std::cerr << "hsc: cannot start llvm-ar '"
              << hitsimple::support::pathToUtf8(*llvmAr.path) << "': "
              << archiveResult.error << '\n';
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
  std::filesystem::copy_file(temporaryArchive,
                             hitsimple::support::pathFromUtf8(archivePath),
                             std::filesystem::copy_options::overwrite_existing,
                             copyError);
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
int compileExecutable(const std::vector<std::string>& inputPaths,
                      const std::optional<std::string>& outputPath,
                      hitsimple::codegen::CodegenOptions codegenOptions,
                      bool cCompatibilityMode,
                      hitsimple::stdlib::BuiltinProviderSelection providerSelection,
                      const hitsimple::support::ClangSelection& clang,
                      const NativeBackendOptions& backendOptions,
                      hitsimple::support::CompilationMetrics& metrics) {
  if (!clang.path) {
    std::cerr << "hsc: " << clang.error << '\n';
    const auto linkStarted = metrics.now();
    metrics.fail(metrics.clangBackendLink(), linkStarted);
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
  std::vector<CompiledTranslationUnit> units;
  units.reserve(inputPaths.size());
  std::size_t mainCount = 0;
  for (const auto& inputPath : inputPaths) {
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
    printDiagnostic(fileLevelDiagnostic(
        hitsimple::diagnostic::Stage::Sema,
        "program must define a main function", inputPaths.front()));
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
  auto sourceModules = compileSourceModules(sourceModuleIds, codegenOptions,
                                            metrics);
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
  if (!validateBackendClangCompatibility(clang)) {
    metrics.fail("clang_backend_link");
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
  std::vector<std::filesystem::path> temporaryPaths;
  temporaryPaths.reserve(units.size());
  std::vector<std::filesystem::path> clangInputPaths;
  clangInputPaths.reserve(units.size());
  const auto tempDirectory = std::filesystem::temp_directory_path();
  const auto tempPrefix =
      "hitsimple-" +
      std::to_string(hitsimple::support::currentProcessId());
  for (std::size_t index = 0; index < units.size(); ++index) {
    const auto tempPath =
        tempDirectory / (tempPrefix + "-" + std::to_string(index) + ".ll");
    if (!writeOptimizedLlvmIr(units[index].llvmIr, tempPath, backendOptions)) {
      for (const auto& path : temporaryPaths) {
        std::filesystem::remove(path);
      }
      metrics.fail(metrics.llvmIrWrite(), writeStarted);
      metrics.fail("llvm_ir_write");
      return EXIT_FAILURE;
    }
    temporaryPaths.push_back(tempPath);
    clangInputPaths.push_back(tempPath);
  }
  metrics.complete(metrics.llvmIrWrite(), writeStarted);

  std::vector<std::string> arguments{"-x", "ir"};
  for (const auto& tempPath : clangInputPaths) {
    arguments.push_back(hitsimple::support::pathToUtf8(tempPath));
  }
  if (!sanitizedRuntimeSourcePaths->empty()) {
    for (const auto& runtimeSource : *sanitizedRuntimeSourcePaths) {
      arguments.insert(arguments.end(),
                       {"-x", "c",
                        hitsimple::support::pathToUtf8(runtimeSource)});
    }
  } else if (const auto runtimeSource =
                 hitsimple::support::pathEnvironmentVariable(
                     "HITSIMPLE_RUNTIME_SOURCE")) {
    arguments.insert(arguments.end(),
                     {"-x", "c",
                      hitsimple::support::pathToUtf8(*runtimeSource)});
  } else {
    arguments.insert(arguments.end(), {"-x", "none",
                                       hitsimple::support::pathToUtf8(
                                           hitsimple::support::runtimeLibraryPath())});
  }
  appendHitSimpleIrCodegenArguments(arguments, backendOptions);
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
  const auto linkStarted = metrics.now();
  const auto process = hitsimple::support::runProcess(*clang.path, arguments);
  for (const auto& tempPath : temporaryPaths) {
    std::filesystem::remove(tempPath);
  }
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
    const std::vector<std::string>& inputPaths,
    const std::optional<std::string>& outputPath,
    hitsimple::codegen::CodegenOptions codegenOptions, bool cCompatibilityMode,
    hitsimple::stdlib::BuiltinProviderSelection providerSelection,
    const std::optional<std::filesystem::path>& clangOverride,
    const std::optional<std::filesystem::path>& clangxxOverride,
    const ExternalBuildInputs& externalInputs,
    const NativeBackendOptions& backendOptions,
    hitsimple::support::CompilationMetrics& metrics) {
  const bool linkOnlyExternalInputs = externalInputs.cSources.empty() &&
      externalInputs.cxxSources.empty() &&
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

  const bool useCxxLinker = externalInputs.linkerLanguage
      ? *externalInputs.linkerLanguage == LinkerLanguage::Cxx
      : !externalInputs.cxxSources.empty();
  const bool needsCCompiler = true;
  const bool needsCxxCompiler = !externalInputs.cxxSources.empty() ||
      useCxxLinker;
  const auto clang = hitsimple::support::resolveClang(clangOverride);
  const auto clangxx = hitsimple::support::resolveClangxx(clangxxOverride);
  if (needsCCompiler && !clang.path) {
    std::cerr << "hsc: " << clang.error << '\n';
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
  if (needsCxxCompiler && !clangxx.path) {
    std::cerr << "hsc: " << clangxx.error << '\n';
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }
  if (!validateBackendClangCompatibility(clang) ||
      (needsCxxCompiler && !validateBackendClangCompatibility(clangxx))) {
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }

  const auto temporaryPath = std::filesystem::temp_directory_path() /
      ("hitsimple-link-" +
       std::to_string(hitsimple::support::currentProcessId()));
  std::error_code directoryError;
  std::filesystem::remove_all(temporaryPath, directoryError);
  directoryError.clear();
  if (!std::filesystem::create_directories(temporaryPath, directoryError)) {
    std::cerr << "hsc: cannot create link temporary directory '"
              << hitsimple::support::pathToUtf8(temporaryPath) << "': "
              << directoryError.message() << '\n';
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
    const auto objectPath = temporaryPath /
        ("hitsimple-" + std::to_string(index) + ".o");
    auto compiled = compileObjectTranslationUnit(
        inputPaths[index], objectPath,
        temporaryPath / ("hitsimple-" + std::to_string(index) + ".ll"),
        codegenOptions, cCompatibilityMode, providerSelection, false, clang,
        backendOptions, metrics);
    if (!compiled) {
      return EXIT_FAILURE;
    }
    mainCount += compiled->mainDefinitionCount;
    units.push_back(std::move(*compiled));
    for (const auto& module : units.back().sourceModules) {
      if (seenSourceModules.insert(module).second) {
        sourceModuleIds.push_back(module);
      }
    }
    hitSimpleObjects.push_back(objectPath);
  }
  if (externalInputs.entryMode == EntryMode::HitSimple && mainCount == 0) {
    printDiagnostic(fileLevelDiagnostic(
        hitsimple::diagnostic::Stage::Sema,
        "program must define a main function", inputPaths.front()));
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
    printDiagnostic(fileLevelDiagnostic(
        hitsimple::diagnostic::Stage::Sema,
        "--entry=native forbids a HitSimple main function", inputPaths.front()));
    metrics.fail("sema_hir");
    return EXIT_FAILURE;
  }
  std::vector<CompiledTranslationUnit> compatibilityUnits;
  compatibilityUnits.reserve(units.size());
  for (const auto& unit : units) {
    compatibilityUnits.push_back(
        {inputPaths[compatibilityUnits.size()], "", unit.mainDefinitionCount,
         unit.compatibilityLinkage, {}});
  }
  if (!validateCCompatibilityExternalAbi(compatibilityUnits)) {
    metrics.fail("sema_hir");
    return EXIT_FAILURE;
  }
  auto sourceModules = compileSourceModules(sourceModuleIds, codegenOptions,
                                            metrics);
  if (!sourceModules) {
    return EXIT_FAILURE;
  }
  for (std::size_t index = 0; index < sourceModules->size(); ++index) {
    const auto objectPath = temporaryPath /
        ("hitsimple-stdlib-" + std::to_string(index) + ".o");
    if (!emitObjectWithClang(
            (*sourceModules)[index].llvmIr,
            temporaryPath / ("hitsimple-stdlib-" + std::to_string(index) +
                             ".ll"),
            objectPath, clang, backendOptions)) {
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
  metrics.complete(metrics.llvmIrWrite(), writeStarted);

  std::string sanitizerRuntimeError;
  const auto sanitizedRuntimeSourcePaths =
      sanitizerRuntimeSources(backendOptions, sanitizerRuntimeError);
  if (!sanitizedRuntimeSourcePaths) {
    std::cerr << "hsc: " << sanitizerRuntimeError << '\n';
    metrics.fail("clang_backend_link");
    return EXIT_FAILURE;
  }

  std::vector<std::filesystem::path> externalObjects;
  externalObjects.reserve(externalInputs.cSources.size() +
                          externalInputs.cxxSources.size() +
                          sanitizedRuntimeSourcePaths->size() + 1U);
  const auto compileExternalSource = [&](const std::string& source,
                                         std::string_view label,
                                         const hitsimple::support::ClangSelection& compiler,
                                         std::size_t index) -> bool {
    const auto objectPath = temporaryPath /
        (std::string(label) + "-" + std::to_string(index) + ".o");
    std::vector<std::string> arguments{"-c", source, "-o",
                                        hitsimple::support::pathToUtf8(objectPath)};
    appendClangCodegenArguments(arguments, backendOptions);
    if (codegenOptions.emitDebugInfo) {
      arguments.push_back("-g");
    }
    const auto process = hitsimple::support::runProcess(*compiler.path, arguments);
    if (!process.launched) {
      std::cerr << "hsc: cannot start " << label << " compiler '"
                << hitsimple::support::pathToUtf8(*compiler.path) << "': "
                << process.error << '\n';
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
  for (std::size_t index = 0; index < externalInputs.cxxSources.size(); ++index) {
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
      if (!compileExternalSource(
              hitsimple::support::pathToUtf8(
                  (*sanitizedRuntimeSourcePaths)[index]),
              "runtime C", clang, index)) {
        metrics.fail("clang_backend_link");
        return EXIT_FAILURE;
      }
      runtimeObjects.push_back(externalObjects.back());
      externalObjects.pop_back();
    }
  } else if (const auto runtimeSource =
                 hitsimple::support::pathEnvironmentVariable(
                     "HITSIMPLE_RUNTIME_SOURCE")) {
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
  for (const auto& objectPath : hitSimpleObjects) {
    linkArguments.push_back(hitsimple::support::pathToUtf8(objectPath));
  }
  for (const auto& objectPath : externalObjects) {
    linkArguments.push_back(hitsimple::support::pathToUtf8(objectPath));
  }
  linkArguments.insert(linkArguments.end(), externalInputs.linkInputs.begin(),
                       externalInputs.linkInputs.end());
  for (const auto& directory : externalInputs.libraryDirectories) {
    linkArguments.insert(linkArguments.end(), {"-L", directory});
  }
  for (const auto& library : externalInputs.libraries) {
    linkArguments.insert(linkArguments.end(), {"-l", library});
  }
  linkArguments.insert(linkArguments.end(), externalInputs.linkArguments.begin(),
                       externalInputs.linkArguments.end());
  if (!runtimeObjects.empty()) {
    for (const auto& runtimeObject : runtimeObjects) {
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
  const auto& linker = useCxxLinker ? clangxx : clang;
  const auto linkResult =
      hitsimple::support::runProcess(*linker.path, linkArguments);
  if (!linkResult.launched) {
    std::cerr << "hsc: cannot start linker '"
              << hitsimple::support::pathToUtf8(*linker.path) << "': "
              << linkResult.error << '\n';
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
