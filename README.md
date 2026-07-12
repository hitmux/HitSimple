# HitSimple

HitSimple is an experimental programming language and compiler built around a simple idea: **data is memory; meaning comes from explicit interpretation**.

Instead of making a static type system the centre of the language, HitSimple works with byte sequences, addresses, lengths, lifetimes, and interpretation views. The same bytes can be read as an integer, a floating-point value, a string, an address, or a structured layout when the program explicitly chooses that interpretation.

This makes memory representation visible in ordinary programming while keeping a compiled workflow: source code is lowered through parsing, semantic analysis, HitSimple HIR, and LLVM IR to an object file or executable.

## Status

HitSimple is under active, experimental development. The `hsc` compiler already provides a working compiler pipeline, standard-library headers, three safety modes, preprocessing, and a focused C-compatibility mode. Treat the implementation as a tool for exploration and development rather than a stable production platform.

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

## Safety Modes

The compiler supports three execution modes:

| Option | Behaviour |
| --- | --- |
| `--unchecked` | Disables safety checks. This is the default mode. |
| `--static-checked` | Reports statically provable safety errors without inserting runtime checks. |
| `--checked` | Performs static checks and emits runtime checks for supported dynamic errors. |

The exact guarantees, unchecked behaviour, and implementation boundaries are specified in [`Standard.md`](Standard.md).

## Explore Further

- [`examples/`](examples/) contains small programs that can be compiled directly.
- [`Standard.md`](Standard.md) defines the language and its normative behaviour.
- [`USAGE.md`](USAGE.md) is the compiler command guide.
- [`CONTRIBUTING.md`](CONTRIBUTING.md) explains how to build, test, and extend the compiler.
