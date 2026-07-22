"""Execution and artifact handling for generated HitSimple property tests."""

from __future__ import annotations

import json
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional, Sequence

from property_cases import PropertyCase


OPTIMIZATION_LEVELS: tuple[str, ...] = ("O0", "O1", "O2", "O3", "Os")


@dataclass(frozen=True)
class ProcessResult:
    command: tuple[str, ...]
    exit_code: Optional[int]
    stdout: str
    stderr: str
    timed_out: bool


@dataclass(frozen=True)
class VariantResult:
    case: PropertyCase
    optimization: str
    artifact_dir: Path
    compile_result: ProcessResult
    run_result: Optional[ProcessResult]
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
            command=tuple(command),
            exit_code=completed.returncode,
            stdout=completed.stdout.decode("utf-8", errors="replace"),
            stderr=completed.stderr.decode("utf-8", errors="replace"),
            timed_out=False,
        )
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or b""
        stderr = error.stderr or b""
        if isinstance(stdout, str):
            stdout = stdout.encode("utf-8", errors="replace")
        if isinstance(stderr, str):
            stderr = stderr.encode("utf-8", errors="replace")
        return ProcessResult(
            command=tuple(command),
            exit_code=None,
            stdout=stdout.decode("utf-8", errors="replace"),
            stderr=stderr.decode("utf-8", errors="replace"),
            timed_out=True,
        )
    except OSError as error:
        return ProcessResult(
            command=tuple(command),
            exit_code=None,
            stdout="",
            stderr="cannot start process: " + str(error) + "\n",
            timed_out=False,
        )


def _write_process_artifacts(artifact_dir: Path, phase: str, result: ProcessResult) -> None:
    _write_json(artifact_dir / (phase + ".command.json"), list(result.command))
    _write_json(
        artifact_dir / (phase + ".result.json"),
        {"exit_code": result.exit_code, "timed_out": result.timed_out},
    )
    _write_text(artifact_dir / (phase + ".stdout"), result.stdout)
    _write_text(artifact_dir / (phase + ".stderr"), result.stderr)


def _resolve_executable(path: Path) -> Path:
    if path.is_file():
        return path
    windows_path = path.with_suffix(path.suffix + ".exe")
    return windows_path if windows_path.is_file() else path


def _process_failure(
    result: ProcessResult,
    phase: str,
    expected_stdout: Optional[str] = None,
) -> Optional[str]:
    if result.timed_out:
        return phase + " timed out"
    if result.exit_code != 0:
        return phase + " exit code: expected 0, got " + str(result.exit_code)
    if result.stderr:
        return phase + " wrote to stderr"
    if expected_stdout is not None and result.stdout != expected_stdout:
        return phase + " stdout did not match the property oracle"
    return None


def run_variant(
    hsc: Path,
    case: PropertyCase,
    optimization: str,
    case_root: Path,
    timeout_seconds: float,
) -> VariantResult:
    artifact_dir = case_root / optimization
    if artifact_dir.exists():
        shutil.rmtree(artifact_dir)
    artifact_dir.mkdir(parents=True, exist_ok=True)
    source_path = artifact_dir / "source.hs"
    _write_text(source_path, case.source)
    _write_text(artifact_dir / "expected.stdout", case.expected_stdout)
    _write_json(
        artifact_dir / "case.json",
        {
            "name": case.name,
            "families": list(case.families),
            "metadata": case.metadata,
            "optimization": optimization,
            "timeout_seconds": timeout_seconds,
        },
    )

    executable = artifact_dir / "program"
    compile_result = _run_process(
        [str(hsc), "--unchecked", "-" + optimization, str(source_path), "-o", str(executable)],
        artifact_dir,
        timeout_seconds,
    )
    _write_process_artifacts(artifact_dir, "compile", compile_result)
    compile_failure = _process_failure(compile_result, "compile")
    if compile_failure:
        return VariantResult(case, optimization, artifact_dir, compile_result, None, compile_failure)

    run_result = _run_process([str(_resolve_executable(executable))], artifact_dir, timeout_seconds)
    _write_process_artifacts(artifact_dir, "run", run_result)
    run_failure = _process_failure(run_result, "run", case.expected_stdout)
    return VariantResult(case, optimization, artifact_dir, compile_result, run_result, run_failure)


def _differential_mismatches(reference: ProcessResult, candidate: ProcessResult) -> list[str]:
    mismatches = []
    if reference.timed_out != candidate.timed_out:
        mismatches.append("run timeout status differs")
    if reference.exit_code != candidate.exit_code:
        mismatches.append("run exit code differs")
    if reference.stdout != candidate.stdout:
        mismatches.append("run stdout differs")
    if reference.stderr != candidate.stderr:
        mismatches.append("run stderr differs")
    return mismatches


def _with_failure(result: VariantResult, failure: str) -> VariantResult:
    combined = failure if result.failure is None else result.failure + "; " + failure
    return VariantResult(
        result.case,
        result.optimization,
        result.artifact_dir,
        result.compile_result,
        result.run_result,
        combined,
    )


def apply_differential_checks(results: Sequence[VariantResult], case_root: Path) -> Sequence[VariantResult]:
    indexed = list(results)
    reference_index = next(index for index, result in enumerate(indexed) if result.optimization == "O0")
    reference = indexed[reference_index]
    variants = []
    for index, result in enumerate(indexed):
        mismatches: list[str] = []
        if index != reference_index:
            if reference.run_result is None or result.run_result is None:
                mismatches = ["run result is unavailable for comparison"]
            else:
                mismatches = _differential_mismatches(reference.run_result, result.run_result)
            if mismatches:
                result = _with_failure(
                    result,
                    "differential O0 vs " + result.optimization + " " + "; ".join(mismatches),
                )
                indexed[index] = result
        variants.append(
            {
                "optimization": result.optimization,
                "artifact_dir": str(result.artifact_dir),
                "compile_exit_code": result.compile_result.exit_code,
                "run_exit_code": result.run_result.exit_code if result.run_result else None,
                "matches_o0": not mismatches,
                "mismatches": mismatches,
            }
        )
    _write_json(
        case_root / "differential.json",
        {"version": 1, "reference_optimization": "O0", "variants": variants},
    )
    return indexed


def run_cases(
    hsc: Path,
    cases: Sequence[PropertyCase],
    artifact_root: Path,
    seed: int,
    timeout_seconds: float,
) -> Sequence[VariantResult]:
    all_results = []
    seed_root = artifact_root / ("seed-" + str(seed))
    for case in cases:
        case_root = seed_root / case.name
        case_results = [
            run_variant(hsc, case, optimization, case_root, timeout_seconds)
            for optimization in OPTIMIZATION_LEVELS
        ]
        all_results.extend(apply_differential_checks(case_results, case_root))
    return all_results
