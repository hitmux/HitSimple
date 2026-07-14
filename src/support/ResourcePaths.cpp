#include "hitsimple/support/ResourcePaths.h"

#include "hitsimple/support/Process.h"
#include "hitsimple/support/Path.h"

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#ifndef HITSIMPLE_MCPP_EXECUTABLE
#define HITSIMPLE_MCPP_EXECUTABLE "hsc_mcpp"
#endif

#ifndef HITSIMPLE_PROJECT_SOURCE_DIR
#define HITSIMPLE_PROJECT_SOURCE_DIR "."
#endif

#ifndef HITSIMPLE_RUNTIME_SOURCE
#define HITSIMPLE_RUNTIME_SOURCE "runtime/hitsimple_runtime.c"
#endif

#ifndef HITSIMPLE_RUNTIME_LIBRARY
#define HITSIMPLE_RUNTIME_LIBRARY "libhitsimple_runtime.a"
#endif

#ifndef HITSIMPLE_INSTALL_LIBDIR
#define HITSIMPLE_INSTALL_LIBDIR "lib"
#endif

namespace hitsimple::support {
namespace {

std::optional<std::filesystem::path> environmentPath(const char* name) {
  return pathEnvironmentVariable(name);
}

std::optional<std::filesystem::path> installedPrefix() {
  const auto executable = currentExecutablePath();
  if (!executable) {
    return std::nullopt;
  }
  const auto binDirectory = executable->parent_path();
  if (binDirectory.filename() != "bin") {
#ifdef _WIN32
    return binDirectory;
#else
    return std::nullopt;
#endif
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

std::filesystem::path runtimeLibraryPath() {
  return resolvePath("HITSIMPLE_RUNTIME_LIBRARY",
                     HITSIMPLE_INSTALL_LIBDIR "/hitsimple/libhitsimple_runtime.a",
                     HITSIMPLE_RUNTIME_LIBRARY);
}

std::filesystem::path preprocessorExecutablePath() {
#ifdef _WIN32
  constexpr std::string_view installedPath = "libexec/hitsimple/hsc_mcpp.exe";
#else
  constexpr std::string_view installedPath = "libexec/hitsimple/hsc_mcpp";
#endif
  return resolvePath("HITSIMPLE_MCPP_EXECUTABLE",
                     installedPath,
                     HITSIMPLE_MCPP_EXECUTABLE);
}

std::filesystem::path bundledClangPath() {
#ifdef _WIN32
  return resolvePath("HITSIMPLE_BUNDLED_CLANG",
                     "toolchain/bin/clang++.exe",
                     "toolchain/bin/clang++.exe");
#else
  return resolvePath("HITSIMPLE_BUNDLED_CLANG",
                     "toolchain/bin/clang++",
                     "toolchain/bin/clang++");
#endif
}

} // namespace hitsimple::support
