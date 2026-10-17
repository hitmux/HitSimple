# Temporary View full-expression lifetimes

Standard §§6.10, 8.10 and 14.3 require a temporary rvalue View to expire at the
end of its source full expression. An address returned by `strchr` into that
View does not extend its lifetime. Named storage copied from that View has its
own lifetime. `as` changes interpretation, never ownership.

## Source boundaries and lowering

HIR marks source assignment/expression roots, each declaration initializer,
each global initializer and each comma-separated for-post entry. Generated
captures, assignment stores and intermediate results are not independent full
expressions. If/while/for conditions end after their Boolean value has been
consumed, before entering the selected successor. Short-circuit and ternary
sequence points do not themselves end the enclosing full expression.

Checked lowering calls `hs_full_expression_end` at these boundaries. The runtime
clears temporary registrations owned by the current frame, reclaiming registry
slots rather than accumulating tombstones. Nested callees clean their own
registrations without expiring caller temporaries still used as arguments.
Named locals, static locals, globals and heap allocations retain their existing
lifetimes. Return ABI values are constructed or copied before frame cleanup;
caught throw delivery similarly precedes full-expression and lexical cleanup.

Core string literals use invocation-local temporary copies of pooled constant
bytes. Pooling the initializer must not confer static lifetime on the rvalue.
C-compatibility string literals explicitly retain C static storage duration.
Internal user-template View-ABI results are caller-owned temporaries, not named
locals. Fixed/dynamic resize, postfix unsigned, byte-swap, assignment and ternary
results retain their existing temporary ownership registrations.

## Bounded static proof

Static address facts distinguish live and expired temporary provenance, separate
from local/static/heap storage. Evaluated `strchr` arguments preserve lifetime
facts before argument captures are released. At source boundaries these facts
expire in pointer bytes and rebound locations. Definite later accesses receive
`static safety check failed: use after full-expression end` in static-checked
and checked modes. Unchecked mode does not make invalid uses defined.

Control-flow handling records break/continue and lexical throw edges instead of
restoring stale pre-loop/pre-try facts. A pointer repaired from a live named
object inside an unconditional for-break or a catch path remains usable. A
finite while joins carried mutations even when its first condition is known
true. Joins deliberately lose disagreeing facts; the analysis is conservative
and noninterprocedural, not complete alias analysis.

## Bounded regression evidence

- Exact HIR and verified LLVM tests distinguish three source boundaries from
  internal multiple-assignment captures, and verify cleanup before three
  condition branches without a premature short-circuit barrier.
- Static tests cover direct literal, fixed/dynamic resize, unsigned, assignment,
  ternary, byte-swap, declarator and condition escape, plus live copies, repaired
  loops/catches, opposite expired paths and lexical scope exit.
- The legal host fixture covers both library providers, all three safety modes
  and O0/O2: same-expression reads, nested callers, named/static/global/heap
  storage, return copies, user operators/methods, multiple RHS snapshots,
  repeated positive/empty sites, control flow and caught throw delivery.
- Opaque negative fixtures run invalid accesses only in checked mode and require
  exit 120 with a source-located checked-load diagnostic. Unchecked and
  static-checked variants are compile-only. A direct proven negative requires
  a compile-time diagnostic. The exact original fixed-resize escape
  `strchr(resize_bytes("abc", 4) as cstr, 'a')` has both legal same-expression and
  checked-only escaped-pointer controls.
- A portable standalone C driver observes registry state without dereferencing
  expired C storage. It covers nested frame isolation, 4096 slot reuses,
  positive-to-empty clearing, shrinking/replacing a registration, preserved
  named/static/heap storage, and source-located checked failure.
- A C-compatibility fixture verifies a literal returned through a pointer
  identity function survives the originating source expression.

These are configured bounded oracles, not universal conformance or an assertion
that they ran on every platform. Actual execution belongs to the tested build
and exact-head CI.

```sh
./build/hsc_unit_tests LLVMCodegen_FullExpressionBoundariesPreserveInternalCaptures \
  LLVMCodegen_TemporaryCleanupAfterConditionConsumption \
  StaticSafetyAnalyzer_TemporaryFullExpressionExpiry \
  StaticSafetyAnalyzer_TemporarySameExpressionAndStorageCopies \
  StaticSafetyAnalyzer_TemporaryFactsFollowLoopAndThrowExits
ctest --test-dir build -R '^hsc_(runtime_temporary_lifetimes|temporary_view_lifetimes)$' --output-on-failure
python3 tools/standard_evidence.py --check
```

## Limits

The registry observes raw addresses. It cannot distinguish an old pointer from
a newly registered live object reusing the same address. This remains a bounded
runtime detector, not a generation-tagged pointer model. Dynamic alloca stack
consumption and initialized-byte tracking are separate work. The existing
uncaught-throw path discards its operand after semantic analysis; this stage
covers caught delivery and does not claim to repair uncaught evaluation.
