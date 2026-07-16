---
name: hitsimple-language
description: >-
  Use this skill when writing, explaining, reviewing, debugging, translating, or
  validating HitSimple (hs) source code. It provides a compact high-frequency
  language model, task-routed advanced references, examples, and the complete
  HitSimple Standard 1.0.0-Beta.21 as the final authority.
---

# HitSimple Language

## Goal

Produce clear, portable, standard-conforming HitSimple code while loading as little context as possible.

Use progressive disclosure:

1. Start with this file for ordinary code generation and explanation.
2. Open one relevant file under [`references/advanced/`](references/advanced/) when the task needs details.
3. Consult [`references/standard/INDEX.md`](references/standard/INDEX.md) and the complete standard for ambiguous, normative, implementation-sensitive, or conformance questions.

Authority order:

```text
complete standard > advanced references > this quick guide > assumptions
```

When sources appear inconsistent, follow the complete standard and state the relevant section.

## Core Mental Model

HitSimple is memory-centered:

```text
storage object -> View -> interpretation template -> operation resolution -> observable behavior
```

Keep these rules active while writing code:

- Data is a contiguous byte sequence. Meaning comes from an explicit interpretation template.
- Every expression evaluates to a View with bytes/region, length, lvalue-or-rvalue state, and an optional template.
- `as Template` changes interpretation only. It does not convert numbers, resize bytes, reorder bytes, rebind addresses, or change lifetime.
- `resize_bytes()` changes byte length only. `byte_swap()` changes byte order only. `to_iN()`, `to_uN()`, and `to_fN()` perform numeric conversion.
- Core indexing is byte indexing. `x[i]` selects one byte.
- User-template layout is packed in declaration order with no implicit alignment.
- Ordinary functions and methods receive parameters by View-value copy. Caller-visible mutation uses assignment via `op =`, explicit addresses and dereference, or a returned value assigned by the caller.
- Operation and method resolution is static. HitSimple has no inheritance, virtual dispatch, implicit `self`, implicit constructors/destructors, or implicit cross-template conversion.

Minimum standard templates:

| Templates | Byte length | Meaning |
|---|---:|---|
| `i8/i16/i32/i64` | `1/2/4/8` | signed integers |
| `u8/u16/u32/u64` | `1/2/4/8` | unsigned integers |
| `f16/f32/f64/f128` | `2/4/8/16` | IEEE floating point |
| `bool` | `1` | Boolean |
| `addr`, `handle` | `P` | address and opaque file handle |
| `bytes`, `cstr` | positive dynamic length | raw bytes and bounded zero-terminated bytes |

String literals are UTF-8 byte sequences with a trailing `0x00`, and therefore carry template `cstr`.

## Default Authoring Workflow

1. Identify every storage object's byte length and persistent template.
2. Use core syntax unless the user explicitly requests the C compatibility layer.
3. Add the official system header for every standard-library function used.
4. Keep arithmetic operands on the same standard template when practical.
5. Use explicit conversion or interpretation when templates differ.
6. Make caller-visible writes explicit.
7. Check return count, return length, and return template.
8. Review the generated code with the checklist near the end of this file.

## Quick Syntax

### Program and declarations

```hs
$include <stdio.hsh>

new global_count as u64 = 0

func add(a as i32, b as i32) -> i32 {
    return a + b
}

func main() -> i32 {
    new x as i32 = 20
    new y as i32 = 22
    new result as i32 = add(x, y)
    print(result)
    return 0
}
```

A line break or `;` can terminate a simple declaration or statement.

Common declarations:

```hs
new count as i32 = 0
new ratio as f64 = 0.5
new flag as bool = true
new raw[64] as bytes
new text[64] as cstr = "Hello"
new ptr[P] as addr = &raw

func demo() -> () {
    static calls as u64 = 0
    calls++
}
```

Length inference is available from a fixed-length template, a user template, or a statically sized initializer. `bytes` and `cstr` need an explicit length when no initializer supplies one.

### Conditions and loops

Any all-zero View is false; any other bit pattern is true.

```hs
if (flag) {
    print("yes")
} else {
    print("no")
}

new i as u64 = 0
while (i < 10) {
    i++
}

for (new j as i32 = 0; j < 10; j++) {
    if (j == 5) {
        continue
    }
}
```

`++` and `--` are statements and apply to integer-template or untemplated integer lvalues.

