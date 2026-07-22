#!/usr/bin/env python3
"""Run deterministic HitSimple property and metamorphic tests on the local host."""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from typing import Optional, Sequence

from property_cases import generate_cases
from property_runner import OPTIMIZATION_LEVELS, run_cases


DEFAULT_SEED = 20260722
DEFAULT_TRIALS = 3
DEFAULT_TIMEOUT_SECONDS = 10.0


def _seed(value: str) -> int:
    try:
        return int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError("seed must be an integer") from error


def parse_arguments(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hsc", required=True, type=Path, help="path to the hsc executable")
    parser.add_argument("--seed", type=_seed, default=DEFAULT_SEED, help="deterministic generator seed")
    parser.add_argument("--trials", type=int, default=DEFAULT_TRIALS, help="random trials in each property family")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_SECONDS, help="per compile/run timeout in seconds")
    parser.add_argument("--artifacts", type=Path, help="directory for generated sources and process artifacts")
    parser.add_argument("--list", action="store_true", help="list generated property cases without compiling them")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_arguments(argv)
    hsc = args.hsc.resolve()
    if not hsc.is_file():
        print("property tests: error: --hsc does not exist: " + str(hsc), file=sys.stderr)
        return 2
    if os.name != "nt" and not os.access(str(hsc), os.X_OK):
        print("property tests: error: --hsc is not executable: " + str(hsc), file=sys.stderr)
        return 2
    if args.trials <= 0:
        print("property tests: error: --trials must be positive", file=sys.stderr)
        return 2
    if args.timeout <= 0:
        print("property tests: error: --timeout must be positive", file=sys.stderr)
        return 2

    cases = generate_cases(args.seed, args.trials)
    for case in cases:
        print(case.name + " [" + ", ".join(case.families) + "]")
    if args.list:
        return 0

    artifact_root = (args.artifacts or Path.cwd() / "build" / "test-artifacts" / "properties").resolve()
    artifact_root.mkdir(parents=True, exist_ok=True)
    results = run_cases(hsc, cases, artifact_root, args.seed, args.timeout)
    for result in results:
        status = "PASS" if result.passed else "FAIL"
        label = result.case.name + " [" + result.optimization + "]"
        if result.failure:
            print("[" + status + "] " + label + ": " + result.failure)
        else:
            print("[" + status + "] " + label)
    passed = sum(1 for result in results if result.passed)
    print(str(passed) + "/" + str(len(results)) + " PASS")
    print("Seed: " + str(args.seed))
    print("Optimizations: " + ", ".join("-" + level for level in OPTIMIZATION_LEVELS))
    print("Artifacts: " + str(artifact_root))
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
