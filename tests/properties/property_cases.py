"""Deterministic property and metamorphic programs for the HitSimple test suite."""

from __future__ import annotations

from dataclasses import dataclass
from random import Random
from typing import Mapping, Sequence


INTEGER_WIDTHS: tuple[tuple[str, int], ...] = (
    ("u8", 8),
    ("u16", 16),
    ("u32", 32),
    ("u64", 64),
)
SIGNED_INTEGER_WIDTHS: tuple[tuple[str, int], ...] = (
    ("i8", 8),
    ("i16", 16),
    ("i32", 32),
    ("i64", 64),
)


@dataclass(frozen=True)
class PropertyCase:
    name: str
    families: tuple[str, ...]
    source: str
    expected_stdout: str
    metadata: Mapping[str, object]


def _unsigned(value: int, bits: int) -> int:
    return value & ((1 << bits) - 1)


def _signed(value: int, bits: int) -> int:
    value = _unsigned(value, bits)
    sign_bit = 1 << (bits - 1)
    return value if value < sign_bit else value - (1 << bits)


def _truncating_division(left: int, right: int) -> int:
    quotient = abs(left) // abs(right)
    return -quotient if (left < 0) != (right < 0) else quotient


def _program(body: Sequence[str]) -> str:
    return "\n".join(
        [
            "$include <stdio.hsh>",
            "$include <stdlib.hsh>",
            "",
            "func main() {",
            *body,
            "    return 0",
            "}",
            "",
        ]
    )


def _append_check(lines: list[str], condition: str, index: int) -> int:
    check_name = "check_" + str(index)
    output_name = "check_output_" + str(index)
    lines.append("    new " + check_name + " as bool = " + condition)
    lines.append("    new " + output_name + " as u8 = " + check_name + " ? 1 : 0")
    lines.append('    printf("%d\\n", ' + output_name + ")")
    return index + 1


def _finish_case(
    name: str,
    families: tuple[str, ...],
    lines: list[str],
    checks: int,
    metadata: Mapping[str, object],
) -> PropertyCase:
    return PropertyCase(
        name=name,
        families=families,
        source=_program(lines),
        expected_stdout="1\n" * checks,
        metadata=metadata,
    )


def _append_binary_property(
    lines: list[str],
    prefix: str,
    template: str,
    left: int,
    operator: str,
    right: int,
    expected: int,
    check_index: int,
) -> int:
    left_name = prefix + "_left"
    right_name = prefix + "_right"
    actual_name = prefix + "_actual"
    expected_name = prefix + "_expected"
    lines.append("    new " + left_name + " as " + template + " = " + str(left))
    lines.append("    new " + right_name + " as " + template + " = " + str(right))
    lines.append(
        "    new "
        + actual_name
        + " as "
        + template
        + " = "
        + left_name
        + " "
        + operator
        + " "
        + right_name
    )
    lines.append("    new " + expected_name + " as " + template + " = " + str(expected))
    return _append_check(lines, actual_name + " == " + expected_name, check_index)


def _unsigned_integer_case(random: Random, trials: int) -> PropertyCase:
    lines: list[str] = []
    checks = 0
    for template, bits in INTEGER_WIDTHS:
        mask = (1 << bits) - 1
        for trial in range(trials):
            add_left = mask - random.randrange(0, min(mask, 31) + 1)
            add_right = random.randrange(1, min(mask, 31) + 1)
            checks = _append_binary_property(
                lines,
                template + "_add_" + str(trial),
                template,
                add_left,
                "+",
                add_right,
                _unsigned(add_left + add_right, bits),
                checks,
            )

            sub_left = random.randrange(0, min(mask, 255) + 1)
            sub_right = sub_left + random.randrange(1, min(mask - sub_left, 255) + 1)
            checks = _append_binary_property(
                lines,
                template + "_sub_" + str(trial),
                template,
                sub_left,
                "-",
                sub_right,
                _unsigned(sub_left - sub_right, bits),
                checks,
            )

            multiply_left = random.randrange(1, mask + 1)
            multiply_right = random.randrange(2, min(mask, 17) + 1)
            checks = _append_binary_property(
                lines,
                template + "_mul_" + str(trial),
                template,
                multiply_left,
                "*",
                multiply_right,
                _unsigned(multiply_left * multiply_right, bits),
                checks,
            )
    return _finish_case(
        "integer_unsigned_widths",
        ("integer-width", "unsigned"),
        lines,
        checks,
        {"trials": trials, "templates": [name for name, _ in INTEGER_WIDTHS]},
    )


