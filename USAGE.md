# Using `hsc`

`hsc` is the HitSimple compiler. It preprocesses each input, parses it, performs semantic analysis, lowers it through HitSimple HIR and LLVM IR, and links executable builds with the host toolchain.

This document describes the compiler interface. [`Standard.md`](Standard.md) remains the authority for language syntax and semantics.

## Build the Compiler

Configure and build the project before invoking `hsc`:

```bash
cmake -S . -B build
cmake --build build --parallel
```

The resulting executable is `build/hsc`.

## Command Form

```text
hsc [options] <input>...
```

An input path is required unless the selected action is `--help`, `--version`, or `--target-info`. An unknown option, a missing input, a missing `-o` value, or a non-existent output directory is an error.

## Compile an Executable

Compile a HitSimple source file into an executable:

```bash
./build/hsc examples/hello.hs -o hello
./hello
```

Without `-o`, the executable is written as `a.out` in the current directory:

```bash
./build/hsc examples/hello.hs
./a.out
```

The output directory must already exist:

```bash
mkdir -p out
./build/hsc examples/hello.hs -o out/hello
```

### Multiple Translation Units

Pass multiple source files to compile each one as an independent translation unit and link them into one executable:

```bash
./build/hsc app/main.hs app/math.hs app/io.hs -o app/program
```

Cross-translation-unit declarations must match. Macros, typedefs, and file-scope `static` declarations remain translation-unit local.

## Select a Safety Mode

Choose one of the following options for an executable or LLVM IR build:

| Option | Effect |
| --- | --- |
| `--unchecked` | Disables safety checks. This is the default. |
| `--static-checked` | Reports safety errors that can be proved statically and adds no runtime checks. |
| `--checked` | Performs static checking and emits supported runtime checks for dynamic errors. |

For example:

```bash
./build/hsc --checked examples/hello.hs -o hello-checked
./build/hsc --static-checked examples/hello.hs -o hello-static
```

The exact set of checked behaviours and the boundaries of runtime coverage are defined by [`Standard.md`](Standard.md) and reported for the current target by `--target-info`.

## Inspect a Program

Inspection actions take exactly one input file and print to standard output. They cannot be combined with `-o`.

```bash
# Print lexer tokens.
./build/hsc --dump-tokens examples/hello.hs

# Print the parsed AST.
./build/hsc --dump-ast examples/hello.hs

# Print semantic HIR.
./build/hsc --dump-hir examples/hello.hs
```

Use these actions separately. `hsc` rejects a command that requests more than one action.

## Emit LLVM IR

`--emit-llvm` takes exactly one input. With no output path it writes LLVM IR to standard output; with `-o`, it writes the IR to a file.

```bash
./build/hsc --emit-llvm examples/hello.hs
./build/hsc --emit-llvm examples/hello.hs -o hello.ll
```

Safety-mode options can be used with this action:

```bash
./build/hsc --checked --emit-llvm examples/hello.hs -o hello-checked.ll
```

## Preprocess Source

Use `-E` or `--preprocess-only` to print the source after preprocessing. This action takes exactly one input and may write to standard output or to a file.

```bash
./build/hsc --preprocess-only examples/hello.hs
./build/hsc -E examples/hello.hs -o hello.preprocessed.hs
```

This is useful when diagnosing `$include`, macro expansion, and conditional preprocessing.

## Use C Compatibility Mode

`--c-compat` parses every supplied input as focused C-compatibility source, lowers it to the core HitSimple representation, and applies C ABI metadata before linking.

```bash
./build/hsc --c-compat path/to/program.c -o program
./build/hsc --c-compat app/main.c app/support.c -o app/program
```

It can be used with `--dump-ast`, `--dump-hir`, and `--emit-llvm`. It cannot be used with `--dump-tokens` or `--target-info`.

The supported C subset and ABI restrictions are intentionally limited. Check [`Standard.md`](Standard.md) before relying on a C construct or a target-specific calling convention.

## Inspect the Compiler and Target

```bash
# Display the HitSimple and LLVM versions.
./build/hsc --version

# Display all command-line options.
./build/hsc --help

# Display implementation-defined target, ABI, standard-library, and safety details.
./build/hsc --target-info
```

`--target-info` has no input file and cannot be combined with `-o` or `--c-compat`.

## Action Constraints

`--dump-tokens`, `--dump-ast`, `--dump-hir`, `--emit-llvm`, `--preprocess-only`, and `--target-info` are actions. Only one action may appear in a command.

| Action | Input files | Supports `-o` | Supports `--c-compat` |
| --- | --- | --- | --- |
| Default executable build | One or more | Yes | Yes |
| `--dump-tokens` | Exactly one | No | No |
| `--dump-ast` | Exactly one | No | Yes |
| `--dump-hir` | Exactly one | No | Yes |
| `--emit-llvm` | Exactly one | Yes | Yes |
| `--preprocess-only` / `-E` | Exactly one | Yes | No effect |
| `--target-info` | None | No | No |

`--preprocess-only` does not parse the input, so `--c-compat` does not alter its output.

## Next Steps

- Start with [`examples/hello.hs`](examples/hello.hs), then explore the other programs under [`examples/`](examples/).
- Read [`Standard.md`](Standard.md) when you need the normative definition of a language feature.
- Read [`CONTRIBUTING.md`](CONTRIBUTING.md) to build and test the compiler itself.
