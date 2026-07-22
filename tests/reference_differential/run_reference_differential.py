#!/usr/bin/env python3
"""Compile and compare restricted HitSimple and C reference programs on host."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional, Sequence

from ReferenceCases import ReferenceCase, generate_cases


HSC_OPTIMIZATIONS: tuple[str, ...] = ("O0", "O2")
DEFAULT_SEED = 20260722
DEFAULT_TRIALS = 2
DEFAULT_TIMEOUT_SECONDS = 10.0


@dataclass(frozen=True)
class ProcessResult:
    command: tuple[str, ...]
    exit_code: Optional[int]
    stdout: str
    stderr: str
    timed_out: bool


@dataclass(frozen=True)
class CaseResult:
    case: ReferenceCase
    failure: Optional[str]

    @property
    def passed(self) -> bool:
        return self.failure is None


def _write_text(path: Path, content: str) -> None:
    path.write_text(content, encoding="utf-8")


def _write_json(path: Path, value: Any) -> None:
    _write_text(path, json.dumps(value, indent=2, sort_keys=True) + "\n")


def _run_process(command: Sequence[str], cwd: Path, timeout_seconds: float) -> ProcessResult:
    try:
        completed = subprocess.run(
            list(command),
            cwd=str(cwd),
            stdin=subprocess.DEVNULL,
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


def _write_process_artifacts(directory: Path, phase: str, result: ProcessResult) -> None:
    _write_json(directory / (phase + ".command.json"), list(result.command))
    _write_json(
        directory / (phase + ".result.json"),
        {"exit_code": result.exit_code, "timed_out": result.timed_out},
    )
    _write_text(directory / (phase + ".stdout"), result.stdout)
    _write_text(directory / (phase + ".stderr"), result.stderr)


def _resolve_executable(path: Path) -> Path:
    if path.is_file():
        return path
    windows_path = path.with_suffix(path.suffix + ".exe")
    return windows_path if windows_path.is_file() else path


def observable_mismatches(reference: ProcessResult, candidate: ProcessResult) -> list[str]:
    """Return observable process-result fields that differ from the C oracle."""

    return [
        field
        for field in ("timed_out", "exit_code", "stdout", "stderr")
        if getattr(reference, field) != getattr(candidate, field)
    ]


def _result_summary(result: Optional[ProcessResult]) -> dict[str, object]:
    if result is None:
        return {"available": False}
    return {
        "available": True,
        "exit_code": result.exit_code,
        "timed_out": result.timed_out,
        "stdout": result.stdout,
        "stderr": result.stderr,
    }


def _write_comparison(
    case_root: Path,
    case: ReferenceCase,
    runs: dict[str, Optional[ProcessResult]],
) -> list[str]:
    reference = runs["reference"]
    mismatches: dict[str, list[str]] = {}
    expected_failures: list[str] = []
    if reference is None:
        expected_failures.append("reference run result is unavailable")
    else:
        if reference.stdout != case.expected_stdout:
            expected_failures.append("reference stdout differs from semantic model")
        for label in ("hsc-O0", "hsc-O2"):
            candidate = runs[label]
            if candidate is None:
                mismatches[label] = ["run result is unavailable"]
            else:
                mismatch = observable_mismatches(reference, candidate)
                if mismatch:
                    mismatches[label] = mismatch
                if candidate.stdout != case.expected_stdout:
                    expected_failures.append(label + " stdout differs from semantic model")
    _write_json(
        case_root / "comparison.json",
        {
            "version": 1,
            "reference": "clang-c11-O2",
            "expected_stdout": case.expected_stdout,
            "runs": {label: _result_summary(result) for label, result in runs.items()},
            "mismatches": mismatches,
            "semantic_model_failures": expected_failures,
        },
    )
    messages = [label + ": " + ", ".join(fields) for label, fields in mismatches.items()]
    return messages + expected_failures


def _compile_and_run(
    command: Sequence[str], executable: Path, directory: Path, timeout_seconds: float
) -> tuple[Optional[ProcessResult], Optional[str]]:
    compile_result = _run_process(command, directory, timeout_seconds)
    _write_process_artifacts(directory, "compile", compile_result)
    if compile_result.timed_out:
        return None, "compile timed out"
    if compile_result.exit_code != 0:
        return None, "compile exited with " + str(compile_result.exit_code)
    run_result = _run_process([str(_resolve_executable(executable))], directory, timeout_seconds)
    _write_process_artifacts(directory, "run", run_result)
    if run_result.timed_out:
        return run_result, "run timed out"
    if run_result.exit_code != 0:
        return run_result, "run exited with " + str(run_result.exit_code)
    return run_result, None


def _execute_case(
    hsc: Path, clang: Path, case: ReferenceCase, case_root: Path, timeout_seconds: float
) -> CaseResult:
    _write_text(case_root / "source.hs", case.hs_source)
    _write_text(case_root / "reference.c", case.c_source)
    _write_json(
        case_root / "case.json",
        {
            "name": case.name,
            "family": case.family,
            "metadata": case.metadata,
            "expected_stdout": case.expected_stdout,
            "timeout_seconds": timeout_seconds,
        },
    )

    runs: dict[str, Optional[ProcessResult]] = {"reference": None, "hsc-O0": None, "hsc-O2": None}
    failures: list[str] = []
    reference_root = case_root / "reference"
    reference_root.mkdir(parents=True, exist_ok=True)
    reference_program = reference_root / "program"
    reference_result, reference_failure = _compile_and_run(
        [str(clang), "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", str(case_root / "reference.c"), "-o", str(reference_program)],
        reference_program,
        reference_root,
        timeout_seconds,
    )
    runs["reference"] = reference_result
    if reference_failure:
        failures.append("reference: " + reference_failure)

    for optimization in HSC_OPTIMIZATIONS:
        label = "hsc-" + optimization
        implementation_root = case_root / label
        implementation_root.mkdir(parents=True, exist_ok=True)
        program = implementation_root / "program"
        result, failure = _compile_and_run(
            [str(hsc), "--unchecked", "-" + optimization, str(case_root / "source.hs"), "-o", str(program)],
            program,
            implementation_root,
            timeout_seconds,
        )
        runs[label] = result
        if failure:
            failures.append(label + ": " + failure)

    failures.extend(_write_comparison(case_root, case, runs))
    return CaseResult(case, "; ".join(failures) if failures else None)


def _positive_int(value: str) -> int:
    try:
        parsed = int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be an integer") from error
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return parsed


def _positive_float(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be a number") from error
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return parsed


def parse_arguments(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hsc", required=True, type=Path, help="path to hsc")
    parser.add_argument("--clang", required=True, type=Path, help="path to the C reference compiler")
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--trials", type=_positive_int, default=DEFAULT_TRIALS)
    parser.add_argument("--timeout", type=_positive_float, default=DEFAULT_TIMEOUT_SECONDS)
    parser.add_argument("--artifacts", type=Path, help="directory for source and process artifacts")
    parser.add_argument("--list", action="store_true", help="list cases without compiling")
    return parser.parse_args(argv)


def _validate_executable(label: str, path: Path) -> Optional[str]:
    if not path.is_file():
        return label + " does not exist: " + str(path)
    if os.name != "nt" and not os.access(str(path), os.X_OK):
        return label + " is not executable: " + str(path)
    return None


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_arguments(argv)
    hsc = args.hsc.resolve()
    clang = args.clang.resolve()
    for label, path in (("--hsc", hsc), ("--clang", clang)):
        validation_error = _validate_executable(label, path)
        if validation_error:
            print("reference differential: error: " + validation_error, file=sys.stderr)
            return 2

    cases = generate_cases(args.seed, args.trials)
    for case in cases:
        print(case.name + " [" + case.family + "]")
    if args.list:
        return 0

    artifact_root = (args.artifacts or Path.cwd() / "build" / "test-artifacts" / "reference-differential").resolve()
    seed_root = artifact_root / ("seed-" + str(args.seed))
    seed_root.mkdir(parents=True, exist_ok=True)
    results: list[CaseResult] = []
    for case in cases:
        case_root = seed_root / case.name
        if case_root.exists():
            shutil.rmtree(case_root)
        case_root.mkdir(parents=True)
        results.append(_execute_case(hsc, clang, case, case_root, args.timeout))

    for result in results:
        status = "PASS" if result.passed else "FAIL"
        message = "[" + status + "] " + result.case.name + " [" + result.case.family + "]"
        if result.failure:
            message += ": " + result.failure
        print(message)
    passed = sum(result.passed for result in results)
    print(str(passed) + "/" + str(len(results)) + " PASS")
    print("Seed: " + str(args.seed))
    print("Artifacts: " + str(artifact_root))
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
