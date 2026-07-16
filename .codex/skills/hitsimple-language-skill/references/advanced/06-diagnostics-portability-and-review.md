# Diagnostics, Portability, and Conformance Review

Use this reference when reviewing code, explaining compiler errors, checking standard conformance, or separating portable behavior from implementation-defined behavior.

Standard references: Chapters 18–19, plus the relevant semantic chapter.

## 1. Diagnostic Strategy

Analyze a failure in this order:

1. lexical/syntax validity;
2. name and scope resolution;
3. View lvalue/rvalue category;
4. byte length;
5. carried template or interpretation attribute;
6. assignment/operation/method candidate matching;
7. return signature;
8. safety mode;
9. implementation-defined behavior.

This order usually identifies the earliest decisive error and avoids speculative runtime explanations for compile-time failures.

## 2. Diagnostics Apply in Every Mode

Execution modes change safety checking policy. They do not disable static semantic diagnostics.

Every mode still diagnoses syntax, unknown names, invalid template matching, return mismatches, overload conflicts, invalid signatures, and other required compile-time conditions.

## 3. High-Frequency Compile-Time Errors

### Length errors

- fixed-template declaration length differs from template size;
- user-template View length differs from `sizeof(Template)`;
- `[0]`, `[t0]` declaration count, or `[0]*` dereference;
- dynamic length used where static length is required;
- `bytes`, `cstr`, or `none` parameter/return lacks explicit length;
- invalid template-array count/access.

### View-category errors

- assigning to an rvalue;
- taking an rvalue address;
- incrementing/decrementing an rvalue;
- using a non-writable target for scan or assignment.

### Interpretation and assignment errors

- default assignment outside the standard assignment matrix;
- uninterpreted `none`, `bytes`, `cstr`, or user-template data used as numeric conversion input;
- `as` target size mismatch;
- source incompatible with a destination template;
- invalid explicit typed-operator width.

### Operation/method errors

- missing applicable candidate;
- multiple applicable candidates;
- duplicate overload key;
- op parameter without explicit template;
- user op with invalid arity;
- op return length/template not statically known;
- comparison not returning `bool`;
- invalid `op format` shape;
- method missing explicit first parameter;
- first method parameter template differs from enclosing impl;
- method call on a View carrying `none`.

### Control-flow errors

- return count/template/length mismatch;
- `break`/`continue` outside loops;
- unknown/duplicate/cross-function `goto` labels;
- jump entering an uninitialized local scope;
- ternary branches with different templates or lengths.

### Library and interop errors

- missing/wrong official standard header;
- handwritten conflicting standard-function extern;
- standard-library meta-signature names used in user signatures;
- invalid `scanf`/`fscanf` left context;
- `handle` used in unsupported arithmetic/memory operations;
- C compatibility construct outside the minimum subset;
- core extern by-value `bytes`/`cstr` without a fixed ABI-safe representation.

## 4. Static-Checked Safety Diagnostics

When statically provable, diagnose:

- out-of-bounds access;
- division by zero;
- negative shift count or exponent;
- null dereference;
- invalid free, double free, use after free;
- dynamic-length/template inconsistency that resolves statically;
- literal-format target/capacity mismatch;
- insufficient destination capacity;
- `size * count` overflow;
- missing `cstr` terminator;
- raw-address boundary/capacity failure;
- recursive entry into a local-static initializer.

Static-checked mode adds no runtime checking code.

## 5. Checked-Mode Runtime Errors

When detectable at runtime, checked mode reports:

- bounds violations;
- invalid free/double free/use after free;
- null dereference;
- integer division by zero;
- negative shift/exponent;
- dynamic length/format/template mismatch;
- missing `cstr` terminator;
- insufficient standard-library capacity;
- multiplication overflow;
- overlapping `memcpy`;
- `abs(min_signed)` overflow;
- invalid file handle;
- insufficient memory/file source or destination length;
- unverifiable positive-length raw-address memory operands;
- recursive local-static initialization.

The implementation documents error codes and whether detected runtime errors convert to `throw`.

## 6. Implementation-Defined Checklist

Ask for implementation documentation when code depends on:

- pointer length `P`;
- native byte order;
- alignment guarantees;
- `f16`/`f128` lowering and precision;
- NaN payload behavior;
- external ABI and C ABI compatibility;
- multiple-return and vararg ABI;
- non-null file-handle representation and I/O error codes;
- checked/static-checked enablement;
- boundary tracking for raw/FFI addresses;
- allocation failure and zero-size allocation behavior;
- preprocessor paths/macros/extensions;
- C compatibility and legacy mode extensions;
- random algorithm;
- time fallback behavior;
- `bytes` formatting;
- evaluation order among function arguments;
- local-static synchronization under concurrency;
- uncaught-throw termination/flush policy.

## 7. Portable Authoring Policy

For portable code:

- rely on same-template numeric operations;
- use explicit conversions for mixed widths/signedness/floating-point combinations;
- avoid relying on argument evaluation order;
- avoid assuming alignment beyond documented guarantees;
- treat addresses as byte addresses;
- keep C compatibility and FFI behind documented boundaries;
- prefer official system headers and standard signatures;
- choose checked mode during development when available;
- make endian-sensitive byte logic explicit with `byte_swap()` or documented encoding.

## 8. Review Template

Use this concise report structure:

```text
Finding: <first decisive issue>
Category: syntax | name | lvalue/rvalue | length | template | resolution | safety | portability
Why: <rule in View/length/template terms>
Fix: <corrected code>
Standard: <chapter/section>
Portability note: <only when relevant>
```

## 9. Escalation to the Complete Standard

Open the complete standard for:

- disputed grammar or operator precedence;
- postfix `?` versus ternary parsing;
- exact overload conflict behavior;
- `scanf`/`fscanf` left-context details;
- C compatibility translations;
- diagnostic completeness;
- checked-mode boundaries;
- standard-template pseudocode;
- any question using MUST/MUST NOT/SHOULD/MAY wording.
