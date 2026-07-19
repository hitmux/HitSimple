#include "support/TestRunner.h"

#include "hitsimple/diagnostic/Diagnostic.h"
#include "hitsimple/diagnostic/SourceLocation.h"

#include <filesystem>
#include <fstream>
#include <string>

using hitsimple::diagnostic::Diagnostic;
using hitsimple::diagnostic::Stage;
using hitsimple::diagnostic::SourceLocation;
using hitsimple::diagnostic::SourceRange;

HS_TEST(SourceLocation_DefaultsToFirstLineAndColumn) {
  const SourceLocation location;

  HS_EXPECT_EQ(location.line, 1U);
  HS_EXPECT_EQ(location.column, 1U);
  HS_EXPECT_TRUE(location.file.empty());
}

HS_TEST(SourceRange_StoresBeginAndEndLocations) {
  SourceRange range;
  range.begin.file = "example.hs";
  range.begin.line = 2;
  range.begin.column = 5;
  range.end.file = "example.hs";
  range.end.line = 2;
  range.end.column = 8;

  HS_EXPECT_EQ(range.begin.file, "example.hs");
  HS_EXPECT_EQ(range.begin.line, 2U);
  HS_EXPECT_EQ(range.begin.column, 5U);
  HS_EXPECT_EQ(range.end.file, "example.hs");
  HS_EXPECT_EQ(range.end.line, 2U);
  HS_EXPECT_EQ(range.end.column, 8U);
}

HS_TEST(Diagnostic_FormatsStageSeverityAndMessage) {
  const auto diagnostic = Diagnostic::error(Stage::Sema, "undeclared variable");

  HS_EXPECT_EQ(diagnostic.str(), "undeclared variable");
  HS_EXPECT_TRUE(diagnostic.format().find("sema: error: undeclared variable") !=
                 std::string::npos);
  HS_EXPECT_TRUE(diagnostic.find("undeclared") != std::string::npos);
}

HS_TEST(Diagnostic_FormatsSourceLocationWhenPresent) {
  auto diagnostic = Diagnostic::error(Stage::Parser, "unexpected token");
  SourceRange range;
  range.begin.file = "example.hs";
  range.begin.line = 3;
  range.begin.column = 7;
  range.end = range.begin;
  diagnostic.range = range;

  HS_EXPECT_EQ(diagnostic.format(),
               "example.hs:3:7: parser: error: unexpected token");
}

HS_TEST(Diagnostic_FormatsPrimaryAndRelatedLocationsAsJson) {
  auto diagnostic = Diagnostic::error(Stage::Sema, "duplicate declaration \"x\"");
  diagnostic.range = SourceRange{{"example.hs", 3, 5}, {"example.hs", 3, 10}};
  diagnostic.labels.push_back(
      {SourceRange{{"example.hs", 2, 5}, {"example.hs", 2, 10}},
       "first declaration is here"});

  const auto json = diagnostic.formatJson();
  HS_EXPECT_TRUE(json.find("\"severity\":\"error\"") != std::string::npos);
  HS_EXPECT_TRUE(json.find("\"stage\":\"sema\"") != std::string::npos);
  HS_EXPECT_TRUE(json.find("duplicate declaration \\\"x\\\"") !=
                 std::string::npos);
  HS_EXPECT_TRUE(json.find("\"primary\":{\"begin\":{\"file\":\"example.hs\",\"line\":3,\"column\":5}") !=
                 std::string::npos);
  HS_EXPECT_TRUE(json.find("\"related\":[{\"message\":\"first declaration is here\"") !=
                 std::string::npos);
}

HS_TEST(Diagnostic_FormatsMissingPrimaryAsJsonNull) {
  const auto diagnostic = Diagnostic::error(Stage::Cli, "missing input file");

  HS_EXPECT_TRUE(diagnostic.formatJson().find("\"primary\":null") !=
                 std::string::npos);
}

HS_TEST(Diagnostic_UsesPreprocessorStageName) {
  HS_EXPECT_EQ(hitsimple::diagnostic::stageName(Stage::Preprocessor),
               "preprocessor");
}

HS_TEST(Diagnostic_RendersSingleLineSourceExcerptWithTabs) {
  const auto path = std::filesystem::temp_directory_path() /
                    "hitsimple-diagnostic-source-excerpt.hs";
  {
    std::ofstream output(path, std::ios::binary);
    output << "\tinvalid token\n";
  }

  auto diagnostic = Diagnostic::error(Stage::Lexer, "invalid token");
  diagnostic.range = SourceRange{{path.string(), 1, 2},
                                 {path.string(), 1, 9}};
  const auto excerpt = hitsimple::diagnostic::renderSourceExcerpt(diagnostic);

  HS_EXPECT_EQ(excerpt, "      invalid token\n      ^~~~~~~");
  HS_EXPECT_EQ(diagnostic.format(),
               path.string() + ":1:2: lexer: error: invalid token");
  std::filesystem::remove(path);
}

HS_TEST(Diagnostic_SafelySkipsUnavailableSourceExcerpt) {
  auto diagnostic = Diagnostic::error(Stage::Sema, "undeclared variable");
  diagnostic.range = SourceRange{{"/does/not/exist.hs", 1, 1},
                                 {"/does/not/exist.hs", 1, 1}};

  HS_EXPECT_TRUE(hitsimple::diagnostic::renderSourceExcerpt(diagnostic).empty());
}
