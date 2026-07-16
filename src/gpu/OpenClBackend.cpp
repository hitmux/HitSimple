#include "GpuAnalysisInternal.h"

#include "OpenClDriver.h"
#include "OpenClKernels.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>
#include <vector>

namespace hitsimple::gpu::detail {
namespace {

using Clock = std::chrono::steady_clock;

constexpr ClInt ClSuccess = 0;
constexpr ClInt ClDeviceNotFound = -1;
constexpr ClInt ClMemObjectAllocationFailure = -4;
constexpr ClInt ClOutOfResources = -5;
constexpr ClInt ClOutOfHostMemory = -6;
constexpr ClDeviceType ClDeviceTypeGpu = 1U << 2U;
constexpr ClMemFlags ClMemReadWrite = 1U << 0U;
constexpr ClBool ClTrue = 1U;
constexpr ClDeviceInfo ClDeviceGlobalMemSize = 0x101FU;
constexpr ClDeviceInfo ClDeviceName = 0x102BU;
constexpr ClDeviceInfo ClDeviceVersion = 0x102FU;
constexpr ClProgramBuildInfo ClProgramBuildLog = 0x1183U;

std::uint64_t elapsedNs(Clock::time_point started) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count());
}

struct DeviceBuffer final {
  ClMem memory = nullptr;
  std::size_t capacity = 0;
};

OpenClRunResult failure(OpenClRunStatus status, std::string detail) {
  OpenClRunResult result;
  result.status = status;
  result.detail = std::move(detail);
  return result;
}

std::uint32_t iterationLimit(const PackedAnalysisInput& input,
                             std::uint32_t configured) {
  if (configured != 0U) {
    return configured;
  }
  const auto derived = std::max(static_cast<std::uint64_t>(input.blockCount),
                                static_cast<std::uint64_t>(input.valueCount)) + 1U;
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(
      derived, std::numeric_limits<std::uint32_t>::max()));
}

bool isOutOfMemory(ClInt error) {
  return error == ClMemObjectAllocationFailure || error == ClOutOfResources ||
         error == ClOutOfHostMemory;
}

} // namespace

class OpenClBackend::Impl final {
public:
  ~Impl() { releaseRuntime(); }

