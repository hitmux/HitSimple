#include "support/TestRunner.h"

#include "hitsimple/parser/Parser.h"
#include "hitsimple/preprocessor/Preprocessor.h"
#include "hitsimple/support/Path.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

namespace {

class ScopedEnvironment final {
public:
  ScopedEnvironment(const char* name, const std::string& value) : name_(name) {
    if (const char* current = std::getenv(name)) {
      previousValue_ = current;
    }
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
  }

  ~ScopedEnvironment() {
#ifdef _WIN32
    _putenv_s(name_.c_str(), previousValue_ ? previousValue_->c_str() : "");
#else
    if (previousValue_) {
      setenv(name_.c_str(), previousValue_->c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
#endif
  }

private:
  std::string name_;
  std::optional<std::string> previousValue_;
};

} // namespace

HS_TEST(Preprocessor_ExpandsObjectFunctionAndVariadicMacros) {
  const auto result = hitsimple::preprocessor::preprocessSource(
      "$define VALUE 40\n"
      "$define ADD(a, b) a + b\n"
      "$define FIRST(name, ...) name\n"
      "new x[4] = ADD(VALUE, 2)\n"
      "new y[4] = FIRST(7, 8, 9)\n",
      "test.hs");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.source.find("new x[4]") != std::string::npos);
  HS_EXPECT_TRUE(result.source.find("40 + 2") != std::string::npos);
  HS_EXPECT_TRUE(result.source.find("new y[4]") != std::string::npos);
  HS_EXPECT_TRUE(result.source.find("7") != std::string::npos);
}

HS_TEST(Preprocessor_HandlesConditionalsAndUndef) {
  const auto result = hitsimple::preprocessor::preprocessSource(
      "$define ENABLED 1\n"
      "$ifdef ENABLED\n"
      "new x[4] = 1\n"
      "$else\n"
      "new x[4] = 2\n"
      "$endif\n"
      "$undef ENABLED\n"
      "$ifndef ENABLED\n"
      "new y[4] = 3\n"
      "$endif\n",
      "test.hs");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.source.find("new x[4] = 1") != std::string::npos);
  HS_EXPECT_TRUE(result.source.find("new x[4] = 2") == std::string::npos);
  HS_EXPECT_TRUE(result.source.find("new y[4] = 3") != std::string::npos);
}

HS_TEST(Preprocessor_ReportsErrorDirective) {
  const auto result = hitsimple::preprocessor::preprocessSource(
      "$error \"bad config\"\n",
      "test.hs");

  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("bad config") !=
                 std::string::npos);
}

HS_TEST(Preprocessor_ReportsWarningDirectiveWithoutError) {
  const auto result = hitsimple::preprocessor::preprocessSource(
      "$warning \"compat note\"\n"
      "new x[4] = 1\n",
      "test.hs");

  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_EQ(result.diagnostics[0].severity,
               hitsimple::diagnostic::Severity::Warning);
  HS_EXPECT_TRUE(result.source.find("new x[4] = 1") != std::string::npos);
}

HS_TEST(Preprocessor_TracksOutputLineOrigins) {
  const auto result = hitsimple::preprocessor::preprocessSource(
      "$define VALUE 1\n"
      "new x[4] = VALUE\n",
      "test.hs");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(!result.lineOrigins.empty());
}

HS_TEST(Preprocessor_SourceMapFeedsParserDiagnostics) {
  const auto root = std::filesystem::temp_directory_path() /
                    "hitsimple-preprocessor-parser-diagnostic";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "main.hs") << "$include \"broken.hsi\"\n";
  std::ofstream(root / "broken.hsi") << "func main(]\n";

  auto preprocessed =
      hitsimple::preprocessor::preprocessFile((root / "main.hs").string());
  HS_EXPECT_TRUE(preprocessed.diagnostics.empty());
  auto result = hitsimple::parser::parseSource(
      preprocessed.source, (root / "main.hs").string(),
      std::move(preprocessed.lineOrigins));
  std::filesystem::remove_all(root);

  HS_EXPECT_TRUE(!result.unit);
  HS_EXPECT_TRUE(result.error.find("broken.hsi:1:11:") != std::string::npos);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);

  const auto& diagnostic = result.diagnostics[0];
  HS_EXPECT_EQ(diagnostic.stage, hitsimple::diagnostic::Stage::Parser);
  HS_EXPECT_EQ(diagnostic.severity, hitsimple::diagnostic::Severity::Error);
  HS_EXPECT_TRUE(diagnostic.range.has_value());
  HS_EXPECT_EQ(diagnostic.range->begin.file, (root / "broken.hsi").string());
  HS_EXPECT_EQ(diagnostic.range->begin.line, 1U);
  HS_EXPECT_EQ(diagnostic.range->begin.column, 11U);
  HS_EXPECT_EQ(diagnostic.format(),
               (root / "broken.hsi").string() +
                   ":1:11: parser: error: " + diagnostic.message);
}

