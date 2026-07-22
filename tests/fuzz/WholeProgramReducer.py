"""Structure-preserving source reduction for generated whole-program failures."""

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
    """Yield source variants with one integer literal reduced at a time."""

    for match in reversed(tuple(re.finditer(r"(?<![A-Za-z0-9_])([0-9]+)(?![A-Za-z0-9_])", source))):
        value = int(match.group(1))
        for replacement in ("0", "1"):
            if match.group(1) == replacement:
                continue
            yield source[: match.start(1)] + replacement + source[match.end(1) :]
        if value > 2:
            yield source[: match.start(1)] + str(value // 2) + source[match.end(1) :]


def reduce_source(
    source: str,
    predicate: Callable[[str], bool],
    removable_fragments: Sequence[str] = (),
    max_attempts: int = 48,
) -> ReductionResult:
    """Keep only transformations that reproduce the caller's failure signature.

    ``removable_fragments`` are statement or function subtrees emitted by the
    grammar-aware generator.  Removing an entire fragment cannot leave a
    partial branch or loop behind.  Literal shrinking is deliberately applied
    only after structural attempts, so a minimized case stays readable.
    """

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

    # Repeatedly remove complete generated statements/functions.  Each pass
    # starts from the current source so dependent declarations naturally stay
    # whenever the oracle still needs them.
    made_progress = True
    while made_progress and attempts < max_attempts:
        made_progress = try_candidates(
            current.replace(fragment, "", 1)
            for fragment in removable_fragments
            if fragment and fragment in current
        )

    made_progress = True
    while made_progress and attempts < max_attempts:
        made_progress = try_candidates(_literal_candidates(current))

    return ReductionResult(current, attempts, accepted)