  OpenClRunResult run(const flowir::Module& module, const PackedAnalysisInput& input,
                      std::uint64_t memoryBudgetBytes, std::uint32_t maxIterations,
                      std::uint64_t kernelTimeoutNs, GpuAnalysisTestFault testFault) {
    OpenClRunResult result;
    const auto setup = ensureReady(testFault);
    if (setup.status != OpenClRunStatus::Success) {
      return setup;
    }
    result.device = device_;
    if (testFault == GpuAnalysisTestFault::OutOfMemory) {
      auto failed = failure(OpenClRunStatus::OutOfMemory,
                            "test-injected OpenCL device allocation failure");
      failed.device = device_;
      return failed;
    }
    const auto effectiveBudget = memoryBudgetBytes == 0U
                                     ? device_->availableMemoryBytes
                                     : std::min(memoryBudgetBytes,
                                                device_->availableMemoryBytes);
    if (input.estimatedDeviceBytes > effectiveBudget) {
      auto failed = failure(OpenClRunStatus::MemoryBudgetExceeded,
                            "FlowIR GPU working set exceeds the configured OpenCL memory budget");
      failed.device = device_;
      return failed;
    }

    const auto uploadStarted = Clock::now();
    const bool reused = residentFingerprint_ == input.fingerprint;
    const auto resident = ensureResidentInput(input);
    if (resident.status != OpenClRunStatus::Success) {
      auto failed = resident;
      failed.device = device_;
      return failed;
    }
    if (!copyToDevice(reachable_, input.entryReachability) ||
        !zeroDevice(liveIn_, input.uses.size()) ||
        !copyToDevice(rangeStatesA_, input.rangeStates) ||
        !copyToDevice(rangeObjectsA_, input.rangeObjects) ||
        !copyToDevice(rangeOffsetsA_, input.rangeOffsets) ||
        !copyToDevice(rangeLengthsA_, input.rangeLengths) ||
        !zeroDevice(changed_, 1U)) {
      return failure(OpenClRunStatus::KernelFailure,
                     "OpenCL failed while preparing analysis workspace");
    }
    result.uploadNs = elapsedNs(uploadStarted);
    result.reusedResidentBuffers = reused;

    if (testFault == GpuAnalysisTestFault::KernelFailure) {
      auto failed = failure(OpenClRunStatus::KernelFailure,
                            "test-injected OpenCL kernel failure");
      failed.device = device_;
      failed.reusedResidentBuffers = reused;
      return failed;
    }

    const auto kernelStarted = Clock::now();
    const auto limit = iterationLimit(input, maxIterations);
    const auto timedOut = [&] {
      return kernelTimeoutNs != 0U && elapsedNs(kernelStarted) > kernelTimeoutNs;
    };
    for (std::uint32_t iteration = 0; iteration < limit; ++iteration) {
      if (!zeroDevice(changed_, 1U) ||
          !launchReachability(static_cast<std::uint32_t>(input.edgeFrom.size())) ||
          !readChanged()) {
        return failure(OpenClRunStatus::KernelFailure,
                       "OpenCL reachability kernel failed");
      }
      ++result.reachabilityIterations;
      if (timedOut()) {
        return failure(OpenClRunStatus::Timeout,
                       "OpenCL reachability exceeded the configured wall-clock budget");
      }
      if (changedHost_ == 0U) {
        break;
      }
      if (iteration + 1U == limit) {
        return failure(OpenClRunStatus::KernelFailure,
                       "OpenCL reachability did not converge within its finite bound");
      }
    }
    for (std::uint32_t iteration = 0; iteration < limit; ++iteration) {
      if (!zeroDevice(changed_, 1U) ||
          !launchLiveness(input.blockCount, input.livenessWordCount) || !readChanged()) {
        return failure(OpenClRunStatus::KernelFailure, "OpenCL liveness kernel failed");
      }
      ++result.livenessIterations;
      if (timedOut()) {
        return failure(OpenClRunStatus::Timeout,
                       "OpenCL liveness exceeded the configured wall-clock budget");
      }
      if (changedHost_ == 0U) {
        break;
      }
      if (iteration + 1U == limit) {
        return failure(OpenClRunStatus::KernelFailure,
                       "OpenCL liveness did not converge within its finite bound");
      }
    }
    rangeCurrentIsA_ = true;
    for (std::uint32_t iteration = 0; iteration < limit; ++iteration) {
      if (!copyRangeWorkspace(rangeCurrentIsA_) || !zeroDevice(changed_, 1U) ||
          !launchViewRanges(static_cast<std::uint32_t>(input.rangeSources.size()),
                            rangeCurrentIsA_) ||
          !readChanged()) {
        return failure(OpenClRunStatus::KernelFailure,
                       "OpenCL View-range kernel failed");
      }
      ++result.viewRangeIterations;
      if (timedOut()) {
        return failure(OpenClRunStatus::Timeout,
                       "OpenCL View-range analysis exceeded the configured wall-clock budget");
      }
      rangeCurrentIsA_ = !rangeCurrentIsA_;
      if (changedHost_ == 0U) {
        break;
      }
      if (iteration + 1U == limit) {
        return failure(OpenClRunStatus::KernelFailure,
                       "OpenCL View-range analysis did not converge within its finite bound");
      }
    }
    result.kernelNs = elapsedNs(kernelStarted);

    const auto downloadStarted = Clock::now();
    std::vector<std::uint32_t> reachable(input.blockCount, 0U);
    std::vector<std::uint32_t> liveIn(input.uses.size(), 0U);
    std::vector<std::uint32_t> rangeStates(input.rangeStates.size(), 0U);
    std::vector<std::uint32_t> rangeObjects(input.rangeObjects.size(), flowir::InvalidId);
    std::vector<std::uint32_t> rangeOffsets(input.rangeOffsets.size(), 0U);
    std::vector<std::uint32_t> rangeLengths(input.rangeLengths.size(), 0U);
    const auto& finalRangeStates = rangeCurrentIsA_ ? rangeStatesA_ : rangeStatesB_;
    const auto& finalRangeObjects = rangeCurrentIsA_ ? rangeObjectsA_ : rangeObjectsB_;
    const auto& finalRangeOffsets = rangeCurrentIsA_ ? rangeOffsetsA_ : rangeOffsetsB_;
    const auto& finalRangeLengths = rangeCurrentIsA_ ? rangeLengthsA_ : rangeLengthsB_;
    if (!copyFromDevice(reachable, reachable_) || !copyFromDevice(liveIn, liveIn_) ||
        !copyFromDevice(rangeStates, finalRangeStates) ||
        !copyFromDevice(rangeObjects, finalRangeObjects) ||
        !copyFromDevice(rangeOffsets, finalRangeOffsets) ||
        !copyFromDevice(rangeLengths, finalRangeLengths)) {
      return failure(OpenClRunStatus::KernelFailure,
                     "OpenCL failed while reading analysis facts");
    }
    result.downloadNs = elapsedNs(downloadStarted);
    result.facts = unpackGpuDataflowFacts(module, input, reachable, liveIn,
                                           rangeStates, rangeObjects,
                                           rangeOffsets, rangeLengths);
    result.status = OpenClRunStatus::Success;
    result.detail = "OpenCL completed reachability, liveness, and View ranges";
    return result;
  }

private:
  OpenClRunResult ensureReady(GpuAnalysisTestFault testFault) {
    if (driver_ == nullptr) {
      driver_ = std::make_unique<OpenClDriver>();
      std::string error;
      if (!driver_->load(error)) {
        driver_.reset();
        return failure(OpenClRunStatus::DriverUnavailable, std::move(error));
      }
    }
    if (context_ != nullptr) {
      OpenClRunResult ready;
      ready.status = OpenClRunStatus::Success;
      ready.device = device_;
      return ready;
    }

    ClUInt platformCount = 0U;
    const auto platformStatus = driver_->getPlatformIds_(0U, nullptr, &platformCount);
    if (platformStatus != ClSuccess || platformCount == 0U) {
      return failure(OpenClRunStatus::NoDevice,
                     "OpenCL ICD loader reports no platform with a GPU device");
    }
    std::vector<ClPlatformId> platforms(platformCount);
    if (driver_->getPlatformIds_(platformCount, platforms.data(), nullptr) != ClSuccess) {
      return failure(OpenClRunStatus::DriverUnavailable,
                     "OpenCL failed while enumerating platforms");
    }

    for (const auto platform : platforms) {
      ClUInt deviceCount = 0U;
      const auto status = driver_->getDeviceIds_(platform, ClDeviceTypeGpu, 0U,
                                                 nullptr, &deviceCount);
      if (status == ClDeviceNotFound || deviceCount == 0U) {
        continue;
      }
      if (status != ClSuccess) {
        return failure(OpenClRunStatus::DriverUnavailable,
                       "OpenCL failed while enumerating GPU devices");
      }
      std::vector<ClDeviceId> devices(deviceCount);
      if (driver_->getDeviceIds_(platform, ClDeviceTypeGpu, deviceCount,
                                 devices.data(), nullptr) != ClSuccess) {
        return failure(OpenClRunStatus::DriverUnavailable,
                       "OpenCL failed while reading GPU device IDs");
      }
      deviceId_ = devices.front();
      break;
    }
    if (deviceId_ == nullptr) {
      return failure(OpenClRunStatus::NoDevice,
                     "OpenCL found no GPU device");
    }

    std::string name;
    std::string version;
    std::uint64_t totalMemory = 0U;
    if (!readDeviceString(ClDeviceName, name) ||
        !readDeviceString(ClDeviceVersion, version) ||
        driver_->getDeviceInfo_(deviceId_, ClDeviceGlobalMemSize, sizeof(totalMemory),
                                &totalMemory, nullptr) != ClSuccess) {
      return failure(OpenClRunStatus::DriverUnavailable,
                     "OpenCL GPU device capability query failed");
    }

    ClInt error = ClSuccess;
    context_ = driver_->createContext_(nullptr, 1U, &deviceId_, nullptr, nullptr, &error);
    if (context_ == nullptr || error != ClSuccess) {
      context_ = nullptr;
      return failure(OpenClRunStatus::NoDevice, "OpenCL failed to create a GPU context");
    }
    queue_ = driver_->createCommandQueue_(context_, deviceId_, 0U, &error);
    if (queue_ == nullptr || error != ClSuccess) {
      releaseRuntime();
      return failure(OpenClRunStatus::NoDevice, "OpenCL failed to create a command queue");
    }
    device_ = GpuDeviceInfo{0U, std::move(name), std::move(version), totalMemory,
                            totalMemory, true};

    constexpr const char* invalidTestSource =
        "__kernel void hs_test_build_failure() { this_is_not_valid; }";
    const char* source = testFault == GpuAnalysisTestFault::ProgramBuild
                             ? invalidTestSource
                             : openClSource();
    program_ = driver_->createProgramWithSource_(context_, 1U, &source, nullptr, &error);
    if (program_ == nullptr || error != ClSuccess ||
        driver_->buildProgram_(program_, 1U, &deviceId_, "-cl-std=CL1.2", nullptr,
                              nullptr) != ClSuccess) {
      const auto buildLog = programBuildLog();
      auto failed = failure(
          OpenClRunStatus::KernelFailure,
          testFault == GpuAnalysisTestFault::ProgramBuild
              ? "test-injected OpenCL program build failure"
              : "OpenCL failed to build embedded analysis kernels" +
                    (buildLog.empty() ? std::string() : ": " + buildLog));
      failed.device = device_;
      releaseRuntime();
      return failed;
    }
    reachabilityKernel_ = driver_->createKernel_(program_, "hs_propagate_reachability", &error);
    if (reachabilityKernel_ == nullptr || error != ClSuccess) {
      releaseRuntime();
      return failure(OpenClRunStatus::KernelFailure,
                     "OpenCL failed to create the reachability kernel");
    }
    livenessKernel_ = driver_->createKernel_(program_, "hs_propagate_liveness", &error);
    if (livenessKernel_ == nullptr || error != ClSuccess) {
      releaseRuntime();
      return failure(OpenClRunStatus::KernelFailure,
                     "OpenCL failed to create the liveness kernel");
    }
    viewRangeKernel_ = driver_->createKernel_(program_, "hs_propagate_view_ranges", &error);
    if (viewRangeKernel_ == nullptr || error != ClSuccess) {
      releaseRuntime();
      return failure(OpenClRunStatus::KernelFailure,
                     "OpenCL failed to create the View-range kernel");
    }

    OpenClRunResult ready;
    ready.status = OpenClRunStatus::Success;
    ready.device = device_;
    return ready;
  }

