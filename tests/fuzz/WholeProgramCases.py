"""Deterministic grammar-aware HitSimple whole-program fuzz cases.

The generator deliberately starts with a small, defined subset of HitSimple:
fixed-width unsigned integers, pure helper calls, structured control flow, and
one passive user-template declaration.  A narrow subset makes an optimization
failure actionable: generated programs do not depend on host input, address
values, unspecified evaluation order, or language-level undefined behavior.
"""

from __future__ import annotations

from dataclasses import dataclass
from random import Random
from typing import Iterable, Optional, Sequence


@dataclass(frozen=True)
class GeneratorState:
    """Names and grammar facts available while rendering a valid program."""

    readable_variables: tuple[str, ...]
    writable_variables: tuple[str, ...]
    functions: tuple[str, ...]
    templates: tuple[str, ...]


@dataclass(frozen=True)
class ProgramStatement:
    """A removable, syntactically complete statement subtree."""

    name: str
    lines: tuple[str, ...]

    def render(self) -> str:
        return "\n".join("    " + line if line else "" for line in self.lines) + "\n"


@dataclass(frozen=True)
class WholeProgramCase:
    """One generated source program and its oracle contract."""

    name: str
    kind: str
    source: str
    expected_diagnostic: Optional[str]
    metadata: dict[str, object]
    removable_fragments: tuple[str, ...] = ()
    safety_mode: str = "unchecked"
    expected_stdout: Optional[str] = None
    expected_runtime_error: Optional[str] = None


SAFE_TEMPLATES: tuple[str, ...] = ("u32",)
INVALID_RULES: tuple[str, ...] = (
    "template_mismatch",
    "length_mismatch",
    "unsupported_operation",
    "invalid_assignment",
    "invalid_call",
    "invalid_ternary",
    "raw_only_literal_misuse",
)


def _literal(random: Random) -> str:
    return str(random.randrange(0, 1 << 16))


def _value(random: Random, state: GeneratorState) -> str:
    if state.readable_variables and random.randrange(3) != 0:
        return random.choice(state.readable_variables)
    return _literal(random)


def _expression(random: Random, state: GeneratorState, depth: int = 0) -> str:
    if depth >= 2 or random.randrange(3) == 0:
        return _value(random, state)

    left = _expression(random, state, depth + 1)
    right = _expression(random, state, depth + 1)
    if state.functions and random.randrange(5) == 0:
        return random.choice(state.functions) + "(" + left + ", " + right + ")"
    return "(" + left + " " + random.choice(("+", "-", "*")) + " " + right + ")"


def _render_template() -> str:
    return "\n".join(
        (
            "template GeneratedPair {",
            "    left[4] as u32",
            "    right[4] as u32",
            "}",
            "",
        )
    )


def _render_helper() -> str:
    return "\n".join(
        (
            "func mix_u32(left as u32, right as u32) -> u32 {",
            "    return left + right",
            "}",
            "",
        )
    )


def _declaration_statement(
    random: Random, state: GeneratorState, index: int
) -> tuple[ProgramStatement, GeneratorState]:
    name = "value_" + str(index)
    statement = ProgramStatement(
        "declaration_" + str(index),
        (
            "new " + name + " as u32 = " + _expression(random, state),
            "checksum = checksum + " + name,
        ),
    )
    return statement, GeneratorState(
        state.readable_variables + (name,),
        state.writable_variables + (name,),
        state.functions,
        state.templates,
    )


def _assignment_statement(
    random: Random, state: GeneratorState, index: int
) -> ProgramStatement:
    target = random.choice(state.writable_variables)
    return ProgramStatement(
        "assignment_" + str(index),
        (
            target + " = " + _expression(random, state),
            "checksum = checksum + " + target,
        ),
    )


