#!/usr/bin/env python3
"""Compare the explicit OpenCL FlowIR prototype with the CPU reference.

The runner accepts a CPU fallback as a valid execution outcome.  It always
compares stable analysis dumps, then preserves the per-run device/report data
so a no-device result cannot be mistaken for a GPU performance measurement.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import statistics
import subprocess
import tempfile
import time
from pathlib import Path


SCHEMA_VERSION = 1


def digest(content: str) -> str:
    return hashlib.sha256(content.encode()).hexdigest()


def summarize(values: list[int]) -> dict[str, int]:
    ordered = sorted(values)
    return {
        "count": len(values),
        "min_ns": ordered[0],
        "median_ns": int(statistics.median(ordered)),
        "max_ns": ordered[-1],
    }


def run_command(command: list[str], timeout: int) -> tuple[int, str]:
    started = time.perf_counter_ns()
    completed = subprocess.run(
        command, check=False, capture_output=True, text=True, timeout=timeout
    )
    elapsed = time.perf_counter_ns() - started
    if completed.returncode != 0:
        message = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError(f"analysis command failed ({completed.returncode}): {message}")
    return elapsed, completed.stdout


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(
        description="Differential baseline for FlowIR CPU and explicit OpenCL analyses."
    )
    parser.add_argument("--project-root", type=Path, default=root)
    parser.add_argument("--hsc", type=Path, default=root / "build" / "hsc")
    parser.add_argument(
        "--input", type=Path, default=root / "tests/cases/run/try_catch_float_view.hs"
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--cpu-threads", type=int, default=4)
    parser.add_argument("--gpu-mode", choices=("auto", "opencl", "cpu"), default="auto")
    parser.add_argument("--timeout", type=int, default=120)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.runs <= 0 or args.warmups < 0 or args.cpu_threads <= 0 or args.timeout <= 0:
        raise RuntimeError("runs, warmups, cpu-threads, and timeout must be positive")
    hsc = args.hsc.resolve()
    source = args.input.resolve()
    if not hsc.is_file():
        raise RuntimeError(f"hsc executable does not exist: {hsc}")
    if not source.is_file():
        raise RuntimeError(f"input does not exist: {source}")

    cpu_values: list[int] = []
    gpu_values: list[int] = []
    expected_hash: str | None = None
    reports: list[dict[str, object]] = []
    with tempfile.TemporaryDirectory(prefix="hitsimple-flowir-gpu-") as temporary:
        workspace = Path(temporary)
        modes = ("cpu", "gpu")
        for index in range(args.warmups + args.runs):
            order = modes if index % 2 == 0 else tuple(reversed(modes))
            for mode in order:
                if mode == "cpu":
                    command = [str(hsc), f"--dump-flow-ir-analysis={args.cpu_threads}", str(source)]
                else:
                    report_path = workspace / f"gpu-{index}.json"
                    command = [
                        str(hsc),
                        f"--dump-flow-ir-gpu-analysis={args.gpu_mode}",
                        f"--flow-ir-gpu-report={report_path}",
                        str(source),
                    ]
                elapsed, stdout = run_command(command, args.timeout)
                current_hash = digest(stdout)
                if expected_hash is None:
                    expected_hash = current_hash
                elif current_hash != expected_hash:
                    raise RuntimeError("CPU and GPU FlowIR analysis dumps differ")
                if mode == "gpu":
                    try:
                        report = json.loads(report_path.read_text(encoding="utf-8"))
                    except (OSError, json.JSONDecodeError) as error:
                        raise RuntimeError(f"cannot read GPU analysis report: {error}") from error
                    if report.get("schema_version") != SCHEMA_VERSION:
                        raise RuntimeError("unexpected GPU analysis report schema")
                    reports.append(report)
                if index >= args.warmups:
                    (cpu_values if mode == "cpu" else gpu_values).append(elapsed)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    report = {
        "schema_version": SCHEMA_VERSION,
        "kind": "hitsimple_flowir_gpu_differential_baseline",
        "contract": {
            "cpu_oracle": "stable --dump-flow-ir-analysis output",
            "gpu_result_validation": "stable dump SHA-256 equality",
            "fallback_is_not_gpu_measurement": True,
            "hsc_invocations_are_sequential": True,
        },
        "input": {"path": str(source), "sha256": hashlib.sha256(source.read_bytes()).hexdigest()},
        "configuration": {
            "hsc": str(hsc),
            "runs": args.runs,
            "warmups": args.warmups,
            "cpu_threads": args.cpu_threads,
            "gpu_mode": args.gpu_mode,
        },
        "analysis_output": {"sha256": expected_hash},
        "modes": {"cpu": summarize(cpu_values), "gpu_action": summarize(gpu_values)},
        "gpu_execution_reports": reports,
    }
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"flowir GPU differential baseline: error: {error}")
        raise SystemExit(1)