  bool readDeviceString(ClDeviceInfo attribute, std::string& value) const {
    std::size_t size = 0U;
    if (driver_->getDeviceInfo_(deviceId_, attribute, 0U, nullptr, &size) != ClSuccess ||
        size == 0U) {
      return false;
    }
    std::vector<char> buffer(size);
    if (driver_->getDeviceInfo_(deviceId_, attribute, buffer.size(), buffer.data(),
                                nullptr) != ClSuccess) {
      return false;
    }
    while (!buffer.empty() && buffer.back() == '\0') {
      buffer.pop_back();
    }
    value.assign(buffer.begin(), buffer.end());
    return true;
  }

  std::string programBuildLog() const {
    if (program_ == nullptr || deviceId_ == nullptr) {
      return {};
    }
    std::size_t size = 0U;
    if (driver_->getProgramBuildInfo_(program_, deviceId_, ClProgramBuildLog,
                                      0U, nullptr, &size) != ClSuccess || size == 0U) {
      return {};
    }
    std::vector<char> buffer(size);
    if (driver_->getProgramBuildInfo_(program_, deviceId_, ClProgramBuildLog,
                                      buffer.size(), buffer.data(), nullptr) != ClSuccess) {
      return {};
    }
    while (!buffer.empty() && buffer.back() == '\0') {
      buffer.pop_back();
    }
    return {buffer.begin(), buffer.end()};
  }

