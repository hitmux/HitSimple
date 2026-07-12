#include "hitsimple/support/Path.h"

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

namespace hitsimple::support {

std::filesystem::path pathFromUtf8(std::string_view value) {
#ifdef _WIN32
  if (value.empty()) {
    return {};
  }
  int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                 static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) {
    size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                               static_cast<int>(value.size()), nullptr, 0);
  }
  if (size <= 0) {
    return {};
  }
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size);
  return std::filesystem::path(result);
#else
  return std::filesystem::path(std::string(value));
#endif
}

std::string pathToUtf8(const std::filesystem::path& value) {
#ifdef _WIN32
  const auto& native = value.native();
  if (native.empty()) {
    return {};
  }
  const int size = WideCharToMultiByte(
      CP_UTF8, 0, native.data(), static_cast<int>(native.size()), nullptr, 0,
      nullptr, nullptr);
  if (size <= 0) {
    return {};
  }
  std::string result(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, native.data(),
                      static_cast<int>(native.size()), result.data(), size,
                      nullptr, nullptr);
  return result;
#else
  return value.string();
#endif
}

std::optional<std::filesystem::path> pathEnvironmentVariable(
    const char* name) {
#ifdef _WIN32
  const auto wideName = pathFromUtf8(name).wstring();
  const wchar_t* value = _wgetenv(wideName.c_str());
  if (value == nullptr || value[0] == L'\0') {
    return std::nullopt;
  }
  return std::filesystem::path(value);
#else
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return std::nullopt;
  }
  return std::filesystem::path(value);
#endif
}

} // namespace hitsimple::support
