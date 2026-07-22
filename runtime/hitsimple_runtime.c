#define __STDC_WANT_IEC_60559_TYPES_EXT__ 1

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32)
#include <sys/types.h>
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(_WIN32) || defined(HITSIMPLE_SOFTWARE_F128)
#define HS_SOFTWARE_F128 1
extern int hs_f128_format(const void *bits, char *buffer, size_t capacity);
extern int hs_f128_parse(const char *text, void *bits);
#elif defined(__clang__) && defined(__GLIBC__) && defined(__x86_64__) && \
    defined(__SIZEOF_FLOAT128__) && \
    (!defined(__HAVE_FLOAT128) || !__HAVE_FLOAT128)
/* glibc 2.35 treats Clang's GCC compatibility version as too old to expose
   its binary128 typedef and conversion declarations on x86_64. */
typedef __float128 HsFloat128;
extern __float128 strtof128(const char *restrict, char **restrict);
extern int strfromf128(char *restrict, size_t, const char *restrict,
                       __float128);
#else
typedef _Float128 HsFloat128;
#endif

#ifndef HS_SOFTWARE_F128
_Static_assert(sizeof(HsFloat128) == 16,
               "HitSimple f128 requires IEEE 754 binary128");

#endif

enum {
  HS_MAX_OBJECTS = 1024,
  HS_MAX_FRAMES = 1024,
  HS_RUNTIME_ERROR = 120,
};

typedef enum {
  HS_OBJECT_HEAP,
  HS_OBJECT_STATIC,
  HS_OBJECT_LOCAL,
  HS_OBJECT_TEMPORARY,
} HsObjectKind;

typedef struct {
  void *ptr;
  uint64_t size;
  uint64_t frame;
  uint64_t scope;
  uint64_t registration_id;
  HsObjectKind kind;
  int freed;
} HsAlloc;

typedef struct {
  const void *data;
  uint64_t length;
  uint32_t kind;
} HsFormatArg;

typedef struct {
  void *data;
  uint64_t capacity;
  uint32_t kind;
} HsScanTarget;

enum {
  HS_FORMAT_BYTES = 0,
  HS_FORMAT_FLOAT = 1,
  HS_FORMAT_STRING = 2,
};

enum {
  HS_SCAN_ANY = 0,
  HS_SCAN_FLOAT = 1,
  HS_SCAN_POINTER = 2,
  HS_SCAN_STRING = 3,
};

static HsAlloc hs_allocs[HS_MAX_OBJECTS];
static uint64_t hs_current_frame;
static uint64_t hs_frame_scopes[HS_MAX_FRAMES];

#if defined(_WIN32)
#define HS_THREAD_LOCAL __declspec(thread)
#else
#define HS_THREAD_LOCAL _Thread_local
#endif

typedef struct {
  const char *file;
  uint64_t line;
  uint64_t column;
} HsRuntimeSourceLocation;

static HS_THREAD_LOCAL HsRuntimeSourceLocation hs_runtime_source_location;

void hs_set_source_location(const char *file, uint64_t line, uint64_t column) {
  hs_runtime_source_location.file = file;
  hs_runtime_source_location.line = line;
  hs_runtime_source_location.column = column;
}

static void hs_fail(const char *message) {
  fprintf(stderr, "hitsimple runtime error: %s", message);
  if (hs_runtime_source_location.file != NULL &&
      hs_runtime_source_location.line != 0 &&
      hs_runtime_source_location.column != 0) {
    fprintf(stderr, " at %s:%llu:%llu", hs_runtime_source_location.file,
            (unsigned long long)hs_runtime_source_location.line,
            (unsigned long long)hs_runtime_source_location.column);
  }
  fputc('\n', stderr);
  exit(HS_RUNTIME_ERROR);
}

void hs_abs_overflow(void) { hs_fail("abs of minimum signed value"); }

static HsAlloc *hs_find(void *ptr) {
  uintptr_t address = (uintptr_t)ptr;
  for (int i = 0; i < HS_MAX_OBJECTS; ++i) {
    if (hs_allocs[i].ptr == NULL) {
      continue;
    }
    uintptr_t begin = (uintptr_t)hs_allocs[i].ptr;
    if (address >= begin && address - begin < hs_allocs[i].size) {
      return &hs_allocs[i];
    }
  }
  return NULL;
}

static HsAlloc *hs_slot(void) {
  for (int i = 0; i < HS_MAX_OBJECTS; ++i) {
    if (hs_allocs[i].ptr == NULL || hs_allocs[i].freed) {
      return &hs_allocs[i];
    }
  }
  hs_fail("object registry is full");
  return NULL;
}

static HsAlloc *hs_find_exact(void *ptr, HsObjectKind kind,
                              uint64_t frame, uint64_t scope) {
  for (int i = 0; i < HS_MAX_OBJECTS; ++i) {
    if (hs_allocs[i].ptr == ptr && hs_allocs[i].kind == kind &&
        hs_allocs[i].frame == frame && hs_allocs[i].scope == scope &&
        !hs_allocs[i].freed) {
      return &hs_allocs[i];
    }
  }
  return NULL;
}

static HsAlloc *hs_find_temporary(uint64_t frame, uint64_t scope,
                                  uint64_t registration_id) {
  for (int i = 0; i < HS_MAX_OBJECTS; ++i) {
    if (hs_allocs[i].ptr != NULL &&
        hs_allocs[i].kind == HS_OBJECT_TEMPORARY &&
        hs_allocs[i].frame == frame && hs_allocs[i].scope == scope &&
        hs_allocs[i].registration_id == registration_id &&
        !hs_allocs[i].freed) {
      return &hs_allocs[i];
    }
  }
  return NULL;
}

