#include "hitsimple/flowir/Analysis.h"

#include "AnalysisInternal.h"
#include "hitsimple/flowir/Serialization.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <thread>
#include <utility>

namespace hitsimple::flowir {
namespace {

using ValueSet = std::set<ValueId>;
using ByteIntervals = std::vector<std::pair<std::uint32_t, std::uint32_t>>;

struct FunctionResult final {
  std::vector<ReachabilityFact> reachability;
  std::vector<LivenessFact> liveness;
  std::vector<LifetimeFact> lifetimes;
  std::vector<InitializedBytesFact> initializedBytes;
  AnalysisConvergence convergence;
};

std::vector<BlockId> blocksFor(const Module& module, FunctionId function) {
  const auto& record = module.functions[function];
  std::vector<BlockId> blocks;
  blocks.reserve(record.blockCount);
  for (std::uint32_t offset = 0; offset < record.blockCount; ++offset) {
    blocks.push_back(record.firstBlock + offset);
  }
  return blocks;
}

std::vector<BlockId> successors(const Module& module, BlockId block) {
  const auto& record = module.blocks[block];
  std::vector<BlockId> result;
  result.reserve(record.successorCount);
  for (std::uint32_t offset = 0; offset < record.successorCount; ++offset) {
    const auto edge = module.successorEdges[record.successorBegin + offset];
    result.push_back(module.edges[edge].to);
  }
  return result;
}

std::vector<BlockId> predecessors(const Module& module, BlockId block) {
  const auto& record = module.blocks[block];
  std::vector<BlockId> result;
  result.reserve(record.predecessorCount);
  for (std::uint32_t offset = 0; offset < record.predecessorCount; ++offset) {
    const auto edge = module.predecessorEdges[record.predecessorBegin + offset];
    result.push_back(module.edges[edge].from);
  }
  return result;
}

void unionInto(ValueSet& target, const ValueSet& source) {
  target.insert(source.begin(), source.end());
}

std::vector<ValueId> asVector(const ValueSet& values) {
  return {values.begin(), values.end()};
}

LifetimeState joinLifetime(LifetimeState left, LifetimeState right) {
  return left == right ? left : LifetimeState::MaybeLive;
}

ByteIntervals normalize(ByteIntervals intervals) {
  std::sort(intervals.begin(), intervals.end());
  ByteIntervals result;
  for (const auto& [begin, end] : intervals) {
    if (begin >= end) {
      continue;
    }
    if (!result.empty() && begin <= result.back().second) {
      result.back().second = std::max(result.back().second, end);
    } else {
      result.emplace_back(begin, end);
    }
  }
  return result;
}

ByteIntervals markInitialized(ByteIntervals intervals, std::uint32_t begin,
                              std::uint32_t length) {
  intervals.emplace_back(begin, begin + length);
  return normalize(std::move(intervals));
}

ByteIntervals intersectIntervals(const ByteIntervals& left,
                                 const ByteIntervals& right) {
  ByteIntervals result;
  std::size_t leftIndex = 0;
  std::size_t rightIndex = 0;
  while (leftIndex < left.size() && rightIndex < right.size()) {
    const auto begin = std::max(left[leftIndex].first, right[rightIndex].first);
    const auto end = std::min(left[leftIndex].second, right[rightIndex].second);
    if (begin < end) {
      result.emplace_back(begin, end);
    }
    if (left[leftIndex].second < right[rightIndex].second) {
      ++leftIndex;
    } else {
      ++rightIndex;
    }
  }
  return result;
}

using LifetimeMap = std::map<ObjectId, LifetimeState>;
using InitializationMap = std::map<ObjectId, ByteIntervals>;

LifetimeMap joinLifetimes(const Module& module, const std::vector<BlockId>& preds,
                          const std::map<BlockId, LifetimeMap>& exits) {
  LifetimeMap result;
  bool first = true;
  for (const auto predecessor : preds) {
    const auto found = exits.find(predecessor);
    if (found == exits.end()) {
      continue;
    }
    if (first) {
      result = found->second;
      first = false;
      continue;
    }
    for (const auto& object : module.objects) {
      if (object.function == InvalidId) {
        continue;
      }
    }
    for (auto& [object, state] : result) {
      const auto other = found->second.contains(object)
                             ? found->second.at(object)
                             : LifetimeState::Dead;
      state = joinLifetime(state, other);
    }
    for (const auto& [object, state] : found->second) {
      if (!result.contains(object)) {
        result.emplace(object, joinLifetime(LifetimeState::Dead, state));
      }
    }
  }
  return result;
}

InitializationMap joinInitializations(const std::vector<BlockId>& preds,
                                      const std::map<BlockId, InitializationMap>& exits) {
  InitializationMap result;
  bool first = true;
  for (const auto predecessor : preds) {
    const auto found = exits.find(predecessor);
    if (found == exits.end()) {
      continue;
    }
    if (first) {
      result = found->second;
      first = false;
      continue;
    }
    for (auto iterator = result.begin(); iterator != result.end();) {
      const auto other = found->second.find(iterator->first);
      if (other == found->second.end()) {
        iterator = result.erase(iterator);
      } else {
        iterator->second = intersectIntervals(iterator->second, other->second);
        ++iterator;
      }
    }
  }
  return result;
}

FunctionResult analyzeFunction(const Module& module, FunctionId function) {
  FunctionResult result;
  result.convergence.function = function;
  const auto blocks = blocksFor(module, function);
  if (blocks.empty()) {
    return result;
  }

  std::map<BlockId, bool> reachable;
  reachable[blocks.front()] = true;
  bool changed = true;
  while (changed) {
    changed = false;
    ++result.convergence.reachabilityIterations;
    for (const auto block : blocks) {
      if (!reachable[block]) {
        continue;
      }
      for (const auto successor : successors(module, block)) {
        if (!reachable[successor]) {
          reachable[successor] = true;
          changed = true;
        }
      }
    }
  }
  for (const auto block : blocks) {
    result.reachability.push_back(ReachabilityFact{function, block, reachable[block]});
  }

  std::map<BlockId, ValueSet> uses;
  std::map<BlockId, ValueSet> definitions;
  for (const auto block : blocks) {
    const auto& record = module.blocks[block];
    for (InstructionId instruction = record.firstInstruction;
         instruction < record.firstInstruction + record.instructionCount; ++instruction) {
      const auto& current = module.instructions[instruction];
      for (OperandId operand = current.operandBegin;
           operand < current.operandBegin + current.operandCount; ++operand) {
        const auto value = module.operands[operand];
        if (!definitions[block].contains(value)) {
          uses[block].insert(value);
        }
      }
      for (ValueId output = current.resultBegin;
           output < current.resultBegin + current.resultCount; ++output) {
        definitions[block].insert(module.results[output]);
      }
    }
  }
  std::map<BlockId, ValueSet> liveIn;
  std::map<BlockId, ValueSet> liveOut;
  changed = true;
  while (changed) {
    changed = false;
    ++result.convergence.livenessIterations;
    for (auto iterator = blocks.rbegin(); iterator != blocks.rend(); ++iterator) {
      const auto block = *iterator;
      ValueSet out;
      for (const auto successor : successors(module, block)) {
        unionInto(out, liveIn[successor]);
      }
      ValueSet in = uses[block];
      for (const auto value : out) {
        if (!definitions[block].contains(value)) {
          in.insert(value);
        }
      }
      if (out != liveOut[block] || in != liveIn[block]) {
        liveOut[block] = std::move(out);
        liveIn[block] = std::move(in);
        changed = true;
      }
    }
  }
  for (const auto block : blocks) {
    result.liveness.push_back(
        LivenessFact{function, block, asVector(liveIn[block]), asVector(liveOut[block])});
  }

  std::vector<ObjectId> objects;
  for (ObjectId object = 0; object < module.objects.size(); ++object) {
    if (module.objects[object].function == function) {
      objects.push_back(object);
    }
  }
  std::map<BlockId, LifetimeMap> lifetimeIn;
  std::map<BlockId, LifetimeMap> lifetimeOut;
  changed = true;
  while (changed) {
    changed = false;
    ++result.convergence.lifetimeIterations;
    for (const auto block : blocks) {
      LifetimeMap entered = block == blocks.front()
                                 ? LifetimeMap{}
                                 : joinLifetimes(module, predecessors(module, block), lifetimeOut);
      for (const auto object : objects) {
        entered.try_emplace(object, LifetimeState::Dead);
      }
      auto exited = entered;
      const auto& record = module.blocks[block];
      for (InstructionId instruction = record.firstInstruction;
           instruction < record.firstInstruction + record.instructionCount; ++instruction) {
        const auto& current = module.instructions[instruction];
        if (current.slice.aliasClass != AliasClass::KnownObject ||
            !exited.contains(current.slice.object)) {
          continue;
        }
        if (current.opcode == Opcode::LifetimeStart) {
          exited[current.slice.object] = LifetimeState::Live;
        } else if (current.opcode == Opcode::LifetimeEnd) {
          exited[current.slice.object] = LifetimeState::Dead;
        }
      }
      if (entered != lifetimeIn[block] || exited != lifetimeOut[block]) {
        lifetimeIn[block] = std::move(entered);
        lifetimeOut[block] = std::move(exited);
        changed = true;
      }
    }
  }
  for (const auto block : blocks) {
    for (const auto object : objects) {
      result.lifetimes.push_back(LifetimeFact{function, block, object,
                                              lifetimeIn[block][object],
                                              lifetimeOut[block][object]});
    }
  }

  std::map<BlockId, InitializationMap> initializationIn;
  std::map<BlockId, InitializationMap> initializationOut;
  changed = true;
  while (changed) {
    changed = false;
    ++result.convergence.initializationIterations;
    for (const auto block : blocks) {
      auto entered = block == blocks.front()
                         ? InitializationMap{}
                         : joinInitializations(predecessors(module, block), initializationOut);
      auto exited = entered;
      const auto& record = module.blocks[block];
      for (InstructionId instruction = record.firstInstruction;
           instruction < record.firstInstruction + record.instructionCount; ++instruction) {
        const auto& current = module.instructions[instruction];
        if (current.slice.aliasClass != AliasClass::KnownObject ||
            !std::binary_search(objects.begin(), objects.end(), current.slice.object)) {
          continue;
        }
        if (current.opcode == Opcode::LifetimeStart) {
          exited.erase(current.slice.object);
        } else if (current.opcode == Opcode::Store) {
          exited[current.slice.object] = markInitialized(
              std::move(exited[current.slice.object]), current.slice.offset,
              current.slice.byteLength);
        }
      }
      if (entered != initializationIn[block] || exited != initializationOut[block]) {
        initializationIn[block] = std::move(entered);
        initializationOut[block] = std::move(exited);
        changed = true;
      }
    }
  }
  for (const auto block : blocks) {
    for (const auto& [object, intervals] : initializationOut[block]) {
      for (const auto& [offset, end] : intervals) {
        result.initializedBytes.push_back(
            InitializedBytesFact{function, block, object, offset, end - offset});
      }
    }
  }
  return result;
}

} // namespace

AnalysisResult analyze(const Module& module, AnalysisOptions options) {
  AnalysisResult result;
  if (module.schemaVersion != SchemaVersion) {
    result.diagnostics.push_back(diagnostic::Diagnostic::error(
        diagnostic::Stage::Hir, "cannot analyze an unsupported FlowIR schema"));
    return result;
  }
  std::vector<FunctionResult> functions(module.functions.size());
  const auto workers = std::max<std::size_t>(1, options.workerCount);
  if (workers == 1 || module.functions.size() < 2U) {
    for (FunctionId function = 0; function < module.functions.size(); ++function) {
      functions[function] = analyzeFunction(module, function);
    }
  } else {
    std::atomic<std::size_t> next{0};
    const auto worker = [&] {
      for (;;) {
        const auto index = next.fetch_add(1);
        if (index >= module.functions.size()) {
          return;
        }
        functions[index] = analyzeFunction(module, static_cast<FunctionId>(index));
      }
    };
    std::vector<std::thread> threads;
    threads.reserve(std::min(workers, module.functions.size()));
    for (std::size_t index = 0; index < std::min(workers, module.functions.size()); ++index) {
      threads.emplace_back(worker);
    }
    for (auto& thread : threads) {
      thread.join();
    }
  }
  for (auto& function : functions) {
    result.reachability.insert(result.reachability.end(), function.reachability.begin(), function.reachability.end());
    result.liveness.insert(result.liveness.end(), function.liveness.begin(), function.liveness.end());
    result.lifetimes.insert(result.lifetimes.end(), function.lifetimes.begin(), function.lifetimes.end());
    result.initializedBytes.insert(result.initializedBytes.end(), function.initializedBytes.begin(), function.initializedBytes.end());
    result.convergence.push_back(function.convergence);
  }
  result.viewRanges.resize(module.values.size());
  for (ValueId value = 0; value < module.values.size(); ++value) {
    const auto view = module.values[value].view;
    const auto& record = module.views[view];
    result.viewRanges[value] = record.object != InvalidId
                                   ? ViewRangeFact{value, RangeState::Known,
                                                   record.object, record.offset,
                                                   record.byteLength}
                                   : ViewRangeFact{value, RangeState::Unknown,
                                                   InvalidId, 0, record.byteLength};
  }
  // The View-range lattice is Known(object, interval) below Unknown.  FlowIR
  // has no phi instruction today, so the ordered SSA table reaches its fixed
  // point by forward transfer; the loop remains explicit for future joins.
  bool rangeChanged = true;
  std::uint32_t rangeIterations = 0;
  while (rangeChanged) {
    rangeChanged = false;
    ++rangeIterations;
    for (InstructionId instruction = 0; instruction < module.instructions.size(); ++instruction) {
      const auto& current = module.instructions[instruction];
      if (current.operandCount != 1U || current.resultCount == 0U ||
          (current.opcode != Opcode::ReinterpretView && current.opcode != Opcode::Convert &&
           current.opcode != Opcode::Unary && current.opcode != Opcode::ByteSwap)) {
        continue;
      }
      const auto input = module.operands[current.operandBegin];
      const auto& source = result.viewRanges[input];
      if (source.state != RangeState::Known) {
        continue;
      }
      for (ValueId output = current.resultBegin;
           output < current.resultBegin + current.resultCount; ++output) {
        const auto value = module.results[output];
        auto& destination = result.viewRanges[value];
        if (destination.state == RangeState::Unknown) {
          destination.state = RangeState::Known;
          destination.object = source.object;
          destination.offset = source.offset;
          destination.byteLength = source.byteLength;
          rangeChanged = true;
        }
      }
    }
  }
  for (auto& convergence : result.convergence) {
    convergence.viewRangeIterations = rangeIterations;
  }
  result.effects = detail::summarizeEffects(module);
  return result;
}

const AnalysisResult& AnalysisCache::analyze(const Module& module,
                                             AnalysisOptions options) {
  const auto fingerprint = serialize(module);
  if (!result_ || fingerprint != moduleFingerprint_) {
    moduleFingerprint_ = fingerprint;
    result_ = flowir::analyze(module, options);
  }
  return *result_;
}

void AnalysisCache::invalidate() {
  moduleFingerprint_.clear();
  result_.reset();
}

bool AnalysisCache::validFor(const Module& module) const {
  return result_.has_value() && moduleFingerprint_ == serialize(module);
}

} // namespace hitsimple::flowir
