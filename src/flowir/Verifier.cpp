#include "hitsimple/flowir/Verifier.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace hitsimple::flowir {
namespace {

class Verifier final {
public:
  std::vector<diagnostic::Diagnostic> run(const Module &module) {
    module_ = &module;
    if (module.schemaVersion != SchemaVersion) {
      error("unsupported FlowIR schema version");
      return std::move(errors_);
    }
    verifyTemplates();
    verifyFunctionsAndBlocks();
    verifyObjectsAndViews();
    verifyInstructions();
    verifyEdges();
    verifyDominance();
    verifyLifetimes();
    return std::move(errors_);
  }

private:
  const Module *module_ = nullptr;
  std::vector<diagnostic::Diagnostic> errors_;

  void error(std::string message) {
    errors_.push_back(
        diagnostic::Diagnostic::error(diagnostic::Stage::Hir, std::move(message)));
  }

  bool validString(StringId id) const {
    return id == InvalidId || id < module_->strings.size();
  }

  bool validTemplate(TemplateId id) const {
    return id == InvalidId || id < module_->templates.size();
  }

  bool validSource(SourceMapId id) const {
    return id == InvalidId || id < module_->sourceMaps.size();
  }

  bool validObject(ObjectId id) const {
    return id == InvalidId || id < module_->objects.size();
  }

  bool validValue(ValueId id) const {
    return id != InvalidId && id < module_->values.size();
  }

  void verifyTemplates() {
    for (const auto &record : module_->templates) {
      if (!validString(record.name)) {
        error("FlowIR template has invalid name ID");
      }
      if (record.isDynamicLength && record.byteLength != 0) {
        error("dynamic FlowIR template must not carry a fixed byte length");
      }
    }
    for (const auto &record : module_->sourceMaps) {
      if (!validString(record.file) || record.file == InvalidId) {
        error("FlowIR source map has invalid file ID");
      }
      if (record.beginLine == 0 || record.beginColumn == 0 ||
          record.endLine == 0 || record.endColumn == 0) {
        error("FlowIR source map has a zero source position");
      }
    }
  }

  void verifyFunctionsAndBlocks() {
    for (FunctionId id = 0; id < module_->functions.size(); ++id) {
      const auto &function = module_->functions[id];
      if (!validString(function.name) || function.name == InvalidId) {
        error("FlowIR function has invalid name ID");
      }
      if (!validSource(function.sourceMap)) {
        error("FlowIR function has invalid source map ID");
      }
      if (function.blockCount == 0) {
        if (function.firstBlock != InvalidId || function.entryBlock != InvalidId) {
          error("FlowIR declaration-only function has a CFG block");
        }
      } else if (function.firstBlock == InvalidId ||
                 static_cast<std::uint64_t>(function.firstBlock) + function.blockCount >
                     module_->blocks.size()) {
        error("FlowIR function block range is invalid");
      } else if (function.entryBlock < function.firstBlock ||
                 function.entryBlock >= function.firstBlock + function.blockCount) {
        error("FlowIR function entry block is outside its block range");
      }
      if (static_cast<std::uint64_t>(function.parameterViewBegin) +
              function.parameterViewCount > module_->views.size() ||
          static_cast<std::uint64_t>(function.returnViewBegin) +
              function.returnViewCount > module_->views.size()) {
        error("FlowIR function signature View range is invalid");
      }
    }
    for (BlockId id = 0; id < module_->blocks.size(); ++id) {
      const auto &block = module_->blocks[id];
      if (block.function == InvalidId || block.function >= module_->functions.size()) {
        error("FlowIR block has invalid function ID");
      }
      if (block.firstInstruction == InvalidId ||
          static_cast<std::uint64_t>(block.firstInstruction) +
              block.instructionCount > module_->instructions.size()) {
        error("FlowIR block instruction range is invalid");
      } else {
        for (InstructionId instruction = block.firstInstruction;
             instruction < block.firstInstruction + block.instructionCount;
             ++instruction) {
          if (module_->instructions[instruction].block != id) {
            error("FlowIR block instruction range is not contiguous");
            break;
          }
        }
      }
    }
  }

