#include "GpuAnalysisInternal.h"

#include "hitsimple/flowir/Serialization.h"

#include <algorithm>
#include <limits>
#include <set>

namespace hitsimple::gpu::detail {
namespace {

bool addBytes(std::uint64_t& total, std::uint64_t count,
              std::string& error) {
  constexpr auto wordBytes = sizeof(std::uint32_t);
  if (count > std::numeric_limits<std::uint64_t>::max() / wordBytes ||
      total > std::numeric_limits<std::uint64_t>::max() - count * wordBytes) {
    error = "GPU analysis working-set size overflows uint64";
    return false;
  }
  total += count * wordBytes;
  return true;
}

bool setBit(std::vector<std::uint32_t>& words, std::size_t base,
            flowir::ValueId value, std::string& error) {
  const auto word = static_cast<std::size_t>(value / 32U);
  if (base > words.size() || word >= words.size() - base) {
    error = "FlowIR value ID exceeds the GPU liveness bitset";
    return false;
  }
  words[base + word] |= 1U << (value % 32U);
  return true;
}

bool hasBit(const std::vector<std::uint32_t>& words, std::size_t base,
            flowir::ValueId value) {
  const auto word = static_cast<std::size_t>(value / 32U);
  return (words[base + word] & (1U << (value % 32U))) != 0U;
}

bool propagatesKnownRange(flowir::Opcode opcode) {
  return opcode == flowir::Opcode::ReinterpretView ||
         opcode == flowir::Opcode::Convert || opcode == flowir::Opcode::Unary ||
         opcode == flowir::Opcode::ByteSwap;
}

} // namespace

bool packAnalysisInput(const flowir::Module& module, PackedAnalysisInput& out,
                       std::string& error) {
  out = {};
  if (module.schemaVersion != flowir::SchemaVersion) {
    error = "unsupported FlowIR schema for GPU analysis";
    return false;
  }
  if (module.blocks.size() > std::numeric_limits<std::uint32_t>::max() ||
      module.values.size() > std::numeric_limits<std::uint32_t>::max() ||
      module.edges.size() > std::numeric_limits<std::uint32_t>::max()) {
    error = "FlowIR table count exceeds the GPU backend's 32-bit ID limit";
    return false;
  }

  out.blockCount = static_cast<std::uint32_t>(module.blocks.size());
  out.valueCount = static_cast<std::uint32_t>(module.values.size());
  out.livenessWordCount = std::max(
      1U, static_cast<std::uint32_t>((static_cast<std::uint64_t>(out.valueCount) + 31U) / 32U));
  const auto livenessWords = static_cast<std::uint64_t>(out.blockCount) *
                             out.livenessWordCount;
  if (livenessWords > std::numeric_limits<std::size_t>::max() ||
      livenessWords > std::numeric_limits<std::uint32_t>::max()) {
    error = "FlowIR liveness workspace exceeds the GPU backend's indexing limit";
    return false;
  }

  out.edgeFrom.reserve(module.edges.size());
  out.edgeTo.reserve(module.edges.size());
  for (const auto& edge : module.edges) {
    out.edgeFrom.push_back(edge.from);
    out.edgeTo.push_back(edge.to);
  }

  out.entryReachability.assign(out.blockCount, 0U);
  for (const auto& function : module.functions) {
    if (function.blockCount != 0U) {
      // The CPU reference iterates each function's contiguous block range from
      // its first block.  Preserve that oracle until FlowIR makes entryBlock
      // the sole CFG-root invariant.
      if (function.firstBlock >= out.blockCount) {
        error = "FlowIR function block range is invalid for GPU analysis";
        return false;
      }
      out.entryReachability[function.firstBlock] = 1U;
    }
  }

  out.successorOffsets.resize(static_cast<std::size_t>(out.blockCount) + 1U, 0U);
  for (flowir::BlockId block = 0; block < out.blockCount; ++block) {
    const auto& record = module.blocks[block];
    if (record.successorCount > std::numeric_limits<std::uint32_t>::max() -
                                    out.successorOffsets[block]) {
      error = "FlowIR successor table exceeds the GPU backend's 32-bit limit";
      return false;
    }
    out.successorOffsets[block + 1U] =
        out.successorOffsets[block] + record.successorCount;
    for (std::uint32_t offset = 0; offset < record.successorCount; ++offset) {
      const auto edge = module.successorEdges[record.successorBegin + offset];
      out.successors.push_back(module.edges[edge].to);
    }
  }

  out.uses.assign(static_cast<std::size_t>(livenessWords), 0U);
  out.definitions.assign(static_cast<std::size_t>(livenessWords), 0U);
  for (flowir::BlockId block = 0; block < out.blockCount; ++block) {
    const auto& record = module.blocks[block];
    const auto base = static_cast<std::size_t>(block) * out.livenessWordCount;
    for (flowir::InstructionId instruction = record.firstInstruction;
         instruction < record.firstInstruction + record.instructionCount; ++instruction) {
      const auto& current = module.instructions[instruction];
      for (flowir::OperandId operand = current.operandBegin;
           operand < current.operandBegin + current.operandCount; ++operand) {
        const auto value = module.operands[operand];
        if (value >= out.valueCount) {
          error = "FlowIR operand has an invalid value ID for GPU analysis";
          return false;
        }
        if (!hasBit(out.definitions, base, value) &&
            !setBit(out.uses, base, value, error)) {
          return false;
        }
      }
      for (flowir::ValueId result = current.resultBegin;
           result < current.resultBegin + current.resultCount; ++result) {
        const auto value = module.results[result];
        if (value >= out.valueCount || !setBit(out.definitions, base, value, error)) {
          if (error.empty()) {
            error = "FlowIR result has an invalid value ID for GPU analysis";
          }
          return false;
        }
      }
    }
  }

  out.rangeStates.resize(out.valueCount, 0U);
  out.rangeObjects.resize(out.valueCount, flowir::InvalidId);
  out.rangeOffsets.resize(out.valueCount, 0U);
  out.rangeLengths.resize(out.valueCount, 0U);
  for (flowir::ValueId value = 0; value < out.valueCount; ++value) {
    const auto view = module.values[value].view;
    if (view >= module.views.size()) {
      error = "FlowIR value has an invalid View ID for GPU range analysis";
      return false;
    }
    const auto& record = module.views[view];
    out.rangeStates[value] = record.object == flowir::InvalidId ? 0U : 1U;
    out.rangeObjects[value] = record.object;
    out.rangeOffsets[value] = record.offset;
    out.rangeLengths[value] = record.byteLength;
  }
  for (const auto& instruction : module.instructions) {
    if (instruction.operandCount != 1U || instruction.resultCount == 0U ||
        !propagatesKnownRange(instruction.opcode)) {
      continue;
    }
    const auto source = module.operands[instruction.operandBegin];
    if (source >= out.valueCount) {
      error = "FlowIR range transfer has an invalid operand value ID";
      return false;
    }
    for (flowir::ValueId result = instruction.resultBegin;
         result < instruction.resultBegin + instruction.resultCount; ++result) {
      const auto destination = module.results[result];
      if (destination >= out.valueCount) {
        error = "FlowIR range transfer has an invalid result value ID";
        return false;
      }
      out.rangeSources.push_back(source);
      out.rangeDestinations.push_back(destination);
    }
  }

  std::uint64_t estimatedBytes = 0;
  if (!addBytes(estimatedBytes, out.edgeFrom.size(), error) ||
      !addBytes(estimatedBytes, out.edgeTo.size(), error) ||
      !addBytes(estimatedBytes, out.entryReachability.size(), error) ||
      !addBytes(estimatedBytes, out.successorOffsets.size(), error) ||
      !addBytes(estimatedBytes, out.successors.size(), error) ||
      !addBytes(estimatedBytes, out.uses.size(), error) ||
      !addBytes(estimatedBytes, out.definitions.size(), error) ||
      !addBytes(estimatedBytes, out.rangeStates.size(), error) ||
      !addBytes(estimatedBytes, out.rangeObjects.size(), error) ||
      !addBytes(estimatedBytes, out.rangeOffsets.size(), error) ||
      !addBytes(estimatedBytes, out.rangeLengths.size(), error) ||
      !addBytes(estimatedBytes, out.rangeStates.size(), error) ||
      !addBytes(estimatedBytes, out.rangeObjects.size(), error) ||
      !addBytes(estimatedBytes, out.rangeOffsets.size(), error) ||
      !addBytes(estimatedBytes, out.rangeLengths.size(), error) ||
      !addBytes(estimatedBytes, out.rangeSources.size(), error) ||
      !addBytes(estimatedBytes, out.rangeDestinations.size(), error) ||
      !addBytes(estimatedBytes, livenessWords, error) ||
      !addBytes(estimatedBytes, 1U, error)) {
    return false;
  }
  out.estimatedDeviceBytes = estimatedBytes;
  out.fingerprint = flowir::serialize(module);
  return true;
}

GpuDataflowFacts unpackGpuDataflowFacts(const flowir::Module& module,
                                        const PackedAnalysisInput& input,
                                        const std::vector<std::uint32_t>& reachable,
                                        const std::vector<std::uint32_t>& liveIn,
                                        const std::vector<std::uint32_t>& rangeStates,
                                        const std::vector<std::uint32_t>& rangeObjects,
                                        const std::vector<std::uint32_t>& rangeOffsets,
                                        const std::vector<std::uint32_t>& rangeLengths) {
  GpuDataflowFacts facts;
  facts.reachability.reserve(input.blockCount);
  facts.liveness.reserve(input.blockCount);
  for (flowir::BlockId block = 0; block < input.blockCount; ++block) {
    const auto function = module.blocks[block].function;
    facts.reachability.push_back(flowir::ReachabilityFact{
        function, block, reachable[block] != 0U});

    std::set<flowir::ValueId> liveOut;
    for (std::uint32_t successorIndex = input.successorOffsets[block];
         successorIndex < input.successorOffsets[block + 1U]; ++successorIndex) {
      const auto successor = input.successors[successorIndex];
      const auto successorBase = static_cast<std::size_t>(successor) * input.livenessWordCount;
      for (flowir::ValueId value = 0; value < input.valueCount; ++value) {
        if (hasBit(liveIn, successorBase, value)) {
          liveOut.insert(value);
        }
      }
    }
    std::vector<flowir::ValueId> liveInValues;
    const auto base = static_cast<std::size_t>(block) * input.livenessWordCount;
    for (flowir::ValueId value = 0; value < input.valueCount; ++value) {
      if (hasBit(liveIn, base, value)) {
        liveInValues.push_back(value);
      }
    }
    facts.liveness.push_back(flowir::LivenessFact{
        function, block, std::move(liveInValues),
        {liveOut.begin(), liveOut.end()}});
  }
  facts.viewRanges.reserve(input.valueCount);
  for (flowir::ValueId value = 0; value < input.valueCount; ++value) {
    if (rangeStates[value] != 0U) {
      facts.viewRanges.push_back(flowir::ViewRangeFact{
          value, flowir::RangeState::Known, rangeObjects[value],
          rangeOffsets[value], rangeLengths[value]});
    } else {
      facts.viewRanges.push_back(flowir::ViewRangeFact{
          value, flowir::RangeState::Unknown, flowir::InvalidId, 0U,
          rangeLengths[value]});
    }
  }
  return facts;
}

bool matchesReferenceFacts(const GpuDataflowFacts& gpu,
                           const flowir::AnalysisResult& cpu) {
  return gpu.reachability == cpu.reachability && gpu.liveness == cpu.liveness &&
         gpu.viewRanges == cpu.viewRanges;
}

} // namespace hitsimple::gpu::detail
