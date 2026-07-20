#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace hitsimple::support {

struct ClangSelection {
  std::optional<std::filesystem::path> path;
  std::string source;
  std::string error;
};

struct LlvmArSelection {
  std::optional<std::filesystem::path> path;
  std::string source;
  std::string error;
};

ClangSelection resolveClang(
    const std::optional<std::filesystem::path>& commandLineOverride);

ClangSelection resolveClangxx(
    const std::optional<std::filesystem::path>& commandLineOverride);

std::string preferredLlvmArExecutableName();
LlvmArSelection resolveLlvmAr();

} // namespace hitsimple::support
