#!/usr/bin/env python3
"""Compare HitSimple benchmark medians with an approved native baseline."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Mapping, Sequence


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--current", required=True, type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--warn-percent", type=float, default=3.0)
    parser.add_argument("--investigate-percent", type=float, default=5.0)
    parser.add_argument("--fail-percent", type=float, default=10.0)
    parser.add_argument("--instruction-fail-percent", type=float, default=8.0)
    parser.add_argument("--stack-memory-fail-percent", type=float, default=8.0)
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


def median(report: Mapping[str, Any], workload: str, implementation: str) -> float:
    try:
        value = report["workloads"][workload][implementation]["median_ns"]
    except (KeyError, TypeError) as error:
        raise ValueError(
            "report is missing workloads."
            + workload
            + "."
            + implementation
            + ".median_ns"
        ) from error
    if not isinstance(value, (int, float)) or isinstance(value, bool) or value <= 0:
        raise ValueError(
            "median for '" + workload + "." + implementation + "' must be a positive number"
        )
    return float(value)


def workload_measurements(report: Mapping[str, Any]) -> dict[str, dict[str, float]]:
    try:
        workloads = report["workloads"]
    except KeyError as error:
        raise ValueError("report is missing workloads") from error
    if not isinstance(workloads, dict) or not workloads:
        raise ValueError("report workloads must be a non-empty object")
    return {
        str(workload): {
            "hitsimple": median(report, str(workload), "hitsimple"),
            "cpp": median(report, str(workload), "cpp"),
        }
        for workload in workloads
    }


def baseline_compatibility(
    current: Mapping[str, Any], baseline: Mapping[str, Any]
) -> list[str]:
    differences = []
    for field in ("target_triple",):
        current_value = current.get(field)
        baseline_value = baseline.get(field)
        if not isinstance(current_value, str) or not isinstance(baseline_value, str):
            differences.append(field + " is missing from the current report or baseline")
        elif current_value != baseline_value:
            differences.append(
                field + " differs: current=" + current_value + ", baseline=" + baseline_value
            )
    current_cpu = current.get("cpu")
    baseline_cpu = baseline.get("cpu")
    for field in ("model", "governor"):
        current_value = current_cpu.get(field) if isinstance(current_cpu, dict) else None
        baseline_value = baseline_cpu.get(field) if isinstance(baseline_cpu, dict) else None
        if not isinstance(current_value, str) or not isinstance(baseline_value, str):
            differences.append("cpu." + field + " is missing from the current report or baseline")
        elif current_value != baseline_value:
            differences.append(
                "cpu."
                + field
                + " differs: current="
                + current_value
                + ", baseline="
                + baseline_value
            )
    current_cxx = current.get("cxx")
    baseline_cxx = baseline.get("cxx")
    current_version = current_cxx.get("version") if isinstance(current_cxx, dict) else None
    baseline_version = baseline_cxx.get("version") if isinstance(baseline_cxx, dict) else None
    if not isinstance(current_version, str) or not isinstance(baseline_version, str):
        differences.append("cxx.version is missing from the current report or baseline")
    elif current_version != baseline_version:
        differences.append(
            "cxx.version differs: current=" + current_version + ", baseline=" + baseline_version
        )
    return differences


def quality_metrics(report: Mapping[str, Any]) -> dict[str, dict[str, int]]:
    try:
        workloads = report["machine_quality"]["workloads"]
    except (KeyError, TypeError):
        return {}
    if not isinstance(workloads, dict):
        return {}
    result = {}
    for workload, value in workloads.items():
        try:
            metrics = value["hsc_o2"]
            instructions = metrics["instruction_count"]
            stack_memory = metrics["stack_memory_operations"]
        except (KeyError, TypeError):
            continue
        if (
            isinstance(instructions, int)
            and not isinstance(instructions, bool)
            and instructions > 0
            and isinstance(stack_memory, int)
            and not isinstance(stack_memory, bool)
            and stack_memory >= 0
        ):
            result[str(workload)] = {
                "instruction_count": instructions,
                "stack_memory_operations": stack_memory,
            }
    return result


def write_json(path: Path, value: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main(argv: Sequence[str]) -> int:
    args = parse_arguments(argv)
    if not (0 <= args.warn_percent <= args.investigate_percent <= args.fail_percent):
        print("trend thresholds must satisfy 0 <= warn <= investigate <= fail", file=sys.stderr)
        return 2
    if args.instruction_fail_percent < 0 or args.stack_memory_fail_percent < 0:
        print("machine-quality thresholds must be non-negative", file=sys.stderr)
        return 2
    try:
        current = read_json(args.current)
        current_measurements = workload_measurements(current)
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 2

    report: dict[str, Any] = {
        "version": 2,
        "current": str(args.current.resolve()),
        "thresholds_percent": {
            "warn": args.warn_percent,
            "investigate": args.investigate_percent,
            "fail": args.fail_percent,
        },
        "machine_quality_thresholds_percent": {
            "instruction_fail": args.instruction_fail_percent,
            "stack_memory_fail": args.stack_memory_fail_percent,
        },
    }
    if args.baseline is None or not args.baseline.is_file():
        report.update(
            {
                "status": "baseline_missing",
                "next_action": "approve a native benchmark report as the baseline before enforcing regressions",
                "workloads": {
                    workload: {
                        "current_hitsimple_median_ns": values["hitsimple"],
                        "current_cpp_median_ns": values["cpp"],
                        "status": "baseline_missing",
                    }
                    for workload, values in current_measurements.items()
                },
            }
        )
        write_json(args.output, report)
        print("benchmark baseline is unavailable; saved a non-gating trend report")
        return 0

    try:
        baseline = read_json(args.baseline)
        baseline_measurements = workload_measurements(baseline)
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 2

    compatibility_differences = baseline_compatibility(current, baseline)
    if compatibility_differences:
        report.update(
            {
                "status": "baseline_incompatible",
                "baseline": str(args.baseline.resolve()),
                "next_action": "run on hardware matching the approved baseline before enforcing regressions",
                "compatibility_differences": compatibility_differences,
            }
        )
        write_json(args.output, report)
        print("benchmark baseline is incompatible with the current runner; saved a non-gating trend report")
        return 0

    statuses = []
    workloads: dict[str, Any] = {}
    missing_baselines = []
    for workload, current_values in current_measurements.items():
        if workload not in baseline_measurements:
            missing_baselines.append(workload)
            workloads[workload] = {
                "current_hitsimple_median_ns": current_values["hitsimple"],
                "current_cpp_median_ns": current_values["cpp"],
                "status": "baseline_missing",
            }
            continue
        baseline_values = baseline_measurements[workload]
        baseline_ratio = baseline_values["hitsimple"] / baseline_values["cpp"]
        current_ratio = current_values["hitsimple"] / current_values["cpp"]
        regression_percent = (current_ratio / baseline_ratio - 1.0) * 100.0
        absolute_hitsimple_delta_percent = (
            current_values["hitsimple"] / baseline_values["hitsimple"] - 1.0
        ) * 100.0
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
            "baseline_hitsimple_median_ns": baseline_values["hitsimple"],
            "baseline_cpp_median_ns": baseline_values["cpp"],
            "current_hitsimple_median_ns": current_values["hitsimple"],
            "current_cpp_median_ns": current_values["cpp"],
            "baseline_hs_cpp_ratio": baseline_ratio,
            "current_hs_cpp_ratio": current_ratio,
            "ratio_regression_percent": regression_percent,
            "hitsimple_absolute_delta_percent": absolute_hitsimple_delta_percent,
            "status": status,
        }

    current_quality = quality_metrics(current)
    baseline_quality = quality_metrics(baseline)
    quality_workloads: dict[str, Any] = {}
    missing_quality_baselines = []
    for workload, current_metrics in current_quality.items():
        if workload not in baseline_quality:
            missing_quality_baselines.append(workload)
            quality_workloads[workload] = {
                "current": current_metrics,
                "status": "baseline_missing",
            }
            continue
        baseline_metrics = baseline_quality[workload]
        metric_results: dict[str, Any] = {}
        quality_status = "pass"
        for metric, threshold in (
            ("instruction_count", args.instruction_fail_percent),
            ("stack_memory_operations", args.stack_memory_fail_percent),
        ):
            baseline_value = baseline_metrics[metric]
            current_value = current_metrics[metric]
            if baseline_value == 0:
                regression_percent = None
                metric_status = "not_comparable"
            else:
                regression_percent = (current_value / baseline_value - 1.0) * 100.0
                metric_status = "failure" if regression_percent > threshold else "pass"
            if metric_status == "failure":
                quality_status = "failure"
            metric_results[metric] = {
                "baseline": baseline_value,
                "current": current_value,
                "regression_percent": regression_percent,
                "status": metric_status,
            }
        statuses.append(quality_status)
        quality_workloads[workload] = {
            "metrics": metric_results,
            "status": quality_status,
        }

    if "failure" in statuses:
        overall = "failure"
    elif missing_baselines or missing_quality_baselines:
        overall = "baseline_incomplete"
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
            "missing_baseline_workloads": missing_baselines,
            "machine_quality": {
                "missing_baseline_workloads": missing_quality_baselines,
                "workloads": quality_workloads,
            },
            "workloads": workloads,
        }
    )
    write_json(args.output, report)
    print("benchmark trend status: " + overall)
    return 1 if overall == "failure" else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
