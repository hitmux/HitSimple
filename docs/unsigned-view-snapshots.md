# Unsigned View snapshots

Standard §8.9 defines postfix `?` as a byte interpretation: any operand View
produces a same-length, unsigned-integer rvalue with template `none`. The operand
is evaluated once, and its complete bytes are copied before later writes can
change them. No numeric conversion, width adjustment, or byte-order change is
part of postfix `?`.

## Implemented boundary

Sema preserves the validated operand's static or dynamic extent and removes its
template and lvalue flags. HIR verification enforces that contract, including
dynamic lengths and a required operand. Code generation materializes the operand
through the generic View path and copies its bytes into independent temporary
storage. Fixed integer consumers subsequently read those bytes at their full
source width before any consumer-requested extension or truncation.

This accepts floating, Boolean, address, opaque handle, `bytes`, `cstr`, user-
template, integer, raw character, and string Views. In particular, a noncanonical
Boolean byte is preserved, floating signed zero and NaN payloads are preserved,
a multibyte character retains all UTF-8 bytes, and a string retains its trailing
zero and bytes after any embedded zero. No string-termination condition is
imposed when the suffix reads a `cstr` as raw bytes.

A direct `pointer?` used as a memory-builtin input denotes the pointer's copied
bytes. It does not borrow the pointee region. The separately represented address-
offset extension retains its existing memory-operand behavior. Address provenance
is still available when the copied integer is explicitly used as an address.

Length queries evaluate their operands: `length(view)` only forms a named View,
whereas `length(view?)` reads the complete snapshot. Checked snapshots validate
that complete source extent and lifetime, including rebound names. Static checks
likewise diagnose provably short or expired backing storage.

Dynamic suffix results work with existing generic View consumers such as `length`,
Boolean tests, raw memory reads, byte reversal/resizing, and explicit `as`.
Positive and empty runtime extents remain intact. A checked empty temporary
registers no readable storage, and replacing a prior nonempty instance at the
same registration site clears its old range. This repairs the zero-length
registration prerequisite without declaring zero-byte storage objects legal.
Known `resize_bytes(..., 0)` currently uses the runtime-buffer HIR representation;
its observed result extent is still exactly zero.

C compatibility unsigned-result markers remain distinct width-adjustment cases.
A comparison/logical marker retains its Boolean result; the core suffix on a
Boolean View still produces the unsigned `none` snapshot.

## Cross-stage integration evidence

The `unsigned-snapshot-integration` harness adds 24 host variants at O0/O2.
Twelve positive variants cover both providers and all three safety modes:
embedded-NUL/UTF-8/long literal ternaries followed by `?`, once-only selected
branch calls, named raw and cstr returns, full-width upper-half u64 values in
16-byte named returns, unsigned widening of negative snapshots, 32-byte captures,
rebound RHS isolation, and dynamic positive/empty snapshots after fixed selection.
The cstr return oracle separately checks first-NUL padding; a one-byte cstr
return may scan only the available terminator, while a ternary snapshot must read
its whole selected extent. A short unselected rebound branch is not read.

The remaining 12 variants exercise selected-branch extent and expired-lifetime
guards: four checked executions require an invalid-load failure, while eight
unchecked/static-checked variants compile without executing undefined reads.
One LLVM test also observes two full-width branch copies followed by one suffix
copy in the merge block, with one condition call, across all three safety modes.
The native AArch64 and Windows release selections include both unsigned gates
alongside literal ternary and wide-literal gates. These selections are checked
structurally here; local host results do not establish execution on those targets.

## Evidence and remaining limits

`SemaUnsignedViewTests.cpp` checks categories, widths, dynamic/empty metadata and
explicit diagnostics for unsupported runtime-sized arithmetic. HIR tests reject
changed extents, lvalue results, incorrect interpretations, and missing operands.
LLVM tests verify one source call and byte-copy snapshots with no FP-to-integer
conversion. Static tests distinguish formation from reading and cover rebound
extent/lifetime, raw unterminated cstr reads and pointer-snapshot capacity.

The `unsigned-view-snapshots` harness configures 42 host variants at O0/O2:
12 positive provider/safety combinations, six formation-only cases, and 24
checked guard/compile-only combinations. The positive oracle requires
`unsigned-views 0`. It covers all four floating widths with positive/negative
zero and selected NaN patterns, 33-byte storage, literals, raw UTF-8, templates,
Boolean representation, addresses/handles, captured RHS isolation, rebinding,
once-only calls, dynamic positive/empty Views, and repeated temporary sites.
Invalid unchecked/static-checked accesses are compiled but never executed.

This does not complete general runtime-sized integer arithmetic or conversion.
Unary/binary integer operations now diagnose unknown computation widths rather
than silently treating a dynamic source as zero bytes. Numeric consumers that
still require a fixed width need explicit `as` with the required length; checked
mode validates that length. Runtime-sized slices remain a separate limitation.
Fixed raw/string ternary materialization composes with the independently reviewed
ternary stage. Both selected-branch copying and the subsequent unsigned snapshot
retain the complete View bytes.

Independent existing limits remain: fixed-size `resize_bytes` calls at 1/2/4/8
bytes retain an integer-lowering fast path that needs its own raw-byte audit;
explicit typed integer operators currently restrict widths to 1/2/4/8; some
negative floating-literal contexts lack floating unary lowering. These are not
redefined as restrictions of Standard §8.9 or §7.8. Host execution does not prove
other targets, every NaN payload, every library View input, or full conformance.

```sh
./build/hsc_unit_tests
ctest --test-dir build -R '^hsc_unsigned_view_snapshots$' --output-on-failure
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```
