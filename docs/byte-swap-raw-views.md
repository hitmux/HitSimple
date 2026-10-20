# Byte-swap raw rvalue Views

Standard §14.2 defines `byte_swap(expr as view) -> none[length(expr)]`:
reverse every byte in the source View, preserve its extent, and return a
nonwritable rvalue without a template. It is a representation operation, not a
numeric conversion (§7.9); explicit interpretation is needed before a numeric
consumer. The byte-buffer model follows §7.8.

The semantic correction applies to the integer intrinsic shortcut, the generic
fixed-size reversal, and the runtime-sized path. All three now expose RawBytes,
RawOnly, an empty template, and neither addressability nor lvalue mutability.
Only byte_swap is exempted from the two early bootstrap handle restrictions;
opaque handles remain invalid inputs to numeric conversion, resize, free, and
ordinary arithmetic. No runtime or production code-generation change is needed.

## Bounded evidence

- Semantic tests inspect the result metadata and extent for bool, integers,
  all four floating widths, raw bytes, cstr, a user template, handle, addr,
  complete embedded-NUL/UTF-8 literals, slices, dereferences, nesting and
  runtime/zero resize inputs. Two swapped raw-only operands do not introduce a
  bytes comparison candidate; explicit `as bytes` supplies that interpretation.
- Numeric conversion requires explicit `as u16`; unrelated handle prohibitions
  retain handle-specific diagnostics, including a standalone `free(file)`.
- LLVM tests verify all three safety modes, i32/i64 bswap intrinsics, the generic
  odd-width reverse helper, one floating source call, and no floating conversion.
- The runtime oracle constructs expected reversals independently with indexed
  byte copies at widths 1, 2, 3, 4, 5, 8, 16 and 33. Source categories include
  negative-zero/quiet-NaN floating representations, a real opened file handle,
  addresses, full literal bytes, slices, dereferences, nested reversal,
  assignment and named returns. Counters are checked after the full expression.
  Repeated dynamic sites cover positive, zero and positive extents; an empty
  result still evaluates its source call.
- The portable host harness configures both providers, all three safety modes
  and O0/O2. Constructed floating bit patterns assume the supported little-endian
  targets; the indexed-byte oracle itself uses address order. Configuration and
  source inspection do not establish test execution.

```sh
./build/hsc_unit_tests Sema_ByteSwap LLVMCodegen_ByteSwapReversesRawRepresentations
ctest --test-dir build -R '^hsc_byte_swap_raw_views$' --output-on-failure
python3 tools/standard_evidence.py --check
```

This is a bounded result-contract correction. It does not establish all
full-expression lifetimes, uninitialized-byte tracking, allocation limits,
operator meta-parameter matching, every View form or complete conformance.
