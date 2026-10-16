# C-string literal assignment padding

Standard §7.5 treats `%s=` as a C-style string copy. Section 11.7 gives default
`cstr` assignment the same semantics. A zero byte inside a string literal ends
that string: assigning `"a\0b"` into four bytes writes `61 00 00 00` in hex,
not the literal View's raw `61 00 62 00` sequence. Every destination byte after
the copied prefix is zero, and the final byte is zero even when truncating.

## Lowering boundary

`StringStore` is produced only for literal `%s=` and default `cstr`
assignments, including the shared declaration and stored-return paths. Its
code generator bounds the copied prefix by the first decoded zero byte as
well as destination capacity minus one. The existing byte stores then zero-fill
the remainder with alignment one. The same rule applies at member offsets and
to explicit `%s=` destinations with non-cstr templates.

The literal decoder and `StringLiteral` emission are unchanged. Section 3.5.3
still gives the literal its complete decoded extent plus the appended terminator.
Raw `bytes` initialization, assignment and literal arguments preserve bytes after
an embedded zero. Inferred cstr destination lengths also retain that full extent.
This is a destination-write correction, not literal canonicalization.

## Bounded verification

- `LLVMCodegen_CstrLiteralStoresStopAtFirstNulAndPad` checks every emitted byte
  and alignment in all three safety modes for leading, middle and trailing NUL,
  empty and ordinary literals; capacities 1, 2, 3, 4 and 6; default declarations,
  assignments and named/anonymous returns; and explicit cstr/raw declarations,
  assignments and named returns.
- `Sema_CstrLiteralPaddingPreservesRawLiteralAndInferredLength` checks retained
  literal spelling, cstr source metadata and original lengths while distinguishing
  StringStore from raw ViewCopyStore.
- `cstr_literal_padding.hs` compares complete destination byte sequences, including
  overwritten nonzero tails, all literal shapes, shorter/equal/larger capacities,
  explicit/default writes and named/anonymous results. Return results are observed
  through `bytes` destinations so a second string copy cannot hide bad padding.
  Call counters check once-only execution. Additional controls cover raw bytes,
  direct literal arguments and lengths, globals/statics, member sentinels and
  assignment snapshots. Both providers, all three safety modes and O0/O2 are
  selected by the existing cstr assignment manifest.

The unchanged compiler failed the new IR byte oracle and the native checked
fixture reported 40 byte-check failures. This is regression evidence for this
bounded behavior, not an overall conformance claim.

```sh
./build/hsc_unit_tests Sema_CstrLiteralPaddingPreservesRawLiteralAndInferredLength \
  LLVMCodegen_CstrLiteralStoresStopAtFirstNulAndPad
ctest --test-dir build -R '^hsc_cstr_assignment_' --output-on-failure
```

Missing source terminators, overlap semantics, arbitrary nonliteral `%s=` rvalues,
rebinding, dynamic cstr lengths and unrelated return contracts remain separate.
