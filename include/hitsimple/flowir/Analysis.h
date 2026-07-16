#pragma once

#include "hitsimple/diagnostic/Diagnostic.h"
#include "hitsimple/flowir/FlowIR.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hitsimple::flowir {

// Every data-flow analysis in this interface uses a finite, monotone lattice.
// Results are sorted by stable FlowIR IDs before they leave the reference path.
enum class LifetimeState : std::uint8_t {
  Dead,
  Live,
  MaybeLive,
};

enum class RangeState : std::uint8_t {
  Known,
  Unknown,
};

enum EffectFlag : std::uint32_t {
  EffectNone = 0,
  EffectRead = 1U << 0U,
  EffectWrite = 1U << 1U,
  EffectAllocates = 1U << 2U,
  EffectFrees = 1U << 3U,
  EffectThrows = 1U << 4U,
  EffectIo = 1U << 5U,
  EffectUnknown = 1U << 6U,
};

struct ReachabilityFact final {
  FunctionId function = InvalidId;
  BlockId block = InvalidId;
  bool reachable = false;
  bool operator==(const ReachabilityFact&) const = default;
};

struct LivenessFact final {
  FunctionId function = InvalidId;
  BlockId block = InvalidId;
  std::vector<ValueId> liveIn;
  std::vector<ValueId> liveOut;
  bool operator==(const LivenessFact&) const = default;
};

struct ViewRangeFact final {
  ValueId value = InvalidId;
  RangeState state = RangeState::Unknown;
  ObjectId object = InvalidId;
  std::uint32_t offset = 0;
  std::uint32_t byteLength = 0;
  bool operator==(const ViewRangeFact&) const = default;
};

struct LifetimeFact final {
  FunctionId function = InvalidId;
  BlockId block = InvalidId;
  ObjectId object = InvalidId;
  LifetimeState entry = LifetimeState::Dead;
  LifetimeState exit = LifetimeState::Dead;
  bool operator==(const LifetimeFact&) const = default;
};

struct InitializedBytesFact final {
  FunctionId function = InvalidId;
  BlockId block = InvalidId;
  ObjectId object = InvalidId;
  std::uint32_t offset = 0;
  std::uint32_t byteLength = 0;
  bool operator==(const InitializedBytesFact&) const = default;
};

struct EffectSummary final {
  FunctionId function = InvalidId;
  std::uint32_t flags = EffectNone;
  std::uint32_t declaredFlags = EffectNone;
  bool hasExplicitContract = false;
  std::vector<FunctionId> callees;
  bool operator==(const EffectSummary&) const = default;
};

struct AnalysisConvergence final {
  FunctionId function = InvalidId;
  std::uint32_t reachabilityIterations = 0;
  std::uint32_t livenessIterations = 0;
  std::uint32_t viewRangeIterations = 0;
  std::uint32_t lifetimeIterations = 0;
  std::uint32_t initializationIterations = 0;
  bool operator==(const AnalysisConvergence&) const = default;
};

struct AnalysisResult final {
  std::vector<ReachabilityFact> reachability;
  std::vector<LivenessFact> liveness;
  std::vector<ViewRangeFact> viewRanges;
  std::vector<LifetimeFact> lifetimes;
  std::vector<InitializedBytesFact> initializedBytes;
  std::vector<EffectSummary> effects;
  std::vector<AnalysisConvergence> convergence;
  std::vector<diagnostic::Diagnostic> diagnostics;
};

struct AnalysisOptions final {
  // One worker is the oracle baseline.  More workers only partition independent
  // function-local fixed points; output is committed by FunctionId order.
  std::size_t workerCount = 1;
};

AnalysisResult analyze(const Module& module, AnalysisOptions options = {});
std::string dumpAnalysisToString(const AnalysisResult& result);
std::vector<std::uint8_t> serializeAnalysis(const AnalysisResult& result);

class AnalysisCache final {
public:
  const AnalysisResult& analyze(const Module& module,
                                AnalysisOptions options = {});
  void invalidate();
  [[nodiscard]] bool validFor(const Module& module) const;

private:
  std::vector<std::uint8_t> moduleFingerprint_;
  std::optional<AnalysisResult> result_;
};

} // namespace hitsimple::flowir
