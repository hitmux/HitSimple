# Named return storage

Standard §§4.4, 5.3, 5.5 and 12.2 require a named return item to create local
storage at function entry, with its statically declared length and template.
The name is visible in the function-body scope alongside parameters. Its bytes
remain **uninitialized until explicitly written**; there is no zero-initialization
promise.

## Implemented behavior

Ordinary functions, template methods and user operators use one body-lowering
path. It places return storage before body statements. Same-scope duplicate
return names, parameter/return collisions and local/static redeclarations are
rejected. Nested blocks may shadow a return name; a bare return still reads the
original return slot, and a recursive call gets independent local storage.

Explicit return expressions assign to return slots in item order, using the
normal destination assignment rules. This includes integer/floating storage,
Boolean normalization, equal-length raw byte Views and user `op =`. C-string
rvalues are materialized once, then copied/truncated with bounded termination
just like named cstr sources. A later expression
can observe a preceding named return assignment. Mixed named/anonymous signatures
use private storage for their anonymous items to retain that order. Each source
expression is evaluated once.

A bare `return` copies the current named storage bytes without another assignment
or normalization. Thus a bool return slot written as `u8` with byte 255 preserves
255 on bare return, while explicit `return value` to that bool slot writes 1.
The signature still determines the returned View if `set` changed the name's
persistent interpretation in the body. Return-slot binding is independent of
nested lexical shadowing.

A nonempty signature containing any anonymous item still requires return
expressions. Explicit counts must match the full signature. This change does
not introduce implicit return on fallthrough: existing missing-return diagnostics
remain, including for named signatures. Empty-return functions continue to use
bare return normally.

## Boundaries still open

- INIT-001: the compiler/runtime does not yet track initialized bytes generally.
  For example, `func f() -> value as i32 { return; }` currently compiles even in
  static-checked/checked modes, although reading that unwritten slot has undefined
  behavior and a provable read must be diagnosed under §5.3. The allocation test
  inspects this function's IR only; it never executes or assigns a value to those
  unwritten bytes. No conformance claim is made for initialization diagnostics.
- Anonymous-only signatures without cstr retain the prior lowering and its
  separately documented assignment limitations. Signatures containing cstr now
  share return storage through the bounded [cstr assignment correction](cstr-assignment-contracts.md).
- General multi-return destination assignment and name rebinding remain separate
  work. The runtime pair tests use already-supported same-width destinations.
- Existing method/user-op return-count limits, omitted-signature legacy inference,
  C ABI boundaries, arbitrary rvalue member receivers and broad initialization
  analysis are not expanded here.

The immutable [0.3.10 release notes](releases/0.3.10.md) describe that released
version's missing named storage. This source correction is not a new release.

## Regression gates

`SemaNamedReturnTests.cpp` inspects local storage placement and absence of an
initializer, explicit store ordering, saved binding/signature identity, bool
Any-View assignment, shared method/operator paths and collision/count errors.
LLVM tests check no invented zero store and retained missing-return diagnostics.
The existing bool test now writes its named slot and verifies raw bare return.

The runtime fixture exercises signed/unsigned integer widths, f16/f32/f64/f128,
16-byte raw Views, cstr/none/address/handle results, bounded string rvalue
copy/truncation, all 256 bool storage bytes,
global and nested shadowing, recursion, mixed and multiple returns, once-only
source evaluation, methods/operators and observable user-assignment calls.
Every executed return slot is written before reading.

```sh
./build/hsc_unit_tests Sema_NamedReturn LLVMCodegen_NamedReturn \
  Sema_PreservesInferredAndExpressionlessReturnScope
ctest --test-dir build -R '^hsc_named_return_storage$' --output-on-failure
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```

The harness selects both stdlib providers, all three safety modes and O0/O2,
requiring exact zero-failure output and exit status. Configuration alone is not
execution evidence or proof of all Standard clauses.