  void verifyObjectsAndViews() {
    for (const auto &object : module_->objects) {
      if (object.function != InvalidId && object.function >= module_->functions.size()) {
        error("FlowIR object has invalid function ID");
      }
      if (!validString(object.bindingName) || object.bindingName == InvalidId ||
          !validTemplate(object.templateId) || !validSource(object.sourceMap)) {
        error("FlowIR object has an invalid metadata ID");
      }
    }
    for (const auto &view : module_->views) {
      if (!validTemplate(view.templateId) || !validObject(view.object)) {
        error("FlowIR View has an invalid metadata ID");
        continue;
      }
      if (view.templateId != InvalidId) {
        const auto &templateRecord = module_->templates[view.templateId];
        if (!templateRecord.isDynamicLength &&
            templateRecord.byteLength != view.byteLength) {
          error("FlowIR View byte length violates its template contract");
        }
      }
      if (view.object != InvalidId) {
        const auto &object = module_->objects[view.object];
        const auto end = static_cast<std::uint64_t>(view.offset) + view.byteLength;
        if (end > object.byteLength) {
          error("FlowIR addressable View exceeds its object slice");
        }
      }
    }
    for (const auto &value : module_->values) {
      if (value.definition == InvalidId || value.definition >= module_->instructions.size() ||
          value.view == InvalidId || value.view >= module_->views.size()) {
        error("FlowIR value has an invalid definition or View ID");
      }
    }
  }

  void verifyInstructionSlice(const ObjectSliceRecord &slice) {
    if (slice.aliasClass == AliasClass::KnownObject) {
      if (slice.object == InvalidId || slice.object >= module_->objects.size()) {
        error("known FlowIR object slice has an invalid object ID");
      } else {
        const auto end = static_cast<std::uint64_t>(slice.offset) + slice.byteLength;
        if (end > module_->objects[slice.object].byteLength) {
          error("FlowIR object slice exceeds object bounds");
        }
      }
    } else if (slice.object != InvalidId) {
      error("unknown-external FlowIR slice must not name a local object");
    }
    if (slice.dynamicOffset != InvalidId && !validValue(slice.dynamicOffset)) {
      error("FlowIR object slice has invalid dynamic offset value");
    }
    if (slice.dynamicLength != InvalidId && !validValue(slice.dynamicLength)) {
      error("FlowIR object slice has invalid dynamic length value");
    }
  }

  void verifyInstructions() {
    for (InstructionId id = 0; id < module_->instructions.size(); ++id) {
      const auto &instruction = module_->instructions[id];
      if (instruction.opcode == Opcode::Invalid || instruction.block == InvalidId ||
          instruction.block >= module_->blocks.size()) {
        error("FlowIR instruction has invalid opcode or block ID");
      }
      if (!validString(instruction.symbol) || !validSource(instruction.sourceMap) ||
          static_cast<std::uint64_t>(instruction.operandBegin) +
                  instruction.operandCount > module_->operands.size() ||
          static_cast<std::uint64_t>(instruction.resultBegin) +
                  instruction.resultCount > module_->results.size()) {
        error("FlowIR instruction has an invalid table range");
      }
      for (OperandId operand = instruction.operandBegin;
           operand < instruction.operandBegin + instruction.operandCount &&
           operand < module_->operands.size(); ++operand) {
        if (!validValue(module_->operands[operand])) {
          error("FlowIR instruction uses an invalid value ID");
        }
      }
      for (ValueId result = instruction.resultBegin;
           result < instruction.resultBegin + instruction.resultCount &&
           result < module_->results.size(); ++result) {
        const auto value = module_->results[result];
        if (!validValue(value) || module_->values[value].definition != id) {
          error("FlowIR instruction result table does not match value definition");
        }
      }
      verifyInstructionSlice(instruction.slice);

      if (instruction.opcode == Opcode::Return &&
          instruction.block < module_->blocks.size()) {
        const auto function = module_->blocks[instruction.block].function;
        if (function < module_->functions.size() &&
            instruction.operandCount != module_->functions[function].returnViewCount) {
          error("FlowIR return value count does not match function signature");
        }
      }
    }
  }

