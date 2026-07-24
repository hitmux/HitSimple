# Using `hsc`

`hsc` performs preprocessing, parsing, semantic analysis, HIR lowering, LLVM IR generation, and final linking in sequence. `Standard.md` remains the authority for language syntax and semantics; this document describes only the compiler interface and release-package usage.

## Command Form

```text
hsc [options] <input>...
```

Except for `--help`, `--version`, and `--target-info`, commands require an input file. Unknown options, a missing value for `-o`, a nonexistent output directory, or a nonexistent timing JSON directory produce an error.

## Build an Executable

```bash
hsc examples/hello.hs -o hello
./hello
```

When `-o` is omitted, Linux and macOS generate `a.out` by default, and Windows generates `a.exe`.

The output directory must already exist:

```bash
mkdir -p out
hsc examples/hello.hs -o out/hello
```

### Multiple Translation Units

Each input file is preprocessed, parsed, and lowered to an LLVM module independently, then all modules are linked together:

```bash
hsc app/main.hs app/math.hs app/io.hs -o app/program
```

`extern` declarations must agree across translation units. Macros, typedefs, and file-scope `static` declarations remain local to their translation unit.

## Emit an Object or Static Library

```bash
hsc --emit-object library.hs -o library.o
hsc --crate-type=object library.hs -o library.o
hsc --crate-type=staticlib library.hs -o liblibrary.a
```

`--emit-object` is an alias of `--crate-type=object`; it accepts exactly one
HitSimple input and uses LLVM's target machine to write a native object. Without
`-o`, the input extension is replaced with `.o`.

`--crate-type=staticlib` accepts one or more HitSimple inputs and writes
`libhitsimple.a` by default. It packages the generated objects together with
the HitSimple runtime archive. Archive creation uses `HITSIMPLE_LLVM_AR` when
set, then the archive tool bundled in a full package, then
`llvm-ar-<embedded LLVM major version>`, and finally `llvm-ar` on `PATH`.
External C, C++, and Rust objects or libraries are linked by their parent build
step, not copied into the HitSimple static library.

These output modes do not require a `main` function and cannot be combined
with dump, preprocessing, or `--emit-llvm` actions.

## Optimize and Profile

```bash
hsc -O0 examples/hello.hs -o hello-debug
hsc -O2 examples/hello.hs -o hello
hsc -O3 examples/hello.hs -o hello-fast
hsc -Os examples/hello.hs -o hello-small
```

`-O0`, `-O1`, `-O2`, `-O3`, and `-Os` select the embedded LLVM New Pass
Manager pipeline; the default is `-O2`, and the last optimization option wins.
`hsc` verifies generated IR before optimization, after its empty
`HitSimpleCanonicalize` registration point, and after LLVM's default pipeline.
The final Clang invocation receives optimized HitSimple IR in backend-only
`-O0` mode, so it does not select a second IR optimization pipeline. Native C
and C++ sources supplied with `--c-source` or `--cxx-source` still receive the
requested `-O*` level.

Use `--optimization-remarks=<path>` to write opt-in pipeline remarks. The
first release emits a `HitSimpleCanonicalize` boundary remark; it is an
execution proof, not a promise of a HitSimple-specific optimization pass.

```bash
hsc -O2 --optimization-remarks=build/optimization-remarks.txt \
  examples/hello.hs -o hello
```

`--emit-llvm` remains a frontend inspection action and always writes
unoptimized LLVM IR, regardless of `-O*`. The VS Code Debug Current File
workflow continues to force `-g -O0`.

Instrumentation PGO uses the same embedded pipeline and does not require an
external `opt` executable:

```bash
hsc -O2 --pgo-instrument=program.profraw app.hs -o app-instrumented
./app-instrumented
llvm-profdata merge -sparse program.profraw -o program.profdata
hsc -O2 --pgo-use=program.profdata app.hs -o app-pgo
```

PGO options are executable-build options and are mutually exclusive.

## Sanitizer Test Builds (Linux and macOS)

```bash
hsc --sanitize=address tests/cases/run/checked_oob.hs -o asan-oob
./asan-oob
```

`--sanitize=address` and `--sanitize=undefined` are Linux and macOS test modes
for native executable builds. AddressSanitizer is added by HitSimple's embedded
LLVM pass pipeline to generated IR, then the runtime sources are relinked with
the selected sanitizer. UndefinedBehaviorSanitizer currently instruments the
runtime and any native C/C++ inputs, because Clang does not add UBSan checks
when consuming pre-existing LLVM IR. `undefined` makes UBSan reports terminate
the executable. The option cannot be used with `--emit-llvm`, preprocessing,
object output, or static-library output. Release packages include the runtime
sources required for this test-only relink path; ordinary builds continue to
link the runtime archive.

