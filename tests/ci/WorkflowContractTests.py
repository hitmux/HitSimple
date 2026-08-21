#!/usr/bin/env python3
"""Guard CI workflow contracts that GitHub Actions cannot validate locally."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


SOURCE_ROOT = Path(__file__).resolve().parents[2]


class WorkflowContractTests(unittest.TestCase):
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
