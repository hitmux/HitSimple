"""Fixed-width integer semantics used by the independent HS-Smith oracle."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class IntegerTemplate:
    name: str
    bits: int
    signed: bool

    @property
    def minimum(self) -> int:
        return -(1 << (self.bits - 1)) if self.signed else 0

    @property
    def maximum(self) -> int:
        return (1 << (self.bits - 1)) - 1 if self.signed else (1 << self.bits) - 1

    @property
    def mask(self) -> int:
        return (1 << self.bits) - 1

    def coerce(self, value: int) -> int:
        narrowed = value & self.mask
        if self.signed and narrowed >= (1 << (self.bits - 1)):
            return narrowed - (1 << self.bits)
        return narrowed


INTEGER_TEMPLATES: tuple[IntegerTemplate, ...] = (
    IntegerTemplate("u8", 8, False),
    IntegerTemplate("i8", 8, True),
    IntegerTemplate("u16", 16, False),
    IntegerTemplate("i16", 16, True),
    IntegerTemplate("u32", 32, False),
    IntegerTemplate("i32", 32, True),
    IntegerTemplate("u64", 64, False),
    IntegerTemplate("i64", 64, True),
)

TEMPLATES_BY_NAME = {template.name: template for template in INTEGER_TEMPLATES}
U8 = TEMPLATES_BY_NAME["u8"]
U32 = TEMPLATES_BY_NAME["u32"]
U64 = TEMPLATES_BY_NAME["u64"]


def require_template(name: str) -> IntegerTemplate:
    try:
        return TEMPLATES_BY_NAME[name]
    except KeyError as error:
        raise ValueError("unsupported integer template: " + name) from error