HS_TEST(Preprocessor_MapsCanonicalizedTemporarySourcePaths) {
#ifdef _WIN32
  return;
#else
  const auto root = std::filesystem::temp_directory_path() /
                    "hitsimple-preprocessor-canonical-source-map";
  const auto realTemporaryRoot = root / "real-temporary";
  const auto aliasedTemporaryRoot = root / "temporary-alias";
  const auto sourcePath = root / "source" / "broken.hs";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  std::filesystem::create_directories(realTemporaryRoot);
  std::filesystem::create_directories(sourcePath.parent_path());
  std::filesystem::create_directory_symlink(realTemporaryRoot,
                                             aliasedTemporaryRoot, error);
  HS_EXPECT_TRUE(!error);
  std::ofstream(sourcePath) << "func main(]\n";

  hitsimple::parser::ParseResult parsed;
  {
    const ScopedEnvironment temporaryDirectory(
        "TMPDIR", hitsimple::support::pathToUtf8(aliasedTemporaryRoot));
    auto preprocessed =
        hitsimple::preprocessor::preprocessFile(sourcePath.string());
    HS_EXPECT_TRUE(preprocessed.diagnostics.empty());
    parsed = hitsimple::parser::parseSource(
        preprocessed.source, sourcePath.string(),
        std::move(preprocessed.lineOrigins));
  }
  std::filesystem::remove_all(root, error);

  HS_EXPECT_TRUE(!parsed.unit);
  HS_EXPECT_EQ(parsed.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(parsed.diagnostics[0].range.has_value());
  HS_EXPECT_EQ(parsed.diagnostics[0].range->begin.file, sourcePath.string());
#endif
}

HS_TEST(Preprocessor_ExpandsNestedFunctionMacroArguments) {
  const auto result = hitsimple::preprocessor::preprocessSource(
      "$define VALUE 4\n"
      "$define ADD(a, b) a + b\n"
      "$define ID(x) x\n"
      "new x[4] = ADD(ID(1), ID(VALUE))\n",
      "test.hs");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.source.find("new x[4]") != std::string::npos);
  HS_EXPECT_TRUE(result.source.find("1 + 4") != std::string::npos);
}

HS_TEST(Preprocessor_DoesNotExpandInsideStrings) {
  const auto result = hitsimple::preprocessor::preprocessSource(
      "$define VALUE 4\n"
      "printf(\"VALUE\") // VALUE\n"
      "new x[4] = VALUE\n",
      "test.hs");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.source.find("printf(\"VALUE\")") != std::string::npos);
  HS_EXPECT_TRUE(result.source.find("printf(\"4\")") == std::string::npos);
  HS_EXPECT_TRUE(result.source.find("new x[4]") != std::string::npos);
  HS_EXPECT_TRUE(result.source.find("4") != std::string::npos);
}

HS_TEST(Preprocessor_HandlesElifOnlyUntilFirstTakenBranch) {
  const auto result = hitsimple::preprocessor::preprocessSource(
      "$define MODE 2\n"
      "$if 0\n"
      "new x[4] = 0\n"
      "$elif MODE\n"
      "new x[4] = 2\n"
      "$else\n"
      "new x[4] = 3\n"
      "$endif\n",
      "test.hs");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.source.find("new x[4] = 2") != std::string::npos);
  HS_EXPECT_TRUE(result.source.find("new x[4] = 0") == std::string::npos);
  HS_EXPECT_TRUE(result.source.find("new x[4] = 3") == std::string::npos);
}

HS_TEST(Preprocessor_ReportsDirectivePairingErrors) {
  const auto elseResult =
      hitsimple::preprocessor::preprocessSource("$else\n", "test.hs");
  HS_EXPECT_EQ(elseResult.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(!elseResult.diagnostics[0].message.empty());

  const auto unterminated = hitsimple::preprocessor::preprocessSource(
      "$ifdef MISSING\n"
      "new x[4] = 1\n",
      "test.hs");
  HS_EXPECT_EQ(unterminated.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(unterminated.diagnostics[0].message.find("unterminated") !=
                 std::string::npos);
}

HS_TEST(Preprocessor_RejectsInvalidUtf8Source) {
  std::string source = "new x[1]\n";
  source.push_back(static_cast<char>(0xFF));
  source += "\n";

  const auto result =
      hitsimple::preprocessor::preprocessSource(source, "invalid.hs");

  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].message.find("valid UTF-8") !=
                 std::string::npos);
}