static void hs_clear(HsAlloc *allocation) {
  allocation->ptr = NULL;
  allocation->size = 0;
  allocation->frame = 0;
  allocation->scope = 0;
  allocation->registration_id = 0;
  allocation->freed = 0;
}

static void hs_register(void *ptr, uint64_t size, HsObjectKind kind,
                        uint64_t frame, uint64_t scope) {
  if (ptr == NULL || size == 0) {
    hs_fail("invalid object registration");
  }

  HsAlloc *existing = hs_find_exact(ptr, kind, frame, scope);
  if (existing != NULL) {
    if (existing->size != size) {
      hs_fail("conflicting object registration");
    }
    return;
  }

  HsAlloc *slot = hs_slot();
  slot->ptr = ptr;
  slot->size = size;
  slot->frame = frame;
  slot->scope = scope;
  slot->registration_id = 0;
  slot->kind = kind;
  slot->freed = 0;
}

void hs_frame_enter(void) {
  if (hs_current_frame + 1U >= HS_MAX_FRAMES) {
    hs_fail("runtime frame depth overflow");
  }
  ++hs_current_frame;
  hs_frame_scopes[hs_current_frame] = 0;
}

void hs_frame_exit(void) {
  if (hs_current_frame == 0) {
    hs_fail("runtime frame underflow");
  }

  for (int i = 0; i < HS_MAX_OBJECTS; ++i) {
    if (hs_allocs[i].ptr != NULL &&
        (hs_allocs[i].kind == HS_OBJECT_LOCAL ||
         hs_allocs[i].kind == HS_OBJECT_TEMPORARY) &&
        hs_allocs[i].frame == hs_current_frame) {
      hs_clear(&hs_allocs[i]);
    }
  }
  hs_frame_scopes[hs_current_frame] = 0;
  --hs_current_frame;
}

void hs_scope_enter(void) {
  if (hs_current_frame == 0) {
    hs_fail("scope entered outside a runtime frame");
  }
  if (hs_frame_scopes[hs_current_frame] == UINT64_MAX) {
    hs_fail("runtime scope depth overflow");
  }
  ++hs_frame_scopes[hs_current_frame];
}

void hs_scope_exit(void) {
  if (hs_current_frame == 0 || hs_frame_scopes[hs_current_frame] == 0) {
    hs_fail("runtime scope underflow");
  }

  const uint64_t scope = hs_frame_scopes[hs_current_frame];
  for (int i = 0; i < HS_MAX_OBJECTS; ++i) {
    if (hs_allocs[i].ptr != NULL &&
        (hs_allocs[i].kind == HS_OBJECT_LOCAL ||
         hs_allocs[i].kind == HS_OBJECT_TEMPORARY) &&
        hs_allocs[i].frame == hs_current_frame && hs_allocs[i].scope == scope) {
      hs_clear(&hs_allocs[i]);
    }
  }
  --hs_frame_scopes[hs_current_frame];
}

void hs_scope_exit_to(uint64_t target_scope) {
  if (hs_current_frame == 0 || target_scope > hs_frame_scopes[hs_current_frame]) {
    hs_fail("invalid runtime scope exit");
  }
  while (hs_frame_scopes[hs_current_frame] > target_scope) {
    hs_scope_exit();
  }
}

void hs_register_local(void *ptr, uint64_t size) {
  if (hs_current_frame == 0) {
    hs_fail("local object registered outside a runtime frame");
  }
  hs_register(ptr, size, HS_OBJECT_LOCAL, hs_current_frame,
              hs_frame_scopes[hs_current_frame]);
}

void hs_register_temporary(void *ptr, uint64_t size,
                           uint64_t registration_id) {
  if (hs_current_frame == 0) {
    hs_fail("temporary object registered outside a runtime frame");
  }
  if (ptr == NULL || size == 0) {
    hs_fail("invalid object registration");
  }

  const uint64_t scope = hs_frame_scopes[hs_current_frame];
  HsAlloc *existing =
      hs_find_temporary(hs_current_frame, scope, registration_id);
  if (existing != NULL) {
    existing->ptr = ptr;
    existing->size = size;
    return;
  }

  HsAlloc *slot = hs_slot();
  slot->ptr = ptr;
  slot->size = size;
  slot->frame = hs_current_frame;
  slot->scope = scope;
  slot->registration_id = registration_id;
  slot->kind = HS_OBJECT_TEMPORARY;
  slot->freed = 0;
}

void hs_register_static(void *ptr, uint64_t size) {
  hs_register(ptr, size, HS_OBJECT_STATIC, 0, 0);
}

void hs_free(void *ptr);

void *hs_alloc(uint64_t size) {
  if (size == 0) {
    return NULL;
  }
  void *ptr = malloc((size_t)size);
  if (ptr == NULL) {
    hs_fail("allocation returned null");
  }
  hs_register(ptr, size, HS_OBJECT_HEAP, 0, 0);
  return ptr;
}

void *hs_calloc(uint64_t count, uint64_t size) {
  if (count != 0 && size > UINT64_MAX / count) {
    hs_fail("calloc size overflow");
  }
  const uint64_t total = count * size;
  if (total == 0) {
    return NULL;
  }
  void *ptr = calloc(1, (size_t)total);
  if (ptr == NULL) {
    hs_fail("allocation returned null");
  }
  hs_register(ptr, total, HS_OBJECT_HEAP, 0, 0);
  return ptr;
}

