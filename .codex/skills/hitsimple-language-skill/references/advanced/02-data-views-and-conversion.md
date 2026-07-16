# Data, Views, Interpretation, and Conversion

Use this reference for the View model, declaration templates, `as`, `set`, assignment, conversions, typed operators, indexing, slicing, and byte representation.

Standard references: Chapters 5–8 and Sections 9.2–9.4.

## 1. View Model

Every expression produces a View containing:

1. bytes or a locatable memory region;
2. a length;
3. lvalue or rvalue state;
4. a standard template, user template, or `none`;
5. for lvalues, a start address and write permission.

Names, member access, indexing, slicing, and dereference can produce lvalue Views. Literals, calls, ordinary operation results, `sizeof()`, and postfix `?` produce rvalue Views.

An rvalue cannot be assigned to, incremented/decremented, or used as an address-of target.

## 2. Persistent and Temporary Interpretation

Declaration-level `as` sets persistent default interpretation:

```hs
new count as i32 = 0
new raw[4] as bytes
```

Expression-level `as` creates a temporary View:

```hs
new bits as u32 = 0x3F800000
new value as f32 = bits as f32
```

`as` preserves bytes, byte order, address binding, length, and lifetime. A fixed-length target template requires matching View length. A user template requires `length == sizeof(Template)`.

Use `set` for persistent metadata on a statically locatable name/member chain:

```hs
new raw[4] as bytes
set raw as i32
raw = 100
set raw as none
```

Valid `set` targets are names and member chains. Indexes, slices, dereferences, dynamic Views, and temporary expressions use expression-level `as`.

For member interpretation, precedence is:

```text
expression-level as
-> member-chain set override
-> member declaration template
-> none
```

## 3. Template Propagation

- Reading a name carries its persistent template.
- Byte indexing produces length 1, template `none`.
- Ordinary slicing produces template `none`.
- A member carries its member override/template.
- Postfix `?` produces same length, template `none`, unsigned-integer interpretation.
- An ordinary operator gets its result template from the selected standard/user operation.
- An explicit typed operator returns template `none`.

## 4. Default Assignment

Assignment is destination-driven:

```text
destination View <- source View
```

The destination must be an lvalue.

Main default matrix:

| Destination | Common accepted source | Semantics |
|---|---|---|
| `iN`, `uN` | integer literal, integer `none`, integer templates, `bool` | integer width adjustment |
| `fN` | floating literal, `fM` | IEEE numeric conversion |
| `bool` | any View | all-zero/nonzero normalization |
| `addr` | `addr`, integer literal/value, integer templates, `bool` | adjust to `P` bytes |
| `none` | integer-like Views and `addr` | raw-integer assignment |
| `handle` | `handle` | opaque copy |
| `cstr` | string literal, `cstr` | `%s=` semantics |
| `bytes` | any equal-length View | byte-for-byte copy |
| user template | same template | `op =` or fallback byte copy |

An uninterpreted `none`, `bytes`, `cstr`, or user-template source does not automatically become a number.

## 5. Explicit Assignment Operators

### `%d=` integer assignment

```hs
new x as i64
x %d= source
```

- wider source to narrower destination: reduce modulo destination width;
- narrower signed source: sign-extend;
- narrower unsigned source: zero-extend;
- equal lengths: copy bytes.

This is numeric significance, not low-address-prefix truncation.

### `%f=` floating assignment

```hs
new x as f32
x %f= 1.25
```

Destination length must be 2, 4, 8, or 16 bytes. Numeric integer-to-float conversion uses `to_f*()`.

### `%s=` string assignment

```hs
new name[16] as cstr
name %s= "Kai"
```

Copies or truncates and guarantees a trailing `0x00`. When truncating, the final destination byte is forced to zero.

### `%b=` Boolean assignment

```hs
new flag as bool
flag %b= source
```

Writes `0x00` or `0x01`; any higher-address destination bytes are zero-filled.

### `&=` address rebinding

```hs
new window[8] as bytes
window &= ptr
```

Only a single name can be rebound. `&=` changes its binding address while preserving declared length, definition address, template, and lifetime.

## 6. Numeric Conversion, Reinterpretation, Resize, and Byte Order

Use one explicit operation per intent:

| Intent | Operation |
|---|---|
| temporary interpretation | `expr as Template` |
| numeric conversion | `to_i*`, `to_u*`, `to_f*` |
| byte-length change | `resize_bytes(expr, length)` |
| byte-order reversal | `byte_swap(expr)` |
| unsigned integer interpretation | postfix `?` |

Example:

```hs
$include <stdlib.hsh>

new integer as i32 = 42
new real as f64 = to_f64(integer)

new small[4] as bytes
new large[8] as bytes = resize_bytes(small, 8) as bytes

new swapped[4] as bytes = byte_swap(small) as bytes
```

`resize_bytes()` copies in address order. Short sources are zero-filled at higher addresses. Long sources keep the low-address prefix. On big-endian systems, this differs from retaining integer least-significant bytes.

## 7. Untemplated Integer Operations

When all operands use template `none`, ordinary integer rules use computation width:

```text
C = max(operand lengths)
```

Memory Views with template `none` are signed two's-complement integers by default. Postfix `?` requests unsigned interpretation for the current integer operation.

```hs
new x[4] = -1
new y[4] = x? + 1
```

Results are reduced modulo `2^(8C)`.

## 8. Explicit Typed Operators

Integer examples:

```hs
new result as i64 = a %8d+ b
new shifted as u32 = a %4d<< count
```

Without an explicit width, `%d` uses the maximum operand length. Width must be greater than zero.

Floating examples:

```hs
new result as f64 = (a %8f+ b) as f64
```

Floating width must be 2, 4, 8, or 16 bytes. Typed-operator results carry template `none`; a destination template can accept the result through its assignment rules.

## 9. Address-Of, Byte Indexing, and Slicing

```hs
new data[64] as bytes
new start as addr = &data

new byte as u8 = data[3] as u8
new chunk[8] as bytes = data[8:16] as bytes
new chunk2[8] as bytes = data[8:+8] as bytes
```

Core `data[index]` selects one byte. The index is read as signed; negative and out-of-range values are boundary errors.

Slices use half-open ranges and produce lvalue Views into the original storage:

```text
[start:end] -> bytes from start through end-1
[start:+len] -> len bytes beginning at start
```

Dynamic-length slices can be assigned to an existing target, passed to standard functions, or queried with `length()`. A declaration with omitted length cannot infer from a dynamic-length slice.

## 10. Dereference

```hs
new p as addr = &value
new copy as i32 = [4]*p as i32
[4]*p as i32 = 7
```

`[L]*expr` treats `expr` as a system-pointer-length address and locates exactly `L` bytes. `L` can be a positive literal, `P`, or `sizeof(Template)`.

Core standard syntax does not use `*ptr` or `[]*ptr`.

## 11. Template Size and Instances

```hs
new p as Point
new q[sizeof(Point)] as Point
new arr[t10] as Point
new first as Point = arr[t0]
```

`sizeof()` works with fixed-length standard templates and user templates. It is invalid for variable-length `bytes` and `cstr`.

## 12. Common Review Traps

- `as` reinterprets; it does not numerically convert.
- `resize_bytes` resizes bytes; it does not preserve numeric significance on every endian.
- Core indexing is byte indexing.
- An ordinary slice defaults to `none` and often needs `as bytes` or another explicit interpretation.
- A typed operator returns `none`.
- `set` targets names/member chains, not arbitrary expressions.
- `&=` changes a name binding, while scope cleanup still applies to the original definition address.
