#include "hitsimple/support/Toolchain.h"

#include "hitsimple/support/Process.h"
#include "hitsimple/support/Path.h"
#include "hitsimple/support/ResourcePaths.h"

#include <cstdlib>
#include <vector>

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

std::optional<std::string> clangToolchainVersion(
    const std::filesystem::path& clangPath) {
  std::vector<std::filesystem::path> candidates{clangPath};
  std::error_code error;
  const auto canonical = std::filesystem::weakly_canonical(clangPath, error);
  if (!error) {
    candidates.push_back(canonical);
  }
  for (const auto& candidate : candidates) {
    const auto filename = candidate.filename().string();
    constexpr std::string_view clangPrefix = "clang-";
    if (filename.starts_with(clangPrefix) && filename.size() > clangPrefix.size()) {
      return filename.substr(clangPrefix.size());
    }
    const auto toolchainDirectory =
        candidate.parent_path().parent_path().filename().string();
    constexpr std::string_view llvmPrefix = "llvm-";
    if (toolchainDirectory.starts_with(llvmPrefix) &&
        toolchainDirectory.size() > llvmPrefix.size()) {
      return toolchainDirectory.substr(llvmPrefix.size());
    }
  }
  return std::nullopt;
}

LlvmOptSelection resolveLlvmOptForVersion(
    const std::optional<std::string>& version) {
  if (version) {
    if (const auto resolved = findExecutable("opt-" + *version)) {
      return {*resolved, "Clang-compatible opt-" + *version, {}};
    }
  }
  for (const auto* name : {"opt-18", "opt-19", "opt-17", "opt-16", "opt"}) {
    if (const auto resolved = findExecutable(name)) {
      return {*resolved, std::string("PATH ") + name, {}};
    }
  }
  return {std::nullopt, "not found",
          "no LLVM opt tool found; set HITSIMPLE_OPT or install opt"};
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

ClangSelection resolveClangxx(
    const std::optional<std::filesystem::path>& commandLineOverride) {
  if (commandLineOverride) {
    return resolveConfigured(*commandLineOverride, "--clangxx");
  }
  if (const auto environment = pathEnvironmentVariable("HITSIMPLE_CLANGXX")) {
    return resolveConfigured(*environment, "HITSIMPLE_CLANGXX");
  }
  if (const auto resolved = findExecutable("clang++-18")) {
    return {*resolved, "clang++-18", {}};
  }
  if (const auto resolved = findExecutable("clang++")) {
    return {*resolved, "PATH clang++", {}};
  }
  return {std::nullopt, "not found",
          "no compatible Clang++ toolchain found; use --clangxx <path>, set "
          "HITSIMPLE_CLANGXX, or install Clang 18"};
}

LlvmArSelection resolveLlvmAr() {
  if (const auto environment = pathEnvironmentVariable("HITSIMPLE_LLVM_AR")) {
    if (const auto resolved = findExecutable(*environment)) {
      return {*resolved, "HITSIMPLE_LLVM_AR", {}};
    }
    return {std::nullopt, "HITSIMPLE_LLVM_AR",
            "configured llvm-ar executable was not found: " +
                pathToUtf8(*environment)};
  }
  if (const auto resolved = findExecutable("llvm-ar-18")) {
    return {*resolved, "PATH llvm-ar-18", {}};
  }
  if (const auto resolved = findExecutable("llvm-ar")) {
    return {*resolved, "PATH llvm-ar", {}};
  }
  return {std::nullopt, "not found",
          "no llvm-ar tool found; set HITSIMPLE_LLVM_AR or install llvm-ar"};
}

LlvmOptSelection resolveLlvmOpt() {
  if (const auto environment = pathEnvironmentVariable("HITSIMPLE_OPT")) {
    if (const auto resolved = findExecutable(*environment)) {
      return {*resolved, "HITSIMPLE_OPT", {}};
    }
    return {std::nullopt, "HITSIMPLE_OPT",
            "configured opt executable was not found: " +
                pathToUtf8(*environment)};
  }
  return resolveLlvmOptForVersion(std::nullopt);
}

LlvmOptSelection resolveLlvmOpt(
    const std::optional<std::filesystem::path>& clangPath) {
  if (const auto environment = pathEnvironmentVariable("HITSIMPLE_OPT")) {
    if (const auto resolved = findExecutable(*environment)) {
      return {*resolved, "HITSIMPLE_OPT", {}};
    }
    return {std::nullopt, "HITSIMPLE_OPT",
            "configured opt executable was not found: " +
                pathToUtf8(*environment)};
  }
  return resolveLlvmOptForVersion(
      clangPath ? clangToolchainVersion(*clangPath) : std::nullopt);
}

} // namespace hitsimple::support
