#!/usr/bin/env python3
"""Run manifest-defined HitSimple runtime tests with preserved artifacts.

This first harness deliberately supports only execution on the local host.  The
manifest still carries a target dimension so cross-target runners can be added
without changing the test contract in the later T8 work.
"""

from __future__ import annotations

import argparse
import itertools
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping, Optional, Sequence


MANIFEST_VERSION = 1
DEFAULT_TIMEOUT_SECONDS = 10.0
SAFETY_FLAGS = {
    "unchecked": "--unchecked",
    "checked": "--checked",
    "static-checked": "--static-checked",
}
OPTIMIZATION_FLAGS = {level: "-" + level for level in ("O0", "O1", "O2", "O3", "Os")}
SAFE_PATH_COMPONENT = re.compile(r"[^A-Za-z0-9._-]+")


class HarnessError(Exception):
    """Raised for invalid harness inputs before any test is executed."""


@dataclass(frozen=True)
class ExpectedProcess:
    exit_code: int
    stdout: str
    stderr: str


@dataclass(frozen=True)
class ProcessResult:
    command: Sequence[str]
    exit_code: Optional[int]
    stdout: str
    stderr: str
    timed_out: bool


@dataclass(frozen=True)
class TestCase:
    name: str
    suite: str
    source: Path
    targets: Sequence[str]
    safety_modes: Sequence[str]
    optimization_levels: Sequence[str]
    timeout_seconds: float
    compile_expect: ExpectedProcess
    run_expect: Optional[ExpectedProcess]
    compile_args: Sequence[str]
    run_args: Sequence[str]
    manifest_path: Path


@dataclass(frozen=True)
class TestVariant:
    case: TestCase
    target: str
    safety_mode: str
    optimization_level: str


@dataclass(frozen=True)
class VariantResult:
    variant: TestVariant
    passed: bool
    failure: Optional[str]
    artifact_dir: Path
    compile_result: ProcessResult
    run_result: Optional[ProcessResult]


