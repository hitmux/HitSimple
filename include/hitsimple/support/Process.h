#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hitsimple::support {

struct ProcessResult {
  bool launched = false;
  int exitCode = -1;
  std::string error;
};

std::optional<std::filesystem::path> currentExecutablePath();
std::optional<std::filesystem::path> findExecutable(
    const std::filesystem::path& path);
unsigned long long currentProcessId();

ProcessResult runProcess(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments,
    const std::optional<std::filesystem::path>& standardOutput = std::nullopt,
    const std::optional<std::filesystem::path>& standardError = std::nullopt);

} // namespace hitsimple::support
