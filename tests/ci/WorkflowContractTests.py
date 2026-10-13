#!/usr/bin/env python3
"""Guard CI workflow contracts that GitHub Actions cannot validate locally."""

from __future__ import annotations

import json
import re
import unittest
from pathlib import Path


SOURCE_ROOT = Path(__file__).resolve().parents[2]


class WorkflowContractTests(unittest.TestCase):
    def release_version(self) -> str:
        cmake = (SOURCE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        match = re.search(r"^  VERSION (\d+\.\d+\.\d+)$", cmake, re.MULTILINE)
        self.assertIsNotNone(match)
        assert match is not None
        return match.group(1)

    def test_release_versions_match_compiler_extension_and_dispatch(self) -> None:
        version = self.release_version()
        extension = SOURCE_ROOT / "vscode/hitsimple"
        package = json.loads((extension / "package.json").read_text(encoding="utf-8"))
        lock = json.loads((extension / "package-lock.json").read_text(encoding="utf-8"))
        self.assertEqual(package["version"], version)
        self.assertEqual(lock["version"], version)
        self.assertEqual(lock["packages"][""]["version"], version)

        workflow = (SOURCE_ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        dispatch_version = re.search(
            r'^      version:\n(?:(?!^      \w).)*?^        default: "(\d+\.\d+\.\d+)"$',
            workflow,
            re.MULTILINE | re.DOTALL,
        )
        self.assertIsNotNone(dispatch_version)
        assert dispatch_version is not None
        self.assertEqual(dispatch_version.group(1), version)

    def test_release_package_documentation_matches_compiler(self) -> None:
        version = self.release_version()
        for name in ("README.md", "USAGE.md"):
            with self.subTest(document=name):
                document = (SOURCE_ROOT / name).read_text(encoding="utf-8")
                versions = re.findall(r"hitsimple(?:-vscode)?[-_](\d+\.\d+\.\d+)", document)
                self.assertTrue(versions)
                self.assertEqual(set(versions), {version})

    def test_release_bundle_uses_versioned_patch_notes(self) -> None:
        version = self.release_version()
        notes = (SOURCE_ROOT / "docs/releases" / f"{version}.md").read_text(encoding="utf-8")
        self.assertTrue(notes.startswith(f"# HitSimple {version}\n"))
        workflow = (SOURCE_ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        self.assertIn(
            'cp "$GITHUB_WORKSPACE/docs/releases/${HITSIMPLE_VERSION}.md" ../release-notes.txt',
            workflow,
        )

    def test_mutation_cmake_argument_binds_a_dash_prefixed_value(self) -> None:
        workflow = (SOURCE_ROOT / ".github/workflows/mutation.yml").read_text(encoding="utf-8")
        self.assertIn(
            '--cmake-arg="-DLLVM_DIR=$(llvm-config-18 --cmakedir)"',
            workflow,
        )

    def test_weekly_fuzz_keeps_a_half_hour_of_hosted_runner_headroom(self) -> None:
        workflow = (SOURCE_ROOT / ".github/workflows/fuzz-campaign.yml").read_text(encoding="utf-8")
        weekly = re.search(r"^  weekly:\n(?P<body>.*?)(?=^  [a-z][a-z_-]*:|\Z)", workflow, re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(weekly)
        assert weekly is not None

        timeout = re.search(r"^    timeout-minutes: (\d+)$", weekly.group("body"), re.MULTILINE)
        seconds = re.search(r"^            --seconds-per-target (\d+)", weekly.group("body"), re.MULTILINE)
        self.assertIsNotNone(timeout)
        self.assertIsNotNone(seconds)
        assert timeout is not None and seconds is not None

        timeout_minutes = int(timeout.group(1))
        campaign_seconds = int(seconds.group(1))
        self.assertLessEqual(timeout_minutes, 360)
        self.assertGreaterEqual(timeout_minutes * 60 - campaign_seconds, 30 * 60)

    def test_daily_fuzz_keeps_forty_five_minutes_of_hosted_runner_headroom(self) -> None:
        workflow = (SOURCE_ROOT / ".github/workflows/fuzz-campaign.yml").read_text(encoding="utf-8")
        daily = re.search(r"^  daily:\n(?P<body>.*?)(?=^  [a-z][a-z_-]*:|\Z)", workflow, re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(daily)
        assert daily is not None

        timeout = re.search(r"^    timeout-minutes: (\d+)$", daily.group("body"), re.MULTILINE)
        seconds = re.search(r"^            --seconds-per-target (\d+)", daily.group("body"), re.MULTILINE)
        self.assertIsNotNone(timeout)
        self.assertIsNotNone(seconds)
        assert timeout is not None and seconds is not None

        timeout_minutes = int(timeout.group(1))
        campaign_seconds = int(seconds.group(1))
        self.assertLessEqual(timeout_minutes, 360)
        self.assertGreaterEqual(timeout_minutes * 60 - campaign_seconds, 45 * 60)


if __name__ == "__main__":
    unittest.main()