## Select a Standard-Library Provider

```bash
hsc --stdlib-provider=optimized examples/hello.hs -o hello
hsc --stdlib-provider=reference examples/hello.hs -o hello-reference
```

`optimized` is the default selection. `reference` chooses the reference
provider declared by `StandardLibraryManifest.json` when an API has one; APIs
without a reference provider keep their default implementation. Selection
happens at compile time and does not change the public View contract or the
selected safety mode. The reference selection is primarily useful for
contract and artifact-difference validation.

## C ABI Interoperability

Use `extern "C"` to select the stable C ABI explicitly:

```hs
extern "C" native_add(value as i32) -> i32

extern "C" func hsc_increment(value as i32) -> i32 {
    return value %d+ 1
}
```

Imports and exports retain the source identifier as an unmangled C symbol.
The supported parameter and single-return templates are `bool`, `i8`–`i64`,
`u8`–`u64`, `f32`, `f64`, `addr`, `cstr`, and `handle`; `()` denotes no return.
`addr`, `cstr`, and `handle` map to C pointers. Structure values, user
templates, Views, `bytes`, `f16`, `f128`, multiple returns, and varargs are
rejected at the boundary. `throw` and `try`/`catch` are forbidden in a C ABI
export.

Compile C++ through an `extern "C"` wrapper. Rust must use an unmangled
`pub extern "C"` export and should build its library as `staticlib`. C++
exceptions, Rust panics, and HitSimple exceptions must be handled on their
own side of the ABI boundary.

## Build with Native C or C++

Position arguments always name HitSimple sources. Add native inputs explicitly:

```bash
# A HitSimple main calls a C function.
hsc app.hs --c-source native/helper.c -o app

# A HitSimple main calls a C++ extern "C" wrapper.
hsc app.hs --cxx-source native/wrapper.cpp -o app

# A native main calls an exported HitSimple function.
hsc library.hs --entry=native --cxx-source native/main.cpp -o app
```

`--c-source` and `--cxx-source` may be repeated. Each source is compiled to a
temporary object with Clang or Clang++, then `hsc` performs one final link with
the HitSimple objects and runtime. `--entry=hsc` is the default and requires
exactly one HitSimple `main`. `--entry=native` forbids a HitSimple `main` and
requires a C or C++ source to provide it.

Use `--link-input <path>` for an object, archive, or shared library; repeat
`-L <dir>`, `-l <name>`, and `--link-arg <arg>` as needed. When linking only
archive/object inputs, select the driver explicitly with
`--linker-language=c|cxx`. A C++ source selects the C++ driver by default.
`--clangxx <path>` overrides that driver; its lookup otherwise uses
`HITSIMPLE_CLANGXX`, `clang++-18`, then `clang++` on `PATH`.

The native-source and linker options are executable-build options. They cannot
be combined with dump/preprocessing/IR actions, `--emit-object`, or
`--crate-type=staticlib`. The supported mixed-build scope is Linux x86_64 and
AArch64. C++ names, class ABI, and exception propagation are outside the
contract; use an `extern "C"` wrapper.

## Build with a Cargo Static Library

`hsc` can build and link one Cargo library that declares `staticlib`:

```toml
[lib]
crate-type = ["staticlib"]
```

```bash
hsc app.hs \
  --cargo-manifest rust/Cargo.toml \
  --cargo-profile release \
  --cargo-features ffi \
  -o app
```

Use `--cargo-package <name>` for a workspace package; it is mandatory when
the manifest is a virtual workspace. `--cargo-features <list>` passes Cargo's
comma- or space-separated feature list, and `--cargo-no-default-features`
forwards Cargo's corresponding switch. Cargo is resolved through
`HITSIMPLE_CARGO` or `cargo` on `PATH`.

The compiler requests Cargo JSON messages and uses the reported static archive
instead of guessing a `target/` path. Supported Cargo native search paths and
`static`/`dylib` library requirements are forwarded to the final link; other
native-link forms fail explicitly. Cargo build diagnostics are retained and
forwarded. Rust entry points stay under Cargo: to call HitSimple from Rust,
first create a HitSimple `staticlib`, then link it from Cargo through a build
script or equivalent native-link configuration.

