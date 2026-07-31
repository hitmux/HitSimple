#!/usr/bin/env python3
"""Unit tests for the mutation operator format, driver, and score gate."""

from __future__ import annotations

import os
from pathlib import Path
import tempfile
import unittest

from mutation_operators import (
    CATEGORIES,
    Operator,
    OperatorError,
    apply_operator,
    check_anchors,
    load_operators,
    parse_operator,
    revert_operator,
    select_operators,
)
from run_mutations import (
    EQUIVALENT,
    INVALID_BUILD,
    KILLED,
    SURVIVED,
    TIMEOUT,
    TestOutcome,
    classify,
    parse_arguments,
    reset_build_tree,
    score_by_category,
    sync_tree,
    thresholds_met,
)


SOURCE_ROOT = Path(__file__).resolve().parents[2]
OPERATOR_ROOT = Path(__file__).resolve().parent / "operators"

SAMPLE = """# name: sample
# category: core
# summary: a sample operator

--- edit: a.c ---
--- anchor ---
  return 1;
--- replacement ---
  return 0;
"""


def _operator(name: str, category: str, expected: str = "killed") -> Operator:
    return Operator(name, category, "summary", expected, "rationale", ())


class OperatorFormatTests(unittest.TestCase):
    def test_parses_headers_and_a_single_edit(self) -> None:
        operator = parse_operator(SAMPLE, Path("sample.mutant"))
        self.assertEqual(operator.name, "sample")
        self.assertEqual(operator.category, "core")
        self.assertEqual(operator.expected, "killed")
        self.assertEqual(len(operator.edits), 1)
        self.assertEqual(operator.edits[0].path, "a.c")
        self.assertEqual(operator.edits[0].anchor, "  return 1;\n")
        self.assertEqual(operator.edits[0].replacement, "  return 0;\n")
        self.assertEqual(operator.edits[0].occurrences, 1)

    def test_reads_an_occurrence_count_and_an_empty_replacement(self) -> None:
        text = SAMPLE.replace("--- edit: a.c ---", "--- edit: a.c occurrences: 3 ---")
        text = text[: text.index("--- replacement ---")] + "--- replacement ---\n"
        operator = parse_operator(text, Path("sample.mutant"))
        self.assertEqual(operator.edits[0].occurrences, 3)
        self.assertEqual(operator.edits[0].replacement, "")

    def test_rejects_a_name_that_does_not_match_the_file(self) -> None:
        with self.assertRaises(OperatorError):
            parse_operator(SAMPLE, Path("other.mutant"))

    def test_rejects_an_unknown_category(self) -> None:
        with self.assertRaises(OperatorError):
            parse_operator(SAMPLE.replace("category: core", "category: parser"), Path("sample.mutant"))

    def test_rejects_an_equivalent_operator_without_a_rationale(self) -> None:
        text = SAMPLE.replace("# summary:", "# expected: equivalent\n# summary:")
        with self.assertRaises(OperatorError):
            parse_operator(text, Path("sample.mutant"))

    def test_rejects_an_operator_without_an_edit(self) -> None:
        with self.assertRaises(OperatorError):
            parse_operator("# name: sample\n# category: core\n# summary: s\n", Path("sample.mutant"))


class OperatorApplicationTests(unittest.TestCase):
    def test_applies_and_reverts_without_touching_other_content(self) -> None:
        operator = parse_operator(SAMPLE, Path("sample.mutant"))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "a.c").write_text("int f(void) {\n  return 1;\n}\n", encoding="utf-8")
            originals = apply_operator(operator, root)
            self.assertEqual((root / "a.c").read_text(encoding="utf-8"), "int f(void) {\n  return 0;\n}\n")
            revert_operator(originals, root)
            self.assertEqual((root / "a.c").read_text(encoding="utf-8"), "int f(void) {\n  return 1;\n}\n")

    def test_rejects_an_anchor_that_matches_a_different_number_of_times(self) -> None:
        operator = parse_operator(SAMPLE, Path("sample.mutant"))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "a.c").write_text("  return 1;\n  return 1;\n", encoding="utf-8")
            with self.assertRaises(OperatorError):
                check_anchors(operator, root)

    def test_replaces_the_file_instead_of_writing_through_a_hard_link(self) -> None:
        operator = parse_operator(SAMPLE, Path("sample.mutant"))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = root / "original.c"
            original.write_text("  return 1;\n", encoding="utf-8")
            (root / "a.c").hardlink_to(original)
            apply_operator(operator, root)
            self.assertEqual(original.read_text(encoding="utf-8"), "  return 1;\n")


class RepositoryOperatorTests(unittest.TestCase):
    """The shipped operators must stay aligned with the current implementation."""

    def setUp(self) -> None:
        self.operators = load_operators(OPERATOR_ROOT)

    @unittest.skipIf(os.environ.get("HSC_MUTATION_ACTIVE") == "1",
                     "a mutation campaign intentionally changes one anchor")
    def test_every_operator_anchor_still_matches_the_source_tree(self) -> None:
        for operator in self.operators:
            with self.subTest(operator=operator.name):
                check_anchors(operator, SOURCE_ROOT)

    def test_the_operator_set_covers_both_categories_and_the_documented_defects(self) -> None:
        categories = {operator.category for operator in self.operators}
        self.assertEqual(categories, set(CATEGORIES))
        self.assertGreaterEqual(len(self.operators), 12)
        names = {operator.name for operator in self.operators}
        for required in (
            "core-checked-bounds-guard-removed",
            "core-checked-bounds-guard-inverted",
            "core-checked-lifetime-state-not-updated",
            "core-checked-realloc-keeps-stale-address",
            "core-checked-interior-free-accepted",
            "core-boolean-test-lowest-byte-only",
            "core-integer-width-truncated-to-32",
            "codegen-view-load-always-sign-extends",
            "codegen-view-load-always-zero-extends",
            "codegen-packed-alignment-overstated",
        ):
            self.assertIn(required, names)

    def test_selection_rejects_an_unknown_operator(self) -> None:
        with self.assertRaises(OperatorError):
            select_operators(self.operators, ["does-not-exist"])

    def test_selection_preserves_the_requested_order(self) -> None:
        selected = select_operators(self.operators, ["codegen-packed-alignment-overstated", "core-checked-bounds-guard-removed"])
        self.assertEqual([operator.name for operator in selected], ["codegen-packed-alignment-overstated", "core-checked-bounds-guard-removed"])


