#pragma once

#include "hitsimple/hir/HIR.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hitsimple::support {

enum class MetricStageStatus {
  NotStarted,
  Skipped,
  Completed,
  Failed,
};

std::string_view toString(MetricStageStatus status);

struct StageMetrics final {
  MetricStageStatus status = MetricStageStatus::NotStarted;
  std::uint64_t durationNs = 0;
};

struct HirNodeMetrics final {
  std::size_t total = 0;
  std::size_t expressions = 0;
  std::size_t statements = 0;
  std::size_t functions = 0;
  std::size_t globals = 0;
};

HirNodeMetrics collectHirNodeMetrics(const hir::TranslationUnit& unit);

struct TranslationUnitMetrics final {
  explicit TranslationUnitMetrics(std::string inputPath);

  std::string inputPath;
  StageMetrics preprocess;
  StageMetrics parse;
  StageMetrics cCompatLowering;
  StageMetrics semaHir;
  StageMetrics llvmEmission;
  HirNodeMetrics hirNodes;
  std::size_t llvmIrBytes = 0;
};

class CompilationMetrics final {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  CompilationMetrics();

  TranslationUnitMetrics& addTranslationUnit(std::string inputPath);
  void markSkipped(StageMetrics& stage);
  void complete(StageMetrics& stage, TimePoint started);
  void fail(StageMetrics& stage, TimePoint started);

  void fail(std::string stage);
  void succeed();

  [[nodiscard]] TimePoint now() const;
  [[nodiscard]] std::uint64_t totalDurationNs() const;
  [[nodiscard]] bool succeeded() const;
  [[nodiscard]] const std::optional<std::string>& failedStage() const;
  [[nodiscard]] const std::vector<TranslationUnitMetrics>& translationUnits() const;
  [[nodiscard]] std::vector<TranslationUnitMetrics>& translationUnits();
  [[nodiscard]] StageMetrics& llvmIrWrite();
  [[nodiscard]] const StageMetrics& llvmIrWrite() const;
  [[nodiscard]] StageMetrics& nativeOptimization();
  [[nodiscard]] const StageMetrics& nativeOptimization() const;
  [[nodiscard]] StageMetrics& objectEmission();
  [[nodiscard]] const StageMetrics& objectEmission() const;
  [[nodiscard]] StageMetrics& clangBackendLink();
  [[nodiscard]] const StageMetrics& clangBackendLink() const;

  [[nodiscard]] std::string toJson() const;
  void printSummary(std::ostream& out) const;

private:
  TimePoint started_;
  bool succeeded_ = false;
  std::optional<std::string> failedStage_;
  std::vector<TranslationUnitMetrics> translationUnits_;
  StageMetrics llvmIrWrite_;
  StageMetrics nativeOptimization_;
  StageMetrics objectEmission_;
  StageMetrics clangBackendLink_;
};

bool timingOutputPathIsValid(const std::filesystem::path& path,
                             std::string& error);
bool writeTimingJsonAtomically(const std::filesystem::path& path,
                               const CompilationMetrics& metrics,
                               std::string& error);

} // namespace hitsimple::support
