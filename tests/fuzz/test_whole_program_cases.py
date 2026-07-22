#!/usr/bin/env python3
"""Unit tests for the grammar-aware whole-program generator and reducer."""

from __future__ import annotations

import unittest

from WholeProgramCases import INVALID_RULES, generate_cases, generate_invalid_cases, generate_valid_cases
from WholeProgramReducer import reduce_source


class WholeProgramCaseTests(unittest.TestCase):
    def test_valid_generation_is_deterministic_and_structured(self) -> None:
        first = generate_valid_cases(17, 3, 6)
        second = generate_valid_cases(17, 3, 6)

        self.assertEqual(first, second)
        self.assertEqual([case.name for case in first], ["valid-001", "valid-002", "valid-003"])
        for case in first:
            self.assertEqual(case.kind, "valid")
            self.assertIn("template GeneratedPair", case.source)
            self.assertIn("func mix_u32", case.source)
            self.assertIn("func main()", case.source)
            self.assertIn("printf(\"%d\\n\", output)", case.source)
            self.assertTrue(case.removable_fragments)

    def test_invalid_generation_covers_one_rule_per_case(self) -> None:
        cases = generate_invalid_cases(20260722, len(INVALID_RULES))

        self.assertEqual({str(case.metadata["rule"]) for case in cases}, set(INVALID_RULES))
        for case in cases:
            self.assertEqual(case.kind, "invalid")
            self.assertTrue(case.expected_diagnostic)
            self.assertIn("func main()", case.source)

    def test_combined_generation_has_requested_case_counts(self) -> None:
        cases = generate_cases(1, 2, 4, 5)
        self.assertEqual(len(cases), 6)
        self.assertEqual(sum(case.kind == "valid" for case in cases), 2)
        self.assertEqual(sum(case.kind == "invalid" for case in cases), 4)

    def test_repeated_invalid_rules_keep_distinct_artifact_names(self) -> None:
        cases = generate_invalid_cases(17, len(INVALID_RULES) + 2)
        self.assertEqual(len({case.name for case in cases}), len(cases))
        self.assertEqual(cases[-1].metadata["repetition"], 2)

    def test_reducer_removes_complete_fragments_then_shrinks_literals(self) -> None:
        source = "prefix\nkeep 99\nremove 88\nsuffix\n"
        result = reduce_source(
            source,
            lambda candidate: "keep" in candidate and "suffix" in candidate,
            ("remove 88\n",),
            max_attempts=12,
        )

        self.assertNotIn("remove", result.source)
        self.assertIn("keep", result.source)
        self.assertIn("suffix", result.source)
        self.assertLess(len(result.source), len(source))
        self.assertGreaterEqual(result.accepted_transformations, 1)


if __name__ == "__main__":
    unittest.main()
