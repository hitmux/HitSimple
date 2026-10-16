# Literal and raw ternary Views

Standard §§3.5.3 and 4.8 give a string literal its complete decoded bytes plus
one trailing NUL, and a ternary an rvalue copy of its selected branch. An embedded
NUL does not shorten that View. Selection evaluates the condition once, then only
the selected branch. Both branches must still satisfy the existing static checks.

Previously, `false ? "z\0w" : "a\0b"` reached integer expression lowering while
materializing its View and failed with `unsupported integer expression`. A raw
multi-byte character-literal branch similarly failed with `raw character literal
cannot be used as an integer`. The semantic analyzer already accepted these
matching, fixed-length Views correctly.

## Materialization boundary

The existing floating ternary byte-copy path now materializes every ternary View.
It allocates a fixed, alignment-one temporary and copies the full selected branch
into it. Literal data, including bytes after the first NUL, is unchanged. Raw,
bytes, cstr and user-template ternaries passed through an integer-shaped by-value
ABI first materialize that View, then load its representation. Numeric integer
expression lowering remains separate and unchanged.

Destination semantics remain a later operation. For example, selecting `"a\0b"`
produces `61 00 62 00`; assigning that selection to a four-byte `cstr` destination
produces `61 00 00 00` under §§7.5 and 11.7. Returning a cstr uses the existing
stored-return contract. Ordinary by-value cstr parameters retain the full source
View bytes. No acceptance matrix, conversion rule, parser or Standard text changes.

## Bounded regression evidence

- HIR checks preserve full static extents, branch templates and non-addressable,
  non-mutable rvalue results for empty/string/raw character literal selections.
- LLVM checks verify valid modules in all three safety modes, two exact-width
  branch-local copies, no branch integer loads/conversions for literal sources,
  complete embedded-NUL globals, and once-only condition/branch call placement.
- Native checks cover both choices, nested choices, calls, raw/default cstr
  declarations and assignments, return contracts, ordinary by-value arguments,
  assignment and multiple-RHS snapshots, direct `as`/`length`, Boolean reads,
  capacities 1/2/4/6, leading/middle/trailing NULs, empty/UTF-8/non-power-of-two/long
  literals and 2,048 checked-temporary iterations. Byte comparisons inspect full
  destinations so a later cstr copy cannot hide padding mistakes.
- Six CLI rejection cases retain length/template matching, unselected-branch
  validation, explicit integer interpretation, and rvalue restrictions. Unit tests
  additionally reject wrong-width `as`.

The positive matrix has 12 variants and the rejection matrix 72: both providers,
three safety modes and O0/O2 on the available host. Native AArch64 and Windows
release selections include both gates; those platforms are not claimed as
executed by the local host run. The new native source fails
on the unchanged base compiler with both reported codegen diagnostics. These are
bounded examples, not proof of all View sizes, lifetimes, ABI targets or behavior
of unrelated operators. Dynamic-length ternaries remain rejected by existing sema.

```sh
./build/hsc_unit_tests Sema_LiteralTernaries LLVMCodegen_LiteralTernaries
ctest --test-dir build -R '^hsc_literal_ternary_' --output-on-failure
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```