def _signed_integer_case(random: Random, trials: int) -> PropertyCase:
    lines: list[str] = []
    checks = 0
    for template, bits in SIGNED_INTEGER_WIDTHS:
        minimum = -(1 << (bits - 1))
        maximum = (1 << (bits - 1)) - 1
        for trial in range(trials):
            if trial == 0:
                add_left, add_right = minimum, -1
                sub_left, sub_right = minimum, 1
                multiply_left, multiply_right = minimum, 2
            else:
                add_left = random.randint(minimum, maximum)
                add_right = random.randint(minimum, maximum)
                sub_left = random.randint(minimum, maximum)
                sub_right = random.randint(minimum, maximum)
                multiply_left = random.randint(minimum, maximum)
                multiply_right = random.randint(minimum, maximum)

            checks = _append_binary_property(
                lines,
                template + "_add_" + str(trial),
                template,
                add_left,
                "+",
                add_right,
                _signed(add_left + add_right, bits),
                checks,
            )
            checks = _append_binary_property(
                lines,
                template + "_sub_" + str(trial),
                template,
                sub_left,
                "-",
                sub_right,
                _signed(sub_left - sub_right, bits),
                checks,
            )
            checks = _append_binary_property(
                lines,
                template + "_mul_" + str(trial),
                template,
                multiply_left,
                "*",
                multiply_right,
                _signed(multiply_left * multiply_right, bits),
                checks,
            )

            checks = _append_binary_property(
                lines,
                template + "_minimum_div_neg_one_" + str(trial),
                template,
                minimum,
                "/",
                -1,
                _signed(_truncating_division(minimum, -1), bits),
                checks,
            )
    return _finish_case(
        "integer_signedness",
        ("integer-width", "signedness"),
        lines,
        checks,
        {"trials": trials, "templates": [name for name, _ in SIGNED_INTEGER_WIDTHS]},
    )


def _conversion_case(random: Random, trials: int) -> PropertyCase:
    lines: list[str] = []
    checks = 0
    for trial in range(trials):
        u8_value = random.randrange(0, 1 << 8)
        u8_prefix = "u8_round_trip_" + str(trial)
        lines.append("    new " + u8_prefix + "_source as u8 = " + str(u8_value))
        lines.append("    new " + u8_prefix + "_wide as u64 = to_u64(" + u8_prefix + "_source)")
        lines.append("    new " + u8_prefix + "_round_trip as u8 = to_u8(" + u8_prefix + "_wide)")
        checks = _append_check(
            lines,
            u8_prefix + "_round_trip == " + u8_prefix + "_source",
            checks,
        )

        u32_value = random.randrange(1 << 31, 1 << 32)
        u32_prefix = "u32_zero_extend_" + str(trial)
        lines.append("    new " + u32_prefix + "_source as u32 = " + str(u32_value))
        lines.append("    new " + u32_prefix + "_wide as u64 = to_u64(" + u32_prefix + "_source)")
        lines.append("    new " + u32_prefix + "_expected as u64 = " + str(u32_value))
        checks = _append_check(
            lines,
            u32_prefix + "_wide == " + u32_prefix + "_expected",
            checks,
        )

        i32_value = random.randrange(-(1 << 31), 0)
        i32_prefix = "i32_sign_extend_" + str(trial)
        lines.append("    new " + i32_prefix + "_source as i32 = " + str(i32_value))
        lines.append("    new " + i32_prefix + "_wide as i64 = to_i64(" + i32_prefix + "_source)")
        lines.append("    new " + i32_prefix + "_expected as i64 = " + str(i32_value))
        checks = _append_check(
            lines,
            i32_prefix + "_wide == " + i32_prefix + "_expected",
            checks,
        )

        raw_value = random.randrange(0, 1 << 32)
        raw_prefix = "reinterpret_round_trip_" + str(trial)
        lines.append("    new " + raw_prefix + "_source as u32 = " + str(raw_value))
        lines.append("    new " + raw_prefix + "_raw[4] as bytes = " + raw_prefix + "_source")
        lines.append("    new " + raw_prefix + "_round_trip as u32 = " + raw_prefix + "_raw as u32")
        checks = _append_check(
            lines,
            raw_prefix + "_round_trip == " + raw_prefix + "_source",
            checks,
        )
    return _finish_case(
        "conversion_properties",
        ("conversion", "reinterpretation"),
        lines,
        checks,
        {"trials": trials},
    )


