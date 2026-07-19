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

struct LlvmOptSelection {
  std::optional<std::filesystem::path> path;
  std::string source;
  std::string error;
};

ClangSelection resolveClang(
    const std::optional<std::filesystem::path>& commandLineOverride);

ClangSelection resolveClangxx(
    const std::optional<std::filesystem::path>& commandLineOverride);

LlvmArSelection resolveLlvmAr();

LlvmOptSelection resolveLlvmOpt();

LlvmOptSelection resolveLlvmOpt(
    const std::optional<std::filesystem::path>& clangPath);

} // namespace hitsimple::support