## Select a Safety Mode

| Option | Behavior |
| --- | --- |
| `--unchecked` | Does not insert safety checks. This is the default mode. |
| `--static-checked` | Reports statically provable issues without inserting runtime checks. |
| `--checked` | Performs static checks and inserts runtime checks for supported dynamic errors. Checked executables embed the normalized source location for each generated runtime check. |

```bash
hsc --checked examples/hello.hs -o hello-checked
hsc --static-checked examples/hello.hs -o hello-static
```

When a checked runtime check fails, `hsc` preserves the existing error reason
and appends its originating source location when one is available:

```text
hitsimple runtime error: invalid memcpy destination range at path/to/file.hs:8:26
```

The location context is thread-local for native interop. It is emitted only by
`--checked`; `--unchecked` and `--static-checked` do not contain runtime
source-location calls. Checked artifacts therefore contain normalized source
paths plus line and column data. Treat source paths as build metadata when
distributing binaries. A runtime call made without compiler-provided context
falls back to the original `hitsimple runtime error: <message>` form.

Use `--target-info` to inspect the actual coverage. `Standard.md` is the authority for the normative contract.

## Select a Clang Toolchain

When producing an executable, `hsc` searches for Clang in this order:

1. The `--clang <path>` command-line option.
2. The `HITSIMPLE_CLANG` environment variable.
3. `toolchain/bin/clang++.exe` in the Windows full package.
4. `clang-<embedded LLVM major version>`.
5. `clang` and `clang++` on PATH.

Specify it explicitly:

```bash
hsc --clang /opt/llvm/bin/clang++ examples/hello.hs -o hello
```

Use an environment variable:

```bash
export HITSIMPLE_CLANG=/opt/llvm/bin/clang++
hsc examples/hello.hs -o hello
```

On Windows PowerShell:

```powershell
$env:HITSIMPLE_CLANG = 'C:\llvm-mingw\bin\clang++.exe'
.\bin\hsc.exe examples\hello.hs -o hello.exe
```

The selected backend Clang major version must match the LLVM major version
embedded in `hsc`; otherwise compilation fails before native code generation.
`--emit-llvm` does not link, so it does not require Clang, even when the
supplied `--clang` path is invalid.

Mixed builds resolve Clang++ independently through `--clangxx`,
`HITSIMPLE_CLANGXX`, `clang++-<embedded LLVM major version>`, and `clang++`
on `PATH`; its major version is checked as well.

`HITSIMPLE_RUNTIME_SOURCE` can still override the static runtime during development and debugging. Normal installations and release packages link `lib/hitsimple/libhitsimple_runtime.a` by default.

## Inspect the Compiler and Target

```bash
hsc --version
hsc --help
hsc --target-info
```

`--target-info` reports:

- The actual target triple.
- The Clang path and how it was resolved.
- The runtime kind and path.
- The `f128` backend.
- ABI, safety-mode, and standard-library implementation boundaries.

The Windows target triple is fixed as `x86_64-w64-windows-gnu`; macOS uses the current native Darwin target. This release generates programs only for the current platform and provides no arbitrary `--target` cross-compilation interface.

## Inspect Intermediate Representations

Each of the following actions accepts exactly one input file and cannot be combined with the others:

```bash
hsc --dump-tokens examples/hello.hs
hsc --dump-ast examples/hello.hs
hsc --dump-hir examples/hello.hs
```

These outputs are written to standard output and do not support `-o`.

## Generate LLVM IR

```bash
hsc --emit-llvm examples/hello.hs
hsc --emit-llvm examples/hello.hs -o hello.ll
hsc --checked --emit-llvm examples/hello.hs -o hello-checked.ll
```

`--emit-llvm` accepts exactly one input file per invocation.

## Debug Information

`-g` emits native debug information for executable, object, and static-library
builds, as well as `--emit-llvm`. Linux and macOS targets emit DWARF v5; Windows
targets emit CodeView metadata. It is rejected for token, AST, HIR, target, and
preprocessing actions. Without `-g`, generated LLVM IR and native outputs contain
no compiler debug metadata.

```bash
hsc -g examples/hello.hs -o hello-debug
gdb ./hello-debug

hsc -g --emit-llvm examples/hello.hs -o hello-debug.ll
```

Local `new` declarations and parameters are exposed as fixed-length unsigned
byte arrays that point at their actual storage. This preserves HitSimple's
memory-and-View semantics; templates, `as` Views, and byte interpretation are
not represented as C-like static types in the debugger.

