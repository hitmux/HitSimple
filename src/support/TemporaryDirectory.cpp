#include "hitsimple/support/TemporaryDirectory.h"

#include "hitsimple/support/Path.h"

#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FileSystem.h>

#include <utility>

namespace hitsimple::support {

std::optional<TemporaryDirectory> TemporaryDirectory::create(
    const std::filesystem::path& prefix, std::error_code& error) {
  llvm::SmallString<128> path;
  // LLVM creates each candidate atomically and retries existing names. A PID is
  // not unique when compiler processes share /tmp across PID namespaces.
  error = llvm::sys::fs::createUniqueDirectory(pathToUtf8(prefix), path);
  if (error) {
    return std::nullopt;
  }
  return TemporaryDirectory(pathFromUtf8({path.data(), path.size()}));
}

TemporaryDirectory::TemporaryDirectory(std::filesystem::path path)
    : path_(std::move(path)) {}

TemporaryDirectory::TemporaryDirectory(TemporaryDirectory&& other) noexcept
    : path_(std::exchange(other.path_, {})) {}

TemporaryDirectory::~TemporaryDirectory() {
  if (!path_.empty()) {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
}

} // namespace hitsimple::support
