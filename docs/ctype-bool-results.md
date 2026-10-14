# Canonical character-predicate results

## Scope

The four character predicates in Standard §14.7 return one-byte `bool` Views:
`is_digit`, `is_alpha`, `is_alnum`, and `is_space`. Their optimized LLVM
lowering now zero-extends each computed `i1` predicate to canonical `i8`
`0x00` or `0x01` before returning it to expression consumers.

Previously the producer returned `i1`, although its declared View was one
byte. Consumers such as equality, ordinary arguments, ternaries, integer
assignment, and numeric casts could sign-extend true to `0xff`. Thus
`is_space(9) == true` was false and `to_i32(is_space(9))` was -1, while
direct truth tests and default bool storage appeared correct. This happened
at both O0 and O2; it was not an LLVM optimization-pass defect.

The fix is confined to those four producers. Case conversion, low-byte input
handling, generic integer widening, View reinterpretation, argument copies,
and operation acceptance are unchanged. Standard §11.5 still rejects bool
arithmetic, bitwise operations, and shifts.

## Regression evidence

`LLVMCodegen_MaterializesCtypePredicatesAsCanonicalBoolBytes` uses nonconstant
arguments for each classifier. It verifies the module, checks eight canonical
`i1`-to-`i8` producer extensions per classifier across the affected consumers,
and rejects any sign-extension of an `i1` result.

The `ctype-bool-representation` runtime suite uses an explicit expected output
for both providers, three safety modes, and O0/O2. It exercises equality,
arguments, returns, ternaries, storage, conversions, and byte observations.
Ordinary integer widening and raw bool argument-copy controls guard against
an overly broad normalization fix. The existing `ctype-providers` suite
continues to check the separate low-byte input contract.

Run the focused and full gates with a freshly built compiler:

```sh
./build/hsc_unit_tests \
  LLVMCodegen_MaterializesCtypePredicatesAsCanonicalBoolBytes \
  Sema_RejectsOrdinaryBoolArithmeticBitwiseAndShiftOperations \
  Sema_RejectsBoolUnaryMinusAndBitwiseNot
ctest --test-dir build -R '^hsc_ctype_' --output-on-failure
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```

## Related Boolean-value boundaries

The independently scoped [Boolean value-read correction](bool-value-reads.md)
handles noncanonical raw bool equality, explicit numeric conversions, explicit
bool return assignment, and full-width host `feof` results. It preserves raw
View reinterpretation and ordinary argument copying rather than globally
normalizing every bool-typed View.
