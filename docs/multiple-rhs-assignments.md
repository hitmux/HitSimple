# Multiple-assignment source Views

Standard §7.10 evaluates the complete RHS sequence before assigning destinations
in their left-hand order. A source is a View, not necessarily a byte snapshot.

For `g = 3`, `g, out = 9, observe()` must run `observe()` before either target
write. When `observe()` returns `g` as `i32`, `out` receives 3. In contrast,
`x, y = y, x` with `x = 1` and `y = 2` captures two lvalue locations: the first
assignment writes 2 into `x`, then the second reads that current `x`, leaving
both names containing 2. `x, y = y + 0, x + 0` evaluates rvalues first and swaps
the original values. These oracles follow the View distinction in §§6 and 7.1.

## Lowering boundary

Sema emits `CaptureView` statements before the destination operations. Each
`CapturedViewRef` preserves the original template, integer interpretation,
length kind, and lvalue flags. LLVM lowering forms the source once: an lvalue
keeps its address and length; an rvalue keeps its evaluated backing bytes.
Runtime-length byte Views preserve both their pointer and evaluated length.
Contextual floating and extreme integer literals retain ordinary assignment
acceptance. Boolean and bytes destinations do not accidentally change a
floating literal's source width to their own width.

Every `_` still evaluates its source. An all-discard assignment has only
captures and no destination operations. The existing direct multiple-return
and left-context input paths remain separate. For ordinary targets the shared
assignment matcher handles destination conversions and user `op =`. A user
operation borrows the destination and copies its source when that destination
operation runs. Fixed cstr copies can consume a captured source View, preserving
aliasing without re-evaluating function-return sources.

Targets are located in target order after the captures. Dynamic source indices
and pointer expressions are evaluated once when their Views are formed.
Checked mode validates a captured lvalue again when its bytes are consumed;
source evaluation may have invalidated the located storage in the meantime.
Static safety visits capture expressions and their effects, retains live facts
for direct named lvalues, and snapshots available rvalue facts. Complex lvalue
value facts are conservatively unknown instead of recomputing their addresses.

The compiler currently evaluates separate RHS expressions left-to-right. This
is an implementation choice under §8.10, not a new language guarantee or a
license to rely on unsequenced conflicting side effects. Regression oracles do
not depend on an order between conflicting RHS byte reads and writes.

## Bounded evidence

- Structural HIR tests require every source capture before destination stores,
  preserve live lvalue flags versus call rvalues, retain dynamic lengths, keep
  discarded calls, and exercise user assignment borrowing.
- HIR verification rejects source references without matching preceding
  formation or with altered lvalue metadata.
- LLVM tests verify the global observation call precedes its target store,
  discarded calls survive, all four floating widths lower without float-to-int
  conversion, and static safety still sees discarded division by zero and the
  distinction between lvalue and rvalue dataflow.
- Runtime fixtures cover the call barrier, single/all/partial discards, aliased
  lvalues and copied rvalues, mixed calls, source address/index capture, ordered
  target addresses/indices, compound assignment, repeated targets, members,
  fixed slices, user `op =`, all floating widths, mixed precision, bool and
  explicit `%b=`, bytes, signed/unsigned width changes, cstr copies, and dynamic
  resize lengths.
- Negative fixtures cover invalid discarded expressions, result-count mismatch,
  float-to-integer default assignment, user-template mismatch, bytes-length
  mismatch, and an out-of-range literal. Both manifests run both providers,
  three safety modes, and O0/O2. Release selection includes AArch64 and Windows;
  that configuration is not evidence of local execution on those platforms.

```sh
./build/hsc_unit_tests Sema_MultipleAssignment Hir_CapturedViews LLVMCodegen_MultipleAssignment
ctest --test-dir build -R '^hsc_multiple_rhs_' --output-on-failure
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```

## Limits

This stage does not implement true §7.7 `&=` name rebinding. The preceding
assignment-result stage supplies floating snapshots and rvalue metadata. It inherits the
ordinary matcher limits for target/operator combinations, including explicit
floating stores through dynamic pointer targets, compound pointer targets that
re-evaluate their target address, and same-width nonliteral explicit `%f=`.
Complex lvalue static-value/lifetime proofs, dynamic string-copy lengths,
multiple-return components mixed into a list of other RHS expressions, general
unsequenced-conflict diagnostics, and full assignment conformance remain
outside this witness. No Standard text is changed.
