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
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```

Useful focused checks are:

```bash
./build/hsc --help
./build/hsc --version
./build/hsc_unit_tests
```

## Native Build Temporary Directories

Native executable linking, mixed-language linking, static archive assembly,
and Cargo metadata/build-output capture use independently owned temporary
directories. Allocation uses LLVM's atomic unique-directory creation rather
than a PID-only name: separate PID namespaces can share a temporary filesystem.
Only a successfully allocated directory is registered for cleanup; an existing
path must never be deleted to make room for a build.

Focused checks for this contract are:

```bash
./build/hsc_unit_tests TemporaryDirectory_
ctest --test-dir build -R '^hsc_temporary_directory_isolation$' --output-on-failure
```

The unit checks cover ownership, moves, failure, Unicode paths, and concurrent
same-prefix allocation. The Linux CLI check runs eight parallel native, mixed,
archive, and Cargo builds in a shared temporary directory, preserves sentinels
at the old PID-only paths, executes the results, and checks cleanup. It requires
the existing Clang, llvm-ar, and Cargo toolchains. This is a bounded scratch
ownership regression, not a general temporary-file security audit; preprocessor
scratch and timing JSON publication are separate implementations.

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
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```

If a required check cannot run, state the command, failure, and unverified scope clearly.

## Plan-Driven Change Scope

When `Plan.md` assigns work to named PRs, a pull request must cover exactly one
of those PR boundaries. Its description must contain these standalone lines:

```text
Plan: PR <number>
Issues: <ISSUE-ID>[, <ISSUE-ID>...]
Focused tests: <command(s) for the mapped CTest label or other required gate>
Acceptance: <observable completion criterion>
```

The listed issue IDs must exist in the Plan's Issue-to-Plan-to-PR tracking
table, and the focused tests must cover their mapped labels or stated
artifacts. A reviewer must reject a change that crosses another planned PR
boundary, lacks an independently runnable acceptance path, or adds an
unplanned concern. Split it into its own PR, or update `Issue.md` and `Plan.md`
with evidence before review. The `PR traceability` GitHub Actions job checks
that the four fields are present; branch protection must require that check
before merge.

## Standard Evidence Map

The [bounded evidence map](docs/standard-evidence.md) separates Standard navigation,
manifest ownership, source-inspected witnesses, and actual test execution. When a
referenced source changes, inspect the witness claim and assertions before updating
its reviewed source-text hash in `docs/evidence/standard-witnesses.json`.
Regeneration deliberately cannot refresh review hashes automatically.

```bash
python3 tools/standard_evidence.py --write
python3 tools/standard_evidence.py --check
python3 tests/evidence/test_standard_evidence.py
ctest --test-dir build -R '^hsc_standard_evidence_' --output-on-failure
```

Add exact test names rather than substring prefixes. A unit-test filter with no
matches can report `0/0 PASS`; the evidence runner rejects this and requires
exactly one passing selection per witness. Runtime evidence remains tied to its
own CTest and per-variant harness artifacts. Do not turn an unmapped heading into
an unimplemented claim, or structural ownership into behavioral coverage.
