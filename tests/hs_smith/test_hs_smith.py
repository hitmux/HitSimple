#!/usr/bin/env python3
"""Unit tests for HS-Smith models, emitters, contracts, and reducers."""

from __future__ import annotations

import unittest
from pathlib import Path
from unittest.mock import patch

from emitter_c import emit as emit_c
from emitter_hs import emit as emit_hs
from generator import FIRST_PHASE_FEATURES, generate_cases, generate_memory_cases
from interpreter import evaluate
from memory_model import MemoryError, MemoryModel
from mutation import run_mutations, score
from oracle import Failure, FailureSignature, OracleLevel, deduplicate
from reducer import reduce_source
from sandbox import SandboxPolicy, detect, wrap


class HsSmithTests(unittest.TestCase):
    def test_generation_is_deterministic_and_covers_all_fixed_width_templates(self) -> None:
        first = generate_cases(17, 8)
        second = generate_cases(17, 8)
        self.assertEqual(first, second)
        self.assertEqual(
            [case.template.name for case in first],
            ["u8", "i8", "u16", "i16", "u32", "i32", "u64", "i64"],
        )
        for case in first:
            self.assertEqual(set(case.feature_tags), set(FIRST_PHASE_FEATURES))
            self.assertTrue(evaluate(case.program).endswith("\n"))

    def test_emitters_represent_the_same_structured_features(self) -> None:
        case = generate_cases(9, 1)[0]
        hs_source = emit_hs(case.program)
        c_source = emit_c(case.program)
        self.assertIn("template SmithRecord", hs_source)
        self.assertIn("raw[1:3]", hs_source)
        self.assertIn("static counter as u32", hs_source)
        self.assertIn("struct SmithRecord", c_source)
        self.assertIn("#pragma pack(push, 1)", c_source)

    def test_memory_model_realloc_invalidates_old_alias_and_preserves_bytes(self) -> None:
        model = MemoryModel()
        pointer = model.calloc(1, 2)
        alias = pointer
        model.store(pointer, 17)
        pointer = model.realloc(pointer, 4)
        self.assertEqual(model.load(pointer), 17)
        with self.assertRaisesRegex(MemoryError, "stale-address"):
            model.load(alias)

    def test_memory_profiles_record_checked_contract_fields(self) -> None:
        safe, invalid = generate_memory_cases(17)
        self.assertEqual(safe.detectability, "required")
        self.assertEqual(safe.address_origin, "tracked-dynamic-base")
        self.assertEqual({case.expected_error for case in invalid}, {"use-after-free", "invalid-free"})
        self.assertTrue(all("single-rule-invalid" in case.feature_tags for case in invalid))

    def test_failure_signatures_deduplicate_without_using_source_hash(self) -> None:
        signature = FailureSignature("run", "oracle-stdout", OracleLevel.A, ("u32",))
        grouped = deduplicate((Failure(signature, "first source"), Failure(signature, "second source")))
        self.assertEqual(len(grouped), 1)
        self.assertEqual(len(next(iter(grouped.values()))), 2)

    def test_reducer_removes_fragments_and_shrinks_literals(self) -> None:
        fragments = tuple("noise " + str(index) + "\n" for index in range(40))
        source = "keep 99\n" + "".join(fragments) + "end\n"
        result = reduce_source(source, lambda item: "keep" in item and "end" in item, fragments, max_attempts=48)
        self.assertNotIn("noise", result.source)
        self.assertLessEqual(len(result.source.splitlines()), 3)
        self.assertGreater(result.accepted_transformations, 0)

    def test_mutation_gate_kills_every_representative_first_phase_mutant(self) -> None:
        results = run_mutations()
        self.assertGreaterEqual(score(results), 0.8)
        self.assertTrue(all(result.killed for result in results))

    def test_network_isolation_uses_sudo_fallback_without_root_artifacts(self) -> None:
        with (
            patch("sandbox.shutil.which", side_effect=lambda name: "/usr/bin/" + name),
            patch("sandbox.subprocess.run", side_effect=[
                type("Result", (), {"returncode": 1})(),
                type("Result", (), {"returncode": 0})(),
            ]) as run,
            patch("sandbox._uid_process_count", return_value=9),
        ):
            plan = detect(Path("/workspace"))

        self.assertTrue(plan.enabled)
        self.assertTrue(plan.network_isolated)
        self.assertEqual(plan.reason, "bubblewrap network namespace via sudo fallback")
        self.assertEqual(plan.existing_uid_processes, 9)
        self.assertEqual(plan.command_prefix[:2], ("/usr/bin/sudo", "-n"))
        self.assertIn("--reuid", plan.command_prefix)
        self.assertIn("--regid", plan.command_prefix)
        self.assertIn("--no-new-privs", plan.command_prefix)
        with patch("sandbox.shutil.which", return_value="/usr/bin/prlimit"):
            command = wrap(plan, ("run",), SandboxPolicy())
        self.assertLess(command.index("/usr/bin/setpriv"), command.index("/usr/bin/prlimit"))
        self.assertIn("--nproc=25", command)
        self.assertEqual(run.call_count, 2)


if __name__ == "__main__":
    unittest.main()
