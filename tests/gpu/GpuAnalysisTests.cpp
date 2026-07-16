#include "sema/SemaTestSupport.h"

#include "hitsimple/flowir/Analysis.h"
#include "hitsimple/flowir/Builder.h"
#include "hitsimple/flowir/Serialization.h"
#include "hitsimple/flowir/Verifier.h"
#include "hitsimple/gpu/GpuAnalysis.h"

#include "../../src/gpu/GpuAnalysisInternal.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using hitsimple::testing::sema::analyzeSource;

namespace {

hitsimple::flowir::Module buildModule(std::string_view source) {
  auto analyzed = analyzeSource(source);
  HS_EXPECT_TRUE(analyzed.unit != nullptr);
  HS_EXPECT_TRUE(analyzed.diagnostics.empty());
  auto built = hitsimple::flowir::build(*analyzed.unit);
  HS_EXPECT_TRUE(built.diagnostics.empty());
  HS_EXPECT_TRUE(built.module.has_value());
  auto module = std::move(*built.module);
  HS_EXPECT_TRUE(hitsimple::flowir::verify(module).empty());
  return module;
}

} // namespace

HS_TEST(GpuAnalysis_CpuModeUsesTheReferenceResultWithoutADevice) {
  constexpr std::string_view source =
      "func main() -> i32 {\n"
      "    new value as i32 = 0\n"
      "    while (value < 4) {\n"
      "        value = value + 1\n"
      "    }\n"
      "    return value\n"
      "}\n";

  const auto module = buildModule(source);
  const auto reference = hitsimple::flowir::analyze(module);
  const auto execution = hitsimple::gpu::analyzeWithGpu(
      module, hitsimple::gpu::GpuAnalysisOptions{
                  hitsimple::gpu::GpuAnalysisMode::Cpu});

  HS_EXPECT_TRUE(reference.diagnostics.empty());
  HS_EXPECT_TRUE(execution.analysis.diagnostics.empty());
  HS_EXPECT_EQ(hitsimple::flowir::serializeAnalysis(reference),
               hitsimple::flowir::serializeAnalysis(execution.analysis));
  HS_EXPECT_EQ(execution.report.executedBackend,
               hitsimple::gpu::GpuAnalysisBackend::Cpu);
  HS_EXPECT_EQ(execution.report.fallbackReason,
               hitsimple::gpu::GpuFallbackReason::Disabled);
}

HS_TEST(GpuAnalysis_AutoModeIsDifferentiallyCheckedOrFallsBack) {
  constexpr std::string_view source =
      "func helper(value[4]) -> [4] {\n"
      "    new result[4] = value\n"
      "    if (result) {\n"
      "        result = result + 1\n"
      "    }\n"
      "    return result\n"
      "}\n"
      "func main() -> i32 {\n"
      "    new value[4] = helper(0)\n"
      "    return value\n"
      "}\n";

  const auto module = buildModule(source);
  const auto reference = hitsimple::flowir::analyze(module);
  const auto execution = hitsimple::gpu::analyzeWithGpu(module);

  HS_EXPECT_TRUE(reference.diagnostics.empty());
  HS_EXPECT_TRUE(execution.analysis.diagnostics.empty());
  HS_EXPECT_EQ(hitsimple::flowir::serializeAnalysis(reference),
               hitsimple::flowir::serializeAnalysis(execution.analysis));
  if (execution.report.executedBackend == hitsimple::gpu::GpuAnalysisBackend::OpenCl) {
    HS_EXPECT_TRUE(execution.report.gpuFactsVerified);
    HS_EXPECT_EQ(execution.report.fallbackReason,
                 hitsimple::gpu::GpuFallbackReason::None);
  } else {
    HS_EXPECT_TRUE(execution.report.fallbackReason !=
                   hitsimple::gpu::GpuFallbackReason::None);
  }
}

HS_TEST(GpuAnalysis_ResidentAnalyzerReusesVerifiedOpenClBuffers) {
  constexpr std::string_view source =
      "func main() -> i32 {\n"
      "    new bits as u32 = 1065353216\n"
      "    new value as f32 = bits as f32\n"
      "    return 0\n"
      "}\n";

  const auto module = buildModule(source);
  const auto reference = hitsimple::flowir::analyze(module);
  hitsimple::gpu::GpuAnalysisOptions options;
  options.mode = hitsimple::gpu::GpuAnalysisMode::OpenCl;
  hitsimple::gpu::GpuAnalyzer analyzer(options);
  const auto first = analyzer.analyze(module);
  const auto second = analyzer.analyze(module);

  HS_EXPECT_EQ(hitsimple::flowir::serializeAnalysis(reference),
               hitsimple::flowir::serializeAnalysis(first.analysis));
  HS_EXPECT_EQ(hitsimple::flowir::serializeAnalysis(reference),
               hitsimple::flowir::serializeAnalysis(second.analysis));
  if (first.report.executedBackend == hitsimple::gpu::GpuAnalysisBackend::OpenCl) {
    HS_EXPECT_TRUE(first.report.gpuFactsVerified);
    HS_EXPECT_EQ(first.report.fallbackReason, hitsimple::gpu::GpuFallbackReason::None);
    HS_EXPECT_EQ(second.report.executedBackend, hitsimple::gpu::GpuAnalysisBackend::OpenCl);
    HS_EXPECT_TRUE(second.report.gpuFactsVerified);
    HS_EXPECT_EQ(second.report.fallbackReason, hitsimple::gpu::GpuFallbackReason::None);
    HS_EXPECT_TRUE(second.report.reusedResidentBuffers);
  } else {
    HS_EXPECT_TRUE(first.report.fallbackReason != hitsimple::gpu::GpuFallbackReason::None);
    HS_EXPECT_TRUE(second.report.fallbackReason != hitsimple::gpu::GpuFallbackReason::None);
  }
}

