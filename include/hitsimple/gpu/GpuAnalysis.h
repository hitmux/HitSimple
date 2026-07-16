#pragma once

#include "hitsimple/flowir/Analysis.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hitsimple::gpu {

enum class GpuAnalysisMode : std::uint8_t {
  Auto,
  OpenCl,
  Cpu,
};

enum class GpuAnalysisBackend : std::uint8_t {
  Cpu,
  OpenCl,
};

enum class GpuFallbackReason : std::uint8_t {
  None,
  Disabled,
  DriverUnavailable,
  NoDevice,
  UnsupportedDevice,
  MemoryBudgetExceeded,
  OutOfMemory,
  KernelFailure,
  Timeout,
  VerificationFailed,
  InvalidInput,
};

// Test-only OpenCL failure injection. The command-line interface never sets it.
enum class GpuAnalysisTestFault : std::uint8_t {
  None,
  ProgramBuild,
  OutOfMemory,
  KernelFailure,
};

struct GpuDeviceInfo final {
  std::uint32_t ordinal = 0;
  std::string name;
  std::string apiVersion;
  std::uint64_t totalMemoryBytes = 0;
  std::uint64_t availableMemoryBytes = 0;
  bool availableMemoryIsEstimated = false;
};

struct GpuAnalysisTimings final {
  std::uint64_t preparationNs = 0;
  std::uint64_t uploadNs = 0;
  std::uint64_t kernelNs = 0;
  std::uint64_t downloadNs = 0;
  std::uint64_t verificationNs = 0;
  std::uint64_t cpuFallbackNs = 0;
};

struct GpuAnalysisReport final {
  GpuAnalysisMode requestedMode = GpuAnalysisMode::Auto;
  GpuAnalysisBackend executedBackend = GpuAnalysisBackend::Cpu;
  GpuFallbackReason fallbackReason = GpuFallbackReason::None;
  std::optional<GpuDeviceInfo> device;
  std::uint64_t estimatedDeviceBytes = 0;
  bool reusedResidentBuffers = false;
  bool gpuFactsVerified = false;
  std::uint32_t reachabilityIterations = 0;
  std::uint32_t livenessIterations = 0;
  std::uint32_t viewRangeIterations = 0;
  GpuAnalysisTimings timings;
  std::string detail;
};

struct GpuAnalysisOptions final {
  GpuAnalysisMode mode = GpuAnalysisMode::Auto;
  std::size_t cpuWorkerCount = 1;
  // Zero uses the device's currently available memory as the budget.
  std::uint64_t deviceMemoryBudgetBytes = 0;
  // Zero derives a finite monotone fixed-point bound from the FlowIR CFG.
  std::uint32_t maxIterations = 0;
  // Zero disables the post-synchronization GPU wall-clock budget.
  std::uint64_t kernelTimeoutNs = 30'000'000'000ULL;
  GpuAnalysisTestFault testFault = GpuAnalysisTestFault::None;
};

struct GpuAnalysisExecution final {
  flowir::AnalysisResult analysis;
  GpuAnalysisReport report;
};

class GpuAnalyzer final {
public:
  explicit GpuAnalyzer(GpuAnalysisOptions options = {});
  ~GpuAnalyzer();

  GpuAnalyzer(GpuAnalyzer&&) noexcept;
  GpuAnalyzer& operator=(GpuAnalyzer&&) noexcept;
  GpuAnalyzer(const GpuAnalyzer&) = delete;
  GpuAnalyzer& operator=(const GpuAnalyzer&) = delete;

  GpuAnalysisExecution analyze(const flowir::Module& module);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

GpuAnalysisExecution analyzeWithGpu(const flowir::Module& module,
                                    GpuAnalysisOptions options = {});

std::string_view toString(GpuAnalysisMode mode);
std::string_view toString(GpuAnalysisBackend backend);
std::string_view toString(GpuFallbackReason reason);

bool writeGpuAnalysisReportJson(const GpuAnalysisReport& report,
                                const std::string& path,
                                std::string& error);

} // namespace hitsimple::gpu
