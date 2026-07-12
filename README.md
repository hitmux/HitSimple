# HitSimple

HitSimple is an experimental programming language and compiler built around a simple idea: **data is memory; meaning comes from explicit interpretation**.

Instead of making a static type system the centre of the language, HitSimple works with byte sequences, addresses, lengths, lifetimes, and interpretation views. The same bytes can be read as an integer, a floating-point value, a string, an address, or a structured layout when the program explicitly chooses that interpretation.

This makes memory representation visible in ordinary programming while keeping a compiled workflow: source code is lowered through parsing, semantic analysis, HitSimple HIR, and LLVM IR to an object file or executable.

## Status

HitSimple is under active, experimental development. The `hsc` compiler already provides a working compiler pipeline, standard-library headers, three safety modes, preprocessing, and a focused C-compatibility mode. The repository also includes a VS Code extension with syntax support, compiler diagnostics, and configurable Build/Run commands. Treat the implementation as a tool for exploration and development rather than a stable production platform.

`Standard.md` is the authoritative language specification. This README is deliberately a practical entry point; it does not replace the standard.

## Requirements

- CMake 3.24 or later
- A C++20 compiler
- LLVM development packages
- Bison
- re2c
- Clang

The CMake configuration checks for LLVM, Bison, re2c, and Clang before generating the build files.

## Build

```bash
cmake -S . -B build
cmake --build build --parallel
```

The compiler is written to `build/hsc`.

## Your First Program

Create `hello.hs`:

```hs
$include <stdio.hsh>

func main() {
    new x[1]
    x %d= 42
    printf("%d\\n", x)
    return 0
}
```

Compile and run it:

```bash
./build/hsc hello.hs -o hello
./hello
```

The repository also includes this example at [`examples/hello.hs`](examples/hello.hs).

## The Core Idea

In HitSimple, storage and interpretation are separate concerns. A storage object owns bytes; an expression produces a view over bytes; an interpretation template gives that view operations such as assignment, arithmetic, comparison, formatting, and method resolution.

```text
storage object -> lvalue/rvalue view -> interpretation template -> operation resolution
```

This model avoids implicit cross-template conversion. It gives systems-oriented programs direct control over byte layout and interpretation without treating raw memory as an escape hatch outside the language.

Read [`Standard.md`](Standard.md) for the complete model, syntax, standard templates, safety rules, ABI boundaries, and C-compatibility rules.

## Compiler Usage

The short form is:

```text
hsc [options] <input>...
```

```bash
./build/hsc examples/hello.hs -o hello
./build/hsc --checked examples/hello.hs -o hello-checked
./build/hsc --emit-llvm examples/hello.hs -o hello.ll
./build/hsc --target-info
```

[`USAGE.md`](USAGE.md) is the complete compiler guide. It covers executable builds, multiple translation units, inspection actions, preprocessing, LLVM IR, C compatibility, safety modes, output paths, and command constraints.

## VS Code Extension

The extension source is in [`vscode/hitsimple/`](vscode/hitsimple/). It provides:

- TextMate syntax highlighting for `.hs`, `.hsh`, and `.hsi` files.
- HitSimple-aware indentation, bracket handling, and snippets.
- A `$hsc` Problem Matcher for located lexer, parser, and semantic diagnostics.
- `HitSimple: Build Current File` and `HitSimple: Run Current File` commands.
- Configurable compiler path, safety mode, output directory, and additional argv entries.

Build a local VSIX and install it with the VS Code CLI:

```bash
cd vscode/hitsimple
npm ci
npm test
npm run package:vsix
code --install-extension dist/hitsimple-vscode-0.1.0.vsix --force
```

The runtime settings are:

| Setting | Default | Purpose |
| --- | --- | --- |
| `hitsimple.compilerPath` | `hsc` | Compiler executable name or path. Executable names are resolved from the Extension Host `PATH`. |
| `hitsimple.mode` | `unchecked` | Selects `unchecked`, `static-checked`, or `checked`. |
| `hitsimple.outputDirectory` | `.hitsimple/build` | Output directory. Relative paths cannot escape the workspace. |
| `hitsimple.additionalArgs` | `[]` | Extra arguments passed as individual argv entries without shell parsing. |

Build and Run require a trusted local or supported remote workspace and a saved `.hs` source file. Header and include files (`.hsh` and `.hsi`) are recognized for editing but are not standalone build targets.

The `.hs` suffix is also used by Haskell. If both language extensions are installed, set the association explicitly in workspace settings:

```json
{
  "files.associations": {
    "*.hs": "hitsimple"
  }
}
```

The completed implementation scope and current validation boundaries are recorded in [`Plan.md`](Plan.md).

## Safety Modes

The compiler supports three execution modes:

| Option | Behaviour |
| --- | --- |
| `--unchecked` | Disables safety checks. This is the default mode. |
| `--static-checked` | Reports statically provable safety errors without inserting runtime checks. |
| `--checked` | Performs static checks and emits runtime checks for supported dynamic errors. |

The exact guarantees, unchecked behaviour, and implementation boundaries are specified in [`Standard.md`](Standard.md).

## Tests

Run the compiler test suite after building:

```bash
./build/hsc_unit_tests
ctest --test-dir build --output-on-failure --parallel 4
```

Run the extension's Node and Extension Host tests separately:

```bash
cd vscode/hitsimple
npm test
xvfb-run -a npm run test:extension
```

The `xvfb-run` wrapper is required only for a headless Linux environment without an existing display server.

## Explore Further

- [`examples/`](examples/) contains small programs that can be compiled directly.
- [`Standard.md`](Standard.md) defines the language and its normative behaviour.
- [`USAGE.md`](USAGE.md) is the compiler command guide.
- [`CONTRIBUTING.md`](CONTRIBUTING.md) explains how to build, test, and extend the compiler.
- [`vscode/hitsimple/`](vscode/hitsimple/) contains the VS Code extension source and packaging scripts.
- [`Plan.md`](Plan.md) records the completed editor-integration scope and verification evidence.
