# Semantic completion checklist

`Standard.md` (1.0.0-Beta.21) is the contract. This checklist tracks concrete
work and evidence; heading counts, manifest entries, and green tests alone do
not establish complete conformance. The [evidence map](standard-evidence.md)
records bounded witnesses rather than a coverage percentage.

Status below records the integrated stack and publication state as of 2026-10-16.
Merged pull requests are evidence pointers for their bounded scope; reviewed local
candidates and in-progress CI are not merged work.

| Stage | Scope and completion gate | State |
| --- | --- | --- |
| VIEW-001 | Raw-memory floating Views: argument metadata and fill bytes; four widths and ABI/rvalue paths | Merged in [PR30](https://github.com/hitmux/HitSimple/pull/30) |
| ASSIGN-VIEW-001 | Direct-name assignment-expression rvalue snapshots, independent of later argument writes | Merged in [PR31](https://github.com/hitmux/HitSimple/pull/31); [bounded evidence](assignment-result-views.md) |
| RETURN-001 | Named return storage, scope, and explicit/bare returns without invented zero initialization | Merged in [PR32](https://github.com/hitmux/HitSimple/pull/32); [bounded evidence](named-return-storage.md); INIT-001 remains open |
| LENGTH-001 | Validated inferred declaration lengths and evaluated `length()` operands | Merged in [PR33](https://github.com/hitmux/HitSimple/pull/33); [remaining limits](validated-view-lengths.md) |
| ASSIGN-002 | Ordinary destination contracts for captured multiple-return sources, ordered targets and discards | Merged in [PR34](https://github.com/hitmux/HitSimple/pull/34); [bounded evidence](multi-return-assignments.md) |
| CTYPE-001 | Raw-byte character inputs from floating and other Views in both providers | Merged in [PR35](https://github.com/hitmux/HitSimple/pull/35); [bounded evidence](ctype-view-inputs.md) |
| BUILD-TEMP-001 | Independently owned native-build temporary directories and concurrent isolation | Merged in [PR36](https://github.com/hitmux/HitSimple/pull/36); not a general temporary-file security audit |
| ASSIGN-001 | Evaluate multiple separate RHS Views before writes; retain discard effects | Merged in [PR37](https://github.com/hitmux/HitSimple/pull/37); [bounded evidence](multiple-rhs-assignments.md) |
| CSTR-001 | cstr source acceptance, bounded copying and distinct destination contracts | Merged in [PR38](https://github.com/hitmux/HitSimple/pull/38); [bounded evidence](cstr-assignment-contracts.md) |
| CSTR-002 | First-NUL truncation and zero padding for literal cstr assignment | Merged in [PR39](https://github.com/hitmux/HitSimple/pull/39); [bounded evidence](cstr-literal-padding.md) |
| LIB-ASCII-001 | Additional ASCII predicates with explicit extension ownership and byte oracles | Reviewed candidate in [PR40](https://github.com/hitmux/HitSimple/pull/40), CI pending; [extension boundary](ascii-character-extensions.md) |
| BIND-001 | Distinct mutable name-binding address with original storage/lifetime, declared length and persistent template retained | Reviewed candidate in [PR41](https://github.com/hitmux/HitSimple/pull/41), CI pending; [bounded evidence](true-name-rebinding.md) |
| TERNARY-RAW-001 | Full selected string/raw View bytes, lazy branches and separate cstr assignment | Reviewed local candidate, publication pending (planned PR42); [bounded evidence](literal-ternary-views.md) |
| INTEGER-WIDE-001 | Standard and folded integer values in fixed destinations wider than eight bytes | Reviewed local candidate, publication pending (planned PR43); [bounded evidence](wide-integer-literal-destinations.md) |
| UNSIGNED-001 | Same-length raw-byte postfix snapshots, including supported dynamic/empty extents | Source stage independently reviewed; integration with ternary/wide/rebinding stages in progress; [evidence and remaining limits](unsigned-view-snapshots.md); integration review and exact-head CI required |
| RESIZE-001 | Address-order byte copies for every resize width, raw-only results, single evaluation and fixed storage reuse | Source stage independently reviewed; integrated with unsigned snapshots, ternaries, wide literals and rebinding; [bounded evidence](fixed-resize-byte-views.md); integration review and exact-head CI required |
| ASSIGN-VIEW-002 | Complex assignment-expression targets and their result snapshots | Bounded implementation work in progress; direct-name coverage does not complete this stage |
| LIB-001 | Full View input domain of all 75 Chapter 14 APIs, errors/bounds, provider equivalence and zero-length consumers | Open; manifest presence is not semantic coverage |
| LIB-002 | Further documented extensions: bounded search/comparison and integer parsing | Proposed extensions, not missing Beta.21 API names |
| INIT-001 | Initialized-byte tracking and provable/runtime uninitialized-read diagnostics | Open, including named-return slots and ordinary storage; no zero-initialization guarantee |
| AUDIT-001 | Every normative rule, positive/negative/runtime evidence, implementation-defined disclosures and target verification | Open; no full-conformance claim |

Each stage should be independently reviewable. Merge only the tested candidate
with passing CI and no blocking review. Retain exact command/commit evidence in
its pull request. Do not alter the Standard to match an implementation bug, and
do not infer that an untested rule works from a neighboring witness.
