# Raw View inputs to character functions

Standard §14.7 makes all six character functions consume the lowest raw byte of
a View. A floating object or rvalue is not first converted to an integer. Both
the intrinsic provider and the Core-HS reference provider must observe the same
byte, even though the reference module uses a private i32 parameter.

The two provider paths now use the shared one-byte View load. This avoids the
previous floating-ABI diagnostic and avoids reading four bytes from a shorter
View. Classification results retain canonical bool bytes; case conversion
retains the ASCII-only, unchanged-other-byte rule.

## Evidence and boundaries

The LLVM regression verifies aligned i8 loads for the six Standard functions
(and now eight [optional extensions](ascii-character-extensions.md)), without
floating-to-integer conversion for f16/f32/f64/f128 parameters. The runtime oracle exhausts all 256
low-byte values independently for those four floating widths and 1-, 3-, and
32-byte raw Views. It also covers floating calls evaluated once, ternaries,
assignment-result snapshots, positive dynamic one-byte Views, character and
string literals. Floating fixtures have zero high bytes, so they exercise byte
transport without depending on arithmetic or NaN-payload behavior. The existing
integer suite continues to cover wide and negative prefixes.

The new matrix selects both providers, all three safety modes, and O0/O2;
Windows and AArch64 native release validation explicitly select it. Exact
candidate execution results belong in its PR, not in this configuration claim.

This stage does not add character APIs or establish the entire Chapter 14 View
domain. Opaque-handle meta-View acceptance, zero-length dynamic registration,
and other builtin lowering paths remain under LIB-001. Snapshot cases depend on
the separately reviewed direct-name assignment-result fix.

```sh
./build/hsc_unit_tests LLVMCodegen_CharacterClassifiersObserveOneRawByte
ctest --test-dir build -R '^hsc_ctype_view_inputs$' --output-on-failure
```
