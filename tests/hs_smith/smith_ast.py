"""Small structured AST for the first HS-Smith semantic subset.

This model deliberately represents only features for which the Python
interpreter is authoritative.  It is independent from HitSimple's AST/HIR.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import TypeAlias

from smith_types import IntegerTemplate


@dataclass(frozen=True)
class Literal:
    value: int


@dataclass(frozen=True)
class Variable:
    name: str


@dataclass(frozen=True)
class Binary:
    left: "Expr"
    operator: str
    right: "Expr"


@dataclass(frozen=True)
class Compare:
    left: "Expr"
    operator: str
    right: "Expr"


@dataclass(frozen=True)
class Convert:
    template: IntegerTemplate
    operand: "Expr"


@dataclass(frozen=True)
class Call:
    name: str
    arguments: tuple["Expr", ...]


@dataclass(frozen=True)
class ArrayRead:
    array: str
    index: "Expr"


@dataclass(frozen=True)
class BooleanTest:
    array: str


@dataclass(frozen=True)
class SliceLength:
    array: str
    start: "Expr"
    end: "Expr"


@dataclass(frozen=True)
class MemberRead:
    object_name: str
    member: str


Expr: TypeAlias = (
    Literal
    | Variable
    | Binary
    | Compare
    | Convert
    | Call
    | ArrayRead
    | BooleanTest
    | SliceLength
    | MemberRead
)


@dataclass(frozen=True)
class Declare:
    name: str
    template: IntegerTemplate
    value: Expr
    storage: str = "new"


@dataclass(frozen=True)
class Assign:
    name: str
    value: Expr


@dataclass(frozen=True)
class Increment:
    name: str


@dataclass(frozen=True)
class If:
    condition: Expr
    then_body: tuple["Stmt", ...]
    else_body: tuple["Stmt", ...]


@dataclass(frozen=True)
class While:
    condition: Expr
    body: tuple["Stmt", ...]


@dataclass(frozen=True)
class For:
    init: Declare | Assign | Increment
    condition: Expr
    post: tuple[Assign | Increment, ...]
    body: tuple["Stmt", ...]


@dataclass(frozen=True)
class ArrayDeclare:
    name: str
    length: int


@dataclass(frozen=True)
class ArrayWrite:
    array: str
    index: Expr
    value: Expr


@dataclass(frozen=True)
class SliceCopy:
    target: str
    source: str
    start: Expr
    end: Expr


@dataclass(frozen=True)
class MemberWrite:
    object_name: str
    member: str
    value: Expr


@dataclass(frozen=True)
class ObjectDeclare:
    name: str
    template_name: str


@dataclass(frozen=True)
class Return:
    value: Expr


@dataclass(frozen=True)
class Output:
    value: Expr


Stmt: TypeAlias = (
    Declare
    | Assign
    | Increment
    | If
    | While
    | For
    | ArrayDeclare
    | ArrayWrite
    | SliceCopy
    | MemberWrite
    | ObjectDeclare
    | Return
    | Output
)


@dataclass(frozen=True)
class Parameter:
    name: str
    template: IntegerTemplate


@dataclass(frozen=True)
class Function:
    name: str
    parameters: tuple[Parameter, ...]
    return_template: IntegerTemplate
    body: tuple[Stmt, ...]


@dataclass(frozen=True)
class TemplateMember:
    name: str
    template: IntegerTemplate


@dataclass(frozen=True)
class Template:
    name: str
    members: tuple[TemplateMember, ...]


@dataclass(frozen=True)
class Program:
    templates: tuple[Template, ...]
    functions: tuple[Function, ...]
    main: Function
    removable_fragments: tuple[str, ...] = ()