  OpenClRunResult ensureResidentInput(const PackedAnalysisInput& input) {
    if (residentFingerprint_ == input.fingerprint) {
      OpenClRunResult result;
      result.status = OpenClRunStatus::Success;
      return result;
    }
    residentFingerprint_.clear();
    if (!ensureBuffer(edgeFrom_, input.edgeFrom.size()) ||
        !ensureBuffer(edgeTo_, input.edgeTo.size()) ||
        !ensureBuffer(reachable_, input.entryReachability.size()) ||
        !ensureBuffer(successorOffsets_, input.successorOffsets.size()) ||
        !ensureBuffer(successors_, input.successors.size()) ||
        !ensureBuffer(uses_, input.uses.size()) ||
        !ensureBuffer(definitions_, input.definitions.size()) ||
        !ensureBuffer(liveIn_, input.uses.size()) ||
        !ensureBuffer(rangeStatesA_, input.rangeStates.size()) ||
        !ensureBuffer(rangeObjectsA_, input.rangeObjects.size()) ||
        !ensureBuffer(rangeOffsetsA_, input.rangeOffsets.size()) ||
        !ensureBuffer(rangeLengthsA_, input.rangeLengths.size()) ||
        !ensureBuffer(rangeStatesB_, input.rangeStates.size()) ||
        !ensureBuffer(rangeObjectsB_, input.rangeObjects.size()) ||
        !ensureBuffer(rangeOffsetsB_, input.rangeOffsets.size()) ||
        !ensureBuffer(rangeLengthsB_, input.rangeLengths.size()) ||
        !ensureBuffer(rangeSources_, input.rangeSources.size()) ||
        !ensureBuffer(rangeDestinations_, input.rangeDestinations.size()) ||
        !ensureBuffer(changed_, 1U)) {
      return failure(isOutOfMemory(lastError_) ? OpenClRunStatus::OutOfMemory
                                                : OpenClRunStatus::KernelFailure,
                     "OpenCL device allocation failed for FlowIR analysis buffers");
    }
    if (!copyToDevice(edgeFrom_, input.edgeFrom) || !copyToDevice(edgeTo_, input.edgeTo) ||
        !copyToDevice(successorOffsets_, input.successorOffsets) ||
        !copyToDevice(successors_, input.successors) || !copyToDevice(uses_, input.uses) ||
        !copyToDevice(definitions_, input.definitions) ||
        !copyToDevice(rangeSources_, input.rangeSources) ||
        !copyToDevice(rangeDestinations_, input.rangeDestinations)) {
      return failure(OpenClRunStatus::KernelFailure,
                     "OpenCL failed while uploading FlowIR analysis buffers");
    }
    residentFingerprint_ = input.fingerprint;
    OpenClRunResult result;
    result.status = OpenClRunStatus::Success;
    return result;
  }

