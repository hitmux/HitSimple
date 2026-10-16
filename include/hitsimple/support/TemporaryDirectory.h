#pragma once

#include <filesystem>
#include <optional>
#include <system_error>

namespace hitsimple::support {

// Owns only the directory atomically created by create(), never an existing path.
class TemporaryDirectory final {
public:
  // Relative prefixes are placed in the system temporary directory. An absolute
  // prefix selects its parent directory; that parent must already exist.
  static std::optional<TemporaryDirectory> create(
      const std::filesystem::path& prefix, std::error_code& error);

  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
  TemporaryDirectory(TemporaryDirectory&& other) noexcept;
  TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;
  ~TemporaryDirectory();

  const std::filesystem::path& path() const { return path_; }

private:
  explicit TemporaryDirectory(std::filesystem::path path);

  std::filesystem::path path_;
};

} // namespace hitsimple::support