### Functions and returns

```hs
func divide(a as f64, b as f64) -> f64 {
    return a / b
}

func parse() -> (ok as bool, value as i32) {
    return true, 42
}
```

Omitting a return signature is equivalent to `-> ()`.

Parameters and return items must have statically known lengths. `bytes`, `cstr`, and `none` require explicit lengths in ordinary parameter/return signatures:

```hs
func checksum(data[32] as bytes) -> u64 {
    return 0
}

func block(input[16] as bytes) -> [16] as bytes {
    return input
}
```

### Templates, members, and instances

```hs
template Point {
    x[8] as f64
    y[8] as f64
}

new p as Point
p.x = 3.0
p.y = 4.0

new points[t4] as Point
points[t0].x = 1.0
```

`[tN]` means a compile-time count of user-template instances. `[tK]` accesses a compile-time instance index.

### Methods and operators

```hs
$include <math.hsh>

template Vec2 {
    x[8] as f64
    y[8] as f64
}

impl Vec2 {
    op + (lhs as Vec2, rhs as Vec2) -> Vec2 {
        new out as Vec2
        out.x = lhs.x + rhs.x
        out.y = lhs.y + rhs.y
        return out
    }

    func length(self as Vec2) -> f64 {
        return f_sqrt(self.x * self.x + self.y * self.y)
    }
}
```

A method's first parameter is explicit and must use the enclosing template. Methods receive `self` by value. Write functional updates as:

```hs
v = v.normalized()
```

User-overloadable operations are:

```text
= == != < <= > >= + - * / % ** << >> & | ^ format
```

Comparisons return `bool`. User unary overloads are outside standard syntax.

### Views and interpretation

```hs
new bits as u32 = 0x3F800000
new value as f32 = bits as f32

new raw[16] as bytes
new len as f64 = (raw as Vec2).length()

set raw as none
```

Use `as` only when byte lengths match the target fixed-length template or user-template size.

`set name as Template` changes a persistent default template for a statically locatable name/member chain. Use expression-level `as` for temporary interpretation.

### Assignment and conversion

```hs
new wide as i64
wide %d= 127

new real as f64
real %f= 1.25

new name[16] as cstr
name %s= "Kai"

new enabled as bool
enabled %b= 123

new resized[8] as bytes = resize_bytes(source, 8) as bytes
new number as f64 = to_f64(integer_value)
```

Assignment families:

- `=`: destination-template default assignment.
- `%d=`: explicit integer assignment with numeric width adjustment.
- `%f=`: explicit floating-point assignment/conversion.
- `%s=`: C-string copy/truncate with a terminating zero byte.
- `%b=`: Boolean normalization.
- `&=`: rebind a name's binding address.

Typed operators select explicit arithmetic width and return template `none`:

```hs
new c as i64 = a %8d+ b
```

### Addresses, dereference, slices, and byte indexing

```hs
new buf[64] as bytes
new base as addr = &buf
new offset as u64 = 8
new ptr as addr
ptr = base? + offset?

new one_byte as u8 = buf[3] as u8
new part[8] as bytes = buf[8:16] as bytes
new same_part[8] as bytes = buf[8:+8] as bytes

[4]*ptr as i32 = 123
new value as i32 = [4]*ptr as i32
```

`?` creates an unsigned-integer rvalue View with template `none`. `[L]*address` dereferences exactly `L` bytes.

### Standard library headers

Include the owning header before each standard-library use:

| Header | Main facilities |
|---|---|
| `<stdlib.hsh>` | allocation, `length`, conversions, integer math, random, process control |
| `<string.hsh>` | memory operations and C strings |
| `<stdio.hsh>` | console and file I/O |
| `<math.hsh>` | floating-point math |
| `<ctype.hsh>` | ASCII classification/case conversion |
| `<time.hsh>` | clocks |
| `<assert.hsh>` | `assert`, `panic` |

Frequently used functions:

```text
length alloc calloc realloc free
memset memcpy memmove memcmp
resize_bytes byte_swap
to_i8 to_i16 to_i32 to_i64
to_u8 to_u16 to_u32 to_u64
to_f16 to_f32 to_f64 to_f128
strlen strcmp strcpy strncpy strcat strchr
get put print printf scanf
fopen fclose fread fwrite fprintf fscanf fflush fseek ftell feof ferror
abs min max f_abs f_sqrt f_pow f_sin f_cos f_tan f_log f_exp
```

