# HitSimple

HitSimple is an experimental programming language and a C++20 compiler. Its core model is "data resides in memory, and meaning comes from explicit interpretation": the same bytes can be interpreted as integers, floating-point values, strings, addresses, or structured layouts, while storage remains separate from interpretation templates.

The compiler pipeline is operational end to end:

```text
source -> preprocessor -> lexer -> parser -> AST -> sema -> HIR -> LLVM IR -> executable
```

`Standard.md` is the authority for language semantics. The current tests validate implemented behavior; they do not replace a clause-by-clause review of the Standard.

## Current Status

`v0.2.1` is a Beta prerelease with release builds for:

- Linux x86_64/aarch64 tar.gz archives.
- Linux amd64/arm64 DEB packages.
- macOS arm64/x86_64 tar.gz archives.
- Fedora 44 and EL9 x86_64/aarch64 RPM packages.
- Windows x64 full/slim ZIP archives.
- A VS Code extension VSIX.

Windows builds programs only for the current Windows x64 platform, with the target ABI fixed as `x86_64-w64-windows-gnu`. macOS uses the current native Darwin target. The release scope excludes other UNIX platforms and does not provide cross-target `hsc --target` support.

On Windows and macOS, `f128` uses Boost 1.85.0 `cpp_bin_float` as a 113-bit software backend and passes raw IEEE 754 binary128 bit patterns across runtime boundaries. Decimal conversion and binary128 encoding explicitly use `roundTiesToEven`; preservation and propagation of NaN payloads remain implementation-defined. Linux continues to use native LLVM `fp128` and glibc interfaces.

## Build from Source

Requirements:

- CMake 3.24 or later.
- A C++20 compiler.
- LLVM development packages.
- Bison.
- re2c.
- Clang.

macOS builds also require Boost 1.85.0 or later. Release packages still require an external Clang 18 or later for final linking.

```bash
cmake -S . -B build
cmake --build build --parallel
```

The resulting compiler is `build/hsc`.

## First Program

The repository includes `examples/hello.hs`:

```hs
$include <stdio.hsh>

func main() {
    new x[1]
    x %d= 42
    printf("%d\\n", x)
    return 0
}
```

```bash
./build/hsc examples/hello.hs -o hello
./hello
```

## Linux Distribution Packages

The DEB installation layout is:

```text
/usr/bin/hsc
/usr/libexec/hitsimple/hsc_mcpp
/usr/lib/hitsimple/libhitsimple_runtime.a
/usr/share/hitsimple/stdlib
/usr/share/doc/HitSimple
```

Install a local DEB package:

```bash
sudo apt install ./hitsimple_0.2.1_amd64.deb
```

