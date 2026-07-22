#!/usr/bin/env python3
"""Unit tests for deterministic property-program generation."""

from __future__ import annotations

import unittest

from property_cases import generate_cases


class PropertyCaseTests(unittest.TestCase):
    def test_generation_is_deterministic_and_covers_all_families(self) -> None:
        first = generate_cases(17, 2)
        second = generate_cases(17, 2)

        self.assertEqual(first, second)
        self.assertEqual(
            [case.name for case in first],
            [
                "integer_unsigned_widths",
                "integer_signedness",
                "conversion_properties",
                "boolean_view_properties",
                "packed_template_layout",
                "metamorphic_equivalences",
            ],
        )
        self.assertTrue(any("integer-width" in case.families for case in first))
        self.assertTrue(any("boolean-test" in case.families for case in first))
        self.assertTrue(any("conversion" in case.families for case in first))
        self.assertTrue(any("packed-layout" in case.families for case in first))
        self.assertTrue(any("metamorphic" in case.families for case in first))

    def test_cases_emit_one_success_marker_for_each_check(self) -> None:
        for case in generate_cases(9, 1):
            self.assertIn("func main()", case.source)
            self.assertIn('printf("%d\\n"', case.source)
            self.assertTrue(case.expected_stdout)
            self.assertEqual(set(case.expected_stdout.splitlines()), {"1"})

    def test_trials_must_be_positive(self) -> None:
        with self.assertRaises(ValueError):
            generate_cases(1, 0)


if __name__ == "__main__":
    unittest.main()