The VS Code extension's Debug Current File command rebuilds the active `.hs`
entry with `-g -O0` and uses the Microsoft C/C++ extension
(`ms-vscode.cpptools`). Linux x86_64/aarch64 uses `cppdbg` with GDB; macOS
arm64/x86_64 uses `cppdbg` with LLDB; Windows x64 uses `cppvsdbg` with
CodeView/PDB. `hitsimple.gdbPath` remains the GDB setting and
`hitsimple.lldbPath` selects an MI-compatible LLDB executable, defaulting to
`lldb`; with that default, macOS uses cpptools' bundled signed `lldb-mi` when
available. A custom `hitsimple.lldbPath` must point to an executable
MI-compatible LLDB.
Windows does not require a debugger-path setting: debug builds remove any old
PDB and fail unless the compiler creates a same-base-name `.pdb` next to the
executable. The command uses the workspace root as its working directory and
does not create `launch.json` or override F5. Cross-target debugging, remote
debugging, and custom debug adapter configurations are outside this interface
contract.

## Compilation Timing

`--timing` writes a human-readable summary to standard error. It never changes
LLVM IR, AST, HIR, token, or preprocessing output written to standard output.

```bash
hsc --timing examples/hello.hs -o hello
hsc --timing --emit-llvm examples/hello.hs > hello.ll
```

`--timing-json=<path>` writes the same compilation lifecycle as an atomic JSON
replacement. It can be used with or without `--timing`:

```bash
hsc --timing-json=build/timing.json examples/hello.hs -o build/hello
```

The parent directory must already exist, and the timing JSON path cannot equal
the executable or IR output path, including the default `a.out`/`a.exe` path.
Successful and failed compilations both write a record. The current format is
`schema_version: 1` and contains:

- `outcome`, `failed_stage`, `total_duration_ns`, and `translation_unit_count`.
- One entry per translation unit with `preprocess`, `parse`,
  `c_compat_lowering`, `sema_hir`, and `llvm_emission` stage states and
  nanosecond durations.
- HIR node counts and emitted LLVM IR byte counts for each translation unit.
- Global `llvm_ir_write` and `clang_backend_link` stage states and durations.

Each stage state is `not_started`, `skipped`, `completed`, or `failed`.

## Diagnostics

By default, compiler diagnostics use one stable human-readable primary line:

```text
hsc: file:line:column: stage: severity: message
```

When a declaration conflicts with an earlier local, global, function, template,
or label declaration, the primary error points to the redefinition and a
following `note` points to the first declaration. File-level errors such as a
missing `main` or an incompatible C external ABI declaration point to line 1,
column 1 of the relevant input file. CLI errors without an input remain
unlocated.

Use `--diagnostic-format=json` for CI or other direct CLI consumers. It writes
newline-delimited JSON objects to standard error, one compiler diagnostic per
line. Each object has `severity`, `stage`, `message`, `primary` (`null` or a
range with `begin` and `end`), and `related` labels. It does not change the
separate `--timing-json=<path>` file:

```bash
hsc --diagnostic-format=json --emit-llvm broken.hs
```

The VS Code `$hsc` Problem Matcher consumes the default human format, including
file-level diagnostics. Do not put `--diagnostic-format=json` in
`hitsimple.additionalArgs` for Build, Run, or Debug Current File tasks.

## Preprocess Only

```bash
hsc --preprocess-only examples/hello.hs
hsc -E examples/hello.hs -o hello.preprocessed.hs
```

This mode is useful for diagnosing `$include`, macro expansion, and conditional preprocessing. It does not enter the parser, so `--c-compat` does not change the preprocessing result.

## C Compatibility

```bash
hsc --c-compat path/to/program.c -o program
hsc --c-compat app/main.c app/support.c -o app/program
```

`--c-compat` can be used with `--dump-ast`, `--dump-hir`, and `--emit-llvm`; it cannot be used with `--dump-tokens` or `--target-info`.

This mode translates a restricted C syntax subset into the core AST. It explicitly rejects multidimensional arrays, complex declarators, function pointers, pointers to arrays, C varargs, and GNU extensions. C struct pass-by-value ABI covers only supported x86_64 SysV ELF layouts; Windows and Darwin do not support that ABI scope.

## Action Constraints

