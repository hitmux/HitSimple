#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hitsimple::support {

struct CargoSelection final {
  std::optional<std::filesystem::path> path;
  std::string source;
  std::string error;
};

struct CargoBuildOptions final {
  std::filesystem::path manifestPath;
  std::optional<std::string> package;
  std::optional<std::string> profile;
  std::optional<std::string> features;
  bool noDefaultFeatures = false;
};

struct CargoStaticLibrary final {
  std::filesystem::path archivePath;
  std::vector<std::string> libraryDirectories;
  std::vector<std::string> libraries;
};

CargoSelection resolveCargo();

std::optional<CargoStaticLibrary> buildCargoStaticLibrary(
    const CargoSelection& cargo, const CargoBuildOptions& options,
    std::string& error);

} // namespace hitsimple::support