  bool ensureBuffer(DeviceBuffer& buffer, std::size_t count) {
    if (count == 0U || buffer.capacity >= count) {
      return true;
    }
    if (buffer.memory != nullptr) {
      driver_->releaseMemObject_(buffer.memory);
      buffer = {};
    }
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t)) {
      lastError_ = ClOutOfHostMemory;
      return false;
    }
    ClInt error = ClSuccess;
    buffer.memory = driver_->createBuffer_(context_, ClMemReadWrite,
                                           count * sizeof(std::uint32_t), nullptr, &error);
    lastError_ = error;
    if (buffer.memory == nullptr || error != ClSuccess) {
      buffer = {};
      return false;
    }
    buffer.capacity = count;
    return true;
  }

  bool copyToDevice(const DeviceBuffer& destination,
                    const std::vector<std::uint32_t>& source) {
    return source.empty() ||
           driver_->enqueueWriteBuffer_(queue_, destination.memory, ClTrue, 0U,
                                        source.size() * sizeof(std::uint32_t), source.data(),
                                        0U, nullptr, nullptr) == ClSuccess;
  }

  bool copyFromDevice(std::vector<std::uint32_t>& destination,
                      const DeviceBuffer& source) {
    return destination.empty() ||
           driver_->enqueueReadBuffer_(queue_, source.memory, ClTrue, 0U,
                                       destination.size() * sizeof(std::uint32_t),
                                       destination.data(), 0U, nullptr, nullptr) == ClSuccess;
  }

  bool copyDeviceToDevice(const DeviceBuffer& destination,
                          const DeviceBuffer& source, std::size_t count) {
    return count == 0U ||
           driver_->enqueueCopyBuffer_(queue_, source.memory, destination.memory,
                                       0U, 0U, count * sizeof(std::uint32_t),
                                       0U, nullptr, nullptr) == ClSuccess;
  }

  bool copyRangeWorkspace(bool fromA) {
    const auto& inputStates = fromA ? rangeStatesA_ : rangeStatesB_;
    const auto& inputObjects = fromA ? rangeObjectsA_ : rangeObjectsB_;
    const auto& inputOffsets = fromA ? rangeOffsetsA_ : rangeOffsetsB_;
    const auto& inputLengths = fromA ? rangeLengthsA_ : rangeLengthsB_;
    const auto& outputStates = fromA ? rangeStatesB_ : rangeStatesA_;
    const auto& outputObjects = fromA ? rangeObjectsB_ : rangeObjectsA_;
    const auto& outputOffsets = fromA ? rangeOffsetsB_ : rangeOffsetsA_;
    const auto& outputLengths = fromA ? rangeLengthsB_ : rangeLengthsA_;
    return copyDeviceToDevice(outputStates, inputStates, inputStates.capacity) &&
           copyDeviceToDevice(outputObjects, inputObjects, inputObjects.capacity) &&
           copyDeviceToDevice(outputOffsets, inputOffsets, inputOffsets.capacity) &&
           copyDeviceToDevice(outputLengths, inputLengths, inputLengths.capacity) &&
           driver_->finish_(queue_) == ClSuccess;
  }

  bool zeroDevice(const DeviceBuffer& destination, std::size_t count) {
    zeros_.assign(count, 0U);
    return copyToDevice(destination, zeros_);
  }

  bool readChanged() {
    return driver_->enqueueReadBuffer_(queue_, changed_.memory, ClTrue, 0U,
                                       sizeof(changedHost_), &changedHost_, 0U,
                                       nullptr, nullptr) == ClSuccess;
  }

  bool setMemArg(ClKernel kernel, ClUInt& index, const DeviceBuffer& buffer) {
    return driver_->setKernelArg_(kernel, index++, sizeof(buffer.memory),
                                  &buffer.memory) == ClSuccess;
  }

  bool setUIntArg(ClKernel kernel, ClUInt& index, std::uint32_t value) {
    return driver_->setKernelArg_(kernel, index++, sizeof(value), &value) == ClSuccess;
  }

  bool launchReachability(std::uint32_t edgeCount) {
    if (edgeCount == 0U) {
      return true;
    }
    ClUInt argument = 0U;
    if (!setMemArg(reachabilityKernel_, argument, edgeFrom_) ||
        !setMemArg(reachabilityKernel_, argument, edgeTo_) ||
        !setUIntArg(reachabilityKernel_, argument, edgeCount) ||
        !setMemArg(reachabilityKernel_, argument, reachable_) ||
        !setMemArg(reachabilityKernel_, argument, changed_)) {
      return false;
    }
    const std::size_t globalSize = edgeCount;
    return driver_->enqueueNdRangeKernel_(queue_, reachabilityKernel_, 1U, nullptr,
                                          &globalSize, nullptr, 0U, nullptr,
                                          nullptr) == ClSuccess &&
           driver_->finish_(queue_) == ClSuccess;
  }

  bool launchLiveness(std::uint32_t blockCount, std::uint32_t wordCount) {
    const auto itemCount = static_cast<std::uint64_t>(blockCount) * wordCount;
    if (itemCount == 0U) {
      return true;
    }
    ClUInt argument = 0U;
    if (!setMemArg(livenessKernel_, argument, successorOffsets_) ||
        !setMemArg(livenessKernel_, argument, successors_) ||
        !setMemArg(livenessKernel_, argument, uses_) ||
        !setMemArg(livenessKernel_, argument, definitions_) ||
        !setMemArg(livenessKernel_, argument, liveIn_) ||
        !setUIntArg(livenessKernel_, argument, blockCount) ||
        !setUIntArg(livenessKernel_, argument, wordCount) ||
        !setMemArg(livenessKernel_, argument, changed_)) {
      return false;
    }
    const std::size_t globalSize = static_cast<std::size_t>(itemCount);
    return driver_->enqueueNdRangeKernel_(queue_, livenessKernel_, 1U, nullptr,
                                          &globalSize, nullptr, 0U, nullptr,
                                          nullptr) == ClSuccess &&
           driver_->finish_(queue_) == ClSuccess;
  }

  bool launchViewRanges(std::uint32_t pairCount, bool inputIsA) {
    if (pairCount == 0U) {
      return true;
    }
    const auto& inputStates = inputIsA ? rangeStatesA_ : rangeStatesB_;
    const auto& inputObjects = inputIsA ? rangeObjectsA_ : rangeObjectsB_;
    const auto& inputOffsets = inputIsA ? rangeOffsetsA_ : rangeOffsetsB_;
    const auto& inputLengths = inputIsA ? rangeLengthsA_ : rangeLengthsB_;
    const auto& outputStates = inputIsA ? rangeStatesB_ : rangeStatesA_;
    const auto& outputObjects = inputIsA ? rangeObjectsB_ : rangeObjectsA_;
    const auto& outputOffsets = inputIsA ? rangeOffsetsB_ : rangeOffsetsA_;
    const auto& outputLengths = inputIsA ? rangeLengthsB_ : rangeLengthsA_;
    ClUInt argument = 0U;
    if (!setMemArg(viewRangeKernel_, argument, inputStates) ||
        !setMemArg(viewRangeKernel_, argument, inputObjects) ||
        !setMemArg(viewRangeKernel_, argument, inputOffsets) ||
        !setMemArg(viewRangeKernel_, argument, inputLengths) ||
        !setMemArg(viewRangeKernel_, argument, outputStates) ||
        !setMemArg(viewRangeKernel_, argument, outputObjects) ||
        !setMemArg(viewRangeKernel_, argument, outputOffsets) ||
        !setMemArg(viewRangeKernel_, argument, outputLengths) ||
        !setMemArg(viewRangeKernel_, argument, rangeSources_) ||
        !setMemArg(viewRangeKernel_, argument, rangeDestinations_) ||
        !setUIntArg(viewRangeKernel_, argument, pairCount) ||
        !setMemArg(viewRangeKernel_, argument, changed_)) {
      return false;
    }
    const std::size_t globalSize = pairCount;
    return driver_->enqueueNdRangeKernel_(queue_, viewRangeKernel_, 1U, nullptr,
                                          &globalSize, nullptr, 0U, nullptr,
                                          nullptr) == ClSuccess &&
           driver_->finish_(queue_) == ClSuccess;
  }

  void releaseBuffer(DeviceBuffer& buffer) {
    if (buffer.memory != nullptr && driver_) {
      driver_->releaseMemObject_(buffer.memory);
    }
    buffer = {};
  }

  void releaseRuntime() {
    releaseBuffers();
    residentFingerprint_.clear();
    if (reachabilityKernel_ != nullptr && driver_) {
      driver_->releaseKernel_(reachabilityKernel_);
    }
    if (livenessKernel_ != nullptr && driver_) {
      driver_->releaseKernel_(livenessKernel_);
    }
    if (viewRangeKernel_ != nullptr && driver_) {
      driver_->releaseKernel_(viewRangeKernel_);
    }
    reachabilityKernel_ = nullptr;
    livenessKernel_ = nullptr;
    viewRangeKernel_ = nullptr;
    if (program_ != nullptr && driver_) {
      driver_->releaseProgram_(program_);
    }
    program_ = nullptr;
    if (queue_ != nullptr && driver_) {
      driver_->releaseCommandQueue_(queue_);
    }
    queue_ = nullptr;
    if (context_ != nullptr && driver_) {
      driver_->releaseContext_(context_);
    }
    context_ = nullptr;
    deviceId_ = nullptr;
    device_.reset();
  }

  void releaseBuffers() {
    releaseBuffer(edgeFrom_);
    releaseBuffer(edgeTo_);
    releaseBuffer(reachable_);
    releaseBuffer(successorOffsets_);
    releaseBuffer(successors_);
    releaseBuffer(uses_);
    releaseBuffer(definitions_);
    releaseBuffer(liveIn_);
    releaseBuffer(rangeStatesA_);
    releaseBuffer(rangeObjectsA_);
    releaseBuffer(rangeOffsetsA_);
    releaseBuffer(rangeLengthsA_);
    releaseBuffer(rangeStatesB_);
    releaseBuffer(rangeObjectsB_);
    releaseBuffer(rangeOffsetsB_);
    releaseBuffer(rangeLengthsB_);
    releaseBuffer(rangeSources_);
    releaseBuffer(rangeDestinations_);
    releaseBuffer(changed_);
  }

  std::unique_ptr<OpenClDriver> driver_;
  ClDeviceId deviceId_ = nullptr;
  ClContext context_ = nullptr;
  ClCommandQueue queue_ = nullptr;
  ClProgram program_ = nullptr;
  ClKernel reachabilityKernel_ = nullptr;
  ClKernel livenessKernel_ = nullptr;
  ClKernel viewRangeKernel_ = nullptr;
  std::optional<GpuDeviceInfo> device_;
  std::vector<std::uint8_t> residentFingerprint_;
  DeviceBuffer edgeFrom_;
  DeviceBuffer edgeTo_;
  DeviceBuffer reachable_;
  DeviceBuffer successorOffsets_;
  DeviceBuffer successors_;
  DeviceBuffer uses_;
  DeviceBuffer definitions_;
  DeviceBuffer liveIn_;
  DeviceBuffer rangeStatesA_;
  DeviceBuffer rangeObjectsA_;
  DeviceBuffer rangeOffsetsA_;
  DeviceBuffer rangeLengthsA_;
  DeviceBuffer rangeStatesB_;
  DeviceBuffer rangeObjectsB_;
  DeviceBuffer rangeOffsetsB_;
  DeviceBuffer rangeLengthsB_;
  DeviceBuffer rangeSources_;
  DeviceBuffer rangeDestinations_;
  DeviceBuffer changed_;
  bool rangeCurrentIsA_ = true;
  std::vector<std::uint32_t> zeros_;
  std::uint32_t changedHost_ = 0U;
  ClInt lastError_ = ClSuccess;
};

OpenClBackend::OpenClBackend() : impl_(std::make_unique<Impl>()) {}
OpenClBackend::~OpenClBackend() = default;
OpenClBackend::OpenClBackend(OpenClBackend&&) noexcept = default;
OpenClBackend& OpenClBackend::operator=(OpenClBackend&&) noexcept = default;

OpenClRunResult OpenClBackend::run(const flowir::Module& module,
                                   const PackedAnalysisInput& input,
                                   std::uint64_t memoryBudgetBytes,
                                   std::uint32_t maxIterations,
                                   std::uint64_t kernelTimeoutNs,
                                   GpuAnalysisTestFault testFault) {
  return impl_->run(module, input, memoryBudgetBytes, maxIterations,
                    kernelTimeoutNs, testFault);
}

} // namespace hitsimple::gpu::detail