void *hs_realloc(void *ptr, uint64_t size) {
  if (ptr == NULL) {
    return hs_alloc(size);
  }
  if (size == 0) {
    hs_free(ptr);
    return NULL;
  }
  HsAlloc *entry = hs_find(ptr);
  if (entry == NULL || entry->freed || entry->ptr != ptr ||
      entry->kind != HS_OBJECT_HEAP) {
    hs_fail("invalid realloc");
  }
  void *next = realloc(ptr, (size_t)size);
  if (next == NULL) {
    hs_fail("realloc returned null");
  }
  entry->ptr = next;
  entry->size = size;
  entry->frame = 0;
  entry->kind = HS_OBJECT_HEAP;
  entry->freed = 0;
  return next;
}

void hs_free(void *ptr) {
  if (ptr == NULL) {
    return;
  }
  HsAlloc *entry = hs_find(ptr);
  if (entry == NULL || entry->ptr != ptr || entry->kind != HS_OBJECT_HEAP) {
    hs_fail("invalid free");
  }
  if (entry->freed) {
    hs_fail("double free");
  }
  entry->freed = 1;
  free(ptr);
}

static void hs_check(void *ptr, uint64_t size, const char *message) {
  if (size == 0) {
    return;
  }
  if (ptr == NULL) {
    hs_fail("null address");
  }
  HsAlloc *entry = hs_find(ptr);
  if (entry == NULL || entry->freed) {
    hs_fail(message);
  }
  uintptr_t address = (uintptr_t)ptr;
  uintptr_t begin = (uintptr_t)entry->ptr;
  if (size > entry->size || address - begin > entry->size - size) {
    hs_fail(message);
  }
}

void hs_check_load(void *ptr, uint64_t size) {
  hs_check(ptr, size, "invalid checked load");
}

void hs_check_store(void *ptr, uint64_t size) {
  hs_check(ptr, size, "invalid checked store");
}

void hs_check_view_length(uint64_t actual, uint64_t expected) {
  if (actual != expected) {
    hs_fail("dynamic View length does not match fixed context");
  }
}

int32_t hs_view_any_nonzero(const void *data, uint64_t length) {
  const unsigned char *bytes = (const unsigned char *)data;
  for (uint64_t index = 0; index < length; ++index) {
    if (bytes[index] != 0U) {
      return 1;
    }
  }
  return 0;
}

void hs_checked_division_by_zero(void) { hs_fail("integer division by zero"); }

void hs_checked_negative_shift(void) { hs_fail("negative shift count"); }

void hs_checked_negative_exponent(void) { hs_fail("negative exponent"); }

void hs_reverse_bytes(void *dst, const void *src, uint64_t size) {
  if (size == 0) {
    return;
  }
  if (dst == NULL || src == NULL) {
    hs_fail("invalid byte reversal address");
  }
  unsigned char *out = dst;
  const unsigned char *in = src;
  for (uint64_t index = 0; index < size; ++index) {
    out[index] = in[size - index - 1U];
  }
}

static uint64_t hs_checked_product(uint64_t size, uint64_t count,
                                   const char *message) {
  if (size != 0 && count > UINT64_MAX / size) {
    hs_fail(message);
  }
  return size * count;
}

static uint64_t hs_cstr_length(const char *value, const char *message) {
  if (value == NULL) {
    hs_fail(message);
  }
  HsAlloc *entry = hs_find((void *)value);
  if (entry == NULL || entry->freed) {
    hs_fail(message);
  }
  const uint64_t offset = (uint64_t)((uintptr_t)value -
                                     (uintptr_t)entry->ptr);
  const uint64_t remaining = entry->size - offset;
  for (uint64_t index = 0; index < remaining; ++index) {
    if (value[index] == '\0') {
      return index;
    }
  }
  hs_fail(message);
  return 0;
}

static FILE *hs_require_file(FILE *file) {
  if (file == NULL) {
    hs_fail("invalid file handle");
  }
  return file;
}

void hs_check_file_handle(FILE *file) { (void)hs_require_file(file); }

void *hs_memset(void *dst, int value, uint64_t size) {
  hs_check(dst, size, "invalid memset range");
  if (size == 0) {
    return dst;
  }
  return memset(dst, value, (size_t)size);
}

void *hs_memcpy(void *dst, const void *src, uint64_t size) {
  hs_check(dst, size, "invalid memcpy destination range");
  hs_check((void *)src, size, "invalid memcpy source range");
  if (size == 0) {
    return dst;
  }
  const uintptr_t dstBegin = (uintptr_t)dst;
  const uintptr_t srcBegin = (uintptr_t)src;
  if (dstBegin < srcBegin + size && srcBegin < dstBegin + size) {
    hs_fail("overlapping memcpy ranges");
  }
  return memcpy(dst, src, (size_t)size);
}

void *hs_memmove(void *dst, const void *src, uint64_t size) {
  hs_check(dst, size, "invalid memmove destination range");
  hs_check((void *)src, size, "invalid memmove source range");
  if (size == 0) {
    return dst;
  }
  return memmove(dst, src, (size_t)size);
}

int32_t hs_memcmp(const void *left, const void *right, uint64_t size) {
  hs_check((void *)left, size, "invalid memcmp left range");
  hs_check((void *)right, size, "invalid memcmp right range");
  if (size == 0) {
    return 0;
  }
  return memcmp(left, right, (size_t)size);
}

uint64_t hs_strlen(const char *value) {
  return hs_cstr_length(value, "unterminated checked string");
}

