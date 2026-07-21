#include "support/TestRunner.h"

#include "hitsimple/hir/HIR.h"
#include "hitsimple/support/CompilationMetrics.h"
#include "hitsimple/support/Path.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

std::string readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

} // namespace

HS_TEST(CompilationMetrics_SerializesVersionedStageStates) {
  hitsimple::support::CompilationMetrics metrics;
  auto& unit = metrics.addTranslationUnit("source with spaces.hs");
  const auto started = metrics.now();
  metrics.complete(unit.preprocess, started);
  metrics.markSkipped(unit.cCompatLowering);
  metrics.fail(unit.parse, metrics.now());
  metrics.fail("parse");

  const auto json = metrics.toJson();
  HS_EXPECT_TRUE(json.find("\"schema_version\": 1") != std::string::npos);
  HS_EXPECT_TRUE(json.find("\"outcome\": \"failure\"") !=
                 std::string::npos);
  HS_EXPECT_TRUE(json.find("\"failed_stage\": \"parse\"") !=
                 std::string::npos);
  HS_EXPECT_TRUE(json.find("\"status\": \"completed\"") !=
                 std::string::npos);
  HS_EXPECT_TRUE(json.find("\"status\": \"skipped\"") !=
                 std::string::npos);
  HS_EXPECT_TRUE(json.find("\"status\": \"failed\"") !=
                 std::string::npos);
}

HS_TEST(CompilationMetrics_CountsHirNodes) {
  using namespace hitsimple::hir;
  std::vector<std::unique_ptr<Stmt>> statements;
  statements.push_back(std::make_unique<LocalMemory>("value", "value", 4,
                                                       MemoryStorage::Local));
  statements.push_back(std::make_unique<IntegerStore>(
      "value", "value", 4, MemoryStorage::Local,
      std::make_unique<IntegerLiteral>(
          "42", viewSemanticsForTemplate("i32", 4))));
  std::vector<std::unique_ptr<Function>> functions;
  functions.push_back(std::make_unique<Function>(
      "main", std::make_unique<Block>(std::move(statements))));
  const TranslationUnit unit(std::move(functions));

  const auto metrics = hitsimple::support::collectHirNodeMetrics(unit);
  HS_EXPECT_EQ(metrics.functions, std::size_t{1});
  HS_EXPECT_EQ(metrics.globals, std::size_t{0});
  HS_EXPECT_EQ(metrics.statements, std::size_t{2});
  HS_EXPECT_EQ(metrics.expressions, std::size_t{1});
  HS_EXPECT_EQ(metrics.total, std::size_t{4});
}

HS_TEST(CompilationMetrics_WritesJsonAtomicallyAndRejectsMissingParent) {
  hitsimple::support::CompilationMetrics metrics;
  metrics.succeed();
  const auto output = std::filesystem::temp_directory_path() /
                      hitsimple::support::pathFromUtf8(
                          "hitsimple-compilation-metrics-test.json");
  std::error_code ignored;
  std::filesystem::remove(output, ignored);

  std::string error;
  HS_EXPECT_TRUE(
      hitsimple::support::writeTimingJsonAtomically(output, metrics, error));
  const auto json = readFile(output);
  HS_EXPECT_TRUE(json.find("\"outcome\": \"success\"") !=
                 std::string::npos);
  std::filesystem::remove(output, ignored);

  const auto missing = std::filesystem::temp_directory_path() /
                       "hitsimple-compilation-metrics-missing" / "metrics.json";
  HS_EXPECT_TRUE(
      !hitsimple::support::writeTimingJsonAtomically(missing, metrics, error));
}
