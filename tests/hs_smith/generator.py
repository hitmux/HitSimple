"""Deterministic semantic-model generation for the first HS-Smith profile."""

from __future__ import annotations

from dataclasses import dataclass
from random import Random

from smith_ast import (
    ArrayDeclare,
    ArrayRead,
    ArrayWrite,
    Assign,
    Binary,
    BooleanTest,
    Call,
    Compare,
    Convert,
    Declare,
    For,
    Function,
    If,
    Increment,
    Literal,
    MemberRead,
    MemberWrite,
    ObjectDeclare,
    Output,
    Parameter,
    Program,
    Return,
    SliceLength,
    SliceCopy,
    Template,
    TemplateMember,
    Variable,
    While,
)
from memory_model import MemoryCase, invalid_memory_cases, safe_memory_case
from smith_types import INTEGER_TEMPLATES, U8, U32, U64, IntegerTemplate


@dataclass(frozen=True)
class SmithCase:
    name: str
    program: Program
    seed: int
    template: IntegerTemplate
    feature_tags: tuple[str, ...]
    removable_fragments: tuple[str, ...]


FIRST_PHASE_FEATURES: tuple[str, ...] = (
    "fixed-width-integer",
    "boolean-test",
    "new",
    "static",
    "if",
    "while",
    "for",
    "pure-function",
    "fixed-array",
    "index",
    "slice",
    "fixed-user-template",
)


def _bounded_value(random: Random, template: IntegerTemplate) -> int:
    # Keep the first semantic profile free of signed-overflow UB.  Width-wrap
    # behavior is exercised by the existing dedicated property suite.
    upper = min(template.maximum, 10)
    lower = max(template.minimum, 0)
    return random.randint(lower, upper)


def _program(random: Random, template: IntegerTemplate) -> Program:
    first = _bounded_value(random, template)
    second = _bounded_value(random, template)
    raw = (0, 0, random.randint(0, 7), random.randint(0, 7))
    if random.randrange(2):
        raw = (0, 0, 0, max(1, raw[3]))
    while_count = random.randint(1, 3)
    for_count = random.randint(1, 3)
    noise_values = (random.randint(0, 9), random.randint(0, 9))
    template_name = "SmithRecord"

    pure_add = Function(
        "smith_add",
        (Parameter("left", template), Parameter("right", template)),
        template,
        (Return(Binary(Variable("left"), "+", Variable("right"))),),
    )
    static_tick = Function(
        "smith_tick",
        (),
        U32,
        (
            Declare("counter", U32, Literal(0), storage="static"),
            Increment("counter"),
            Return(Variable("counter")),
        ),
    )
    main = Function(
        "main",
        (),
        U32,
        (
            Declare("noise_0", U8, Literal(noise_values[0])),
            Declare("noise_1", U8, Literal(noise_values[1])),
            Declare("first", template, Literal(first)),
            Declare("second", template, Literal(second)),
            Declare("total", template, Call("smith_add", (Variable("first"), Variable("second")))),
            Declare("tick", template, Convert(template, Call("smith_tick", ()))),
            Assign("total", Binary(Variable("total"), "+", Variable("tick"))),
            ArrayDeclare("raw", 4),
            ArrayWrite("raw", Literal(0), Literal(raw[0])),
            ArrayWrite("raw", Literal(1), Literal(raw[1])),
            ArrayWrite("raw", Literal(2), Literal(raw[2])),
            ArrayWrite("raw", Literal(3), Literal(raw[3])),
            If(
                BooleanTest("raw"),
                (Assign("total", Binary(Variable("total"), "+", Convert(template, ArrayRead("raw", Literal(3))))),),
                (Assign("total", Binary(Variable("total"), "+", Convert(template, ArrayRead("raw", Literal(0))))),),
            ),
            Declare("remaining", U8, Literal(while_count)),
            While(
                Compare(Variable("remaining"), ">", Literal(0)),
                (
                    Assign("total", Binary(Variable("total"), "+", Literal(1))),
                    Increment("remaining"),
                    Assign("remaining", Binary(Variable("remaining"), "-", Literal(2))),
                ),
            ),
            For(
                Declare("index", U8, Literal(0)),
                Compare(Variable("index"), "<", Literal(for_count)),
                (Increment("index"),),
                (Assign("total", Binary(Variable("total"), "+", Convert(template, Variable("index")))),),
            ),
            ObjectDeclare("record", template_name),
            MemberWrite("record", "primary", Variable("total")),
            MemberWrite("record", "marker", ArrayRead("raw", Literal(2))),
            Assign(
                "total",
                Binary(
                    MemberRead("record", "primary"),
                    "+",
                    Convert(template, MemberRead("record", "marker")),
                ),
            ),
            ArrayDeclare("slice", 2),
            SliceCopy("slice", "raw", Literal(1), Literal(3)),
            Assign("total", Binary(Variable("total"), "+", Literal(2))),
            Output(Convert(U64, Variable("total"))),
            Return(Literal(0)),
        ),
    )
    return Program(
        (Template(template_name, (TemplateMember("primary", template), TemplateMember("marker", U8))),),
        (pure_add, static_tick),
        main,
    )


def generate_cases(seed: int, count: int) -> list[SmithCase]:
    if count <= 0:
        raise ValueError("case count must be positive")
    random = Random(seed)
    cases: list[SmithCase] = []
    for index in range(count):
        template = INTEGER_TEMPLATES[index % len(INTEGER_TEMPLATES)]
        cases.append(
            SmithCase(
                "integer-" + template.name + "-" + str(index + 1).zfill(3),
                _program(random, template),
                seed,
                template,
                FIRST_PHASE_FEATURES,
                (
                    "    new noise_0 as u8 = ",
                    "    new noise_1 as u8 = ",
                ),
            )
        )
    return cases


def generate_memory_cases(seed: int) -> tuple[MemoryCase, tuple[MemoryCase, ...]]:
    random = Random(seed ^ 0x4853_534D)
    return safe_memory_case(random.randint(1, 99), random.randint(1, 99)), invalid_memory_cases()
