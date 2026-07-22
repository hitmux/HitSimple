"""Safe, deterministic models for the host-only C reference differential.

Each case is rendered twice from the same restricted semantic model: once as
HitSimple and once as ISO C.  The C output intentionally uses unsigned
arithmetic, byte arrays and ``memcpy`` only; this keeps the oracle independent
from HSC while excluding C signed-overflow, alignment and aliasing undefined
behavior.
"""

from __future__ import annotations

from dataclasses import dataclass
from random import Random
from typing import Sequence


U32_MASK = (1 << 32) - 1
FAMILIES: tuple[str, ...] = ("integer", "boolean-test", "packed-layout", "loop")


@dataclass(frozen=True)
class ReferenceCase:
    """A portable reference-differential input and its observable oracle."""

    name: str
    family: str
    hs_source: str
    c_source: str
    expected_stdout: str
    metadata: dict[str, object]


def _bytes(value: int) -> tuple[int, int, int, int]:
    return tuple((value >> (8 * index)) & 0xFF for index in range(4))  # type: ignore[return-value]


def _hs_print_u32(name: str) -> list[str]:
    return [
        "    new " + name + "_byte_0 as u8 = to_u8(" + name + ")",
        "    new " + name + "_byte_1 as u8 = to_u8(" + name + " >> 8)",
        "    new " + name + "_byte_2 as u8 = to_u8(" + name + " >> 16)",
        "    new " + name + "_byte_3 as u8 = to_u8(" + name + " >> 24)",
        '    printf("%d %d %d %d\\n", '
        + "to_u32("
        + name
        + "_byte_0), to_u32("
        + name
        + "_byte_1), to_u32("
        + name
        + "_byte_2), to_u32("
        + name
        + "_byte_3))",
    ]


def _c_print_u32(name: str) -> list[str]:
    return [
        '    printf("%u %u %u %u\\n",',
        "           (unsigned)(" + name + " & UINT32_C(0xff)),",
        "           (unsigned)((" + name + " >> 8) & UINT32_C(0xff)),",
        "           (unsigned)((" + name + " >> 16) & UINT32_C(0xff)),",
        "           (unsigned)((" + name + " >> 24) & UINT32_C(0xff)));",
    ]


def _wrap_program(lines: Sequence[str]) -> str:
    return "\n".join(
        (
            "$include <stdio.hsh>",
            "$include <stdlib.hsh>",
            "",
            "func main() {",
            *lines,
            "    return 0",
            "}",
            "",
        )
    )


def _wrap_c_program(lines: Sequence[str]) -> str:
    return "\n".join(
        (
            "#include <stdint.h>",
            "#include <stdio.h>",
            "#include <string.h>",
            "",
            "int main(void) {",
            *lines,
            "    return 0;",
            "}",
            "",
        )
    )


def _integer_case(random: Random, index: int, seed: int) -> ReferenceCase:
    left = random.randrange(1 << 31, 1 << 32)
    right = random.randrange(1, 1 << 16)
    factor = random.randrange(2, 33)
    result = ((left + right) & U32_MASK) * factor & U32_MASK
    expected = "{} {} {} {}\n".format(*_bytes(result))
    hs_source = _wrap_program(
        (
            "    new left as u32 = " + str(left),
            "    new right as u32 = " + str(right),
            "    new factor as u32 = " + str(factor),
            "    new sum as u32 = left + right",
            "    new result as u32 = sum * factor",
            *_hs_print_u32("result"),
        )
    )
    c_source = _wrap_c_program(
        (
            "    const uint32_t left = UINT32_C(" + str(left) + ");",
            "    const uint32_t right = UINT32_C(" + str(right) + ");",
            "    const uint32_t factor = UINT32_C(" + str(factor) + ");",
            "    const uint32_t sum = (uint32_t)(((uint64_t)left + (uint64_t)right) & UINT32_MAX);",
            "    const uint32_t result = (uint32_t)(((uint64_t)sum * (uint64_t)factor) & UINT32_MAX);",
            *_c_print_u32("result"),
        )
    )
    return ReferenceCase(
        "integer-" + str(index).zfill(3),
        "integer",
        hs_source,
        c_source,
        expected,
        {"seed": seed, "left": left, "right": right, "factor": factor, "uses": ["u32", "wrap"]},
    )


def _boolean_case(random: Random, index: int, seed: int) -> ReferenceCase:
    values = [random.randrange(256) for _ in range(4)]
    if index % 2 == 0:
        values = [0, 0, random.randrange(1, 256), 0]
    else:
        values = [0, 0, 0, 0]
    expected_value = int(any(values))
    hs_lines = ["    new raw[4] as bytes"]
    c_lines = ["    const uint8_t raw[4] = {" + ", ".join(str(value) for value in values) + "};"]
    for offset, value in enumerate(values):
        hs_lines.append("    raw[" + str(offset) + "] = " + str(value))
    hs_lines.extend(
        (
            "    new selected as u8 = raw ? 1 : 0",
            '    printf("%d\\n", selected)',
        )
    )
    c_lines.extend(
        (
            "    const unsigned selected = (unsigned)(raw[0] != 0 || raw[1] != 0 || raw[2] != 0 || raw[3] != 0);",
            '    printf("%u\\n", selected);',
        )
    )
    return ReferenceCase(
        "boolean-test-" + str(index).zfill(3),
        "boolean-test",
        _wrap_program(hs_lines),
        _wrap_c_program(c_lines),
        str(expected_value) + "\n",
        {"seed": seed, "bytes": values, "uses": ["bytes", "Boolean-test", "ternary"]},
    )


