#include "support/TestRunner.h"

#include "hitsimple/support/Process.h"
#include "hitsimple/support/Path.h"
#include "hitsimple/support/Toolchain.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

HS_TEST(Process_CurrentExecutablePathExists) {
  const auto executable = hitsimple::support::currentExecutablePath();
  HS_EXPECT_TRUE(executable.has_value());
  HS_EXPECT_TRUE(std::filesystem::is_regular_file(*executable));
}

HS_TEST(Process_RunProcessUsesArgvAndUnicodeRedirectPath) {
  const auto executable = hitsimple::support::currentExecutablePath();
  HS_EXPECT_TRUE(executable.has_value());
  const auto output = std::filesystem::temp_directory_path() /
                      hitsimple::support::pathFromUtf8(
                          "HitSimple 进程 output.txt");
  const auto result = hitsimple::support::runProcess(
      *executable, {"No matching test filter with spaces"}, output);
  HS_EXPECT_TRUE(result.launched);
  HS_EXPECT_EQ(result.exitCode, 0);
  std::string text;
  {
    std::ifstream input(output, std::ios::binary);
    text.assign(std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>());
  }
  std::filesystem::remove(output);
  HS_EXPECT_TRUE(text.find("0/0 PASS") != std::string::npos);
}

HS_TEST(Path_Utf8RoundTripsUnicodeAndSpaces) {
  const std::string text = "HitSimple 空格路径/编译器.exe";
  HS_EXPECT_EQ(
      hitsimple::support::pathToUtf8(hitsimple::support::pathFromUtf8(text)),
      text);
}

HS_TEST(Toolchain_UsesExplicitExecutableBeforeDiscovery) {
  const auto executable = hitsimple::support::currentExecutablePath();
  HS_EXPECT_TRUE(executable.has_value());
  const auto selection = hitsimple::support::resolveClang(*executable);
  HS_EXPECT_TRUE(selection.path.has_value());
  HS_EXPECT_EQ(selection.source, std::string("--clang"));
}

HS_TEST(Toolchain_UsesExplicitCxxExecutableBeforeDiscovery) {
  const auto executable = hitsimple::support::currentExecutablePath();
  HS_EXPECT_TRUE(executable.has_value());
  const auto selection = hitsimple::support::resolveClangxx(*executable);
  HS_EXPECT_TRUE(selection.path.has_value());
  HS_EXPECT_EQ(selection.source, std::string("--clangxx"));
}
