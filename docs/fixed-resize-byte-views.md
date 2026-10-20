# Fixed resize byte Views

Standard §§7.8 and 14.2 define `resize_bytes` as an address-order byte copy:
copy `min(source length, result length)` bytes, fill any higher-address tail
with zero, and return an untemplated, nonnumeric rvalue. No numeric conversion,
sign extension, byte-order change, or implicit `bytes` template is permitted.

## Correction

The old literal-length shortcut for 1, 2, 4 and 8 bytes lowered resizing to an
integer-shaped builtin call. A floating lvalue happened to work because the
integer emitter loaded its representation; a floating function result instead
failed with `function call result is not an integer`. Literal and other
nonnumeric rvalues could also reach inappropriate integer emission. Integer
truncation/extension was not a portable address-order byte-copy implementation.

All resize lengths now use the existing byte-buffer HIR and copy lowering.
Its metadata carries the static extent when known, an empty template name,
raw-only interpretation, and rvalue capabilities. Fixed result buffers use
function-entry storage, so a resize inside a loop does not accumulate stack
allocations. A statically known length expression is still evaluated, including
side effects in `length(call())`. Runtime-sized allocation retains its existing
path. The obsolete integer-call codegen entry reports malformed HIR instead of
silently converting a value.

Static safety validation also treats formation of a resized rvalue as an actual
source read, including when only its length or raw View is consumed. It checks
the copied prefix rather than the whole source View extent. Known freed and
out-of-bounds sources remain diagnosed; valid shorter prefix reads and zero-byte
reads do not acquire a false whole-View access requirement.

## Bounded regression evidence

- Structural tests cover literal/folded/static-`length`/runtime/zero extents,
  raw-only metadata and non-writable results. Numeric conversion requires an
  explicit interpretation after resizing.
- Static-safety tests reject known freed/short sources through memory, length,
  initialization and nested-resize consumers, with valid-prefix and zero-byte
  controls in static-checked/checked modes.
- Verified LLVM tests cover all four formerly special result sizes in all three
  safety modes, a single floating source-call evaluation, byte copy/zero-fill
  operations, no floating-to-integer conversion, and entry-block fixed storage.
- The native fixture independently builds expected bytes with `memcpy` and
  `memset`. It covers f16/f32/f64/f128, signed and unsigned integers, bool, addr
  representation, raw bytes, cstr and a packed user template. Float cases include
  finite values, negative zero, quiet NaN representations, calls, ternaries and
  explicit conversions. Literal strings with embedded NULs and multi-byte
  characters preserve their complete source representation. Integer-literal
  padding is distinguished from signed numeric widening.
- Exact-size, prefix truncation and higher-address padding are checked, including
  3- and 16-byte controls. Other cases cover assignment-result snapshots, source
  mutation inside the consuming function, returned copies of local sources, nested resize,
  slices, dereferences, runtime lengths, side effects and repeated loop use.
- The host matrix runs both library providers, three safety modes and O0/O2.
  Constructed f128 signed-zero/NaN patterns assume the supported little-endian
  targets; the copy oracle itself uses address order. A configured matrix is
  not execution proof; actual results belong to the tested build or CI.

```sh
./build/hsc_unit_tests Sema_ResizeBytesProducesRawRvalueViewsAtEveryLength \
  Sema_ResizeBytesRequiresExplicitNumericInterpretation \
  LLVMCodegen_ResizeBytesCopiesRepresentationsAtFixedWidths \
  StaticSafetyAnalyzer_ResizeCopiesValidateActualSourcePrefix
ctest --test-dir build -R '^hsc_fixed_resize_byte_views$' --output-on-failure
python3 tools/standard_evidence.py --check
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```

## Integration with unsigned snapshots

The integrated `resize-unsigned-integration` harness composes the independently
reviewed resize and unsigned-View implementations. Both providers, all three
safety modes and O0/O2 exercise known-empty and dynamic-empty results, with
source/length calls still evaluated. Repeated sites alternate positive, zero
and positive extents through resize, postfix `?`, `byte_swap`, and selected
fixed-length ternaries. Independent byte arrays check every observed prefix and
reversal. Other cases cover fixed raw literal/UTF-8 ternaries, upper-half u64
magnitudes, unsigned negative snapshots, named returns and rebound prefixes.

The source resize stage was reviewed on a base that still rejected checked
zero-length temporary registration. The integrated unsigned stage already
repairs that registration path and clears the old range when an existing site
becomes empty. These native integration cases now exercise that composition;
they do not declare zero-byte storage objects legal. Static diagnostic controls
also distinguish a short prefix copied before postfix `?` from a full unsigned
source snapshot made before resizing, including when the result length is zero.
A downstream short/empty resize cannot suppress an earlier snapshot read.

```sh
./build/hsc_unit_tests StaticSafetyAnalyzer_ResizePrefixDoesNotShortenUnsignedSourceSnapshot
ctest --test-dir build -R '^hsc_resize_unsigned_integration$' --output-on-failure
```

## Separate limits

This change does not establish all temporary-lifetime enforcement. Checked mode
currently permits a pointer returned by `strchr` into a resized temporary to be
read in the following full expression. The pre-change runtime-length path also
reproduces this: it registers the temporary but does not invalidate it between
those statements. Snapshot and return-copy tests here do not claim to fix that
independent full-expression lifetime gap. Unbounded dynamic resize allocation
also remains separate work. Checked zero-length registration is repaired by
the integrated unsigned stage as described above.

The separate [byte-swap correction](byte-swap-raw-views.md) addresses its result
template. Ordinary floating unary minus remains rejected by sema; signed-zero
resize inputs use representation bytes and do not depend on floating negation.
