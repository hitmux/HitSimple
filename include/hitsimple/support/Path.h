#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace hitsimple::support {

std::filesystem::path pathFromUtf8(std::string_view value);
std::string pathToUtf8(const std::filesystem::path& value);
std::optional<std::filesystem::path> pathEnvironmentVariable(const char* name);

} // namespace hitsimple::support