| Action | Input count | Supports `-o` | Supports `--c-compat` | Supports `-g` |
| --- | --- | --- | --- | --- |
| Default executable build | One or more | Yes | Yes | Yes |
| `--emit-object` / `--crate-type=object` | One | Yes | Yes | Yes |
| `--crate-type=staticlib` | One or more | Yes | Yes | Yes |
| `--dump-tokens` | One | No | No | No |
| `--dump-ast` | One | No | Yes | No |
| `--dump-hir` | One | No | Yes | No |
| `--emit-llvm` | One | Yes | Yes | Yes |
| `--preprocess-only` / `-E` | One | Yes | No effect | No |
| `--target-info` | None | No | No | No |

## Linux DEB

```bash
sudo apt install ./hitsimple_0.3.6_amd64.deb
```

On arm64, use:

```bash
sudo apt install ./hitsimple_0.3.6_arm64.deb
```

The DEB depends exactly on `clang-18`, which matches its embedded LLVM 18; Debian 13's default Clang 19 is not compatible. Ubuntu 22.04 and Debian 12 need a Clang 18 source configured first. The following commands configure apt.llvm.org for the appropriate distribution codename; verify the current [apt.llvm.org](https://apt.llvm.org/) instructions and signing method before running them.

Ubuntu 22.04:

```bash
sudo install -d -m 0755 /etc/apt/keyrings
curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | \
  sudo tee /etc/apt/keyrings/apt.llvm.org.asc >/dev/null
echo 'deb [signed-by=/etc/apt/keyrings/apt.llvm.org.asc] https://apt.llvm.org/jammy/ llvm-toolchain-jammy-18 main' | \
  sudo tee /etc/apt/sources.list.d/llvm-18.list
sudo apt update
sudo apt install clang-18
```

On Debian 12, replace both occurrences of `jammy` above with `bookworm`. The DEB itself does not add or modify package sources.

After installation, verify it with:

```bash
hsc --version
hsc --target-info
hsc examples/hello.hs -o /tmp/hitsimple-hello
/tmp/hitsimple-hello
```

## Fedora 44 / EL9 RPM

RPM packages provide x86_64/aarch64 baselines only for Fedora 44 and EL9. The distributions use distinct release suffixes to avoid claims about a cross-distribution glibc baseline:

```bash
sudo dnf install ./hitsimple-0.3.6-1.fc44.x86_64.rpm
sudo dnf install ./hitsimple-0.3.6-1.el9.aarch64.rpm
```

The packages depend on `clang >= 18`. They do not bundle Clang, add package sources, or provide GPG signatures. After installation, use the same `hsc --version`, `hsc --target-info`, and hello-program checks as for DEB packages.

## macOS tar.gz

Choose the archive matching the machine:

```bash
tar -xzf hitsimple-0.3.6-macos-arm64.tar.gz
cd hitsimple-0.3.6-macos-arm64
bin/hsc --clang /opt/homebrew/opt/llvm@18/bin/clang path/to/hello.hs -o hello
./hello
```

Use `hitsimple-0.3.6-macos-x86_64.tar.gz` on Intel Macs. The extracted directory can be moved; `hsc` discovers the preprocessor, standard library, and static runtime relative to itself. macOS requires an external Clang toolchain matching the embedded LLVM major version (Clang 18 for this release). The package is unsigned and not notarized, and no PKG is provided.

Like Windows, macOS uses the software binary128 backend for `f128`, covering literals, arithmetic, comparisons, numeric conversion, formatting, scanning, and the floating-point entry points in `math.hsh`. Linux continues to use the native `fp128`/glibc backend.

## Windows Full/Slim ZIP

The full package runs directly after extraction:

```powershell
Expand-Archive .\hitsimple-0.3.6-windows-x86_64-full.zip
cd .\hitsimple-0.3.6-windows-x86_64-full
.\bin\hsc.exe path\to\hello.hs -o hello.exe
.\hello.exe
```

The slim package requires a compatible llvm-mingw/Clang 18 through `--clang`, `HITSIMPLE_CLANG`, or PATH. Paths may contain spaces and Unicode characters; arguments are passed through argv rather than shell quoting.

Windows uses the software binary128 backend for `f128`, covering literals, arithmetic, comparisons, numeric conversion, formatting, scanning, and the floating-point entry points in `math.hsh`. Use `--target-info` to inspect the precision and exception-flag boundaries for transcendental functions.

## Verify Release Assets

Official releases include `SHA256SUMS`. After downloading every asset, run on Linux:

```bash
sha256sum -c SHA256SUMS
```

Only when every hash verification passes can the downloaded contents be considered identical to the release assets.
