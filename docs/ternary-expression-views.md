# Direct ternary expression Views

Standard §4.8 requires a ternary to validate both branches, select only one at
runtime, and produce an rvalue copy with their common template and statically
known length. Section 6.7 permits `as` to change interpretation while preserving
that length and those bytes.

The expression `(choose ? left : right) as u64`, with `f64` branches, previously
failed with `expression template view requires a fixed positive byte length`.
The ternary already carried a valid eight-byte View after semantic analysis,
but the expression-level `as` path asked the incomplete AST length inference
helper first.

## Bounded correction

Expression-level `as` now lowers its operand once and uses its validated HIR
View length and value category. The original direct-ternary correction is
subsumed by [validated declaration and `length` Views](validated-view-lengths.md),
which also supports inferred declarations and chained reinterpretations.
Existing destination-template validation still runs, and the retained operand
is reused without extra evaluation or numeric conversion.

The normal ternary analyzer remains responsible for the condition, both
branches, equal templates, and equal static positive lengths. This includes
nested ternaries and fixed-return call branches. Same-width different-template
branches, invalid unselected branches or call arguments, dynamic/zero branch
lengths, and wrong-width or unknown destination templates remain errors. The
result is still a non-addressable rvalue. Dynamic non-ternary interpretations
have separately checked extent rules described in the linked document.

For example, `new bits = (choose ? left : right) as u64` now infers eight
bytes from validated `f64` branches. `length()` validates and evaluates its
operand rather than querying the incomplete AST helper. Other inference-gated
consumers, such as template printing, remain separate audit work.

## Verification

Semantic tests inspect static length, original branch templates, and rvalue
properties for floating, integer, Boolean, raw, string, uninterpreted, and
user-template Views. They cover direct returns, typed initializers, nested
choices, function calls, Boolean literals, and rejection cases. LLVM tests
check branch-local calls and the absence of numeric floating conversions.

The existing `floating-ternary-views` runtime gate now also checks direct
reinterpretation of f16/f32/f64/f128 selections, both arms, nested/call branches,
exact negative-zero and quiet-NaN representations, and condition/branch
side-effect counters. Its unchanged matrix covers both providers, all three
safety modes, and O0/O2, including native platform release gates.

```sh
./build/hsc_unit_tests Sema_TernaryAs LLVMCodegen_TernaryAs
ctest --test-dir build -R '^hsc_floating_ternary_views$' --output-on-failure
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```
