"""Oracle grading, failure signatures, and quality-report calculations."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from enum import Enum
from hashlib import sha256
import json
from typing import Iterable


class OracleLevel(str, Enum):
    A = "A"
    B = "B"
    C = "C"
    D = "D"
    E = "E"


@dataclass(frozen=True)
class FailureSignature:
    phase: str
    kind: str
    oracle_level: OracleLevel
    feature_tags: tuple[str, ...]
    diagnostic: str | None = None
    runtime_error: str | None = None
    result_pair: tuple[str, str] | None = None

    @property
    def key(self) -> str:
        canonical = json.dumps(asdict(self), default=str, sort_keys=True, separators=(",", ":"))
        return sha256(canonical.encode("utf-8")).hexdigest()[:16]

    def as_dict(self) -> dict[str, object]:
        value = asdict(self)
        value["oracle_level"] = self.oracle_level.value
        value["key"] = self.key
        return value


@dataclass(frozen=True)
class Failure:
    signature: FailureSignature
    message: str


def classify_differential(oracle_level: OracleLevel, matches_oracle: bool, matches_o0: bool) -> str:
    if oracle_level in {OracleLevel.A, OracleLevel.B, OracleLevel.E} and not matches_oracle:
        return "confirmed_by_oracle"
    if not matches_o0:
        return "optimizer_suspect"
    if oracle_level == OracleLevel.D:
        return "environmental"
    return "matches_oracle"


def deduplicate(failures: Iterable[Failure]) -> dict[str, list[Failure]]:
    grouped: dict[str, list[Failure]] = {}
    for failure in failures:
        grouped.setdefault(failure.signature.key, []).append(failure)
    return grouped


def coverage_report(feature_sets: Iterable[Iterable[str]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for features in feature_sets:
        for feature in features:
            counts[feature] = counts.get(feature, 0) + 1
    return dict(sorted(counts.items()))
