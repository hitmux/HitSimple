#include "support/TestRunner.h"

#include "hitsimple/parser/Parser.h"
#include "hitsimple/sema/Sema.h"
#include "safety/StaticSafetyAnalyzer.h"

#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<hitsimple::stdlib::StandardHeader> allStandardHeaders() {
  const auto headers = hitsimple::stdlib::allStandardHeaders();
  return {headers.begin(), headers.end()};
}

hitsimple::safety::StaticSafetyResult
analyzeSafety(std::string_view source,
              hitsimple::safety::StaticSafetyOptions options = {true, false}) {
  auto parseResult = hitsimple::parser::parseSource(source, "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());

  auto analyzeResult = hitsimple::sema::analyze(
      *parseResult.unit,
      hitsimple::sema::AnalyzeOptions{true, allStandardHeaders()});
  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());

  return hitsimple::safety::analyzeStaticSafety(*analyzeResult.unit, options);
}

bool hasDiagnostic(const hitsimple::safety::StaticSafetyResult& result,
                   std::string_view message) {
  for (const auto& diagnostic : result.diagnostics) {
    if (diagnostic.message.find(message) != std::string::npos) {
      return true;
    }
  }
  return false;
}

} // namespace

HS_TEST(StaticSafetyAnalyzer_ReportsConstantErrorsAsCodegenDiagnostics) {
  constexpr std::string_view source = "func main() -> i32 {\n"
                                      "    new divisor as i32 = 0\n"
                                      "    return 1 / divisor\n"
                                      "}\n";

  const auto unchecked = analyzeSafety(source, {false, false});
  const auto staticChecked = analyzeSafety(source, {true, false});
  const auto checked = analyzeSafety(source, {true, true});

  HS_EXPECT_TRUE(unchecked.diagnostics.empty());
  HS_EXPECT_EQ(staticChecked.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(hasDiagnostic(staticChecked, "division by zero"));
  HS_EXPECT_TRUE(staticChecked.diagnostics.front().stage ==
                 hitsimple::diagnostic::Stage::Codegen);
  HS_EXPECT_TRUE(staticChecked.diagnostics.front().range.has_value());
  HS_EXPECT_EQ(staticChecked.diagnostics.front().range->begin.line, 3U);
  HS_EXPECT_TRUE(hasDiagnostic(checked, "division by zero"));
}

HS_TEST(StaticSafetyAnalyzer_TracksRangeLifetimeAndCStringFacts) {
  const auto outOfBounds = analyzeSafety(
      "func main() -> i32 {\n"
      "    new bytes[1] as bytes\n"
      "    return [2]*&bytes\n"
      "}\n");
  const auto expiredLocal = analyzeSafety(
      "func main() -> i32 {\n"
      "    new p as addr = 0\n"
      "    if (true) {\n"
      "        new value[1] as bytes = 'X'\n"
      "        p = &value\n"
      "    }\n"
      "    return [1]*p\n"
      "}\n");
  const auto unterminatedCString = analyzeSafety(
      "func main() -> i32 {\n"
      "    new raw[1] as bytes = 'A'\n"
      "    set raw as cstr\n"
      "    return raw == \"A\"\n"
      "}\n");

  HS_EXPECT_TRUE(hasDiagnostic(outOfBounds, "memory load out of bounds"));
  HS_EXPECT_TRUE(hasDiagnostic(expiredLocal, "use after scope exit dereference"));
  HS_EXPECT_TRUE(
      hasDiagnostic(unterminatedCString, "missing terminating byte in cstr"));
}

HS_TEST(StaticSafetyAnalyzer_MergesBranchLoopAndGotoState) {
  const auto divergentBranches = analyzeSafety(
      "func divide(flag as i32) -> i32 {\n"
      "    new divisor as i32 = 1\n"
      "    if (flag) {\n"
      "        divisor = 0\n"
      "    } else {\n"
      "        divisor = 1\n"
      "    }\n"
      "    return 1 / divisor\n"
      "}\n"
      "func main() {\n"
      "    return divide(0)\n"
      "}\n");
  const auto loopWrite = analyzeSafety(
      "func divide(flag as i32) -> i32 {\n"
      "    new divisor as i32 = 1\n"
      "    while (flag) {\n"
      "        divisor = 0\n"
      "    }\n"
      "    return 1 / divisor\n"
      "}\n"
      "func main() {\n"
      "    return divide(0)\n"
      "}\n");
  const auto skippedAssignment = analyzeSafety(
      "func main() -> i32 {\n"
      "    new small[1] as bytes\n"
      "    new large[8] as bytes\n"
      "    new p as addr = &small\n"
      "    goto done\n"
      "    p = &large\n"
      "    done: return [2]*p\n"
      "}\n");

  HS_EXPECT_TRUE(divergentBranches.diagnostics.empty());
  HS_EXPECT_TRUE(loopWrite.diagnostics.empty());
  HS_EXPECT_TRUE(hasDiagnostic(skippedAssignment, "memory load out of bounds"));
}

HS_TEST(StaticSafetyAnalyzer_HonorsLogicalShortCircuitEffects) {
  const auto falseAnd = analyzeSafety(
      "func main() -> i32 {\n"
      "    new small[1] as bytes\n"
      "    new large[8] as bytes\n"
      "    new p as addr = &small\n"
      "    if (false && (p = &large)) {}\n"
      "    return [2]*p\n"
      "}\n");
  const auto unknownAnd = analyzeSafety(
      "func main(flag as bool) -> i32 {\n"
      "    new small[1] as bytes\n"
      "    new large[8] as bytes\n"
      "    new p as addr = &small\n"
      "    if (flag && (p = &large)) {}\n"
      "    return [2]*p\n"
      "}\n");

  HS_EXPECT_TRUE(hasDiagnostic(falseAnd, "memory load out of bounds"));
  HS_EXPECT_TRUE(unknownAnd.diagnostics.empty());
}

HS_TEST(StaticSafetyAnalyzer_InvalidatesFactsAfterOrdinaryCalls) {
  const auto result = analyzeSafety(
      "new large[8] as bytes\n"
      "new small[1] as bytes\n"
      "new p as addr = &large\n"
      "func poison() {\n"
      "    p = &small\n"
      "}\n"
      "func main() -> i32 {\n"
      "    poison()\n"
      "    return [2]*p\n"
      "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
}

HS_TEST(StaticSafetyAnalyzer_ReportsUseAfterFreeThroughAddressAlias) {
  const auto result = analyzeSafety(
      "func main() -> i32 {\n"
      "    new p as addr = alloc(8)\n"
      "    new alias as addr = p\n"
      "    free(p)\n"
      "    return [1]*alias\n"
      "}\n");

  HS_EXPECT_TRUE(hasDiagnostic(result, "use after free dereference"));
}

HS_TEST(StaticSafetyAnalyzer_MergesDivergentTryCatchAddressRanges) {
  const auto result = analyzeSafety(
      "func main() -> i32 {\n"
      "    new small[1] as bytes\n"
      "    new large[8] as bytes\n"
      "    new p as addr = &small\n"
      "    try {\n"
      "        p = &large\n"
      "    } catch (error[1]) {\n"
      "        p = &small\n"
      "    }\n"
      "    return [2]*p\n"
      "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
}

HS_TEST(StaticSafetyAnalyzer_InvalidatesFactsAfterIndirectMemoryWrite) {
  const auto result = analyzeSafety(
      "func main() -> i32 {\n"
      "    new small[1] as bytes\n"
      "    new p as addr = &small\n"
      "    memset(&p, 0, 8)\n"
      "    return [2]*p\n"
      "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
}
