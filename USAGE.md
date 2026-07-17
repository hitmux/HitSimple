# Using `hsc`

`hsc` performs preprocessing, parsing, semantic analysis, HIR lowering, LLVM IR generation, and final linking in sequence. `Standard.md` remains the authority for language syntax and semantics; this document describes only the compiler interface and release-package usage.

## Command Form

```text
hsc [options] <input>...
```

Except for `--help`, `--version`, and `--target-info`, commands require an input file. Unknown options, a missing value for `-o`, or a nonexistent output directory produce an error.

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

## Select a Safety Mode

| Option | Behavior |
| --- | --- |
| `--unchecked` | Does not insert safety checks. This is the default mode. |
| `--static-checked` | Reports statically provable issues without inserting runtime checks. |
| `--checked` | Performs static checks and inserts runtime checks for supported dynamic errors. |

```bash
hsc --checked examples/hello.hs -o hello-checked
hsc --static-checked examples/hello.hs -o hello-static
```

Use `--target-info` to inspect the actual coverage. `Standard.md` is the authority for the normative contract.

## Select a Clang Toolchain

When producing an executable, `hsc` searches for Clang in this order:

1. The `--clang <path>` command-line option.
2. The `HITSIMPLE_CLANG` environment variable.
3. `toolchain/bin/clang++.exe` in the Windows full package.
4. `clang-18`.
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

If no compatible toolchain is found, `hsc` returns a clear error before linking. `--emit-llvm` does not link, so it does not require Clang, even when the supplied `--clang` path is invalid.

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

Use `-g` to emit DWARF debug metadata for an executable build or LLVM IR:

```bash
hsc -g examples/hello.hs -o hello-debug
gdb ./hello-debug

hsc -g --emit-llvm examples/hello.hs -o hello-debug.ll
```

`-g` is rejected for token, AST, HIR, preprocessing, and target-information actions. It is an input to the compiler and linker; it does not configure a debugger or create a `launch.json` file.

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

| Action | Input count | Supports `-o` | Supports `--c-compat` |
| --- | --- | --- | --- |
| Default executable build | One or more | Yes | Yes |
| `--dump-tokens` | One | No | No |
| `--dump-ast` | One | No | Yes |
| `--dump-hir` | One | No | Yes |
| `--emit-llvm` | One | Yes | Yes |
| `--preprocess-only` / `-E` | One | Yes | No effect |
| `--target-info` | None | No | No |

## Linux DEB

```bash
sudo apt install ./hitsimple_0.2.2_amd64.deb
```

On arm64, use:

```bash
sudo apt install ./hitsimple_0.2.2_arm64.deb
```

Ubuntu 22.04 and Debian 12 need Clang 18 installed first. The following commands configure apt.llvm.org for the appropriate distribution codename; verify the current [apt.llvm.org](https://apt.llvm.org/) instructions and signing method before running them.

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
sudo dnf install ./hitsimple-0.2.2-1.fc44.x86_64.rpm
sudo dnf install ./hitsimple-0.2.2-1.el9.aarch64.rpm
```

The packages depend on `clang >= 18`. They do not bundle Clang, add package sources, or provide GPG signatures. After installation, use the same `hsc --version`, `hsc --target-info`, and hello-program checks as for DEB packages.

## macOS tar.gz

Choose the archive matching the machine:

```bash
tar -xzf hitsimple-0.2.2-macos-arm64.tar.gz
cd hitsimple-0.2.2-macos-arm64
bin/hsc --clang /opt/homebrew/opt/llvm@18/bin/clang path/to/hello.hs -o hello
./hello
```

Use `hitsimple-0.2.2-macos-x86_64.tar.gz` on Intel Macs. The extracted directory can be moved; `hsc` discovers the preprocessor, standard library, and static runtime relative to itself. macOS requires an external Clang 18 or later. The package is unsigned and not notarized, and no PKG is provided.

Like Windows, macOS uses the software binary128 backend for `f128`, covering literals, arithmetic, comparisons, numeric conversion, formatting, scanning, and the floating-point entry points in `math.hsh`. Linux continues to use the native `fp128`/glibc backend.

## Windows Full/Slim ZIP

The full package runs directly after extraction:

```powershell
Expand-Archive .\hitsimple-0.2.2-windows-x86_64-full.zip
cd .\hitsimple-0.2.2-windows-x86_64-full
.\bin\hsc.exe path\to\hello.hs -o hello.exe
.\hello.exe
```

The slim package requires a compatible llvm-mingw/Clang 18 through `--clang`, `HITSIMPLE_CLANG`, or PATH. Paths may contain spaces and Unicode characters; arguments are passed through argv rather than shell quoting.

Windows uses the software binary128 backend for `f128`, covering literals, arithmetic, comparisons, numeric conversion, formatting, scanning, and the floating-point entry points in `math.hsh`. Use `--target-info` to inspect the precision and exception-flag boundaries for transcendental functions.

## VS Code Extension

The extension provides Build, Run, and Debug Current File commands. Debug Current File rebuilds the active `.hs` program with one `-g` flag and launches it through the Microsoft C/C++ extension's `cppdbg` debugger. It does not create `launch.json` or override F5.

| Setting | Default | Purpose |
| --- | --- | --- |
| `hitsimple.gdbPath` | `gdb` | GDB executable name or path used by Debug Current File. |
| `hitsimple.debugArguments` | `[]` | Program arguments passed as separate argv entries when debugging. |

Debug Current File supports Linux x86_64 only. It requires an executable GDB and the Microsoft C/C++ extension (`ms-vscode.cpptools`), which contributes `cppdbg`. macOS, Windows, LLDB, custom debug adapters, remote debugging, and custom `launch.json` configurations are outside this command's scope.

## Verify Release Assets

Official releases include `SHA256SUMS`. After downloading every asset, run on Linux:

```bash
sha256sum -c SHA256SUMS
```

Only when every hash verification passes can the downloaded contents be considered identical to the release assets.