  void verifyEdges() {
    std::vector<unsigned> successorSeen(module_->edges.size());
    std::vector<unsigned> predecessorSeen(module_->edges.size());
    for (BlockId block = 0; block < module_->blocks.size(); ++block) {
      const auto &record = module_->blocks[block];
      if (static_cast<std::uint64_t>(record.successorBegin) + record.successorCount >
              module_->successorEdges.size() ||
          static_cast<std::uint64_t>(record.predecessorBegin) + record.predecessorCount >
              module_->predecessorEdges.size()) {
        error("FlowIR CFG CSR range is invalid");
        continue;
      }
      for (std::uint32_t index = record.successorBegin;
           index < record.successorBegin + record.successorCount; ++index) {
        const auto edgeId = module_->successorEdges[index];
        if (edgeId >= module_->edges.size() || module_->edges[edgeId].from != block) {
          error("FlowIR successor CSR entry is not symmetric with its edge");
        } else {
          ++successorSeen[edgeId];
        }
      }
      for (std::uint32_t index = record.predecessorBegin;
           index < record.predecessorBegin + record.predecessorCount; ++index) {
        const auto edgeId = module_->predecessorEdges[index];
        if (edgeId >= module_->edges.size() || module_->edges[edgeId].to != block) {
          error("FlowIR predecessor CSR entry is not symmetric with its edge");
        } else {
          ++predecessorSeen[edgeId];
        }
      }
    }
    for (CfgEdgeId id = 0; id < module_->edges.size(); ++id) {
      const auto &edge = module_->edges[id];
      if (edge.from == InvalidId || edge.to == InvalidId ||
          edge.from >= module_->blocks.size() || edge.to >= module_->blocks.size()) {
        error("FlowIR CFG edge has an invalid block ID");
        continue;
      }
      if (successorSeen[id] != 1U || predecessorSeen[id] != 1U) {
        error("FlowIR CFG edge is missing from a CSR adjacency table");
      }
      if (edge.kind == CfgEdgeKind::Exceptional) {
        const auto &source = module_->blocks[edge.from];
        const auto &target = module_->blocks[edge.to];
        if (source.instructionCount == 0 || target.instructionCount == 0 ||
            module_->instructions[source.firstInstruction + source.instructionCount - 1U].opcode !=
                Opcode::Throw) {
          error("FlowIR exceptional edge lacks a terminating throw");
        } else {
          const auto targetOpcode = module_->instructions[target.firstInstruction].opcode;
          if (targetOpcode != Opcode::Catch && targetOpcode != Opcode::Unreachable) {
            error("FlowIR exceptional edge lacks a catch or uncaught target");
          }
        }
      }
    }
  }

