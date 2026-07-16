#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace hitsimple::flowir {

using Id = std::uint32_t;
using StringId = Id;
using FunctionId = Id;
using BlockId = Id;
using InstructionId = Id;
using OperandId = Id;
using ValueId = Id;
using ObjectId = Id;
using ViewId = Id;
using TemplateId = Id;
using SourceMapId = Id;
using CfgEdgeId = Id;

inline constexpr Id InvalidId = UINT32_MAX;
inline constexpr std::uint32_t SchemaVersion = 2;

enum class Opcode : std::uint16_t {
  Invalid,
  LifetimeStart,
  LifetimeEnd,
  ConstantInteger,
  ConstantFloat,
  ConstantString,
  Load,
  AddressOf,
  Dereference,
  Unary,
  Binary,
  Select,
  Convert,
  ReinterpretView,
  DynamicView,
  ByteSwap,
  Call,
  Store,
  Input,
  Catch,
  Branch,
  Jump,
  Return,
  Throw,
  Unreachable,
};

enum class CfgEdgeKind : std::uint8_t {
  Normal,
  True,
  False,
  LoopBack,
  Exceptional,
  Cleanup,
};

enum class AccessKind : std::uint8_t {
  Read,
  Write,
  ReadWrite,
};

enum class AliasClass : std::uint8_t {
  KnownObject,
  UnknownExternal,
};

enum class ValueCategory : std::uint8_t {
  RValue,
  LValue,
};

enum class ObjectStorage : std::uint8_t {
  Global,
  Local,
  StaticLocal,
  Parameter,
  Catch,
};

enum InterpretationFlag : std::uint32_t {
  InterpretationNone = 0,
  InterpretationReinterpreted = 1U << 0U,
  InterpretationDynamicLength = 1U << 1U,
  InterpretationAddressable = 1U << 2U,
};

enum FunctionEffectFlag : std::uint32_t {
  FunctionEffectNone = 0,
  FunctionEffectRead = 1U << 0U,
  FunctionEffectWrite = 1U << 1U,
  FunctionEffectAllocates = 1U << 2U,
  FunctionEffectFrees = 1U << 3U,
  FunctionEffectThrows = 1U << 4U,
  FunctionEffectNothrow = 1U << 5U,
  FunctionEffectIo = 1U << 6U,
  FunctionEffectUnknown = 1U << 7U,
};

struct SourceMapRecord final {
  StringId file = InvalidId;
  std::uint32_t beginLine = 0;
  std::uint32_t beginColumn = 0;
  std::uint32_t endLine = 0;
  std::uint32_t endColumn = 0;
};

struct TemplateRecord final {
  StringId name = InvalidId;
  std::uint32_t byteLength = 0;
  bool isDynamicLength = false;
};

struct FunctionRecord final {
  StringId name = InvalidId;
  BlockId entryBlock = InvalidId;
  BlockId firstBlock = InvalidId;
  std::uint32_t blockCount = 0;
  ViewId parameterViewBegin = 0;
  std::uint32_t parameterViewCount = 0;
  ViewId returnViewBegin = 0;
  std::uint32_t returnViewCount = 0;
  SourceMapId sourceMap = InvalidId;
  std::uint32_t declaredEffects = FunctionEffectNone;
  bool hasExplicitEffectContract = false;
};

struct BlockRecord final {
  FunctionId function = InvalidId;
  InstructionId firstInstruction = InvalidId;
  std::uint32_t instructionCount = 0;
  std::uint32_t successorBegin = 0;
  std::uint32_t successorCount = 0;
  std::uint32_t predecessorBegin = 0;
  std::uint32_t predecessorCount = 0;
};

struct ObjectRecord final {
  FunctionId function = InvalidId;
  StringId bindingName = InvalidId;
  std::uint32_t byteLength = 0;
  ObjectStorage storage = ObjectStorage::Local;
  TemplateId templateId = InvalidId;
  SourceMapId sourceMap = InvalidId;
};

struct ViewRecord final {
  TemplateId templateId = InvalidId;
  std::uint32_t byteLength = 0;
  ValueCategory category = ValueCategory::RValue;
  std::uint32_t interpretationFlags = InterpretationNone;
  ObjectId object = InvalidId;
  std::uint32_t offset = 0;
};

struct ObjectSliceRecord final {
  ObjectId object = InvalidId;
  ValueId dynamicOffset = InvalidId;
  ValueId dynamicLength = InvalidId;
  std::uint32_t offset = 0;
  std::uint32_t byteLength = 0;
  AccessKind access = AccessKind::Read;
  AliasClass aliasClass = AliasClass::UnknownExternal;
};

struct ValueRecord final {
  InstructionId definition = InvalidId;
  ViewId view = InvalidId;
};

struct InstructionRecord final {
  Opcode opcode = Opcode::Invalid;
  BlockId block = InvalidId;
  OperandId operandBegin = 0;
  std::uint32_t operandCount = 0;
  ValueId resultBegin = 0;
  std::uint32_t resultCount = 0;
  ObjectSliceRecord slice{};
  StringId symbol = InvalidId;
  SourceMapId sourceMap = InvalidId;
  std::uint32_t auxiliary0 = 0;
  std::uint32_t auxiliary1 = 0;
};

struct CfgEdgeRecord final {
  BlockId from = InvalidId;
  BlockId to = InvalidId;
  CfgEdgeKind kind = CfgEdgeKind::Normal;
};

static_assert(std::is_trivially_copyable_v<SourceMapRecord>);
static_assert(std::is_trivially_copyable_v<TemplateRecord>);
static_assert(std::is_trivially_copyable_v<FunctionRecord>);
static_assert(std::is_trivially_copyable_v<BlockRecord>);
static_assert(std::is_trivially_copyable_v<ObjectRecord>);
static_assert(std::is_trivially_copyable_v<ViewRecord>);
static_assert(std::is_trivially_copyable_v<ObjectSliceRecord>);
static_assert(std::is_trivially_copyable_v<ValueRecord>);
static_assert(std::is_trivially_copyable_v<InstructionRecord>);
static_assert(std::is_trivially_copyable_v<CfgEdgeRecord>);

struct Module final {
  std::uint32_t schemaVersion = SchemaVersion;
  std::vector<std::string> strings;
  std::vector<TemplateRecord> templates;
  std::vector<SourceMapRecord> sourceMaps;
  std::vector<FunctionRecord> functions;
  std::vector<BlockRecord> blocks;
  std::vector<ObjectRecord> objects;
  std::vector<ViewRecord> views;
  std::vector<ValueRecord> values;
  std::vector<InstructionRecord> instructions;
  std::vector<ValueId> operands;
  std::vector<ValueId> results;
  std::vector<CfgEdgeRecord> edges;
  std::vector<CfgEdgeId> successorEdges;
  std::vector<CfgEdgeId> predecessorEdges;
};

struct Statistics final {
  std::size_t functionCount = 0;
  std::size_t blockCount = 0;
  std::size_t instructionCount = 0;
  std::size_t edgeCount = 0;
  std::size_t objectCount = 0;
  std::size_t viewCount = 0;
};

std::string_view toString(Opcode opcode);
std::string_view toString(CfgEdgeKind kind);
std::string_view toString(AccessKind kind);
std::string_view toString(AliasClass aliasClass);
std::string_view toString(ValueCategory category);
std::string_view toString(ObjectStorage storage);

Statistics statistics(const Module &module);

} // namespace hitsimple::flowir
