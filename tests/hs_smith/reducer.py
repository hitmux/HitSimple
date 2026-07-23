"""Statement-fragment and literal reducer for stable HS-Smith failures."""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Callable, Iterable, Sequence


@dataclass(frozen=True)
class ReductionResult:
    source: str
    attempts: int
    accepted_transformations: int


def _literal_candidates(source: str) -> Iterable[str]:
    for match in reversed(tuple(re.finditer(r"(?<![A-Za-z0-9_])([0-9]+)(?![A-Za-z0-9_])", source))):
        value = int(match.group(1))
        for replacement in ("0", "1"):
            if match.group(1) != replacement:
                yield source[: match.start(1)] + replacement + source[match.end(1) :]
        if value > 2:
            yield source[: match.start(1)] + str(value // 2) + source[match.end(1) :]


def reduce_source(
    source: str,
    predicate: Callable[[str], bool],
    removable_fragments: Sequence[str] = (),
    max_attempts: int = 48,
) -> ReductionResult:
    """Keep only transformations that preserve the original failure predicate."""

    if max_attempts <= 0:
        raise ValueError("max_attempts must be positive")
    if not predicate(source):
        raise ValueError("reducer predicate must accept the original source")
    current = source
    attempts = 0
    accepted = 0

    def try_candidates(candidates: Iterable[str]) -> bool:
        nonlocal current, attempts, accepted
        for candidate in candidates:
            if candidate == current:
                continue
            attempts += 1
            if predicate(candidate):
                current = candidate
                accepted += 1
                return True
            if attempts >= max_attempts:
                return False
        return False

    changed = True
    while changed and attempts < max_attempts:
        changed = try_candidates(
            current.replace(fragment, "", 1)
            for fragment in removable_fragments
            if fragment and fragment in current
        )
    changed = True
    while changed and attempts < max_attempts:
        changed = try_candidates(_literal_candidates(current))
    return ReductionResult(current, attempts, accepted)
