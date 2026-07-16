# Templates, Operations, and Methods

Use this reference for user-template layout, template instances, `impl`, operator definitions, method calls, assignment operations, formatting, and overload diagnostics.

Standard references: Chapters 8–10 and Appendix 21.

## 1. User Templates Define Layout

```hs
template User {
    id[4] as u32
    age[1] as u8
    name[32] as cstr
}
```

Rules:

- members are packed in declaration order;
- no implicit alignment or padding;
- each member has an explicit positive length, `[P]`, or `sizeof(Template)`;
- a member cannot use `[tN]`;
- a member template changes interpretation, not size;
- templates are global;
- template names share the top-level namespace with standard templates, functions, externs, and globals.

A template's size is the sum of its member lengths.

## 2. Instances and Members

```hs
new u as User
u.id = 1001
u.name = "Kai"

new users[t4] as User
users[t0].id = 1
```

`new u as User` allocates `sizeof(User)` bytes. An explicit byte length must equal the template size.

`[tN]` creates a compile-time count of instances. `[tK]` accesses a compile-time zero-based instance index.

Member access is compile-time offset calculation. The resulting member is an lvalue View.

## 3. `impl` Blocks

```hs
$include <math.hsh>

impl Vec2 {
    func length(self as Vec2) -> f64 {
        return f_sqrt(self.x * self.x + self.y * self.y)
    }
}
```

Multiple `impl Vec2` blocks are merged after top-level name collection. Duplicate operation/method keys are diagnosed.

Ordinary user code can define impls for user templates. Extending standard templates requires an implementation extension mode.

## 4. Static Operation Resolution

For an ordinary operator, semantic analysis:

1. collects candidates from operand templates;
2. filters candidates by operator name, arity, parameter templates, lengths, and static return signature;
3. requires exactly one applicable candidate;
4. forms an rvalue result from the selected return signature.

User `impl op` parameters require exact explicit template matching. Runtime values and runtime type information do not select overloads.

An operator overload key is:

```text
(operator_name, arity, parameter_template_sequence)
```

Return type, return length, parameter names, runtime values, and owner discovery path do not distinguish overloads.

## 5. Defining Operators

Supported user operations:

```text
= == != < <= > >= + - * / % ** << >> & | ^ format
```

Except `format`, user operations are binary and take exactly two parameters. Every parameter needs an explicit template. Return length and template must be statically known.

Arithmetic example:

```hs
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
}
```

Mixed-template operations require explicit definitions:

```hs
impl Vec2 {
    op * (lhs as Vec2, scale as f64) -> Vec2 {
        new out as Vec2
        out.x = lhs.x * scale
        out.y = lhs.y * scale
        return out
    }
}
```

## 6. Comparisons

Every comparison operation returns one-byte `bool`:

```hs
impl Vec2 {
    op == (lhs as Vec2, rhs as Vec2) -> bool {
        return (lhs.x == rhs.x) && (lhs.y == rhs.y)
    }
}
```

## 7. `op =` and Caller-Visible Assignment

```hs
impl Vec2 {
    op = (dst as Vec2, src as Vec2) -> Vec2 {
        dst.x = src.x
        dst.y = src.y
        return dst
    }
}
```

The first parameter is the writable lvalue View of the actual assignment target. It is the special borrowing path for user assignment.

Requirements:

- first parameter template matches destination template;
- return signature matches the first parameter's template and length;
- body returns a value satisfying that signature;
- the observable assignment expression result is an rvalue copy of the target after the write.

A user template with no `op =` uses byte-for-byte copy for assignment from the same template.

## 8. Methods

```hs
impl Vec2 {
    func length(self as Vec2) -> f64 {
        return f_sqrt(self.x * self.x + self.y * self.y)
    }
}

new len as f64 = v.length()
```

Method resolution:

1. evaluate the left expression to a View;
2. read its template;
3. find the method in that template's impl;
4. pass the left View as the first argument;
5. pass remaining arguments in written order.

The first parameter is mandatory and its template must match the enclosing impl template. There is no hidden `self`.

A method-overload key is:

```text
(method_name, arity, parameter_template_sequence)
```

Return signature and parameter names do not distinguish overloads.

## 9. Method Mutation Model

Method parameters, including `self`, are passed by View-value copy. Standard `mut self` syntax is reserved and diagnosed.

Use one of these patterns:

### Functional update

```hs
impl Vec2 {
    func scaled(self as Vec2, factor as f64) -> Vec2 {
        new out as Vec2
        out.x = self.x * factor
        out.y = self.y * factor
        return out
    }
}

v = v.scaled(0.5)
```

### Explicit address

```hs
impl Counter {
    func write_to(self as Counter, target[P] as addr) -> () {
        [sizeof(Counter)]*target as Counter = self
    }
}
```

### Assignment semantics

Use `op =` when the caller performs an assignment and the target must be written directly.

## 10. Temporary Template Views and Methods

```hs
new raw[16] as bytes
new len as f64 = (raw as Vec2).length()
```

The byte length must equal `sizeof(Vec2)`.

## 11. Formatting

A user template can be formatted by `print()` only with this exact operation shape:

```hs
impl Vec2 {
    op format (value as Vec2, out[P] as addr) -> i32 {
        // The concrete output-target ABI comes from the implementation.
        return 0
    }
}
```

Requirements:

- exactly two parameters;
- second parameter `[P] as addr`;
- return template `i32`;
- static return View.

The detailed output-target ABI is implementation-defined beyond the standard operation shape.

## 12. User Operations Excluded by Standard Mode

Do not define user overloads for:

- unary `!`;
- unary `~`;
- unary `-`;
- `++` or `--`.

Logical operators always use the built-in Boolean-test path and do not invoke user ops.

## 13. Common Resolution Failures

A compile-time diagnostic is required for:

- unknown template in `impl`;
- op parameter without a template;
- unknown or dynamic op return length;
- `op -> ()`;
- invalid user-op arity;
- invalid `op format` signature;
- `op =` return mismatch;
- duplicate overload keys;
- zero or multiple applicable operator candidates;
- method without an explicit first parameter;
- first method parameter template differing from the enclosing template;
- method call on a View with no template;
- zero or multiple applicable method candidates;
- implicit cross-template conversion inside operation resolution.
