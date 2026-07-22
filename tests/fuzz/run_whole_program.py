#!/usr/bin/env python3
"""Run deterministic grammar-aware whole-program HitSimple fuzz cases."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional, Sequence

from WholeProgramCases import WholeProgramCase, generate_cases
from WholeProgramReducer import ReductionResult, reduce_source


OPTIMIZATIONS: tuple[str, ...] = ("O0", "O2", "O3")
DEFAULT_SEED = 20260722
DEFAULT_VALID_CASES = 4
DEFAULT_INVALID_CASES = 7
DEFAULT_MAX_STATEMENTS = 7
DEFAULT_TIMEOUT_SECONDS = 10.0
DEFAULT_REDUCTION_ATTEMPTS = 48


@dataclass(frozen=True)
class ProcessResult:
    command: tuple[str, ...]
    exit_code: Optional[int]
    stdout: str
    stderr: str
    timed_out: bool


@dataclass(frozen=True)
class CaseResult:
    case: WholeProgramCase
    failure_signature: Optional[str]
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


def _diagnostic_detail(stderr: str) -> str:
    for line in stderr.splitlines():
        match = re.search(r"(?:^|: )(?:[a-z-]+: )?error: (.+)$", line)
        if match:
            return match.group(1).strip()
    return stderr.strip().split("\n", 1)[0][:240] or "no diagnostic output"


def _compile_command(hsc: Path, source: Path, executable: Path, optimization: str) -> list[str]:
    return [str(hsc), "--unchecked", "-" + optimization, str(source), "-o", str(executable)]


def _run_valid_case(
    hsc: Path, case: WholeProgramCase, case_root: Path, timeout_seconds: float
) -> CaseResult:
    source_path = case_root / "source.hs"
    run_results: dict[str, ProcessResult] = {}
    signature: Optional[str] = None
    failure: Optional[str] = None

    for optimization in OPTIMIZATIONS:
        variant_root = case_root / optimization
        variant_root.mkdir(parents=True, exist_ok=True)
        executable = variant_root / "program"
        compile_result = _run_process(
            _compile_command(hsc, source_path, executable, optimization), variant_root, timeout_seconds
        )
        _write_process_artifacts(variant_root, "compile", compile_result)
        if compile_result.timed_out:
            signature = "valid-compile-timeout:" + optimization
            failure = "compile timed out at -" + optimization
            break
        if compile_result.exit_code != 0:
            detail = _diagnostic_detail(compile_result.stderr)
            signature = "valid-compile-diagnostic:" + detail
            failure = "compile rejected a generated valid program at -" + optimization + ": " + detail
            break
        if compile_result.stderr:
            detail = _diagnostic_detail(compile_result.stderr)
            signature = "valid-compile-stderr:" + detail
            failure = "compile wrote to stderr at -" + optimization + ": " + detail
            break

        run_result = _run_process([str(_resolve_executable(executable))], variant_root, timeout_seconds)
        _write_process_artifacts(variant_root, "run", run_result)
        run_results[optimization] = run_result
        if run_result.timed_out:
            signature = "valid-run-timeout:" + optimization
            failure = "generated executable timed out at -" + optimization
            break
        if run_result.exit_code != 0:
            signature = "valid-run-exit:" + str(run_result.exit_code)
            failure = "generated executable failed at -" + optimization
            break
        if run_result.stderr:
            signature = "valid-run-stderr:" + _diagnostic_detail(run_result.stderr)
            failure = "generated executable wrote to stderr at -" + optimization
            break

    if failure is None:
        reference = run_results["O0"]
        variants: list[dict[str, object]] = []
        mismatched_fields: list[str] = []
        for optimization in OPTIMIZATIONS:
            result = run_results[optimization]
            mismatches = []
            for name in ("timed_out", "exit_code", "stdout", "stderr"):
                if getattr(reference, name) != getattr(result, name):
                    mismatches.append(name)
            if optimization != "O0" and mismatches:
                mismatched_fields.extend(optimization + ":" + item for item in mismatches)
            variants.append(
                {
                    "optimization": optimization,
                    "exit_code": result.exit_code,
                    "timed_out": result.timed_out,
                    "matches_o0": not mismatches,
                    "mismatches": mismatches,
                }
            )
        _write_json(
            case_root / "differential.json",
            {"version": 1, "reference_optimization": "O0", "variants": variants},
        )
        if mismatched_fields:
            signature = "optimization-differential:" + ",".join(mismatched_fields)
            failure = "-O0 observable result differs from optimized variants: " + ", ".join(
                mismatched_fields
            )

    return CaseResult(case, signature, failure)


def _run_invalid_case(
    hsc: Path, case: WholeProgramCase, case_root: Path, timeout_seconds: float
) -> CaseResult:
    source_path = case_root / "source.hs"
    executable = case_root / "program"
    result = _run_process(_compile_command(hsc, source_path, executable, "O0"), case_root, timeout_seconds)
    _write_process_artifacts(case_root, "compile", result)
    if result.timed_out:
        return CaseResult(case, "invalid-compile-timeout", "invalid program compilation timed out")
    if result.exit_code == 0:
        return CaseResult(case, "invalid-accepted", "invalid program compiled successfully")
    if "internal error" in result.stderr.lower():
        return CaseResult(
            case,
            "invalid-internal-error:" + _diagnostic_detail(result.stderr),
            "invalid program reached an internal compiler error",
        )
    expected = case.expected_diagnostic
    if expected is None or expected not in result.stderr:
        actual = _diagnostic_detail(result.stderr)
        return CaseResult(
            case,
            "invalid-diagnostic:" + actual,
            "expected diagnostic fragment was not observed: " + str(expected),
        )
    return CaseResult(case, None, None)


def _execute_case(
    hsc: Path, case: WholeProgramCase, case_root: Path, timeout_seconds: float
) -> CaseResult:
    if case.kind == "valid":
        return _run_valid_case(hsc, case, case_root, timeout_seconds)
    return _run_invalid_case(hsc, case, case_root, timeout_seconds)


def _write_case_metadata(case_root: Path, case: WholeProgramCase, timeout_seconds: float) -> None:
    _write_text(case_root / "source.hs", case.source)
    _write_json(
        case_root / "case.json",
        {
            "name": case.name,
            "kind": case.kind,
            "expected_diagnostic": case.expected_diagnostic,
            "metadata": case.metadata,
            "timeout_seconds": timeout_seconds,
        },
    )


def _reduce_failure(
    hsc: Path,
    case: WholeProgramCase,
    case_root: Path,
    timeout_seconds: float,
    original: CaseResult,
    max_attempts: int,
) -> ReductionResult:
    if original.failure_signature is None:
        raise ValueError("cannot reduce a passing case")

    probe_root = case_root / "reduction-probe"

    def preserves_failure(candidate_source: str) -> bool:
        if probe_root.exists():
            shutil.rmtree(probe_root)
        probe_root.mkdir(parents=True, exist_ok=True)
        candidate = WholeProgramCase(
            case.name,
            case.kind,
            candidate_source,
            case.expected_diagnostic,
            case.metadata,
            case.removable_fragments,
        )
        _write_case_metadata(probe_root, candidate, timeout_seconds)
        result = _execute_case(hsc, candidate, probe_root, timeout_seconds)
        return result.failure_signature == original.failure_signature

    result = reduce_source(
        case.source,
        preserves_failure,
        case.removable_fragments,
        max_attempts,
    )
    _write_text(case_root / "reduced.hs", result.source)
    _write_json(
        case_root / "reduction.json",
        {
            "failure_signature": original.failure_signature,
            "original_bytes": len(case.source.encode("utf-8")),
            "reduced_bytes": len(result.source.encode("utf-8")),
            "attempts": result.attempts,
            "accepted_transformations": result.accepted_transformations,
        },
    )
    shutil.rmtree(probe_root, ignore_errors=True)
    return result


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
    parser.add_argument("--hsc", required=True, type=Path, help="path to the hsc executable")
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED, help="deterministic generator seed")
    parser.add_argument("--valid-cases", type=_positive_int, default=DEFAULT_VALID_CASES)
    parser.add_argument("--invalid-cases", type=_positive_int, default=DEFAULT_INVALID_CASES)
    parser.add_argument("--max-statements", type=_positive_int, default=DEFAULT_MAX_STATEMENTS)
    parser.add_argument("--timeout", type=_positive_float, default=DEFAULT_TIMEOUT_SECONDS)
    parser.add_argument("--artifacts", type=Path, help="directory for sources and process artifacts")
    parser.add_argument("--reduction-attempts", type=_positive_int, default=DEFAULT_REDUCTION_ATTEMPTS)
    parser.add_argument("--no-reduce", action="store_true", help="do not minimize a discovered failure")
    parser.add_argument("--list", action="store_true", help="list generated cases without compiling")
    return parser.parse_args(argv)


def _validate_hsc(path: Path) -> Optional[str]:
    if not path.is_file():
        return "--hsc does not exist: " + str(path)
    if os.name != "nt" and not os.access(str(path), os.X_OK):
        return "--hsc is not executable: " + str(path)
    return None


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_arguments(argv)
    hsc = args.hsc.resolve()
    validation_error = _validate_hsc(hsc)
    if validation_error:
        print("whole-program fuzz: error: " + validation_error, file=sys.stderr)
        return 2
    if args.max_statements < 3:
        print("whole-program fuzz: error: --max-statements must be at least 3", file=sys.stderr)
        return 2

    cases = generate_cases(args.seed, args.valid_cases, args.invalid_cases, args.max_statements)
    for case in cases:
        print(case.name + " [" + case.kind + "]")
    if args.list:
        return 0

    artifact_root = (
        args.artifacts or Path.cwd() / "build" / "test-artifacts" / "whole-program-fuzz"
    ).resolve()
    artifact_root.mkdir(parents=True, exist_ok=True)
    seed_root = artifact_root / ("seed-" + str(args.seed))
    results: list[CaseResult] = []
    for case in cases:
        case_root = seed_root / case.name
        if case_root.exists():
            shutil.rmtree(case_root)
        case_root.mkdir(parents=True, exist_ok=True)
        _write_case_metadata(case_root, case, args.timeout)
        result = _execute_case(hsc, case, case_root, args.timeout)
        if result.failure and not args.no_reduce:
            _reduce_failure(hsc, case, case_root, args.timeout, result, args.reduction_attempts)
        results.append(result)

    for result in results:
        status = "PASS" if result.passed else "FAIL"
        message = "[" + status + "] " + result.case.name + " [" + result.case.kind + "]"
        if result.failure:
            message += ": " + result.failure
        print(message)
    passed = sum(1 for result in results if result.passed)
    print(str(passed) + "/" + str(len(results)) + " PASS")
    print("Seed: " + str(args.seed))
    print("Optimizations: " + ", ".join("-" + item for item in OPTIMIZATIONS))
    print("Artifacts: " + str(artifact_root))
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
