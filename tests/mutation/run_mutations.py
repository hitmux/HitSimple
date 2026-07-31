#!/usr/bin/env python3
"""Score the HitSimple test suite against representative compiler defects.

Each operator injects one defect into a private copy of the source tree, builds
that mutant compiler, and runs the registered CTest suite against it.  A mutant
that no test rejects is a survivor: the suite cannot distinguish that defect
class and the corresponding coverage gap has to be closed before the Mutation
Score may be quoted as evidence.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from typing import Sequence
from xml.etree import ElementTree

from mutation_operators import (
    Operator,
    OperatorError,
    apply_operator,
    check_anchors,
    load_operators,
    revert_operator,
    select_operators,
)


DEFAULT_CORE_THRESHOLD = 0.9
DEFAULT_CODEGEN_THRESHOLD = 0.8
DEFAULT_BUILD_TIMEOUT_SECONDS = 3600.0
DEFAULT_TEST_TIMEOUT_SECONDS = 1800.0

KILLED = "killed"
SURVIVED = "survived"
EQUIVALENT = "equivalent"
INVALID_BUILD = "invalid-build"
TIMEOUT = "timeout"

SCORED_CLASSIFICATIONS = (KILLED, SURVIVED)


@dataclass(frozen=True)
class TestOutcome:
    total: int
    failures: int
    failed: tuple[str, ...]
    timed_out: bool
    returncode: int


@dataclass
class MutantResult:
    name: str
    category: str
    summary: str
    classification: str
    reason: str = ""
    killed_by: tuple[str, ...] = ()
    duration_seconds: float = 0.0
    log: str = ""


@dataclass
class CategoryScore:
    killed: int = 0
    survived: int = 0
    equivalent: int = 0
    invalid_build: int = 0
    timeout: int = 0
    names: dict[str, list[str]] = field(default_factory=dict)

    @property
    def scored(self) -> int:
        return self.killed + self.survived

    @property
    def score(self) -> float | None:
        return self.killed / self.scored if self.scored else None


def _positive_float(value: str) -> float:
    parsed = float(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return parsed


def _ratio(value: str) -> float:
    parsed = float(value)
    if not 0.0 <= parsed <= 1.0:
        raise argparse.ArgumentTypeError("must be between 0 and 1")
    return parsed


def parse_arguments(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--work-dir", type=Path, help="scratch tree and build directory (default: <source-root>/build-mutation)")
    parser.add_argument("--operators", type=Path, default=Path(__file__).resolve().parent / "operators")
    parser.add_argument("--select", nargs="+", metavar="NAME", help="run only the named operators")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--cmake-arg", action="append", default=[], metavar="ARG", help="extra argument for the mutant configure step")
    parser.add_argument("--test-regex", help="restrict the killer suite to matching CTest names")
    parser.add_argument("--core-threshold", type=_ratio, default=DEFAULT_CORE_THRESHOLD)
    parser.add_argument("--codegen-threshold", type=_ratio, default=DEFAULT_CODEGEN_THRESHOLD)
    parser.add_argument("--build-timeout", type=_positive_float, default=DEFAULT_BUILD_TIMEOUT_SECONDS)
    parser.add_argument("--test-timeout", type=_positive_float, default=DEFAULT_TEST_TIMEOUT_SECONDS)
    parser.add_argument("--report", type=Path, help="write the JSON report to this path")
    parser.add_argument("--skip-baseline", action="store_true", help="reuse a previously verified baseline build")
    parser.add_argument("--allow-invalid-build", action="store_true", help="report a mutant that fails to build instead of failing the run")
    parser.add_argument("--list", action="store_true", help="list operators without building anything")
    return parser.parse_args(argv)


def tracked_files(source_root: Path) -> tuple[str, ...]:
    completed = subprocess.run(
        ["git", "-C", str(source_root), "ls-files", "--cached", "--others",
         "--exclude-standard", "-z"],
        capture_output=True,
        check=True,
        text=True,
    )
    return tuple(entry for entry in completed.stdout.split("\0") if entry)


def sync_tree(source_root: Path, tree_root: Path, files: Sequence[str]) -> int:
    if tree_root.resolve() == source_root.resolve():
        raise OperatorError("the mutation tree must not be the source tree")
    wanted = set(files)
    copied = 0
    for relative in files:
        source = source_root / relative
        target = tree_root / relative
        if not source.is_file():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        copied += 1
    for existing in sorted(tree_root.rglob("*"), reverse=True):
        relative = str(existing.relative_to(tree_root))
        if existing.is_file() and relative not in wanted:
            existing.unlink()
        elif existing.is_dir() and not any(existing.iterdir()):
            existing.rmdir()
    return copied


def reset_build_tree(build_root: Path) -> None:
    """Discard prior objects so a reverted source file cannot reuse a mutant."""

    if build_root.is_symlink():
        raise OperatorError("the mutation build root must not be a symlink: " + str(build_root))
    if build_root.is_file():
        raise OperatorError("the mutation build root must be a directory: " + str(build_root))
    if build_root.exists():
        shutil.rmtree(build_root)


def _run(
    command: Sequence[str], timeout: float, log: Path,
    environment: dict[str, str] | None = None,
) -> tuple[int, bool]:
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w", encoding="utf-8") as stream:
        try:
            completed = subprocess.run(command, stdout=stream,
                                       stderr=subprocess.STDOUT, timeout=timeout,
                                       check=False, env=environment)
        except subprocess.TimeoutExpired:
            stream.write("\ntimed out after " + str(timeout) + " seconds\n")
            return 124, True
    return completed.returncode, False


def configure(
    tree_root: Path,
    build_root: Path,
    extra: Sequence[str],
    timeout: float,
    log: Path,
) -> tuple[int, bool]:
    command = [
        "cmake",
        "-S",
        str(tree_root),
        "-B",
        str(build_root),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_TESTING=ON",
    ]
    if shutil.which("ccache"):
        command += ["-DCMAKE_C_COMPILER_LAUNCHER=ccache", "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"]
    command += list(extra)
    return _run(command, timeout, log)


def build(build_root: Path, jobs: int, timeout: float, log: Path) -> tuple[int, bool]:
    return _run(["cmake", "--build", str(build_root), "--parallel", str(jobs)], timeout, log)


def run_ctest(
    build_root: Path,
    quiet_test_script: Path,
    jobs: int,
    timeout: float,
    junit: Path,
    log: Path,
    test_regex: str | None,
    stop_on_failure: bool,
) -> TestOutcome:
    junit.parent.mkdir(parents=True, exist_ok=True)
    if junit.exists():
        junit.unlink()
    command = [
        "cmake",
        "-DBUILD_DIR=" + str(build_root),
        "-DPARALLEL=" + str(jobs),
        "-DJUNIT_OUTPUT_FILE=" + str(junit),
        "-DSTOP_ON_FAILURE=" + ("ON" if stop_on_failure else "OFF"),
    ]
    if test_regex:
        command.append("-DTEST_REGEX=" + test_regex)
    command += ["-P", str(quiet_test_script)]
    environment = dict(os.environ)
    # The anchor-drift assertion validates the unmodified source tree.  A
    # campaign intentionally changes exactly one such anchor in its private
    # tree, so that assertion is not a compiler-defect killer.
    environment["HSC_MUTATION_ACTIVE"] = "1"
    returncode, timed_out = _run(command, timeout, log, environment)
    total, failures, failed = read_junit(junit)
    return TestOutcome(total, failures, failed, timed_out, returncode)


def read_junit(junit: Path) -> tuple[int, int, tuple[str, ...]]:
    if not junit.is_file():
        return 0, 0, ()
    root = ElementTree.parse(junit).getroot()
    total = int(root.get("tests", "0"))
    failures = int(root.get("failures", "0"))
    failed = tuple(
        case.get("name", "")
        for case in root.iter("testcase")
        if case.get("status", "") == "fail"
    )
    return total, failures, failed


def classify(operator: Operator, outcome: TestOutcome) -> tuple[str, str]:
    if outcome.timed_out:
        return TIMEOUT, "the killer suite exceeded its time limit"
    if outcome.total == 0:
        return INVALID_BUILD, "the killer suite reported no test result"
    if outcome.failures > 0 or outcome.returncode != 0:
        return KILLED, ""
    if operator.expected == EQUIVALENT:
        return EQUIVALENT, operator.rationale
    return SURVIVED, "no registered test distinguishes this defect"


def score_by_category(results: Sequence[MutantResult]) -> dict[str, CategoryScore]:
    scores: dict[str, CategoryScore] = {}
    for result in results:
        score = scores.setdefault(result.category, CategoryScore())
        score.names.setdefault(result.classification, []).append(result.name)
        if result.classification == KILLED:
            score.killed += 1
        elif result.classification == SURVIVED:
            score.survived += 1
        elif result.classification == EQUIVALENT:
            score.equivalent += 1
        elif result.classification == INVALID_BUILD:
            score.invalid_build += 1
        elif result.classification == TIMEOUT:
            score.timeout += 1
    return scores


def thresholds_met(
    scores: dict[str, CategoryScore],
    core_threshold: float,
    codegen_threshold: float,
) -> tuple[bool, tuple[str, ...]]:
    limits = {"core": core_threshold, "codegen": codegen_threshold}
    violations: list[str] = []
    for category, score in sorted(scores.items()):
        limit = limits.get(category)
        value = score.score
        if limit is None or value is None:
            continue
        if value + 1e-9 < limit:
            violations.append(
                category + " mutation score " + _percentage(value) + " is below the " + _percentage(limit) + " gate"
            )
    return not violations, tuple(violations)


def _percentage(value: float) -> str:
    return str(round(value * 100)) + "%"


def _commit(source_root: Path) -> str:
    completed = subprocess.run(
        ["git", "-C", str(source_root), "rev-parse", "HEAD"],
        capture_output=True,
        check=False,
        text=True,
    )
    return completed.stdout.strip() if completed.returncode == 0 else "unavailable"


def run_operator(
    operator: Operator,
    tree_root: Path,
    build_root: Path,
    artifacts: Path,
    arguments: argparse.Namespace,
) -> MutantResult:
    root = artifacts / operator.name
    root.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    originals = apply_operator(operator, tree_root)
    try:
        for path, content in originals.items():
            mutated = (tree_root / path).read_text(encoding="utf-8")
            if mutated == content:
                raise OperatorError(operator.name + ": edit did not change " + path)
        returncode, timed_out = build(build_root, arguments.jobs, arguments.build_timeout, root / "build.log")
        if timed_out:
            return MutantResult(operator.name, operator.category, operator.summary, TIMEOUT, "the mutant build exceeded its time limit", (), time.monotonic() - started, str(root / "build.log"))
        if returncode != 0:
            return MutantResult(operator.name, operator.category, operator.summary, INVALID_BUILD, "the mutant compiler does not build", (), time.monotonic() - started, str(root / "build.log"))
        outcome = run_ctest(
            build_root,
            tree_root / "cmake" / "RunQuietTests.cmake",
            arguments.jobs,
            arguments.test_timeout,
            root / "ctest.xml",
            root / "ctest.log",
            arguments.test_regex,
            stop_on_failure=True,
        )
        classification, reason = classify(operator, outcome)
        return MutantResult(
            operator.name,
            operator.category,
            operator.summary,
            classification,
            reason,
            outcome.failed,
            time.monotonic() - started,
            str(root / "ctest.log"),
        )
    finally:
        revert_operator(originals, tree_root)


def main(argv: Sequence[str] | None = None) -> int:
    # A campaign runs for minutes per mutant; keep CI logs current.
    sys.stdout.reconfigure(line_buffering=True)
    arguments = parse_arguments(argv)
    source_root = arguments.source_root.resolve()
    try:
        operators = select_operators(load_operators(arguments.operators), arguments.select)
    except OperatorError as error:
        print("[FAIL] " + str(error))
        return 1

    if arguments.list:
        for operator in operators:
            print(operator.category + "  " + operator.name + "  " + operator.summary)
        print(str(len(operators)) + " operators")
        return 0

    work_dir = (arguments.work_dir or source_root / "build-mutation").resolve()
    if work_dir == source_root:
        print("[FAIL] the mutation work directory must not be the source tree")
        return 1
    tree_root = work_dir / "tree"
    build_root = work_dir / "build"
    artifacts = work_dir / "mutants"
    tree_root.mkdir(parents=True, exist_ok=True)
    artifacts.mkdir(parents=True, exist_ok=True)

    try:
        files = tracked_files(source_root)
        sync_tree(source_root, tree_root, files)
        for operator in operators:
            check_anchors(operator, tree_root)
        reset_build_tree(build_root)
    except (OperatorError, subprocess.CalledProcessError) as error:
        print("[FAIL] " + str(error))
        return 1

    configure_returncode, configure_timed_out = configure(
        tree_root,
        build_root,
        arguments.cmake_arg,
        arguments.build_timeout,
        work_dir / "configure.log",
    )
    if configure_returncode != 0 or configure_timed_out:
        print("[FAIL] the mutation build could not be configured, see " + str(work_dir / "configure.log"))
        return 2

    baseline_tests = 0
    if not arguments.skip_baseline:
        returncode, timed_out = build(build_root, arguments.jobs, arguments.build_timeout, work_dir / "baseline-build.log")
        if returncode != 0 or timed_out:
            print("[FAIL] the unmutated compiler does not build, see " + str(work_dir / "baseline-build.log"))
            return 2
        baseline = run_ctest(
            build_root,
            tree_root / "cmake" / "RunQuietTests.cmake",
            arguments.jobs,
            arguments.test_timeout,
            work_dir / "baseline.xml",
            work_dir / "baseline-ctest.log",
            arguments.test_regex,
            stop_on_failure=False,
        )
        if baseline.failures != 0 or baseline.returncode != 0 or baseline.total == 0:
            print("[FAIL] the unmutated compiler does not pass the killer suite, see " + str(work_dir / "baseline-ctest.log"))
            return 2
        baseline_tests = baseline.total
        print("Baseline: " + str(baseline.total) + " tests pass without a mutation")

    results: list[MutantResult] = []
    for index, operator in enumerate(operators, start=1):
        prefix = "[" + str(index) + "/" + str(len(operators)) + "] "
        try:
            result = run_operator(operator, tree_root, build_root, artifacts, arguments)
        except OperatorError as error:
            print("[FAIL] " + str(error))
            return 1
        results.append(result)
        detail = ""
        if result.classification == KILLED and result.killed_by:
            detail = " by " + result.killed_by[0]
        elif result.reason:
            detail = " (" + result.reason + ")"
        print(prefix + result.classification.upper() + " " + result.name + detail)

    scores = score_by_category(results)
    met, violations = thresholds_met(scores, arguments.core_threshold, arguments.codegen_threshold)
    invalid = [result.name for result in results if result.classification == INVALID_BUILD]

    report = {
        "schema": 1,
        "source_commit": _commit(source_root),
        "operator_count": len(operators),
        "baseline_tests": baseline_tests,
        "thresholds": {"core": arguments.core_threshold, "codegen": arguments.codegen_threshold},
        "categories": {
            category: {
                "killed": score.killed,
                "survived": score.survived,
                "equivalent": score.equivalent,
                "invalid_build": score.invalid_build,
                "timeout": score.timeout,
                "score": score.score,
                "names": score.names,
            }
            for category, score in sorted(scores.items())
        },
        "results": [
            {
                "name": result.name,
                "category": result.category,
                "summary": result.summary,
                "classification": result.classification,
                "reason": result.reason,
                "killed_by": list(result.killed_by),
                "duration_seconds": round(result.duration_seconds, 3),
                "log": result.log,
            }
            for result in results
        ],
    }
    report_path = arguments.report or work_dir / "mutation-report.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    for category, score in sorted(scores.items()):
        value = "n/a" if score.score is None else _percentage(score.score)
        print(
            category
            + ": "
            + value
            + " ("
            + str(score.killed)
            + "/"
            + str(score.scored)
            + " killed, "
            + str(score.equivalent)
            + " equivalent, "
            + str(score.invalid_build)
            + " invalid-build, "
            + str(score.timeout)
            + " timeout)"
        )
    for name in sorted(result.name for result in results if result.classification == SURVIVED):
        print("[SURVIVED] " + name)
    print("Report: " + str(report_path))

    if invalid and not arguments.allow_invalid_build:
        print("[FAIL] operators that do not build: " + ", ".join(sorted(invalid)))
        return 1
    if not met:
        for violation in violations:
            print("[FAIL] " + violation)
        return 1
    print("Mutation gate: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
