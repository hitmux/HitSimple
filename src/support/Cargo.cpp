#include "hitsimple/support/Cargo.h"

#include "hitsimple/support/Path.h"
#include "hitsimple/support/Process.h"

#include <llvm/Support/JSON.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace hitsimple::support {
namespace {

std::string_view stringView(llvm::StringRef value) {
  return {value.data(), value.size()};
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

std::optional<std::string> readFile(const std::filesystem::path& path,
                                    std::string& error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "cannot read Cargo output '" + pathToUtf8(path) + "'";
    return std::nullopt;
  }
  std::ostringstream content;
  content << input.rdbuf();
  if (!input.good() && !input.eof()) {
    error = "cannot read Cargo output '" + pathToUtf8(path) + "'";
    return std::nullopt;
  }
  return content.str();
}

void forwardCargoDiagnostics(std::string_view output) {
  std::istringstream lines{std::string(output)};
  std::string line;
  while (std::getline(lines, line)) {
    auto parsed = llvm::json::parse(line);
    if (!parsed) {
      llvm::consumeError(parsed.takeError());
      if (!line.empty()) {
        std::cerr << line << '\n';
      }
      continue;
    }
    const auto* object = parsed->getAsObject();
    const auto reason = object == nullptr
        ? std::nullopt
        : object->getString("reason");
    if (!reason || *reason != "compiler-message") {
      continue;
    }
    const auto* message = object->getObject("message");
    if (message == nullptr) {
      continue;
    }
    if (const auto rendered = message->getString("rendered")) {
      std::cerr.write(rendered->data(), static_cast<std::streamsize>(rendered->size()));
      if (!rendered->ends_with("\n")) {
        std::cerr << '\n';
      }
    }
  }
}

bool containsStaticLibTarget(const llvm::json::Object& target) {
  const auto* crateTypes = target.getArray("crate_types");
  if (crateTypes == nullptr) {
    return false;
  }
  for (const auto& item : *crateTypes) {
    const auto type = item.getAsString();
    if (type && *type == "staticlib") {
      return true;
    }
  }
  return false;
}

std::optional<std::filesystem::path> staticLibraryFilename(
    const llvm::json::Object& artifact) {
  const auto* filenames = artifact.getArray("filenames");
  if (filenames == nullptr) {
    return std::nullopt;
  }
  for (const auto& item : *filenames) {
    const auto filename = item.getAsString();
    if (!filename) {
      continue;
    }
    const auto path = pathFromUtf8(stringView(*filename));
    if (path.extension() == ".a" || path.extension() == ".lib") {
      return path;
    }
  }
  return std::nullopt;
}

bool appendNativeLibrary(std::string_view requirement,
                         CargoStaticLibrary& library, std::string& error) {
  const auto separator = requirement.find('=');
  const auto kind = requirement.substr(0, separator);
  const auto name = separator == std::string_view::npos
      ? requirement
      : requirement.substr(separator + 1U);
  if (name.empty()) {
    error = "Cargo reported an empty native link requirement";
    return false;
  }
  if (separator == std::string_view::npos || kind == "dylib" ||
      kind == "static") {
    library.libraries.emplace_back(name);
    return true;
  }
  error = "Cargo reported unsupported native link requirement '" +
      std::string(requirement) + "'";
  return false;
}

bool appendNativeSearchPath(std::string_view requirement,
                            CargoStaticLibrary& library, std::string& error) {
  constexpr std::string_view nativePrefix = "native=";
  if (requirement.starts_with(nativePrefix)) {
    requirement.remove_prefix(nativePrefix.size());
  } else if (requirement.find('=') != std::string_view::npos) {
    error = "Cargo reported unsupported native link search path '" +
        std::string(requirement) + "'";
    return false;
  }
  if (requirement.empty()) {
    error = "Cargo reported an empty native link search path";
    return false;
  }
  library.libraryDirectories.emplace_back(requirement);
  return true;
}

bool parseCargoOutput(std::string_view output, CargoStaticLibrary& library,
                      std::string& error) {
  std::optional<std::filesystem::path> archive;
  std::istringstream lines{std::string(output)};
  std::string line;
  while (std::getline(lines, line)) {
    auto parsed = llvm::json::parse(line);
    if (!parsed) {
      llvm::consumeError(parsed.takeError());
      continue;
    }
    const auto* object = parsed->getAsObject();
    if (object == nullptr) {
      continue;
    }
    const auto reason = object->getString("reason");
    if (reason && *reason == "compiler-artifact") {
      const auto* target = object->getObject("target");
      if (target == nullptr || !containsStaticLibTarget(*target)) {
        continue;
      }
      const auto artifact = staticLibraryFilename(*object);
      if (!artifact) {
        error = "Cargo reported a staticlib target without an archive filename";
        return false;
      }
      if (archive && *archive != *artifact) {
        error = "Cargo reported more than one staticlib artifact; select one "
                "with --cargo-package";
        return false;
      }
      archive = *artifact;
      continue;
    }
    if (!reason || *reason != "build-script-executed") {
      continue;
    }
    if (const auto* linkedPaths = object->getArray("linked_paths")) {
      for (const auto& item : *linkedPaths) {
        const auto path = item.getAsString();
        if (!path || !appendNativeSearchPath(stringView(*path), library, error)) {
          return false;
        }
      }
    }
    if (const auto* linkedLibraries = object->getArray("linked_libs")) {
      for (const auto& item : *linkedLibraries) {
        const auto name = item.getAsString();
        if (!name || !appendNativeLibrary(stringView(*name), library, error)) {
          return false;
        }
      }
    }
  }
  if (!archive) {
    error = "Cargo did not emit a staticlib artifact; configure [lib] "
            "crate-type = [\"staticlib\"]";
    return false;
  }
  std::error_code archiveError;
  if (!std::filesystem::is_regular_file(*archive, archiveError)) {
    error = "Cargo reported staticlib archive '" + pathToUtf8(*archive) +
        "' that is unavailable";
    if (archiveError) {
      error += ": " + archiveError.message();
    }
    return false;
  }
  library.archivePath = *archive;
  return true;
}

bool manifestContainsPackage(const llvm::json::Object& metadata,
                             const std::filesystem::path& manifestPath) {
  const auto* packages = metadata.getArray("packages");
  if (packages == nullptr) {
    return false;
  }
  std::error_code canonicalError;
  const auto canonicalManifest =
      std::filesystem::weakly_canonical(manifestPath, canonicalError);
  for (const auto& item : *packages) {
    const auto* package = item.getAsObject();
    if (package == nullptr) {
      continue;
    }
    const auto packageManifest = package->getString("manifest_path");
    if (!packageManifest) {
      continue;
    }
    std::error_code packageError;
    const auto canonicalPackage = std::filesystem::weakly_canonical(
        pathFromUtf8(stringView(*packageManifest)), packageError);
    if (!canonicalError && !packageError &&
        canonicalPackage == canonicalManifest) {
      return true;
    }
  }
  return false;
}

bool validateCargoManifest(const CargoSelection& cargo,
                           const CargoBuildOptions& options,
                           const std::filesystem::path& outputPath,
                           std::string& error) {
  const auto metadataPath = outputPath.parent_path() / "metadata.json";
  const auto diagnosticsPath = outputPath.parent_path() / "metadata.stderr";
  const auto process = runProcess(
      *cargo.path,
      {"metadata", "--no-deps", "--format-version", "1", "--manifest-path",
       pathToUtf8(options.manifestPath), "--color", "never"},
      metadataPath, diagnosticsPath);
  std::string diagnosticsError;
  const auto diagnostics = readFile(diagnosticsPath, diagnosticsError);
  if (diagnostics && !diagnostics->empty()) {
    std::cerr << *diagnostics;
    if (!diagnostics->ends_with('\n')) {
      std::cerr << '\n';
    }
  }
  if (!process.launched) {
    error = "cannot start Cargo '" + pathToUtf8(*cargo.path) + "': " +
        process.error;
    return false;
  }
  if (process.exitCode != 0) {
    error = "Cargo metadata failed for '" + pathToUtf8(options.manifestPath) +
        "' (exit code " + std::to_string(process.exitCode) + ")";
    return false;
  }
  std::string metadataError;
  const auto metadata = readFile(metadataPath, metadataError);
  if (!metadata) {
    error = std::move(metadataError);
    return false;
  }
  auto parsed = llvm::json::parse(*metadata);
  if (!parsed) {
    llvm::consumeError(parsed.takeError());
    error = "Cargo metadata did not return valid JSON";
    return false;
  }
  const auto* object = parsed->getAsObject();
  if (object == nullptr) {
    error = "Cargo metadata did not return a JSON object";
    return false;
  }
  if (!options.package && !manifestContainsPackage(*object, options.manifestPath)) {
    error = "Cargo manifest is a virtual workspace; use --cargo-package <name>";
    return false;
  }
  return true;
}

} // namespace

