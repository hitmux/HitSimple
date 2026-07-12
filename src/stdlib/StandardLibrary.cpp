#include "hitsimple/stdlib/StandardLibrary.h"

#include <array>

namespace hitsimple::stdlib {
namespace {

using enum BuiltinId;
using enum BuiltinLowering;
using enum BuiltinReturnMode;

constexpr std::array<BuiltinSpec, 75> kBuiltins = {{
    {Length, "length", StandardHeader::Stdlib, "length(x as view) -> u64", Fixed, SemanticOnly},
    {Alloc, "alloc", StandardHeader::Stdlib, "alloc(size as u64) -> addr", Fixed, RuntimeBridge},
    {Calloc, "calloc", StandardHeader::Stdlib, "calloc(count as u64, size as u64) -> addr", Fixed, RuntimeBridge},
    {Realloc, "realloc", StandardHeader::Stdlib, "realloc(ptr as addr, size as u64) -> addr", Fixed, RuntimeBridge},
    {Free, "free", StandardHeader::Stdlib, "free(ptr as addr) -> ()", Void, RuntimeBridge},
    {Memset, "memset", StandardHeader::String, "memset(dst as mem_lview, value as view, len as u64) -> addr", Fixed, LibcBridge},
    {Memcpy, "memcpy", StandardHeader::String, "memcpy(dst as mem_lview, src as mem_view, len as u64) -> addr", Fixed, LibcBridge},
    {Memmove, "memmove", StandardHeader::String, "memmove(dst as mem_lview, src as mem_view, len as u64) -> addr", Fixed, LibcBridge},
    {Memcmp, "memcmp", StandardHeader::String, "memcmp(a as mem_view, b as mem_view, len as u64) -> i32", Fixed, LibcBridge},
    {ResizeBytes, "resize_bytes", StandardHeader::Stdlib, "resize_bytes(expr as view, length as u64) -> none[length]", DynamicLength, SemanticOnly},
    {ByteSwap, "byte_swap", StandardHeader::Stdlib, "byte_swap(expr as view) -> none[length(expr)]", ArgumentLength, Intrinsic},
    {ToF16, "to_f16", StandardHeader::Stdlib, "to_f16(data as view) -> f16", Fixed, Intrinsic},
    {ToF32, "to_f32", StandardHeader::Stdlib, "to_f32(data as view) -> f32", Fixed, Intrinsic},
    {ToF64, "to_f64", StandardHeader::Stdlib, "to_f64(data as view) -> f64", Fixed, Intrinsic},
    {ToF128, "to_f128", StandardHeader::Stdlib, "to_f128(data as view) -> f128", Fixed, Intrinsic},
    {ToI8, "to_i8", StandardHeader::Stdlib, "to_i8(data as view) -> i8", Fixed, Intrinsic},
    {ToI16, "to_i16", StandardHeader::Stdlib, "to_i16(data as view) -> i16", Fixed, Intrinsic},
    {ToI32, "to_i32", StandardHeader::Stdlib, "to_i32(data as view) -> i32", Fixed, Intrinsic},
    {ToI64, "to_i64", StandardHeader::Stdlib, "to_i64(data as view) -> i64", Fixed, Intrinsic},
    {ToU8, "to_u8", StandardHeader::Stdlib, "to_u8(data as view) -> u8", Fixed, Intrinsic},
    {ToU16, "to_u16", StandardHeader::Stdlib, "to_u16(data as view) -> u16", Fixed, Intrinsic},
    {ToU32, "to_u32", StandardHeader::Stdlib, "to_u32(data as view) -> u32", Fixed, Intrinsic},
    {ToU64, "to_u64", StandardHeader::Stdlib, "to_u64(data as view) -> u64", Fixed, Intrinsic},
    {Strlen, "strlen", StandardHeader::String, "strlen(s as cstr_view) -> u64", Fixed, LibcBridge},
    {Strcmp, "strcmp", StandardHeader::String, "strcmp(a as cstr_view, b as cstr_view) -> i32", Fixed, LibcBridge},
    {Strcpy, "strcpy", StandardHeader::String, "strcpy(dst as lview, src as cstr_view) -> addr", Fixed, LibcBridge},
    {Strncpy, "strncpy", StandardHeader::String, "strncpy(dst as lview, src as cstr_view, n as u64) -> addr", Fixed, LibcBridge},
    {Strcat, "strcat", StandardHeader::String, "strcat(dst as lview, src as cstr_view) -> addr", Fixed, LibcBridge},
    {Strchr, "strchr", StandardHeader::String, "strchr(s as cstr_view, ch as view) -> addr", Fixed, LibcBridge},
    {Get, "get", StandardHeader::Stdio, "get() -> i32", Fixed, LibcBridge},
    {Put, "put", StandardHeader::Stdio, "put(x as view) -> i32", Fixed, LibcBridge},
    {Print, "print", StandardHeader::Stdio, "print(x as view) -> i32", Fixed, Format},
    {Printf, "printf", StandardHeader::Stdio, "printf(fmt as cstr_view, ...) -> i32", Fixed, Format},
    {Scanf, "scanf", StandardHeader::Stdio, "scanf(fmt as cstr_view) -> left-context", LeftContext, Format},
    {Fopen, "fopen", StandardHeader::Stdio, "fopen(name as cstr_view, mode as cstr_view) -> handle", Fixed, LibcBridge},
    {Fclose, "fclose", StandardHeader::Stdio, "fclose(fh as handle) -> i32", Fixed, LibcBridge},
    {Fget, "fget", StandardHeader::Stdio, "fget(fh as handle) -> i32", Fixed, LibcBridge},
    {Fput, "fput", StandardHeader::Stdio, "fput(fh as handle, x as view) -> i32", Fixed, LibcBridge},
    {Fread, "fread", StandardHeader::Stdio, "fread(dst as lview, size as u64, count as u64, fh as handle) -> u64", Fixed, LibcBridge},
    {Fwrite, "fwrite", StandardHeader::Stdio, "fwrite(src as view, size as u64, count as u64, fh as handle) -> u64", Fixed, LibcBridge},
    {Fprintf, "fprintf", StandardHeader::Stdio, "fprintf(fh as handle, fmt as cstr_view, ...) -> i32", Fixed, Format},
    {Fscanf, "fscanf", StandardHeader::Stdio, "fscanf(fh as handle, fmt as cstr_view) -> left-context", LeftContext, Format},
    {Fflush, "fflush", StandardHeader::Stdio, "fflush(fh as handle) -> i32", Fixed, LibcBridge},
    {Fseek, "fseek", StandardHeader::Stdio, "fseek(fh as handle, offset as i64, whence as i32) -> i32", Fixed, LibcBridge},
    {Ftell, "ftell", StandardHeader::Stdio, "ftell(fh as handle) -> i64", Fixed, LibcBridge},
    {Feof, "feof", StandardHeader::Stdio, "feof(fh as handle) -> bool", Fixed, LibcBridge},
    {Ferror, "ferror", StandardHeader::Stdio, "ferror(fh as handle) -> i32", Fixed, LibcBridge},
    {Abs, "abs", StandardHeader::Stdlib, "abs(x as iN) -> iN", ArgumentLength, Intrinsic},
    {Min, "min", StandardHeader::Stdlib, "min(a as T, b as T) -> T", ArgumentLength, Intrinsic},
    {Max, "max", StandardHeader::Stdlib, "max(a as T, b as T) -> T", ArgumentLength, Intrinsic},
    {FAbs, "f_abs", StandardHeader::Math, "f_abs(x as fN) -> fN", ArgumentLength, Intrinsic},
    {FSqrt, "f_sqrt", StandardHeader::Math, "f_sqrt(x as fN) -> fN", ArgumentLength, Intrinsic},
    {FPow, "f_pow", StandardHeader::Math, "f_pow(x as fN, y as fN) -> fN", ArgumentLength, Intrinsic},
    {FSin, "f_sin", StandardHeader::Math, "f_sin(x as fN) -> fN", ArgumentLength, Intrinsic},
    {FCos, "f_cos", StandardHeader::Math, "f_cos(x as fN) -> fN", ArgumentLength, Intrinsic},
    {FTan, "f_tan", StandardHeader::Math, "f_tan(x as fN) -> fN", ArgumentLength, Intrinsic},
    {FLog, "f_log", StandardHeader::Math, "f_log(x as fN) -> fN", ArgumentLength, Intrinsic},
    {FExp, "f_exp", StandardHeader::Math, "f_exp(x as fN) -> fN", ArgumentLength, Intrinsic},
    {FFloor, "f_floor", StandardHeader::Math, "f_floor(x as fN) -> fN", ArgumentLength, Intrinsic},
    {FCeil, "f_ceil", StandardHeader::Math, "f_ceil(x as fN) -> fN", ArgumentLength, Intrinsic},
    {FRound, "f_round", StandardHeader::Math, "f_round(x as fN) -> fN", ArgumentLength, Intrinsic},
    {IsDigit, "is_digit", StandardHeader::Ctype, "is_digit(ch as view) -> bool", Fixed, Intrinsic},
    {IsAlpha, "is_alpha", StandardHeader::Ctype, "is_alpha(ch as view) -> bool", Fixed, Intrinsic},
    {IsAlnum, "is_alnum", StandardHeader::Ctype, "is_alnum(ch as view) -> bool", Fixed, Intrinsic},
    {IsSpace, "is_space", StandardHeader::Ctype, "is_space(ch as view) -> bool", Fixed, Intrinsic},
    {ToUpper, "to_upper", StandardHeader::Ctype, "to_upper(ch as view) -> u8", Fixed, Intrinsic},
    {ToLower, "to_lower", StandardHeader::Ctype, "to_lower(ch as view) -> u8", Fixed, Intrinsic},
    {Srand, "srand", StandardHeader::Stdlib, "srand(seed as u32) -> ()", Void, LibcBridge},
    {Rand, "rand", StandardHeader::Stdlib, "rand() -> u32", Fixed, LibcBridge},
    {TimeMs, "time_ms", StandardHeader::Time, "time_ms() -> u64", Fixed, RuntimeBridge},
    {ClockMs, "clock_ms", StandardHeader::Time, "clock_ms() -> u64", Fixed, RuntimeBridge},
    {Exit, "exit", StandardHeader::Stdlib, "exit(code as i32) -> ()", Void, LibcBridge},
    {Abort, "abort", StandardHeader::Stdlib, "abort() -> ()", Void, LibcBridge},
    {BuiltinId::Assert, "assert", StandardHeader::Assert, "assert(condition as view, error_code as i32) -> ()", Void, RuntimeBridge},
    {Panic, "panic", StandardHeader::Assert, "panic(error_code as i32) -> ()", Void, RuntimeBridge},
}};

constexpr std::array<StandardHeader, 7> kHeaders = {
    StandardHeader::Stdlib, StandardHeader::String, StandardHeader::Stdio,
    StandardHeader::Math, StandardHeader::Ctype, StandardHeader::Time,
    StandardHeader::Assert,
};

} // namespace

std::span<const BuiltinSpec> builtinSpecs() { return kBuiltins; }

const BuiltinSpec* findBuiltin(std::string_view name) {
  for (const auto& spec : kBuiltins) {
    if (spec.name == name) {
      return &spec;
    }
  }
  return nullptr;
}

const BuiltinSpec* findBuiltin(BuiltinId id) {
  for (const auto& spec : kBuiltins) {
    if (spec.id == id) {
      return &spec;
    }
  }
  return nullptr;
}

std::string_view headerName(StandardHeader header) {
  switch (header) {
  case StandardHeader::Stdlib: return "stdlib.hsh";
  case StandardHeader::String: return "string.hsh";
  case StandardHeader::Stdio: return "stdio.hsh";
  case StandardHeader::Math: return "math.hsh";
  case StandardHeader::Ctype: return "ctype.hsh";
  case StandardHeader::Time: return "time.hsh";
  case StandardHeader::Assert: return "assert.hsh";
  }
  return {};
}

std::string_view headerGuard(StandardHeader header) {
  switch (header) {
  case StandardHeader::Stdlib: return "HITSIMPLE_STDLIB_HSH";
  case StandardHeader::String: return "HITSIMPLE_STRING_HSH";
  case StandardHeader::Stdio: return "HITSIMPLE_STDIO_HSH";
  case StandardHeader::Math: return "HITSIMPLE_MATH_HSH";
  case StandardHeader::Ctype: return "HITSIMPLE_CTYPE_HSH";
  case StandardHeader::Time: return "HITSIMPLE_TIME_HSH";
  case StandardHeader::Assert: return "HITSIMPLE_ASSERT_HSH";
  }
  return {};
}

const StandardHeader* findStandardHeader(std::string_view name) {
  for (const auto& header : kHeaders) {
    if (headerName(header) == name) {
      return &header;
    }
  }
  return nullptr;
}

std::span<const StandardHeader> allStandardHeaders() { return kHeaders; }

bool isRemovedLegacyName(std::string_view name) {
  return name == "core.hsh" || name == "to_float" || name == "to_int" ||
         name == "reinterpret";
}

std::string_view replacementForRemovedLegacyName(std::string_view name) {
  if (name == "core.hsh") return "the grouped standard headers";
  if (name == "to_float") return "to_f16(), to_f32(), to_f64(), or to_f128()";
  if (name == "to_int") return "to_i8(), to_i16(), to_i32(), to_i64(), or to_uN()";
  if (name == "reinterpret") return "resize_bytes()";
  return {};
}

} // namespace hitsimple::stdlib
