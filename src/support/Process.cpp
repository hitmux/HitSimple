#include "hitsimple/support/Process.h"

#include "hitsimple/support/Path.h"

#include <cstdlib>
#include <system_error>

#ifdef _WIN32
#include <windows.h>

#include <cwctype>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <limits.h>
#endif

namespace hitsimple::support {
namespace {

#ifdef _WIN32
std::wstring quoteWindowsArgument(std::wstring_view argument) {
  if (argument.empty()) {
    return L"\"\"";
  }
  if (argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
    return std::wstring(argument);
  }

  std::wstring quoted = L"\"";
  std::size_t backslashes = 0;
  for (const wchar_t ch : argument) {
    if (ch == L'\\') {
      ++backslashes;
      continue;
    }
    if (ch == L'\"') {
      quoted.append(backslashes * 2U + 1U, L'\\');
      quoted.push_back(ch);
      backslashes = 0;
      continue;
    }
    quoted.append(backslashes, L'\\');
    backslashes = 0;
    quoted.push_back(ch);
  }
  quoted.append(backslashes * 2U, L'\\');
  quoted.push_back(L'\"');
  return quoted;
}

std::wstring windowsErrorMessage(DWORD code) {
  wchar_t* buffer = nullptr;
  const DWORD length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
  std::wstring message;
  if (length != 0 && buffer != nullptr) {
    message.assign(buffer, length);
    while (!message.empty() && std::iswspace(message.back())) {
      message.pop_back();
    }
  }
  if (buffer != nullptr) {
    LocalFree(buffer);
  }
  if (message.empty()) {
    message = L"Windows error " + std::to_wstring(code);
  }
  return message;
}

std::string utf8(std::wstring_view value) {
  if (value.empty()) {
    return {};
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) {
    return "Windows process error";
  }
  std::string result(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

std::vector<std::wstring> executableExtensions() {
  const wchar_t* value = _wgetenv(L"PATHEXT");
  std::wstring extensions = value == nullptr ? L".COM;.EXE;.BAT;.CMD" : value;
  std::vector<std::wstring> result;
  std::size_t begin = 0;
  while (begin <= extensions.size()) {
    const auto end = extensions.find(L';', begin);
    auto extension = extensions.substr(begin, end - begin);
    if (!extension.empty()) {
      result.push_back(std::move(extension));
    }
    if (end == std::wstring::npos) {
      break;
    }
    begin = end + 1U;
  }
  return result;
}
#endif

bool isExecutableFile(const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) {
    return false;
  }
#ifdef _WIN32
  return true;
#else
  return ::access(path.c_str(), X_OK) == 0;
#endif
}

} // namespace

std::optional<std::filesystem::path> currentExecutablePath() {
#ifdef _WIN32
  std::wstring buffer(260, L'\0');
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      return std::nullopt;
    }
    if (length < buffer.size() - 1U) {
      buffer.resize(length);
      return std::filesystem::path(buffer);
    }
    buffer.resize(buffer.size() * 2U);
  }
#else
  std::array<char, PATH_MAX> buffer{};
  const auto length =
      ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1U);
  if (length <= 0) {
    return std::nullopt;
  }
  return std::filesystem::path(std::string(buffer.data(), length));
#endif
}

std::optional<std::filesystem::path> findExecutable(
    const std::filesystem::path& requested) {
  if (requested.empty()) {
    return std::nullopt;
  }
  if (requested.has_parent_path()) {
    if (isExecutableFile(requested)) {
      return std::filesystem::absolute(requested);
    }
#ifdef _WIN32
    if (!requested.has_extension()) {
      for (const auto& extension : executableExtensions()) {
        auto candidate = requested;
        candidate += extension;
        if (isExecutableFile(candidate)) {
          return std::filesystem::absolute(candidate);
        }
      }
    }
#endif
    return std::nullopt;
  }

#ifdef _WIN32
  const wchar_t* pathValue = _wgetenv(L"PATH");
#else
  const char* pathValue = std::getenv("PATH");
#endif
  if (pathValue == nullptr) {
    return std::nullopt;
  }
#ifdef _WIN32
  constexpr wchar_t separator = L';';
  const auto extensions = executableExtensions();
  const std::wstring searchPath(pathValue);
#else
  constexpr char separator = ':';
  const std::string searchPath(pathValue);
#endif
  std::size_t begin = 0;
  while (begin <= searchPath.size()) {
    const auto end = searchPath.find(separator, begin);
    const auto directoryText = searchPath.substr(begin, end - begin);
    const auto directory = directoryText.empty()
                               ? std::filesystem::current_path()
                               : std::filesystem::path(directoryText);
    const auto candidate = directory / requested;
    if (isExecutableFile(candidate)) {
      return std::filesystem::absolute(candidate);
    }
#ifdef _WIN32
    if (!requested.has_extension()) {
      for (const auto& extension : extensions) {
        auto extended = candidate;
        extended += extension;
        if (isExecutableFile(extended)) {
          return std::filesystem::absolute(extended);
        }
      }
    }
#endif
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1U;
  }
  return std::nullopt;
}

