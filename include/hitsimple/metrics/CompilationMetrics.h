#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hitsimple::hir {
struct TranslationUnit;
}

namespace hitsimple::metrics {

// This schema deliberately describes the current HIR -> LLVM pipeline only.
// FlowIR and device metrics are added when those representations exist.
inline constexpr unsigned kTimingJsonSchemaVersion = 1;

enum class CompilationStage {
  Preprocess,
  Parse,
  CCompatParse,
  CCompatLowering,
  Sema,
  LlvmEmission,
  TemporaryLlWrite,
  ClangBackendLink,
};

struct HirStatistics final {
  std::size_t functionCount = 0;
  std::size_t globalCount = 0;
  std::size_t statementCount = 0;
  std::size_t expressionCount = 0;
  std::size_t estimatedHostBytes = 0;
};

struct StageTiming final {
  CompilationStage stage;
  std::chrono::nanoseconds wallTime{};
};

struct TranslationUnitMetrics final {
  std::string inputPath;
  std::size_t inputBytes = 0;
  std::vector<StageTiming> stages;
  std::optional<HirStatistics> hir;
};

class CompilationMetrics final {
public:
  CompilationMetrics();

  std::size_t beginTranslationUnit(std::string inputPath,
                                   std::size_t inputBytes);
  void recordTranslationUnitStage(std::size_t index, CompilationStage stage,
                                  std::chrono::nanoseconds wallTime);
  void recordProgramStage(CompilationStage stage,
                          std::chrono::nanoseconds wallTime);
  void recordHirStatistics(std::size_t index, HirStatistics statistics);
  void finish(bool succeeded);

  bool writeJson(const std::filesystem::path& path, std::string& error) const;

private:
  std::chrono::steady_clock::time_point startedAt_;
  std::chrono::nanoseconds totalWallTime_{};
  bool finished_ = false;
  bool succeeded_ = false;
  std::vector<TranslationUnitMetrics> translationUnits_;
  std::vector<StageTiming> programStages_;
};

class ScopedStageTimer final {
public:
  ScopedStageTimer(CompilationMetrics* metrics,
                   std::optional<std::size_t> translationUnitIndex,
                   CompilationStage stage);
  ~ScopedStageTimer();

  ScopedStageTimer(const ScopedStageTimer&) = delete;
  ScopedStageTimer& operator=(const ScopedStageTimer&) = delete;

private:
  CompilationMetrics* metrics_ = nullptr;
  std::optional<std::size_t> translationUnitIndex_;
  CompilationStage stage_;
  std::chrono::steady_clock::time_point startedAt_;
};

HirStatistics collectHirStatistics(const hir::TranslationUnit& unit);

} // namespace hitsimple::metrics
