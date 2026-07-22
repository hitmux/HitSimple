#!/usr/bin/env python3
"""Run replayable libFuzzer campaigns without mutating the curated seed corpus.

The runner copies each committed corpus to the artifact directory, runs a
selected fuzzer against that copy, records its exact command/logs, minimizes
any crash artifact, and can produce a deduplicated corpus with libFuzzer's
``-merge=1`` mode.  CI uploads both this directory and the corresponding
fuzzer binary, so a failure can be replayed without the original checkout.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional, Sequence


TARGETS: dict[str, str] = {
    "lexer": "fuzz_lexer",
    "parser": "fuzz_parser",
    "sema": "fuzz_sema",
    "codegen": "fuzz_codegen",
}
DEFAULT_SECONDS_PER_TARGET = 900
DEFAULT_TIMEOUT_SECONDS = 10
DEFAULT_RSS_LIMIT_MB = 2048


@dataclass(frozen=True)
class ProcessResult:
    command: tuple[str, ...]
    exit_code: Optional[int]
    stdout: str
    stderr: str
    timed_out: bool


def _write_text(path: Path, content: str) -> None:
    path.write_text(content, encoding="utf-8")


def _write_json(path: Path, value: Any) -> None:
    _write_text(path, json.dumps(value, indent=2, sort_keys=True) + "\n")


def _run_process(command: Sequence[str], cwd: Path, timeout_seconds: Optional[float] = None) -> ProcessResult:
    try:
        completed = subprocess.run(
            list(command),
            cwd=str(cwd),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout_seconds,
            check=False,
        )
        return ProcessResult(
            tuple(command),
            completed.returncode,
            completed.stdout.decode("utf-8", errors="replace"),
            completed.stderr.decode("utf-8", errors="replace"),
            False,
        )
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or b""
        stderr = error.stderr or b""
        if isinstance(stdout, str):
            stdout = stdout.encode("utf-8", errors="replace")
        if isinstance(stderr, str):
            stderr = stderr.encode("utf-8", errors="replace")
        return ProcessResult(
            tuple(command),
            None,
            stdout.decode("utf-8", errors="replace"),
            stderr.decode("utf-8", errors="replace"),
            True,
        )
    except OSError as error:
        return ProcessResult(tuple(command), None, "", str(error) + "\n", False)


def _write_process(directory: Path, prefix: str, result: ProcessResult) -> None:
    _write_json(directory / (prefix + ".command.json"), list(result.command))
    _write_json(
        directory / (prefix + ".result.json"),
        {"exit_code": result.exit_code, "timed_out": result.timed_out},
    )
    _write_text(directory / (prefix + ".stdout"), result.stdout)
    _write_text(directory / (prefix + ".stderr"), result.stderr)


def _fuzzer_path(build_dir: Path, target: str) -> Path:
    candidate = build_dir / TARGETS[target]
    if candidate.is_file():
        return candidate
    windows_candidate = candidate.with_suffix(".exe")
    return windows_candidate if windows_candidate.is_file() else candidate


def campaign_command(
    fuzzer: Path,
    corpus: Path,
    crash_directory: Path,
    seconds: int,
    timeout_seconds: int,
    rss_limit_mb: int,
) -> list[str]:
    return [
        str(fuzzer),
        "-max_total_time=" + str(seconds),
        "-timeout=" + str(timeout_seconds),
        "-rss_limit_mb=" + str(rss_limit_mb),
        "-artifact_prefix=" + str(crash_directory) + "/",
        str(corpus),
    ]


def _crash_inputs(directory: Path) -> list[Path]:
    return sorted(
        path
        for path in directory.iterdir()
        if path.is_file() and (path.name.startswith("crash-") or path.name.startswith("leak-") or path.name.startswith("timeout-"))
    )


def _copy_seed_corpus(source: Path, destination: Path) -> None:
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(source, destination)


def _minimize_crashes(
    fuzzer: Path, crashes: Sequence[Path], target_root: Path, timeout_seconds: int
) -> list[dict[str, object]]:
    reductions = target_root / "reductions"
    reductions.mkdir(parents=True, exist_ok=True)
    results: list[dict[str, object]] = []
    for index, crash in enumerate(crashes, start=1):
        minimized = reductions / ("minimized-" + str(index))
        command = [
            str(fuzzer),
            "-minimize_crash=1",
            "-exact_artifact_path=" + str(minimized),
            str(crash),
        ]
        result = _run_process(command, target_root, timeout_seconds)
        _write_process(target_root, "minimize-" + str(index), result)
        results.append(
            {
                "crash": str(crash.relative_to(target_root)),
                "minimized": str(minimized.relative_to(target_root)),
                "exit_code": result.exit_code,
                "timed_out": result.timed_out,
            }
        )
    return results


def _deduplicate_corpus(
    fuzzer: Path, raw_corpus: Path, target_root: Path, timeout_seconds: int
) -> ProcessResult:
    output = target_root / "deduplicated-corpus"
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    result = _run_process(
        [str(fuzzer), "-merge=1", str(output), str(raw_corpus)], target_root, timeout_seconds
    )
    _write_process(target_root, "deduplicate", result)
    return result


def _replay_text(target: str, fuzzer: Path, raw_corpus: Path, crashes: Sequence[Path]) -> str:
    lines = [
        "# Fuzz campaign replay",
        "",
        "The workflow artifact contains this directory and the matching fuzzer binary.",
        "From this target directory, set `FUZZER` to that binary and replay the copied corpus or an individual crash:",
        "",
        "```bash",
        "export FUZZER=/path/to/" + fuzzer.name,
        '"$FUZZER" ' + raw_corpus.name,
    ]
    for crash in crashes:
        lines.append('"$FUZZER" ' + str(crash.relative_to(raw_corpus.parent)))
    lines.extend(("```", "", "Target: " + target, ""))
    return "\n".join(lines)


def _positive_int(value: str) -> int:
    try:
        parsed = int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be an integer") from error
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return parsed


def parse_arguments(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--corpus-root", required=True, type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--target", choices=tuple(TARGETS), action="append", help="target to run; defaults to all")
    parser.add_argument("--seconds-per-target", type=_positive_int, default=DEFAULT_SECONDS_PER_TARGET)
    parser.add_argument("--timeout", type=_positive_int, default=DEFAULT_TIMEOUT_SECONDS)
    parser.add_argument("--rss-limit-mb", type=_positive_int, default=DEFAULT_RSS_LIMIT_MB)
    parser.add_argument("--deduplicate", action="store_true", help="run libFuzzer -merge=1 after each target")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_arguments(argv)
    build_dir = args.build_dir.resolve()
    corpus_root = args.corpus_root.resolve()
    artifact_root = args.artifacts.resolve()
    selected = args.target or list(TARGETS)
    if not build_dir.is_dir():
        print("fuzz campaign: error: --build-dir does not exist: " + str(build_dir), file=sys.stderr)
        return 2
    if not corpus_root.is_dir():
        print("fuzz campaign: error: --corpus-root does not exist: " + str(corpus_root), file=sys.stderr)
        return 2
    for target in selected:
        fuzzer = _fuzzer_path(build_dir, target)
        if not fuzzer.is_file():
            print("fuzz campaign: error: fuzzer does not exist: " + str(fuzzer), file=sys.stderr)
            return 2
        seed_corpus = corpus_root / target
        if not seed_corpus.is_dir():
            print("fuzz campaign: error: seed corpus does not exist: " + str(seed_corpus), file=sys.stderr)
            return 2

    artifact_root.mkdir(parents=True, exist_ok=True)
    campaign_summary: dict[str, object] = {
        "version": 1,
        "seconds_per_target": args.seconds_per_target,
        "timeout_seconds": args.timeout,
        "rss_limit_mb": args.rss_limit_mb,
        "deduplicate": args.deduplicate,
        "targets": {},
    }
    failed = False
    for target in selected:
        fuzzer = _fuzzer_path(build_dir, target)
        target_root = artifact_root / target
        raw_corpus = target_root / "corpus"
        crash_directory = target_root / "crashes"
        target_root.mkdir(parents=True, exist_ok=True)
        crash_directory.mkdir(parents=True, exist_ok=True)
        _copy_seed_corpus(corpus_root / target, raw_corpus)
        result = _run_process(
            campaign_command(
                fuzzer,
                raw_corpus,
                crash_directory,
                args.seconds_per_target,
                args.timeout,
                args.rss_limit_mb,
            ),
            target_root,
            args.seconds_per_target + args.timeout + 60,
        )
        _write_process(target_root, "campaign", result)
        crashes = _crash_inputs(crash_directory)
        reductions = _minimize_crashes(fuzzer, crashes, target_root, args.timeout)
        dedup_result: Optional[ProcessResult] = None
        if args.deduplicate and result.exit_code == 0 and not result.timed_out:
            dedup_result = _deduplicate_corpus(fuzzer, raw_corpus, target_root, args.timeout + 60)
        _write_text(target_root / "REPLAY.md", _replay_text(target, fuzzer, raw_corpus, crashes))
        target_failed = result.exit_code != 0 or result.timed_out or (dedup_result is not None and dedup_result.exit_code != 0)
        failed = failed or target_failed
        campaign_summary["targets"][target] = {
            "fuzzer": str(fuzzer),
            "campaign_exit_code": result.exit_code,
            "campaign_timed_out": result.timed_out,
            "crashes": [str(path.relative_to(target_root)) for path in crashes],
            "reductions": reductions,
            "deduplicate_exit_code": None if dedup_result is None else dedup_result.exit_code,
            "failed": target_failed,
        }
        print(("[FAIL] " if target_failed else "[PASS] ") + target)
    _write_json(artifact_root / "campaign.json", campaign_summary)
    print("Artifacts: " + str(artifact_root))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
