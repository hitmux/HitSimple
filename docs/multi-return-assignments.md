# Multiple-return destination assignments

Standard §§7.2, 7.10, and 18.2 item 31 require each multiple-return component
to obey its destination's ordinary assignment contract. Equal byte lengths do
not permit copying an `f64` result directly into a `u64` destination with `=`.

## Lowering boundary

One function call produces the complete source sequence. Sema captures its
components in compiler-private storage with their declared lengths and templates,
then lowers each non-discard destination through the shared ordinary assignment
matcher in left-to-right order. These physical captures remain rvalue source
Views. The existing aggregate-return ABI and raw capture HIR are unchanged.

The destination matcher handles default template assignment, supported explicit
operators, integer width changes, writable members/slices/dereferences, byte
copies, Boolean normalization, and user `op =`. A user operation borrows its
writable destination and receives a copied source. A discarded component does
not run a destination operation; even an entirely discarded call executes once.
The lowering result refers to the final non-discard destination, including its
length and template, rather than to the function return or private capture.
Multiple-target syntax currently occurs only in statement position.

The shared matcher also now accepts handle Views for default `bool` assignment,
equal-length `bytes` assignment, and explicit `%b=` to an ordinary destination.
These contracts accept any View; other handle-source destinations retain their
existing restrictions.

## Bounded regression evidence

- Structural HIR tests assert one capture call precedes destination operations,
  return indices are distinct, source operands retain `f64` metadata and rvalue
  flags, user `op =` receives a writable destination, and unsigned widening uses
  its original source template.
- Negative tests reject equal-width float-to-integer defaults, mismatched user
  templates/byte lengths, result-count mismatches, and rebinding targets. The
  float-to-integer fixture runs in all safety modes.
- Verified LLVM modules cover all four floating widths, one aggregate call and
  two extracts, explicit destination interpretation, and default `f64` to `f32`
  precision conversion.
- Runtime tests observe integer widening/truncation, four floating widths,
  default and explicit Boolean assignment, user-operation mutation and source
  isolation, repeated targets, full/partial discard, members, fixed slices,
  dereferences, dynamic-index target order, single address evaluation, byte
  copies, and handle-to-bool/bytes assignment.
- Runtime and diagnostic manifests configure both providers, three safety modes,
  and O0/O2. Release workflow selections include native AArch64 and Windows;
  configuration is not a claim those platforms were executed locally.

```sh
./build/hsc_unit_tests Sema_MultiReturn LLVMCodegen_MultiReturnDestinationContracts
ctest --test-dir build -R '^hsc_multi_return_assignment' --output-on-failure
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```

This stage does not fix evaluation ordering for multiple separate RHS
expressions, single-source discard, or `&=` rebinding. It inherits ordinary
assignment's current target/operator limits, including same-width nonliteral
explicit `%f=` and unsupported floating explicit stores through dynamic pointer
forms. It does not establish every assignment matrix entry, dynamic-return
support, general callable forms beyond the existing direct function-call path,
or full conformance of multiple assignment.

## Named-return integration

Named producer returns perform their own ordinary assignments before result
capture. The Box runtime oracle observes both producer and destination user
operations (7 becomes 17 in each named slot, then 27 in each destination).
A fully discarded call still performs producer assignments. Raw named slots
are initialized in place and bare-returned without redeclaring their names.
