#!/usr/bin/env python3
"""Load, validate, apply, and revert compiler mutation operators.

An operator describes a representative compiler defect as one or more anchored
source edits.  Anchors are verbatim source excerpts instead of line numbers so
that an operator fails loudly when the surrounding implementation moves; silent
drift would turn the Mutation Score into an unfounded claim.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


OPERATOR_SUFFIX = ".mutant"
CATEGORIES = ("core", "codegen")
EXPECTATIONS = ("killed", "equivalent")

_EDIT_MARKER = "--- edit:"
_ANCHOR_MARKER = "--- anchor ---"
_REPLACEMENT_MARKER = "--- replacement ---"


class OperatorError(Exception):
    """A malformed operator definition or an anchor that no longer matches."""


@dataclass(frozen=True)
class Edit:
    path: str
    anchor: str
    replacement: str
    occurrences: int


@dataclass(frozen=True)
class Operator:
    name: str
    category: str
    summary: str
    expected: str
    rationale: str
    edits: tuple[Edit, ...]

    @property
    def files(self) -> tuple[str, ...]:
        seen: list[str] = []
        for edit in self.edits:
            if edit.path not in seen:
                seen.append(edit.path)
        return tuple(seen)


def _parse_edit_header(line: str, source: Path) -> tuple[str, int]:
    body = line[len(_EDIT_MARKER):].strip()
    if not body.endswith("---"):
        raise OperatorError(str(source) + ": edit header must end with '---'")
    body = body[:-3].strip()
    occurrences = 1
    if "occurrences:" in body:
        body, _, count = body.partition("occurrences:")
        body = body.strip()
        try:
            occurrences = int(count.strip())
        except ValueError as error:
            raise OperatorError(str(source) + ": occurrences must be an integer") from error
        if occurrences < 1:
            raise OperatorError(str(source) + ": occurrences must be positive")
    if not body:
        raise OperatorError(str(source) + ": edit header must name a file")
    return body, occurrences


def _block(lines: Sequence[str]) -> str:
    if not lines:
        return ""
    return "\n".join(lines) + "\n"


def parse_operator(text: str, source: Path) -> Operator:
    headers: dict[str, str] = {}
    edits: list[Edit] = []
    path = ""
    occurrences = 1
    anchor: list[str] = []
    replacement: list[str] = []
    section = "headers"

    def flush() -> None:
        if section == "headers":
            return
        if not anchor:
            raise OperatorError(str(source) + ": edit for " + path + " has an empty anchor")
        edits.append(Edit(path, _block(anchor), _block(replacement), occurrences))

    for line in text.splitlines():
        if line.startswith(_EDIT_MARKER):
            flush()
            path, occurrences = _parse_edit_header(line, source)
            anchor = []
            replacement = []
            section = "pending"
            continue
        if line.strip() == _ANCHOR_MARKER:
            if section == "headers":
                raise OperatorError(str(source) + ": anchor without an edit header")
            section = "anchor"
            continue
        if line.strip() == _REPLACEMENT_MARKER:
            if section != "anchor":
                raise OperatorError(str(source) + ": replacement without an anchor")
            section = "replacement"
            continue
        if section == "headers":
            stripped = line.strip()
            if not stripped:
                continue
            if not stripped.startswith("#"):
                raise OperatorError(str(source) + ": unexpected text before the first edit")
            key, separator, value = stripped[1:].partition(":")
            if not separator:
                continue
            headers[key.strip()] = value.strip()
            continue
        if section == "anchor":
            anchor.append(line)
            continue
        if section == "replacement":
            replacement.append(line)
            continue
        if line.strip():
            raise OperatorError(str(source) + ": text between an edit header and its anchor")

    flush()

    name = headers.get("name", "")
    if not name:
        raise OperatorError(str(source) + ": missing 'name'")
    if name != source.stem:
        raise OperatorError(str(source) + ": name must match the file stem")
    category = headers.get("category", "")
    if category not in CATEGORIES:
        raise OperatorError(str(source) + ": category must be one of " + ", ".join(CATEGORIES))
    summary = headers.get("summary", "")
    if not summary:
        raise OperatorError(str(source) + ": missing 'summary'")
    expected = headers.get("expected", "killed")
    if expected not in EXPECTATIONS:
        raise OperatorError(str(source) + ": expected must be one of " + ", ".join(EXPECTATIONS))
    rationale = headers.get("rationale", "")
    if expected == "equivalent" and not rationale:
        raise OperatorError(str(source) + ": an equivalent operator must state a rationale")
    if not edits:
        raise OperatorError(str(source) + ": operator defines no edit")

    return Operator(name, category, summary, expected, rationale, tuple(edits))


def load_operators(directory: Path) -> tuple[Operator, ...]:
    if not directory.is_dir():
        raise OperatorError("operator directory does not exist: " + str(directory))
    operators = [
        parse_operator(path.read_text(encoding="utf-8"), path)
        for path in sorted(directory.glob("*" + OPERATOR_SUFFIX))
    ]
    if not operators:
        raise OperatorError("no operator definitions below " + str(directory))
    names = [operator.name for operator in operators]
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        raise OperatorError("duplicate operator names: " + ", ".join(duplicates))
    return tuple(operators)


def select_operators(
    operators: Iterable[Operator],
    names: Sequence[str] | None,
) -> tuple[Operator, ...]:
    available = tuple(operators)
    if not names:
        return available
    index = {operator.name: operator for operator in available}
    unknown = sorted(name for name in names if name not in index)
    if unknown:
        raise OperatorError("unknown operator: " + ", ".join(unknown))
    return tuple(index[name] for name in names)


def check_anchors(operator: Operator, root: Path) -> None:
    """Raise when an anchor no longer matches the tree below ``root``."""

    for edit in operator.edits:
        target = root / edit.path
        if not target.is_file():
            raise OperatorError(operator.name + ": missing file " + edit.path)
        content = target.read_text(encoding="utf-8")
        found = content.count(edit.anchor)
        if found != edit.occurrences:
            raise OperatorError(
                operator.name
                + ": anchor in "
                + edit.path
                + " matched "
                + str(found)
                + " times, expected "
                + str(edit.occurrences)
            )


def apply_operator(operator: Operator, root: Path) -> dict[str, str]:
    """Apply every edit and return the original content of each touched file.

    Files are replaced through a temporary file and ``Path.replace`` so that a
    hard-linked working tree never writes back into the original repository.
    """

    check_anchors(operator, root)
    originals: dict[str, str] = {}
    for path in operator.files:
        originals[path] = (root / path).read_text(encoding="utf-8")

    updated = dict(originals)
    for edit in operator.edits:
        updated[edit.path] = updated[edit.path].replace(edit.anchor, edit.replacement)

    for path, content in updated.items():
        _write(root / path, content)
    return originals


def revert_operator(originals: dict[str, str], root: Path) -> None:
    for path, content in originals.items():
        _write(root / path, content)


def _write(target: Path, content: str) -> None:
    temporary = target.with_name(target.name + ".mutation-tmp")
    temporary.write_text(content, encoding="utf-8")
    temporary.replace(target)
