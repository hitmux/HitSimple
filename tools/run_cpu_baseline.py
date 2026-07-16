#!/usr/bin/env python3
"""Run the reproducible Phase 0 CPU compiler baseline.

The runner deliberately invokes one hsc command at a time.  It measures the
normal end-to-end compiler path separately from the same path with
--timing-json enabled, so instrumentation overhead is reported instead of
being folded into the CPU baseline.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Sequence


RUNNER_SCHEMA_VERSION = 1
TIMING_SCHEMA_VERSION = 1


@dataclass(frozen=True)
class Workload:
    identifier: str
    description: str
    source_paths: tuple[str, ...]
    compiler_options: tuple[str, ...] = ()


WORKLOADS: tuple[Workload, ...] = (
    Workload(
        "comprehensive_project_unchecked",
        "Existing comprehensive HitSimple example using the unchecked mode.",
        ("examples/comprehensive_project.hs",),
        ("--unchecked",),
    ),
    Workload(
        "multifile_fixture",
        "Existing two-translation-unit HitSimple runtime fixture.",
        (
            "tests/cases/run/multifile/lib.hs",
            "tests/cases/run/multifile/main.hs",
        ),
    ),
    Workload(
        "stdlib_core_fixture",
        "Existing standard-library HitSimple runtime fixture.",
        ("tests/cases/run/stdlib_core.hs",),
    ),
    Workload(
        "c_compat_runtime_double",
        "Existing C compatibility runtime fixture.",
        ("tests/cases/compat/c_runtime_double.c",),
        ("--c-compat",),
    ),
)


class BaselineError(RuntimeError):
    pass


def parse_non_negative(value: str) -> int:
    parsed = int(value)
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be non-negative")
    return parsed


def parse_positive(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return parsed


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_first_line(path: Path) -> str | None:
    try:
        with path.open(encoding="utf-8", errors="replace") as source:
            return source.readline().strip() or None
    except OSError:
        return None


def read_os_release() -> dict[str, str]:
    path = Path("/etc/os-release")
    if not path.is_file():
        return {}

    result: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        result[key] = value.strip().strip('"')
    return result


def read_linux_meminfo() -> dict[str, int]:
    path = Path("/proc/meminfo")
    if not path.is_file():
        return {}

    result: dict[str, int] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        key, separator, value = line.partition(":")
        if not separator:
            continue
        fields = value.split()
        if not fields or not fields[0].isdigit():
            continue
        multiplier = 1024 if len(fields) > 1 and fields[1] == "kB" else 1
        result[key] = int(fields[0]) * multiplier
    return result


def cpu_model() -> str | None:
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.is_file():
        for line in cpuinfo.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("model name") and ":" in line:
                return line.split(":", 1)[1].strip()
    processor = platform.processor().strip()
    return processor or None


def capture_command(command: Sequence[str], timeout_seconds: int = 10) -> str | None:
    try:
        completed = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout_seconds,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None

    output = completed.stdout.strip()
    if output:
        return output
    return completed.stderr.strip() or None


def detect_gpu_environment() -> dict[str, Any]:
    return {
        "opencl": {
            "clinfo_path": shutil.which("clinfo"),
            "note": "OpenCL device details are recorded by the FlowIR GPU analysis report.",
        }
    }


def configured_clang_path(hsc: Path, clang: Path | None) -> str | None:
    command = [str(hsc)]
    if clang is not None:
        command.extend(("--clang", str(clang)))
    command.append("--target-info")
    target_info = capture_command(command)
    if target_info:
        for line in target_info.splitlines():
            key, separator, value = line.partition(":")
            if key == "clang.path" and separator:
                return value.strip() or None
    return str(clang) if clang is not None else shutil.which("clang")


def collect_environment(hsc: Path, clang: Path | None) -> dict[str, Any]:
    memory = read_linux_meminfo()
    allowed_cpus: list[int] | None = None
    if hasattr(os, "sched_getaffinity"):
        try:
            allowed_cpus = sorted(os.sched_getaffinity(0))
        except OSError:
            pass

    clang_command = configured_clang_path(hsc, clang)
    return {
        "operating_system": {
            "system": platform.system(),
            "release": platform.release(),
            "version": platform.version(),
            "machine": platform.machine(),
            "os_release": read_os_release(),
        },
        "cpu": {
            "model": cpu_model(),
            "logical_cpu_count": os.cpu_count(),
            "allowed_cpu_ids": allowed_cpus,
        },
        "memory": {
            "mem_total_bytes": memory.get("MemTotal"),
            "mem_available_bytes": memory.get("MemAvailable"),
        },
        "gpu": detect_gpu_environment(),
        "compilers": {
            "hsc_path": str(hsc),
            "hsc_version": capture_command([str(hsc), "--version"]),
            "clang_path": clang_command,
            "clang_version": capture_command([clang_command, "--version"])
            if clang_command
            else None,
            "python_version": sys.version,
        },
    }


def summarize(values: Iterable[int]) -> dict[str, float | int]:
    measured = list(values)
    if not measured:
        raise BaselineError("cannot summarize an empty measurement set")
    median = statistics.median(measured)
    mean = statistics.fmean(measured)
    standard_deviation = statistics.pstdev(measured) if len(measured) > 1 else 0.0
    return {
        "count": len(measured),
        "minimum_ns": min(measured),
        "median_ns": median,
        "mean_ns": mean,
        "maximum_ns": max(measured),
        "population_standard_deviation_ns": standard_deviation,
        "relative_standard_deviation": standard_deviation / mean if mean else 0.0,
    }


def select_workloads(selected_ids: Sequence[str]) -> tuple[Workload, ...]:
    by_identifier = {workload.identifier: workload for workload in WORKLOADS}
    if not selected_ids:
        return WORKLOADS
    selected: list[Workload] = []
    for identifier in selected_ids:
        workload = by_identifier.get(identifier)
        if workload is None:
            raise BaselineError(f"unknown workload '{identifier}'")
        selected.append(workload)
    return tuple(selected)


def source_metadata(project_root: Path, workload: Workload) -> list[dict[str, Any]]:
    metadata: list[dict[str, Any]] = []
    for relative_path in workload.source_paths:
        path = project_root / relative_path
        if not path.is_file():
            raise BaselineError(
                f"workload '{workload.identifier}' source is missing: {path}"
            )
        metadata.append(
            {
                "path": relative_path,
                "bytes": path.stat().st_size,
                "sha256": sha256_file(path),
            }
        )
    return metadata


def build_command(
    hsc: Path,
    clang: Path | None,
    project_root: Path,
    workload: Workload,
    executable_path: Path,
    timing_path: Path | None,
) -> list[str]:
    command = [str(hsc)]
    if clang is not None:
        command.extend(("--clang", str(clang)))
    if timing_path is not None:
        command.append(f"--timing-json={timing_path}")
    command.extend(workload.compiler_options)
    command.extend(str(project_root / source) for source in workload.source_paths)
    command.extend(("-o", str(executable_path)))
    return command


def load_timing_metrics(path: Path) -> dict[str, Any]:
    try:
        metrics = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BaselineError(f"cannot read timing JSON '{path}': {error}") from error

    if metrics.get("schema_version") != TIMING_SCHEMA_VERSION:
        raise BaselineError(
            f"unexpected timing schema in '{path}': {metrics.get('schema_version')}"
        )
    if not metrics.get("completed") or not metrics.get("succeeded"):
        raise BaselineError(f"timing JSON records an incomplete compilation: {path}")
    return metrics


def run_once(
    hsc: Path,
    clang: Path | None,
    project_root: Path,
    workload: Workload,
    workspace: Path,
    sequence_number: int,
    with_timing: bool,
    timeout_seconds: int,
) -> dict[str, Any]:
    suffix = ".exe" if os.name == "nt" else ""
    executable_path = workspace / f"{workload.identifier}-{sequence_number}{suffix}"
    timing_path = (
        workspace / f"{workload.identifier}-{sequence_number}.timing.json"
        if with_timing
        else None
    )
    command = build_command(
        hsc, clang, project_root, workload, executable_path, timing_path
    )

    started_at = time.perf_counter_ns()
    try:
        completed = subprocess.run(
            command,
            cwd=project_root,
            check=False,
            capture_output=True,
            timeout=timeout_seconds,
        )
    except subprocess.TimeoutExpired as error:
        raise BaselineError(
            f"workload '{workload.identifier}' timed out after {timeout_seconds} seconds"
        ) from error
    elapsed = time.perf_counter_ns() - started_at

    if completed.returncode != 0:
        stderr = completed.stderr.decode("utf-8", errors="replace").strip()
        raise BaselineError(
            f"workload '{workload.identifier}' failed with exit code "
            f"{completed.returncode}: {stderr}"
        )
    if not executable_path.is_file():
        raise BaselineError(
            f"workload '{workload.identifier}' succeeded without creating {executable_path}"
        )

    observation: dict[str, Any] = {
        "sequence_number": sequence_number,
        "process_wall_time_ns": elapsed,
        "stdout_bytes": len(completed.stdout),
        "stderr_bytes": len(completed.stderr),
    }
    if timing_path is not None:
        observation["timing_metrics"] = load_timing_metrics(timing_path)
    return observation


def run_workload(
    hsc: Path,
    clang: Path | None,
    project_root: Path,
    workload: Workload,
    workspace: Path,
    warmups: int,
    repetitions: int,
    timeout_seconds: int,
) -> dict[str, Any]:
    sequence_number = 0
    modes = (False, True)
    for warmup_index in range(warmups):
        order = modes if warmup_index % 2 == 0 else tuple(reversed(modes))
        for with_timing in order:
            run_once(
                hsc,
                clang,
                project_root,
                workload,
                workspace,
                sequence_number,
                with_timing,
                timeout_seconds,
            )
            sequence_number += 1

    observations: dict[bool, list[dict[str, Any]]] = {False: [], True: []}
    for repetition_index in range(repetitions):
        order = modes if repetition_index % 2 == 0 else tuple(reversed(modes))
        for with_timing in order:
            observation = run_once(
                hsc,
                clang,
                project_root,
                workload,
                workspace,
                sequence_number,
                with_timing,
                timeout_seconds,
            )
            observations[with_timing].append(observation)
            sequence_number += 1

    without_timing = observations[False]
    with_timing = observations[True]
    without_summary = summarize(
        observation["process_wall_time_ns"] for observation in without_timing
    )
    with_summary = summarize(
        observation["process_wall_time_ns"] for observation in with_timing
    )
    baseline_median = float(without_summary["median_ns"])
    instrumented_median = float(with_summary["median_ns"])

    timing_internal_summary = summarize(
        int(observation["timing_metrics"]["program"]["wall_time_ns"])
        for observation in with_timing
    )
    return {
        "identifier": workload.identifier,
        "description": workload.description,
        "compiler_options": list(workload.compiler_options),
        "sources": source_metadata(project_root, workload),
        "warmup_runs_per_mode": warmups,
        "measurements": {
            "without_timing": {
                "observations": without_timing,
                "process_wall_time_summary": without_summary,
            },
            "with_timing": {
                "observations": with_timing,
                "process_wall_time_summary": with_summary,
                "hsc_internal_wall_time_summary": timing_internal_summary,
            },
        },
        "instrumentation_overhead": {
            "basis": "median process wall time across separately measured invocations",
            "median_delta_ns": instrumented_median - baseline_median,
            "median_relative_overhead": (
                instrumented_median / baseline_median - 1.0
                if baseline_median
                else None
            ),
        },
    }


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = path.with_name(f".{path.name}.tmp")
    with temporary_path.open("w", encoding="utf-8") as output:
        json.dump(value, output, indent=2, sort_keys=True, allow_nan=False)
        output.write("\n")
    temporary_path.replace(path)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Measure the Phase 0 sequential CPU compiler baseline and the "
            "overhead of --timing-json."
        )
    )
    parser.add_argument(
        "--project-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="HitSimple repository root (default: inferred from this script)",
    )
    parser.add_argument(
        "--hsc",
        type=Path,
        default=Path("build/hsc"),
        help="path to the hsc binary, relative to --project-root when relative",
    )
    parser.add_argument(
        "--clang",
        type=Path,
        help="optional clang executable passed to hsc via --clang",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="versioned JSON report path",
    )
    parser.add_argument(
        "--runs",
        type=parse_positive,
        default=7,
        help="measured repetitions per workload and mode (default: 7)",
    )
    parser.add_argument(
        "--warmups",
        type=parse_non_negative,
        default=1,
        help="discarded warmup repetitions per workload and mode (default: 1)",
    )
    parser.add_argument(
        "--timeout-seconds",
        type=parse_positive,
        default=180,
        help="timeout for one hsc invocation (default: 180)",
    )
    parser.add_argument(
        "--workload",
        action="append",
        default=[],
        metavar="ID",
        help="measure only this workload; repeat to select multiple workloads",
    )
    parser.add_argument(
        "--list-workloads",
        action="store_true",
        help="print available workload IDs and exit",
    )
    return parser.parse_args()


def resolve_path(path: Path, project_root: Path) -> Path:
    return path.resolve() if path.is_absolute() else (project_root / path).resolve()


def main() -> int:
    arguments = parse_arguments()
    if arguments.list_workloads:
        for workload in WORKLOADS:
            print(f"{workload.identifier}: {workload.description}")
        return 0

    if arguments.output is None:
        raise BaselineError("--output is required unless --list-workloads is used")

    project_root = arguments.project_root.resolve()
    hsc = resolve_path(arguments.hsc, project_root)
    clang = resolve_path(arguments.clang, project_root) if arguments.clang else None
    output_path = resolve_path(arguments.output, project_root)
    if not project_root.is_dir():
        raise BaselineError(f"project root does not exist: {project_root}")
    if not hsc.is_file():
        raise BaselineError(f"hsc binary does not exist: {hsc}")
    if clang is not None and not clang.is_file():
        raise BaselineError(f"clang executable does not exist: {clang}")

    workloads = select_workloads(arguments.workload)
    report: dict[str, Any] = {
        "schema_version": RUNNER_SCHEMA_VERSION,
        "kind": "hitsimple_cpu_single_process_baseline",
        "created_at_utc": datetime.now(timezone.utc).isoformat(),
        "measurement_contract": {
            "runner_parallelism": 1,
            "hsc_invocations_per_measurement": 1,
            "hsc_invocations_are_sequential": True,
            "external_clang_accounting": "hsc reports the current combined clang_backend_link stage",
            "timing_schema_version": TIMING_SCHEMA_VERSION,
            "instrumentation_comparison": "separate alternating hsc invocations with and without --timing-json",
        },
        "configuration": {
            "project_root": str(project_root),
            "hsc": str(hsc),
            "clang": str(clang) if clang else None,
            "runs_per_mode": arguments.runs,
            "warmups_per_mode": arguments.warmups,
            "timeout_seconds": arguments.timeout_seconds,
        },
        "environment": collect_environment(hsc, clang),
        "workloads": [],
    }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=".hsc-cpu-baseline-", dir=output_path.parent
    ) as temporary_directory:
        workspace = Path(temporary_directory)
        for workload in workloads:
            report["workloads"].append(
                run_workload(
                    hsc,
                    clang,
                    project_root,
                    workload,
                    workspace,
                    arguments.warmups,
                    arguments.runs,
                    arguments.timeout_seconds,
                )
            )

    write_json(output_path, report)
    print(output_path)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BaselineError as error:
        print(f"cpu baseline: error: {error}", file=sys.stderr)
        raise SystemExit(1)
