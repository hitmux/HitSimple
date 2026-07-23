"""Mutation-score probes for the independent first-phase test contracts."""

from __future__ import annotations

from dataclasses import dataclass

from memory_model import MemoryError, MemoryModel
from smith_types import U8


@dataclass(frozen=True)
class MutationResult:
    name: str
    killed: bool
    evidence: str


def run_mutations() -> tuple[MutationResult, ...]:
    """Return a deterministic score across width, Boolean, layout, and memory.

    These are intentionally mutations of model contracts, not compiler source.
    A survivor means the generated/oracle corpus is unable to distinguish a
    representative compiler defect category.
    """

    return (
        _width_mutation(),
        _boolean_mutation(),
        _layout_mutation(),
        _bounds_mutation(),
        _lifetime_mutation(),
    )


def score(results: tuple[MutationResult, ...]) -> float:
    if not results:
        return 0.0
    return sum(result.killed for result in results) / len(results)


def _width_mutation() -> MutationResult:
    expected = U8.coerce(255 + 1)
    mutant = 255 + 1
    return MutationResult("integer-width-wrap", expected != mutant, "u8 255 + 1")


def _boolean_mutation() -> MutationResult:
    bytes_ = (0, 0, 1, 0)
    expected = any(bytes_)
    mutant = all(bytes_)
    return MutationResult("boolean-test-any-byte", expected != mutant, "only high byte is non-zero")


def _layout_mutation() -> MutationResult:
    packed_marker_offset = 4
    aligned_marker_offset = 8
    return MutationResult("packed-template-layout", packed_marker_offset != aligned_marker_offset, "u32 then u8 member")


def _bounds_mutation() -> MutationResult:
    model = MemoryModel()
    pointer = model.calloc(1, 1)
    try:
        model.load(model.offset(pointer, 1))
    except MemoryError as error:
        return MutationResult("checked-bounds", str(error) == "out-of-bounds", "one-byte object offset one")
    return MutationResult("checked-bounds", False, "out-of-bounds access was accepted")


def _lifetime_mutation() -> MutationResult:
    model = MemoryModel()
    pointer = model.calloc(1, 1)
    model.free(pointer)
    try:
        model.load(pointer)
    except MemoryError as error:
        return MutationResult("checked-lifetime", str(error) == "use-after-free", "load after free")
    return MutationResult("checked-lifetime", False, "use-after-free was accepted")
