# Wide integer literal destinations

Standard sections 3.5.1, 7.2 and 7.3 separate an integer token's magnitude
and inferred source View from its assignment destination. Every standard
nonnegative literal magnitude (through 2^64-1) fits a signed raw-integer
object wider than eight bytes. A wider destination does not expand the
standard token range or change the literal's inferred source length.

The semantic fit check now accepts those wider destinations after validating
the token magnitude. LLVM literal construction first preserves the source-width bit pattern,
then extends according to its integer interpretation. Upper-half u64 token
magnitudes zero-extend; folded signed results and unary negative constants
retain their actual sign and source width.

The regression uses 9-, 16- and 32-byte destinations, ordinary and upper-half
u64 magnitudes, hexadecimal spelling, -1 and the minimum signed i64 constant.
It covers local/global/static initialization, reassignment, explicit integer
assignment and named returns. The runtime oracle compares each literal path
with assignment from an already formed i64/u64 variable, which is independent
of the changed constant-widening path and avoids an endianness assumption.
Folded one-byte underflow and signed eight-byte overflow are also checked.
An LLVM oracle additionally checks exact APInt values and verifies modules
in all three safety modes. Oversized token magnitudes remain rejected, and a
narrow signed-destination rejection is retained as a compatibility control.

The registered host matrix uses both standard-library providers, three safety
modes and O0/O2. Configuration is not proof that those executions occurred;
actual run evidence belongs to the build/CI results.

This bounded correction does not redesign narrow-literal truncation, expand
the standard integer token range, add standard integer templates wider than
u64, or establish every dynamic-width/operator/conversion contract.
