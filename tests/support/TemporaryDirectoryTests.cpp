#include "support/TestRunner.h"

#include "hitsimple/support/Path.h"
#include "hitsimple/support/TemporaryDirectory.h"

#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using hitsimple::support::TemporaryDirectory;

static_assert(!std::is_copy_constructible_v<TemporaryDirectory>);
static_assert(!std::is_copy_assignable_v<TemporaryDirectory>);
static_assert(std::is_nothrow_move_constructible_v<TemporaryDirectory>);
static_assert(!std::is_move_assignable_v<TemporaryDirectory>);

} // namespace

HS_TEST(TemporaryDirectory_SamePrefixPreservesOtherOwners) {
  std::error_code error;
  auto root = TemporaryDirectory::create("hitsimple-temp-test", error);
  HS_EXPECT_TRUE(root.has_value());
  const auto prefix = root->path() /
      hitsimple::support::pathFromUtf8("scratch 空格");
  // A pre-existing path is not ours to remove, even if it matches the prefix.
  HS_EXPECT_TRUE(std::filesystem::create_directory(prefix));
  const auto sentinel = prefix / "keep.txt";
  std::ofstream(sentinel) << "keep";

  auto first = TemporaryDirectory::create(prefix, error);
  HS_EXPECT_TRUE(first.has_value());
  HS_EXPECT_TRUE(!error);
  const auto firstPath = first->path();
  const auto firstFile = firstPath / "object.o";
  std::ofstream(firstFile) << "first object";
  {
    auto second = TemporaryDirectory::create(prefix, error);
    HS_EXPECT_TRUE(second.has_value());
    HS_EXPECT_TRUE(firstPath != second->path());
    HS_EXPECT_TRUE(std::filesystem::is_regular_file(firstFile));
    HS_EXPECT_TRUE(std::filesystem::is_regular_file(sentinel));
  }
  HS_EXPECT_TRUE(std::filesystem::is_regular_file(firstFile));
  first.reset();
  HS_EXPECT_TRUE(!std::filesystem::exists(firstPath));
  HS_EXPECT_TRUE(std::filesystem::is_regular_file(sentinel));
}

HS_TEST(TemporaryDirectory_MoveTransfersCleanupOwnership) {
  std::error_code error;
  auto original = TemporaryDirectory::create("hitsimple-temp-test", error);
  HS_EXPECT_TRUE(original.has_value());
  const auto path = original->path();
  {
    auto moved = std::move(*original);
    original.reset();
    HS_EXPECT_EQ(moved.path(), path);
    HS_EXPECT_TRUE(std::filesystem::is_directory(path));
    std::ofstream(path / "owned.txt") << "owned";
  }
  HS_EXPECT_TRUE(!std::filesystem::exists(path));
}

HS_TEST(TemporaryDirectory_FailureDoesNotOwnOrRemoveParent) {
  std::error_code error;
  auto root = TemporaryDirectory::create("hitsimple-temp-test", error);
  HS_EXPECT_TRUE(root.has_value());
  const auto file = root->path() / "not-a-directory";
  std::ofstream(file) << "keep";
  auto failed = TemporaryDirectory::create(file / "scratch", error);
  HS_EXPECT_TRUE(!failed.has_value());
  HS_EXPECT_TRUE(static_cast<bool>(error));
  HS_EXPECT_TRUE(std::filesystem::is_regular_file(file));
  auto missing = TemporaryDirectory::create(
      root->path() / "missing" / "scratch", error);
  HS_EXPECT_TRUE(!missing.has_value());
  HS_EXPECT_TRUE(static_cast<bool>(error));
  HS_EXPECT_TRUE(!std::filesystem::exists(root->path() / "missing"));
  auto succeeded = TemporaryDirectory::create(root->path() / "scratch", error);
  HS_EXPECT_TRUE(succeeded.has_value());
  HS_EXPECT_TRUE(!error);
}

HS_TEST(TemporaryDirectory_ConcurrentSamePrefixIsUnique) {
  std::error_code error;
  auto root = TemporaryDirectory::create("hitsimple-temp-test", error);
  HS_EXPECT_TRUE(root.has_value());
  const auto prefix = root->path() / "scratch";
  // MSVC 14.29 futures assign their stored result; keep the owner itself
  // nonassignable and return an assignment-capable owning wrapper.
  std::vector<std::future<std::unique_ptr<TemporaryDirectory>>> futures;
  for (int index = 0; index < 16; ++index) {
    futures.push_back(std::async(std::launch::async, [prefix]()
        -> std::unique_ptr<TemporaryDirectory> {
      std::error_code error;
      auto directory = TemporaryDirectory::create(prefix, error);
      if (!directory) {
        return {};
      }
      return std::make_unique<TemporaryDirectory>(std::move(*directory));
    }));
  }
  std::vector<TemporaryDirectory> owners;
  std::set<std::filesystem::path> paths;
  for (auto& future : futures) {
    auto directory = future.get();
    HS_EXPECT_TRUE(directory != nullptr);
    HS_EXPECT_TRUE(paths.insert(directory->path()).second);
    HS_EXPECT_TRUE(std::filesystem::is_directory(directory->path()));
    owners.push_back(std::move(*directory));
  }
  owners.clear();
  for (const auto& path : paths) {
    HS_EXPECT_TRUE(!std::filesystem::exists(path));
  }
  HS_EXPECT_TRUE(std::filesystem::is_empty(root->path()));
}
