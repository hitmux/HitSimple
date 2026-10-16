# Name binding addresses

Standard §§5.2, 5.6, and 7.7 distinguish a name's stable definition storage from
its current binding address. `name &= address` changes only that address.
The name retains its declared extent, persistent template and lifetime; reads,
writes, members, slices, and `&name` subsequently use the new address. Saved
addresses still locate their original storage. Scope cleanup never releases the
rebound-to object on behalf of the name.

For example, after `new x as i32 = 1; new y as i32 = 9; x &= &y; x = 17;`,
`x` and `y` both read 17. An address saved from `&x` before the rebind still
locates the original four bytes containing 1. To store an address as pointer
content, use `new p as addr = &y`, then `[4]*p` to access the pointee.

## Lowering and source contract

`BindingRebind` is a distinct HIR statement, rather than an integer content
store. A whole-translation-unit scan identifies names that need mutable address
slots, including globals referenced by a function emitted before the function
that rebinds them. Automatic slots reset to definition storage whenever the
declaration executes. Global and static-local slots persist. Parameters,
borrowed user-operation destinations, catch variables, and named return slots
use the same distinction.

The RHS accepts integer-interpreted Views (`none` with an integer interpretation,
`iN`, `uN`, `addr`, and `bool`) and integer literals. This follows the integer
assignment source matrix in §§7.2, 7.3, 11.6, and 11.10 when §7.7 requires adjustment
to system pointer width. A raw `bytes`, `cstr`, floating, user-template, or
opaque-handle source needs an explicit integer interpretation first; the target
can retain any of those templates. The implementation does not require the
source or target extent to equal pointer width. Signed shorter sources are
sign-extended, unsigned shorter sources are zero-extended, and longer sources
are truncated by integer significance. RHS evaluation occurs once.

Only an identifier is accepted on the left of `&=`. Member, index, slice,
temporary-template View, and dereference targets receive the required §18.2
item 18 diagnostic. Declaration initialization uses the declared identifier.
When used as an expression, the existing assignment-result path copies the
newly bound target bytes immediately, with the target's retained extent and
template. Rebinding in statement position does not itself read those bytes.

A captured RHS lvalue freezes its location, not the name used to obtain that
location. In `(x &=), out = &next, x`, `out` reads the original captured location
after `x` has been rebound. Capturing or discarding that lvalue does not read
its bytes; consuming it does. Rvalue captures and rebinding-expression results
remain byte snapshots.

## Safety and access paths

The definition address remains the address used for registration, cleanup,
and debug declaration storage. Content accesses go through the binding slot.
Potentially rebound addresses use byte alignment and non-inbounds address
arithmetic, rather than inheriting alignment or object bounds from the original
definition. Checked loads and stores validate the actual accessed region.
View formation and `length()` do not read bytes. Whole-View copies and snapshots
validate the complete copied extent. Memory and file builtins keep the selected
View's visible capacity independently of the backing allocation. Only memory
builtins with `mem_view`/`mem_lview` parameters give raw `addr` operands their
pointed-to object range. File buffers use `view`/`lview`, so an `addr` argument
reads or writes its pointer-content bytes, including after rebinding. Dynamic
indices and slices check the retained source extent before pointer-width truncation, including
when the result is only addressed or measured. These formation checks do not
read the result's bytes.

C-string scans check termination within both the visible View and live backing
storage. A larger declared extent need not be fully readable for a shorter
valid terminated string scan. Default cstr assignment validates its cstr source
contract, while explicit `%s=` retains raw-source support and stops at NUL.
Argument locations are evaluated in order and string validation occurs after
all argument effects, so a later argument cannot invalidate an earlier check
or cause a valid repaired source to be rejected prematurely.
String copies and append operations separately enforce destination capacity.

Static safety tracks binding locations separately from facts about stored
pointer or numeric contents. It preserves dynamic-object identity, scope
expiry, saved original addresses, and captured locations. Joins and calls retain
unknown binding states instead of treating an unknown name as its original
object. Writes invalidate facts about the actual aliased object. Member
accesses and memory builtins use the accessed member or requested byte count.

## Bounded regression evidence

- HIR tests distinguish binding updates from content stores and reject malformed
  rebinding statements. Sema tests cover non-pointer-sized targets and the five
  non-identifier target categories.
- LLVM tests require a separate slot, current-address loads, translation-unit
  global discovery, signed/unsigned widening and truncation, and preserved
  handle target metadata.
