#pragma once

#include "support/TestRunner.h"

#include "hitsimple/parser/Parser.h"
#include "hitsimple/preprocessor/Preprocessor.h"
#include "hitsimple/sema/Sema.h"

#include <string>
#include <string_view>
#include <utility>

namespace hitsimple::testing::sema {

inline std::vector<hitsimple::stdlib::StandardHeader> allStandardHeaders() {
  const auto headers = hitsimple::stdlib::allStandardHeaders();
  return {headers.begin(), headers.end()};
}

inline hitsimple::sema::AnalyzeResult analyzeSource(std::string_view source) {
  auto parseResult = hitsimple::parser::parseSource(source, "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());
  return hitsimple::sema::analyze(
      *parseResult.unit,
      hitsimple::sema::AnalyzeOptions{true, allStandardHeaders()});
}

inline hitsimple::sema::AnalyzeResult
analyzePreprocessedSource(std::string_view source) {
  auto preprocessed =
      hitsimple::preprocessor::preprocessSource(std::string(source), "test.hs");
  HS_EXPECT_TRUE(preprocessed.diagnostics.empty());
  auto parseResult = hitsimple::parser::parseSource(
      preprocessed.source, "test.hs", std::move(preprocessed.lineOrigins));
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());
  return hitsimple::sema::analyze(
      *parseResult.unit,
      hitsimple::sema::AnalyzeOptions{true,
                                       std::move(preprocessed.standardHeaders)});
}

inline constexpr std::string_view minimalProgram = "func main() {\n"
                                                   "    new x[1]\n"
                                                   "    x %d= 42\n"
                                                   "    printf(\"%d\\n\", x)\n"
                                                   "    return 0\n"
                                                   "}\n";

} // namespace hitsimple::testing::sema