def _if_statement(random: Random, state: GeneratorState, index: int) -> ProgramStatement:
    condition = random.choice(state.readable_variables)
    then_value = _expression(random, state)
    else_value = _expression(random, state)
    return ProgramStatement(
        "if_" + str(index),
        (
            "if (" + condition + ") {",
            "    checksum = checksum + " + then_value,
            "} else {",
            "    checksum = checksum + " + else_value,
            "}",
        ),
    )


def _while_statement(random: Random, state: GeneratorState, index: int) -> ProgramStatement:
    loop_name = "remaining_" + str(index)
    body_value = _expression(random, state)
    iterations = str(random.randrange(1, 5))
    return ProgramStatement(
        "while_" + str(index),
        (
            "new " + loop_name + " as u32 = " + iterations,
            "while (" + loop_name + " > 0) {",
            "    checksum = checksum + " + body_value,
            "    " + loop_name + "--",
            "}",
        ),
    )


def _render_valid_program(statements: Sequence[ProgramStatement]) -> str:
    body = "".join(statement.render() for statement in statements)
    return "\n".join(
        (
            "$include <stdio.hsh>",
            "$include <stdlib.hsh>",
            "",
            _render_template().rstrip(),
            "",
            _render_helper().rstrip(),
            "",
            "func main() {",
            "    new checksum as u32 = 0",
            body.rstrip(),
            "    new output as u8 = to_u8(checksum)",
            '    printf("%d\\n", output)',
            "    return 0",
            "}",
            "",
        )
    )


def generate_valid_cases(seed: int, count: int, max_statements: int) -> list[WholeProgramCase]:
    if count <= 0:
        raise ValueError("valid case count must be positive")
    if max_statements < 3:
        raise ValueError("max statements must be at least 3")

    random = Random(seed)
    cases: list[WholeProgramCase] = []
    for case_index in range(count):
        statement_count = random.randrange(3, max_statements + 1)
        state = GeneratorState((), (), ("mix_u32",), ("GeneratedPair",))
        statements: list[ProgramStatement] = []
        for statement_index in range(statement_count):
            if not state.readable_variables:
                statement, state = _declaration_statement(random, state, statement_index)
            else:
                kind = random.choice(("declaration", "assignment", "if", "while"))
                if kind == "declaration":
                    statement, state = _declaration_statement(random, state, statement_index)
                elif kind == "assignment":
                    statement = _assignment_statement(random, state, statement_index)
                elif kind == "if":
                    statement = _if_statement(random, state, statement_index)
                else:
                    statement = _while_statement(random, state, statement_index)
            statements.append(statement)

        source = _render_valid_program(statements)
        cases.append(
            WholeProgramCase(
                name="valid-" + str(case_index + 1).zfill(3),
                kind="valid",
                source=source,
                expected_diagnostic=None,
                metadata={
                    "seed": seed,
                    "safe_templates": list(SAFE_TEMPLATES),
                    "statement_count": statement_count,
                    "functions": list(state.functions),
                    "templates": list(state.templates),
                },
                removable_fragments=(
                    _render_template(),
                    _render_helper(),
                    *(statement.render() for statement in statements),
                ),
            )
        )
    return cases


