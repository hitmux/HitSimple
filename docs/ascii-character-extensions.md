# Optional ASCII character predicates

These eight implementation extensions are available from `<ctype.hsh>`.
They are **not required APIs in HitSimple Standard 1.0.0-Beta.21** and do not
increase Standard conformance coverage. Standard §14.7 still specifies only
`is_digit`, `is_alpha`, `is_alnum`, `is_space`, `to_upper`, and `to_lower`.

Each extension accepts one supported, nonempty View, evaluates its argument
exactly once, and observes only the byte at the beginning of its storage.
On supported little-endian targets this is the low byte. It does not convert
floating-point values to integers or interpret UTF-8 code points. It returns
a one-byte `bool` View containing exactly `0x00` (false) or `0x01` (true).

The ranges below are inclusive and use fixed ASCII byte values. They do not
depend on the host locale. Every predicate returns false for bytes 128–255.

| Function | True for |
| --- | --- |
| `is_lower(ch as view) -> bool` | 97–122 (`a`–`z`) |
| `is_upper(ch as view) -> bool` | 65–90 (`A`–`Z`) |
| `is_xdigit(ch as view) -> bool` | 48–57, 65–70, 97–102 (`0`–`9`, `A`–`F`, `a`–`f`) |
| `is_blank(ch as view) -> bool` | 9 (horizontal tab) or 32 (space) |
| `is_control(ch as view) -> bool` | 0–31 or 127 (DEL) |
| `is_print(ch as view) -> bool` | 32–126 (includes space) |
| `is_graph(ch as view) -> bool` | 33–126 (excludes space) |
| `is_punct(ch as view) -> bool` | 33–47, 58–64, 91–96, 123–126 (graphical non-alphanumeric ASCII) |

For example, `is_lower(0x161)` and `is_lower(-159)` are both true because
the low byte is 97. `is_control(0)` is true. `is_blank(10)` and
`is_print(127)` are false. An `f32` whose first stored byte is `0x61` is
classified exactly like byte 97, irrespective of its numerical value.

```hs
$include <ctype.hsh>

func main() -> i32 {
    if (is_xdigit('F') && is_punct('!') && !is_blank('\n')) {
        return 0
    }
    return 1
}
```

## Providers and generated metadata

Both `--stdlib-provider=optimized` and `--stdlib-provider=reference` expose
the same contract in all three safety modes. The optimized provider uses
explicit LLVM ASCII comparisons and canonical bool-byte materialization.
The reference provider reuses `stdlib/ctype.hs`; the compiler passes its
single raw input byte, zero-extended to the module's private `i32` ABI.
There is no additional module, locale state, or libc character predicate.
Existing language restrictions still apply: opaque `handle` arguments cannot
be passed to generic View parameters.

The version-1 manifest remains compatible with existing Standard entries.
An extension has an empty `standardSection` and a nonempty
`extensionDocument` pointing to this file; the generator rejects conflicting
or missing attribution and missing documentation. Generated header comments
identify extensions. The evidence inventory keeps the 75 Standard APIs in
`apis` and these eight optional APIs separately in `extensions`.

## Verification

`ctype_ascii_extensions.hs` checks all 256 bytes against an explicit mask
oracle verified independently by `test_ascii_ctype.py`. It covers both zero
and nonzero high storage bytes, all eight integer widths/signedness choices,
negative and wide integers, all four floating widths, raw bool bytes,
addresses, raw Views of 1/3/32 bytes, `none`, `cstr`, and a packed
user-template View. Argument/return copies, side-effectful argument calls,
ternaries, assignments, runtime-sized resized Views, slices, and dereference
Views are covered. Every predicate is tested for canonical bool bytes and
integer consumers. Failures cannot wrap to a successful exit status.

The harness runs both providers × three safety modes × O0/O2 (12 variants),
requiring exit zero and empty stdout/stderr. LLVM unit tests separately
verify one-byte loads without numeric floating conversions and canonical
`i1`-to-`i8` extensions. Metadata/generator, evidence, editor syntax, and
installed-compiler tests cover the exposed API surface.

```sh
ctest --test-dir build -R 'hsc_(ctype_|ascii_ctype_|installed_ascii_)' --output-on-failure
python3 tools/standard_evidence.py --check
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```
