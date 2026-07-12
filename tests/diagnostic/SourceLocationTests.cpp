#include "support/TestRunner.h"

#include "hitsimple/diagnostic/Diagnostic.h"
#include "hitsimple/diagnostic/SourceLocation.h"

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