- Native positive fixtures cover extents 1/3/4/8/16/32, unaligned access, raw
  copying, all four floating widths, bool and cstr stores, user members and
  borrowed `op =`, low-byte ctype/memset/strchr inputs, input count/data targets,
  global/static/parameter/catch/named slots, loop reset, cleanup, saved definition addresses, pointer-valued source
  content, captured old locations, multi-return rebinding, valid dynamic indices,
  ternary source-width adjustment, file I/O through rebound addr Views, later
  argument repair, and assignment-result isolation.
- Checked failures cover null loads/stores, insufficient object extent, raw
  copy extent, freed dynamic storage, expired local storage, selected View capacity
  for memory/file/string operations, invalid dynamic index/slice formation,
  borrowed user-operation tail access, and late-argument mutations of string
  inputs. Static tests additionally cover known alias content, joins, call invalidation, narrow member and captured-slice
  access, unknown-provenance visible bounds, raw-address contrasts, cstr source
  contracts, and full snapshots versus short cstr scans.
- The three native manifests configure 60 positive variants, 124 checked-error
  variants, and 30 compile-negative variants. Positives use both providers,
  three safety modes, and O0/O2. Error cases use both providers and O0/O2;
  target diagnostics run all safety modes and O0/O2. Release selection includes
  AArch64 and Windows; selection is not evidence of local execution there.

```sh
./build/hsc_unit_tests Rebinding SeparatesNameBinding NonIdentifierRebinding
ctest --test-dir build -R '^hsc_true_name_rebinding' --output-on-failure
python3 tools/standard_evidence.py --check
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```

Existing fixtures whose intended operation was pointer-value storage now use
`p = &object`: pointer slices/increments, checked global/static pointers,
escaped-local lifetime tests, their Sema/LLVM tests, and the comprehensive
example's `view.ptr` member. The genuine aggregate rebinding test is retained.
Lexer token fixtures and C compatibility bitwise `&=` are unchanged.

## Integrated contract regression

The combined stack retains first-NUL literal padding, fixed cstr rvalue acceptance,
all eight optional ASCII predicates, and the 75 normative standard-library APIs.
The integration fixture combines rebinding-expression cstr snapshots, frozen
multiple-RHS locations, named/anonymous/rebound return storage, and ASCII raw-byte
reads from rebound floating Views. In particular, one-byte character consumers
can read a valid one-byte backing object without demanding the name's full extent.
Three checked-error fixtures cover missing termination in materialized expression
and named-return sources, plus a short-backing expression snapshot. Static safety
and structural cstr source-metadata tests cover the same integration boundaries.
These are bounded host witnesses; they do not establish all combinations.

## Remaining boundaries

This witness does not establish all language semantics, complete static safety,
interprocedural proofs, or every target architecture. Static facts remain
conservative for partial-object contents and exact cstr terminator positions;
unknown aliases/calls discard facts. General loop and integer-conversion
precision limits remain. Native execution evidence is host-specific.

Postfix `?` remains a separate §8.7 gap: the Standard permits interpreting the
bytes of any View as unsigned, while the current analyzer rejects bytes/cstr,
floating, and user-template operands and does not preserve dynamic-length
source formation. For example, `new raw[8] as bytes; new name as i32;
name &= raw?` fails with `operand of unsigned interpretation is not an integer
expression`. This failure occurs while forming the source, before rebinding's
pointer-width adjustment. The gap is not justified by the source matrix above.
A related pre-existing source-formation issue is that `raw as none` currently
acquires a signed-integer interpretation even when `raw` is a `bytes` View;
§6.7 describes clearing the template rather than creating a new interpretation.
That source reaches rebinding already marked integer and needs a separate
interpretation audit.

The preceding base also has an independent literal-fit limitation for default
assignment such as `new wide[16] = 16`; the runtime test supplies an i32 View
instead. The integrated cstr-source contract accepts fixed cstr assignment-expression
sources. Its shared assignment/return lowering retains cstr source-View metadata
after materialization, preserving visible-extent and missing-NUL checks alongside
per-byte checked, stop-at-NUL reads. A rebinding expression snapshots the complete
retained extent before string consumption, so a short valid terminated backing
object permits direct scanning but does not permit an oversized snapshot.
Direct slice arguments to writable memory builtins also remain a separate
Sema acceptance gap (`memset(name[0:+1], 0, 2)` is rejected as not a writable
memory View). Static selected/captured-slice capacity tests use accepted read
forms and valid HIR for the write contract rather than widening that unrelated
source acceptance. No Standard text is changed to accommodate implementation
behavior.