def _invalid_cases() -> Iterable[WholeProgramCase]:
    yield WholeProgramCase(
        "invalid-template-mismatch",
        "invalid",
        """template Left {
    code[4] as i32
}
template Right {
    code[4] as i32
}
func main() {
    new source as Left
    new target as Right = source
    return 0
}
""",
        "default user template assignment requires matching templates",
        {"rule": "template_mismatch"},
    )
    yield WholeProgramCase(
        "invalid-length-mismatch",
        "invalid",
        """func main() {
    new source[4] as bytes
    new target[2] as bytes = source
    return 0
}
""",
        "bytes assignment requires an equal-length source View",
        {"rule": "length_mismatch"},
    )
    yield WholeProgramCase(
        "invalid-unsupported-operation",
        "invalid",
        """func main() {
    new value[1]
    value %d= 1 %100d+ 2
    return 0
}
""",
        "unsupported binary operator",
        {"rule": "unsupported_operation"},
    )
    yield WholeProgramCase(
        "invalid-assignment",
        "invalid",
        """func main() {
    new source[4] as bytes
    new target as u32 = source
    return 0
}
""",
        "right operand of '=' is not an integer expression",
        {"rule": "invalid_assignment"},
    )
    yield WholeProgramCase(
        "invalid-call",
        "invalid",
        """func consume(value as u32) -> u32 {
    return value
}
func main() {
    new source[4] as bytes
    new result as u32 = consume(source)
    return 0
}
""",
        "function call 'consume' argument 1 must exactly match parameter template and byte length",
        {"rule": "invalid_call"},
    )
    yield WholeProgramCase(
        "invalid-ternary",
        "invalid",
        """func main() {
    new condition as bool = 1
    new narrow as u32 = 1
    new wide as u64 = 2
    new result as u32 = condition ? narrow : wide
    return 0
}
""",
        "ternary branches must have the same template and byte length",
        {"rule": "invalid_ternary"},
    )
    yield WholeProgramCase(
        "invalid-raw-only-literal",
        "invalid",
        """func main() {
    new result as u8 = 'AB'
    return 0
}
""",
        "character literal byte length does not fit target 'result'",
        {"rule": "raw_only_literal_misuse"},
    )


def generate_invalid_cases(seed: int, count: int) -> list[WholeProgramCase]:
    if count <= 0:
        raise ValueError("invalid case count must be positive")
    base = list(_invalid_cases())
    random = Random(seed ^ 0x48534655)
    random.shuffle(base)
    cases: list[WholeProgramCase] = []
    for index in range(count):
        original = base[index % len(base)]
        repetition = index // len(base)
        if repetition == 0:
            cases.append(original)
            continue
        cases.append(
            WholeProgramCase(
                original.name + "-repeat-" + str(repetition + 1),
                original.kind,
                original.source,
                original.expected_diagnostic,
                {**original.metadata, "repetition": repetition + 1},
                original.removable_fragments,
            )
        )
    return cases


