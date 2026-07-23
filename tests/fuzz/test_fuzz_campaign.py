#!/usr/bin/env python3
"""Unit tests for long-running fuzz campaign command and replay contracts."""

from __future__ import annotations

import os
import stat
import tempfile
import unittest
from pathlib import Path

from run_campaign import TARGETS, _crash_inputs, _replay_text, campaign_command, main


class FuzzCampaignTests(unittest.TestCase):
    def test_campaign_command_has_bounded_resource_and_artifact_contract(self) -> None:
        command = campaign_command(
            Path("/build/fuzz_parser"), Path("/corpus/parser"), Path("/artifacts/crashes"), 900, 10, 2048
        )
        self.assertEqual(command[0], "/build/fuzz_parser")
        self.assertIn("-max_total_time=900", command)
        self.assertIn("-timeout=10", command)
        self.assertIn("-rss_limit_mb=2048", command)
        self.assertIn("-artifact_prefix=/artifacts/crashes/", command)
        self.assertEqual(command[-1], "/corpus/parser")

    def test_crash_discovery_and_replay_document_only_replayable_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = root / "parser"
            crash_directory = target / "crashes"
            crash_directory.mkdir(parents=True)
            for name in ("crash-123", "timeout-456", "leak-789", "not-a-crash"):
                (crash_directory / name).write_text("seed", encoding="utf-8")
            crashes = _crash_inputs(crash_directory)
            self.assertEqual([path.name for path in crashes], ["crash-123", "leak-789", "timeout-456"])
            replay = _replay_text("parser", Path("/build/fuzz_parser"), target / "corpus", crashes)
            self.assertIn("export FUZZER=/path/to/fuzz_parser", replay)
            self.assertIn('"$FUZZER" corpus', replay)

    def test_target_catalog_stays_aligned_with_committed_seed_directories(self) -> None:
        seed_root = Path(__file__).parents[1] / "corpus" / "seeds"
        self.assertEqual(set(TARGETS), {path.name for path in seed_root.iterdir() if path.is_dir()})

    @unittest.skipIf(os.name == "nt", "the fake fuzzer uses a POSIX shell fixture")
    def test_injected_crash_is_minimized_and_documented_for_replay(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "build"
            corpus = root / "seeds" / "parser"
            artifacts = root / "artifacts"
            build.mkdir(parents=True)
            corpus.mkdir(parents=True)
            (corpus / "seed").write_text("seed", encoding="utf-8")
            fuzzer = build / "fuzz_parser"
            fuzzer.write_text(
                """#!/bin/sh
for argument in "$@"; do
  case "$argument" in
    -artifact_prefix=*) prefix="${argument#-artifact_prefix=}" ;;
    -exact_artifact_path=*) output="${argument#-exact_artifact_path=}" ;;
    -minimize_crash=1) minimize=1 ;;
  esac
done
if [ "${minimize:-0}" = 1 ]; then
  mkdir -p "$(dirname "$output")"
  printf minimized > "$output"
  exit 0
fi
mkdir -p "$prefix"
printf crash > "${prefix}crash-injected"
exit 1
""",
                encoding="utf-8",
            )
            fuzzer.chmod(fuzzer.stat().st_mode | stat.S_IXUSR)

            self.assertEqual(
                main(
                    [
                        "--build-dir",
                        str(build),
                        "--corpus-root",
                        str(root / "seeds"),
                        "--artifacts",
                        str(artifacts),
                        "--target",
                        "parser",
                        "--seconds-per-target",
                        "1",
                    ]
                ),
                1,
            )
            target = artifacts / "parser"
            self.assertTrue((target / "crashes" / "crash-injected").is_file())
            self.assertTrue((target / "reductions" / "minimized-1").is_file())
            self.assertIn("crash-injected", (target / "REPLAY.md").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