HS_TEST(GpuAnalysis_InjectedOpenClFailuresFallBackToTheCpuReference) {
  constexpr std::string_view source =
      "func main() -> i32 {\n"
      "    new value[4] = 1\n"
      "    while (value < 4) {\n"
      "        value = value + 1\n"
      "    }\n"
      "    return value\n"
      "}\n";

  const auto module = buildModule(source);
  const auto reference = hitsimple::flowir::analyze(module);
  const std::vector<std::pair<hitsimple::gpu::GpuAnalysisTestFault,
                              hitsimple::gpu::GpuFallbackReason>> cases{
      {hitsimple::gpu::GpuAnalysisTestFault::ProgramBuild,
       hitsimple::gpu::GpuFallbackReason::KernelFailure},
      {hitsimple::gpu::GpuAnalysisTestFault::OutOfMemory,
       hitsimple::gpu::GpuFallbackReason::OutOfMemory},
      {hitsimple::gpu::GpuAnalysisTestFault::KernelFailure,
       hitsimple::gpu::GpuFallbackReason::KernelFailure},
  };
  for (const auto& [fault, expectedReason] : cases) {
    hitsimple::gpu::GpuAnalysisOptions options;
    options.mode = hitsimple::gpu::GpuAnalysisMode::OpenCl;
    options.testFault = fault;
    const auto execution = hitsimple::gpu::analyzeWithGpu(module, options);
    HS_EXPECT_EQ(hitsimple::flowir::serializeAnalysis(reference),
                 hitsimple::flowir::serializeAnalysis(execution.analysis));
    if (execution.report.device) {
      HS_EXPECT_EQ(execution.report.fallbackReason, expectedReason);
      HS_EXPECT_TRUE(execution.report.detail.find("test-injected") != std::string::npos);
    } else {
      HS_EXPECT_TRUE(execution.report.fallbackReason !=
                     hitsimple::gpu::GpuFallbackReason::None);
    }
  }
}

HS_TEST(GpuAnalysis_PackedViewRangeSubsetMatchesTheCpuReference) {
  constexpr std::string_view source =
      "func main() -> i32 {\n"
      "    new bits as u32 = 1065353216\n"
      "    new value as f32 = bits as f32\n"
      "    return 0\n"
      "}\n";

  const auto module = buildModule(source);
  const auto reference = hitsimple::flowir::analyze(module);
  hitsimple::gpu::detail::PackedAnalysisInput input;
  std::string error;
  HS_EXPECT_TRUE(hitsimple::gpu::detail::packAnalysisInput(module, input, error));
  HS_EXPECT_TRUE(error.empty());
  HS_EXPECT_TRUE(!input.rangeSources.empty());

  std::vector<std::uint32_t> reachable(input.blockCount, 0U);
  for (const auto& fact : reference.reachability) {
    reachable[fact.block] = fact.reachable ? 1U : 0U;
  }
  std::vector<std::uint32_t> liveIn(input.uses.size(), 0U);
  for (const auto& fact : reference.liveness) {
    const auto base = static_cast<std::size_t>(fact.block) * input.livenessWordCount;
    for (const auto value : fact.liveIn) {
      liveIn[base + value / 32U] |= 1U << (value % 32U);
    }
  }
  std::vector<std::uint32_t> states(input.valueCount, 0U);
  std::vector<std::uint32_t> objects(input.valueCount, hitsimple::flowir::InvalidId);
  std::vector<std::uint32_t> offsets(input.valueCount, 0U);
  std::vector<std::uint32_t> lengths(input.valueCount, 0U);
  for (const auto& fact : reference.viewRanges) {
    states[fact.value] = fact.state == hitsimple::flowir::RangeState::Known ? 1U : 0U;
    objects[fact.value] = fact.object;
    offsets[fact.value] = fact.offset;
    lengths[fact.value] = fact.byteLength;
  }
  const auto facts = hitsimple::gpu::detail::unpackGpuDataflowFacts(
      module, input, reachable, liveIn, states, objects, offsets, lengths);
  HS_EXPECT_TRUE(hitsimple::gpu::detail::matchesReferenceFacts(facts, reference));
}

HS_TEST(GpuAnalysis_ReportRecordsFallbackAndStageTimings) {
  constexpr std::string_view source =
      "func main() -> i32 {\n"
      "    return 0\n"
      "}\n";

  const auto module = buildModule(source);
  const auto execution = hitsimple::gpu::analyzeWithGpu(
      module, hitsimple::gpu::GpuAnalysisOptions{
                  hitsimple::gpu::GpuAnalysisMode::Cpu});
  const auto path = std::filesystem::temp_directory_path() /
                    "hitsimple-gpu-analysis-report-test.json";
  std::string error;
  HS_EXPECT_TRUE(hitsimple::gpu::writeGpuAnalysisReportJson(
      execution.report, path.string(), error));
  HS_EXPECT_TRUE(error.empty());
  std::ifstream input(path, std::ios::binary);
  HS_EXPECT_TRUE(static_cast<bool>(input));
  const std::string content{std::istreambuf_iterator<char>(input),
                            std::istreambuf_iterator<char>()};
  HS_EXPECT_TRUE(content.find("hitsimple_flowir_gpu_analysis") != std::string::npos);
  HS_EXPECT_TRUE(content.find("cpu_fallback_ns") != std::string::npos);
  std::filesystem::remove(path);
}
