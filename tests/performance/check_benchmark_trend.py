#!/usr/bin/env python3
"""Compare HitSimple benchmark medians with an approved native baseline."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Mapping, Sequence


WORKLOADS = ("mandelbrot", "memory")


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--current", required=True, type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--warn-percent", type=float, default=3.0)
    parser.add_argument("--investigate-percent", type=float, default=5.0)
    parser.add_argument("--fail-percent", type=float, default=10.0)
    return parser.parse_args(argv)


def read_json(path: Path) -> Mapping[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise ValueError("cannot read '" + str(path) + "': " + str(error)) from error
    except json.JSONDecodeError as error:
        raise ValueError("invalid JSON in '" + str(path) + "': " + str(error)) from error
    if not isinstance(value, dict):
        raise ValueError("'" + str(path) + "' must contain an object")
    return value


def median(report: Mapping[str, Any], workload: str) -> float:
    try:
        value = report["workloads"][workload]["hitsimple"]["median_ns"]
    except (KeyError, TypeError) as error:
        raise ValueError(
            "report is missing workloads."
            + workload
            + ".hitsimple.median_ns"
        ) from error
    if not isinstance(value, (int, float)) or isinstance(value, bool) or value <= 0:
        raise ValueError("median for '" + workload + "' must be a positive number")
    return float(value)


def write_json(path: Path, value: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main(argv: Sequence[str]) -> int:
    args = parse_arguments(argv)
    if not (0 <= args.warn_percent <= args.investigate_percent <= args.fail_percent):
        print("trend thresholds must satisfy 0 <= warn <= investigate <= fail", file=sys.stderr)
        return 2
    try:
        current = read_json(args.current)
        current_medians = {workload: median(current, workload) for workload in WORKLOADS}
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 2

    report: dict[str, Any] = {
        "version": 1,
        "current": str(args.current.resolve()),
        "thresholds_percent": {
            "warn": args.warn_percent,
            "investigate": args.investigate_percent,
            "fail": args.fail_percent,
        },
    }
    if args.baseline is None or not args.baseline.is_file():
        report.update(
            {
                "status": "baseline_missing",
                "next_action": "approve a native benchmark report as the baseline before enforcing regressions",
                "workloads": {
                    workload: {"current_median_ns": value}
                    for workload, value in current_medians.items()
                },
            }
        )
        write_json(args.output, report)
        print("benchmark baseline is unavailable; saved a non-gating trend report")
        return 0

    try:
        baseline = read_json(args.baseline)
        baseline_medians = {workload: median(baseline, workload) for workload in WORKLOADS}
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 2

    statuses = []
    workloads: dict[str, Any] = {}
    for workload in WORKLOADS:
        current_median = current_medians[workload]
        baseline_median = baseline_medians[workload]
        regression_percent = (current_median / baseline_median - 1.0) * 100.0
        if regression_percent > args.fail_percent:
            status = "failure"
        elif regression_percent > args.investigate_percent:
            status = "investigate"
        elif regression_percent > args.warn_percent:
            status = "warning"
        else:
            status = "pass"
        statuses.append(status)
        workloads[workload] = {
            "baseline_median_ns": baseline_median,
            "current_median_ns": current_median,
            "regression_percent": regression_percent,
            "status": status,
        }

    if "failure" in statuses:
        overall = "failure"
    elif "investigate" in statuses:
        overall = "investigate"
    elif "warning" in statuses:
        overall = "warning"
    else:
        overall = "pass"
    report.update(
        {
            "status": overall,
            "baseline": str(args.baseline.resolve()),
            "workloads": workloads,
        }
    )
    write_json(args.output, report)
    print("benchmark trend status: " + overall)
    return 1 if overall == "failure" else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