HS_TEST(Preprocessor_RejectsInvalidUtf8IncludeFile) {
  const auto root = std::filesystem::temp_directory_path() /
                    "hitsimple-preprocessor-invalid-utf8";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  {
    std::ofstream(root / "main.hs") << "$include \"bad.hsi\"\n";
    std::ofstream include(root / "bad.hsi", std::ios::binary);
    include << "$define VALUE 1\n";
    include.put(static_cast<char>(0xFF));
    include << '\n';
  }

  const auto result =
      hitsimple::preprocessor::preprocessFile((root / "main.hs").string());
  std::filesystem::remove_all(root);

  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].message.find("valid UTF-8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.diagnostics[0].range &&
                 result.diagnostics[0].range->begin.file.find("bad.hsi") !=
                     std::string::npos);
}

HS_TEST(Preprocessor_RejectsBareHashInUserSource) {
  const auto hash = hitsimple::preprocessor::preprocessSource(
      "new x[1]\n"
      "# raw\n",
      "test.hs");
  HS_EXPECT_EQ(hash.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(hash.diagnostics[0].message.find("bare #") !=
                 std::string::npos);

  const auto paste = hitsimple::preprocessor::preprocessSource(
      "new x[1]\n"
      "a ## b\n",
      "test.hs");
  HS_EXPECT_EQ(paste.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(paste.diagnostics[0].message.find("bare ##") !=
                 std::string::npos);
}

HS_TEST(Preprocessor_RejectsInvalidMacroNames) {
  const auto keyword = hitsimple::preprocessor::preprocessSource(
      "$define func 1\n", "test.hs");
  HS_EXPECT_EQ(keyword.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(keyword.diagnostics[0].message.find("valid macro name") !=
                 std::string::npos);

  const auto reservedCounter = hitsimple::preprocessor::preprocessSource(
      "$ifdef t12\n"
      "$endif\n",
      "test.hs");
  HS_EXPECT_EQ(reservedCounter.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(reservedCounter.diagnostics[0].message.find(
                     "valid macro name") != std::string::npos);

  const auto underscore = hitsimple::preprocessor::preprocessSource(
      "$undef _\n", "test.hs");
  HS_EXPECT_EQ(underscore.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(underscore.diagnostics[0].message.find("valid macro name") !=
                 std::string::npos);
}

HS_TEST(Preprocessor_ProcessesQuotedRelativeIncludeFiles) {
  const auto casePath =
      std::filesystem::path(__FILE__).parent_path().parent_path() / "cases" /
      "preprocessor" / "main.hs";
  const auto result =
      hitsimple::preprocessor::preprocessFile(casePath.string());

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.source.find("func main()") != std::string::npos);
  HS_EXPECT_TRUE(result.source.find("new x[4]") != std::string::npos);
  HS_EXPECT_TRUE(result.source.find("40 + 2") != std::string::npos);
  HS_EXPECT_TRUE(!result.lineOrigins.empty());
  HS_EXPECT_TRUE(result.lineOrigins[0].file.find("main.hs") !=
                 std::string::npos);
}

HS_TEST(Preprocessor_ProcessesUnicodeInputAndIncludePaths) {
  const auto root = std::filesystem::temp_directory_path() /
                    hitsimple::support::pathFromUtf8(
                        "HitSimple 预处理 空格路径");
  const auto includeDirectory =
      root / hitsimple::support::pathFromUtf8("中文目录");
  const auto mainPath = root / hitsimple::support::pathFromUtf8("入口.hs");
  const auto includePath =
      includeDirectory / hitsimple::support::pathFromUtf8("数值.hsi");
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(includeDirectory);
  std::ofstream(mainPath) << "$include \"中文目录/数值.hsi\"\n"
                            "new x[4] = VALUE\n";
  std::ofstream(includePath) << "$define VALUE 42\n";

  const auto result = hitsimple::preprocessor::preprocessFile(
      hitsimple::support::pathToUtf8(mainPath));
  std::filesystem::remove_all(root);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.source.find("new x[4]") != std::string::npos);
  HS_EXPECT_TRUE(result.source.find("42") != std::string::npos);
  HS_EXPECT_TRUE(!result.lineOrigins.empty());
  HS_EXPECT_TRUE(result.lineOrigins[0].file.find("入口.hs") !=
                 std::string::npos);
}

HS_TEST(Preprocessor_AcceptsDirectiveWithoutSpaceAfterDollar) {
  const auto result = hitsimple::preprocessor::preprocessSource(
      "$define VALUE 9\n"
      "$if defined(VALUE)\n"
      "new x[4] = VALUE\n"
      "$endif\n",
      "test.hs");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.source.find("new x[4]") != std::string::npos);
  HS_EXPECT_TRUE(result.source.find("9") != std::string::npos);
}

HS_TEST(Preprocessor_AcceptsWhitespaceAfterDollar) {
  const auto result = hitsimple::preprocessor::preprocessSource(
      "$\tdefine VALUE 5\n"
      "$ if defined(VALUE)\n"
      "new x[4] = VALUE\n"
      "$ endif\n",
      "test.hs");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.source.find("new x[4]") != std::string::npos);
  HS_EXPECT_TRUE(result.source.find("5") != std::string::npos);
}

HS_TEST(Preprocessor_HandlesStringizeTokenPasteFileAndLine) {
  const auto result = hitsimple::preprocessor::preprocessSource(
      "$define STRINGIFY(x) #x\n"
      "$define CONCAT(a, b) a##b\n"
      "printf(STRINGIFY(hello world))\n"
      "new x[4] = CONCAT(4, 2)\n"
      "printf(__FILE__)\n"
      "new y[4] = __LINE__\n",
      "features.hs");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.source.find("\"hello world\"") != std::string::npos);
  HS_EXPECT_TRUE(result.source.find("new x[4] =  42") != std::string::npos);
  HS_EXPECT_TRUE(result.source.find("\"features.hs\"") != std::string::npos);
  HS_EXPECT_TRUE(result.source.find("new y[4] =  6") != std::string::npos);
}

