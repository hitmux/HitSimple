# Assignment-result View copies

Standard §§7.2 and 8.2 require an assignment expression to finish the write,
then form an rvalue copy of the current destination bytes. A user `op =`
returns the destination's observable contents even if its body returns a
different same-template value.

The previous HIR retained the destination's lvalue flags. Code generation
materialized assignment expressions only through the integer path, rejecting
floating ABI objects and typed floating consumers. A raw observer also needs
a snapshot: a later argument may overwrite the same assignment destination.

## Implementation

Assignment-expression metadata now clears addressability and mutability while
retaining the destination's length, template, and interpretation attributes.
The verifier checks that rvalue contract separately from the assigned View's
value semantics. A shared View path emits the existing stores, reads their
result View, and copies its bytes into an aligned temporary. Integer, floating,
and raw consumers observe that same snapshot. It does not convert floating
values numerically merely to copy their representations.

This correction covers the existing direct-name assignment-result lowering.
Complex target results, multi-assignment RHS sequencing, multi-return target
assignment, address rebinding, and generic expression-length inference are
separate stages. It does not make an assignment expression writable.

## Evidence

Structural tests check rvalue metadata for i32 and all four floating widths;
a negative HIR test rejects writable assignment results. LLVM tests verify
byte-copy materialization and no floating-to-integer conversion.

The runtime suite covers floating ABI parameters, typed returns/declarations,
raw memory observers, single evaluation, repeated temporary reuse, signed zero
and copied quiet NaNs. Argument snapshots are tested without relying on the
chosen argument evaluation order: assigning different values to the same name
in two argument expressions must produce different snapshots in either order.
Integer, string, and user `op =` controls verify ordinary behavior and that the
actual assigned target is observed rather than the operation's return value.
The f128 special-value byte patterns assume supported little-endian targets;
no arithmetic NaN-payload guarantee is claimed.

```sh
./build/hsc_unit_tests Sema_AssignmentExpressionsProduceRvalueViews \
  LLVMCodegen_CopiesFloatingAssignmentResultViews \
  Hir_VerifierRejectsLvalueAssignmentResult
ctest --test-dir build -R '^hsc_assignment_result_views$' --output-on-failure
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```

The manifest selects both providers, all three safety modes, and O0/O2.
Windows and AArch64 native validation explicitly select the suite. A configured
matrix is not an execution report; the exact-candidate PR records results.