  void verifyDominance() {
    for (FunctionId functionId = 0; functionId < module_->functions.size(); ++functionId) {
      const auto &function = module_->functions[functionId];
      if (function.blockCount == 0 || function.firstBlock == InvalidId ||
          function.entryBlock == InvalidId) {
        continue;
      }
      const auto begin = function.firstBlock;
      const auto end = begin + function.blockCount;
      std::vector<bool> reachable(module_->blocks.size());
      std::vector<BlockId> work{function.entryBlock};
      reachable[function.entryBlock] = true;
      for (std::size_t index = 0; index < work.size(); ++index) {
        const auto block = work[index];
        const auto &record = module_->blocks[block];
        for (std::uint32_t edgeIndex = record.successorBegin;
             edgeIndex < record.successorBegin + record.successorCount; ++edgeIndex) {
          const auto target = module_->edges[module_->successorEdges[edgeIndex]].to;
          if (!reachable[target]) {
            reachable[target] = true;
            work.push_back(target);
          }
        }
      }
      std::vector<std::vector<bool>> dominators(
          module_->blocks.size(), std::vector<bool>(module_->blocks.size()));
      for (BlockId block = begin; block < end; ++block) {
        if (!reachable[block]) {
          dominators[block][block] = true;
        } else if (block == function.entryBlock) {
          dominators[block][block] = true;
        } else {
          for (BlockId candidate = begin; candidate < end; ++candidate) {
            dominators[block][candidate] = reachable[candidate];
          }
        }
      }
      bool changed = true;
      while (changed) {
        changed = false;
        for (BlockId block = begin; block < end; ++block) {
          if (!reachable[block] || block == function.entryBlock) {
            continue;
          }
          std::vector<bool> next(module_->blocks.size(), true);
          bool hasPredecessor = false;
          const auto &record = module_->blocks[block];
          for (std::uint32_t edgeIndex = record.predecessorBegin;
               edgeIndex < record.predecessorBegin + record.predecessorCount; ++edgeIndex) {
            const auto predecessor = module_->edges[module_->predecessorEdges[edgeIndex]].from;
            if (!reachable[predecessor]) {
              continue;
            }
            hasPredecessor = true;
            for (BlockId candidate = begin; candidate < end; ++candidate) {
              next[candidate] = next[candidate] && dominators[predecessor][candidate];
            }
          }
          if (!hasPredecessor) {
            std::fill(next.begin() + begin, next.begin() + end, false);
          }
          next[block] = true;
          if (next != dominators[block]) {
            dominators[block] = std::move(next);
            changed = true;
          }
        }
      }
      for (InstructionId instruction = 0; instruction < module_->instructions.size();
           ++instruction) {
        const auto &use = module_->instructions[instruction];
        if (use.block < begin || use.block >= end) {
          continue;
        }
        for (std::uint32_t index = use.operandBegin;
             index < use.operandBegin + use.operandCount; ++index) {
          const auto value = module_->operands[index];
          if (!validValue(value)) {
            continue;
          }
          const auto definition = module_->values[value].definition;
          const auto definitionBlock = module_->instructions[definition].block;
          if (definitionBlock == use.block && definition >= instruction) {
            error("FlowIR SSA definition does not precede its use");
          } else if (definitionBlock != use.block &&
                     !dominators[use.block][definitionBlock]) {
            error("FlowIR SSA definition does not dominate its use");
          }
        }
      }
    }
  }

  void verifyLifetimes() {
    std::vector<bool> starts(module_->objects.size());
    std::vector<bool> ends(module_->objects.size());
    for (const auto &instruction : module_->instructions) {
      if (instruction.slice.aliasClass != AliasClass::KnownObject ||
          instruction.slice.object == InvalidId ||
          instruction.slice.object >= module_->objects.size()) {
        continue;
      }
      if (instruction.opcode == Opcode::LifetimeStart) {
        starts[instruction.slice.object] = true;
      } else if (instruction.opcode == Opcode::LifetimeEnd) {
        ends[instruction.slice.object] = true;
      }
    }
    for (ObjectId id = 0; id < module_->objects.size(); ++id) {
      const auto &object = module_->objects[id];
      if (object.storage == ObjectStorage::Local && (!starts[id] || !ends[id])) {
        error("FlowIR local object is missing explicit lifetime actions");
      }
      if (object.storage == ObjectStorage::Catch && !starts[id]) {
        error("FlowIR catch object is missing a lifetime start");
      }
    }
  }
};

} // namespace

std::vector<diagnostic::Diagnostic> verify(const Module &module) {
  return Verifier{}.run(module);
}

} // namespace hitsimple::flowir