Use `hitsimple_0.2.1_arm64.deb` on arm64. The package depends on `clang-18`, or a Debian `clang` package at version 18 or later. When the default Ubuntu 22.04 or Debian 12 repositories do not provide Clang 18, first configure the corresponding `jammy` or `bookworm` repository from [apt.llvm.org](https://apt.llvm.org/); the DEB package does not modify system package sources.

The tar.gz archive is a relocatable directory package. Run its `bin/hsc` directly after extraction. The compiler locates the preprocessor, standard library, and runtime relative to its own location.

Fedora 44 and EL9 each provide x86_64/aarch64 RPM packages, for example:

```bash
sudo dnf install ./hitsimple-0.2.1-1.fc44.x86_64.rpm
```

EL9 uses the `1.el9` release suffix. RPM packages install to `/usr/bin`, `/usr/libexec/hitsimple`, the distribution library directory (`/usr/lib64/hitsimple` on x86_64/aarch64), and `/usr/share/hitsimple`; the system must already provide `clang >= 18`. They do not bundle Clang or provide a GPG signature. Fedora 44 and EL9 are the only currently supported RPM baselines.

## macOS Distribution Packages

```text
hitsimple-0.2.1-macos-arm64.tar.gz
hitsimple-0.2.1-macos-x86_64.tar.gz
```

After extraction, use `bin/hsc` directly. The package locates `hsc_mcpp`, the static runtime, and the standard library relative to the executable, so the extracted directory remains usable after being moved. The system must provide Clang 18 or later, selected through `--clang`, `HITSIMPLE_CLANG`, or PATH.

macOS provides only unsigned relocatable tar.gz archives; no signing, notarization, or PKG installer is provided.

## Windows Distribution Packages

Full package:

```text
hitsimple-0.2.1-windows-x86_64-full.zip
```

It includes `hsc.exe`, the standard library, the static runtime, and the pinned `llvm-mingw-20240619-ucrt-x86_64` toolchain. No Visual Studio or system Clang installation is required after extraction.

Slim package:

```text
hitsimple-0.2.1-windows-x86_64-slim.zip
```

The slim package requires a compatible llvm-mingw/Clang 18 installation. Toolchain lookup order is:

1. `--clang <path>`.
2. `HITSIMPLE_CLANG`.
3. `toolchain/bin/clang++.exe` in the full package.
4. `clang-18`.
5. `clang` and `clang++` on PATH.

When `-o` is omitted, Windows generates `a.exe` by default. User programs link the GCC/C++ runtime statically and should depend only on Windows system DLLs.

## Compiler Usage

```text
hsc [options] <input>...
```

Common commands:

```bash
hsc examples/hello.hs -o hello
hsc --checked examples/hello.hs -o hello-checked
hsc --emit-llvm examples/hello.hs -o hello.ll
hsc --clang /path/to/clang++ examples/hello.hs -o hello
hsc --target-info
```

See [`USAGE.md`](USAGE.md) for complete CLI documentation. See [`Standard.md`](Standard.md) for the language model, standard templates, safety semantics, and ABI contracts.

## VS Code Extension

The extension source is in `vscode/hitsimple/`. It provides syntax highlighting, indentation, snippets, the `$hsc` Problem Matcher, and Build/Run commands.

```bash
cd vscode/hitsimple
npm ci
npm test
npm run package:vsix
code --install-extension dist/hitsimple-vscode-0.2.1.vsix --force
```

Key settings:

| Setting | Default | Purpose |
| --- | --- | --- |
| `hitsimple.compilerPath` | `hsc` | Compiler executable name or path. |
| `hitsimple.mode` | `unchecked` | Selects `unchecked`, `static-checked`, or `checked`. |
| `hitsimple.outputDirectory` | `.hitsimple/build` | Output directory inside the workspace. |
| `hitsimple.additionalArgs` | `[]` | Additional arguments passed as separate argv entries. |

Haskell also uses `.hs`. When both language extensions are installed, explicitly associate the extension in the workspace:

```json
{
  "files.associations": {
    "*.hs": "hitsimple"
  }
}
```

## Verification

```bash
./build/hsc_unit_tests
ctest --test-dir build --output-on-failure --parallel 4
```

VS Code Extension Host tests require `xvfb-run` in headless Linux environments:

```bash
cd vscode/hitsimple
xvfb-run -a npm run test:extension
```

## Known Boundaries

- Checked mode does not yet fully track boundaries for `extern` globals, raw FFI addresses, or file handles.
- C compatibility is a restricted subset. C struct pass-by-value ABI supports only covered x86_64 SysV ELF layouts; Windows and Darwin reject it explicitly.
- `f16` arithmetic computes through `f32` before writing back. Linux `f128` depends on platform binary128/glibc support; Windows and macOS use the software backend.
- `mut self` and ordinary `mut` parameters remain reserved semantics and produce explicit diagnostics.

## License and Third-Party Components

HitSimple is licensed under the GNU Affero General Public License v3.0 only (`AGPL-3.0-only`); see [LICENSE](LICENSE). Release packages include this license and licenses or copyright notices for used third-party components, including mcpp, LLVM/llvm-mingw, and the Boost Software License 1.0. Review those terms separately before using or redistributing them.
