#!/usr/bin/env python3
"""Contract tests for the tutorial entry points; native execution is a CTest gate."""

import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/support/harness"))
import run_tests as harness

spec = importlib.util.spec_from_file_location("practical_examples", ROOT / "examples/practical/run.py")
examples = importlib.util.module_from_spec(spec)
spec.loader.exec_module(examples)


class PracticalExampleTests(unittest.TestCase):
    def test_exactly_six_named_examples(self):
        self.assertEqual(len(examples.EXAMPLES), 6)
        self.assertEqual(len(set(examples.EXAMPLES)), 6)

    def test_manifests_use_existing_harness_and_real_sources(self):
        with tempfile.TemporaryDirectory() as temporary:
            for example in examples.CLI_EXAMPLES:
                with self.subTest(example=example):
                    path = Path(temporary) / (example + ".json")
                    path.write_text(json.dumps(examples.manifest_for(example)))
                    cases = harness.load_manifest(path)
                    self.assertTrue(cases)
                    self.assertTrue(all(case.source.is_file() for case in cases))

    def test_rust_requires_bonus_and_release_staticlib(self):
        case = examples.manifest_for("rust-staticlib")["tests"][0]
        args = case["compile_args"]
        self.assertEqual(args[args.index("--cargo-features") + 1], "bonus")
        self.assertEqual(args[args.index("--cargo-profile") + 1], "release")
        manifest = Path(args[args.index("--cargo-manifest") + 1])
        self.assertIn('crate-type = ["staticlib"]', manifest.read_text())

    def test_checked_error_has_no_unchecked_execution(self):
        case = examples.manifest_for("checked-error")["tests"][0]
        self.assertEqual(case["safety"], "checked")
        self.assertEqual(set(case["expect_by_mode"]), {"checked"})
        oracle = case["expect_by_mode"]["checked"]["run"]
        self.assertEqual(oracle["exit_code"], 120)
        self.assertRegex("hitsimple runtime error: invalid checked store at /tmp/checked_oob.hs:10:5\n",
                         oracle["stderr_regex"])

    def test_ctype_providers_share_exact_observable_oracle(self):
        cases = examples.manifest_for("ctype-providers")["tests"]
        self.assertEqual([case["name"] for case in cases], ["optimized", "reference"])
        self.assertEqual(cases[0]["source"], cases[1]["source"])
        self.assertEqual(cases[0]["expect_by_mode"], cases[1]["expect_by_mode"])
        for case in cases:
            self.assertEqual(case["safety"], ["unchecked", "static-checked", "checked"])
            for expectation in case["expect_by_mode"].values():
                self.assertEqual(expectation["run"], examples.successful_process())

    def test_guide_links_resolve_and_entries_match_cli(self):
        directory = ROOT / "examples/practical"
        guide = (directory / "README.md").read_text()
        for target in re.findall(r"\]\(([^)#]+)(?:#[^)]*)?\)", guide):
            self.assertTrue((directory / target).is_file(), target)
        for example in examples.EXAMPLES:
            self.assertIn("run.py " + example + " --hsc build/hsc", guide)

    def test_debug_reuses_canonical_source(self):
        runner = (ROOT / "vscode/hitsimple/test/run-extension-host.mjs").read_text()
        self.assertIn('path.join(repoRoot, "tests/cases/run/debug_info.hs")', runner)
        source = (ROOT / "tests/cases/run/debug_info.hs").read_text().splitlines()
        self.assertIn('printf("%d\\n", value)', source[5])
        host = (ROOT / "vscode/hitsimple/test/extension-host/index.js").read_text()
        self.assertIn("new vscode.Position(breakpointLine, 0)", host)
        self.assertIn('"hitsimple.debugCurrentFile"', host)

    def test_unsupported_host_is_an_error(self):
        with patch.object(examples.platform, "system", return_value="Darwin"):
            with self.assertRaisesRegex(ValueError, "native Linux"):
                examples.main(["cli"])

    def test_missing_compiler_is_an_error(self):
        with patch.object(examples.platform, "system", return_value="Linux"), \
             patch.object(examples.platform, "machine", return_value="x86_64"):
            with self.assertRaisesRegex(ValueError, "executable compiler"):
                examples.main(["cli", "--hsc", "/missing-hitsimple-test-compiler"])

    @unittest.skipUnless(sys.platform == "linux", "launcher intentionally supports native Linux")
    def test_checked_oracle_rejects_wrong_exit_and_wrong_diagnostic(self):
        # Exercise the actual documented entry and existing runtime harness.
        # This fake compiler only tests the runner, not HitSimple semantics.
        with tempfile.TemporaryDirectory(prefix="practical example ") as temporary:
            directory = Path(temporary)
            compiler = directory / "fake hsc"
            compiler.write_text(
                "#!" + sys.executable + "\n"
                "import os, pathlib, sys\n"
                "assert '--checked' in sys.argv and '--unchecked' not in sys.argv\n"
                "source = next(arg for arg in sys.argv if arg.endswith('checked_oob.hs'))\n"
                "diagnostic = 'hitsimple runtime error: invalid checked store at ' + source + ':10:5\\n'\n"
                "if os.environ.get('WRONG_DIAGNOSTIC'): diagnostic = 'different failure\\n'\n"
                "output = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
                "output.write_text('#!' + sys.executable + '\\nimport sys\\nsys.stderr.write(' + repr(diagnostic) + ')\\nsys.exit(' + os.environ.get('FAKE_EXIT', '120') + ')\\n')\n"
                "output.chmod(0o755)\n"
            )
            compiler.chmod(0o755)
            command = [sys.executable, str(ROOT / "examples/practical/run.py"),
                       "checked-error", "--hsc", str(compiler),
                       "--artifacts", str(directory / "artifacts")]
            for overrides, expected in [({}, 0), ({"FAKE_EXIT": "1"}, 1),
                                        ({"FAKE_EXIT": "0"}, 1), ({"WRONG_DIAGNOSTIC": "1"}, 1)]:
                with self.subTest(overrides=overrides):
                    result = subprocess.run(command, env={**os.environ, **overrides},
                                            text=True, capture_output=True, timeout=30)
                    self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                    self.assertEqual("PASS checked-error" in result.stdout, expected == 0)


if __name__ == "__main__":
    unittest.main()
