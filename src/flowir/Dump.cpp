#include "hitsimple/flowir/Dump.h"

#include <ostream>
#include <sstream>

namespace hitsimple::flowir {
namespace {

std::string_view stringAt(const Module &module, StringId id) {
  if (id == InvalidId || id >= module.strings.size()) {
    return "-";
  }
  return module.strings[id];
}

std::string idOrDash(Id id) {
  return id == InvalidId ? "-" : std::to_string(id);
}

} // namespace

void dump(const Module &module, std::ostream &out) {
  out << "FlowIR schema=" << module.schemaVersion << '\n';
  for (StringId id = 0; id < module.strings.size(); ++id) {
    out << "String id=" << id << " value=" << module.strings[id] << '\n';
  }
  for (TemplateId id = 0; id < module.templates.size(); ++id) {
    const auto &record = module.templates[id];
    out << "Template id=" << id << " name=" << stringAt(module, record.name)
        << " bytes=" << record.byteLength
        << " dynamic=" << (record.isDynamicLength ? "true" : "false") << '\n';
  }
  for (SourceMapId id = 0; id < module.sourceMaps.size(); ++id) {
    const auto &record = module.sourceMaps[id];
    out << "SourceMap id=" << id << " file=" << stringAt(module, record.file)
        << " begin=" << record.beginLine << ':' << record.beginColumn
        << " end=" << record.endLine << ':' << record.endColumn << '\n';
  }
  for (FunctionId id = 0; id < module.functions.size(); ++id) {
    const auto &record = module.functions[id];
    out << "Function id=" << id << " name=" << stringAt(module, record.name)
        << " entry=" << idOrDash(record.entryBlock)
        << " blocks=" << idOrDash(record.firstBlock) << '+' << record.blockCount
        << " params=" << record.parameterViewBegin << '+' << record.parameterViewCount
        << " returns=" << record.returnViewBegin << '+' << record.returnViewCount
        << " source=" << idOrDash(record.sourceMap)
        << " effects=" << record.declaredEffects
        << " explicit-effects=" << (record.hasExplicitEffectContract ? "true" : "false")
        << '\n';
  }
  for (ObjectId id = 0; id < module.objects.size(); ++id) {
    const auto &record = module.objects[id];
    out << "Object id=" << id << " function=" << idOrDash(record.function)
        << " binding=" << stringAt(module, record.bindingName)
        << " bytes=" << record.byteLength << " storage=" << toString(record.storage)
        << " template=" << idOrDash(record.templateId)
        << " source=" << idOrDash(record.sourceMap) << '\n';
  }
  for (ViewId id = 0; id < module.views.size(); ++id) {
    const auto &record = module.views[id];
    out << "View id=" << id << " template=" << idOrDash(record.templateId)
        << " bytes=" << record.byteLength << " category=" << toString(record.category)
        << " flags=" << record.interpretationFlags
        << " object=" << idOrDash(record.object)
        << " offset=" << record.offset << '\n';
  }
  for (BlockId id = 0; id < module.blocks.size(); ++id) {
    const auto &record = module.blocks[id];
    out << "Block id=" << id << " function=" << idOrDash(record.function)
        << " instructions=" << idOrDash(record.firstInstruction) << '+'
        << record.instructionCount << " successors=" << record.successorBegin
        << '+' << record.successorCount << " predecessors=" << record.predecessorBegin
        << '+' << record.predecessorCount << '\n';
  }
  for (InstructionId id = 0; id < module.instructions.size(); ++id) {
    const auto &record = module.instructions[id];
    out << "Instruction id=" << id << " opcode=" << toString(record.opcode)
        << " block=" << idOrDash(record.block) << " operands="
        << record.operandBegin << '+' << record.operandCount << " results="
        << record.resultBegin << '+' << record.resultCount
        << " slice.object=" << idOrDash(record.slice.object)
        << " slice.offset=" << record.slice.offset
        << " slice.bytes=" << record.slice.byteLength
        << " slice.access=" << toString(record.slice.access)
        << " slice.alias=" << toString(record.slice.aliasClass)
        << " symbol=" << stringAt(module, record.symbol)
        << " source=" << idOrDash(record.sourceMap)
        << " aux=" << record.auxiliary0 << ',' << record.auxiliary1 << '\n';
  }
  for (CfgEdgeId id = 0; id < module.edges.size(); ++id) {
    const auto &record = module.edges[id];
    out << "Edge id=" << id << " from=" << idOrDash(record.from)
        << " to=" << idOrDash(record.to) << " kind=" << toString(record.kind)
        << '\n';
  }
}

std::string dumpToString(const Module &module) {
  std::ostringstream out;
  dump(module, out);
  return out.str();
}

} // namespace hitsimple::flowir
