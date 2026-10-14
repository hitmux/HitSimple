# Six runnable practical examples

These examples use capabilities already in HitSimple. The linked source files
are also the compiler's regression fixtures: there is one source and one
expected-result oracle, rather than a tutorial copy that can drift.

## Prerequisites and entry point

Run from the repository root on **native Linux x86_64 or AArch64**. The mixed
C/C++ and Cargo commands below have that scope; they are not cross-compilation
recipes. See [build requirements](../../README.md#build-from-source) and
[CLI/toolchain details](../../USAGE.md).

- Python 3.10 or later.
- A built compiler at `build/hsc`, or pass `--hsc /absolute/path/to/hsc`.
- A Clang toolchain matching the compiler's embedded LLVM major version.
  C++ also needs Clang++ and the native C++ standard library.
- For Rust: Cargo and rustc on PATH. The supplied crate has no external
  dependencies. `--cargo-features bonus` is required by its expected result.
- For VS Code: Node.js/npm, GDB, a graphical session (or `xvfb-run` on a
  headless host), and `npm ci --prefix vscode/hitsimple`. The automated suite
  downloads VS Code and installs Microsoft C/C++ in an isolated temporary
  extension directory, so it also requires network access.

Build once:

```sh
cmake -S . -B build
cmake --build build --parallel
```

Run one example with `python3 examples/practical/run.py NAME --hsc build/hsc`.
The runner exits 0 only after all expectations pass; missing prerequisites and
failed checks are errors, never successful skips. For the first five (CLI)
examples, compiler commands, stdout, stderr and exit codes are preserved under
`build/practical-examples/`; override that location with `--artifacts PATH`.
The VS Code suite writes progress to the terminal and removes its temporary
workspace afterward; `--artifacts` does not apply to it. Native helpers and the Rust crate are
compiled from the checkout; retain the full repository when using this guide.

## Index

| Name | What you learn | Canonical source |
| --- | --- | --- |
| `c-import` | Call a C function with an explicit C ABI declaration | [HitSimple](../../tests/cases/interop/c_abi_import.hs), [C](../../tests/cases/interop/c_abi_import_helper.c) |
| `cpp-wrapper` | Put C++ implementation behind an `extern "C"` wrapper | [HitSimple](../../tests/cases/interop/hsc_calls_cpp.hs), [C++](../../tests/cases/interop/cpp_wrapper.cpp) |
| `rust-staticlib` | Build and link a Cargo static library with a required feature | [HitSimple](../../tests/cases/interop/hsc_calls_rust.hs), [Cargo](../../tests/cases/interop/rust_staticlib/Cargo.toml), [Rust](../../tests/cases/interop/rust_staticlib/src/lib.rs) |
| `checked-error` | Recognize an expected checked runtime failure | [HitSimple](../../tests/cases/run/checked_oob.hs) |
| `ctype-providers` | Verify optimized/reference providers against the same ASCII contract | [HitSimple](../../tests/cases/run/ctype_reference.hs) |
| `vscode-debug` | Stop at source, inspect stack and local bytes | [HitSimple](../../tests/cases/run/debug_info.hs) |

## 1. Import C

```sh
python3 examples/practical/run.py c-import --hsc build/hsc
```

`extern "C" native_add(left as i32, right as i32) -> i32` matches the C
helper's two native `int` inputs and return value on these supported targets.
`--c-source` asks hsc to compile and link that helper. The program checks
`native_add(19, 23) == 42`; it exits 0 with empty stdout/stderr.
The entry command finishes with `PASS c-import`.

## 2. Wrap C++

```sh
python3 examples/practical/run.py cpp-wrapper --hsc build/hsc
```

The C++ implementation uses `std::string`/`std::stoi` internally but exposes
only `extern "C" int cpp_add_suffix(int)`. HitSimple calls that C ABI wrapper,
not a C++ class or mangled symbol. `--cxx-source` selects the C++ compile/link
path. The program checks `cpp_add_suffix(18) == 42`, exits 0 and produces no
stdout/stderr. The entry command finishes with `PASS cpp-wrapper`.

## 3. Call a Rust static library

```sh
python3 examples/practical/run.py rust-staticlib --hsc build/hsc
```

The manifest declares `crate-type = ["staticlib"]`; Rust exports
`#[no_mangle] pub extern "C" fn rust_add(value: i32) -> i32`. The entry command
passes `--cargo-manifest`, `--cargo-profile release`, and
`--cargo-features bonus`. With `bonus`, `rust_add(18)` is 42. Without it, the
result is 19 and this example's assertion fails. Neither the Rust ABI nor a
Rust `main` crosses this boundary.

Cargo may print build progress. The executable itself exits 0 with empty
stdout/stderr; the entry command finishes with `PASS rust-staticlib`.

## 4. Diagnose a checked bounds error

```sh
python3 examples/practical/run.py checked-error --hsc build/hsc
```

The program allocates four bytes, then intentionally tries to store one byte
at offset 4. Valid offsets are 0–3. `--checked` detects the invalid store.
Compilation succeeds, but the executable must exit **120**, with no stdout and
this stderr (the source prefix is an absolute path):

```text
hitsimple runtime error: invalid checked store at /.../tests/cases/run/checked_oob.hs:10:5
```

The runner checks the exact exit status, error class and source location using
the existing [checked-runtime oracle](../../tests/support/harness/manifests/checked-differential.json).
It then finishes with `PASS checked-error` and exits 0: the failure was expected.
Do not run this invalid program unchecked or static-checked; the entry command
never does so. A crash or any other nonzero exit is not a passing result.

## 5. Compare ctype providers

```sh
python3 examples/practical/run.py ctype-providers --hsc build/hsc
```

The same source checks ASCII digit/letter/alphanumeric/space boundaries, case
conversion, NUL, DEL and a non-ASCII byte. It is compiled with each of
`--stdlib-provider=optimized` and `--stdlib-provider=reference` in unchecked,
static-checked and checked modes. All six executions must produce the same
observable result: exit 0, empty stdout, empty stderr. Nonzero exit codes
1–16 identify failed assertion groups in the source.

Expect `6/6 PASS` followed by `PASS ctype-providers`. This is a correctness
comparison, not a timing benchmark. Provider selection is compile-time and
does not change the public View contract or safety mode.

## 6. Debug the current file in VS Code

For an interactive session, install the HitSimple and Microsoft C/C++
extensions, open this repository as a trusted workspace, and set
`hitsimple.compilerPath` to your compiler. GDB must be executable through
`hitsimple.gdbPath`. If another extension claims `.hs`, select the HitSimple
language mode or associate `*.hs` with `hitsimple` in workspace settings.

1. Open [debug_info.hs](../../tests/cases/run/debug_info.hs).
2. Set a breakpoint on line 6, the `printf` after `value %d= 41`.
3. Run **HitSimple: Debug Current File** from the Command Palette
   (`hitsimple.debugCurrentFile`). It rebuilds this file with `-g -O0`.
4. At the breakpoint, expect `helper` above `main` in Call Stack. `value` is a
   four-byte local, displayed as bytes/an array by the native debugger, not
   necessarily a scalar integer. On the supported little-endian targets,
   its initialized bytes represent 41 (`0x29, 0, 0, 0`).
5. Continue: the program prints `41` and exits 0.

The command neither creates `launch.json` nor binds F5. See the
[extension setup and scope](../../README.md#vs-code-extension).

One-command automated verification (after the prerequisites above):

```sh
python3 examples/practical/run.py vscode-debug --hsc build/hsc
```

On a headless Linux host, use:

```sh
xvfb-run -a python3 examples/practical/run.py vscode-debug --hsc build/hsc
```

This invokes the existing real Extension Host suite. It copies the same source
into an isolated workspace, calls `hitsimple.debugCurrentFile`, and checks the
source breakpoint, `helper`/`main` frames and `value` local through the native
debug adapter, along with the existing Build/Run/Problems checks. Expect
`HitSimple Extension Host assertions passed` and `PASS vscode-debug`.

## Run the collection and keep it verified

```sh
# The first five examples, including six ctype provider/mode combinations:
python3 examples/practical/run.py cli --hsc build/hsc

# All six, after installing the VS Code test prerequisites:
xvfb-run -a python3 examples/practical/run.py all --hsc build/hsc
```

CTest registers the same CLI entry command as `hsc_practical_examples` on
native Linux x86_64/AArch64. Release CI runs it on both architectures and uses
the same `vscode-debug` entry in both Linux Extension Host jobs. No example
changes compiler semantics or introduces another test harness.