## Advanced Capability Map

Open only the file that matches the current task:

- Everyday program construction, functions, returns, control flow, errors, and I/O: [`references/advanced/01-writing-programs.md`](references/advanced/01-writing-programs.md)
- Views, templates, `as`, `set`, assignment, conversion, slicing, and byte representation: [`references/advanced/02-data-views-and-conversion.md`](references/advanced/02-data-views-and-conversion.md)
- User templates, `impl`, operators, methods, overload matching, and formatting: [`references/advanced/03-templates-operations-and-methods.md`](references/advanced/03-templates-operations-and-methods.md)
- Storage, addresses, rebinding, allocation, lifetimes, execution modes, and memory safety: [`references/advanced/04-memory-addresses-and-safety.md`](references/advanced/04-memory-addresses-and-safety.md)
- Standard library, preprocessor, C compatibility layer, `extern`, and FFI: [`references/advanced/05-library-preprocessor-and-interop.md`](references/advanced/05-library-preprocessor-and-interop.md)
- Diagnostics, portability, conformance review, and implementation-defined behavior: [`references/advanced/06-diagnostics-portability-and-review.md`](references/advanced/06-diagnostics-portability-and-review.md)
- Complete chapter map: [`references/standard/INDEX.md`](references/standard/INDEX.md)
- Complete normative text: [`references/standard/HitSimple-Standard-1.0.0-Beta.21.md`](references/standard/HitSimple-Standard-1.0.0-Beta.21.md)

## Nonexistent or Restricted Features

Do not invent these in standard-mode HitSimple:

- traditional classes, inheritance, virtual functions, dynamic dispatch
- implicit constructors or destructors
- implicit `self`
- standard `mut self`
- implicit cross-template conversion
- automatic boxing/unboxing
- runtime-type-based operation resolution
- implicit array-to-address decay in core syntax
- implicit numeric meaning for arbitrary `bytes`, `cstr`, user-template, or uninterpreted `none` Views
- core `*ptr` dereference; use `[L]*ptr`
- core element indexing for byte arrays; core `x[i]` is byte indexing
- legacy `;Template`, `[sN]`, `reinterpret()`, `to_float()`, or `to_int()`

Use the C compatibility layer only when requested. Compatibility syntax is translated to core syntax before semantic analysis.

## Generation Rules

When asked to write HitSimple code:

- Prefer simple core syntax and explicit templates.
- Include complete runnable code when the request is program-sized.
- Keep same-template arithmetic operands aligned; use `to_*`, `as`, explicit typed operators, or explicit assignment when needed.
- Use `as` for interpretation and `to_*` for numeric conversion.
- Use `resize_bytes()` for byte-length changes and add `as bytes` when the destination requires `bytes`.
- Treat `bytes` as raw byte sequences and `cstr` as bounded zero-terminated byte strings.
- Add system headers for all standard functions.
- Use `memmove` when overlap is possible.
- Use `handle` for standard file-I/O values.
- Use `addr` plus explicit `[L]*` for address-based access.
- Keep FFI ABI claims qualified as implementation-defined unless implementation documentation is supplied.

When asked to debug or review code:

1. Identify the first invalid syntax or semantic operation.
2. Explain it in terms of length, View category, template, or operation resolution.
3. Provide corrected code.
4. Note any implementation-defined dependency.
5. Open the relevant advanced reference; use the full standard for disputed or edge cases.

## Final Review Checklist

Before returning generated code, verify:

- Every standard function has its official header.
- Every identifier is declared and every local name follows declaration order.
- Every fixed-template length matches its template.
- Every `bytes`, `cstr`, or `none` parameter/return has an explicit static length.
- Every function return count, length, and template matches its signature.
- Every ordinary mixed-template operation has an explicit valid path.
- Every `as` operation preserves byte length.
- Every numeric conversion uses `to_*` or a defined assignment/operator path.
- Every user method has an explicit first parameter matching the enclosing template.
- Every comparison operation returns `bool`.
- Every caller-visible mutation uses a permitted explicit mechanism.
- Every slice/index/dereference is byte-based and within known bounds.
- Every `free()` target is a current dynamic-object base address or null.
- Every `cstr` has visible capacity for a terminating `0x00`.
- Every portability-sensitive assumption is identified.