int32_t hs_strcmp(const char *left, const char *right) {
  const uint64_t leftLength = hs_cstr_length(left, "unterminated checked string");
  const uint64_t rightLength = hs_cstr_length(right, "unterminated checked string");
  const uint64_t shared = leftLength < rightLength ? leftLength : rightLength;
  const int result = memcmp(left, right, (size_t)shared);
  if (result != 0) {
    return result;
  }
  return leftLength == rightLength ? 0 : (leftLength < rightLength ? -1 : 1);
}

char *hs_strcpy(char *dst, const char *src) {
  const uint64_t sourceLength =
      hs_cstr_length(src, "unterminated checked string");
  hs_check(dst, sourceLength + 1U, "insufficient strcpy destination capacity");
  memcpy(dst, src, (size_t)(sourceLength + 1U));
  return dst;
}

char *hs_strncpy(char *dst, const char *src, uint64_t size) {
  hs_check(dst, size, "insufficient strncpy destination capacity");
  (void)hs_cstr_length(src, "unterminated checked string");
  if (size == 0) {
    return dst;
  }
  return strncpy(dst, src, (size_t)size);
}

char *hs_strcat(char *dst, const char *src) {
  const uint64_t destinationLength =
      hs_cstr_length(dst, "unterminated checked destination string");
  const uint64_t sourceLength =
      hs_cstr_length(src, "unterminated checked string");
  hs_check(dst, destinationLength + sourceLength + 1U,
           "insufficient strcat destination capacity");
  memcpy(dst + destinationLength, src, (size_t)(sourceLength + 1U));
  return dst;
}

char *hs_strchr(const char *value, int ch) {
  const uint64_t length = hs_cstr_length(value, "unterminated checked string");
  return memchr(value, ch, (size_t)(length + 1U));
}

FILE *hs_fopen(const char *name, const char *mode) {
  (void)hs_cstr_length(name, "unterminated checked file name");
  (void)hs_cstr_length(mode, "unterminated checked file mode");
  return fopen(name, mode);
}

int32_t hs_fclose(FILE *file) { return fclose(hs_require_file(file)); }

int32_t hs_fget(FILE *file) { return fgetc(hs_require_file(file)); }

int32_t hs_fflush(FILE *file) { return fflush(hs_require_file(file)); }

int32_t hs_feof(FILE *file) { return feof(hs_require_file(file)); }

int32_t hs_ferror(FILE *file) { return ferror(hs_require_file(file)); }

int32_t hs_fseek(FILE *file, int64_t offset, int32_t whence) {
#if defined(_WIN32)
  return _fseeki64(hs_require_file(file), offset, whence);
#else
  return fseeko(hs_require_file(file), (off_t)offset, whence);
#endif
}

int64_t hs_ftell(FILE *file) {
#if defined(_WIN32)
  return _ftelli64(hs_require_file(file));
#else
  return (int64_t)ftello(hs_require_file(file));
#endif
}

uint64_t hs_fread(void *dst, uint64_t size, uint64_t count, FILE *file) {
  file = hs_require_file(file);
  const uint64_t total =
      hs_checked_product(size, count, "fread size overflow");
  hs_check(dst, total, "invalid fread destination range");
  if (total == 0) {
    return 0;
  }
  return fread(dst, (size_t)size, (size_t)count, file);
}

uint64_t hs_fwrite(const void *src, uint64_t size, uint64_t count,
                   FILE *file) {
  file = hs_require_file(file);
  const uint64_t total =
      hs_checked_product(size, count, "fwrite size overflow");
  hs_check((void *)src, total, "invalid fwrite source range");
  if (total == 0) {
    return 0;
  }
  return fwrite(src, (size_t)size, (size_t)count, file);
}

int32_t hs_put(const void *src, uint64_t size) {
  hs_check((void *)src, size, "invalid put source range");
  if (size == 0) {
    return 0;
  }
  return (int32_t)fwrite(src, 1, (size_t)size, stdout);
}

int32_t hs_fput(FILE *file, const void *src, uint64_t size) {
  file = hs_require_file(file);
  hs_check((void *)src, size, "invalid fput source range");
  if (size == 0) {
    return 0;
  }
  return (int32_t)fwrite(src, 1, (size_t)size, file);
}

static uint64_t hs_read_unsigned(const HsFormatArg *argument, int checked) {
  if (argument->length == 0 || argument->length > sizeof(uint64_t)) {
    hs_fail("invalid format integer argument length");
  }
  if (checked) {
    hs_check((void *)argument->data, argument->length,
             "invalid format integer argument");
  }
  uint64_t value = 0;
  const unsigned char *bytes = argument->data;
  for (uint64_t index = 0; index < argument->length; ++index) {
    value |= (uint64_t)bytes[index] << (index * 8U);
  }
  return value;
}

static int64_t hs_read_signed(const HsFormatArg *argument, int checked) {
  const uint64_t value = hs_read_unsigned(argument, checked);
  if (argument->length == sizeof(uint64_t)) {
    return (int64_t)value;
  }
  const uint64_t sign = UINT64_C(1) << (argument->length * 8U - 1U);
  if ((value & sign) == 0) {
    return (int64_t)value;
  }
  return (int64_t)(value | (~UINT64_C(0) << (argument->length * 8U)));
}

static float hs_f16_to_f32(uint16_t value) {
  const uint32_t sign = (uint32_t)(value & 0x8000U) << 16U;
  const uint32_t exponent = (value >> 10U) & 0x1fU;
  const uint32_t fraction = value & 0x03ffU;
  uint32_t bits = 0;
  if (exponent == 0) {
    if (fraction == 0) {
      bits = sign;
    } else {
      uint32_t normalized = fraction;
      int shift = -1;
      while ((normalized & 0x0400U) == 0) {
        normalized <<= 1U;
        --shift;
      }
      normalized &= 0x03ffU;
      bits = sign | (uint32_t)(127 - 15 + 1 + shift) << 23U |
             normalized << 13U;
    }
  } else if (exponent == 0x1fU) {
    bits = sign | 0x7f800000U | fraction << 13U;
  } else {
    bits = sign | (exponent + 112U) << 23U | fraction << 13U;
  }
  float result;
  memcpy(&result, &bits, sizeof(result));
  return result;
}