CargoSelection resolveCargo() {
  if (const auto environment = pathEnvironmentVariable("HITSIMPLE_CARGO")) {
    if (const auto resolved = findExecutable(*environment)) {
      return {*resolved, "HITSIMPLE_CARGO", {}};
    }
    return {std::nullopt, "HITSIMPLE_CARGO",
            "configured Cargo executable was not found: " +
                pathToUtf8(*environment)};
  }
  if (const auto resolved = findExecutable("cargo")) {
    return {*resolved, "PATH cargo", {}};
  }
  return {std::nullopt, "not found",
          "no Cargo tool found; set HITSIMPLE_CARGO or install Cargo"};
}

std::optional<CargoStaticLibrary> buildCargoStaticLibrary(
    const CargoSelection& cargo, const CargoBuildOptions& options,
    std::string& error) {
  if (!cargo.path) {
    error = cargo.error;
    return std::nullopt;
  }
  std::error_code manifestError;
  if (!std::filesystem::is_regular_file(options.manifestPath, manifestError)) {
    error = "Cargo manifest is unavailable '" + pathToUtf8(options.manifestPath) +
        "'";
    if (manifestError) {
      error += ": " + manifestError.message();
    }
    return std::nullopt;
  }

  const auto temporaryPath = std::filesystem::temp_directory_path() /
      ("hitsimple-cargo-" + std::to_string(currentProcessId()));
  std::error_code directoryError;
  std::filesystem::remove_all(temporaryPath, directoryError);
  directoryError.clear();
  if (!std::filesystem::create_directories(temporaryPath, directoryError)) {
    error = "cannot create Cargo temporary directory '" +
        pathToUtf8(temporaryPath) + "': " + directoryError.message();
    return std::nullopt;
  }
  TemporaryDirectory temporaryDirectory(temporaryPath);
  const auto outputPath = temporaryDirectory.path() / "build.json";
  if (!validateCargoManifest(cargo, options, outputPath, error)) {
    return std::nullopt;
  }

  std::vector<std::string> arguments{
      "build", "--lib", "--message-format=json-render-diagnostics",
      "--color", "never", "--manifest-path", pathToUtf8(options.manifestPath)};
  if (options.package) {
    arguments.insert(arguments.end(), {"--package", *options.package});
  }
  if (options.profile) {
    arguments.insert(arguments.end(), {"--profile", *options.profile});
  }
  if (options.features) {
    arguments.insert(arguments.end(), {"--features", *options.features});
  }
  if (options.noDefaultFeatures) {
    arguments.push_back("--no-default-features");
  }
  const auto diagnosticsPath = temporaryDirectory.path() / "build.stderr";
  const auto process = runProcess(*cargo.path, arguments, outputPath, diagnosticsPath);
  std::string outputError;
  const auto output = readFile(outputPath, outputError);
  if (output) {
    forwardCargoDiagnostics(*output);
  }
  std::string diagnosticsError;
  const auto diagnostics = readFile(diagnosticsPath, diagnosticsError);
  if (diagnostics && !diagnostics->empty()) {
    std::cerr << *diagnostics;
    if (!diagnostics->ends_with('\n')) {
      std::cerr << '\n';
    }
  }
  if (!process.launched) {
    error = "cannot start Cargo '" + pathToUtf8(*cargo.path) + "': " +
        process.error;
    return std::nullopt;
  }
  if (process.exitCode != 0) {
    error = "Cargo build failed for '" + pathToUtf8(options.manifestPath) +
        "' (exit code " + std::to_string(process.exitCode) + ")";
    return std::nullopt;
  }
  if (!output) {
    error = std::move(outputError);
    return std::nullopt;
  }
  CargoStaticLibrary library;
  if (!parseCargoOutput(*output, library, error)) {
    return std::nullopt;
  }
  return library;
}

} // namespace hitsimple::support