def _boolean_case(random: Random, trials: int) -> PropertyCase:
    lines: list[str] = []
    checks = 0
    for trial in range(trials):
        values = [0, 0, 0, 0] if trial == 0 else [random.randrange(256) for _ in range(4)]
        if trial == 1:
            values = [0, 0, 1, 0]
        expected = 1 if any(values) else 0
        prefix = "boolean_view_" + str(trial)
        lines.append("    new " + prefix + "_raw[4] as bytes")
        for index, value in enumerate(values):
            lines.append("    " + prefix + "_raw[" + str(index) + "] = " + str(value))
        lines.append("    new " + prefix + "_selected as u8 = " + prefix + "_raw ? 1 : 0")
        lines.append("    new " + prefix + "_expected as u8 = " + str(expected))
        lines.append("    new " + prefix + "_expected_bool as bool = " + prefix + "_expected")
        lines.append("    new " + prefix + "_normalized as bool = " + prefix + "_raw")
        lines.append("    new " + prefix + "_direct_not as bool = !" + prefix + "_raw")
        lines.append("    new " + prefix + "_normalized_not as bool = !" + prefix + "_normalized")
        checks = _append_check(
            lines,
            prefix + "_selected == " + prefix + "_expected",
            checks,
        )
        checks = _append_check(
            lines,
            prefix + "_normalized == " + prefix + "_expected_bool",
            checks,
        )
        checks = _append_check(
            lines,
            prefix + "_direct_not == " + prefix + "_normalized_not",
            checks,
        )
    return _finish_case(
        "boolean_view_properties",
        ("boolean-test", "ternary"),
        lines,
        checks,
        {"trials": trials},
    )


def _layout_case(random: Random) -> PropertyCase:
    member_templates = ((1, "u8"), (2, "u16"), (4, "u32"), (8, "u64"))
    members = [member_templates[random.randrange(len(member_templates))] for _ in range(3 + random.randrange(4))]
    template_lines: list[str] = ["template PropertyPacked {"]
    for index, (size, template) in enumerate(members):
        template_lines.append("    field_" + str(index) + "[" + str(size) + "] as " + template)
    template_lines.append("}")
    lines: list[str] = ["    new packed as PropertyPacked", "    new base as addr = &packed"]

    checks = 0
    total_size = sum(size for size, _ in members)
    lines.append("    new packed_size as u64 = sizeof(PropertyPacked)")
    lines.append("    new expected_packed_size as u64 = " + str(total_size))
    checks = _append_check(lines, "packed_size == expected_packed_size", checks)
    offset = 0
    for index, (size, _) in enumerate(members):
        lines.append("    new field_address_" + str(index) + " as addr = &packed.field_" + str(index))
        lines.append(
            "    new field_offset_"
            + str(index)
            + " as u64 = to_u64(field_address_"
            + str(index)
            + "? - base?)"
        )
        lines.append("    new expected_field_offset_" + str(index) + " as u64 = " + str(offset))
        checks = _append_check(
            lines,
            "field_offset_" + str(index) + " == expected_field_offset_" + str(index),
            checks,
        )
        offset += size
    result = _finish_case(
        "packed_template_layout",
        ("packed-layout", "template"),
        lines,
        checks,
        {"members": [{"size": size, "template": template} for size, template in members]},
    )
    return PropertyCase(
        name=result.name,
        families=result.families,
        source="\n".join(template_lines) + "\n\n" + result.source,
        expected_stdout=result.expected_stdout,
        metadata=result.metadata,
    )


