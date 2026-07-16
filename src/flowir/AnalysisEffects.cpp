#include "AnalysisInternal.h"

#include <algorithm>
#include <map>
#include <string>
#include <string_view>

namespace hitsimple::flowir::detail {
namespace {

std::uint32_t builtinEffects(std::string_view name) {
  if (name == "alloc" || name == "calloc" || name == "realloc") return EffectAllocates;
  if (name == "free") return EffectFrees;
  if (name == "put" || name == "get" || name == "print" || name == "printf" ||
      name == "fprintf" || name == "scanf" || name == "fscanf" ||
      name == "fopen" || name == "fclose" || name == "fread" || name == "fwrite" ||
      name == "fflush" || name == "fseek" || name == "ftell" || name == "time_ms" ||
      name == "clock_ms" || name == "rand" || name == "srand" || name == "exit" ||
      name == "abort" || name == "panic") return EffectIo;
  return EffectNone;
}

std::uint32_t analysisEffects(std::uint32_t declared) {
  std::uint32_t result = EffectNone;
  if ((declared & FunctionEffectRead) != 0U) result |= EffectRead;
  if ((declared & FunctionEffectWrite) != 0U) result |= EffectWrite;
  if ((declared & FunctionEffectAllocates) != 0U) result |= EffectAllocates;
  if ((declared & FunctionEffectFrees) != 0U) result |= EffectFrees;
  if ((declared & FunctionEffectThrows) != 0U) result |= EffectThrows;
  if ((declared & FunctionEffectIo) != 0U) result |= EffectIo;
  if ((declared & FunctionEffectUnknown) != 0U) result |= EffectUnknown;
  return result;
}

} // namespace

std::vector<EffectSummary> summarizeEffects(const Module& module) {
  std::map<std::string, FunctionId> functionIds;
  for (FunctionId function = 0; function < module.functions.size(); ++function) {
    const auto name = module.functions[function].name;
    if (name != InvalidId && name < module.strings.size()) {
      functionIds.emplace(module.strings[name], function);
    }
  }
  std::vector<EffectSummary> results(module.functions.size());
  for (FunctionId function = 0; function < module.functions.size(); ++function) {
    auto& summary = results[function];
    summary.function = function;
    const auto& record = module.functions[function];
    summary.declaredFlags = analysisEffects(record.declaredEffects);
    summary.hasExplicitContract = record.hasExplicitEffectContract;
    if (record.blockCount == 0) {
      summary.flags = record.hasExplicitEffectContract ? summary.declaredFlags
                                                        : EffectUnknown;
      continue;
    }
    for (BlockId block = record.firstBlock;
         block < record.firstBlock + record.blockCount; ++block) {
      const auto& blockRecord = module.blocks[block];
      for (InstructionId instruction = blockRecord.firstInstruction;
           instruction < blockRecord.firstInstruction + blockRecord.instructionCount;
           ++instruction) {
        const auto& current = module.instructions[instruction];
        if (current.slice.aliasClass == AliasClass::UnknownExternal &&
            (current.slice.access == AccessKind::Read || current.slice.access == AccessKind::ReadWrite)) {
          summary.flags |= EffectRead;
        }
        if (current.slice.aliasClass == AliasClass::UnknownExternal &&
            (current.slice.access == AccessKind::Write || current.slice.access == AccessKind::ReadWrite)) {
          summary.flags |= EffectWrite;
        }
        if (current.opcode == Opcode::Throw) summary.flags |= EffectThrows;
        if (current.opcode == Opcode::Input) summary.flags |= EffectIo | EffectWrite;
        if (current.opcode != Opcode::Call || current.symbol == InvalidId ||
            current.symbol >= module.strings.size()) continue;
        const auto& callee = module.strings[current.symbol];
        summary.flags |= builtinEffects(callee);
        if (const auto found = functionIds.find(callee); found == functionIds.end()) {
          summary.flags |= EffectUnknown;
        } else {
          summary.callees.push_back(found->second);
        }
      }
    }
    std::sort(summary.callees.begin(), summary.callees.end());
    summary.callees.erase(std::unique(summary.callees.begin(), summary.callees.end()),
                          summary.callees.end());
  }
  bool changed = true;
  while (changed) {
    changed = false;
    for (auto& summary : results) {
      auto flags = summary.flags;
      for (const auto callee : summary.callees) flags |= results[callee].flags;
      if (flags != summary.flags) {
        summary.flags = flags;
        changed = true;
      }
    }
  }
  return results;
}

} // namespace hitsimple::flowir::detail