static uint16_t hs_f32_to_f16(float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  const uint16_t sign = (uint16_t)(bits >> 16U) & 0x8000U;
  const uint32_t exponent = (bits >> 23U) & 0xffU;
  const uint32_t fraction = bits & 0x007fffffU;
  if (exponent == 0xffU) {
    return (uint16_t)(sign | 0x7c00U |
                      (fraction == 0 ? 0 : (fraction >> 13U) | 1U));
  }

  const int32_t halfExponent = (int32_t)exponent - 127 + 15;
  if (halfExponent >= 31) {
    return (uint16_t)(sign | 0x7c00U);
  }
  if (halfExponent <= 0) {
    if (halfExponent < -10) {
      return sign;
    }
    const uint32_t mantissa = fraction | 0x00800000U;
    const uint32_t shift = (uint32_t)(14 - halfExponent);
    uint32_t result = mantissa >> shift;
    const uint32_t remainder = mantissa & ((UINT32_C(1) << shift) - 1U);
    const uint32_t halfway = UINT32_C(1) << (shift - 1U);
    if (remainder > halfway || (remainder == halfway && (result & 1U) != 0)) {
      ++result;
    }
    return (uint16_t)(sign | result);
  }

  uint32_t result = ((uint32_t)halfExponent << 10U) | (fraction >> 13U);
  const uint32_t remainder = fraction & 0x1fffU;
  if (remainder > 0x1000U || (remainder == 0x1000U && (result & 1U) != 0)) {
    ++result;
  }
  return (uint16_t)(sign | result);
}

#ifndef HS_SOFTWARE_F128
HsFloat128 hs_f128_literal(const char *text) {
  if (text == NULL) {
    hs_fail("null f128 literal");
  }
  char *end = NULL;
  errno = 0;
  const HsFloat128 value = strtof128(text, &end);
  if (end == text || *end != '\0' || errno == ERANGE) {
    hs_fail("invalid f128 literal");
  }
  return value;
}

HsFloat128 hs_f128_add(HsFloat128 left, HsFloat128 right) {
  return left + right;
}

HsFloat128 hs_f128_sub(HsFloat128 left, HsFloat128 right) {
  return left - right;
}

HsFloat128 hs_f128_mul(HsFloat128 left, HsFloat128 right) {
  return left * right;
}

HsFloat128 hs_f128_div(HsFloat128 left, HsFloat128 right) {
  return left / right;
}

uint8_t hs_f128_eq(HsFloat128 left, HsFloat128 right) { return left == right; }
uint8_t hs_f128_ne(HsFloat128 left, HsFloat128 right) { return left != right; }
uint8_t hs_f128_lt(HsFloat128 left, HsFloat128 right) { return left < right; }
uint8_t hs_f128_le(HsFloat128 left, HsFloat128 right) { return left <= right; }
uint8_t hs_f128_gt(HsFloat128 left, HsFloat128 right) { return left > right; }
uint8_t hs_f128_ge(HsFloat128 left, HsFloat128 right) { return left >= right; }

HsFloat128 hs_f128_from_i64(int64_t value) { return (HsFloat128)value; }
HsFloat128 hs_f128_from_u64(uint64_t value) { return (HsFloat128)value; }
HsFloat128 hs_f128_from_f32(float value) { return (HsFloat128)value; }
HsFloat128 hs_f128_from_f64(double value) { return (HsFloat128)value; }
int64_t hs_f128_to_i64(HsFloat128 value) { return (int64_t)value; }
uint64_t hs_f128_to_u64(HsFloat128 value) { return (uint64_t)value; }
float hs_f128_to_f32(HsFloat128 value) { return (float)value; }
double hs_f128_to_f64(HsFloat128 value) { return (double)value; }

#endif

static int hs_write(FILE *file, const char *bytes, size_t size,
                    int64_t *written) {
  const size_t count = fwrite(bytes, 1, size, file);
  *written += (int64_t)count;
  return count == size;
}

