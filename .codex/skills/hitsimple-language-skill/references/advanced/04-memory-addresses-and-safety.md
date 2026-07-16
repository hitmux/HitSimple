# Memory, Addresses, Lifetimes, and Safety

Use this reference for storage objects, definition/binding addresses, `new`, `static`, allocation, `free`, rebinding, dereference, scope exit, execution modes, and runtime safety.

Standard references: Chapters 2, 5, 6.10, 8.10, and 18.

## 1. Storage Objects

A storage object has:

- a contiguous byte region;
- a length;
- a definition address;
- a lifetime.

It does not permanently store traditional integer/string/object type state. Templates are interpretation metadata on names and Views.

## 2. Name State

A `new` or `static` name has:

- **definition address**: original declaration allocation; stable for the name's lifetime;
- **binding address**: current address used through the name; initially equal to definition address;
- **declared length**;
- **persistent default template**.

`&=` changes the binding address only.

```hs
new window[16] as bytes
window &= external_ptr
```

Scope cleanup still releases the original local `new` definition object, not the current binding target.

## 3. Storage Duration

- Global `new`: entire program lifetime.
- Local `new`: automatically released when leaving its declaring scope.
- Local `static`: program lifetime, block-scoped name, one-time initialization on first reach.
- Dynamic allocation: lifetime controlled by `alloc`/`calloc`/`realloc`/`free`.
- `extern`: lifetime and properties supplied by the external environment.

Leaving a scope through `return`, `throw`, `break`, `continue`, or `goto` releases already-created local `new` objects in exited scopes.

## 4. Uninitialized Bytes

Bytes not initialized at declaration are uninitialized. Reading them is undefined behavior. Static-checked/checked modes diagnose statically provable reads; checked mode reports detectable runtime reads.

Use explicit initialization when later code reads the full region:

```hs
$include <stdlib.hsh>
$include <string.hsh>

new buf[64] as bytes
memset(buf, 0, length(buf))
```

## 5. Address-Of

```hs
new value as i32 = 7
new p as addr = &value
```

Address-of accepts locatable lvalues: names, members, indexes, slices, and dereference lvalues. It rejects rvalues.

`&name` returns the current binding address.

## 6. Address Arithmetic

Use explicit unsigned interpretation:

```hs
new base as addr = &buffer
new offset as u64 = 8
new p as addr
p = base? + offset?
```

Ordinary `addr + integer` is not an implicit address-arithmetic path.

## 7. Dereference

```hs
new x as i32 = [4]*p as i32
[4]*p as i32 = 9
```

`[L]*expr` locates `L` bytes at an address represented by the operand. `L` can be a positive literal, `P`, or `sizeof(Template)`.

Invalid cases include null, dangling, and out-of-bounds addresses.

## 8. Dynamic Allocation

```hs
$include <stdlib.hsh>

new size as u64 = 1024
new p as addr = alloc(size)

if (p) {
    // use p
    free(p)
}
```

`calloc(count, size)` allocates and zero-fills. `realloc(ptr, size)` may move the object; on failure the original remains valid.

A non-null `free()` or `realloc()` pointer must be the base address of a currently valid dynamic object returned by allocation/reallocation.

Valid `free()` target categories:

- null address;
- current base address of a live dynamic object.

Invalid target categories include:

- local/global `new`;
- `static`;
- `extern`;
- interior address;
- slice address;
- offset address;
- already-freed base;
- non-dynamic FFI address.

## 9. Temporary Views and Lifetimes

A temporary rvalue View ends at the full expression. A temporary lvalue View does not extend the underlying object's lifetime.

Addresses derived from temporary values require special care. For example, `strchr()` on a temporary rvalue string yields an address valid only within the current full expression.

## 10. Execution Modes

### unchecked

Required baseline mode. Runtime safety checks are not required. Many invalid memory/runtime-value operations are undefined behavior.

### static-checked

Performs every safety check provable statically and emits compile-time diagnostics. It inserts no runtime safety-check code and adds no runtime checking overhead.

### checked

Performs static checks and reports detectable runtime safety violations.

Static semantic diagnostics such as syntax, name resolution, template matching, return signatures, and overload conflicts apply in every mode.

## 11. Safety Conditions

Representative safety checks:

- out-of-bounds index/slice/dereference;
- null/dangling dereference;
- invalid free, double free, use after free;
- division by zero;
- negative shift count or exponent;
- dynamic length/template mismatch;
- insufficient standard-library capacity;
- `size * count` overflow;
- missing `0x00` inside a visible `cstr` extent;
- detectable `memcpy` overlap;
- invalid file handle;
- insufficient source/destination length for memory/file operations.

## 12. Sequence Points and Side Effects

Sequence points occur:

- after the left operand of `&&`;
- after the left operand of `||`;
- after the condition of `?:`;
- after all call arguments and before the function body;
- between `for` init/condition/post components;
- between comma-separated `for_post` expression statements;
- at the end of a full expression.

Avoid multiple unsequenced accesses when at least one writes overlapping memory.

## 13. Standard Memory Functions

`<stdlib.hsh>`:

```text
alloc calloc realloc free length resize_bytes byte_swap conversions
```

`<string.hsh>`:

```text
memset memcpy memmove memcmp strlen strcmp strcpy strncpy strcat strchr
```

Use `memmove` when source and destination may overlap. A zero-length `memcpy`/`memmove`/`memset`/`memcmp` does not access pointed content and can accept evaluable addresses without dereference.

For positive-length operations on raw addresses, boundary metadata must be available for checked-mode verification.

## 14. Portability Boundaries

Implementation documentation controls:

- pointer length `P`;
- byte order;
- alignment guarantees;
- allocation failure and zero-size allocation behavior;
- checked-mode enablement and error policy;
- raw/FFI address boundary detectability;
- runtime-error-to-`throw` policy;
- file-handle representation beyond the fixed all-zero null handle.

Do not assume a C ABI or a specific alignment unless implementation documentation states it.
