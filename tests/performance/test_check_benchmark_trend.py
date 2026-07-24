#!/usr/bin/env python3
"""Focused contract tests for the performance trend threshold policy."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("check_benchmark_trend.py")


def report(
    mandelbrot: float,
    memory: float,
    mandelbrot_cpp: float | None = None,
    memory_cpp: float | None = None,
) -> dict[str, object]:
    return {
        "version": 2,
        "target_triple": "aarch64-unknown-linux-gnu",
        "cpu": {"model": "remote", "governor": "performance"},
        "cxx": {"version": "clang 18"},
        "workloads": {
            "mandelbrot": {
                "hitsimple": {"median_ns": mandelbrot},
                "cpp": {"median_ns": mandelbrot / 2 if mandelbrot_cpp is None else mandelbrot_cpp},
            },
            "memory": {
                "hitsimple": {"median_ns": memory},
                "cpp": {"median_ns": memory / 2 if memory_cpp is None else memory_cpp},
            },
        },
    }


class BenchmarkTrendTests(unittest.TestCase):
    def run_checker(
        self, current: dict[str, object], baseline: dict[str, object] | None
    ) -> tuple[subprocess.CompletedProcess[str], dict[str, object]]:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            current_path = root / "current.json"
            output_path = root / "trend.json"
            current_path.write_text(json.dumps(current), encoding="utf-8")
            command = [sys.executable, str(SCRIPT), "--current", str(current_path), "--output", str(output_path)]
            if baseline is not None:
                baseline_path = root / "baseline.json"
                baseline_path.write_text(json.dumps(baseline), encoding="utf-8")
                command.extend(("--baseline", str(baseline_path)))
            result = subprocess.run(command, text=True, capture_output=True, check=False)
            return result, json.loads(output_path.read_text(encoding="utf-8"))

    def test_missing_baseline_is_visible_but_not_a_failure(self) -> None:
        result, output = self.run_checker(report(100, 200), None)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(output["status"], "baseline_missing")

    def test_small_regression_stays_within_noise_budget(self) -> None:
        result, output = self.run_checker(report(102, 198), report(100, 200))
        self.assertEqual(result.returncode, 0)
        self.assertEqual(output["status"], "pass")

    def test_warning_threshold_is_non_blocking(self) -> None:
        result, output = self.run_checker(report(104, 200, 50), report(100, 200, 50))
        self.assertEqual(result.returncode, 0)
        self.assertEqual(output["status"], "warning")

    def test_investigation_threshold_is_non_blocking(self) -> None:
        result, output = self.run_checker(report(106, 200, 50), report(100, 200, 50))
        self.assertEqual(result.returncode, 0)
        self.assertEqual(output["status"], "investigate")

    def test_large_regression_fails(self) -> None:
        result, output = self.run_checker(report(111, 200, 50), report(100, 200, 50))
        self.assertEqual(result.returncode, 1)
        self.assertEqual(output["status"], "failure")

    def test_new_workload_requires_an_explicit_baseline(self) -> None:
        current = report(100, 200)
        current["workloads"]["bitops"] = {
            "hitsimple": {"median_ns": 300},
            "cpp": {"median_ns": 150},
        }
        result, output = self.run_checker(current, report(100, 200))
        self.assertEqual(result.returncode, 0)
        self.assertEqual(output["status"], "baseline_incomplete")
        self.assertEqual(output["missing_baseline_workloads"], ["bitops"])
        self.assertEqual(output["workloads"]["bitops"]["status"], "baseline_missing")

    def test_equal_hardware_slowdown_is_diagnostic_not_a_regression(self) -> None:
        result, output = self.run_checker(report(111, 222, 55.5, 111), report(100, 200, 50, 100))
        self.assertEqual(result.returncode, 0)
        self.assertEqual(output["status"], "pass")
        self.assertEqual(output["workloads"]["mandelbrot"]["ratio_regression_percent"], 0.0)
        self.assertAlmostEqual(
            output["workloads"]["mandelbrot"]["hitsimple_absolute_delta_percent"],
            11.0,
        )

    def test_incompatible_hardware_is_not_compared(self) -> None:
        current = report(200, 400)
        current["target_triple"] = "x86_64-pc-linux-gnu"
        current["cpu"] = {"model": "local"}
        baseline = report(100, 200)
        baseline["target_triple"] = "aarch64-unknown-linux-gnu"
        baseline["cpu"] = {"model": "remote"}
        result, output = self.run_checker(current, baseline)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(output["status"], "baseline_incompatible")

    def test_cxx_version_and_cpu_governor_are_required_for_gating(self) -> None:
        current = report(100, 200)
        baseline = report(100, 200)
        current["cxx"] = {"version": "clang 19"}
        current["cpu"] = {"model": "remote", "governor": "powersave"}
        result, output = self.run_checker(current, baseline)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(output["status"], "baseline_incompatible")
        self.assertIn("cxx.version differs: current=clang 19, baseline=clang 18", output["compatibility_differences"])
        self.assertIn("cpu.governor differs: current=powersave, baseline=performance", output["compatibility_differences"])

    def test_instruction_regression_fails_when_quality_baseline_exists(self) -> None:
        current = report(100, 200)
        baseline = report(100, 200)
        current["machine_quality"] = {
            "workloads": {
                "mandelbrot": {
                    "hsc_o2": {
                        "instruction_count": 109,
                        "stack_memory_operations": 4,
                    }
                }
            }
        }
        baseline["machine_quality"] = {
            "workloads": {
                "mandelbrot": {
                    "hsc_o2": {
                        "instruction_count": 100,
                        "stack_memory_operations": 4,
                    }
                }
            }
        }
        result, output = self.run_checker(current, baseline)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(output["status"], "failure")
        self.assertEqual(
            output["machine_quality"]["workloads"]["mandelbrot"]["status"],
            "failure",
        )


if __name__ == "__main__":
    unittest.main()