int32_t hs_format_output(FILE *file, const char *format,
                          const HsFormatArg *arguments, uint64_t argumentCount,
                          int checked) {
  if (file == NULL) {
    file = stdout;
  }
  const uint64_t formatLength = checked
                                    ? hs_cstr_length(format,
                                                     "unterminated format string")
                                    : (uint64_t)strlen(format);
  uint64_t argumentIndex = 0;
  int64_t written = 0;
  for (uint64_t index = 0; index < formatLength; ++index) {
    if (format[index] != '%') {
      if (!hs_write(file, format + index, 1, &written)) {
        return -1;
      }
      continue;
    }
    ++index;
    if (index >= formatLength) {
      hs_fail("unterminated format specifier");
    }
    if (format[index] == '%') {
      if (!hs_write(file, "%", 1, &written)) {
        return -1;
      }
      continue;
    }
    uint64_t floatLength = 0;
    while (index < formatLength && format[index] >= '0' && format[index] <= '9') {
      const uint64_t digit = (uint64_t)(format[index] - '0');
      if (floatLength > (UINT64_MAX - digit) / 10U) {
        hs_fail("format width overflow");
      }
      floatLength = floatLength * 10U + digit;
      ++index;
    }
    if (index >= formatLength) {
      hs_fail("unterminated format specifier");
    }
    const char specifier = format[index];
    if (specifier != 'f' && floatLength != 0) {
      hs_fail("format byte length is only valid for %f");
    }
    if (argumentIndex >= argumentCount) {
      hs_fail("format item count does not match argument count");
    }
    const HsFormatArg *argument = &arguments[argumentIndex++];
    char buffer[160];
    int length = 0;
    switch (specifier) {
    case 'd':
      if (checked && argument->kind == HS_FORMAT_FLOAT) {
        hs_fail("format type mismatch for %d");
      }
      length = snprintf(buffer, sizeof(buffer), "%lld",
                        (long long)hs_read_signed(argument, checked));
      break;
    case 'u':
      if (checked && argument->kind == HS_FORMAT_FLOAT) {
        hs_fail("format type mismatch for %u");
      }
      length = snprintf(buffer, sizeof(buffer), "%llu",
                        (unsigned long long)hs_read_unsigned(argument, checked));
      break;
    case 'x':
      length = snprintf(buffer, sizeof(buffer), "%llx",
                        (unsigned long long)hs_read_unsigned(argument, checked));
      break;
    case 'o':
      length = snprintf(buffer, sizeof(buffer), "%llo",
                        (unsigned long long)hs_read_unsigned(argument, checked));
      break;
    case 'b': {
      uint64_t value = hs_read_unsigned(argument, checked);
      char reverse[65];
      size_t reverseLength = 0;
      do {
        reverse[reverseLength++] = (char)('0' + (value & 1U));
        value >>= 1U;
      } while (value != 0);
      for (size_t bit = 0; bit < reverseLength; ++bit) {
        buffer[bit] = reverse[reverseLength - bit - 1U];
      }
      length = (int)reverseLength;
      break;
    }
    case 'c':
      buffer[0] = (char)hs_read_unsigned(argument, checked);
      length = 1;
      break;
    case 'p': {
      const uintptr_t value = (uintptr_t)hs_read_unsigned(argument, checked);
      length = snprintf(buffer, sizeof(buffer), "%p", (void *)value);
      break;
    }
    case 's': {
      if (checked && argument->kind == HS_FORMAT_FLOAT) {
        hs_fail("format type mismatch for %s");
      }
      const char *text = argument->data;
      const uint64_t textLength = checked
                                      ? hs_cstr_length(text,
                                                       "unterminated format string argument")
                                      : (uint64_t)strlen(text);
      if (!hs_write(file, text, (size_t)textLength, &written)) {
        return -1;
      }
      continue;
    }
    case 'f':
      if (floatLength == 0) {
        floatLength = 8;
      }
      if (floatLength != 2 && floatLength != 4 && floatLength != 8 &&
          floatLength != 16) {
        hs_fail("invalid float format byte length");
      }
      if (checked && (argument->kind != HS_FORMAT_FLOAT ||
                      argument->length != floatLength)) {
        hs_fail("format type mismatch for %f");
      }
      if (checked) {
        hs_check((void *)argument->data, argument->length,
                 "invalid format float argument");
      }
      if (floatLength == 2) {
        uint16_t value;
        memcpy(&value, argument->data, sizeof(value));
        length = snprintf(buffer, sizeof(buffer), "%.9g",
                          (double)hs_f16_to_f32(value));
      } else if (floatLength == 4) {
        float value;
        memcpy(&value, argument->data, sizeof(value));
        length = snprintf(buffer, sizeof(buffer), "%.9g", (double)value);
      } else if (floatLength == 8) {
        double value;
        memcpy(&value, argument->data, sizeof(value));
        length = snprintf(buffer, sizeof(buffer), "%.17g", value);
      } else {
#ifdef HS_SOFTWARE_F128
        length = hs_f128_format(argument->data, buffer, sizeof(buffer));
#else
        HsFloat128 value;
        memcpy(&value, argument->data, sizeof(value));
        length = strfromf128(buffer, sizeof(buffer), "%g", value);
#endif
      }
      break;
    default:
      hs_fail("unknown format specifier");
    }
    if (length < 0 || (size_t)length >= sizeof(buffer) ||
        !hs_write(file, buffer, (size_t)length, &written)) {
      return -1;
    }
  }
  if (argumentIndex != argumentCount) {
    hs_fail("format item count does not match argument count");
  }
  return written > INT32_MAX ? INT32_MAX : (int32_t)written;
}

static FILE *hs_input_file(FILE *file) { return file == NULL ? stdin : file; }

static int hs_digit_value(int ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return ch - 'a' + 10;
  }
  if (ch >= 'A' && ch <= 'F') {
    return ch - 'A' + 10;
  }
  return -1;
}

static int hs_read_nonspace(FILE *file) {
  int ch;
  do {
    ch = fgetc(file);
  } while (ch != EOF && isspace((unsigned char)ch));
  return ch;
}

static void hs_skip_input_space(FILE *file) {
  int ch;
  do {
    ch = fgetc(file);
  } while (ch != EOF && isspace((unsigned char)ch));
  if (ch != EOF) {
    ungetc(ch, file);
  }
}