HS_TEST(Preprocessor_ProcessesSystemIncludePath) {
  const auto result = hitsimple::preprocessor::preprocessSource(
      "$include <stdlib.hsh>\n"
      "$if defined(HITSIMPLE_STDLIB_HSH)\n"
      "new x[4] = HITSIMPLE_STDLIB_HSH\n"
      "$endif\n",
      "test.hs");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_EQ(result.standardHeaders.size(), 1U);
  HS_EXPECT_EQ(result.standardHeaders[0], hitsimple::stdlib::StandardHeader::Stdlib);
  HS_EXPECT_TRUE(result.source.find("new x[4]") != std::string::npos);
  HS_EXPECT_TRUE(result.source.find("1") != std::string::npos);
}

HS_TEST(Preprocessor_DefinesHostOperatingSystemAndArchitectureMacros) {
#if defined(_WIN32)
  std::string source =
      "$ifndef _WIN32\n"
      "$error \"missing Windows preprocessor macro\"\n"
      "$endif\n";
#if defined(_WIN64)
  source +=
      "$ifndef _WIN64\n"
      "$error \"missing Windows x64 preprocessor macro\"\n"
      "$endif\n";
#endif
#elif defined(__APPLE__)
  std::string source =
      "$ifndef __APPLE__\n"
      "$error \"missing Darwin preprocessor macro\"\n"
      "$endif\n";
#else
  std::string source =
      "$ifndef __linux__\n"
      "$error \"missing Linux preprocessor macro\"\n"
      "$endif\n";
#if defined(__x86_64__)
  source +=
      "$ifndef __x86_64__\n"
      "$error \"missing x86_64 preprocessor macro\"\n"
      "$endif\n";
#elif defined(__aarch64__)
  source +=
      "$ifndef __aarch64__\n"
      "$error \"missing aarch64 preprocessor macro\"\n"
      "$endif\n";
#endif
#endif
  source += "func main() { return 0 }\n";

  const auto result =
      hitsimple::preprocessor::preprocessSource(source, "test.hs");
  HS_EXPECT_TRUE(result.diagnostics.empty());
}

HS_TEST(Preprocessor_RejectsRemovedCoreStandardHeader) {
  const auto result = hitsimple::preprocessor::preprocessSource(
      "$include <core.hsh>\n", "test.hs");

  HS_EXPECT_TRUE(!result.diagnostics.empty());
  HS_EXPECT_TRUE(result.diagnostics[0].message.find("was removed") !=
                 std::string::npos);
}

HS_TEST(Preprocessor_TracksNestedIncludeSourceMap) {
  const auto root = std::filesystem::temp_directory_path() /
                    "hitsimple-preprocessor-nested";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "inner");
  {
    std::ofstream(root / "main.hs")
        << "$include \"inner/value.hsi\"\n"
        << "new x[4] = INCLUDED_VALUE\n";
    std::ofstream(root / "inner" / "value.hsi")
        << "$define INCLUDED_VALUE 11\n"
        << "new y[4] = INCLUDED_VALUE\n";
  }

  const auto result =
      hitsimple::preprocessor::preprocessFile((root / "main.hs").string());
  std::filesystem::remove_all(root);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.source.find("new y[4]") != std::string::npos);
  HS_EXPECT_TRUE(result.source.find("11") != std::string::npos);
  HS_EXPECT_TRUE(!result.lineOrigins.empty());
  HS_EXPECT_TRUE(result.lineOrigins[0].file.find("value.hsi") !=
                 std::string::npos);
}
