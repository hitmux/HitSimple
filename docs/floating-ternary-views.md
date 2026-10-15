# Floating ternary View copies

Standard §4.8 evaluates the condition first, establishes a sequence point,
and evaluates only the selected ternary branch. The result is an rvalue copy
of that branch, with the common statically known length and template. This is
a View copy, not numeric conversion or floating arithmetic.

Floating ternaries previously reached integer lowering while materializing
Views. Selecting ordinary floating ABI parameters therefore failed with
`cannot use floating ABI object ... as an integer`. Typed floating consumers
also lacked a ternary lowering path and reported `unsupported float expression`.
Both failures occurred for `f16`, `f32`, `f64`, and `f128`.

## Implementation

The floating-category ternary path in `emitViewValue` creates one fixed-size,
byte-aligned temporary. After condition evaluation, control flow enters only
the selected branch, materializes its View and copies its bytes into that
temporary. Typed floating consumers load the same copied View. Checked-mode
temporary registration follows the existing runtime lifecycle.

This preserves the representation of positive/negative zero, finite values,
and quiet-NaN payloads without doing arithmetic on the selected value. NaN
payload propagation during arithmetic remains implementation-defined; this
change tests only byte-copy behavior. Boolean consumers still test all bits
(§7.6), so negative zero is true. `as` and ordinary parameter copying retain
their byte-preserving contracts (§§6.7, 12.1).

The existing integer and nonfloating ternary lowering paths are unchanged.
No source-level template, branch matching, implicit conversion, or numeric
conversion rule is broadened.

## Regression gates

Sema tests retain fixed floating rvalue View semantics at all four widths.
LLVM tests verify branch-local byte copies, full-width integer Boolean tests,
absence of floating conversion/comparison instructions, and branch-local
calls. Runtime checks exercise raw representations, both choices, nested and
computed branches, typed and Boolean consumers, and side-effect sequencing.
The runtime manifest spans both providers, all three safety modes, and O0/O2;
release validation explicitly runs it on AArch64 and Windows as well.

```sh
./build/hsc_unit_tests Sema_FloatingTernaries LLVMCodegen_FloatingTernar
ctest --test-dir build -R '^hsc_floating_ternary_views$' --output-on-failure
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```

Direct expression-level `as` on ternaries is covered by the separately scoped
[ternary expression View correction](ternary-expression-views.md). Generic
AST length inference and direct floating arguments to raw-memory builtin
metadata remain separate boundaries. Raw-memory observers use explicit
`bytes` Views.