static int hs_read_integer(FILE *file, int base, int signedValue,
                           uint64_t *result) {
  int ch = hs_read_nonspace(file);
  if (ch == EOF) {
    return 0;
  }
  int negative = 0;
  if (ch == '+' || ch == '-') {
    negative = ch == '-';
    ch = fgetc(file);
  }
  if (base == 16 && ch == '0') {
    const int prefix = fgetc(file);
    if (prefix == 'x' || prefix == 'X') {
      ch = fgetc(file);
    } else if (prefix != EOF) {
      ungetc(prefix, file);
    }
  }
  uint64_t value = 0;
  int digits = 0;
  while (ch != EOF) {
    const int digit = hs_digit_value(ch);
    if (digit < 0 || digit >= base) {
      ungetc(ch, file);
      break;
    }
    if (value > (UINT64_MAX - (uint64_t)digit) / (uint64_t)base) {
      hs_fail("scan integer overflow");
    }
    value = value * (uint64_t)base + (uint64_t)digit;
    ++digits;
    ch = fgetc(file);
  }
  if (digits == 0) {
    return 0;
  }
  if (negative) {
    if (!signedValue) {
      return 0;
    }
    value = (uint64_t)(-(int64_t)value);
  }
  *result = value;
  return 1;
}

static int hs_read_float_token(FILE *file, char *buffer, size_t size) {
  int ch = hs_read_nonspace(file);
  if (ch == EOF) {
    return 0;
  }
  size_t length = 0;
  while (ch != EOF && (isalnum((unsigned char)ch) || ch == '+' || ch == '-' ||
                       ch == '.')) {
    if (length + 1 >= size) {
      hs_fail("scan float token is too long");
    }
    buffer[length++] = (char)ch;
    ch = fgetc(file);
  }
  if (ch != EOF) {
    ungetc(ch, file);
  }
  buffer[length] = '\0';
  return length != 0;
}

static void hs_store_unsigned(void *data, uint64_t capacity, uint64_t value) {
  if (capacity == 1) {
    const uint8_t narrowed = (uint8_t)value;
    memcpy(data, &narrowed, sizeof(narrowed));
  } else if (capacity == 2) {
    const uint16_t narrowed = (uint16_t)value;
    memcpy(data, &narrowed, sizeof(narrowed));
  } else if (capacity == 4) {
    const uint32_t narrowed = (uint32_t)value;
    memcpy(data, &narrowed, sizeof(narrowed));
  } else if (capacity == 8) {
    memcpy(data, &value, sizeof(value));
  } else {
    hs_fail("invalid scan integer target capacity");
  }
}

static void hs_validate_scan_target(const HsScanTarget *target,
                                    char specifier, uint64_t floatLength,
                                    int checked) {
  if (target->data == NULL) {
    hs_fail("null scan target");
  }
  if (checked) {
    hs_check(target->data, target->capacity, "invalid scan target range");
  }
  if (specifier == 's') {
    if (target->capacity < 2) {
      hs_fail("scan string target must leave room for a NUL terminator");
    }
    if (target->kind != HS_SCAN_ANY && target->kind != HS_SCAN_STRING) {
      hs_fail("scan target type mismatch for %s");
    }
    return;
  }
  if (specifier == 'c') {
    if (target->capacity < 1) {
      hs_fail("scan character target has no capacity");
    }
    if (target->kind != HS_SCAN_ANY) {
      hs_fail("scan target type mismatch for %c");
    }
    return;
  }
  if (specifier == 'p') {
    if (target->capacity != sizeof(void *)) {
      hs_fail("scan pointer target capacity mismatch");
    }
    if (target->kind != HS_SCAN_ANY && target->kind != HS_SCAN_POINTER) {
      hs_fail("scan target type mismatch for %p");
    }
    return;
  }
  if (specifier == 'f') {
    if (target->capacity != floatLength) {
      hs_fail("scan float target capacity mismatch");
    }
    if (target->kind != HS_SCAN_ANY && target->kind != HS_SCAN_FLOAT) {
      hs_fail("scan target type mismatch for %f");
    }
    return;
  }
  if (target->capacity != 1 && target->capacity != 2 &&
      target->capacity != 4 && target->capacity != 8) {
    hs_fail("scan integer target capacity must be 1, 2, 4, or 8");
  }
  if (target->kind != HS_SCAN_ANY) {
    hs_fail("scan target type mismatch for integer conversion");
  }
}

static uint64_t hs_validate_scan_format(const char *format,
                                        const HsScanTarget *targets,
                                        uint64_t targetCount, int checked) {
  const uint64_t length = checked
                              ? hs_cstr_length(format,
                                               "unterminated scan format string")
                              : (uint64_t)strlen(format);
  uint64_t targetIndex = 0;
  for (uint64_t index = 0; index < length; ++index) {
    if (format[index] != '%') {
      continue;
    }
    ++index;
    if (index >= length) {
      hs_fail("unterminated scan format specifier");
    }
    if (format[index] == '%') {
      continue;
    }
    uint64_t floatLength = 0;
    while (index < length && format[index] >= '0' && format[index] <= '9') {
      const uint64_t digit = (uint64_t)(format[index] - '0');
      if (floatLength > (UINT64_MAX - digit) / 10U) {
        hs_fail("scan format width overflow");
      }
      floatLength = floatLength * 10U + digit;
      ++index;
    }
    if (index >= length) {
      hs_fail("unterminated scan format specifier");
    }
    const char specifier = format[index];
    if (specifier == 'f') {
      if (floatLength == 0) {
        floatLength = 8;
      }
      if (floatLength != 2 && floatLength != 4 && floatLength != 8 &&
          floatLength != 16) {
        hs_fail("invalid scan float byte length");
      }
    } else if (floatLength != 0) {
      hs_fail("scan byte length is only valid for %f");
    } else if (specifier != 'd' && specifier != 'u' && specifier != 'x' &&
               specifier != 'o' && specifier != 'b' && specifier != 'c' &&
               specifier != 's' && specifier != 'p') {
      hs_fail("unknown scan format specifier");
    }
    if (targetIndex >= targetCount) {
      hs_fail("scan format item count does not match target count");
    }
    hs_validate_scan_target(&targets[targetIndex], specifier, floatLength,
                            checked);
    ++targetIndex;
  }
  if (targetIndex != targetCount) {
    hs_fail("scan format item count does not match target count");
  }
  return length;
}

