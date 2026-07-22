#!/usr/bin/env python3
"""Compile a benchmark module and preserve its LLVM loop-vectorization remarks."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Sequence


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hsc", required=True, type=Path)
    parser.add_argument("--clang", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--minimum-vectorized-loops", type=int, default=2)
    return parser.parse_args(argv)


def run(command: Sequence[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, text=True, capture_output=True, check=False)


def write_text(path: Path, value: str) -> None:
    path.write_text(value, encoding="utf-8")


def main(argv: Sequence[str]) -> int:
    args = parse_arguments(argv)
    if args.minimum_vectorized_loops < 1:
        print("--minimum-vectorized-loops must be at least 1", file=sys.stderr)
        return 2
    for label, path in (("hsc", args.hsc), ("clang", args.clang), ("source", args.source)):
        if not path.is_file():
            print(label + " does not exist: " + str(path), file=sys.stderr)
            return 2

    artifacts = args.artifacts.resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    llvm_ir = artifacts / "benchmark-memory.ll"
    object_file = artifacts / "benchmark-memory.o"
    hsc_command = [str(args.hsc.resolve()), "--emit-llvm", str(args.source.resolve()), "-o", str(llvm_ir)]
    hsc_result = run(hsc_command, artifacts)
    write_text(artifacts / "hsc-command.txt", " ".join(hsc_command) + "\n")
    write_text(artifacts / "hsc.stdout", hsc_result.stdout)
    write_text(artifacts / "hsc.stderr", hsc_result.stderr)
    if hsc_result.returncode != 0:
        print("hsc failed to emit benchmark LLVM IR", file=sys.stderr)
        return hsc_result.returncode or 1

    clang_command = [
        str(args.clang.resolve()),
        "-O2",
        "-Rpass=loop-vectorize",
        "-Rpass-missed=loop-vectorize",
        "-Rpass-analysis=loop-vectorize",
        "-c",
        str(llvm_ir),
        "-o",
        str(object_file),
    ]
    clang_result = run(clang_command, artifacts)
    write_text(artifacts / "clang-command.txt", " ".join(clang_command) + "\n")
    remarks = clang_result.stdout + clang_result.stderr
    write_text(artifacts / "vectorization.remarks", remarks)
    vectorized_loops = len(re.findall(r"vectorized loop", remarks, flags=re.IGNORECASE))
    unsafe_dependent_memory = "unsafe dependent memory operations" in remarks.lower()
    summary = {
        "version": 1,
        "hsc_command": hsc_command,
        "clang_command": clang_command,
        "clang_exit_code": clang_result.returncode,
        "vectorized_loop_count": vectorized_loops,
        "unsafe_dependent_memory_operations": unsafe_dependent_memory,
    }
    (artifacts / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    if clang_result.returncode != 0:
        print("clang failed while collecting vectorization remarks", file=sys.stderr)
        return clang_result.returncode or 1
    if vectorized_loops < args.minimum_vectorized_loops:
        print(
            "expected at least "
            + str(args.minimum_vectorized_loops)
            + " vectorized loops, observed "
            + str(vectorized_loops),
            file=sys.stderr,
        )
        return 1
    if unsafe_dependent_memory:
        print("vectorization remarks report unsafe dependent memory operations", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
