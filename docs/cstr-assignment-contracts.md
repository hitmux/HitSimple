# Default cstr assignment contracts

Standard §§7.2, 11.10 and 18.2 item 31 restrict default cstr assignment to a
string literal or a View whose current template is cstr. Equal byte lengths
and the existence of physical source storage do not make numeric, raw, bytes,
user-template or handle Views acceptable. Explicit `%s=` retains its distinct
raw string interpretation contract from §7.5.

## Shared destination path

Declarations, ordinary assignments, named return storage and captured
multiple-return components now check the same default cstr source contract.
A temporary `as cstr` interpretation is a cstr source; clearing that
interpretation with `as none` is not. No numeric conversion is introduced.

A fixed cstr call, conditional, interpretation View or assignment-result
snapshot is materialized once before the existing bounded StringCopyStore.
The source's visible length and the destination's capacity stay separate.
Calls execute once, only the selected conditional branch executes, and the
assignment expression continues to report the destination's length/template.
Existing direct storage sources and string-literal stores retain their lowering.

Section 12.2 assigns return expressions to return Views in item order.
Ordinary anonymous signatures containing a cstr result now use the existing private
return-storage path too, permitting string padding/truncation instead of
requiring an exact source length. All sibling items in such signatures use that
same established assignment path. Their private slots are not source-visible,
and an expressionless return remains invalid when any return item is anonymous.
Anonymous signatures without cstr and explicit C-ABI signatures are not changed
by this patch.

## Bounded regression evidence

- Sema tests reject nine source categories across direct and multiple-RHS assignment, explicit
  and inferred declarations, global/static initializers, named return writes, named and anonymous return
  expressions, multi-return destinations and multi-return producer expressions.
  Separate cases reject integer/float/character/Boolean literals and cleared
  string interpretations. Structural tests inspect fixed cstr materialization
  and unchanged explicit `%s=` raw-variable acceptance.
- LLVM verification covers all three safety modes, one call per source,
  named/anonymous return paths, one multi-return call with distinct extracts,
  aligned byte stores and absence of float-to-integer conversion.
- Runtime checks cover calls, global/static initializers, selected conditional
  branches, assignment snapshots,
  member capacities/sentinels, temporary source and destination interpretations,
  one-byte/equal/shorter/longer destination capacities, embedded-NUL source Views,
  zero fill, named/anonymous/mixed return signatures, multiple/repeated targets,
  explicit raw integer/floating/bytes/user-template inputs, method/operator cstr
  results, and anonymous mixed siblings (bool/bytes, integer/floating width
  conversions and user-assignment effects).
- Three negative CLI fixtures reject f64 sources in direct, multiple-RHS and multiple-return
  default assignments. Runtime/diagnostic manifests select both standard-library
  providers, all three safety modes and O0/O2. Release selections include native
  AArch64 and Windows; this configuration is not a claim of execution there.

```sh
./build/hsc_unit_tests Sema_Cstr LLVMCodegen_Cstr Sema_NamedReturn
ctest --test-dir build -R '^hsc_cstr_assignment_' --output-on-failure
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```

## Remaining boundaries

This is a source-acceptance and fixed-View lowering correction. It does not
rewrite generic multi-RHS evaluation, rebinding, all anonymous non-cstr return
contracts, source-termination safety checks, overlapping copies or every cstr
operation. Dynamic-length cstr rvalues remain unsupported in this path.
Explicit `%s=` still accepts string literals and existing named-storage source
forms; arbitrary nonliteral rvalues have not been added to it. Inherited complex
target limits, such as a cstr interpretation of a fixed slice, remain separate.

## Separate-source capture integration

Default source-template validation precedes the captured-source copy path.
Valid captured cstr Views are copied from their existing evaluated location;
ordinary fixed rvalues use one-time materialization. Multiple separate RHS
f64-to-cstr negative cases guard against bypassing validation in this path.
