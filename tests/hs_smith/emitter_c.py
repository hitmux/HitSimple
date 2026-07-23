"""Emit a UB-avoiding C11 cross-check for the modeled integer subset."""

from __future__ import annotations

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
    Expr,
    For,
    Function,
    If,
    Increment,
    Literal,
    MemberRead,
    MemberWrite,
    ObjectDeclare,
    Output,
    Program,
    Return,
    SliceLength,
    SliceCopy,
    Stmt,
    Variable,
    While,
)
from smith_types import IntegerTemplate


_C_TYPES = {
    "u8": "uint8_t",
    "i8": "int8_t",
    "u16": "uint16_t",
    "i16": "int16_t",
    "u32": "uint32_t",
    "i32": "int32_t",
    "u64": "uint64_t",
    "i64": "int64_t",
}


class _Emitter:
    def __init__(self, program: Program) -> None:
        self.program = program
        self.arrays: dict[str, int] = {}
        self.values: dict[str, IntegerTemplate] = {}

    def emit(self) -> str:
        lines = ["#include <stdint.h>", "#include <stdio.h>", "", "#pragma pack(push, 1)"]
        for template in self.program.templates:
            lines.append("struct " + template.name + " {")
            for member in template.members:
                lines.append("    " + _ctype(member.template) + " " + member.name + ";")
            lines.append("};")
        lines.extend(("#pragma pack(pop)", ""))
        for function in self.program.functions:
            lines.extend(self._function(function))
            lines.append("")
        lines.extend(self._main())
        lines.append("")
        return "\n".join(lines)

    def _function(self, function: Function) -> list[str]:
        parameters = ", ".join(_ctype(item.template) + " " + item.name for item in function.parameters) or "void"
        lines = ["static " + _ctype(function.return_template) + " " + function.name + "(" + parameters + ") {"]
        for statement in function.body:
            lines.extend(self._statement(statement, 1))
        lines.append("}")
        return lines

    def _main(self) -> list[str]:
        lines = ["int main(void) {"]
        for statement in self.program.main.body:
            lines.extend(self._statement(statement, 1, main=True))
        lines.append("}")
        return lines

    def _statement(self, statement: Stmt, depth: int, main: bool = False) -> list[str]:
        prefix = "    " * depth
        if isinstance(statement, Declare):
            qualifier = "static " if statement.storage == "static" else ""
            self.values[statement.name] = statement.template
            lines = [prefix + qualifier + _ctype(statement.template) + " " + statement.name + " = (" + _ctype(statement.template) + ")(" + self._expr(statement.value) + ");"]
            if statement.name.startswith("noise_"):
                lines.append(prefix + "(void)" + statement.name + ";")
            return lines
        if isinstance(statement, Assign):
            return [prefix + statement.name + " = (" + _ctype(self.values[statement.name]) + ")(" + self._expr(statement.value) + ");"]
        if isinstance(statement, Increment):
            return [prefix + statement.name + "++;"]
        if isinstance(statement, ArrayDeclare):
            self.arrays[statement.name] = statement.length
            return [prefix + "uint8_t " + statement.name + "[" + str(statement.length) + "] = {0};"]
        if isinstance(statement, ArrayWrite):
            return [prefix + statement.array + "[" + self._expr(statement.index) + "] = (uint8_t)(" + self._expr(statement.value) + ");"]
        if isinstance(statement, SliceCopy):
            length = self.arrays[statement.target]
            return [
                prefix
                + statement.target
                + "["
                + str(index)
                + "] = "
                + statement.source
                + "["
                + self._expr(statement.start)
                + " + "
                + str(index)
                + "];"
                for index in range(length)
            ]
        if isinstance(statement, ObjectDeclare):
            return [prefix + "struct " + statement.template_name + " " + statement.name + " = {0};"]
        if isinstance(statement, MemberWrite):
            return [prefix + statement.object_name + "." + statement.member + " = " + self._expr(statement.value) + ";"]
        if isinstance(statement, If):
            lines = [prefix + "if (" + self._expr(statement.condition) + ") {"]
            for child in statement.then_body:
                lines.extend(self._statement(child, depth + 1, main))
            lines.append(prefix + "} else {")
            for child in statement.else_body:
                lines.extend(self._statement(child, depth + 1, main))
            lines.append(prefix + "}")
            return lines
        if isinstance(statement, While):
            lines = [prefix + "while (" + self._expr(statement.condition) + ") {"]
            for child in statement.body:
                lines.extend(self._statement(child, depth + 1, main))
            lines.append(prefix + "}")
            return lines
        if isinstance(statement, For):
            init = self._for_component(statement.init)
            post = ", ".join(self._for_component(item) for item in statement.post)
            lines = [prefix + "for (" + init + "; " + self._expr(statement.condition) + "; " + post + ") {"]
            for child in statement.body:
                lines.extend(self._statement(child, depth + 1, main))
            lines.append(prefix + "}")
            return lines
        if isinstance(statement, Output):
            return [prefix + 'printf("%llu\\n", (unsigned long long)(' + self._expr(statement.value) + "));"]
        if isinstance(statement, Return):
            return [prefix + ("return " if main else "return ") + self._expr(statement.value) + ";"]
        raise ValueError("unsupported statement: " + type(statement).__name__)

    def _for_component(self, statement: Declare | Assign | Increment) -> str:
        if isinstance(statement, Declare):
            self.values[statement.name] = statement.template
            return _ctype(statement.template) + " " + statement.name + " = " + self._expr(statement.value)
        if isinstance(statement, Assign):
            return statement.name + " = " + self._expr(statement.value)
        if isinstance(statement, Increment):
            return statement.name + "++"
        raise ValueError("unsupported for component: " + type(statement).__name__)

    def _expr(self, expression: Expr) -> str:
        if isinstance(expression, Literal):
            return str(expression.value)
        if isinstance(expression, Variable):
            return expression.name
        if isinstance(expression, Binary):
            return "(" + self._expr(expression.left) + " " + expression.operator + " " + self._expr(expression.right) + ")"
        if isinstance(expression, Compare):
            return "(" + self._expr(expression.left) + " " + expression.operator + " " + self._expr(expression.right) + ")"
        if isinstance(expression, Convert):
            return "((" + _ctype(expression.template) + ")(" + self._expr(expression.operand) + "))"
        if isinstance(expression, Call):
            return expression.name + "(" + ", ".join(self._expr(item) for item in expression.arguments) + ")"
        if isinstance(expression, ArrayRead):
            return expression.array + "[" + self._expr(expression.index) + "]"
        if isinstance(expression, BooleanTest):
            length = self.arrays[expression.array]
            return "(" + " || ".join(expression.array + "[" + str(index) + "] != 0" for index in range(length)) + ")"
        if isinstance(expression, SliceLength):
            return "(" + self._expr(expression.end) + " - " + self._expr(expression.start) + ")"
        if isinstance(expression, MemberRead):
            return expression.object_name + "." + expression.member
        raise ValueError("unsupported expression: " + type(expression).__name__)


def _ctype(template: IntegerTemplate) -> str:
    return _C_TYPES[template.name]


def emit(program: Program) -> str:
    return _Emitter(program).emit()