def _packed_layout_case(random: Random, index: int, seed: int) -> ReferenceCase:
    first = random.randrange(256)
    middle = random.randrange(1 << 32)
    tail = random.randrange(1 << 16)
    middle_bytes = _bytes(middle)
    expected_values = (7, 1, first, *middle_bytes, tail & 0xFF, (tail >> 8) & 0xFF)
    expected = "{} {} {} {} {} {} {} {} {}\n".format(*expected_values)
    hs_source = "\n".join(
        (
            "template ReferencePacked {",
            "    first[1] as u8",
            "    middle[4] as u32",
            "    tail[2] as u16",
            "}",
            "",
            _wrap_program(
                (
                    "    new packed as ReferencePacked",
                    "    packed.first = " + str(first),
                    "    packed.middle = " + str(middle),
                    "    packed.tail = " + str(tail),
                    "    new base as addr = &packed",
                    "    new middle_address as addr = &packed.middle",
                    "    new middle_offset as u64 = to_u64(middle_address? - base?)",
                    "    new packed_size as u64 = sizeof(ReferencePacked)",
                    "    new middle_byte_0 as u8 = to_u8(packed.middle)",
                    "    new middle_byte_1 as u8 = to_u8(packed.middle >> 8)",
                    "    new middle_byte_2 as u8 = to_u8(packed.middle >> 16)",
                    "    new middle_byte_3 as u8 = to_u8(packed.middle >> 24)",
                    "    new tail_byte_0 as u8 = to_u8(packed.tail)",
                    "    new tail_byte_1 as u8 = to_u8(packed.tail >> 8)",
                    '    printf("%d %d %d %d %d %d %d %d %d\\n", to_u32(to_u8(packed_size)), to_u32(to_u8(middle_offset)), to_u32(packed.first), to_u32(middle_byte_0), to_u32(middle_byte_1), to_u32(middle_byte_2), to_u32(middle_byte_3), to_u32(tail_byte_0), to_u32(tail_byte_1))',
                )
            ).rstrip(),
            "",
        )
    )
    c_source = _wrap_c_program(
        (
            "    uint8_t packed[7] = {0};",
            "    const uint8_t first = UINT8_C(" + str(first) + ");",
            "    const uint32_t middle = UINT32_C(" + str(middle) + ");",
            "    const uint16_t tail = UINT16_C(" + str(tail) + ");",
            "    memcpy(packed, &first, sizeof(first));",
            "    memcpy(packed + 1, &middle, sizeof(middle));",
            "    memcpy(packed + 5, &tail, sizeof(tail));",
            '    printf("%u %u %u %u %u %u %u %u %u\\n",',
            "           7U, 1U, (unsigned)packed[0], (unsigned)packed[1],",
            "           (unsigned)packed[2], (unsigned)packed[3], (unsigned)packed[4],",
            "           (unsigned)packed[5], (unsigned)packed[6]);",
        )
    )
    return ReferenceCase(
        "packed-layout-" + str(index).zfill(3),
        "packed-layout",
        hs_source,
        c_source,
        expected,
        {
            "seed": seed,
            "first": first,
            "middle": middle,
            "tail": tail,
            "layout_bytes": [1, 4, 2],
            "uses": ["user-template", "packed-layout", "pointer-offset"],
        },
    )


def _loop_case(random: Random, index: int, seed: int) -> ReferenceCase:
    limit = random.randrange(2, 12)
    factor = random.randrange(1, 1 << 16)
    initial = random.randrange(1 << 31, 1 << 32)
    result = initial
    for iteration in range(limit):
        result = (result + iteration * factor) & U32_MASK
    expected = "{} {} {} {}\n".format(*_bytes(result))
    hs_source = "\n".join(
        (
            "func accumulate(limit as u32, factor as u32, initial as u32) -> u32 {",
            "    new total as u32 = initial",
            "    new index as u32 = 0",
            "    while (index < limit) {",
            "        total = total + index * factor",
            "        index++",
            "    }",
            "    return total",
            "}",
            "",
            _wrap_program(
                (
                    "    new result as u32 = accumulate(" + str(limit) + ", " + str(factor) + ", " + str(initial) + ")",
                    *_hs_print_u32("result"),
                )
            ).rstrip(),
            "",
        )
    )
    c_source = "\n".join(
        (
            "#include <stdint.h>",
            "#include <stdio.h>",
            "#include <string.h>",
            "",
            "static uint32_t accumulate(uint32_t limit, uint32_t factor, uint32_t initial) {",
            "    uint32_t total = initial;",
            "    for (uint32_t index = 0; index < limit; ++index) {",
            "        total = (uint32_t)(((uint64_t)total + (uint64_t)index * (uint64_t)factor) & UINT32_MAX);",
            "    }",
            "    return total;",
            "}",
            "",
            "int main(void) {",
            "    const uint32_t result = accumulate(UINT32_C(" + str(limit) + "), UINT32_C(" + str(factor) + "), UINT32_C(" + str(initial) + "));",
            *_c_print_u32("result"),
            "    return 0;",
            "}",
            "",
        )
    )
    return ReferenceCase(
        "loop-" + str(index).zfill(3),
        "loop",
        hs_source,
        c_source,
        expected,
        {"seed": seed, "limit": limit, "factor": factor, "initial": initial, "uses": ["pure-function", "while", "u32"]},
    )


def generate_cases(seed: int, trials: int) -> list[ReferenceCase]:
    """Return all four safe reference families for every deterministic trial."""

    if trials <= 0:
        raise ValueError("trials must be positive")
    random = Random(seed)
    cases: list[ReferenceCase] = []
    for index in range(1, trials + 1):
        cases.extend(
            (
                _integer_case(random, index, seed),
                _boolean_case(random, index, seed),
                _packed_layout_case(random, index, seed),
                _loop_case(random, index, seed),
            )
        )
    return cases
