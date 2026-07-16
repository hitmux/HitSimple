# Standard Library, Preprocessor, Compatibility, and Interop

Use this reference for system headers, standard-library calls, preprocessing, C compatibility syntax, `extern`, and ABI-sensitive code.

Standard references: Chapters 14–17 and Section 2.2.

## 1. Official Headers Are Required

A standard function must be declared by its owning official header before use. Handwritten extern declarations for standard functions and conflicting declarations are diagnostics.

```hs
$include <stdlib.hsh>
$include <string.hsh>
$include <stdio.hsh>
$include <math.hsh>
$include <ctype.hsh>
$include <time.hsh>
$include <assert.hsh>
```

Header ownership:

| Header | Facilities |
|---|---|
| `stdlib.hsh` | allocation, `length`, conversions, integer math, random, process control |
| `string.hsh` | byte memory operations and C-string operations |
| `stdio.hsh` | standard/file I/O and formatted I/O |
| `math.hsh` | floating-point math |
| `ctype.hsh` | ASCII character classification and case conversion |
| `time.hsh` | wall and monotonic milliseconds |
| `assert.hsh` | `assert`, `panic` |

## 2. Standard-Library Signature Meta-Notation

The standard uses names such as `view`, `lview`, `mem_view`, `mem_lview`, `cstr_view`, `bytes_view`, `same_len_view`, `none[len]`, `left-context`, and `...`.

These describe built-in/standard acceptance sets. They are not core templates and cannot appear in ordinary user function, method, `op`, or `extern` signatures.

`lview` and `mem_lview` standard functions borrow caller-writable storage. This is a standard-library-specific write mechanism.

## 3. Allocation and Byte Operations

```hs
$include <stdlib.hsh>
$include <string.hsh>

new p as addr = alloc(128)
new buf[128] as bytes
memset(buf, 0, length(buf))
memcpy(buf[0:+16], source[0:+16], 16)
free(p)
```

Key rules:

- `calloc(count, size)` checks multiplication overflow in checked mode;
- `realloc` requires null or a live dynamic base address;
- `memcpy` has undefined behavior on overlap; checked mode reports detectable overlap;
- `memmove` supports overlap;
- memory operations check source/destination capacity in checked mode;
- length zero accesses no pointed content.

## 4. Conversion Functions

```hs
$include <stdlib.hsh>

new a as i32 = 42
new b as f64 = to_f64(a)
new c as u64 = to_u64(b)
new raw[8] as bytes = resize_bytes(a, 8) as bytes
new rev[8] as bytes = byte_swap(raw) as bytes
```

Conversion input must already have numeric meaning: integer templates, floating templates, `bool`, `addr` where permitted, or `none` carrying an integer interpretation attribute. Interpret raw bytes explicitly before conversion.

## 5. Strings

```hs
$include <string.hsh>

new name[32] as cstr = "Kai"
new n as u64 = strlen(name)
new same as i32 = strcmp(name, "Kai")
```

A `cstr` must contain a terminating zero within its visible View extent. Checked mode reports detectable absence.

`strcpy`, `strncpy`, and `strcat` require writable capacity. `strncpy` writes exactly `n` bytes and can produce a non-terminated destination when the source terminator is absent from the first `n` bytes.

## 6. Console and File I/O

```hs
$include <stdio.hsh>

new fh as handle = fopen("data.bin", "rb")
if (fh) {
    new buf[256] as bytes
    new count as u64 = fread(buf, 1, length(buf), fh)
    fclose(fh)
}
```

Use template `handle` for standard file handles. The all-zero `P`-byte handle is null.

`fread` writes at most `size * count` bytes and returns successfully read elements. `fwrite` reads at most that many source bytes and returns successfully written elements.

`scanf`/`fscanf` use left-context multiple assignment:

```hs
new count as i32
new number as i32
count, number = scanf("%d")
```

The first target gets the conversion count and can be `_`. Later targets must be writable lvalues and cannot use `_`.

## 7. Preprocessor

Directives use `$`:

```hs
$define DEBUG 1

$if DEBUG
$include <stdio.hsh>
$endif
```

Supported standard directives:

```text
$include $define $undef
$if $ifdef $ifndef $elif $else $endif
$error $warning $pragma
```

Object-like, function-like, and variadic macros are supported, along with stringification `#` and token concatenation `##` inside macro replacement lists.

Macros do not expand inside strings, character literals, or comments. Preprocessing completes before core parsing.

Common predefined macros should include:

```text
__HS_VERSION__
__HS_POINTER_SIZE__
__HS_BYTE_ORDER__
__HS_EXECUTION_MODE__
__HS_STATIC_CHECKED__
__HS_CHECKED__
```

Concrete values are implementation documentation.

## 8. C Compatibility Layer

Use only when requested or when translating existing C-like source. Compatibility syntax is parsed and translated to core syntax before core semantic analysis.

Representative mappings:

| Compatibility spelling | Core concept |
|---|---|
| `int` | `i32` |
| `unsigned int` | `u32` |
| `long` | `i64` in the minimum layer |
| `float` | `f32` |
| `double` | `f64` |
| `T *` | `[P] as addr` plus element metadata |
| `char buf[N]` | `N` bytes as `bytes`, or `cstr` with string initialization |
| `T arr[N]` | `N * sizeof(T)` bytes plus element metadata |
| `struct S` | `template S` |

Example translation:

```c
int x = 1;
```

becomes:

```hs
new x as i32 = 1
```

Compatibility pointer arithmetic scales by element size. Core address arithmetic is byte-based and explicit.

Minimum compatibility mode excludes multidimensional arrays, complex parenthesized declarators, function pointers, and pointers to arrays unless documented as extensions.

## 9. Legacy Syntax

Standard mode diagnoses:

```text
;Template
;none
[sN]
reinterpret()
to_float()
to_int()
```

An optional legacy mode may translate the first three forms when explicitly enabled. The removed conversion functions remain diagnosed; use `as`, `resize_bytes`, or `to_i*`/`to_u*`/`to_f*` according to intent.

## 10. Core `extern`

Functions:

```hs
extern puts(s[P] as addr) -> i32
extern native_add(a as i32, b as i32) -> i32
```

Variables:

```hs
extern errno as i32
extern global_buf[256] as bytes
```

Every parameter and return has a static length. Variable-length `bytes` and `cstr` cannot be passed by value in core extern functions; pass an address and document the referenced object contract.

## 11. ABI Claims

External linkage is implementation-defined. Require implementation documentation before claiming:

- C ABI compatibility;
- symbol encoding;
- calling convention;
- register/stack argument placement;
- return-value storage;
- multiple-return ABI;
- vararg ABI;
- alignment;
- FFI boundary tracking.

The standard requires implementations providing external linkage to document these items.