int32_t hs_scan_input(FILE *file, const char *format,
                      HsScanTarget *targets, uint64_t targetCount,
                      int checked) {
  FILE *input = hs_input_file(file);
  const uint64_t formatLength =
      hs_validate_scan_format(format, targets, targetCount, checked);
  uint64_t targetIndex = 0;
  int32_t converted = 0;
  for (uint64_t index = 0; index < formatLength; ++index) {
    if (isspace((unsigned char)format[index])) {
      while (index + 1 < formatLength &&
             isspace((unsigned char)format[index + 1])) {
        ++index;
      }
      hs_skip_input_space(input);
      continue;
    }
    if (format[index] != '%') {
      const int inputByte = fgetc(input);
      if (inputByte == EOF || inputByte != (unsigned char)format[index]) {
        if (inputByte != EOF) {
          ungetc(inputByte, input);
        }
        return converted == 0 && inputByte == EOF ? EOF : converted;
      }
      continue;
    }
    ++index;
    if (format[index] == '%') {
      const int inputByte = fgetc(input);
      if (inputByte != '%') {
        if (inputByte != EOF) {
          ungetc(inputByte, input);
        }
        return converted == 0 && inputByte == EOF ? EOF : converted;
      }
      continue;
    }
    uint64_t floatLength = 0;
    while (format[index] >= '0' && format[index] <= '9') {
      floatLength = floatLength * 10U + (uint64_t)(format[index] - '0');
      ++index;
    }
    const char specifier = format[index];
    if (specifier == 'f' && floatLength == 0) {
      floatLength = 8;
    }
    HsScanTarget *target = &targets[targetIndex++];
    if (specifier == 'c') {
      const int value = fgetc(input);
      if (value == EOF) {
        return converted == 0 ? EOF : converted;
      }
      ((unsigned char *)target->data)[0] = (unsigned char)value;
      ++converted;
      continue;
    }
    if (specifier == 's') {
      int value = hs_read_nonspace(input);
      if (value == EOF) {
        return converted == 0 ? EOF : converted;
      }
      uint64_t written = 0;
      while (value != EOF && !isspace((unsigned char)value) &&
             written + 1U < target->capacity) {
        ((unsigned char *)target->data)[written++] = (unsigned char)value;
        value = fgetc(input);
      }
      if (value != EOF && isspace((unsigned char)value)) {
        ungetc(value, input);
      } else if (value != EOF && written + 1U == target->capacity) {
        ungetc(value, input);
      }
      ((unsigned char *)target->data)[written] = '\0';
      ++converted;
      continue;
    }
    if (specifier == 'f') {
      char token[192];
      if (!hs_read_float_token(input, token, sizeof(token))) {
        return converted;
      }
      char *end = NULL;
      errno = 0;
      if (floatLength == 2) {
        const float value = strtof(token, &end);
        if (end == token || *end != '\0' || errno == ERANGE) {
          return converted;
        }
        const uint16_t half = hs_f32_to_f16(value);
        memcpy(target->data, &half, sizeof(half));
      } else if (floatLength == 4) {
        const float value = strtof(token, &end);
        if (end == token || *end != '\0' || errno == ERANGE) {
          return converted;
        }
        memcpy(target->data, &value, sizeof(value));
      } else if (floatLength == 8) {
        const double value = strtod(token, &end);
        if (end == token || *end != '\0' || errno == ERANGE) {
          return converted;
        }
        memcpy(target->data, &value, sizeof(value));
      } else {
#ifdef HS_SOFTWARE_F128
        if (!hs_f128_parse(token, target->data)) {
          return converted;
        }
#else
        const HsFloat128 value = strtof128(token, &end);
        if (end == token || *end != '\0' || errno == ERANGE) {
          return converted;
        }
        memcpy(target->data, &value, sizeof(value));
#endif
      }
      ++converted;
      continue;
    }
    uint64_t value = 0;
    int base = 10;
    int signedValue = specifier == 'd';
    if (specifier == 'x' || specifier == 'p') {
      base = 16;
    } else if (specifier == 'o') {
      base = 8;
    } else if (specifier == 'b') {
      base = 2;
    }
    if (!hs_read_integer(input, base, signedValue, &value)) {
      return converted;
    }
    hs_store_unsigned(target->data, target->capacity, value);
    ++converted;
  }
  return converted;
}

uint64_t hs_time_ms(void) {
#if defined(_WIN32)
  FILETIME fileTime;
  ULARGE_INTEGER ticks;
  GetSystemTimeAsFileTime(&fileTime);
  ticks.LowPart = fileTime.dwLowDateTime;
  ticks.HighPart = fileTime.dwHighDateTime;
  return (ticks.QuadPart - UINT64_C(116444736000000000)) / UINT64_C(10000);
#else
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    return 0;
  }
  return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
#endif
}

uint64_t hs_clock_ms(void) {
#if defined(_WIN32)
  return (uint64_t)GetTickCount64();
#else
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }
  return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
#endif
}

void hs_panic(int32_t code) {
  fprintf(stderr, "hitsimple panic: %d\n", code);
  exit(code);
}

void hs_assert(int32_t condition, int32_t code) {
  if (!condition) {
    hs_panic(code);
  }
}

void hitsimple_runtime_anchor(void) {}
