#include "hitsimple/flowir/Serialization.h"

#include <string_view>

namespace hitsimple::flowir {
namespace {

void appendU8(std::vector<std::uint8_t> &out, std::uint8_t value) {
  out.push_back(value);
}

void appendU16(std::vector<std::uint8_t> &out, std::uint16_t value) {
  appendU8(out, static_cast<std::uint8_t>(value));
  appendU8(out, static_cast<std::uint8_t>(value >> 8U));
}

void appendU32(std::vector<std::uint8_t> &out, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    appendU8(out, static_cast<std::uint8_t>(value >> shift));
  }
}

void appendString(std::vector<std::uint8_t> &out, std::string_view value) {
  appendU32(out, static_cast<std::uint32_t>(value.size()));
  out.insert(out.end(), value.begin(), value.end());
}

void appendSlice(std::vector<std::uint8_t> &out, const ObjectSliceRecord &slice) {
  appendU32(out, slice.object);
  appendU32(out, slice.dynamicOffset);
  appendU32(out, slice.dynamicLength);
  appendU32(out, slice.offset);
  appendU32(out, slice.byteLength);
  appendU8(out, static_cast<std::uint8_t>(slice.access));
  appendU8(out, static_cast<std::uint8_t>(slice.aliasClass));
}

} // namespace

std::vector<std::uint8_t> serialize(const Module &module) {
  std::vector<std::uint8_t> out;
  out.reserve(64U + module.strings.size() * 8U + module.instructions.size() * 48U);
  appendU32(out, 0x524F4C46U); // "FLOR", little-endian FlowIR marker.
  appendU32(out, module.schemaVersion);
  appendU32(out, static_cast<std::uint32_t>(module.strings.size()));
  appendU32(out, static_cast<std::uint32_t>(module.templates.size()));
  appendU32(out, static_cast<std::uint32_t>(module.sourceMaps.size()));
  appendU32(out, static_cast<std::uint32_t>(module.functions.size()));
  appendU32(out, static_cast<std::uint32_t>(module.blocks.size()));
  appendU32(out, static_cast<std::uint32_t>(module.objects.size()));
  appendU32(out, static_cast<std::uint32_t>(module.views.size()));
  appendU32(out, static_cast<std::uint32_t>(module.values.size()));
  appendU32(out, static_cast<std::uint32_t>(module.instructions.size()));
  appendU32(out, static_cast<std::uint32_t>(module.operands.size()));
  appendU32(out, static_cast<std::uint32_t>(module.results.size()));
  appendU32(out, static_cast<std::uint32_t>(module.edges.size()));
  appendU32(out, static_cast<std::uint32_t>(module.successorEdges.size()));
  appendU32(out, static_cast<std::uint32_t>(module.predecessorEdges.size()));

  for (const auto &value : module.strings) {
    appendString(out, value);
  }
  for (const auto &record : module.templates) {
    appendU32(out, record.name);
    appendU32(out, record.byteLength);
    appendU8(out, record.isDynamicLength ? 1U : 0U);
  }
  for (const auto &record : module.sourceMaps) {
    appendU32(out, record.file);
    appendU32(out, record.beginLine);
    appendU32(out, record.beginColumn);
    appendU32(out, record.endLine);
    appendU32(out, record.endColumn);
  }
  for (const auto &record : module.functions) {
    appendU32(out, record.name);
    appendU32(out, record.entryBlock);
    appendU32(out, record.firstBlock);
    appendU32(out, record.blockCount);
    appendU32(out, record.parameterViewBegin);
    appendU32(out, record.parameterViewCount);
    appendU32(out, record.returnViewBegin);
    appendU32(out, record.returnViewCount);
    appendU32(out, record.sourceMap);
    appendU32(out, record.declaredEffects);
    appendU8(out, record.hasExplicitEffectContract ? 1U : 0U);
  }
  for (const auto &record : module.blocks) {
    appendU32(out, record.function);
    appendU32(out, record.firstInstruction);
    appendU32(out, record.instructionCount);
    appendU32(out, record.successorBegin);
    appendU32(out, record.successorCount);
    appendU32(out, record.predecessorBegin);
    appendU32(out, record.predecessorCount);
  }
  for (const auto &record : module.objects) {
    appendU32(out, record.function);
    appendU32(out, record.bindingName);
    appendU32(out, record.byteLength);
    appendU8(out, static_cast<std::uint8_t>(record.storage));
    appendU32(out, record.templateId);
    appendU32(out, record.sourceMap);
  }
  for (const auto &record : module.views) {
    appendU32(out, record.templateId);
    appendU32(out, record.byteLength);
    appendU8(out, static_cast<std::uint8_t>(record.category));
    appendU32(out, record.interpretationFlags);
    appendU32(out, record.object);
    appendU32(out, record.offset);
  }
  for (const auto &record : module.values) {
    appendU32(out, record.definition);
    appendU32(out, record.view);
  }
  for (const auto &record : module.instructions) {
    appendU16(out, static_cast<std::uint16_t>(record.opcode));
    appendU32(out, record.block);
    appendU32(out, record.operandBegin);
    appendU32(out, record.operandCount);
    appendU32(out, record.resultBegin);
    appendU32(out, record.resultCount);
    appendSlice(out, record.slice);
    appendU32(out, record.symbol);
    appendU32(out, record.sourceMap);
    appendU32(out, record.auxiliary0);
    appendU32(out, record.auxiliary1);
  }
  for (const auto value : module.operands) {
    appendU32(out, value);
  }
  for (const auto value : module.results) {
    appendU32(out, value);
  }
  for (const auto &record : module.edges) {
    appendU32(out, record.from);
    appendU32(out, record.to);
    appendU8(out, static_cast<std::uint8_t>(record.kind));
  }
  for (const auto value : module.successorEdges) {
    appendU32(out, value);
  }
  for (const auto value : module.predecessorEdges) {
    appendU32(out, value);
  }
  return out;
}

} // namespace hitsimple::flowir
