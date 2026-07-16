#!/usr/bin/env python3
"""Measure deterministic Phase 3 CPU FlowIR reference analyses.

The runner invokes one hsc process at a time and records the same analysis with
one worker and a caller-selected worker count.  It rejects output drift before
reporting timing data, so the parallel path is a differential check as well as
a baseline.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import statistics
import subprocess
import sys
import time
from pathlib import Path


SCHEMA_VERSION = 1


def positive(text: str) -> int:
    value = int(text)
    if value <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return value


def non_negative(text: str) -> int:
    value = int(text)
    if value < 0:
        raise argparse.ArgumentTypeError("must be non-negative")
    return value


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summarize(values: list[int]) -> dict[str, float | int]:
    return {
        "count": len(values),
        "minimum_ns": min(values),
        "median_ns": statistics.median(values),
        "maximum_ns": max(values),
    }


def run_once(hsc: Path, source: Path, workers: int, timeout: int) -> tuple[int, str]:
    command = [str(hsc), f"--dump-flow-ir-analysis={workers}", str(source)]
    started = time.perf_counter_ns()
    completed = subprocess.run(command, check=False, capture_output=True,
                               text=True, timeout=timeout)
    elapsed = time.perf_counter_ns() - started
    if completed.returncode != 0:
        message = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError(f"analysis command failed ({completed.returncode}): {message}")
    return elapsed, completed.stdout


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Measure Phase 3 FlowIR CPU single/multi-worker references."
    )
    root = Path(__file__).resolve().parents[1]
    parser.add_argument("--project-root", type=Path, default=root)
    parser.add_argument("--hsc", type=Path, default=Path("build/hsc"))
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--threads", type=positive, default=4)
    parser.add_argument("--runs", type=positive, default=7)
    parser.add_argument("--warmups", type=non_negative, default=1)
    parser.add_argument("--timeout-seconds", type=positive, default=180)
    return parser.parse_args()


def resolve(path: Path, root: Path) -> Path:
    return path.resolve() if path.is_absolute() else (root / path).resolve()


def main() -> int:
    args = parse_args()
    root = args.project_root.resolve()
    hsc = resolve(args.hsc, root)
    source = resolve(args.input, root)
    output = resolve(args.output, root)
    if not hsc.is_file() or not source.is_file():
        raise RuntimeError("--hsc and --input must name existing files")

    modes = [("single", 1), ("parallel", args.threads)]
    measurements: dict[str, list[int]] = {name: [] for name, _ in modes}
    expected: str | None = None
    analysis_lines: int | None = None
    for index in range(args.warmups + args.runs):
        order = modes if index % 2 == 0 else list(reversed(modes))
        for name, workers in order:
            elapsed, stdout = run_once(hsc, source, workers, args.timeout_seconds)
            current = hashlib.sha256(stdout.encode()).hexdigest()
            if expected is None:
                expected = current
                analysis_lines = len(stdout.splitlines())
            elif current != expected:
                raise RuntimeError("single-worker and parallel FlowIR analysis results differ")
            if index >= args.warmups:
                measurements[name].append(elapsed)

    report = {
        "schema_version": SCHEMA_VERSION,
        "kind": "hitsimple_flowir_cpu_reference_baseline",
        "contract": {
            "same_algorithm": True,
            "hsc_invocations_are_sequential": True,
            "differential_oracle": "SHA-256 of stable --dump-flow-ir-analysis output",
            "gpu_involved": False,
        },
        "input": {"path": str(source), "sha256": digest(source)},
        "configuration": {
            "hsc": str(hsc), "runs": args.runs, "warmups": args.warmups,
            "parallel_workers": args.threads,
        },
        "analysis_output": {"sha256": expected, "line_count": analysis_lines},
        "modes": {name: summarize(values) for name, values in measurements.items()},
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(f".{output.name}.tmp")
    temporary.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(output)
    print(output)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        print(f"flowir analysis baseline: error: {error}", file=sys.stderr)
        raise SystemExit(1)
