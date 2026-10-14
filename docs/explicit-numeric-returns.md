# Explicit numeric conversions in return expressions

The Standard evaluates a numeric conversion to its declared result View
(§14.2), then assigns the explicit return expression to the return View (§12.2).
For example, `return to_i32(value)` in a function returning `i32` has an
identity return-assignment plan whose source is the converted `i32` View.
The conversion input is not the source of that return-assignment plan.

Sema already records this distinction. When integer return assignment changes
width, it appends an `IntegerCastExpr` around the source expression; for an
identity or same-width return assignment, it does not. HIR verification uses
the conversion plan's kind and source/destination widths to unwrap only the
appended return-width conversion. A required wrapper must exist and produce
the destination width. Explicit conversions remain independently verified.

This fixes rejection of direct and nested `to_iN`/`to_uN` return expressions
without changing numeric conversion, return assignment, or code generation.
Boolean conversion reads still normalize to 0/1 (§14.2), while raw parameter
View copies and reinterpretations retain their bytes (§§6.7, 12.1).

## Regression gates

- HIR tests cover identity and same-width cross-interpretation return plans,
  nested explicit conversions plus an appended width conversion, mismatched
  source plans, incorrect wrapper widths, and missing wrappers.
- Sema tests inspect source/result boundaries, and LLVM tests verify modules,
  return widths, signed/unsigned extension and finite float-to-integer paths.
- The runtime suite covers both providers, all three safety modes, and O0/O2.
  It checks all raw bool bytes, signed and unsigned controls, explicit nested
  conversions, return widening, named and multiple explicit returns, finite
  in-range floating inputs, and exactly-once evaluation.

```sh
./build/hsc_unit_tests Hir_Return Sema_ReturnConversionPlans \
  LLVMCodegen_EmitsExplicitNumericReturnConversions \
  LLVMCodegen_ReadsBooleanValuesOnlyAtSemanticBoundaries
ctest --test-dir build -R '^hsc_explicit_numeric_returns$' --output-on-failure
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```

Narrowing return-assignment rejection, named expressionless return storage,
and floating ABI parameter ternary View materialization are separate existing
limitations. This change does not expand those implementation boundaries.