def _metamorphic_case(random: Random, trials: int) -> PropertyCase:
    lines: list[str] = []
    checks = 0
    for trial in range(trials):
        prefix = "metamorphic_" + str(trial)
        initial = random.randrange(0, 1 << 32)
        left = random.randrange(0, 1 << 32)
        right = random.randrange(0, 1 << 32)
        condition = "true" if random.randrange(2) else "false"
        limit = 2 + random.randrange(8)
        raw_values = [random.randrange(256) for _ in range(4)]
        if trial == 0:
            raw_values = [0, 0, 1, 0]

        lines.append("    new " + prefix + "_initial as u32 = " + str(initial))
        lines.append("    new " + prefix + "_direct as u32 = " + prefix + "_initial + 1")
        lines.append("    new " + prefix + "_assigned as u32 = " + prefix + "_initial")
        lines.append("    " + prefix + "_assigned = " + prefix + "_assigned + 1")
        lines.append("    new " + prefix + "_incremented as u32 = " + prefix + "_initial")
        lines.append("    " + prefix + "_incremented++")
        checks = _append_check(
            lines,
            prefix + "_direct == " + prefix + "_assigned && " + prefix + "_assigned == " + prefix + "_incremented",
            checks,
        )

        lines.append("    new " + prefix + "_condition as bool = " + condition)
        lines.append("    new " + prefix + "_left as u32 = " + str(left))
        lines.append("    new " + prefix + "_right as u32 = " + str(right))
        lines.append(
            "    new "
            + prefix
            + "_conditional as u32 = "
            + prefix
            + "_condition ? "
            + prefix
            + "_left : "
            + prefix
            + "_right"
        )
        lines.append("    new " + prefix + "_branch as u32 = " + prefix + "_right")
        lines.append("    if (" + prefix + "_condition) {")
        lines.append("        " + prefix + "_branch = " + prefix + "_left")
        lines.append("    }")
        checks = _append_check(
            lines,
            prefix + "_conditional == " + prefix + "_branch",
            checks,
        )

        lines.append("    new " + prefix + "_raw[4] as bytes")
        for index, value in enumerate(raw_values):
            lines.append("    " + prefix + "_raw[" + str(index) + "] = " + str(value))
        lines.append("    new " + prefix + "_if_boolean as u8 = 0")
        lines.append("    if (" + prefix + "_raw) {")
        lines.append("        " + prefix + "_if_boolean = 1")
        lines.append("    }")
        lines.append("    new " + prefix + "_double_not as bool = !(!" + prefix + "_raw)")
        lines.append(
            "    new "
            + prefix
            + "_ternary_boolean as u8 = "
            + prefix
            + "_double_not ? 1 : 0"
        )
        checks = _append_check(
            lines,
            prefix + "_if_boolean == " + prefix + "_ternary_boolean",
            checks,
        )

        lines.append("    new " + prefix + "_while_sum as u32 = 0")
        lines.append("    new " + prefix + "_while_index as u32 = 0")
        lines.append("    while (" + prefix + "_while_index < " + str(limit) + ") {")
        lines.append("        " + prefix + "_while_sum = " + prefix + "_while_sum + " + prefix + "_while_index")
        lines.append("        " + prefix + "_while_index++")
        lines.append("    }")
        lines.append("    new " + prefix + "_for_sum as u32 = 0")
        lines.append(
            "    for (new "
            + prefix
            + "_for_index as u32 = 0; "
            + prefix
            + "_for_index < "
            + str(limit)
            + "; "
            + prefix
            + "_for_index++) {"
        )
        lines.append("        " + prefix + "_for_sum = " + prefix + "_for_sum + " + prefix + "_for_index")
        lines.append("    }")
        checks = _append_check(
            lines,
            prefix + "_while_sum == " + prefix + "_for_sum",
            checks,
        )

        lines.append("    new " + prefix + "_add_zero as u32 = " + prefix + "_initial + 0")
        lines.append("    new " + prefix + "_multiply_one as u32 = " + prefix + "_initial * 1")
        lines.append("    new " + prefix + "_double_inverted as u32 = ~(~" + prefix + "_initial)")
        lines.append("    new " + prefix + "_converted as u32 = to_u32(" + prefix + "_initial)")
        lines.append("    new " + prefix + "_dead_branch as u32 = 0")
        lines.append("    if (false) {")
        lines.append("        " + prefix + "_dead_branch = 0")
        lines.append("    } else {")
        lines.append("        " + prefix + "_dead_branch = " + prefix + "_initial")
        lines.append("    }")
        checks = _append_check(
            lines,
            prefix
            + "_initial == "
            + prefix
            + "_add_zero && "
            + prefix
            + "_initial == "
            + prefix
            + "_multiply_one && "
            + prefix
            + "_initial == "
            + prefix
            + "_double_inverted && "
            + prefix
            + "_initial == "
            + prefix
            + "_converted && "
            + prefix
            + "_initial == "
            + prefix
            + "_dead_branch",
            checks,
        )
    return _finish_case(
        "metamorphic_equivalences",
        ("metamorphic", "ternary", "boolean-test", "optimization"),
        lines,
        checks,
        {"trials": trials},
    )


def generate_cases(seed: int, trials: int) -> Sequence[PropertyCase]:
    if trials <= 0:
        raise ValueError("trials must be positive")
    random = Random(seed)
    return (
        _unsigned_integer_case(random, trials),
        _signed_integer_case(random, trials),
        _conversion_case(random, trials),
        _boolean_case(random, trials),
        _layout_case(random),
        _metamorphic_case(random, trials),
    )
