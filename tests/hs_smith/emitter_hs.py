"""Render the modeled first-phase AST as portable core HitSimple source."""

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


def emit(program: Program) -> str:
    lines = ["$include <stdio.hsh>", "$include <stdlib.hsh>", ""]
    for template in program.templates:
        lines.append("template " + template.name + " {")
        for member in template.members:
            lines.append(
                "    " + member.name + "[" + str(member.template.bits // 8) + "] as " + member.template.name
            )
        lines.extend(("}", ""))
    for function in program.functions:
        lines.extend(_function_lines(function))
        lines.append("")
    lines.extend(_function_lines(program.main))
    lines.append("")
    return "\n".join(lines)


def _function_lines(function: Function) -> list[str]:
    arguments = ", ".join(parameter.name + " as " + parameter.template.name for parameter in function.parameters)
    lines = ["func " + function.name + "(" + arguments + ") -> " + function.return_template.name + " {"]
    for statement in function.body:
        lines.extend(_statement_lines(statement, 1))
    lines.append("}")
    return lines


def _statement_lines(statement: Stmt, depth: int) -> list[str]:
    prefix = "    " * depth
    if isinstance(statement, Declare):
        return [prefix + statement.storage + " " + statement.name + " as " + statement.template.name + " = " + _expression(statement.value)]
    if isinstance(statement, Assign):
        return [prefix + statement.name + " = " + _expression(statement.value)]
    if isinstance(statement, Increment):
        return [prefix + statement.name + "++"]
    if isinstance(statement, ArrayDeclare):
        return [prefix + "new " + statement.name + "[" + str(statement.length) + "] as bytes"]
    if isinstance(statement, ArrayWrite):
        return [prefix + statement.array + "[" + _expression(statement.index) + "] = " + _expression(statement.value)]
    if isinstance(statement, SliceCopy):
        return [prefix + statement.target + " = " + statement.source + "[" + _expression(statement.start) + ":" + _expression(statement.end) + "]"]
    if isinstance(statement, ObjectDeclare):
        return [prefix + "new " + statement.name + " as " + statement.template_name]
    if isinstance(statement, MemberWrite):
        return [prefix + statement.object_name + "." + statement.member + " = " + _expression(statement.value)]
    if isinstance(statement, If):
        lines = [prefix + "if (" + _expression(statement.condition) + ") {"]
        for child in statement.then_body:
            lines.extend(_statement_lines(child, depth + 1))
        lines.append(prefix + "} else {")
        for child in statement.else_body:
            lines.extend(_statement_lines(child, depth + 1))
        lines.append(prefix + "}")
        return lines
    if isinstance(statement, While):
        lines = [prefix + "while (" + _expression(statement.condition) + ") {"]
        for child in statement.body:
            lines.extend(_statement_lines(child, depth + 1))
        lines.append(prefix + "}")
        return lines
    if isinstance(statement, For):
        init = _for_component(statement.init)
        post = ", ".join(_for_component(item) for item in statement.post)
        lines = [prefix + "for (" + init + "; " + _expression(statement.condition) + "; " + post + ") {"]
        for child in statement.body:
            lines.extend(_statement_lines(child, depth + 1))
        lines.append(prefix + "}")
        return lines
    if isinstance(statement, Output):
        return [prefix + 'printf("%d\\n", ' + _expression(statement.value) + ")"]
    if isinstance(statement, Return):
        return [prefix + "return " + _expression(statement.value)]
    raise ValueError("unsupported statement: " + type(statement).__name__)


def _for_component(statement: Declare | Assign | Increment) -> str:
    if isinstance(statement, Declare):
        return "new " + statement.name + " as " + statement.template.name + " = " + _expression(statement.value)
    if isinstance(statement, Assign):
        return statement.name + " = " + _expression(statement.value)
    if isinstance(statement, Increment):
        return statement.name + "++"
    raise ValueError("unsupported for component: " + type(statement).__name__)


def _expression(expression: Expr) -> str:
    if isinstance(expression, Literal):
        return str(expression.value)
    if isinstance(expression, Variable):
        return expression.name
    if isinstance(expression, Binary):
        return "(" + _expression(expression.left) + " " + expression.operator + " " + _expression(expression.right) + ")"
    if isinstance(expression, Compare):
        return "(" + _expression(expression.left) + " " + expression.operator + " " + _expression(expression.right) + ")"
    if isinstance(expression, Convert):
        if isinstance(expression.operand, ArrayRead):
            return "(" + _expression(expression.operand) + "?)"
        return "to_" + expression.template.name + "(" + _expression(expression.operand) + ")"
    if isinstance(expression, Call):
        return expression.name + "(" + ", ".join(_expression(item) for item in expression.arguments) + ")"
    if isinstance(expression, ArrayRead):
        return expression.array + "[" + _expression(expression.index) + "]"
    if isinstance(expression, BooleanTest):
        return expression.array
    if isinstance(expression, SliceLength):
        return "length(&" + expression.array + "[" + _expression(expression.start) + ":" + _expression(expression.end) + "])"
    if isinstance(expression, MemberRead):
        return expression.object_name + "." + expression.member
    raise ValueError("unsupported expression: " + type(expression).__name__)
