#include "GpuAnalysisInternal.h"

#include <chrono>
#include <utility>

namespace hitsimple::gpu {
namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t elapsedNs(Clock::time_point started) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count());
}

GpuFallbackReason fallbackReason(detail::OpenClRunStatus status) {
  switch (status) {
  case detail::OpenClRunStatus::Success:
    return GpuFallbackReason::None;
  case detail::OpenClRunStatus::DriverUnavailable:
    return GpuFallbackReason::DriverUnavailable;
  case detail::OpenClRunStatus::NoDevice:
    return GpuFallbackReason::NoDevice;
  case detail::OpenClRunStatus::UnsupportedDevice:
    return GpuFallbackReason::UnsupportedDevice;
  case detail::OpenClRunStatus::MemoryBudgetExceeded:
    return GpuFallbackReason::MemoryBudgetExceeded;
  case detail::OpenClRunStatus::OutOfMemory:
    return GpuFallbackReason::OutOfMemory;
  case detail::OpenClRunStatus::KernelFailure:
    return GpuFallbackReason::KernelFailure;
  case detail::OpenClRunStatus::Timeout:
    return GpuFallbackReason::Timeout;
  }
  return GpuFallbackReason::KernelFailure;
}

} // namespace

class GpuAnalyzer::Impl final {
public:
  explicit Impl(GpuAnalysisOptions options) : options_(options) {}

  GpuAnalysisExecution analyze(const flowir::Module& module) {
    GpuAnalysisExecution execution;
    execution.report.requestedMode = options_.mode;
    if (options_.mode == GpuAnalysisMode::Cpu) {
      execution.report.fallbackReason = GpuFallbackReason::Disabled;
      execution.report.detail = "CPU mode was requested";
      const auto started = Clock::now();
      execution.analysis = flowir::analyze(
          module, flowir::AnalysisOptions{options_.cpuWorkerCount});
      execution.report.timings.cpuFallbackNs = elapsedNs(started);
      return execution;
    }

    detail::PackedAnalysisInput input;
    std::string packingError;
    const auto packingStarted = Clock::now();
    if (!detail::packAnalysisInput(module, input, packingError)) {
      execution.report.fallbackReason = GpuFallbackReason::InvalidInput;
      execution.report.detail = std::move(packingError);
      execution.report.timings.preparationNs = elapsedNs(packingStarted);
      const auto started = Clock::now();
      execution.analysis = flowir::analyze(
          module, flowir::AnalysisOptions{options_.cpuWorkerCount});
      execution.report.timings.cpuFallbackNs = elapsedNs(started);
      return execution;
    }
    execution.report.timings.preparationNs = elapsedNs(packingStarted);
    execution.report.estimatedDeviceBytes = input.estimatedDeviceBytes;

    const auto gpu = openCl_.run(module, input, options_.deviceMemoryBudgetBytes,
                                 options_.maxIterations, options_.kernelTimeoutNs,
                                 options_.testFault);
    execution.report.device = gpu.device;
    execution.report.reusedResidentBuffers = gpu.reusedResidentBuffers;
    execution.report.reachabilityIterations = gpu.reachabilityIterations;
    execution.report.livenessIterations = gpu.livenessIterations;
    execution.report.viewRangeIterations = gpu.viewRangeIterations;
    execution.report.timings.uploadNs = gpu.uploadNs;
    execution.report.timings.kernelNs = gpu.kernelNs;
    execution.report.timings.downloadNs = gpu.downloadNs;
    execution.report.detail = gpu.detail;

    if (gpu.status != detail::OpenClRunStatus::Success) {
      execution.report.fallbackReason = fallbackReason(gpu.status);
      const auto started = Clock::now();
      execution.analysis = flowir::analyze(
          module, flowir::AnalysisOptions{options_.cpuWorkerCount});
      execution.report.timings.cpuFallbackNs = elapsedNs(started);
      return execution;
    }

    const auto verificationStarted = Clock::now();
    execution.analysis = flowir::analyze(
        module, flowir::AnalysisOptions{options_.cpuWorkerCount});
    execution.report.timings.verificationNs = elapsedNs(verificationStarted);
    if (!execution.analysis.diagnostics.empty() ||
        !detail::matchesReferenceFacts(gpu.facts, execution.analysis)) {
      execution.report.fallbackReason = GpuFallbackReason::VerificationFailed;
      execution.report.detail = "OpenCL reachability/liveness/View-range facts differ from the CPU reference";
      return execution;
    }

    execution.report.executedBackend = GpuAnalysisBackend::OpenCl;
    execution.report.gpuFactsVerified = true;
    execution.report.detail = "OpenCL reachability/liveness/View-range facts match the CPU reference";
    return execution;
  }

private:
  GpuAnalysisOptions options_;
  detail::OpenClBackend openCl_;
};

GpuAnalyzer::GpuAnalyzer(GpuAnalysisOptions options)
    : impl_(std::make_unique<Impl>(options)) {}

GpuAnalyzer::~GpuAnalyzer() = default;
GpuAnalyzer::GpuAnalyzer(GpuAnalyzer&&) noexcept = default;
GpuAnalyzer& GpuAnalyzer::operator=(GpuAnalyzer&&) noexcept = default;

GpuAnalysisExecution GpuAnalyzer::analyze(const flowir::Module& module) {
  return impl_->analyze(module);
}

GpuAnalysisExecution analyzeWithGpu(const flowir::Module& module,
                                    GpuAnalysisOptions options) {
  return GpuAnalyzer(options).analyze(module);
}

std::string_view toString(GpuAnalysisMode mode) {
  switch (mode) {
  case GpuAnalysisMode::Auto:
    return "auto";
  case GpuAnalysisMode::OpenCl:
    return "opencl";
  case GpuAnalysisMode::Cpu:
    return "cpu";
  }
  return "unknown";
}

std::string_view toString(GpuAnalysisBackend backend) {
  switch (backend) {
  case GpuAnalysisBackend::Cpu:
    return "cpu";
  case GpuAnalysisBackend::OpenCl:
    return "opencl";
  }
  return "unknown";
}

std::string_view toString(GpuFallbackReason reason) {
  switch (reason) {
  case GpuFallbackReason::None:
    return "none";
  case GpuFallbackReason::Disabled:
    return "disabled";
  case GpuFallbackReason::DriverUnavailable:
    return "driver_unavailable";
  case GpuFallbackReason::NoDevice:
    return "no_device";
  case GpuFallbackReason::UnsupportedDevice:
    return "unsupported_device";
  case GpuFallbackReason::MemoryBudgetExceeded:
    return "memory_budget_exceeded";
  case GpuFallbackReason::OutOfMemory:
    return "out_of_memory";
  case GpuFallbackReason::KernelFailure:
    return "kernel_failure";
  case GpuFallbackReason::Timeout:
    return "timeout";
  case GpuFallbackReason::VerificationFailed:
    return "verification_failed";
  case GpuFallbackReason::InvalidInput:
    return "invalid_input";
  }
  return "unknown";
}

} // namespace hitsimple::gpu
