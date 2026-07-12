#pragma once

#include <filesystem>

namespace hitsimple::support {

std::filesystem::path standardLibraryRoot();
std::filesystem::path runtimeSourcePath();
std::filesystem::path runtimeLibraryPath();
std::filesystem::path preprocessorExecutablePath();
std::filesystem::path bundledClangPath();

} // namespace hitsimple::support
