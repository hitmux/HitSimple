#include "support/TestRunner.h"

#include "hitsimple/support/ResourcePaths.h"

#include <cstdlib>
#include <optional>
#include <string>

namespace {

class ScopedEnvironment final {
public:
  ScopedEnvironment(const char* name, const char* value) : name_(name) {
    if (const char* current = std::getenv(name)) {
      previousValue_ = current;
    }
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
  }

  ~ScopedEnvironment() {
    if (previousValue_) {
      set(name_.c_str(), previousValue_->c_str());
    } else {
      set(name_.c_str(), "");
    }
  }

private:
  static void set(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    if (value[0] == '\0') {
      unsetenv(name);
    } else {
      setenv(name, value, 1);
    }
#endif
  }

  std::string name_;
  std::optional<std::string> previousValue_;
};

} // namespace

HS_TEST(ResourcePaths_UseConfiguredStandardLibraryDirectory) {
  const ScopedEnvironment environment("HITSIMPLE_STDLIB_DIR",
                                      "/tmp/hitsimple-stdlib");
  HS_EXPECT_EQ(hitsimple::support::standardLibraryRoot().string(),
               std::string("/tmp/hitsimple-stdlib"));
}

HS_TEST(ResourcePaths_UseConfiguredRuntimeSource) {
  const ScopedEnvironment environment("HITSIMPLE_RUNTIME_SOURCE",
                                      "/tmp/hitsimple-runtime.c");
  HS_EXPECT_EQ(hitsimple::support::runtimeSourcePath().string(),
               std::string("/tmp/hitsimple-runtime.c"));
}

HS_TEST(ResourcePaths_UseConfiguredRuntimeLibrary) {
  const ScopedEnvironment environment("HITSIMPLE_RUNTIME_LIBRARY",
                                      "/tmp/libhitsimple_runtime.a");
  HS_EXPECT_EQ(hitsimple::support::runtimeLibraryPath().string(),
               std::string("/tmp/libhitsimple_runtime.a"));
}

HS_TEST(ResourcePaths_UseConfiguredPreprocessorExecutable) {
  const ScopedEnvironment environment("HITSIMPLE_MCPP_EXECUTABLE",
                                      "/tmp/hitsimple-mcpp");
  HS_EXPECT_EQ(hitsimple::support::preprocessorExecutablePath().string(),
               std::string("/tmp/hitsimple-mcpp"));
}
