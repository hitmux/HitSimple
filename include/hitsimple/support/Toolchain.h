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

ClangSelection resolveClang(
    const std::optional<std::filesystem::path>& commandLineOverride);

} // namespace hitsimple::support
