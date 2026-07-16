# Writing Programs

Use this reference for ordinary program construction: declarations, functions, returns, control flow, errors, and common I/O.

Standard references: Chapters 3, 4, 12, 13, and 14.

## 1. Source Shape

A translation unit can contain global `new` declarations, `func` definitions, `extern` declarations, `template` definitions, and `impl` blocks.

Top-level names are collected before bodies are analyzed, so top-level functions, templates, externs, globals, and impls can be forward-referenced. Local names follow lexical scope and declaration order.

Line breaks and semicolons both terminate simple declarations/statements.

```hs
new version as u32 = 1

func square(x as i32) -> i32 {
    return x * x
}
```

## 2. Declarations

```hs
new x as i32 = 1
new y[8] as f64 = 2.0
new buffer[256] as bytes
new name[64] as cstr = "Kai"
```

Inside a function or block, `static` creates block-scoped name visibility with program-lifetime storage and one-time initialization when control first reaches the declaration.

```hs
func next_id() -> u64 {
    static id as u64 = 0
    id++
    return id
}
```

Global `new` initializers run in source order. Reading a later global before its initialization completes is an uninitialized-byte read.

## 3. Function Parameters

Ordinary function parameters are local storage objects initialized by View-value copy.

```hs
func sum(a as i64, b as i64) -> i64 {
    return a + b
}
```

The callee does not obtain general caller-side write access through an ordinary parameter. For caller-visible mutation, use:

- a returned value assigned by the caller;
- an explicit `addr` and `[L]*` dereference;
- `op =`, whose first parameter is the dedicated writable assignment target;
- a standard-library `lview`/`mem_lview` function such as `memset` or `memcpy`.

Variable-length or untemplated by-value parameters need explicit static lengths:

```hs
$include <stdlib.hsh>

func consume_block(data[32] as bytes) -> u64 {
    return length(data)
}
```

The meta-parameter names `view`, `lview`, `mem_view`, `mem_lview`, `cstr_view`, and `bytes_view` belong to standard-library specification notation. They are not ordinary user parameter templates.

## 4. Return Signatures

Supported shapes:

```hs
func a() -> () { }
func b() -> i32 { return 1 }
func c() -> as i32 { return 1 }
func d() -> value as i32 { return 1 }
func e() -> (ok as bool, value as i32) { return true, 1 }
func f() -> [16] as bytes { /* ... */ }
```

Omitting the signature means `-> ()`.

A named return item becomes local return storage at function entry. It remains uninitialized until written.

```hs
func classify(x as i32) -> (ok as bool, value as i32) {
    ok = true
    value = x
    return
}
```

Every ordinary return item must have a statically known length. `none`, `bytes`, and `cstr` need explicit lengths.

## 5. Calls and Evaluation Order

All argument expressions are evaluated before the function body begins. The implementation chooses and documents the order among arguments. Avoid unsequenced reads/writes to overlapping memory in different arguments.

Prefer:

```hs
new left as i32 = compute_left()
new right as i32 = compute_right()
new result as i32 = combine(left, right)
```

for code whose effects require a specific order.

## 6. Conditions

Boolean testing reads all bits:

```text
all bits zero -> false
any nonzero bit -> true
```

`!`, `&&`, and `||` always use this built-in path. `&&` and `||` short-circuit.

```hs
if (ready && valid) {
    run()
} else if (retry) {
    recover()
} else {
    stop()
}
```

The ternary operator evaluates only the selected branch at runtime, while both branches still undergo static analysis. Their result Views must have the same template and statically equal lengths.

```hs
new selected as i32 = condition ? left : right
```

## 7. Loops and Jumps

```hs
new i as i32 = 0
while (i < 10) {
    i++
}

for (new j as i32 = 0; j < 10; j++) {
    if (j == 2) {
        continue
    }
    if (j == 8) {
        break
    }
}
```

`continue` in a `for` runs `post` before the next condition. `continue` in a `while` proceeds to the next condition.

`goto` targets must be labels in the same function. A jump cannot enter a scope past a local object whose initialization has not occurred.

```hs
goto done

done:
return 0
```

## 8. Error Handling

```hs
try {
    risky()
} catch (err as i32) {
    print(err)
}
```

`throw expr` assigns into the dynamically nearest catch parameter using ordinary assignment semantics. The catch parameter's length must be statically known.

Detected checked-mode runtime errors may become `throw` according to implementation documentation. Undefined behavior in unchecked or static-checked mode does not automatically become `throw`.

## 9. Main

Portable form:

```hs
func main() -> i32 {
    return 0
}
```

The host treats the returned `i32` as the process exit code. An implementation documents its exact `main` convention.

## 10. Standard I/O

```hs
$include <stdio.hsh>

func main() -> i32 {
    new value as i32 = 42
    print(value)
    printf("value=%d\n", value)
    return 0
}
```

`put(x)` writes raw bytes. `print(x)` formats according to the View template; standard templates have standard formatting. A user template needs a matching `op format`.

The concrete set of `printf`/`scanf` format specifications is implementation-defined.

`scanf` and `fscanf` are left-context built-ins and appear only as the sole right-hand expression of a multiple-assignment statement:

```hs
new count as i32
new value as i32
count, value = scanf("%d")
```

For a literal format, conversion count, target count, target templates, and `%s` capacity are checked statically. The first left target receives the conversion count and may be `_`.

## 11. Common Review Traps

- Standard functions require official headers.
- Function parameters are by value.
- `++`/`--` are statements.
- `break` and `continue` require an enclosing loop.
- Multiple return target counts must match.
- `bytes`, `cstr`, and `none` need explicit parameter/return lengths.
- Ternary branches need the same length and template.
- Function argument order is implementation-defined.
