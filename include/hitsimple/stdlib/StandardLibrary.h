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
};

enum class BuiltinReturnMode : std::uint8_t {
  Void,
  Fixed,
  ArgumentLength,
  DynamicLength,
  LeftContext,
};

enum class BuiltinLowering : std::uint8_t {
  RuntimeBridge,
  LibcBridge,
  Intrinsic,
  SemanticOnly,
  Format,
};

struct BuiltinSpec final {
  BuiltinId id = BuiltinId::None;
  std::string_view name;
  StandardHeader header = StandardHeader::Stdlib;
  std::string_view signature;
  BuiltinReturnMode returnMode = BuiltinReturnMode::Void;
  BuiltinLowering lowering = BuiltinLowering::RuntimeBridge;
};

std::span<const BuiltinSpec> builtinSpecs();
const BuiltinSpec* findBuiltin(std::string_view name);
const BuiltinSpec* findBuiltin(BuiltinId id);

std::string_view headerName(StandardHeader header);
std::string_view headerGuard(StandardHeader header);
const StandardHeader* findStandardHeader(std::string_view name);
std::span<const StandardHeader> allStandardHeaders();

bool isRemovedLegacyName(std::string_view name);
std::string_view replacementForRemovedLegacyName(std::string_view name);

} // namespace hitsimple::stdlib
