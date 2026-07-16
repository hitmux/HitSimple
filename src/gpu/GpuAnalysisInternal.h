#pragma once

#include "hitsimple/gpu/GpuAnalysis.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hitsimple::gpu::detail {

struct PackedAnalysisInput final {
  std::vector<std::uint32_t> edgeFrom;
  std::vector<std::uint32_t> edgeTo;
  std::vector<std::uint32_t> entryReachability;
  std::vector<std::uint32_t> successorOffsets;
  std::vector<std::uint32_t> successors;
  std::vector<std::uint32_t> uses;
  std::vector<std::uint32_t> definitions;
  std::vector<std::uint32_t> rangeStates;
  std::vector<std::uint32_t> rangeObjects;
  std::vector<std::uint32_t> rangeOffsets;
  std::vector<std::uint32_t> rangeLengths;
  std::vector<std::uint32_t> rangeSources;
  std::vector<std::uint32_t> rangeDestinations;
  std::uint32_t blockCount = 0;
  std::uint32_t valueCount = 0;
  std::uint32_t livenessWordCount = 0;
  std::uint64_t estimatedDeviceBytes = 0;
  std::vector<std::uint8_t> fingerprint;
};

struct GpuDataflowFacts final {
  std::vector<flowir::ReachabilityFact> reachability;
  std::vector<flowir::LivenessFact> liveness;
  std::vector<flowir::ViewRangeFact> viewRanges;
};

enum class OpenClRunStatus : std::uint8_t {
  Success,
  DriverUnavailable,
  NoDevice,
  UnsupportedDevice,
  MemoryBudgetExceeded,
  OutOfMemory,
  KernelFailure,
  Timeout,
};

struct OpenClRunResult final {
  OpenClRunStatus status = OpenClRunStatus::DriverUnavailable;
  std::optional<GpuDeviceInfo> device;
  GpuDataflowFacts facts;
  std::uint64_t uploadNs = 0;
  std::uint64_t kernelNs = 0;
  std::uint64_t downloadNs = 0;
  bool reusedResidentBuffers = false;
  std::uint32_t reachabilityIterations = 0;
  std::uint32_t livenessIterations = 0;
  std::uint32_t viewRangeIterations = 0;
  std::string detail;
};

bool packAnalysisInput(const flowir::Module& module, PackedAnalysisInput& out,
                       std::string& error);
GpuDataflowFacts unpackGpuDataflowFacts(const flowir::Module& module,
                                        const PackedAnalysisInput& input,
                                        const std::vector<std::uint32_t>& reachable,
                                        const std::vector<std::uint32_t>& liveIn,
                                        const std::vector<std::uint32_t>& rangeStates,
                                        const std::vector<std::uint32_t>& rangeObjects,
                                        const std::vector<std::uint32_t>& rangeOffsets,
                                        const std::vector<std::uint32_t>& rangeLengths);
bool matchesReferenceFacts(const GpuDataflowFacts& gpu,
                           const flowir::AnalysisResult& cpu);

class OpenClBackend final {
public:
  OpenClBackend();
  ~OpenClBackend();
  OpenClBackend(OpenClBackend&&) noexcept;
  OpenClBackend& operator=(OpenClBackend&&) noexcept;
  OpenClBackend(const OpenClBackend&) = delete;
  OpenClBackend& operator=(const OpenClBackend&) = delete;

  OpenClRunResult run(const flowir::Module& module,
                      const PackedAnalysisInput& input,
                      std::uint64_t memoryBudgetBytes,
                      std::uint32_t maxIterations,
                      std::uint64_t kernelTimeoutNs,
                      GpuAnalysisTestFault testFault);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace hitsimple::gpu::detail