def _expect_mapping(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise HarnessError(label + " must be an object")
    return value


def _string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise HarnessError(label + " must be a non-empty string")
    return value


def _string_list(value: Any, label: str) -> Sequence[str]:
    if isinstance(value, str):
        values = [value]
    elif isinstance(value, list):
        values = value
    else:
        raise HarnessError(label + " must be a string or a list of strings")
    if not values or any(not isinstance(item, str) or not item for item in values):
        raise HarnessError(label + " must not be empty")
    if len(set(values)) != len(values):
        raise HarnessError(label + " must not contain duplicates")
    return values


def _positive_timeout(value: Any, label: str) -> float:
    if not isinstance(value, (int, float)) or isinstance(value, bool) or value <= 0:
        raise HarnessError(label + " must be a positive number")
    return float(value)


def _expected_process(value: Any, label: str) -> ExpectedProcess:
    data = _expect_mapping(value, label)
    exit_code = data.get("exit_code")
    stdout = data.get("stdout")
    stderr = data.get("stderr")
    if not isinstance(exit_code, int) or isinstance(exit_code, bool):
        raise HarnessError(label + ".exit_code must be an integer")
    if not isinstance(stdout, str) or not isinstance(stderr, str):
        raise HarnessError(label + ".stdout and " + label + ".stderr must be strings")
    return ExpectedProcess(exit_code=exit_code, stdout=stdout, stderr=stderr)


def _optional_string_list(value: Any, label: str) -> Sequence[str]:
    if value is None:
        return []
    return _string_list(value, label)


def _merge_case_value(
    case_data: Mapping[str, Any], defaults: Mapping[str, Any], key: str, fallback: Any
) -> Any:
    if key in case_data:
        return case_data[key]
    if key in defaults:
        return defaults[key]
    return fallback


def load_manifest(manifest_path: Path) -> Sequence[TestCase]:
    try:
        raw = json.loads(manifest_path.read_text(encoding="utf-8"))
    except OSError as error:
        raise HarnessError("cannot read manifest '" + str(manifest_path) + "': " + str(error)) from error
    except json.JSONDecodeError as error:
        raise HarnessError("invalid JSON in manifest '" + str(manifest_path) + "': " + str(error)) from error

    data = _expect_mapping(raw, "manifest")
    if data.get("version") != MANIFEST_VERSION:
        raise HarnessError(
            "manifest '" + str(manifest_path) + "' must declare version " + str(MANIFEST_VERSION)
        )
    suite = _string(data.get("suite"), "manifest.suite")
    defaults = _expect_mapping(data.get("defaults", {}), "manifest.defaults")
    raw_cases = data.get("tests")
    if not isinstance(raw_cases, list) or not raw_cases:
        raise HarnessError("manifest.tests must be a non-empty list")

    cases = []
    names = set()
    for index, raw_case in enumerate(raw_cases):
        label = "manifest.tests[" + str(index) + "]"
        case_data = _expect_mapping(raw_case, label)
        name = _string(case_data.get("name"), label + ".name")
        if name in names:
            raise HarnessError("manifest contains duplicate test name '" + name + "'")
        names.add(name)

        raw_source = _string(case_data.get("source"), label + ".source")
        source = (manifest_path.parent / raw_source).resolve()
        if not source.is_file():
            raise HarnessError("test '" + name + "' source does not exist: " + str(source))

        targets = _string_list(
            _merge_case_value(case_data, defaults, "targets", ["host"]),
            label + ".targets",
        )
        safety_modes = _string_list(
            _merge_case_value(case_data, defaults, "safety", "unchecked"),
            label + ".safety",
        )
        optimization_levels = _string_list(
            _merge_case_value(case_data, defaults, "optimization", "O2"),
            label + ".optimization",
        )
        for target in targets:
            if target != "host":
                raise HarnessError(
                    "test '" + name + "' declares unsupported target '" + target
                    + "'; T1 supports only the local host"
                )
        for mode in safety_modes:
            if mode not in SAFETY_FLAGS:
                raise HarnessError("test '" + name + "' has unsupported safety mode '" + mode + "'")
        for level in optimization_levels:
            if level not in OPTIMIZATION_FLAGS:
                raise HarnessError(
                    "test '" + name + "' has unsupported optimization level '" + level + "'"
                )

        timeout_seconds = _positive_timeout(
            _merge_case_value(case_data, defaults, "timeout_seconds", DEFAULT_TIMEOUT_SECONDS),
            label + ".timeout_seconds",
        )
        expect = _expect_mapping(case_data.get("expect"), label + ".expect")
        compile_expect = _expected_process(
            expect.get("compile", {"exit_code": 0, "stdout": "", "stderr": ""}),
            label + ".expect.compile",
        )
        run_expect = None
        if "run" in expect:
            run_expect = _expected_process(expect["run"], label + ".expect.run")
        elif compile_expect.exit_code == 0:
            raise HarnessError("test '" + name + "' must define expect.run after a successful compile")

        cases.append(
            TestCase(
                name=name,
                suite=suite,
                source=source,
                targets=targets,
                safety_modes=safety_modes,
                optimization_levels=optimization_levels,
                timeout_seconds=timeout_seconds,
                compile_expect=compile_expect,
                run_expect=run_expect,
                compile_args=_optional_string_list(case_data.get("compile_args"), label + ".compile_args"),
                run_args=_optional_string_list(case_data.get("run_args"), label + ".run_args"),
                manifest_path=manifest_path,
            )
        )
    return cases


def discover_manifests(manifest_dir: Path, suites: Sequence[str], explicit: Sequence[Path]) -> Sequence[Path]:
    paths = []
    if explicit:
        paths.extend(path.resolve() for path in explicit)
    else:
        if not suites:
            raise HarnessError("at least one --suite or --manifest is required")
        if "all" in suites:
            paths.extend(sorted(manifest_dir.glob("*.json")))
        else:
            for suite in suites:
                paths.append((manifest_dir / (suite + ".json")).resolve())
    if not paths:
        raise HarnessError("no manifests selected")
    missing = [path for path in paths if not path.is_file()]
    if missing:
        raise HarnessError("manifest does not exist: " + str(missing[0]))
    if len(set(paths)) != len(paths):
        raise HarnessError("the same manifest was selected more than once")
    return paths


def _safe_component(value: str) -> str:
    component = SAFE_PATH_COMPONENT.sub("_", value).strip("._")
    return component or "unnamed"


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


def _process_mismatch(actual: ProcessResult, expected: ExpectedProcess, phase: str) -> Optional[str]:
    if actual.timed_out:
        return phase + " timed out"
    if actual.exit_code != expected.exit_code:
        return phase + " exit code: expected " + str(expected.exit_code) + ", got " + str(actual.exit_code)
    if actual.stdout != expected.stdout:
        return phase + " stdout did not match expectation"
    if actual.stderr != expected.stderr:
        return phase + " stderr did not match expectation"
    return None


def _write_process_artifacts(artifact_dir: Path, phase: str, result: ProcessResult) -> None:
    _write_json(artifact_dir / (phase + ".command.json"), list(result.command))
    _write_json(
        artifact_dir / (phase + ".result.json"),
        {
            "exit_code": result.exit_code,
            "timed_out": result.timed_out,
        },
    )
    _write_text(artifact_dir / (phase + ".stdout"), result.stdout)
    _write_text(artifact_dir / (phase + ".stderr"), result.stderr)


def _resolve_executable(output_path: Path) -> Path:
    if output_path.is_file():
        return output_path
    windows_path = output_path.with_suffix(output_path.suffix + ".exe")
    if windows_path.is_file():
        return windows_path
    return output_path


def run_variant(hsc: Path, variant: TestVariant, artifact_root: Path) -> VariantResult:
    case = variant.case
    artifact_dir = (
        artifact_root
        / _safe_component(case.suite)
        / _safe_component(case.name)
        / _safe_component(variant.target)
        / _safe_component(variant.safety_mode)
        / _safe_component(variant.optimization_level)
    )
    if artifact_dir.exists():
        shutil.rmtree(artifact_dir)
    artifact_dir.mkdir(parents=True, exist_ok=True)

    shutil.copy2(case.source, artifact_dir / ("source" + case.source.suffix))
    _write_json(
        artifact_dir / "case.json",
        {
            "name": case.name,
            "manifest": str(case.manifest_path),
            "source": str(case.source),
            "target": variant.target,
            "safety": variant.safety_mode,
            "optimization": variant.optimization_level,
            "timeout_seconds": case.timeout_seconds,
            "expect": {
                "compile": asdict(case.compile_expect),
                "run": asdict(case.run_expect) if case.run_expect else None,
            },
        },
    )

    executable = artifact_dir / "program"
    compile_command = [
        str(hsc),
        SAFETY_FLAGS[variant.safety_mode],
        OPTIMIZATION_FLAGS[variant.optimization_level],
        *case.compile_args,
        str(case.source),
        "-o",
        str(executable),
    ]
    compile_result = _run_process(compile_command, artifact_dir, case.timeout_seconds)
    _write_process_artifacts(artifact_dir, "compile", compile_result)
    compile_failure = _process_mismatch(compile_result, case.compile_expect, "compile")
    if compile_failure:
        return VariantResult(
            variant,
            False,
            compile_failure,
            artifact_dir,
            compile_result,
            None,
        )
    if case.run_expect is None:
        return VariantResult(variant, True, None, artifact_dir, compile_result, None)

    run_command = [str(_resolve_executable(executable)), *case.run_args]
    run_result = _run_process(run_command, artifact_dir, case.timeout_seconds)
    _write_process_artifacts(artifact_dir, "run", run_result)
    run_failure = _process_mismatch(run_result, case.run_expect, "run")
    return VariantResult(
        variant,
        run_failure is None,
        run_failure,
        artifact_dir,
        compile_result,
        run_result,
    )


def _differential_group_key(variant: TestVariant) -> tuple[str, str, str, str]:
    return (
        variant.case.suite,
        variant.case.name,
        variant.target,
        variant.safety_mode,
    )


def _validate_differential_variants(variants: Sequence[TestVariant]) -> None:
    groups: dict[tuple[str, str, str, str], list[TestVariant]] = {}
    for variant in variants:
        groups.setdefault(_differential_group_key(variant), []).append(variant)

    for group_key, group in groups.items():
        levels = {variant.optimization_level for variant in group}
        group_label = "/".join(group_key)
        if "O0" not in levels:
            raise HarnessError("differential requires O0 in " + group_label)
        if len(levels) < 2:
            raise HarnessError("differential requires at least two optimization levels in " + group_label)
        for variant in group:
            case = variant.case
            if case.compile_expect.exit_code != 0 or case.run_expect is None:
                raise HarnessError(
                    "differential requires a successful runtime case: " + group_label
                )


def _differential_mismatches(reference: ProcessResult, candidate: ProcessResult) -> Sequence[str]:
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
    combined_failure = failure if result.failure is None else result.failure + "; " + failure
    return VariantResult(
        result.variant,
        False,
        combined_failure,
        result.artifact_dir,
        result.compile_result,
        result.run_result,
    )


def _process_summary(result: Optional[ProcessResult]) -> Optional[Mapping[str, Any]]:
    if result is None:
        return None
    return {
        "exit_code": result.exit_code,
        "timed_out": result.timed_out,
    }


def apply_differential_checks(results: Sequence[VariantResult]) -> Sequence[VariantResult]:
    indexed_results = list(results)
    groups: dict[tuple[str, str, str, str], list[int]] = {}
    for index, result in enumerate(indexed_results):
        groups.setdefault(_differential_group_key(result.variant), []).append(index)

    for group_indices in groups.values():
        reference_index = next(
            index
            for index in group_indices
            if indexed_results[index].variant.optimization_level == "O0"
        )
        reference = indexed_results[reference_index]
        variants = []
        for index in group_indices:
            result = indexed_results[index]
            mismatches: Sequence[str] = []
            if index != reference_index:
                if reference.run_result is None or result.run_result is None:
                    mismatches = ["run result is unavailable for comparison"]
                else:
                    mismatches = _differential_mismatches(reference.run_result, result.run_result)
                if mismatches:
                    prefix = "differential O0 vs " + result.variant.optimization_level + " "
                    indexed_results[index] = _with_failure(
                        result,
                        "; ".join(prefix + mismatch for mismatch in mismatches),
                    )
                    result = indexed_results[index]
            variants.append(
                {
                    "optimization": result.variant.optimization_level,
                    "artifact_dir": str(result.artifact_dir),
                    "compile": _process_summary(result.compile_result),
                    "run": _process_summary(result.run_result),
                    "matches_o0": not mismatches,
                    "mismatches": list(mismatches),
                }
            )

        report_path = reference.artifact_dir.parent / "differential.json"
        _write_json(
            report_path,
            {
                "version": 1,
                "reference_optimization": "O0",
                "variants": variants,
            },
        )
    return indexed_results


def selected_variants(cases: Iterable[TestCase], targets: Sequence[str], modes: Sequence[str], levels: Sequence[str]) -> Sequence[TestVariant]:
    target_filter = set(targets or ["host"])
    mode_filter = set(modes)
    level_filter = set(levels)
    unsupported_targets = target_filter - {"host"}
    if unsupported_targets:
        raise HarnessError(
            "T1 supports only --target host; cross-target execution is scheduled for T8"
        )
    unknown_modes = mode_filter - set(SAFETY_FLAGS)
    if unknown_modes:
        raise HarnessError("unsupported --mode value: " + sorted(unknown_modes)[0])
    unknown_levels = level_filter - set(OPTIMIZATION_FLAGS)
    if unknown_levels:
        raise HarnessError("unsupported --optimization value: " + sorted(unknown_levels)[0])

    variants = []
    for case in cases:
        for target, mode, level in itertools.product(case.targets, case.safety_modes, case.optimization_levels):
            if target not in target_filter:
                continue
            if mode_filter and mode not in mode_filter:
                continue
            if level_filter and level not in level_filter:
                continue
            variants.append(TestVariant(case, target, mode, level))
    return variants


def parse_arguments(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hsc", required=True, type=Path, help="path to the hsc executable")
    parser.add_argument("--suite", action="append", default=[], help="manifest suite name, or all")
    parser.add_argument("--manifest", action="append", type=Path, default=[], help="explicit JSON manifest path")
    parser.add_argument("--target", action="append", default=[], help="target selector; T1 supports host")
    parser.add_argument("--mode", action="append", default=[], help="safety-mode selector")
    parser.add_argument("--optimization", action="append", default=[], help="optimization-level selector")
    parser.add_argument("--timeout", type=float, help="override per-case timeout in seconds")
    parser.add_argument("--artifacts", type=Path, help="directory for preserved test artifacts")
    parser.add_argument(
        "--differential",
        action="store_true",
        help="compare each O0 runtime result with the other selected optimization levels",
    )
    parser.add_argument("--list", action="store_true", help="list selected matrix entries without running them")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_arguments(argv)
    hsc = args.hsc.resolve()
    if not hsc.is_file():
        raise HarnessError("--hsc does not exist: " + str(hsc))
    if os.name != "nt" and not os.access(str(hsc), os.X_OK):
        raise HarnessError("--hsc is not executable: " + str(hsc))
    if args.timeout is not None and args.timeout <= 0:
        raise HarnessError("--timeout must be positive")

    script_dir = Path(__file__).resolve().parent
    manifests = discover_manifests(script_dir / "manifests", args.suite, args.manifest)
    cases = []
    for manifest in manifests:
        cases.extend(load_manifest(manifest))
    if args.timeout is not None:
        cases = [
            TestCase(
                name=case.name,
                suite=case.suite,
                source=case.source,
                targets=case.targets,
                safety_modes=case.safety_modes,
                optimization_levels=case.optimization_levels,
                timeout_seconds=args.timeout,
                compile_expect=case.compile_expect,
                run_expect=case.run_expect,
                compile_args=case.compile_args,
                run_args=case.run_args,
                manifest_path=case.manifest_path,
            )
            for case in cases
        ]
    variants = selected_variants(cases, args.target, args.mode, args.optimization)
    if not variants:
        raise HarnessError("selection did not match any test variants")
    if args.differential:
        _validate_differential_variants(variants)

    for variant in variants:
        print(
            variant.case.suite
            + "/"
            + variant.case.name
            + " ["
            + variant.target
            + ", "
            + variant.safety_mode
            + ", "
            + variant.optimization_level
            + "]"
        )
    if args.list:
        return 0

    default_artifacts = Path.cwd() / "build" / "test-artifacts" / "harness"
    artifact_root = (args.artifacts or default_artifacts).resolve()
    artifact_root.mkdir(parents=True, exist_ok=True)
    results = [run_variant(hsc, variant, artifact_root) for variant in variants]
    if args.differential:
        results = apply_differential_checks(results)
    for result in results:
        status = "PASS" if result.passed else "FAIL"
        label = (
            result.variant.case.suite
            + "/"
            + result.variant.case.name
            + " ["
            + result.variant.target
            + ", "
            + result.variant.safety_mode
            + ", "
            + result.variant.optimization_level
            + "]"
        )
        if result.failure:
            print("[" + status + "] " + label + ": " + result.failure)
        else:
            print("[" + status + "] " + label)
    passed = sum(1 for result in results if result.passed)
    print(str(passed) + "/" + str(len(results)) + " PASS")
    print("Artifacts: " + str(artifact_root))
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except HarnessError as error:
        print("harness: error: " + str(error), file=sys.stderr)
        raise SystemExit(2)
