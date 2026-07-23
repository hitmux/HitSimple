"""Independent evaluator for the AST modeled by :mod:`ast`.

It intentionally contains no compiler queries, parsed HIR, or generated LLVM.
The only shared contract is the published Standard semantics selected by the
generator profile.
"""

from __future__ import annotations

from dataclasses import dataclass, field

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
from smith_types import IntegerTemplate, U8, U64


class InterpreterError(RuntimeError):
    pass


class _ReturnSignal(Exception):
    def __init__(self, value: int) -> None:
        self.value = value


@dataclass
class _Cell:
    template: IntegerTemplate
    value: int


@dataclass
class _Frame:
    values: dict[str, _Cell] = field(default_factory=dict)
    arrays: dict[str, list[int]] = field(default_factory=dict)
    objects: dict[str, dict[str, _Cell]] = field(default_factory=dict)


class Interpreter:
    """Runs one generated program and returns its observable stdout."""

    def __init__(self, program: Program) -> None:
        self.program = program
        self.functions = {function.name: function for function in program.functions}
        self.functions[program.main.name] = program.main
        self.templates = {template.name: template for template in program.templates}
        self.static_values: dict[str, _Cell] = {}
        self.output: list[str] = []
        self._loop_budget = 10_000

    def run(self) -> str:
        self._call(self.program.main.name, ())
        return "".join(self.output)

    def _call(self, name: str, arguments: tuple[int, ...]) -> int:
        try:
            function = self.functions[name]
        except KeyError as error:
            raise InterpreterError("unknown function: " + name) from error
        if len(arguments) != len(function.parameters):
            raise InterpreterError("wrong argument count for " + name)
        frame = _Frame(
            values={
                parameter.name: _Cell(parameter.template, parameter.template.coerce(value))
                for parameter, value in zip(function.parameters, arguments, strict=True)
            }
        )
        try:
            self._run_statements(function, frame, function.body)
        except _ReturnSignal as signal:
            return function.return_template.coerce(signal.value)
        return 0

    def _run_statements(self, function: Function, frame: _Frame, statements: tuple[Stmt, ...]) -> None:
        for statement in statements:
            self._run_statement(function, frame, statement)

    def _run_statement(self, function: Function, frame: _Frame, statement: Stmt) -> None:
        if isinstance(statement, Declare):
            value = statement.template.coerce(self._evaluate(frame, statement.value))
            if statement.storage == "static":
                key = function.name + "::" + statement.name
                frame.values[statement.name] = self.static_values.setdefault(
                    key, _Cell(statement.template, value)
                )
            else:
                frame.values[statement.name] = _Cell(statement.template, value)
            return
        if isinstance(statement, Assign):
            cell = self._lookup_value(function, frame, statement.name)
            cell.value = cell.template.coerce(self._evaluate(frame, statement.value))
            return
        if isinstance(statement, Increment):
            cell = self._lookup_value(function, frame, statement.name)
            cell.value = cell.template.coerce(cell.value + 1)
            return
        if isinstance(statement, ArrayDeclare):
            frame.arrays[statement.name] = [0] * statement.length
            return
        if isinstance(statement, ArrayWrite):
            array = self._array(frame, statement.array)
            index = self._evaluate(frame, statement.index)
            self._check_index(array, index)
            array[index] = U8.coerce(self._evaluate(frame, statement.value))
            return
        if isinstance(statement, SliceCopy):
            target = self._array(frame, statement.target)
            source = self._array(frame, statement.source)
            start = self._evaluate(frame, statement.start)
            end = self._evaluate(frame, statement.end)
            if start < 0 or end < start or end > len(source) or end - start != len(target):
                raise InterpreterError("invalid-slice-copy")
            target[:] = source[start:end]
            return
        if isinstance(statement, ObjectDeclare):
            try:
                template = self.templates[statement.template_name]
            except KeyError as error:
                raise InterpreterError("unknown template: " + statement.template_name) from error
            frame.objects[statement.name] = {
                member.name: _Cell(member.template, 0) for member in template.members
            }
            return
        if isinstance(statement, MemberWrite):
            cell = self._member(frame, statement.object_name, statement.member)
            cell.value = cell.template.coerce(self._evaluate(frame, statement.value))
            return
        if isinstance(statement, If):
            branch = statement.then_body if self._evaluate(frame, statement.condition) != 0 else statement.else_body
            self._run_statements(function, frame, branch)
            return
        if isinstance(statement, While):
            while self._evaluate(frame, statement.condition) != 0:
                self._consume_loop_budget()
                self._run_statements(function, frame, statement.body)
            return
        if isinstance(statement, For):
            self._run_statement(function, frame, statement.init)
            while self._evaluate(frame, statement.condition) != 0:
                self._consume_loop_budget()
                self._run_statements(function, frame, statement.body)
                for post in statement.post:
                    self._run_statement(function, frame, post)
            return
        if isinstance(statement, Output):
            self.output.append(str(self._evaluate(frame, statement.value)) + "\n")
            return
        if isinstance(statement, Return):
            raise _ReturnSignal(self._evaluate(frame, statement.value))
        raise InterpreterError("unsupported statement: " + type(statement).__name__)

    def _evaluate(self, frame: _Frame, expression: Expr) -> int:
        if isinstance(expression, Literal):
            return expression.value
        if isinstance(expression, Variable):
            return self._lookup_value_by_name(frame, expression.name).value
        if isinstance(expression, Binary):
            left = self._evaluate(frame, expression.left)
            right = self._evaluate(frame, expression.right)
            if expression.operator == "+":
                return left + right
            if expression.operator == "-":
                return left - right
            if expression.operator == "*":
                return left * right
            if expression.operator == "/":
                if right == 0:
                    raise InterpreterError("integer-division-by-zero")
                quotient = abs(left) // abs(right)
                return -quotient if (left < 0) != (right < 0) else quotient
            raise InterpreterError("unsupported operator: " + expression.operator)
        if isinstance(expression, Compare):
            left = self._evaluate(frame, expression.left)
            right = self._evaluate(frame, expression.right)
            comparisons = {
                "==": left == right,
                "!=": left != right,
                "<": left < right,
                "<=": left <= right,
                ">": left > right,
                ">=": left >= right,
            }
            try:
                return 1 if comparisons[expression.operator] else 0
            except KeyError as error:
                raise InterpreterError("unsupported comparison: " + expression.operator) from error
        if isinstance(expression, Convert):
            return expression.template.coerce(self._evaluate(frame, expression.operand))
        if isinstance(expression, Call):
            return self._call(expression.name, tuple(self._evaluate(frame, item) for item in expression.arguments))
        if isinstance(expression, ArrayRead):
            array = self._array(frame, expression.array)
            index = self._evaluate(frame, expression.index)
            self._check_index(array, index)
            return array[index]
        if isinstance(expression, BooleanTest):
            return 1 if any(self._array(frame, expression.array)) else 0
        if isinstance(expression, SliceLength):
            array = self._array(frame, expression.array)
            start = self._evaluate(frame, expression.start)
            end = self._evaluate(frame, expression.end)
            if start < 0 or end < start or end > len(array):
                raise InterpreterError("invalid-slice")
            return U64.coerce(end - start)
        if isinstance(expression, MemberRead):
            return self._member(frame, expression.object_name, expression.member).value
        raise InterpreterError("unsupported expression: " + type(expression).__name__)

    def _lookup_value(self, function: Function, frame: _Frame, name: str) -> _Cell:
        if name in frame.values:
            return frame.values[name]
        try:
            return self.static_values[function.name + "::" + name]
        except KeyError as error:
            raise InterpreterError("unknown variable: " + name) from error

    @staticmethod
    def _lookup_value_by_name(frame: _Frame, name: str) -> _Cell:
        try:
            return frame.values[name]
        except KeyError as error:
            raise InterpreterError("unknown local variable: " + name) from error

    @staticmethod
    def _array(frame: _Frame, name: str) -> list[int]:
        try:
            return frame.arrays[name]
        except KeyError as error:
            raise InterpreterError("unknown array: " + name) from error

    @staticmethod
    def _member(frame: _Frame, object_name: str, member: str) -> _Cell:
        try:
            return frame.objects[object_name][member]
        except KeyError as error:
            raise InterpreterError("unknown template member: " + object_name + "." + member) from error

    @staticmethod
    def _check_index(array: list[int], index: int) -> None:
        if index < 0 or index >= len(array):
            raise InterpreterError("array-index-out-of-bounds")

    def _consume_loop_budget(self) -> None:
        self._loop_budget -= 1
        if self._loop_budget < 0:
            raise InterpreterError("loop-budget-exhausted")


def evaluate(program: Program) -> str:
    return Interpreter(program).run()
