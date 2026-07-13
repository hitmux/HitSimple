#include "hitsimple/support/Toolchain.h"

#include "hitsimple/support/Process.h"
#include "hitsimple/support/Path.h"
#include "hitsimple/support/ResourcePaths.h"

#include <cstdlib>

namespace hitsimple::support {
namespace {

ClangSelection resolveConfigured(const std::filesystem::path& configured,
                                 std::string source) {
  const auto resolved = findExecutable(configured);
  if (resolved) {
    return {*resolved, std::move(source), {}};
  }
  return {std::nullopt, std::move(source),
          "configured Clang executable was not found: " +
              pathToUtf8(configured)};
}

} // namespace

ClangSelection resolveClang(
    const std::optional<std::filesystem::path>& commandLineOverride) {
  if (commandLineOverride) {
    return resolveConfigured(*commandLineOverride, "--clang");
  }
  if (const auto environment = pathEnvironmentVariable("HITSIMPLE_CLANG")) {
    return resolveConfigured(*environment, "HITSIMPLE_CLANG");
  }
  const auto bundled = bundledClangPath();
  if (const auto resolved = findExecutable(bundled)) {
    return {*resolved, "bundled", {}};
  }
  if (const auto resolved = findExecutable("clang-18")) {
    return {*resolved, "clang-18", {}};
  }
  if (const auto resolved = findExecutable("clang")) {
    return {*resolved, "PATH clang", {}};
  }
  if (const auto resolved = findExecutable("clang++")) {
    return {*resolved, "PATH clang++", {}};
  }
  return {std::nullopt, "not found",
          "no compatible Clang toolchain found; use --clang <path>, set "
          "HITSIMPLE_CLANG, or install Clang 18"};
}

} // namespace hitsimple::support
