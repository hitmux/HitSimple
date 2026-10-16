# Raw-memory floating Views

Standard §§14.0–14.1 define `view`, `mem_view`, and `mem_lview` as View
capabilities, not numeric conversion targets. A floating View may be read as
bytes, and a writable floating View may be the destination of a raw-memory
write. These operations do not convert its IEEE value to an integer.

## Correction

Builtin argument plans now retain the original View semantics for the manifest's
View meta-parameters. Their bootstrap ABI widths no longer create a spurious
floating-to-integer conversion plan. Identity plans also retain dynamic View
lengths; conversions to fixed user-function parameters still require fixed
lengths and their existing checks.

Both expression and statement `memset` read exactly the first byte of the fill
View, zero-extended for the host C ABI. This handles floating ABI parameters,
floating literal/call/ternary rvalues, strings, and positive-length byte Views without an
integer conversion or a four-byte overread. A dynamic fill View is checked for
a readable byte in checked mode.

This change covers raw-memory calls and the shared argument-plan boundary. It
does not establish conformance of every builtin's individual lowering or every
View shape. Floating assignment-expression materialization, zero-length dynamic View
registration, other low-byte builtins, generic length inference, named return
storage, and multi-assignment remain independently scoped work.

## Regression gates

- Sema checks identity metadata at all four floating widths, dynamic View
  metadata, and rejection of non-writable floating destinations.
- LLVM checks verify modules, raw pointer calls, a one-byte fill load, and no
  floating-to-integer conversion instructions.
- The runtime suite copies and compares signed zeros, finite representations,
  and quiet NaNs through ordinary floating ABI parameters, calls, and ternaries;
  checks direct destination mutation and single evaluation; and round-trips
  floating Views through `fwrite`/`fread`. It also covers literal and positive-length dynamic
  rvalues. Raw NaN copies do not claim arithmetic payload preservation. The constructed
  f128 bit patterns assume the supported little-endian targets.
- The manifest runs both providers, all three safety modes, and O0/O2, with
  exact zero-failure output. Test configuration is not itself execution evidence.

```sh
./build/hsc_unit_tests Sema_PreservesFloatingBuiltinViewArgumentPlans \
  Sema_PreservesDynamicBuiltinViewArgumentPlans \
  Sema_RejectsFloatingMemoryRvalueDestination \
  LLVMCodegen_PreservesFloatingRawMemoryBuiltinViews
ctest --test-dir build -R '^hsc_raw_memory_floating_views$' --output-on-failure
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```

The known limitation in the immutable 0.3.10 release notes describes that
released version. This source correction is not a new release.
