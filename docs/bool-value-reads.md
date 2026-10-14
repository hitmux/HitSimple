# Boolean value reads and return assignment

## Standard contract

A one-byte View reinterpreted `as bool` may contain any byte. `as` preserves
bytes (§6.7), and ordinary function arguments are View-value copies (§12.1).
An observer that reinterprets a bool parameter as `u8` must therefore see the
original byte, such as 2 or 255.

These distinct operations read a Boolean value or write a bool destination:

- Appendix 21 defines bool `==` and `!=` using `read_bool` on each operand.
  All nonzero patterns compare equal, and both operands are evaluated once.
- §14.2 explicitly reads bool conversion inputs as numeric 0 or 1 for every
  `to_iN`, `to_uN`, and `to_fN` function.
- §12.2 assigns explicit return expressions to return Views. For an explicit
  bool return signature, §§11.5 and 11.10 accept **Any View** and write 0/1;
  §§7.6 and 8.8 test all source bits. This includes wide integers, raw bytes,
  addresses, handles, user templates, computed floats, and dynamic Views.
  Floating negative zero is true because its sign bit is set. This is a
  storage-bit test, not an IEEE numerical comparison against zero.
- §14.5 declares `feof` as bool. The host C result must be compared against
  zero at its full `int` width before producing the one-byte result. Nonzero
  values such as 2 and 256 cannot be copied or truncated into language bool.

## Implementation boundaries

Sema records explicit Boolean reads with `BooleanTestExpr` only for bool
comparison operands, bool inputs to numeric conversion builtins, and explicit
bool return assignment. Return conversion plans use `BooleanNormalize`, keep
the original source View semantics, and specify a one-byte bool destination.
The HIR verifier checks comparison reads and bool return plan consistency.

Computed floating binary and numeric-conversion rvalues are materialized
through the floating emitter before the shared View-bit test. This also makes the same existing
Boolean-test path work for computed floats in conditions and bool assignment.
No floating numerical comparison is introduced.

`feof` receives a producer-local full-int nonzero comparison and zero
extension to `i8`. `fclose`, `fflush`, and `ferror` retain their `i32` results.
Generic integer casts/assignments, reinterpretation, ordinary View copies,
and bool arithmetic/bitwise/shift diagnostics are unchanged.

### Existing named-return limitation

The Standard distinguishes explicit return assignment from expressionless
return of existing named storage. The compiler currently does not declare
named return storage or accept expressionless returns for nonempty return
signatures. This patch does not implement that separate feature or alter its
diagnostics. A future implementation must preserve the existing storage
bytes on expressionless return rather than normalize them again. Legacy
omitted-signature return inference is also outside this correction.

## Regression gates

Sema tests inspect Boolean-test boundaries and original-source return plans,
including invalid source/destination plan controls. LLVM tests verify raw
parameter observation, Boolean value reads, full-int `feof` normalization,
and computed-floating/dynamic View materialization. Existing negative tests
continue to reject bool arithmetic, bitwise operations and shifts.

Runtime gates exercise both stdlib providers, all three safety modes, and
O0/O2, using independent integer oracles and deterministic host `feof` stubs.
They include all 256 byte patterns, all 65,536 equality pairs, every numeric
conversion width, raw byte preservation, exactly-once eager evaluation,
explicit return assignment, wide/noninteger Views, and integer controls.

Run the focused tests and then the complete suite:

```sh
./build/hsc_unit_tests Sema_RecordsBooleanValueReadBoundaries \
  Sema_BooleanReturnPlansPreserveAnyViewSourceSemantics \
  LLVMCodegen_ReadsBooleanValuesOnlyAtSemanticBoundaries \
  LLVMCodegen_NormalizesFeofBeforeNarrowing \
  LLVMCodegen_BooleanReturnsReadComputedFloatAndDynamicViewBytes
ctest --test-dir build -R '^hsc_(bool_|feof_)' --output-on-failure
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```