class ClassificationTests(unittest.TestCase):
    def test_a_failing_killer_suite_kills_the_mutant(self) -> None:
        outcome = TestOutcome(377, 4, ("hsc_run_checked",), False, 8)
        self.assertEqual(classify(_operator("m", "core"), outcome)[0], KILLED)

    def test_a_passing_killer_suite_leaves_a_survivor(self) -> None:
        outcome = TestOutcome(377, 0, (), False, 0)
        self.assertEqual(classify(_operator("m", "core"), outcome)[0], SURVIVED)

    def test_a_declared_equivalent_operator_is_not_scored_as_a_survivor(self) -> None:
        outcome = TestOutcome(377, 0, (), False, 0)
        classification, reason = classify(_operator("m", "codegen", EQUIVALENT), outcome)
        self.assertEqual(classification, EQUIVALENT)
        self.assertEqual(reason, "rationale")

    def test_a_declared_equivalent_operator_is_still_killed_when_a_test_fails(self) -> None:
        outcome = TestOutcome(377, 1, ("hsc_unit_tests",), False, 8)
        self.assertEqual(classify(_operator("m", "codegen", EQUIVALENT), outcome)[0], KILLED)

    def test_a_timeout_and_an_empty_suite_are_reported_separately(self) -> None:
        self.assertEqual(classify(_operator("m", "core"), TestOutcome(0, 0, (), True, 124))[0], TIMEOUT)
        self.assertEqual(classify(_operator("m", "core"), TestOutcome(0, 0, (), False, 0))[0], INVALID_BUILD)


class ScoreTests(unittest.TestCase):
    def _results(self, classifications: dict[str, list[str]]) -> list:
        from run_mutations import MutantResult

        return [
            MutantResult(name, category, "summary", classification)
            for category, entries in classifications.items()
            for name, classification in zip(("a", "b", "c", "d", "e"), entries)
        ]

    def test_equivalent_and_invalid_build_stay_out_of_the_denominator(self) -> None:
        results = self._results({"core": [KILLED, KILLED, EQUIVALENT, INVALID_BUILD]})
        scores = score_by_category(results)
        self.assertEqual(scores["core"].scored, 2)
        self.assertEqual(scores["core"].score, 1.0)

    def test_the_gate_reports_a_category_below_its_threshold_and_accepts_an_exact_match(self) -> None:
        results = self._results({"core": [KILLED, SURVIVED], "codegen": [KILLED, KILLED, KILLED, KILLED, SURVIVED]})
        scores = score_by_category(results)
        self.assertEqual(scores["codegen"].score, 0.8)
        met, violations = thresholds_met(scores, 0.9, 0.8)
        self.assertFalse(met)
        self.assertEqual(len(violations), 1)
        self.assertIn("core", violations[0])

    def test_a_category_without_a_scored_mutant_does_not_fail_the_gate(self) -> None:
        results = self._results({"core": [EQUIVALENT]})
        met, violations = thresholds_met(score_by_category(results), 0.9, 0.8)
        self.assertTrue(met)
        self.assertEqual(violations, ())


class SyncTests(unittest.TestCase):
    def test_the_scratch_tree_may_not_be_the_source_tree(self) -> None:
        with self.assertRaises(OperatorError):
            sync_tree(SOURCE_ROOT, SOURCE_ROOT, ())

    def test_stale_files_are_pruned_from_the_scratch_tree(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            tree = root / "tree"
            (source / "src").mkdir(parents=True)
            (source / "src" / "keep.c").write_text("keep\n", encoding="utf-8")
            (tree / "src").mkdir(parents=True)
            (tree / "src" / "stale.c").write_text("stale\n", encoding="utf-8")
            sync_tree(source, tree, ("src/keep.c",))
            self.assertTrue((tree / "src" / "keep.c").is_file())
            self.assertFalse((tree / "src" / "stale.c").exists())

    def test_the_mutation_build_tree_is_discarded_between_campaigns(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory) / "build"
            build.mkdir()
            (build / "stale-mutant.o").write_text("mutant", encoding="utf-8")
            reset_build_tree(build)
            self.assertFalse(build.exists())


class ArgumentTests(unittest.TestCase):
    def test_defaults_match_the_documented_stage_gates(self) -> None:
        arguments = parse_arguments([])
        self.assertEqual(arguments.core_threshold, 0.9)
        self.assertEqual(arguments.codegen_threshold, 0.8)
        self.assertEqual(arguments.source_root, SOURCE_ROOT)

    def test_a_threshold_outside_the_unit_interval_is_rejected(self) -> None:
        with self.assertRaises(SystemExit):
            parse_arguments(["--core-threshold", "1.5"])


if __name__ == "__main__":
    unittest.main()