def _feature_valid_cases() -> tuple[WholeProgramCase, ...]:
    """Generate one oracle-backed valid case for every P5 grammar extension."""

    return (
        WholeProgramCase(
            "valid-signed-boundary",
            "valid",
            """$include <stdio.hsh>
$include <stdlib.hsh>

func main() {
    new minimum as i32 = -2147483648
    new adjusted as i32 = minimum + 1
    new maximum as i32 = 2147483647
    new passed as u8 = adjusted < 0 && maximum > 0 ? 1 : 0
    printf("%d\\n", passed)
    return 0
}
""",
            None,
            {"feature": "signed-boundary", "oracle": "1\\n", "invalid_rule": "invalid-signed-return"},
            expected_stdout="1\n",
        ),
        WholeProgramCase(
            "valid-template-field-layout",
            "valid",
            """$include <stdio.hsh>

template GeneratedLayout {
    code[4] as u32
    tag[1] as u8
}

func main() {
    new value as GeneratedLayout
    value.code = 41
    value.tag = 1
    new expected_code as u32 = 41
    new expected_tag as u8 = 1
    new passed as u8 = value.code == expected_code && value.tag == expected_tag ? 1 : 0
    printf("%d\\n", passed)
    return 0
}
""",
            None,
            {"feature": "template-field-layout", "oracle": "1\\n", "invalid_rule": "invalid-template-member"},
            expected_stdout="1\n",
        ),
        WholeProgramCase(
            "valid-try-catch-user-template",
            "valid",
            """$include <stdio.hsh>

template GeneratedFailure {
    code[4] as i32
}

func main() {
    new value as GeneratedFailure
    value.code = 42
    try {
        throw value
    } catch (error as GeneratedFailure) {
        new passed as u8 = error.code == 42 ? 1 : 0
        printf("%d\\n", passed)
        return 0
    }
    return 1
}
""",
            None,
            {"feature": "try-catch", "oracle": "1\\n", "invalid_rule": "invalid-try-catch-view"},
            expected_stdout="1\n",
        ),
        WholeProgramCase(
            "valid-multi-return",
            "valid",
            """$include <stdio.hsh>

func pair(value as u32) -> (first as u32, second as u32) {
    return value, value + 1
}

func main() {
    new first as u32
    new second as u32
    first, second = pair(40)
    new passed as u8 = first == 40 && second == 41 ? 1 : 0
    printf("%d\\n", passed)
    return 0
}
""",
            None,
            {"feature": "multi-return", "oracle": "1\\n", "invalid_rule": "invalid-multi-return-target-count"},
            expected_stdout="1\n",
        ),
        WholeProgramCase(
            "valid-checked-memory-safe-path",
            "valid",
            """$include <stdio.hsh>
$include <stdlib.hsh>
$include <string.hsh>

func main() {
    new source[4] = 0x03020100
    new destination[4]
    new heap = calloc(1, 4)
    new copied = memcpy(destination, source, 4)
    new compared = memcmp(destination, source, 4)
    free(heap)
    new passed as u8 = compared == 0 ? 1 : 0
    printf("%d\\n", passed)
    return 0
}
""",
            None,
            {"feature": "checked-safe-path", "oracle": "1\\n", "invalid_rule": "runtime-checked-divide-by-zero"},
            safety_mode="checked",
            expected_stdout="1\n",
        ),
        WholeProgramCase(
            "runtime-checked-divide-by-zero",
            "runtime-error",
            """func dynamic_zero() -> i32 {
    return 0
}

func main() -> i32 {
    new divisor as i32 = dynamic_zero()
    return 7 / divisor
}
""",
            None,
            {"feature": "checked-error-path", "oracle": "runtime division-by-zero"},
            safety_mode="checked",
            expected_stdout="",
            expected_runtime_error="hitsimple runtime error: integer division by zero",
        ),
    )


def _feature_invalid_cases() -> tuple[WholeProgramCase, ...]:
    """One stable rejection oracle accompanies each newly enabled grammar rule."""

    return (
        WholeProgramCase(
            "invalid-signed-return",
            "invalid",
            """func pair() -> (first as i32, second as i32) {
    return 1
}

func main() {
    return 0
}
""",
            "return value count does not match function signature",
            {"rule": "signed-return-count", "feature": "signed-boundary"},
        ),
        WholeProgramCase(
            "invalid-template-member",
            "invalid",
            """template GeneratedLayout {
    code[4] as u32
}

func main() {
    new value as GeneratedLayout
    value.missing = 1
    return 0
}
""",
            "unknown member 'missing'",
            {"rule": "unknown-template-member", "feature": "template-field-layout"},
        ),
        WholeProgramCase(
            "invalid-try-catch-view",
            "invalid",
            """func main() {
    try {
        throw 7
    } catch (error as f64) {
        return 0
    }
    return 1
}
""",
            "float operand is not a float expression",
            {"rule": "invalid-try-catch-view", "feature": "try-catch"},
        ),
        WholeProgramCase(
            "invalid-multi-return-target-count",
            "invalid",
            """func pair() -> (first as u32, second as u32) {
    return 1, 2
}

func main() {
    new only as u32
    only = pair()
    return 0
}
""",
            "multi-return call result count does not match target count",
            {"rule": "multi-return-target-count", "feature": "multi-return"},
        ),
    )


def generate_cases(
    seed: int, valid_count: int, invalid_count: int, max_statements: int
) -> list[WholeProgramCase]:
    return (
        generate_valid_cases(seed, valid_count, max_statements)
        + list(_feature_valid_cases())
        + generate_invalid_cases(seed, invalid_count)
        + list(_feature_invalid_cases())
    )
