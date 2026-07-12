#include "hitsimple/support/ResourcePaths.h"

#include <array>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include <limits.h>
#include <unistd.h>

#ifndef HITSIMPLE_MCPP_EXECUTABLE
#define HITSIMPLE_MCPP_EXECUTABLE "hsc_mcpp"
#endif

#ifndef HITSIMPLE_PROJECT_SOURCE_DIR
#define HITSIMPLE_PROJECT_SOURCE_DIR "."
#endif

#ifndef HITSIMPLE_RUNTIME_SOURCE
#define HITSIMPLE_RUNTIME_SOURCE "runtime/hitsimple_runtime.c"
#endif

namespace hitsimple::support {
namespace {

std::optional<std::filesystem::path> environmentPath(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return std::nullopt;
  }
  return std::filesystem::path(value);
}

std::optional<std::filesystem::path> installedPrefix() {
  std::array<char, PATH_MAX> buffer{};
  const auto length = ::readlink("/proc/self/exe", buffer.data(),
                                 buffer.size() - 1);
  if (length <= 0) {
    return std::nullopt;
  }

  const auto executable =
      std::filesystem::path(std::string(buffer.data(), length));
  const auto binDirectory = executable.parent_path();
  if (binDirectory.filename() != "bin") {
    return std::nullopt;
  }
  return binDirectory.parent_path();
}

std::filesystem::path resolvePath(
    const char* environmentName,
    const std::filesystem::path& installedRelativePath,
    const std::filesystem::path& buildPath) {
  if (const auto configured = environmentPath(environmentName)) {
    return *configured;
  }

  std::optional<std::filesystem::path> installedPath;
  if (const auto prefix = installedPrefix()) {
    installedPath = *prefix / installedRelativePath;
    if (std::filesystem::exists(*installedPath)) {
      return *installedPath;
    }
  }

  if (std::filesystem::exists(buildPath)) {
    return buildPath;
  }
  return installedPath.value_or(buildPath);
}

} // namespace

std::filesystem::path standardLibraryRoot() {
  return resolvePath("HITSIMPLE_STDLIB_DIR",
                     "share/hitsimple/stdlib",
                     std::filesystem::path(HITSIMPLE_PROJECT_SOURCE_DIR) /
                         "stdlib");
}

std::filesystem::path runtimeSourcePath() {
  return resolvePath("HITSIMPLE_RUNTIME_SOURCE",
                     "share/hitsimple/runtime/hitsimple_runtime.c",
                     HITSIMPLE_RUNTIME_SOURCE);
}

std::filesystem::path preprocessorExecutablePath() {
  return resolvePath("HITSIMPLE_MCPP_EXECUTABLE",
                     "libexec/hitsimple/hsc_mcpp",
                     HITSIMPLE_MCPP_EXECUTABLE);
}

} // namespace hitsimple::support
