#!/usr/bin/env python3
"""Fail-closed tests for the evidence map's identity and freshness checks."""

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("standard_evidence", ROOT / "tools/standard_evidence.py")
assert SPEC and SPEC.loader
evidence = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(evidence)


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        files = {
            "Standard.md": "# Standard\n## 1. Introduction\n### 1.1 Contract\n### 1.1.1 Detail\n",
            "CMakeLists.txt": """  add_executable(hsc_unit_tests
    tests/unit.cpp
  )
  add_test(NAME hsc_runtime COMMAND python ${PROJECT_SOURCE_DIR}/tests/support/harness/run_tests.py --suite sample)
  add_test(NAME hsc_compile_case COMMAND hsc ${PROJECT_SOURCE_DIR}/tests/case.hs -o case)
  add_test(NAME hsc_run_case COMMAND case)
  set_tests_properties(hsc_compile_case PROPERTIES FIXTURES_SETUP case_executable)
  set_tests_properties(hsc_run_case PROPERTIES FIXTURES_REQUIRED case_executable)
""",
            "src/impl.cpp": "void implement() {}\n",
            "tests/support/harness/run_tests.py": "# test runner\n",
            "tests/unit.cpp": "HS_TEST(Unit_One) {}\nHS_TEST(Unit_Two) {}\n",
            "tests/case.hs": "func main() {}\n",
            "tests/support/harness/manifests/sample.json": json.dumps({"suite": "sample", "tests": [{"name": "case", "source": "../../../case.hs"}]}),
            "stdlib/StandardLibraryManifest.json": json.dumps({"headers": [{"id": "Stdlib", "file": "stdlib.hsh"}],
                "defaultTestOwners": ["tests/unit.cpp"], "builtins": [{"name": "api", "visibility": "Public",
                "standardSection": "1.1", "header": "Stdlib", "provider": "Semantic", "implementationSymbol": "sema:api"}]}),
        }
        for path, content in files.items():
            self.write(path, content)
        self.crosswalk = {"version": 1, "witnesses": [{"id": "one", "sections": ["1.1"], "claim": "One assertion.",
            "limitation": "Only this assertion.", "implementation": [{"path": "src/impl.cpp", "anchor": "void implement()"}],
            "evidence": [{"kind": "IR", "path": "tests/unit.cpp", "test": "Unit_One"},
                         {"kind": "runtime", "path": "tests/support/harness/manifests/sample.json", "ctest": "hsc_runtime",
                          "cases": ["case"], "limitation": "Host only."}]}]}
        self.refresh_review()

    def write(self, path, content):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8")

    def save(self):
        self.write(evidence.CROSSWALK, json.dumps(self.crosswalk))

    def refresh_review(self):
        self.crosswalk["reviewed_sha256"] = {p: evidence.digest(self.root / p) for p in
            ("Standard.md", "CMakeLists.txt", "tests/support/harness/run_tests.py", "src/impl.cpp", "tests/unit.cpp", "tests/support/harness/manifests/sample.json", "tests/case.hs")}
        self.save()

    def check_error(self, message):
        self.save()
        with self.assertRaisesRegex(ValueError, message):
            evidence.build(self.root)

    def test_inventory_preserves_mapping_and_execution_boundaries(self):
        inventory, crosswalk = evidence.build(self.root)
        self.assertEqual([s["mapping_status"] for s in inventory["sections"]], ["unmapped", "inspected", "unmapped"])
        self.assertEqual(inventory["sections"][2]["parent"], "1.1")
        self.assertEqual(inventory["apis"][0]["mapping_status"], "declared")
        self.assertEqual(inventory["apis"][0]["evidence_kind"], "structural")
        self.assertEqual(inventory["witnesses"][0]["execution_status"], "not_recorded")
        self.assertEqual(evidence.render(inventory, crosswalk, self.root), evidence.render(inventory, crosswalk, self.root))

    def test_fences_do_not_create_headings(self):
        result = evidence.headings("## 1. Real\n```hs\n## 2. Fake\n```\n~~~\n## 3. Fake\n~~~\n### 1.1 Child\n")
        self.assertEqual([x["section"] for x in result], ["1", "1.1"])
        result = evidence.headings("## 1. Real\n````hs\n```\n## 2. Fake\n````\n### 1.1 Child\n")
        self.assertEqual([x["section"] for x in result], ["1", "1.1"])

    def test_duplicate_and_parentless_sections_fail(self):
        for text in ("## 1. One\n## 1. Again", "### 1.1 Child"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                evidence.headings(text)

    def test_unknown_section_fails(self):
        self.crosswalk["witnesses"][0]["sections"] = ["99.1"]
        self.check_error("unknown Standard")

    def test_duplicate_witness_fails(self):
        self.crosswalk["witnesses"].append(copy.deepcopy(self.crosswalk["witnesses"][0]))
        self.check_error("duplicate witness")

    def test_empty_api_owners_inherit_manifest_defaults(self):
        path = "stdlib/StandardLibraryManifest.json"
        manifest = json.loads((self.root / path).read_text())
        manifest["builtins"][0]["testOwners"] = []
        self.write(path, json.dumps(manifest))
        inventory, _ = evidence.build(self.root)
        self.assertEqual(inventory["apis"][0]["test_owners"], ["tests/unit.cpp"])
        self.assertTrue(inventory["apis"][0]["owners_inherited"])

    def test_missing_or_duplicate_anchor_fails(self):
        for text in ("gone", "void implement() void implement()"):
            with self.subTest(text=text):
                self.write("src/impl.cpp", text)
                self.check_error("anchor must occur once")

    def test_unit_typo_and_wrong_file_fail(self):
        item = self.crosswalk["witnesses"][0]["evidence"][0]
        item["test"] = "Unit_On"
        self.check_error("missing exact HS_TEST")
        item.update(test="Unit_One", path="src/impl.cpp")
        self.check_error("missing exact HS_TEST")

    def test_unit_substring_fanout_fails(self):
        self.write("tests/unit.cpp", "HS_TEST(Unit_One) {}\nHS_TEST(Unit_OneExtra) {}\n")
        self.check_error("substring filter")

    def test_unregistered_source_fails(self):
        self.write("CMakeLists.txt", "add_executable(hsc_unit_tests\n  )\n")
        self.check_error("no registered HS_TEST")

    def test_unit_evidence_cannot_claim_runtime(self):
        self.crosswalk["witnesses"][0]["evidence"][0]["kind"] = "runtime"
        self.check_error("cannot be promoted")

    def test_unknown_evidence_kind_fails(self):
        self.crosswalk["witnesses"][0]["evidence"][0]["kind"] = "conformant"
        self.check_error("invalid evidence kind")

    def test_deleted_runtime_case_fails(self):
        self.crosswalk["witnesses"][0]["evidence"][1]["cases"] = ["typo"]
        self.check_error("missing runtime case")

    def test_wrong_ctest_or_suite_fails(self):
        item = self.crosswalk["witnesses"][0]["evidence"][1]
        item["ctest"] = "missing"
        self.check_error("literal add_test")
        item["ctest"] = "hsc_run_case"
        self.check_error("does not invoke the harness runner")

    def test_manifest_path_must_be_selected_by_the_real_runner(self):
        self.write("tests/alias.json", (self.root / "tests/support/harness/manifests/sample.json").read_text())
        self.crosswalk["witnesses"][0]["evidence"][1]["path"] = "tests/alias.json"
        self.check_error("not the suite-selected path")

    def test_runtime_fixture_cannot_escape_root(self):
        self.write("tests/support/harness/manifests/sample.json", json.dumps({"suite": "sample", "tests": [{"name": "case", "source": "../../../../../outside.hs"}]}))
        with self.assertRaises(ValueError):
            evidence.build(self.root)

    def test_source_paths_must_be_relative_and_confined(self):
        for path in ("../outside", str(self.root / "src/impl.cpp"), "src/../src/impl.cpp"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                evidence.source(self.root, path)

    def test_review_freshness_is_not_refreshed_by_generation(self):
        before = copy.deepcopy(self.crosswalk)
        self.write("src/impl.cpp", "void implement() { changed(); }\n")
        self.check_error("review stale")
        self.assertEqual(self.crosswalk, before)

    def test_native_line_endings_do_not_invalidate_review(self):
        path = self.root / "Standard.md"
        before = evidence.digest(path)
        path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
        self.assertEqual(evidence.digest(path), before)
        evidence.build(self.root)

    def test_review_file_set_must_be_exact(self):
        self.crosswalk["reviewed_sha256"]["unused"] = "0" * 64
        self.check_error("exactly the reviewed")

    def test_standalone_runtime_requires_compile_fixture(self):
        item = self.crosswalk["witnesses"][0]["evidence"][1]
        item.clear()
        item.update(kind="runtime", path="tests/case.hs", ctest="hsc_run_case", compile_ctest="hsc_compile_case",
                    fixture="case_executable", limitation="Default host mode only.")
        del self.crosswalk["reviewed_sha256"]["tests/support/harness/manifests/sample.json"]
        self.save()
        evidence.build(self.root)
        item["fixture"] = "wrong"
        self.check_error("missing CTest fixture")
        item["fixture"] = "case_executable"
        self.write("CMakeLists.txt", (self.root / "CMakeLists.txt").read_text().replace(
            "hsc_run_case COMMAND case", "hsc_run_case COMMAND cmake -E true"))
        self.check_error("does not execute the compiled output")

    def test_runner_rejects_zero_fanout_failure_and_stderr(self):
        binary = self.root / "binary"
        binary.write_bytes(b"test identity")
        inventory, crosswalk = evidence.build(self.root)
        for output, code, stderr, status in (
            ("1/1 PASS\n", 0, "", "passed"), ("1/1 PASS\r\n", 0, "", "passed"),
            ("0/0 PASS\n", 0, "", "failed"), ("2/2 PASS\n", 0, "", "failed"),
            ("1/1 PASS\n", 1, "", "failed"), ("1/1 PASS\n", 0, "warning", "failed"),
            ("[FAIL] Unit_One: x\n0/1 PASS, 1 FAIL\n", 1, "", "failed")):
            result = subprocess.CompletedProcess([], code, output, stderr)
            with self.subTest(output=output, code=code, stderr=stderr), patch.object(evidence.platform, "platform", return_value="test-host"), patch.object(evidence.subprocess, "run", return_value=result) as run:
                report = evidence.run_units(binary, crosswalk, inventory)
                self.assertEqual(report["results"][0]["execution_status"], status)
                self.assertEqual(run.call_args.args[0], [str(binary.resolve()), "Unit_One"])
                self.assertEqual(report["inputs_sha256"], inventory["inputs_sha256"])
                self.assertTrue(report["binary_sha256"])

    def test_runner_timeout_and_launch_failure_are_failed(self):
        binary = self.root / "binary"
        binary.write_bytes(b"test")
        inventory, crosswalk = evidence.build(self.root)
        for error in (subprocess.TimeoutExpired("binary", 120), OSError("cannot execute")):
            with self.subTest(error=error), patch.object(evidence.platform, "platform", return_value="test-host"), patch.object(evidence.subprocess, "run", side_effect=error):
                self.assertEqual(evidence.run_units(binary, crosswalk, inventory)["results"][0]["execution_status"], "failed")

    def test_repository_crosswalk_and_generated_outputs_are_current(self):
        inventory, crosswalk = evidence.build(ROOT)
        self.assertEqual((ROOT / evidence.INVENTORY).read_text(encoding="utf-8"), json.dumps(inventory, indent=2, ensure_ascii=False) + "\n")
        self.assertEqual((ROOT / evidence.REPORT).read_text(encoding="utf-8"), evidence.render(inventory, crosswalk, ROOT))

    def test_evidence_checks_are_in_selective_native_platform_gates(self):
        workflow = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        for name in ("hsc_standard_evidence_check", "hsc_standard_evidence_tests", "hsc_standard_evidence_units"):
            self.assertIn("Python3_EXECUTABLE", evidence.ctest_block(cmake, name))
            for start, end in (
                ("      - name: Run aarch64 front-end and executable regression", "      - name: Verify native preprocessor macro"),
                ("      - name: Run portable Windows CTest cases", "      - name: Assemble full and slim packages"),
            ):
                block = workflow.split(start, 1)[1].split(end, 1)[0]
                patterns = re.findall(r"-DTEST_REGEX=([^']+)", block)
                self.assertTrue(any(re.fullmatch(pattern, name) for pattern in patterns), name)


if __name__ == "__main__":
    unittest.main()
