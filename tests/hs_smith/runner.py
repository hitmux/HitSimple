#!/usr/bin/env python3
"""Run the first HS-Smith phase with A/B/E oracle classification.

The runner keeps every generated source and process result under ``--artifacts``.
Stable failures are deduplicated by semantic signature, retried three times,
and copied to a self-contained failure directory with a minimized source.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
import os
from pathlib import Path
import platform
import shutil
from typing import Callable, Iterable, Sequence

from emitter_c import emit as emit_c
from emitter_hs import emit as emit_hs
from execution import ProcessResult, run_process, write_json, write_process, write_text
from generator import FIRST_PHASE_FEATURES, SmithCase, generate_cases, generate_memory_cases
from interpreter import evaluate
from mutation import run_mutations, score
from oracle import Failure, FailureSignature, OracleLevel, classify_differential, coverage_report, deduplicate
from reducer import reduce_source
from sandbox import SandboxPlan, SandboxPolicy, detect


STANDARD_VERSION = "1.0.0-Beta.21"
GENERATOR_VERSION = "1"
OPTIMIZATIONS = ("O0", "O1", "O2", "O3", "Os")
SAFETY_MODES = ("unchecked", "static-checked", "checked")
DEFAULT_SEED = 20260723
DEFAULT_CASES = 8


@dataclass(frozen=True)
class CaseResult:
    name: str
    features: tuple[str, ...]
    failures: tuple[Failure, ...]

    @property
    def passed(self) -> bool:
        return not self.failures


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


def parse_arguments(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hsc", required=True, type=Path)
    parser.add_argument("--clang", required=True, type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--cases", type=_positive_int, default=DEFAULT_CASES)
    parser.add_argument("--timeout", type=_positive_float, default=5.0)
    parser.add_argument("--require-network-isolation", action="store_true")
    parser.add_argument("--list", action="store_true")
    return parser.parse_args(argv)


def _validate_executable(label: str, path: Path) -> str | None:
    if not path.is_file():
        return label + " does not exist: " + str(path)
    if os.name != "nt" and not os.access(str(path), os.X_OK):
        return label + " is not executable: " + str(path)
    return None


def _observable_failure(result: ProcessResult, expected_stdout: str) -> str | None:
    if result.timed_out:
        return "timeout"
    if result.output_limited:
        return "output-limit"
    if result.signal is not None:
        return "signal-" + str(result.signal)
    if result.exit_code != 0:
        return "exit-" + str(result.exit_code)
    if result.stdout != expected_stdout:
        return "stdout"
    if result.stderr:
        return "stderr"
    return None


def _failure(
    phase: str,
    kind: str,
    level: OracleLevel,
    features: Iterable[str],
    message: str,
    result_pair: tuple[str, str] | None = None,
) -> Failure:
    return Failure(
        FailureSignature(phase, kind, level, tuple(sorted(features)), result_pair=result_pair),
        message,
    )


def _run(
    command: Sequence[str],
    directory: Path,
    phase: str,
    policy: SandboxPolicy,
    sandbox: SandboxPlan,
) -> ProcessResult:
    result = run_process(command, directory, policy, sandbox)
    write_process(directory, phase, result)
    return result


def _emit_inspection_artifacts(
    hsc: Path,
    source: Path,
    root: Path,
    features: tuple[str, ...],
    policy: SandboxPolicy,
    sandbox: SandboxPlan,
) -> list[Failure]:
    failures: list[Failure] = []
    hir = _run([str(hsc), "--dump-hir", str(source)], root, "hir", policy, sandbox)
    if hir.exit_code != 0 or hir.timed_out or not hir.stdout:
        failures.append(_failure("hir", "hir-verifier", OracleLevel.E, features, "--dump-hir did not produce verified HIR"))
    else:
        write_text(root / "hir.txt", hir.stdout)

    before = root / "before-opt.ll"
    llvm = _run([str(hsc), "--emit-llvm", str(source), "-o", str(before)], root, "llvm", policy, sandbox)
    if llvm.exit_code != 0 or llvm.timed_out or not before.is_file() or not before.read_text(encoding="utf-8"):
        failures.append(_failure("llvm", "llvm-verifier", OracleLevel.E, features, "--emit-llvm did not produce LLVM IR"))
    _emit_external_optimization_snapshot(before, root, policy, sandbox)
    return failures


def _emit_external_optimization_snapshot(before: Path, root: Path, policy: SandboxPolicy, sandbox: SandboxPlan) -> None:
    optimizer = shutil.which("opt")
    if not optimizer or not before.is_file():
        write_json(root / "after-opt.status.json", {"available": False, "reason": "opt is unavailable or LLVM IR was not emitted"})
        return
    after = root / "after-opt.ll"
    result = _run(
        [optimizer, "-S", "--passes=default<O3>", "--verify-each", str(before), "-o", str(after)],
        root,
        "external-opt",
        policy,
        sandbox,
    )
    write_json(
        root / "after-opt.status.json",
        {
            "available": result.exit_code == 0 and after.is_file(),
            "kind": "external-comparison-only",
            "reason": "hsc only exposes pre-optimization LLVM IR through --emit-llvm",
        },
    )


def _run_c_reference(
    clang: Path,
    source: Path,
    expected: str,
    root: Path,
    features: tuple[str, ...],
    policy: SandboxPolicy,
    sandbox: SandboxPlan,
) -> list[Failure]:
    root.mkdir(parents=True, exist_ok=True)
    failures: list[Failure] = []
    program = root / "reference-program"
    compile_result = _run(
        [str(clang), "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O2", str(source), "-o", str(program)],
        root,
        "reference-compile",
        policy,
        sandbox,
    )
    if compile_result.exit_code != 0 or compile_result.timed_out:
        return [_failure("reference", "c-compile", OracleLevel.A, features, "C reference failed to compile")]
    run_result = _run([str(program)], root, "reference-run", policy, sandbox)
    mismatch = _observable_failure(run_result, expected)
    if mismatch:
        failures.append(_failure("reference", "c-oracle-" + mismatch, OracleLevel.A, features, "C reference differs from the Python interpreter"))
    return failures


def _run_hsc_matrix(
    hsc: Path,
    source: Path,
    expected: str,
    root: Path,
    features: tuple[str, ...],
    policy: SandboxPolicy,
    sandbox: SandboxPlan,
) -> tuple[list[Failure], list[dict[str, object]]]:
    failures: list[Failure] = []
    variants: list[dict[str, object]] = []
    o0_result: ProcessResult | None = None
    for mode in SAFETY_MODES:
        for optimization in OPTIMIZATIONS:
            variant_root = root / mode / optimization
            variant_root.mkdir(parents=True, exist_ok=True)
            program = variant_root / "program"
            compile_result = _run(
                [str(hsc), "--" + mode, "-" + optimization, str(source), "-o", str(program)],
                variant_root,
                "compile",
                policy,
                sandbox,
            )
            record: dict[str, object] = {
                "safety_mode": mode,
                "optimization": optimization,
                "compile_exit_code": compile_result.exit_code,
                "compile_timed_out": compile_result.timed_out,
                "run": None,
            }
            if compile_result.exit_code != 0 or compile_result.timed_out:
                failures.append(_failure("compile", "valid-program-rejected", OracleLevel.A, features, "HSC rejected an A-oracle valid program at --" + mode + " -" + optimization))
                variants.append(record)
                continue
            run_result = _run([str(program)], variant_root, "run", policy, sandbox)
            mismatch = _observable_failure(run_result, expected)
            record["run"] = {
                "exit_code": run_result.exit_code,
                "signal": run_result.signal,
                "timed_out": run_result.timed_out,
                "output_limited": run_result.output_limited,
                "stdout_sha256": sha256(run_result.stdout.encode("utf-8")).hexdigest(),
                "matches_oracle": mismatch is None,
                "classification": classify_differential(OracleLevel.A, mismatch is None, True),
            }
            if mismatch:
                failures.append(_failure("run", "oracle-" + mismatch, OracleLevel.A, features, "HSC observable behavior differs from the Python oracle at --" + mode + " -" + optimization, (expected, run_result.stdout)))
            if mode == "unchecked" and optimization == "O0":
                o0_result = run_result
            elif o0_result is not None and not _same_observable(o0_result, run_result):
                record["run"]["classification"] = classify_differential(OracleLevel.D, True, False)  # type: ignore[index]
                failures.append(_failure("differential", "optimization-or-mode-mismatch", OracleLevel.D, features, "safe program differs from unchecked -O0 at --" + mode + " -" + optimization))
            variants.append(record)
    return failures, variants


def _same_observable(left: ProcessResult, right: ProcessResult) -> bool:
    return (left.exit_code, left.signal, left.stdout, left.stderr, left.timed_out) == (right.exit_code, right.signal, right.stdout, right.stderr, right.timed_out)


def _run_integer_case(
    case: SmithCase,
    hsc: Path,
    clang: Path,
    root: Path,
    policy: SandboxPolicy,
    sandbox: SandboxPlan,
) -> CaseResult:
    expected = evaluate(case.program)
    source = emit_hs(case.program)
    c_source = emit_c(case.program)
    write_text(root / "original.hs", source)
    write_text(root / "reference.c", c_source)
    write_json(
        root / "case.json",
        {
            "name": case.name,
            "seed": case.seed,
            "template": case.template.name,
            "standard_version": STANDARD_VERSION,
            "generator_version": GENERATOR_VERSION,
            "oracle_level": OracleLevel.A.value,
            "features": list(case.feature_tags),
        },
    )
    write_json(root / "expected.json", {"stdout": expected, "oracle": "independent-python-interpreter"})
    failures = _emit_inspection_artifacts(hsc, root / "original.hs", root, case.feature_tags, policy, sandbox)
    failures.extend(_run_c_reference(clang, root / "reference.c", expected, root / "reference", case.feature_tags, policy, sandbox))
    matrix_failures, variants = _run_hsc_matrix(hsc, root / "original.hs", expected, root / "matrix", case.feature_tags, policy, sandbox)
    failures.extend(matrix_failures)
    write_json(root / "differential.json", {"oracle_level": "A", "reference": "python-interpreter-and-c11", "variants": variants})
    return CaseResult(case.name, case.feature_tags, tuple(failures))


def _run_memory_case(
    case_name: str,
    source: str,
    expected: str | None,
    hsc: Path,
    root: Path,
    features: tuple[str, ...],
    policy: SandboxPolicy,
    sandbox: SandboxPlan,
    provability: str,
    detectability: str,
    address_origin: str,
    invalid: bool,
) -> CaseResult:
    write_text(root / "original.hs", source)
    write_json(
        root / "case.json",
        {
            "name": case_name,
            "standard_version": STANDARD_VERSION,
            "generator_version": GENERATOR_VERSION,
            "oracle_level": OracleLevel.B.value if invalid else OracleLevel.A.value,
            "features": list(features),
            "checked": {"provability": provability, "detectability": detectability, "address_origin": address_origin},
        },
    )
    failures = _emit_inspection_artifacts(hsc, root / "original.hs", root, features, policy, sandbox)
    if not invalid:
        assert expected is not None
        write_json(root / "expected.json", {"stdout": expected, "oracle": "independent-memory-model"})
        matrix_failures, variants = _run_hsc_matrix(hsc, root / "original.hs", expected, root / "matrix", features, policy, sandbox)
        failures.extend(matrix_failures)
        write_json(root / "differential.json", {"oracle_level": "A", "variants": variants})
    else:
        rows: list[dict[str, object]] = []
        for mode in ("static-checked", "checked"):
            variant_root = root / mode
            variant_root.mkdir(parents=True, exist_ok=True)
            result = _run([str(hsc), "--" + mode, "-O0", str(root / "original.hs"), "-o", str(variant_root / "program")], variant_root, "compile", policy, sandbox)
            rows.append({"safety_mode": mode, "exit_code": result.exit_code, "timed_out": result.timed_out})
            if result.exit_code == 0 or result.timed_out:
                failures.append(_failure("checked", "invalid-program-accepted", OracleLevel.B, features, case_name + " was not rejected by --" + mode))
        write_json(root / "checked-matrix.json", rows)
    return CaseResult(case_name, features, tuple(failures))


def _reduction_fragments(source: str) -> tuple[str, ...]:
    return tuple(line + "\n" for line in source.splitlines() if "new noise_" in line)


def _preserves_run_failure(
    source: str,
    hsc: Path,
    expected: str,
    probe_root: Path,
    policy: SandboxPolicy,
    sandbox: SandboxPlan,
) -> bool:
    if probe_root.exists():
        shutil.rmtree(probe_root)
    probe_root.mkdir(parents=True)
    candidate = probe_root / "candidate.hs"
    write_text(candidate, source)
    program = probe_root / "program"
    compiled = run_process([str(hsc), "--unchecked", "-O0", str(candidate), "-o", str(program)], probe_root, policy, sandbox)
    if compiled.exit_code != 0 or compiled.timed_out:
        return False
    run = run_process([str(program)], probe_root, policy, sandbox)
    return _observable_failure(run, expected) is not None


def _save_stable_failure(
    result: CaseResult,
    case_root: Path,
    failures_root: Path,
    regression_root: Path,
    hsc: Path,
    expected: str | None,
    policy: SandboxPolicy,
    sandbox: SandboxPlan,
) -> None:
    if not result.failures:
        return
    first = result.failures[0]
    destination = failures_root / (first.signature.key + "-" + result.name)
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(case_root, destination)
    source = (destination / "original.hs").read_text(encoding="utf-8")
    minimized = source
    reduction: dict[str, object] = {"stable": False, "reason": "not a runtime-oracle failure"}
    if expected is not None and first.signature.phase in {"run", "differential"}:
        probe = destination / "reduction-probe"
        predicate: Callable[[str], bool] = lambda candidate: _preserves_run_failure(candidate, hsc, expected, probe, policy, sandbox)
        if all(predicate(source) for _ in range(3)):
            reduced = reduce_source(source, predicate, _reduction_fragments(source))
            minimized = reduced.source
            reduction = {"stable": True, "attempts": reduced.attempts, "accepted_transformations": reduced.accepted_transformations}
        shutil.rmtree(probe, ignore_errors=True)
    write_text(destination / "minimized.hs", minimized)
    write_json(destination / "failure-signature.json", first.signature.as_dict())
    write_json(destination / "reduction.json", reduction)
    if reduction.get("stable") and first.signature.oracle_level in {OracleLevel.A, OracleLevel.B, OracleLevel.E}:
        regression = regression_root / (first.signature.key + "-" + result.name)
        if regression.exists():
            shutil.rmtree(regression)
        regression.mkdir(parents=True)
        write_text(regression / "source.hs", minimized)
        write_json(
            regression / "manifest.json",
            {
                "status": "confirmed",
                "source": "stable-hs-smith-failure",
                "signature": first.signature.as_dict(),
                "replay": "hsc --unchecked -O0 source.hs -o program && ./program",
            },
        )


def _tool_version(command: Sequence[str], root: Path, policy: SandboxPolicy, sandbox: SandboxPlan) -> str:
    result = run_process(command, root, policy, sandbox)
    return (result.stdout or result.stderr).splitlines()[0] if (result.stdout or result.stderr) else "unavailable"


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_arguments(argv)
    hsc = args.hsc.resolve()
    clang = args.clang.resolve()
    for label, executable in (("--hsc", hsc), ("--clang", clang)):
        error = _validate_executable(label, executable)
        if error:
            print("hs-smith: error: " + error)
            return 2

    generated = generate_cases(args.seed, args.cases)
    safe_memory, invalid_memory = generate_memory_cases(args.seed)
    if args.list:
        for case in generated:
            print(case.name + " [A]")
        print(safe_memory.name + " [A]")
        for case in invalid_memory:
            print(case.name + " [B]")
        return 0

    artifact_root = args.artifacts.resolve()
    artifact_root.mkdir(parents=True, exist_ok=True)
    policy = SandboxPolicy(timeout_seconds=args.timeout)
    sandbox = detect(artifact_root)
    if args.require_network_isolation and not sandbox.network_isolated:
        print("hs-smith: error: network isolation is required but " + sandbox.reason)
        return 2
    write_json(
        artifact_root / "run-manifest.json",
        {
            "standard_version": STANDARD_VERSION,
            "generator_version": GENERATOR_VERSION,
            "seed": args.seed,
            "host": {"machine": platform.machine(), "system": platform.system()},
            "hsc_version": _tool_version((str(hsc), "--version"), artifact_root, policy, sandbox),
            "clang_version": _tool_version((str(clang), "--version"), artifact_root, policy, sandbox),
            "sandbox": {
                "enabled": sandbox.enabled,
                "network_isolated": sandbox.network_isolated,
                "reason": sandbox.reason,
                "existing_uid_processes": sandbox.existing_uid_processes,
            },
            "limits": {"timeout_seconds": policy.timeout_seconds, "memory_bytes": policy.memory_bytes, "process_limit": policy.process_limit, "output_limit_bytes": policy.output_limit_bytes},
        },
    )

    seed_root = artifact_root / ("seed-" + str(args.seed))
    if seed_root.exists():
        shutil.rmtree(seed_root)
    seed_root.mkdir(parents=True)
    results: list[CaseResult] = []
    expected_by_name: dict[str, str | None] = {}
    for case in generated:
        root = seed_root / case.name
        root.mkdir()
        result = _run_integer_case(case, hsc, clang, root, policy, sandbox)
        results.append(result)
        expected_by_name[result.name] = evaluate(case.program)
    safe_root = seed_root / safe_memory.name
    safe_root.mkdir()
    result = _run_memory_case(safe_memory.name, safe_memory.source, safe_memory.expected_stdout, hsc, safe_root, safe_memory.feature_tags, policy, sandbox, safe_memory.provability, safe_memory.detectability, safe_memory.address_origin, False)
    results.append(result)
    expected_by_name[result.name] = safe_memory.expected_stdout
    for memory_case in invalid_memory:
        root = seed_root / memory_case.name
        root.mkdir()
        result = _run_memory_case(memory_case.name, memory_case.source, None, hsc, root, memory_case.feature_tags, policy, sandbox, memory_case.provability, memory_case.detectability, memory_case.address_origin, True)
        results.append(result)
        expected_by_name[result.name] = None

    all_failures = [failure for result in results for failure in result.failures]
    grouped = deduplicate(all_failures)
    failures_root = artifact_root / "failures"
    failures_root.mkdir(exist_ok=True)
    regression_root = artifact_root / "regression-corpus"
    regression_root.mkdir(exist_ok=True)
    for result in results:
        if result.failures:
            _save_stable_failure(result, seed_root / result.name, failures_root, regression_root, hsc, expected_by_name[result.name], policy, sandbox)

    mutations = run_mutations()
    mutation_score = score(mutations)
    quality = {
        "feature_coverage": coverage_report(result.features for result in results),
        "required_first_phase_features": list(FIRST_PHASE_FEATURES),
        "mutation": {"score": mutation_score, "threshold": 0.8, "results": [{"name": item.name, "killed": item.killed, "evidence": item.evidence} for item in mutations]},
        "new_failure_signatures": len(grouped),
        "failure_instances": len(all_failures),
    }
    if mutation_score < 0.8:
        all_failures.append(_failure("mutation", "score-below-threshold", OracleLevel.E, FIRST_PHASE_FEATURES, "first-phase mutation score is below 0.80"))
    write_json(artifact_root / "quality-report.json", quality)
    write_json(artifact_root / "failure-index.json", {key: [failure.message for failure in values] for key, values in grouped.items()})

    for result in results:
        print(("[PASS] " if result.passed else "[FAIL] ") + result.name)
        for failure in result.failures:
            print("  " + failure.signature.key + " " + failure.message)
    print(str(sum(result.passed for result in results)) + "/" + str(len(results)) + " PASS")
    print("Mutation score: {:.0%}".format(mutation_score))
    print("Sandbox: " + sandbox.reason)
    print("Artifacts: " + str(artifact_root))
    return 0 if not all_failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
