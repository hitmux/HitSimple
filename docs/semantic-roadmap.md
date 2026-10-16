# Semantic completion checklist

`Standard.md` (1.0.0-Beta.21) is the contract. This checklist tracks concrete
work and evidence; heading counts, manifest entries, and green tests alone do
not establish complete conformance. The [evidence map](standard-evidence.md)
records bounded witnesses rather than a coverage percentage.

| Stage | Scope and completion gate | State |
| --- | --- | --- |
| VIEW-001 | Raw-memory floating Views: preserve argument metadata and fill bytes; four widths, ABI/rvalue paths, safety/provider/optimization matrix | Merged in [PR30](https://github.com/hitmux/HitSimple/pull/30); exact-head CI and independent review passed |
| LENGTH-001 | Infer declaration lengths from validated expressions; validate every `length()` operand; cover literals, indexing, ternaries, chained `as`, side effects, and dynamic lengths | Implemented with validated HIR lengths and evaluated operands; see [bounded evidence and remaining limits](validated-view-lengths.md), candidate review and CI required |
| RETURN-001 | Establish named return storage, scope, and explicit/bare returns without invented zero initialization | Implemented; candidate review and CI required; initialization-state diagnostics tracked in INIT-001 |
| ASSIGN-VIEW-001 | Direct-name assignment results: rvalue metadata and representation-preserving snapshots, independent of later argument writes | Implemented; candidate review and CI required |
| ASSIGN-001 | Evaluate multiple separate RHS Views before writes, retain discard side effects, and cover complex assignment-expression targets | Open; independent of captured multiple-return sources |
| ASSIGN-002 | Apply ordinary destination contracts to captured multiple-return sources; preserve rvalue sources, target order, discard evaluation, and user op = | Implemented; [bounded evidence](multi-return-assignments.md), candidate review and CI required |
| BIND-001 | Implement §7.7 name rebinding while retaining definition storage/lifetime and declared length; migrate pointer-value fixtures to ordinary assignment | Open; source inspection finds pointer-sized stores in place of binding indirection |
| LIB-001 | Audit the full View input domain of all 75 existing Chapter 14 APIs, error/bounds behavior, provider equivalence, and zero-length dynamic View registration | Open; manifest presence is not semantic coverage |
| LIB-002 | Documented extensions: additional ASCII predicates, bounded search/comparison, then integer parsing after return/assignment prerequisites | Proposed extensions, not missing Beta.21 API names |
| INIT-001 | Track initialized bytes and diagnose provably uninitialized reads in static-checked/checked modes; detect runtime reads where possible | Open; includes named-return slots and ordinary storage; no zero-initialization guarantee |
| AUDIT-001 | Review every normative rule with positive/negative/runtime evidence, implementation-defined disclosures, and target-specific verification | Open; no full-conformance claim |

Each stage should be independently reviewable. Merge only the tested candidate
with passing CI and no blocking review. Retain exact command/commit evidence in
its pull request. Do not alter the Standard to match an implementation bug, and
do not infer that an untested rule works from a neighboring witness.
