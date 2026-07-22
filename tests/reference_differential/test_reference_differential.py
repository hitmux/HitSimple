#!/usr/bin/env python3
"""Unit tests for the independent C reference generator and comparison runner."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from ReferenceCases import FAMILIES, generate_cases
from run_reference_differential import ProcessResult, _write_comparison, observable_mismatches


class ReferenceDifferentialTests(unittest.TestCase):
    def test_generation_is_deterministic_and_covers_every_family(self) -> None:
        first = generate_cases(17, 2)
        second = generate_cases(17, 2)

        self.assertEqual(first, second)
        self.assertEqual({case.family for case in first}, set(FAMILIES))
        self.assertEqual(len(first), len(FAMILIES) * 2)
        for case in first:
            self.assertTrue(case.expected_stdout)
            self.assertIn("func main()", case.hs_source)
            self.assertIn("int main(void)", case.c_source)

    def test_c_oracle_excludes_typed_packed_access_and_signed_arithmetic(self) -> None:
        packed = next(case for case in generate_cases(4, 1) if case.family == "packed-layout")
        self.assertIn("uint8_t packed[7]", packed.c_source)
        self.assertIn("memcpy", packed.c_source)
        self.assertNotIn("struct ", packed.c_source)
        self.assertNotIn(" int32_t", packed.c_source)
        self.assertNotIn("(int)", packed.c_source)

    def test_injected_reference_mismatch_is_reported_with_a_replayable_artifact(self) -> None:
        case = generate_cases(9, 1)[0]
        reference = ProcessResult(("reference",), 0, case.expected_stdout, "", False)
        injected_hsc = ProcessResult(("hsc",), 0, "injected mismatch\n", "", False)
        self.assertEqual(observable_mismatches(reference, injected_hsc), ["stdout"])

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            failures = _write_comparison(
                root,
                case,
                {"reference": reference, "hsc-O0": injected_hsc, "hsc-O2": reference},
            )
            self.assertTrue(any("hsc-O0: stdout" in item for item in failures))
            report = json.loads((root / "comparison.json").read_text(encoding="utf-8"))
            self.assertEqual(report["mismatches"]["hsc-O0"], ["stdout"])
            self.assertEqual(report["runs"]["hsc-O0"]["stdout"], "injected mismatch\n")


if __name__ == "__main__":
    unittest.main()
