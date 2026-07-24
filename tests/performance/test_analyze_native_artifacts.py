#!/usr/bin/env python3
"""Unit tests for architecture-specific native artifact metrics."""

from __future__ import annotations

import unittest

from analyze_native_artifacts import classify_instruction


def metrics() -> dict[str, int]:
    return {
        "instruction_count": 0,
        "branch_instructions": 0,
        "call_instructions": 0,
        "stack_memory_operations": 0,
        "vector_instructions": 0,
    }


class NativeArtifactAnalysisTests(unittest.TestCase):
    def test_aarch64_conditional_branch_is_counted(self) -> None:
        result = metrics()
        classify_instruction("b.ne", "  8: b.ne 0x20", result, "aarch64")
        self.assertEqual(result["branch_instructions"], 1)
        self.assertEqual(result["vector_instructions"], 0)

    def test_non_vector_prologue_instructions_are_not_counted_as_vector(self) -> None:
        x86 = metrics()
        classify_instruction("pushq", "  0: pushq %rbp", x86, "x86_64")
        self.assertEqual(x86["vector_instructions"], 0)

        arm = metrics()
        classify_instruction("paciasp", "  0: paciasp", arm, "aarch64")
        self.assertEqual(arm["vector_instructions"], 0)

    def test_vector_register_and_zeroing_instructions_are_counted(self) -> None:
        arm = metrics()
        classify_instruction("fadd", "  0: fadd v0.4s, v1.4s, v2.4s", arm, "aarch64")
        self.assertEqual(arm["vector_instructions"], 1)

        x86 = metrics()
        classify_instruction("vzeroupper", "  0: vzeroupper", x86, "x86_64")
        self.assertEqual(x86["vector_instructions"], 1)


if __name__ == "__main__":
    unittest.main()
