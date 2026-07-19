#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace hitsimple::stdlib {

enum class StandardHeader : std::uint8_t {
  Stdlib,
  String,
  Stdio,
  Math,
  Ctype,
  Time,
  Assert,
  Count,
};

enum class BuiltinId : std::uint8_t {
  None,
  Length,
  Alloc,
  Calloc,
  Realloc,
  Free,
  Memset,
  Memcpy,
  Memmove,
  Memcmp,
  ResizeBytes,
  ByteSwap,
  ToF16,
  ToF32,
  ToF64,
  ToF128,
  ToI8,
  ToI16,
  ToI32,
  ToI64,
  ToU8,
  ToU16,
  ToU32,
  ToU64,
  Strlen,
  Strcmp,
  Strcpy,
  Strncpy,
  Strcat,
  Strchr,
  Get,
  Put,
  Print,
  Printf,
  Scanf,
  Fopen,
  Fclose,
  Fget,
  Fput,
  Fread,
  Fwrite,
  Fprintf,
  Fscanf,
  Fflush,
  Fseek,
  Ftell,
  Feof,
  Ferror,
  Abs,
  Min,
  Max,
  FAbs,
  FSqrt,
  FPow,
  FSin,
  FCos,
  FTan,
  FLog,
  FExp,
  FFloor,
  FCeil,
  FRound,
  IsDigit,
  IsAlpha,
  IsAlnum,
  IsSpace,
  ToUpper,
  ToLower,
  Srand,
  Rand,
  TimeMs,
  ClockMs,
  Exit,
  Abort,
  Assert,
  Panic,
  Count,
};

enum class BuiltinVisibility : std::uint8_t {
  Public,
  Internal,
};

enum class BuiltinProvider : std::uint8_t {
  None,
  Semantic,
  Intrinsic,
  CoreHs,
  RuntimeBridge,
  LibcBridge,
  FormatProtocol,
};

// This selection is intentionally only exposed to compiler development and
// regression tests. It does not form part of the language or package ABI.
enum class BuiltinProviderSelection : std::uint8_t {
  Optimized,
  Reference,
};

enum class BuiltinReturnMode : std::uint8_t {
  Void,
  Fixed,
  ArgumentLength,
  DynamicLength,
  LeftContext,
};

// These modes retain the Chapter 14 View contract independently from the
// concrete bootstrap ABI shape used while sema enters a builtin signature.
enum class BuiltinParameterMode : std::uint8_t {
  View,
  LView,
  MemView,
  MemLView,
  CStrView,
  BytesView,
  Addr,
  Handle,
  Bool,
  I8,
  I16,
  I32,
  I64,
  U8,
  U16,
  U32,
  U64,
  F16,
  F32,
  F64,
  F128,
  Integer,
  Floating,
  SameType,
  None,
  LeftContext,
  Varargs,
};

enum class BuiltinBootstrapType : std::uint8_t {
  Void,
  Pointer,
  Bytes1,
  Bytes2,
  Bytes4,
  Bytes8,
  Bytes16,
};

struct BuiltinParameter final {
  BuiltinParameterMode mode = BuiltinParameterMode::View;
  BuiltinBootstrapType bootstrapType = BuiltinBootstrapType::Void;
  std::string_view templateName;
  bool requiresCString = false;
};

struct BuiltinResult final {
  BuiltinParameterMode mode = BuiltinParameterMode::None;
  BuiltinBootstrapType bootstrapType = BuiltinBootstrapType::Void;
  std::string_view templateName;
};

struct BuiltinOverload final {
  std::span<const BuiltinParameterMode> parameterModes;
  std::span<const BuiltinParameterMode> resultModes;
};

struct BuiltinSpec final {
  BuiltinId id = BuiltinId::None;
  std::string_view name;
  BuiltinVisibility visibility = BuiltinVisibility::Internal;
  std::string_view standardSection;
  StandardHeader header = StandardHeader::Stdlib;
  std::span<const BuiltinParameter> parameters;
  std::span<const BuiltinResult> results;
  std::span<const BuiltinOverload> overloads;
  BuiltinReturnMode returnMode = BuiltinReturnMode::Void;
  BuiltinProvider provider = BuiltinProvider::None;
  BuiltinProvider referenceProvider = BuiltinProvider::None;
  std::string_view sourceModule;
  std::string_view implementationSymbol;
  std::span<const std::string_view> staticDiagnostics;
  std::span<const std::string_view> checkedObligations;
  std::span<const std::string_view> testOwners;
  std::string_view headerDeclaration;
};

struct SourceModuleSpec final {
  std::string_view id;
  std::string_view file;
  std::span<const std::string_view> dependencies;
};

struct BuiltinCallMetadata final {
  BuiltinId id = BuiltinId::None;
  BuiltinProvider provider = BuiltinProvider::None;
  BuiltinReturnMode returnMode = BuiltinReturnMode::Void;
  std::uint16_t overloadIndex = 0;
};

std::span<const BuiltinSpec> builtinSpecs();
const BuiltinSpec* findBuiltin(std::string_view name);
const BuiltinSpec* findBuiltin(BuiltinId id);
bool isStandardLibraryImplementationSymbol(std::string_view name);
BuiltinCallMetadata builtinCallMetadata(BuiltinId id,
                                        std::uint16_t overloadIndex = 0);
std::uint16_t findBuiltinOverload(
    BuiltinId id, std::span<const std::string_view> argumentTemplates);

std::string_view headerName(StandardHeader header);
std::string_view headerGuard(StandardHeader header);
const StandardHeader* findStandardHeader(std::string_view name);
std::span<const StandardHeader> allStandardHeaders();

std::span<const SourceModuleSpec> sourceModuleSpecs();
const SourceModuleSpec* findSourceModule(std::string_view id);

std::string_view toString(BuiltinProvider provider);
std::string_view toString(BuiltinProviderSelection selection);
std::string_view toString(BuiltinReturnMode mode);
std::string_view toString(BuiltinParameterMode mode);

bool isRemovedLegacyName(std::string_view name);
std::string_view replacementForRemovedLegacyName(std::string_view name);

} // namespace hitsimple::stdlib
