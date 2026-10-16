# Explicit `none` return Views

Standard §4.4 makes `-> [N]` equivalent to `-> [N] as none`, and the same
rule applies to named return items. Calling a function with the explicit
spelling previously failed HIR verification because its legacy `templateName`
field contained `"none"` while its canonical `ViewSemantics` used an empty name.

The two HIR expression-call constructors now normalize only the literal
`"none"` to the empty template name. This covers ordinary and core-extern
calls, template methods, and explicitly untemplated user-operator results.
Lengths, interpretation attributes, signature matching, and code generation
are unchanged. The verifier still rejects any divergent legacy field,
including a different nonempty template name or a field changed after
construction.

## Verification

HIR tests cover both constructors and retained mismatch diagnostics. Sema
checks cover anonymous and named signatures at widths 1, 2, 4, and 8; nested
calls and ternaries; bare and explicitly untemplated methods; user operators;
core externs; multi-return assignments; and invalid signatures/return sources.
The `none-return-views` runtime gate checks exact bytes, ordinary arithmetic,
nested calls, both conditional arms, and selected-branch evaluation counts.
It runs both providers, all three safety modes, and O0/O2, including the
Windows and native AArch64 portable gate selections.

```sh
./build/hsc_unit_tests Hir_CallResults Sema_NoneReturn
ctest --test-dir build -R '^hsc_none_return_views$' --output-on-failure
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```

Named return storage is covered by the later, separately scoped
[named-storage implementation](named-return-storage.md). This correction does
not change the existing default for unannotated user-operator returns, broaden
C-ABI types, or alter generic expression-length inference or raw-memory builtins.
