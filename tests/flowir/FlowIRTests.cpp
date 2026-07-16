#include "sema/SemaTestSupport.h"

#include "hitsimple/flowir/Builder.h"
#include "hitsimple/flowir/Analysis.h"
#include "hitsimple/flowir/Dump.h"
#include "hitsimple/flowir/Serialization.h"
#include "hitsimple/flowir/Verifier.h"

#include <string>

using hitsimple::testing::sema::analyzeSource;

namespace {

const hitsimple::flowir::Module &buildVerified(std::string_view source,
                                                hitsimple::flowir::Module &out) {
  auto analyzed = analyzeSource(source);
  HS_EXPECT_TRUE(analyzed.unit != nullptr);
  HS_EXPECT_TRUE(analyzed.diagnostics.empty());
  auto built = hitsimple::flowir::build(*analyzed.unit);
  HS_EXPECT_TRUE(built.diagnostics.empty());
  HS_EXPECT_TRUE(built.module.has_value());
  out = std::move(*built.module);
  const auto verifierDiagnostics = hitsimple::flowir::verify(out);
  HS_EXPECT_TRUE(verifierDiagnostics.empty());
  return out;
}

} // namespace

HS_TEST(FlowIR_BuildsDeterministicCfgViewsAndExceptionalEdges) {
  constexpr std::string_view source =
      "func main() -> i32 {\n"
      "    new sum as i32 = 0\n"
      "    for (new i as i32 = 0; i < 3; i++) {\n"
      "        if (i == 1) {\n"
      "            continue\n"
      "        }\n"
      "        sum = sum + i\n"
      "    }\n"
      "    try {\n"
      "        throw sum\n"
      "    } catch (error as i32) {\n"
      "        sum = error\n"
      "    }\n"
      "    return sum\n"
      "}\n";

  hitsimple::flowir::Module first;
  hitsimple::flowir::Module second;
  buildVerified(source, first);
  buildVerified(source, second);

  const auto firstDump = hitsimple::flowir::dumpToString(first);
  HS_EXPECT_EQ(firstDump, hitsimple::flowir::dumpToString(second));
  HS_EXPECT_EQ(hitsimple::flowir::serialize(first),
               hitsimple::flowir::serialize(second));
  HS_EXPECT_TRUE(firstDump.find("opcode=lifetime_start") != std::string::npos);
  HS_EXPECT_TRUE(firstDump.find("kind=exceptional") != std::string::npos);
  HS_EXPECT_TRUE(!first.sourceMaps.empty());
  HS_EXPECT_TRUE(firstDump.find("file=test.hs") != std::string::npos);
  const auto stats = hitsimple::flowir::statistics(first);
  HS_EXPECT_TRUE(stats.blockCount > 1U);
  HS_EXPECT_TRUE(stats.instructionCount > stats.blockCount);
}

HS_TEST(FlowIR_VerifierRejectsInvalidCfgAndViewContracts) {
  hitsimple::flowir::Module invalid;
  invalid.strings.push_back("main");
  invalid.functions.push_back(hitsimple::flowir::FunctionRecord{
      0, 0, 0, 1, 0, 0, 0, 0, hitsimple::flowir::InvalidId});
  invalid.blocks.push_back(hitsimple::flowir::BlockRecord{
      0, 0, 1, 0, 0, 0, 0});
  invalid.templates.push_back(hitsimple::flowir::TemplateRecord{0, 4, false});
  invalid.views.push_back(hitsimple::flowir::ViewRecord{
      0, 8, hitsimple::flowir::ValueCategory::RValue, 0,
      hitsimple::flowir::InvalidId, 0});

  const auto diagnostics = hitsimple::flowir::verify(invalid);
  HS_EXPECT_TRUE(!diagnostics.empty());
}

HS_TEST(FlowIR_DeterministicGeneratedProgramsMaintainInvariants) {
  for (int value = 0; value < 24; ++value) {
    const std::string source =
        "func main() -> i32 {\n"
        "    new value as i32 = " + std::to_string(value) + "\n"
        "    if (value) {\n"
        "        value = value - 1\n"
        "    } else {\n"
        "        value = value + 1\n"
        "    }\n"
        "    return value\n"
        "}\n";
    hitsimple::flowir::Module module;
    buildVerified(source, module);
  }
}

