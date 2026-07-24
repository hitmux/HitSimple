#!/usr/bin/env python3
"""Summarize native-object instruction quality from saved llvm-objdump output."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any, Sequence


INSTRUCTION = re.compile(r"^\s*[0-9A-Fa-f]+:\s+([A-Za-z][A-Za-z0-9_.]*)\b")
X86_STACK_MEMORY = re.compile(r"\(%r(?:sp|bp)\)")
ARM_STACK_MEMORY = re.compile(r"\[(?:sp|x29)(?:,|\])")
VECTOR_REGISTER = re.compile(r"%(?:xmm|ymm|zmm)\d+|\bv\d+\.\d")


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(argv)


def classify_instruction(
    mnemonic: str, line: str, metrics: dict[str, int], architecture: str
) -> None:
    metrics["instruction_count"] += 1
    normalized = mnemonic.lower()
    if architecture == "x86_64":
        is_branch = normalized.startswith("j")
        is_call = normalized.startswith("call")
        is_vector = bool(VECTOR_REGISTER.search(line)) or normalized in {
            "vzeroall",
            "vzeroupper",
        }
    elif architecture == "aarch64":
        is_branch = normalized == "b" or normalized.startswith("b.") or normalized in {
            "bl",
            "blr",
            "br",
            "cbz",
            "cbnz",
            "tbz",
            "tbnz",
        }
        is_call = normalized in {"bl", "blr"}
        is_vector = bool(VECTOR_REGISTER.search(line))
    else:
        raise ValueError("unsupported disassembly architecture: " + architecture)
    if is_branch:
        metrics["branch_instructions"] += 1
    if is_call:
        metrics["call_instructions"] += 1
    if X86_STACK_MEMORY.search(line) or ARM_STACK_MEMORY.search(line):
        metrics["stack_memory_operations"] += 1
    if is_vector:
        metrics["vector_instructions"] += 1


def disassembly_architecture(path: Path, contents: str) -> str:
    lowered = contents.lower()
    if "aarch64" in lowered:
        return "aarch64"
    if "x86-64" in lowered or "i386:x86-64" in lowered or "x86_64" in lowered:
        return "x86_64"
    raise ValueError("cannot identify disassembly architecture: " + str(path))


def analyze_disassembly(path: Path) -> dict[str, int]:
    if not path.is_file():
        raise ValueError("missing disassembly: " + str(path))
    contents = path.read_text(encoding="utf-8")
    architecture = disassembly_architecture(path, contents)
    metrics = {
        "instruction_count": 0,
        "branch_instructions": 0,
        "call_instructions": 0,
        "stack_memory_operations": 0,
        "vector_instructions": 0,
    }
    for line in contents.splitlines():
        match = INSTRUCTION.match(line)
        if match:
            classify_instruction(match.group(1), line, metrics, architecture)
    if metrics["instruction_count"] == 0:
        raise ValueError("no instructions found in " + str(path))
    return metrics


def percent_delta(current: int, reference: int) -> float | None:
    if reference == 0:
        return None
    return (current / reference - 1.0) * 100.0


def compare_direct_with_llc(
    direct: dict[str, int], reference: dict[str, int]
) -> dict[str, float | None]:
    return {
        name + "_percent": percent_delta(direct[name], reference[name])
        for name in direct
    }


def main(argv: Sequence[str]) -> int:
    args = parse_arguments(argv)
    artifacts = args.artifacts.resolve()
    if not artifacts.is_dir():
        print("artifact directory does not exist: " + str(artifacts), file=sys.stderr)
        return 2

    workloads: dict[str, Any] = {}
    try:
        for directory in sorted(artifacts.iterdir()):
            if not directory.is_dir():
                continue
            hsc = analyze_disassembly(directory / "hsc-O2.objdump")
            llc = analyze_disassembly(directory / "llc-O2.objdump")
            workloads[directory.name] = {
                "hsc_o2": hsc,
                "llc_o2": llc,
                "direct_vs_llc_percent": compare_direct_with_llc(hsc, llc),
            }
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 2
    if not workloads:
        print("no workload artifact directories found", file=sys.stderr)
        return 2

    output = {
        "version": 2,
        "artifacts": str(artifacts),
        "workloads": workloads,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("native artifact quality collected for " + str(len(workloads)) + " workloads")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
