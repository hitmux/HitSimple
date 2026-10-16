#!/usr/bin/env python3
"""Verify optional ASCII contracts, the exhaustive oracle, and generation."""

from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
NAMES = ("lower", "upper", "xdigit", "blank", "control", "print", "graph", "punct")
DOCUMENT = "docs/ascii-character-extensions.md"
MANIFEST = json.loads((ROOT / "stdlib/StandardLibraryManifest.json").read_text(encoding="utf-8"))
PARSER = argparse.ArgumentParser()
PARSER.add_argument("--manifest-tool", type=Path)
ARGS, UNIT_ARGS = PARSER.parse_known_args()


class AsciiContractTests(unittest.TestCase):
    def test_standard_and_extension_attribution_are_disjoint(self):
        standard = [api for api in MANIFEST["builtins"] if api["standardSection"]]
        extension = [api for api in MANIFEST["builtins"] if api.get("extensionDocument")]
        self.assertEqual(len(standard), 75)
        self.assertEqual([api["name"] for api in extension], ["is_" + name for name in NAMES])
        self.assertEqual(len(standard) + len(extension), len(MANIFEST["builtins"]))
        normative_text = (ROOT / "Standard.md").read_text(encoding="utf-8")
        for api in extension:
            self.assertEqual(api["standardSection"], "")
            self.assertEqual(api["extensionDocument"], DOCUMENT)
            self.assertNotRegex(normative_text, r"\b" + api["name"] + r"\b")
            self.assertEqual(api["parameters"], [["View", "Bytes4", ""]])
            self.assertEqual(api["results"], [["Bool", "Bytes1", "bool"]])
            self.assertEqual(api["provider"], "Intrinsic")
            self.assertEqual(api["referenceProvider"], "CoreHs")
            self.assertEqual(api["sourceModule"], "Ctype")
        self.assertEqual(MANIFEST["sourceModules"], [{"id": "Ctype", "file": "ctype.hs", "dependencies": []}])

    def test_all_256_oracle_bytes_match_independent_ascii_sets(self):
        # Deliberately use explicit ASCII membership strings rather than libc,
        # Unicode is* methods, or the compiler's lowering range expressions.
        lower = set(b"abcdefghijklmnopqrstuvwxyz")
        upper = set(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ")
        hexadecimal = set(b"0123456789ABCDEFabcdef")
        punctuation = set(b"!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~")
        graphical = lower | upper | set(b"0123456789") | punctuation
        controls = set(range(32)) | {127}
        sets = [lower, upper, hexadecimal, {9, 32}, controls, graphical | {32}, graphical, punctuation]
        source = (ROOT / "tests/cases/run/ctype_ascii_extensions.hs").read_text(encoding="utf-8")
        entries = re.findall(r"expected\[(\d+)\] %d= (\d+)", source)
        self.assertEqual([int(index) for index, _ in entries], list(range(256)))
        for index, mask in entries:
            self.assertEqual(int(mask), sum(1 << bit for bit, members in enumerate(sets) if int(index) in members), index)
        self.assertIn('return 1;', source)  # Never return a failure count modulo 256.
        for name in NAMES:
            self.assertIn("is_" + name + "(identity_f128(value))", source)
            self.assertIn("is_" + name + "(resize_bytes(value, count))", source)

    def test_runtime_matrix_requires_all_twelve_successes(self):
        matrix = json.loads((ROOT / "tests/support/harness/manifests/ctype-ascii-extensions.json").read_text(encoding="utf-8"))
        self.assertEqual(matrix["suite"], "ctype-ascii-extensions")
        self.assertEqual(matrix["defaults"]["safety"], ["unchecked", "static-checked", "checked"])
        self.assertEqual(matrix["defaults"]["optimization"], ["O0", "O2"])
        self.assertEqual(matrix["defaults"]["targets"], ["host"])
        self.assertEqual([case["name"] for case in matrix["tests"]], ["optimized", "reference"])
        for case in matrix["tests"]:
            self.assertEqual(case["compile_args"], ["--stdlib-provider=" + case["name"]])
            self.assertEqual(case["source"], "../../../cases/run/ctype_ascii_extensions.hs")
            self.assertEqual(case["expect"], {
                "compile": {"exit_code": 0, "stdout": "", "stderr": ""},
                "run": {"exit_code": 0, "stdout": "", "stderr": ""},
            })
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("NAME hsc_ctype_ascii_extensions", cmake)
        self.assertIn("--suite ctype-ascii-extensions\n      --differential\n      --safety-mode", cmake)
        self.assertIn("add_hsc_shell_test(hsc_installed_ascii_extensions", cmake)

    def test_extensions_are_in_selective_native_platform_gates(self):
        workflow = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        for start, end in (
            ("      - name: Run aarch64 front-end and executable regression", "      - name: Verify native preprocessor macro"),
            ("      - name: Run portable Windows CTest cases", "      - name: Assemble full and slim packages"),
        ):
            block = workflow.split(start, 1)[1].split(end, 1)[0]
            patterns = re.findall(r"-DTEST_REGEX=([^']+)", block)
            for name in ("hsc_ctype_ascii_extensions", "hsc_ascii_ctype_tooling"):
                self.assertTrue(any(re.fullmatch(pattern, name) for pattern in patterns), name)

    def test_editor_grammar_identifies_every_extension_call(self):
        grammar = json.loads((ROOT / "vscode/hitsimple/syntaxes/hitsimple.tmLanguage.json").read_text(encoding="utf-8"))
        def patterns(value):
            if isinstance(value, dict):
                if "match" in value and "is_digit" in value["match"]:
                    yield value["match"]
                for child in value.values():
                    yield from patterns(child)
            elif isinstance(value, list):
                for child in value:
                    yield from patterns(child)
        regex, = list(patterns(grammar))
        for name in NAMES:
            self.assertEqual(re.search(regex, "is_" + name + "(byte)").group(), "is_" + name)
            self.assertIsNone(re.search(regex, "my_is_" + name + "(byte)"))


@unittest.skipUnless(ARGS.manifest_tool, "pass --manifest-tool to exercise generation")
class ManifestGenerationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ascii manifest ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.manifest = copy.deepcopy(MANIFEST)
        # The generator validates existence, so create bounded local fixtures
        # for all owner/document references without depending on a checkout path.
        paths = set(MANIFEST["defaultTestOwners"])
        for api in MANIFEST["builtins"]:
            paths.update(api.get("testOwners", []))
            if api.get("extensionDocument"):
                paths.add(api["extensionDocument"])
        for relative in paths:
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture\n", encoding="utf-8")
        (self.root / "stdlib").mkdir()
        self.extension = next(api for api in self.manifest["builtins"] if api["name"] == "is_lower")

    def generate(self):
        manifest = self.root / "stdlib/manifest.json"
        manifest.write_text(json.dumps(self.manifest), encoding="utf-8")
        return subprocess.run([str(ARGS.manifest_tool.resolve()), "--manifest", str(manifest),
                               "--cpp-out", str(self.root / "generated.cpp"),
                               "--headers-out-dir", str(self.root / "headers")],
                              capture_output=True, text=True, encoding="utf-8", timeout=30)

    def test_valid_manifest_generates_all_extension_declarations_and_metadata(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        header = (self.root / "headers/ctype.hsh").read_text(encoding="utf-8")
        generated = (self.root / "generated.cpp").read_text(encoding="utf-8")
        self.assertEqual(header.count("// Optional extension; see " + DOCUMENT), 8)
        for name in NAMES:
            self.assertIn("extern is_" + name + "(ch as u8) -> bool", header)
            self.assertIn('"__hs_stdlib_ctype_is_' + name + '"', generated)
        self.assertEqual(generated.count('"' + DOCUMENT + '"'), 8)

    def test_legacy_standard_entries_do_not_need_extension_metadata(self):
        self.manifest["builtins"] = [api for api in self.manifest["builtins"] if api["standardSection"]]
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("Optional extension", (self.root / "headers/ctype.hsh").read_text(encoding="utf-8"))

    def test_conflicting_normative_attribution_is_rejected(self):
        self.extension["standardSection"] = "14.7"
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("exactly one Standard section or extension document", result.stderr)

    def test_missing_extension_attribution_is_rejected(self):
        del self.extension["extensionDocument"]
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("exactly one Standard section or extension document", result.stderr)

    def test_invalid_or_missing_extension_documents_are_rejected(self):
        for invalid in ("docs/missing.md", "../outside.md", "docs/../README.md", "README.md", "docs/contract.txt", ""):
            with self.subTest(document=invalid):
                self.extension["extensionDocument"] = invalid
                self.assertNotEqual(self.generate().returncode, 0)

    def test_non_string_extension_documents_are_rejected(self):
        self.extension["extensionDocument"] = False
        result = self.generate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing string field 'extensionDocument'", result.stderr)


if __name__ == "__main__":
    unittest.main(argv=[__file__, *UNIT_ARGS])
