# Checked file-handle state coverage

## Contract and scope

This first stage implements observed runtime state checks, not an ownership or
borrow checker. `Standard.md` sections 2.4, 11.6, 14.5, 16, and 18 require the
implementation to document detectable invalid handles; assignment copies the
opaque handle value and does not transfer exclusive ownership.

`handle` remains exactly `P` bytes and carries a native C `FILE*`. The public
headers, C ABI, and runtime function signatures are unchanged. Only checked
stdlib calls maintain or consult this state; unchecked and static-checked code
do not gain file-state checks or registry maintenance.

- A successful checked `fopen` records its address as open. A failed open
  returns a null handle and creates no entry.
- A checked `fclose` rejects a null or observed-closed handle before calling
  libc. Otherwise it records the address as closed **before** calling libc,
  including when the stream originally came from native code.
- `fclose` consumes the stream even if it returns an error, for example a
  buffered write/flush failure. Its original libc return value is preserved;
  subsequent checked use is rejected. Retrying `fclose` is not valid recovery.
- `fget`, `fput`, `fread`, `fwrite`, `fprintf`, `fscanf`, `fflush`, `fseek`,
  `ftell`, `feof`, and `ferror` reject an observed-closed address before a libc
  stream operation. Zero-byte `fread`/`fwrite` still validate the handle.
- Copies passed through assignments, function arguments, or returns share the
  same observed address state. Closing any copy invalidates all copies within
  the detection limits below. Equality/formatting of the opaque value remains
  unchanged; no copy is automatically cleared or destroyed.
- Formatted I/O checks again in the runtime after argument evaluation, since a
  formatting argument can call a function that closes the stream.

Null handles retain `hitsimple runtime error: invalid file handle`. An observed
closed handle reports `hitsimple runtime error: closed file handle`. Both exit
with status 120, append the compiler-provided call-site location when available,
and are not converted into a language `throw`.

## Detection limits and interoperability

- An unknown non-null FFI handle still passes through, preserving existing
  interoperability with valid native streams, including `stdin`, `stdout`, and
  `stderr`. Its validity is the caller's responsibility. This implementation
  does not try to dereference arbitrary pointers to discover validity.
- Only opens/closes routed through checked wrappers are observed. Native
  `fclose`, `freopen`, unchecked/static-checked modules, native descriptor
  operations, and other external stream changes are not intercepted.
- Native allocators can reuse a `FILE*` address. A checked reopen marks that
  address open again. An old alias with the same bits is then indistinguishable
  from the new stream, so its later misuse is **not detectable**. Conversely,
  an externally opened stream reusing an observed-closed address can be
  rejected because the runtime cannot observe that reopening. No generation
  token or wrapper pointer is substituted because that would change the ABI.
- Console runtime calls retain their existing null-as-default-stream convention;
  a null *explicit* file argument is still rejected by generated checked code.
- The registry is process-global and unsynchronized, matching the current
  object-registration runtime. Concurrent calls into checked runtime operations
  are unsupported, even on different streams. Thread-local diagnostic locations
  do not make the registries thread-safe. No cross-thread close/use guarantee is
  claimed.
- There is no new static close/use dataflow analysis, automatic close on scope
  exit, or cleanup of user streams during exception propagation.

## Metadata and failure policy

A growable table stores one numeric address and an open/closed flag per distinct
observed non-null stream address. Closed entries remain until process exit so
that a later alias use is still detectable; reopening the same address reuses
its entry. Lookups are linear in the number of distinct recorded addresses and
metadata grows with that number, not just currently open files. The table starts
at 16 entries and doubles, with checked capacity arithmetic. Allocation failure
reports `cannot grow file handle registry`; exhausted size capacity reports
`file handle registry is full`. Both terminate with status 120 instead of
silently abandoning tracking. These costs occur only on checked runtime paths.

## Acceptance and regression tests

Plan: PR 18
Issues: SAFE-001

| Requirement | Evidence |
| --- | --- |
| Normal open, alias argument/return, formatted read/write, close and reopen | `checked_file_handle_lifecycle.hs`, all three safety modes, O0/O2 |
| Double-close and close through a different alias | `checked_closed_file_handle.hs`, `fclose` case |
| Every consuming API and zero-length read/write rejects closed state | Fourteen checked cases in `file-handles.json` |
| Argument evaluation cannot bypass formatted I/O validation | `checked_file_handle_argument_close.hs` |
| No unchecked/static-checked instrumentation | LLVM codegen test and mode-specific compile-only invalid cases |
| No undefined libc use in negative runtime tests | Stubbed native stream operations exit with a distinct failure marker if reached |
| Close error, reentry, address reuse, unknown/native streams, null, table growth | `CheckedFileHandleTests.c` and its subprocess runner |

Focused commands after building this source tree:

```sh
ctest --test-dir build -L file-handles --output-on-failure
./build/hsc_unit_tests
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```

The native test driver includes the current runtime source and substitutes only
host stream operations and unrelated binary128 conversion helpers. Each failure
case runs in a separate process; it checks the exact status/reason and verifies
that a closed stream never reaches a host-operation stub. Real HitSimple fixtures
exercise the compiler, actual C streams, and runtime together. The stubs also
model close failure portably instead of relying on a platform-specific device or
calling libc with an already closed pointer.