HS_TEST(FlowIR_CpuReferenceAnalysesAreStableAcrossWorkersAndCacheInvalidation) {
  constexpr std::string_view source =
      "extern unknown_external(value[4]) -> [4]\n"
      "func helper(value[4]) -> [4] {\n"
      "    new result[4] = value\n"
      "    while (result < 3) {\n"
      "        result = result + 1\n"
      "    }\n"
      "    return unknown_external(result)\n"
      "}\n"
      "func main() -> i32 {\n"
      "    new value[4] = helper(0)\n"
      "    try {\n"
      "        throw value\n"
      "    } catch (error[4]) {\n"
      "        value = error\n"
      "    }\n"
      "    return value\n"
      "}\n";

  hitsimple::flowir::Module module;
  buildVerified(source, module);
  const auto single = hitsimple::flowir::analyze(
      module, hitsimple::flowir::AnalysisOptions{1});
  const auto parallel = hitsimple::flowir::analyze(
      module, hitsimple::flowir::AnalysisOptions{4});
  HS_EXPECT_TRUE(single.diagnostics.empty());
  HS_EXPECT_TRUE(parallel.diagnostics.empty());
  HS_EXPECT_EQ(hitsimple::flowir::serializeAnalysis(single),
               hitsimple::flowir::serializeAnalysis(parallel));
  const auto dump = hitsimple::flowir::dumpAnalysisToString(single);
  HS_EXPECT_TRUE(dump.find("Reachability") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("Liveness") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("Initialized") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("Effects") != std::string::npos);

  hitsimple::flowir::AnalysisCache cache;
  const auto& first = cache.analyze(module);
  HS_EXPECT_TRUE(cache.validFor(module));
  const auto firstBytes = hitsimple::flowir::serializeAnalysis(first);
  const auto& second = cache.analyze(module, hitsimple::flowir::AnalysisOptions{4});
  HS_EXPECT_EQ(firstBytes, hitsimple::flowir::serializeAnalysis(second));
  cache.invalidate();
  HS_EXPECT_TRUE(!cache.validFor(module));
}

HS_TEST(FlowIR_EffectSummaryUsesExplicitExternContract) {
  constexpr std::string_view source =
      "extern observe(src[P] as addr, len as u64) -> () "
      "effects(read(src, len), nothrow)\n"
      "func main() -> i32 {\n"
      "    return 0\n"
      "}\n";

  hitsimple::flowir::Module module;
  buildVerified(source, module);
  const auto result = hitsimple::flowir::analyze(module);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_EQ(result.effects.size(), module.functions.size());
  HS_EXPECT_TRUE(result.effects.front().hasExplicitContract);
  HS_EXPECT_TRUE((result.effects.front().flags & hitsimple::flowir::EffectRead) != 0U);
  HS_EXPECT_TRUE((result.effects.front().flags & hitsimple::flowir::EffectUnknown) == 0U);
}

HS_TEST(FlowIR_EffectSummaryConvergesForRecursiveScc) {
  constexpr std::string_view source =
      "func first(value[4]) -> [4] {\n"
      "    return second(value)\n"
      "}\n"
      "func second(value[4]) -> [4] {\n"
      "    return first(value)\n"
      "}\n"
      "func main() -> i32 {\n"
      "    return 0\n"
      "}\n";

  hitsimple::flowir::Module module;
  buildVerified(source, module);
  const auto single = hitsimple::flowir::analyze(module);
  const auto parallel = hitsimple::flowir::analyze(
      module, hitsimple::flowir::AnalysisOptions{3});
  HS_EXPECT_TRUE(single.diagnostics.empty());
  HS_EXPECT_EQ(hitsimple::flowir::serializeAnalysis(single),
               hitsimple::flowir::serializeAnalysis(parallel));
  HS_EXPECT_TRUE(single.effects[0].callees.size() == 1U);
  HS_EXPECT_TRUE(single.effects[1].callees.size() == 1U);
}
