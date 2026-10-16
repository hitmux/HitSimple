# Validated declaration and `length` Views

Standard §§4.1, 5.3, 6.4, 6.7, 12.3 and 14.1 require declaration lengths
and `length(view)` to describe actual, valid Views. `length` returns an
eight-byte `u64`; its argument is an evaluated expression, not unevaluated
syntax resembling a type query.

## Implemented boundary

Local, static-local and global declarations without an explicit or fixed-template
length use their initializer's validated HIR result. This accepts Boolean
literals/comparisons, indexing, fixed slices/dereferences, ternaries, chained
`as`, resolved function/method/operator results, and statically sized byte
resizing/reversal. Dynamic extents still require an explicit destination length.
An inferred declaration does not adopt its initializer's persistent template,
except the existing address/handle inference rules.

A declaration reserves its own name before checking the initializer. An
independent result contract, such as `length`'s eight-byte result, permits
`new count = length(count)` without reading uninitialized bytes. An unresolved
own name still shadows an outer name; an underdetermined `new value = value`
is diagnosed even when the outer name has an address or handle template.
Independent address/handle sources and self-address initializers retain their
existing inference. Top-level callable contracts and global names are collected
before inferred global extents are resolved, allowing forward references and
mutual `length` queries. Extent resolution does not reorder runtime global
initializers, which remain in source order.

`length` keeps one validated HIR operand and returns its emitted View extent.
An invalid name, call, template, branch, or arithmetic expression cannot be
hidden inside it. Calls, assignments, and selected ternary branches execute
once; forming a View of a simple name does not load that object's bytes.
The static-safety pass still traverses the operand, and can use a static
`length` result for bounds checks without discarding its effects.

Expression-level `as` preserves the validated operand's extent and value
category. Dynamic-to-fixed interpretation emits a length check in checked
mode; dynamic `bytes`/`cstr` interpretation emits a positive-length check.
Known empty resize/reversal Views cannot acquire a positive-length template.
`as none` remains valid for dynamic empty Views. Chained reinterpretations of
rvalues remain rvalues. Numeric `to_iN`/`to_fN` conversions likewise use their
validated operand length and floating category, including assignment results.

The runtime buffer representation used for arbitrary-width `resize_bytes`
now records a static HIR extent when the count is known. A retained
`length(call())` count preserves the call's effects. `byte_swap` uses the
source View extent for non-integer and arbitrary-width fixed inputs too.

## Evidence and limits

`SemaViewLengthTests.cpp` checks HIR widths/categories, own-name and forward
binding, resolved method/operator returns, conversions, invalid inputs, known
empty interpretations, and malformed HIR. LLVM tests check evaluated operands,
absence of storage reads for a simple-name query, and dynamic guards. Static
safety tests check retained constants and nested operand diagnostics.

The `validated-view-lengths` harness configures 36 host variants at O0/O2:
12 positive variants across both library providers and all safety modes, plus
24 compile-diagnostic/runtime-guard variants. The positive oracle requires
`view-lengths 0`, including source-order initialization and exact side-effect
counts. Invalid unchecked dynamic interpretation is compiled but deliberately
not executed. The nonempty runtime-helper test calls the guard directly;
that test alone is not evidence of end-to-end zero-View support. The later
[unsigned-snapshot stage](unsigned-view-snapshots.md) repairs empty temporary
registration and adds end-to-end empty View coverage.

Remaining independent boundaries are explicit:

- Fully runtime-sized slice construction is not implemented; these tests use
  supported fixed slices and dynamic `resize_bytes`/`byte_swap` Views.
- Empty temporary registration is covered by the later unsigned-snapshot stage;
  other zero-length library-input contracts still need their own audit.
- Other AST-inference consumers, such as template printing and some math APIs,
  need a separate full-domain audit. This is not a Chapter 14 conformance claim.
- Floating assignment materialization is supplied by the preceding assignment-
  snapshot stage. Integrated native checks cover conversions and evaluated
  length queries on assignment results at all four floating widths.
- The harness runs natively on its selected host. Windows and AArch64 release
  jobs explicitly select this suite; configuration is not an execution report.

```sh
./build/hsc_unit_tests
ctest --test-dir build -R '^hsc_validated_view_lengths$' --output-on-failure
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```
