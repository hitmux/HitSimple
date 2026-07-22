#!/usr/bin/env python3
"""Unit tests for the manifest harness, using a portable fake hsc driver."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


RUNNER = Path(__file__).with_name("run_tests.py")


def write_fake_hsc(directory: Path) -> Path:
    compiler = directory / "fake_hsc.py"
    compiler.write_text(
        """#!/usr/bin/env python3
import pathlib
import sys

arguments = sys.argv[1:]
output = pathlib.Path(arguments[arguments.index('-o') + 1])
source = pathlib.Path(arguments[-3])
if 'compile-fail' in source.read_text(encoding='utf-8'):
    sys.stderr.write('compile failure\\n')
    raise SystemExit(7)
program = \"#!/usr/bin/env python3\\nimport sys\\n\"
if 'sleep' in source.read_text(encoding='utf-8'):
    program += \"import time\\ntime.sleep(1)\\n\"
if 'optimization-diverge' in source.read_text(encoding='utf-8') and '-O2' in arguments:
    program += \"sys.stdout.write('different\\\\n')\\n\"
else:
    program += \"sys.stdout.write('ok\\\\n')\\n\"
output.write_text(program, encoding='utf-8')
output.chmod(0o755)
""",
        encoding="utf-8",
    )
    if os.name == "nt":
        launcher = directory / "fake_hsc.cmd"
        launcher.write_text(
            "@echo off\r\n\"" + sys.executable + "\" \"" + str(compiler) + "\" %*\r\n",
            encoding="utf-8",
        )
        return launcher
    compiler.chmod(0o755)
    return compiler


def write_manifest(directory: Path, source: Path, timeout_seconds: float = 2.0) -> Path:
    manifest = directory / "runtime.json"
    manifest.write_text(
        json.dumps(
            {
                "version": 1,
                "suite": "runtime",
                "defaults": {
                    "targets": ["host"],
                    "safety": ["unchecked", "checked"],
                    "optimization": ["O0", "O2"],
                    "timeout_seconds": timeout_seconds,
                },
                "tests": [
                    {
                        "name": "matrix",
                        "source": source.name,
                        "expect": {
                            "compile": {"exit_code": 0, "stdout": "", "stderr": ""},
                            "run": {"exit_code": 0, "stdout": "ok\n", "stderr": ""},
                        },
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    return manifest


class HarnessTests(unittest.TestCase):
    def run_harness(
        self, hsc: Path, manifest: Path, artifacts: Path, differential: bool = False
    ) -> subprocess.CompletedProcess[str]:
        command = [
            sys.executable,
            str(RUNNER),
            "--hsc",
            str(hsc),
            "--manifest",
            str(manifest),
            "--artifacts",
            str(artifacts),
        ]
        if differential:
            command.append("--differential")
        return subprocess.run(
            command,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def test_expands_matrix_and_saves_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / "case.hs"
            source.write_text("ok", encoding="utf-8")
            result = self.run_harness(
                write_fake_hsc(directory),
                write_manifest(directory, source),
                directory / "artifacts",
            )
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            self.assertIn("4/4 PASS", result.stdout)
            artifact = directory / "artifacts/runtime/matrix/host/checked/O2"
            self.assertTrue((artifact / "source.hs").is_file())
            self.assertTrue((artifact / "compile.command.json").is_file())
            self.assertTrue((artifact / "run.result.json").is_file())

    def test_timeout_is_reported_and_preserved(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / "sleep.hs"
            source.write_text("sleep", encoding="utf-8")
            manifest = write_manifest(directory, source, timeout_seconds=0.5)
            manifest_data = json.loads(manifest.read_text(encoding="utf-8"))
            manifest_data["defaults"]["safety"] = "unchecked"
            manifest_data["defaults"]["optimization"] = "O0"
            manifest.write_text(json.dumps(manifest_data), encoding="utf-8")
            artifacts = directory / "artifacts"
            result = self.run_harness(write_fake_hsc(directory), manifest, artifacts)
            self.assertEqual(result.returncode, 1, result.stderr + result.stdout)
            self.assertIn("run timed out", result.stdout)
            timeout_result = json.loads(
                (artifacts / "runtime/matrix/host/unchecked/O0/run.result.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertTrue(timeout_result["timed_out"])

    def test_compile_stderr_mismatch_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / "compile-fail.hs"
            source.write_text("compile-fail", encoding="utf-8")
            manifest = write_manifest(directory, source)
            manifest_data = json.loads(manifest.read_text(encoding="utf-8"))
            manifest_data["defaults"]["safety"] = "unchecked"
            manifest_data["defaults"]["optimization"] = "O0"
            manifest_data["tests"][0]["expect"] = {
                "compile": {"exit_code": 7, "stdout": "", "stderr": "wrong\n"}
            }
            manifest.write_text(json.dumps(manifest_data), encoding="utf-8")
            artifacts = directory / "artifacts"
            result = self.run_harness(write_fake_hsc(directory), manifest, artifacts)
            self.assertEqual(result.returncode, 1, result.stderr + result.stdout)
            self.assertIn("compile stderr did not match expectation", result.stdout)
            self.assertEqual(
                (artifacts / "runtime/matrix/host/unchecked/O0/compile.stderr").read_text(
                    encoding="utf-8"
                ),
                "compile failure\n",
            )

    def test_differential_reports_observable_mismatch_and_preserves_report(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / "case.hs"
            source.write_text("optimization-diverge", encoding="utf-8")
            manifest = write_manifest(directory, source)
            manifest_data = json.loads(manifest.read_text(encoding="utf-8"))
            manifest_data["defaults"]["safety"] = "unchecked"
            manifest_data["defaults"]["optimization"] = ["O0", "O2", "O3"]
            manifest.write_text(json.dumps(manifest_data), encoding="utf-8")
            artifacts = directory / "artifacts"
            result = self.run_harness(
                write_fake_hsc(directory), manifest, artifacts, differential=True
            )
            self.assertEqual(result.returncode, 1, result.stderr + result.stdout)
            self.assertIn("differential O0 vs O2 run stdout differs", result.stdout)
            report = json.loads(
                (artifacts / "runtime/matrix/host/unchecked/differential.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(report["reference_optimization"], "O0")
            self.assertEqual(report["variants"][1]["mismatches"], ["run stdout differs"])


if __name__ == "__main__":
    unittest.main()
