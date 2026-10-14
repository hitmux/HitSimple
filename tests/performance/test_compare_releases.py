#!/usr/bin/env python3
"""Contracts for the release coordinator; no compiler or ARM emulation required."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import compare_releases as comparison


def fixtures():
    reports = {}
    for label, revision in comparison.REVISIONS.items():
        reports[label] = {
            "git_commit": revision, "target_triple": "aarch64-unknown-linux-gnu",
            "optimization_level": "O2", "cpu": {"model": "N2", "affinity": 0, "governor": "unknown"},
            "runs": 3, "warmups": 1, "cooldown_seconds": 0,
            "cxx": {"path": "/bin/clang++-18", "version": "18.1.3"},
            "workloads": {"bitops": {"checksum": "123", "safety_mode": "unchecked",
                "hitsimple": {"samples_ns": [90, 100, 110], "median_ns": 100},
                "cpp": {"samples_ns": [40, 50, 60], "median_ns": 50}}}}
    manifests = {label: {"source.hs": "abc"} for label in reports}
    return reports, manifests


class ReleaseComparisonTests(unittest.TestCase):
    def test_stats_and_spread(self):
        self.assertEqual(comparison.stats([1, 2, 3]), {
            "n": 3, "min": 1, "max": 3, "median": 2, "median_absolute_deviation": 1})

    def test_stats_rejects_invalid_values(self):
        for values in ([], [True], [0], [-1], [float("nan")], [float("inf")]):
            with self.subTest(values=values), self.assertRaises(ValueError):
                comparison.stats(values)

    def test_compatible_reports(self):
        comparison.verify_reports(*fixtures(), 3, 1)

    def test_rejects_incompatible_metadata(self):
        for field, value in (("git_commit", "bad"), ("target_triple", "x86_64-linux"),
                             ("optimization_level", "O3"), ("cpu", {"affinity": 1}),
                             ("runs", 4), ("warmups", 0), ("cooldown_seconds", 1),
                             ("cxx", {"version": "different"})):
            with self.subTest(field=field), self.assertRaises(ValueError):
                reports, manifests = fixtures()
                reports["v0.3.9"][field] = value
                comparison.verify_reports(reports, manifests, 3, 1)

    def test_rejects_source_changes(self):
        reports, manifests = fixtures()
        manifests["v0.3.9"]["source.hs"] = "changed"
        with self.assertRaises(ValueError):
            comparison.verify_reports(reports, manifests, 3, 1)

    def test_rejects_missing_workload(self):
        reports, manifests = fixtures()
        reports["v0.3.9"]["workloads"] = {}
        with self.assertRaises(ValueError):
            comparison.verify_reports(reports, manifests, 3, 1)

    def test_rejects_wrong_checksum_or_mode(self):
        for field in ("checksum", "safety_mode"):
            with self.subTest(field=field), self.assertRaises(ValueError):
                reports, manifests = fixtures()
                reports["v0.3.9"]["workloads"]["bitops"][field] = "wrong"
                comparison.verify_reports(reports, manifests, 3, 1)

    def test_rejects_bad_runtime_samples(self):
        for samples in ([100], [90, 100, 101, 110], [90, 101, 110], [0, 100, 110]):
            with self.subTest(samples=samples), self.assertRaises(ValueError):
                reports, manifests = fixtures()
                reports["v0.3.9"]["workloads"]["bitops"]["hitsimple"]["samples_ns"] = samples
                comparison.verify_reports(reports, manifests, 3, 1)

    def test_compilation_commands_reuse_t8_modes(self):
        source, compiler = Path("/source"), Path("/build/hsc")
        for kind in ("executable", "object"):
            cmd = comparison.compilation_command(source, compiler, "c_abi", "unchecked", kind,
                                                 Path("/out"), Path("/timing.json"))
            self.assertEqual("--c-source" in cmd, kind == "executable")
            self.assertEqual("--emit-object" in cmd, kind == "object")
            self.assertIn("--timing-json=/timing.json", cmd)
        cmd = comparison.compilation_command(source, compiler, "checked_loop", "checked", "executable",
                                             Path("/out"), Path("/timing"))
        self.assertIn("--checked", cmd)
        self.assertNotIn("--unchecked", cmd)

    def test_compile_warmups_order_raw_samples_and_unique_paths(self):
        reports, _ = fixtures()
        states = {label: (Path("/source") / label, Path("/build") / label) for label in reports}
        calls = []
        def fake_run(command, **kwargs):
            calls.append(command)
            timing = Path(next(x.split("=", 1)[1] for x in command if x.startswith("--timing-json=")))
            timing.write_text(json.dumps({"schema_version": 1, "outcome": "success", "total_duration_ns": 100}))
            Path(command[-1]).write_bytes(b"ELF")
            return 0
        with tempfile.TemporaryDirectory() as temporary, mock.patch.object(comparison, "run", fake_run):
            samples = comparison.collect_compilations(states, reports, Path(temporary), 2, 3, 1)
            self.assertEqual(len(calls), 3 * 2 * 4)
            self.assertEqual(len({cmd[-1] for cmd in calls}), len(calls))
            self.assertEqual([calls[i][3] for i in (0, 6, 12)],
                             [str(states[label][1] / "hsc") for label in states])
            for label in samples:
                for values in samples[label].values():
                    self.assertEqual([v["warmup"] for v in values], [True, False, False, False])
            summary = comparison.summarize(reports, samples)
            self.assertEqual(summary["v0.3.9"]["compilation"]["bitops/object"]["compiler_total_ns"]["n"], 3)
            self.assertEqual(summary["v0.3.9"]["compilation"]["bitops/object"]["size_change_percent"], 0)

    def test_timing_rejects_failure_and_invalid_duration(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "timing.json"
            for change in ({"schema_version": 2}, {"outcome": "failure"}, {"total_duration_ns": 0}):
                data = {"schema_version": 1, "outcome": "success", "total_duration_ns": 1, **change}
                path.write_text(json.dumps(data))
                with self.subTest(change=change), self.assertRaises(ValueError):
                    comparison.timing_total(path)

    def test_native_tests_cannot_be_missing_failed_or_skipped(self):
        cases = ['<testcase name="' + name + '" status="run"/>' for name in (
            "hsc_t8_aarch64_strict_alignment", "hsc_harness_aarch64_semantics", "hsc_t8_vectorization_remarks")]
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "junit.xml"
            path.write_text("<testsuite>" + "".join(cases) + "</testsuite>")
            self.assertEqual(comparison.verify_junit(path), 3)
            for content in ("", "".join(cases[:1]), "".join(cases).replace('status="run"', 'status="notrun"')):
                path.write_text("<testsuite>" + content + "</testsuite>")
                with self.assertRaises(ValueError):
                    comparison.verify_junit(path)

    def test_comparison_excludes_warmups_and_reports_deltas(self):
        reports, _ = fixtures()
        samples = {label: {"bitops/executable": [
            {"warmup": True, "total_duration_ns": 999, "wall_duration_ns": 999, "bytes": 10},
            {"warmup": False, "total_duration_ns": 100, "wall_duration_ns": 120, "bytes": 10}
        ]} for label in reports}
        samples["v0.3.9"]["bitops/executable"][1]["total_duration_ns"] = 110
        samples["v0.3.9"]["bitops/executable"][1]["bytes"] = 12
        summary = comparison.summarize(reports, samples)["v0.3.9"]["compilation"]["bitops/executable"]
        self.assertEqual(summary["compiler_total_ns"]["n"], 1)
        self.assertAlmostEqual(summary["compile_change_percent"], 10)
        self.assertAlmostEqual(summary["size_change_percent"], 20)


if __name__ == "__main__":
    unittest.main()
