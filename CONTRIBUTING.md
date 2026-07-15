# Contributing to HitSimple

HitSimple is an experimental programming language whose semantics are based on data in memory and explicit interpretation. This repository contains its compiler, implemented in C++20. [`Standard.md`](Standard.md) is the authority for language behaviour. Do not infer language semantics from a convenient LLVM lowering or from a single test alone.

## Development Setup

Install the dependencies listed in the [README](README.md#requirements), then configure and build out of tree:

```bash
cmake -S . -B build
cmake --build build --parallel
```

Run the complete test suite with:

```bash
ctest --test-dir build --output-on-failure
```

Useful focused checks are:

```bash
./build/hsc --help
./build/hsc --version
./build/hsc_unit_tests
```

## Repository Layout

| Path | Responsibility |
| --- | --- |
| `src/lexer/`, `src/parser/` | re2c lexer and Bison parser. Parser actions construct AST only. |
| `src/ast/` | Source-oriented syntax tree. |
| `src/sema/` | Name binding, interpretation rules, diagnostics, and HIR construction. |
| `src/hir/` | Semantic representation consumed by code generation. |
| `src/codegen/` | LLVM IR lowering and target/runtime integration. |
| `src/compat/` | Focused C-compatibility parsing and lowering into the core language. |
| `src/preprocessor/` | Preprocessing integration. |
| `src/stdlib/`, `runtime/` | Standard-library bridge and host/ABI-dependent runtime support. |
| `include/hitsimple/` | Public compiler headers. |
| `tests/` | Layered unit tests, fixtures, CLI, and execution regression cases. |
| `examples/` | Small user-facing HitSimple programs. |

The intended compiler boundary is:

```text
source -> preprocessor -> lexer -> parser -> AST -> sema -> HIR -> LLVM IR
```

Keep AST source-oriented. Name resolution, view length, lvalue/rvalue state, interpretation information, layout offsets, and safety-check points belong in HIR. LLVM code generation must consume HIR rather than reconstructing source-level semantics.

## Adding or Changing Language Behaviour

1. Check the relevant clauses in [`Standard.md`](Standard.md). If the desired behaviour is outside the standard, make the extension boundary explicit before coding.
2. Start with the narrowest regression test at the responsible layer: lexer, parser, sema/HIR, code generation, or a CLI/runtime fixture.
3. Keep Bison actions limited to AST construction. Do not add semantic validation to parser actions.
4. Preserve the separation between core HitSimple semantics and `--c-compat` translation. C syntax must lower to core syntax before core semantic analysis.
5. Add end-to-end coverage when observable output, a diagnostic, generated LLVM IR, object linking, or executable behaviour could regress.

Place new test code beside its compiler stage, such as `tests/lexer/`, `tests/parser/`, `tests/sema/`, or `tests/codegen/`. Put source fixtures under `tests/cases/<stage>/` when a CLI or runtime scenario is needed.

## Code Style

- Use C++20 and keep `-Wall -Wextra -Wpedantic` warning-free.
- Use `PascalCase` for types, `camelCase` for functions and variables, and `hitsimple::...` for namespaces.
- Keep files focused on one compiler responsibility. Split independently testable logic instead of growing large all-purpose files.
- Write comments for semantic invariants, design intent, and non-obvious implementation choices.
- Do not introduce a conventional static type system into AST or HIR. The language model is memory plus explicit interpretation.

## Before Sending a Change

Run the relevant focused tests first, then run the full suite when the environment permits:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

If a required check cannot run, state the command, failure, and unverified scope clearly.