unsigned long long currentProcessId() {
#ifdef _WIN32
  return static_cast<unsigned long long>(GetCurrentProcessId());
#else
  return static_cast<unsigned long long>(::getpid());
#endif
}

ProcessResult runProcess(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments,
    const std::optional<std::filesystem::path>& standardOutput,
    const std::optional<std::filesystem::path>& standardError) {
#ifdef _WIN32
  std::wstring commandLine = quoteWindowsArgument(executable.wstring());
  for (const auto& argument : arguments) {
    commandLine.push_back(L' ');
    commandLine += quoteWindowsArgument(pathFromUtf8(argument).wstring());
  }

  SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE outputHandle = INVALID_HANDLE_VALUE;
  HANDLE errorHandle = INVALID_HANDLE_VALUE;
  const auto openRedirect = [&](const std::optional<std::filesystem::path>& path,
                                HANDLE& handle) -> bool {
    if (!path) {
      return true;
    }
    handle = CreateFileW(path->c_str(), GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return handle != INVALID_HANDLE_VALUE;
  };
  if (!openRedirect(standardOutput, outputHandle) ||
      !openRedirect(standardError, errorHandle)) {
    const auto error = utf8(windowsErrorMessage(GetLastError()));
    if (outputHandle != INVALID_HANDLE_VALUE) CloseHandle(outputHandle);
    if (errorHandle != INVALID_HANDLE_VALUE) CloseHandle(errorHandle);
    return {false, -1, error};
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  if (standardOutput || standardError) {
    startup.dwFlags |= STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = standardOutput ? outputHandle
                                        : GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = standardError ? errorHandle
                                      : GetStdHandle(STD_ERROR_HANDLE);
  }
  PROCESS_INFORMATION process{};
  const BOOL created = CreateProcessW(
      executable.c_str(), commandLine.data(), nullptr, nullptr, TRUE, 0,
      nullptr, nullptr, &startup, &process);
  if (outputHandle != INVALID_HANDLE_VALUE) CloseHandle(outputHandle);
  if (errorHandle != INVALID_HANDLE_VALUE) CloseHandle(errorHandle);
  if (!created) {
    return {false, -1, utf8(windowsErrorMessage(GetLastError()))};
  }
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD exitCode = 1;
  GetExitCodeProcess(process.hProcess, &exitCode);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return {true, static_cast<int>(exitCode), {}};
#else
  const pid_t child = ::fork();
  if (child < 0) {
    return {false, -1, std::strerror(errno)};
  }
  if (child == 0) {
    const auto redirect = [](const std::optional<std::filesystem::path>& path,
                             int target) {
      if (!path) {
        return true;
      }
      const int descriptor =
          ::open(path->c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (descriptor < 0 || ::dup2(descriptor, target) < 0) {
        if (descriptor >= 0) ::close(descriptor);
        return false;
      }
      ::close(descriptor);
      return true;
    };
    if (!redirect(standardOutput, STDOUT_FILENO) ||
        !redirect(standardError, STDERR_FILENO)) {
      _exit(126);
    }
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 2U);
    auto executableText = executable.string();
    argv.push_back(executableText.data());
    std::vector<std::string> ownedArguments = arguments;
    for (auto& argument : ownedArguments) {
      argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    ::execv(executable.c_str(), argv.data());
    _exit(errno == ENOENT ? 127 : 126);
  }

  int status = 0;
  while (::waitpid(child, &status, 0) < 0) {
    if (errno != EINTR) {
      return {false, -1, std::strerror(errno)};
    }
  }
  if (WIFEXITED(status)) {
    return {true, WEXITSTATUS(status), {}};
  }
  if (WIFSIGNALED(status)) {
    return {true, 128 + WTERMSIG(status), {}};
  }
  return {true, 1, {}};
#endif
}

} // namespace hitsimple::support
