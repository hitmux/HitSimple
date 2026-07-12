#include "CCompatLoweringInternal.h"

#include "hitsimple/literal/Literal.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace hitsimple::compat::detail {
namespace {

TypeInfo integerType(std::size_t bytes, bool isSigned) {
  CAbiType abi{CAbiValueKind::Integer, bytes, isSigned, ""};
  abi.alignment = bytes;
  return TypeInfo{std::move(abi),
                  std::string(isSigned ? "i" : "u") +
                      std::to_string(bytes * 8U),
                  nullptr};
}

std::unique_ptr<ast::Expr> scaledIndex(std::unique_ptr<ast::Expr> index,
                                       std::size_t elementByteLength,
                                       std::size_t pointerByteLength) {
  return std::make_unique<ast::BinaryExpr>(
      std::move(index), "%" + std::to_string(pointerByteLength) + "d*",
      std::make_unique<ast::IntegerLiteral>(
          std::to_string(elementByteLength)));
}

bool isComparison(std::string_view op) {
  return op == "==" || op == "!=" || op == "<" || op == "<=" ||
         op == ">" || op == ">=";
}

bool isShift(std::string_view op) { return op == "<<" || op == ">>"; }

struct ConstantShiftCount {
  bool isNegative = false;
  std::uint64_t magnitude = 0;
};

struct ConstantInteger {
  std::uint64_t bits = 0;
  std::size_t byteLength = 0;
  bool isSigned = true;
};

struct TypedIntegerOperator {
  std::size_t byteLength = 0;
  bool isSigned = true;
  std::string_view symbol;
};

std::optional<std::uint64_t> integerMask(std::size_t byteLength) {
  if (byteLength != 1U && byteLength != 2U && byteLength != 4U &&
      byteLength != 8U) {
    return std::nullopt;
  }
  if (byteLength == 8U) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return (std::uint64_t{1} << (byteLength * 8U)) - 1U;
}

std::optional<ConstantInteger> convertConstantInteger(
    ConstantInteger value, std::size_t byteLength, bool isSigned) {
  const auto mask = integerMask(byteLength);
  if (!mask) {
    return std::nullopt;
  }
  return ConstantInteger{value.bits & *mask, byteLength, isSigned};
}

std::optional<std::int64_t> signedConstantValue(
    const ConstantInteger& value) {
  if (!value.isSigned) {
    return std::nullopt;
  }
  const auto mask = integerMask(value.byteLength);
  if (!mask) {
    return std::nullopt;
  }
  const auto bits = value.bits & *mask;
  const auto bitWidth = value.byteLength * 8U;
  const auto signBit = std::uint64_t{1} << (bitWidth - 1U);
  if ((bits & signBit) == 0U) {
    return static_cast<std::int64_t>(bits);
  }
  if (value.byteLength == 8U) {
    const auto magnitude = (~bits) + 1U;
    if (magnitude == (std::uint64_t{1} << 63U)) {
      return std::numeric_limits<std::int64_t>::min();
    }
    return -static_cast<std::int64_t>(magnitude);
  }
  return static_cast<std::int64_t>(bits) -
         (std::int64_t{1} << bitWidth);
}

std::optional<ConstantInteger> promoteConstantInteger(
    ConstantInteger value) {
  if (value.byteLength >= 4U) {
    return value;
  }
  std::uint64_t bits = value.bits;
  if (value.isSigned) {
    const auto signedValue = signedConstantValue(value);
    if (!signedValue) {
      return std::nullopt;
    }
    bits = static_cast<std::uint64_t>(*signedValue);
  }
  return convertConstantInteger(
      ConstantInteger{bits, 8U, true}, 4U, true);
}

std::optional<std::uint64_t>
parseCIntegerLiteralMagnitude(std::string_view text) {
  while (!text.empty() &&
         (text.back() == 'u' || text.back() == 'U' || text.back() == 'l' ||
          text.back() == 'L')) {
    text.remove_suffix(1U);
  }
  return literal::parseUnsignedIntegerLiteral(text);
}

std::optional<TypedIntegerOperator> parseTypedIntegerOperator(
    std::string_view op) {
  if (!op.starts_with('%')) {
    return std::nullopt;
  }
  std::size_t cursor = 1U;
  std::size_t byteLength = 0;
  while (cursor < op.size() &&
         std::isdigit(static_cast<unsigned char>(op[cursor]))) {
    byteLength = byteLength * 10U +
                 static_cast<std::size_t>(op[cursor] - '0');
    ++cursor;
  }
  if (cursor == 1U || cursor >= op.size() ||
      (op[cursor] != 'd' && op[cursor] != 'u') ||
      cursor + 1U >= op.size() || !integerMask(byteLength)) {
    return std::nullopt;
  }
  return TypedIntegerOperator{byteLength, op[cursor] == 'd',
                              op.substr(cursor + 1U)};
}

std::optional<ConstantInteger> evaluateIntegerConstant(
    const ast::Expr& expression);

std::optional<ConstantInteger> evaluateTypedIntegerBinary(
    const ast::BinaryExpr& expression, const TypedIntegerOperator& operation) {
  auto left = evaluateIntegerConstant(*expression.left);
  if (!left) {
    return std::nullopt;
  }
  if (operation.symbol == "&&" || operation.symbol == "||") {
    return std::nullopt;
  }
  auto right = evaluateIntegerConstant(*expression.right);
  if (!right) {
    return std::nullopt;
  }

  auto convertedLeft = convertConstantInteger(*left, operation.byteLength,
                                              operation.isSigned);
  if (!convertedLeft) {
    return std::nullopt;
  }
  if (operation.symbol == "<<" || operation.symbol == ">>") {
    auto promotedRight = promoteConstantInteger(*right);
    if (!promotedRight) {
      return std::nullopt;
    }
    if (promotedRight->isSigned) {
      const auto signedCount = signedConstantValue(*promotedRight);
      if (!signedCount || *signedCount < 0) {
        return std::nullopt;
      }
    }
    const auto count = promotedRight->bits &
                       *integerMask(promotedRight->byteLength);
    const auto bitWidth = operation.byteLength * 8U;
    if (count >= bitWidth) {
      return std::nullopt;
    }
    if (operation.symbol == "<<") {
      if (operation.isSigned) {
        const auto signedLeft = signedConstantValue(*convertedLeft);
        if (!signedLeft || *signedLeft < 0 ||
            *signedLeft >
                std::numeric_limits<std::int64_t>::max() >> count) {
          return std::nullopt;
        }
      }
      return convertConstantInteger(
          ConstantInteger{convertedLeft->bits << count, operation.byteLength,
                          operation.isSigned},
          operation.byteLength, operation.isSigned);
    }
    const auto mask = integerMask(operation.byteLength);
    if (!mask) {
      return std::nullopt;
    }
    std::uint64_t bits = convertedLeft->bits >> count;
    if (operation.isSigned &&
        (convertedLeft->bits &
         (std::uint64_t{1} << (bitWidth - 1U))) != 0U) {
      bits |= *mask ^ (*mask >> count);
    }
    return ConstantInteger{bits & *mask, operation.byteLength,
                           operation.isSigned};
  }

  auto convertedRight = convertConstantInteger(*right, operation.byteLength,
                                               operation.isSigned);
  if (!convertedRight) {
    return std::nullopt;
  }
  const auto mask = integerMask(operation.byteLength);
  if (!mask) {
    return std::nullopt;
  }
  const auto leftBits = convertedLeft->bits & *mask;
  const auto rightBits = convertedRight->bits & *mask;

  if (operation.symbol == "&" || operation.symbol == "|" ||
      operation.symbol == "^") {
    const auto bits = operation.symbol == "&"
                          ? leftBits & rightBits
                          : operation.symbol == "|" ? leftBits | rightBits
                                                      : leftBits ^ rightBits;
    return ConstantInteger{bits, operation.byteLength, operation.isSigned};
  }

  if (operation.isSigned) {
    const auto signedLeft = signedConstantValue(*convertedLeft);
    const auto signedRight = signedConstantValue(*convertedRight);
    if (!signedLeft || !signedRight) {
      return std::nullopt;
    }
    const auto bitWidth = operation.byteLength * 8U;
    const auto minimum = bitWidth == 64U
                             ? std::numeric_limits<std::int64_t>::min()
                             : -(std::int64_t{1} << (bitWidth - 1U));
    const auto maximum = bitWidth == 64U
                             ? std::numeric_limits<std::int64_t>::max()
                             : (std::int64_t{1} << (bitWidth - 1U)) - 1;
    if (operation.symbol == "==" || operation.symbol == "!=" ||
        operation.symbol == "<" || operation.symbol == "<=" ||
        operation.symbol == ">" || operation.symbol == ">=") {
      const auto result = operation.symbol == "=="
                              ? *signedLeft == *signedRight
                              : operation.symbol == "!="
                                    ? *signedLeft != *signedRight
                                    : operation.symbol == "<"
                                          ? *signedLeft < *signedRight
                                          : operation.symbol == "<="
                                                ? *signedLeft <= *signedRight
                                                : operation.symbol == ">"
                                                      ? *signedLeft > *signedRight
                                                      : *signedLeft >= *signedRight;
      return ConstantInteger{result ? 1U : 0U, 1U, false};
    }

    std::optional<std::int64_t> result;
    if (operation.symbol == "+") {
      if ((*signedRight > 0 && *signedLeft > maximum - *signedRight) ||
          (*signedRight < 0 && *signedLeft < minimum - *signedRight)) {
        return std::nullopt;
      }
      result = *signedLeft + *signedRight;
    } else if (operation.symbol == "-") {
      if ((*signedRight > 0 && *signedLeft < minimum + *signedRight) ||
          (*signedRight < 0 && *signedLeft > maximum + *signedRight)) {
        return std::nullopt;
      }
      result = *signedLeft - *signedRight;
    } else if (operation.symbol == "*") {
      if (*signedLeft == 0 || *signedRight == 0) {
        result = 0;
      } else if ((*signedLeft == -1 && *signedRight == minimum) ||
                 (*signedRight == -1 && *signedLeft == minimum)) {
        return std::nullopt;
      } else if ((*signedLeft > 0 && *signedRight > 0 &&
                  *signedLeft > maximum / *signedRight) ||
                 (*signedLeft > 0 && *signedRight < 0 &&
                  *signedRight < minimum / *signedLeft) ||
                 (*signedLeft < 0 && *signedRight > 0 &&
                  *signedLeft < minimum / *signedRight) ||
                 (*signedLeft < 0 && *signedRight < 0 &&
                  *signedLeft < maximum / *signedRight)) {
        return std::nullopt;
      } else {
        result = *signedLeft * *signedRight;
      }
    } else if (operation.symbol == "/" || operation.symbol == "%") {
      if (*signedRight == 0 ||
          (*signedLeft == minimum && *signedRight == -1)) {
        return std::nullopt;
      }
      result = operation.symbol == "/" ? *signedLeft / *signedRight
                                         : *signedLeft % *signedRight;
    } else {
      return std::nullopt;
    }
    return convertConstantInteger(
        ConstantInteger{static_cast<std::uint64_t>(*result),
                        operation.byteLength, true},
        operation.byteLength, true);
  }

  if (operation.symbol == "==" || operation.symbol == "!=" ||
      operation.symbol == "<" || operation.symbol == "<=" ||
      operation.symbol == ">" || operation.symbol == ">=") {
    const auto result = operation.symbol == "=="
                            ? leftBits == rightBits
                            : operation.symbol == "!="
                                  ? leftBits != rightBits
                                  : operation.symbol == "<"
                                        ? leftBits < rightBits
                                        : operation.symbol == "<="
                                              ? leftBits <= rightBits
                                              : operation.symbol == ">"
                                                    ? leftBits > rightBits
                                                    : leftBits >= rightBits;
    return ConstantInteger{result ? 1U : 0U, 1U, false};
  }
  if ((operation.symbol == "/" || operation.symbol == "%") &&
      rightBits == 0U) {
    return std::nullopt;
  }
  const auto bits = operation.symbol == "+"
                        ? leftBits + rightBits
                        : operation.symbol == "-"
                              ? leftBits - rightBits
                              : operation.symbol == "*"
                                    ? leftBits * rightBits
                                    : operation.symbol == "/"
                                          ? leftBits / rightBits
                                          : operation.symbol == "%"
                                                ? leftBits % rightBits
                                                : std::uint64_t{0};
  if (operation.symbol != "+" && operation.symbol != "-" &&
      operation.symbol != "*" && operation.symbol != "/" &&
      operation.symbol != "%") {
    return std::nullopt;
  }
  return ConstantInteger{bits & *mask, operation.byteLength, false};
}

std::optional<ConstantInteger> evaluateIntegerConstant(
    const ast::Expr& expression) {
  if (const auto* integer = dynamic_cast<const ast::IntegerLiteral*>(&expression)) {
    const auto magnitude = parseCIntegerLiteralMagnitude(integer->value);
    if (!magnitude) {
      return std::nullopt;
    }
    return ConstantInteger{*magnitude,
                           *magnitude > static_cast<std::uint64_t>(
                                            std::numeric_limits<std::int32_t>::max())
                               ? 8U
                               : 4U,
                           *magnitude <= static_cast<std::uint64_t>(
                                             std::numeric_limits<std::int64_t>::max())};
  }
  if (const auto* unsignedExpr =
          dynamic_cast<const ast::UnsignedExpr*>(&expression)) {
    auto operand = evaluateIntegerConstant(*unsignedExpr->operand);
    return operand ? convertConstantInteger(*operand, unsignedExpr->byteLength,
                                            false)
                   : std::nullopt;
  }
  if (const auto* cast = dynamic_cast<const ast::IntegerCastExpr*>(&expression)) {
    auto operand = evaluateIntegerConstant(*cast->operand);
    return operand ? convertConstantInteger(*operand, cast->byteLength,
                                            cast->isSigned)
                   : std::nullopt;
  }
  if (const auto* unary = dynamic_cast<const ast::UnaryExpr*>(&expression)) {
    auto operand = evaluateIntegerConstant(*unary->operand);
    if (!operand) {
      return std::nullopt;
    }
    if (unary->op == "!") {
      const auto mask = integerMask(operand->byteLength);
      if (!mask) {
        return std::nullopt;
      }
      return ConstantInteger{(operand->bits & *mask) == 0U ? 1U : 0U, 1U,
                             false};
    }
    operand = promoteConstantInteger(*operand);
    if (!operand) {
      return std::nullopt;
    }
    const auto mask = integerMask(operand->byteLength);
    if (!mask) {
      return std::nullopt;
    }
    if (unary->op == "+") {
      return operand;
    }
    if (unary->op == "~") {
      return ConstantInteger{(~operand->bits) & *mask, operand->byteLength,
                             operand->isSigned};
    }
    if (unary->op != "-") {
      return std::nullopt;
    }
    if (!operand->isSigned) {
      return ConstantInteger{(-operand->bits) & *mask, operand->byteLength,
                             false};
    }
    const auto signedValue = signedConstantValue(*operand);
    if (!signedValue ||
        *signedValue == std::numeric_limits<std::int64_t>::min()) {
      return std::nullopt;
    }
    return convertConstantInteger(
        ConstantInteger{static_cast<std::uint64_t>(-*signedValue),
                        operand->byteLength, true},
        operand->byteLength, true);
  }
  if (const auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expression)) {
    if (binary->op == "&&" || binary->op == "||") {
      auto left = evaluateIntegerConstant(*binary->left);
      if (!left) {
        return std::nullopt;
      }
      const auto mask = integerMask(left->byteLength);
      if (!mask) {
        return std::nullopt;
      }
      const bool leftIsTrue = (left->bits & *mask) != 0U;
      if ((binary->op == "&&" && !leftIsTrue) ||
          (binary->op == "||" && leftIsTrue)) {
        return ConstantInteger{binary->op == "||" ? 1U : 0U, 1U, false};
      }
      auto right = evaluateIntegerConstant(*binary->right);
      if (!right) {
        return std::nullopt;
      }
      const auto rightMask = integerMask(right->byteLength);
      if (!rightMask) {
        return std::nullopt;
      }
      return ConstantInteger{(right->bits & *rightMask) != 0U ? 1U : 0U,
                             1U, false};
    }
    const auto operation = parseTypedIntegerOperator(binary->op);
    return operation ? evaluateTypedIntegerBinary(*binary, *operation)
                     : std::nullopt;
  }
  if (const auto* conditional =
          dynamic_cast<const ast::TernaryExpr*>(&expression)) {
    auto condition = evaluateIntegerConstant(*conditional->condition);
    if (!condition) {
      return std::nullopt;
    }
    const auto mask = integerMask(condition->byteLength);
    if (!mask) {
      return std::nullopt;
    }
    return evaluateIntegerConstant((condition->bits & *mask) != 0U
                                       ? *conditional->thenExpr
                                       : *conditional->elseExpr);
  }
  return std::nullopt;
}

std::optional<ConstantShiftCount> constantShiftCount(
    const ast::Expr& expression, const TypeInfo& type) {
  auto value = evaluateIntegerConstant(expression);
  if (!value) {
    return std::nullopt;
  }
  value = promoteConstantInteger(*value);
  if (!value) {
    return std::nullopt;
  }
  value = convertConstantInteger(*value, type.abi.byteLength,
                                 type.abi.isSigned);
  if (!value) {
    return std::nullopt;
  }
  if (!value->isSigned) {
    return ConstantShiftCount{false, value->bits};
  }
  const auto signedValue = signedConstantValue(*value);
  if (!signedValue) {
    return std::nullopt;
  }
  if (*signedValue >= 0) {
    return ConstantShiftCount{false,
                              static_cast<std::uint64_t>(*signedValue)};
  }
  const auto magnitude = *signedValue == std::numeric_limits<std::int64_t>::min()
                             ? std::uint64_t{1} << 63U
                             : static_cast<std::uint64_t>(-*signedValue);
  return ConstantShiftCount{true, magnitude};
}

bool isUnsignedInteger(const TypeInfo& type) {
  return type.abi.kind == CAbiValueKind::Integer && !type.abi.isSigned;
}

TypeInfo promoteInteger(TypeInfo type) {
  if (type.abi.byteLength < 4U) {
    return integerType(4, true);
  }
  return type;
}

TypeInfo usualIntegerType(const TypeInfo& left, const TypeInfo& right) {
  TypeInfo promotedLeft = promoteInteger(left);
  TypeInfo promotedRight = promoteInteger(right);
  if (promotedLeft.abi.isSigned == promotedRight.abi.isSigned) {
    return promotedLeft.abi.byteLength >= promotedRight.abi.byteLength
               ? promotedLeft
               : promotedRight;
  }

  const TypeInfo& unsignedType =
      promotedLeft.abi.isSigned ? promotedRight : promotedLeft;
  const TypeInfo& signedType =
      promotedLeft.abi.isSigned ? promotedLeft : promotedRight;
  if (unsignedType.abi.byteLength >= signedType.abi.byteLength) {
    return integerType(unsignedType.abi.byteLength, false);
  }
  if (signedType.abi.byteLength > unsignedType.abi.byteLength) {
    return signedType;
  }
  return integerType(signedType.abi.byteLength, false);
}

std::string cIntegerOperator(std::size_t byteLength,
                             bool isSigned,
                             std::string_view op) {
  return "%" + std::to_string(byteLength) + (isSigned ? "d" : "u") +
         std::string(op);
}

std::unique_ptr<ast::Expr> preserveUnsigned(std::unique_ptr<ast::Expr> value,
                                            const TypeInfo& type) {
  if (!isUnsignedInteger(type) ||
      dynamic_cast<ast::UnsignedExpr*>(value.get()) != nullptr) {
    return value;
  }
  return std::make_unique<ast::UnsignedExpr>(std::move(value),
                                             type.abi.byteLength);
}

} // namespace

std::optional<ExprResult> Lowerer::lowerUnary(const UnaryExpr& expression) {
  if (expression.op == "*") {
    auto operand = lowerExpr(*expression.operand);
    if (!operand) {
      return std::nullopt;
    }
    operand = decayArray(std::move(*operand), expression.operand->range);
    if (!operand || !isPointer(operand->type) || operand->type.pointee == nullptr) {
      error(expression.range, "C dereference requires a pointer with element metadata");
      return std::nullopt;
    }
    TypeInfo type = *operand->type.pointee;
    if (isVoid(type)) {
      error(expression.range, "cannot dereference void pointer in C compatibility mode");
      return std::nullopt;
    }
    if (isAggregate(type) && !ensureStructComplete(type, expression.range)) {
      return std::nullopt;
    }
    return ExprResult{std::make_unique<ast::DerefExpr>(
                          std::to_string(type.abi.byteLength),
                          std::move(operand->expression)),
                      std::move(type), true, false, 0};
  }
  if (expression.op == "&") {
    auto operand = lowerLvalue(*expression.operand);
    if (!operand) {
      return std::nullopt;
    }
    if (operand->isArray) {
      error(expression.range, "address-of array objects is outside the minimal C subset");
      return std::nullopt;
    }
    auto type = makePointer(operand->type, options_.pointerByteLength);
    return ExprResult{std::make_unique<ast::UnaryExpr>("&", std::move(operand->expression)),
                      std::move(type), false, false, 0};
  }

  auto operand = lowerExpr(*expression.operand);
  if (!operand) {
    return std::nullopt;
  }
  operand = decayArray(std::move(*operand), expression.operand->range);
  if (!operand) {
    return std::nullopt;
  }
  if (expression.op == "!") {
    if (!isInteger(operand->type) && !isPointer(operand->type)) {
      error(expression.range, "C logical negation requires an integer or pointer");
      return std::nullopt;
    }
    return ExprResult{std::make_unique<ast::UnaryExpr>("!", std::move(operand->expression)),
                      integerType(1, false), false, false, 0};
  }
  if (!isInteger(operand->type)) {
    error(expression.range,
          "C unary numeric lowering currently supports integer operands only");
    return std::nullopt;
  }
  TypeInfo resultType = promoteInteger(operand->type);
  auto core = std::make_unique<ast::UnaryExpr>(
      expression.op, preserveUnsigned(std::move(operand->expression),
                                      operand->type));
  return ExprResult{preserveUnsigned(std::move(core), resultType),
                    std::move(resultType), false, false, 0};
}

std::optional<ExprResult> Lowerer::lowerIndex(const IndexExpr& expression) {
  auto base = lowerExpr(*expression.base);
  auto index = lowerExpr(*expression.index);
  if (!base || !index) {
    return std::nullopt;
  }
  index = decayArray(std::move(*index), expression.index->range);
  if (!index || !isInteger(index->type)) {
    error(expression.index->range, "C subscript index must be an integer expression");
    return std::nullopt;
  }

  if (base->isArray) {
    TypeInfo element = base->type;
    if (isAggregate(element) && !ensureStructComplete(element, expression.range)) {
      return std::nullopt;
    }
    if (element.abi.byteLength == 0) {
      error(expression.range,
            "C subscript requires a statically known element byte length");
      return std::nullopt;
    }
    auto offset = scaledIndex(
        preserveUnsigned(std::move(index->expression), index->type),
        element.abi.byteLength, options_.pointerByteLength);
    if (base->arrayBackedByAddress) {
      auto address = std::make_unique<ast::BinaryExpr>(
          std::move(base->expression),
          "%" + std::to_string(options_.pointerByteLength) + "d+",
          std::move(offset));
      return ExprResult{std::make_unique<ast::DerefExpr>(
                            std::to_string(element.abi.byteLength),
                            std::move(address)),
                        std::move(element), true, false, 0};
    }
    auto core = std::make_unique<ast::SliceExpr>(
        std::move(base->expression), std::move(offset),
        std::make_unique<ast::IntegerLiteral>(
            std::to_string(element.abi.byteLength)),
        true);
    return ExprResult{std::move(core), std::move(element), true, false, 0};
  }

  base = decayArray(std::move(*base), expression.base->range);
  if (!base || !isPointer(base->type) || base->type.pointee == nullptr) {
    error(expression.base->range,
          "C subscript base must be an array or pointer with element metadata");
    return std::nullopt;
  }
  TypeInfo element = *base->type.pointee;
  if (isVoid(element)) {
    error(expression.range, "C void pointer cannot be subscripted");
    return std::nullopt;
  }
  if (isAggregate(element) && !ensureStructComplete(element, expression.range)) {
    return std::nullopt;
  }
  if (element.abi.byteLength == 0) {
    error(expression.range,
          "C subscript requires a statically known element byte length");
    return std::nullopt;
  }
  auto offset = scaledIndex(
      preserveUnsigned(std::move(index->expression), index->type),
      element.abi.byteLength, options_.pointerByteLength);
  auto address = std::make_unique<ast::BinaryExpr>(
      std::move(base->expression),
      "%" + std::to_string(options_.pointerByteLength) + "d+", std::move(offset));
  return ExprResult{std::make_unique<ast::DerefExpr>(
                        std::to_string(element.abi.byteLength), std::move(address)),
                    std::move(element), true, false, 0};
}

std::optional<ExprResult> Lowerer::lowerBinary(const BinaryExpr& expression) {
  auto left = lowerExpr(*expression.left);
  auto right = lowerExpr(*expression.right);
  if (!left || !right) {
    return std::nullopt;
  }
  left = decayArray(std::move(*left), expression.left->range);
  right = decayArray(std::move(*right), expression.right->range);
  if (!left || !right) {
    return std::nullopt;
  }

  const bool leftPointer = isPointer(left->type);
  const bool rightPointer = isPointer(right->type);
  if (leftPointer || rightPointer) {
    if (expression.op == "-" && leftPointer && rightPointer) {
      if (left->type.pointee == nullptr || right->type.pointee == nullptr ||
          !sameType(*left->type.pointee, *right->type.pointee) ||
          isVoid(*left->type.pointee)) {
        error(expression.range,
              "C pointer difference requires equal, sized element metadata");
        return std::nullopt;
      }
      TypeInfo element = *left->type.pointee;
      if (isAggregate(element) && !ensureStructComplete(element, expression.range)) {
        return std::nullopt;
      }
      auto difference = std::make_unique<ast::BinaryExpr>(
          std::move(left->expression),
          "%" + std::to_string(options_.pointerByteLength) + "d-",
          std::move(right->expression));
      auto core = std::make_unique<ast::BinaryExpr>(
          std::move(difference),
          "%" + std::to_string(options_.pointerByteLength) + "d/",
          std::make_unique<ast::IntegerLiteral>(
              std::to_string(element.abi.byteLength)));
      return ExprResult{std::move(core), integerType(8, true), false, false, 0};
    }
    if ((expression.op == "+" && (leftPointer || rightPointer)) ||
        (expression.op == "-" && leftPointer && isInteger(right->type))) {
      const bool pointerOnLeft = leftPointer;
      ExprResult* pointer = pointerOnLeft ? &*left : &*right;
      ExprResult* index = pointerOnLeft ? &*right : &*left;
      if (!isInteger(index->type) || pointer->type.pointee == nullptr ||
          isVoid(*pointer->type.pointee)) {
        error(expression.range,
              "C pointer arithmetic requires one sized pointer and one integer");
        return std::nullopt;
      }
      TypeInfo element = *pointer->type.pointee;
      if (isAggregate(element) && !ensureStructComplete(element, expression.range)) {
        return std::nullopt;
      }
      auto offset = scaledIndex(
          preserveUnsigned(std::move(index->expression), index->type),
          element.abi.byteLength, options_.pointerByteLength);
      auto core = std::make_unique<ast::BinaryExpr>(
          std::move(pointer->expression),
          "%" + std::to_string(options_.pointerByteLength) + "d" + expression.op,
          std::move(offset));
      return ExprResult{std::move(core), pointer->type, false, false, 0};
    }
    if (isComparison(expression.op)) {
      return ExprResult{std::make_unique<ast::BinaryExpr>(
                            std::move(left->expression), expression.op,
                            std::move(right->expression)),
                        integerType(1, false), false, false, 0};
    }
    if ((expression.op == "&&" || expression.op == "||") &&
        (leftPointer || rightPointer || isInteger(left->type) || isInteger(right->type))) {
      return ExprResult{std::make_unique<ast::BinaryExpr>(
                            std::move(left->expression), expression.op,
                            std::move(right->expression)),
                        integerType(1, false), false, false, 0};
    }
    error(expression.range, "unsupported C pointer binary operator '" +
                                expression.op + "'");
    return std::nullopt;
  }

  if (isFloating(left->type) || isFloating(right->type)) {
    if (!isFloating(left->type) || !isFloating(right->type) ||
        (expression.op != "+" && expression.op != "-" && expression.op != "*" &&
         expression.op != "/" && !isComparison(expression.op))) {
      error(expression.range,
            "C floating expressions require two float operands and an arithmetic or comparison operator");
      return std::nullopt;
    }
    const auto byteLength = std::max(left->type.abi.byteLength,
                                     right->type.abi.byteLength);
    auto core = std::make_unique<ast::BinaryExpr>(
        std::move(left->expression), floatOperator(byteLength, expression.op),
        std::move(right->expression));
    if (isComparison(expression.op)) {
      return ExprResult{std::move(core), integerType(1, false), false, false, 0};
    }
    CAbiType abi{CAbiValueKind::Floating, byteLength, true, ""};
    abi.alignment = byteLength;
    return ExprResult{std::move(core),
                      TypeInfo{std::move(abi),
                               "f" + std::to_string(byteLength * 8U), nullptr},
                      false, false, 0};
  }

  if (!isInteger(left->type) || !isInteger(right->type)) {
    error(expression.range, "C binary operator requires scalar integer or floating operands");
    return std::nullopt;
  }
  if (isShift(expression.op)) {
    const TypeInfo operationType = promoteInteger(left->type);
    const TypeInfo countType = promoteInteger(right->type);
    if (const auto count =
            constantShiftCount(*right->expression, countType)) {
      if (count->isNegative) {
        error(expression.right->range,
              "C compatibility shift count must be non-negative");
        return std::nullopt;
      }
      const auto bitWidth = operationType.abi.byteLength * 8U;
      if (count->magnitude >= bitWidth) {
        error(expression.right->range,
              "C compatibility shift count must be smaller than " +
                  std::to_string(bitWidth));
        return std::nullopt;
      }
    }
    auto core = std::make_unique<ast::BinaryExpr>(
        preserveUnsigned(std::move(left->expression), left->type),
        cIntegerOperator(operationType.abi.byteLength,
                         operationType.abi.isSigned, expression.op),
        preserveUnsigned(std::move(right->expression), right->type));
    return ExprResult{preserveUnsigned(std::move(core), operationType),
                      std::move(operationType), false, false, 0};
  }
  const TypeInfo operationType = usualIntegerType(left->type, right->type);
  auto leftValue = preserveUnsigned(std::move(left->expression), left->type);
  auto rightValue = preserveUnsigned(std::move(right->expression), right->type);
  if (expression.op == "&&" || expression.op == "||") {
    return ExprResult{std::make_unique<ast::BinaryExpr>(
                          std::move(leftValue), expression.op,
                          std::move(rightValue)),
                      integerType(1, false), false, false, 0};
  }
  auto core = std::make_unique<ast::BinaryExpr>(
      std::move(leftValue),
      cIntegerOperator(operationType.abi.byteLength, operationType.abi.isSigned,
                       expression.op),
      std::move(rightValue));
  if (isComparison(expression.op)) {
    return ExprResult{std::move(core), integerType(1, false), false, false, 0};
  }
  return ExprResult{preserveUnsigned(std::move(core), operationType),
                    std::move(operationType), false, false, 0};
}

std::optional<ExprResult> Lowerer::lowerAssignment(
    const AssignmentExpr& expression) {
  auto target = lowerLvalue(*expression.target);
  auto value = lowerExpr(*expression.value);
  if (!target || !value) {
    return std::nullopt;
  }
  value = decayArray(std::move(*value), expression.value->range);
  if (!value) {
    return std::nullopt;
  }
  if (target->isArray) {
    error(expression.target->range, "C array assignment is outside the minimal subset");
    return std::nullopt;
  }
  const TypeInfo targetType = target->type;
  const TypeInfo valueType = value->type;
  std::string op = expression.op;
  if (op == "=") {
    const bool scalarConversion = isInteger(target->type) && isInteger(value->type);
    const bool nullPointer = isPointer(target->type) && isInteger(value->type);
    if (!sameType(target->type, value->type) && !scalarConversion && !nullPointer) {
      error(expression.range,
            "C assignment requires an explicit supported conversion in compatibility mode");
      return std::nullopt;
    }
  } else {
    if (!isInteger(targetType) || !isInteger(valueType) ||
        (op != "+=" && op != "-=" && op != "*=" && op != "/=" &&
         op != "%=" && op != "<<=" && op != ">>=" && op != "&=" &&
         op != "^=" && op != "|=")) {
      error(expression.range,
            "C compound assignment is not supported for this lowered type/operator");
      return std::nullopt;
    }
    const bool shift = op == "<<=" || op == ">>=";
    const TypeInfo operationType =
        shift ? promoteInteger(targetType) : usualIntegerType(targetType, valueType);
    if (shift) {
      const TypeInfo countType = promoteInteger(valueType);
      if (const auto count =
              constantShiftCount(*value->expression, countType)) {
        if (count->isNegative) {
          error(expression.value->range,
                "C compatibility shift count must be non-negative");
          return std::nullopt;
        }
        const auto bitWidth = operationType.abi.byteLength * 8U;
        if (count->magnitude >= bitWidth) {
          error(expression.value->range,
                "C compatibility shift count must be smaller than " +
                    std::to_string(bitWidth));
          return std::nullopt;
        }
      }
    }
    op = cIntegerOperator(operationType.abi.byteLength,
                          operationType.abi.isSigned,
                          std::string_view(op).substr(0, op.size() - 1U)) +
         "=";
  }
  auto core = std::make_unique<ast::AssignmentExpr>(
      std::move(target->expression), std::move(op),
      preserveUnsigned(std::move(value->expression), valueType));
  core->targets.front().unsignedTarget = isUnsignedInteger(targetType);
  return ExprResult{std::move(core), targetType, false, false, 0};
}

std::optional<ExprResult> Lowerer::lowerCast(const CastExpr& expression) {
  auto target = resolveType(expression.type, expression.pointerDepth);
  auto operand = lowerExpr(*expression.operand);
  if (!target || !operand) {
    return std::nullopt;
  }
  operand = decayArray(std::move(*operand), expression.operand->range);
  if (!operand || isAggregate(*target)) {
    if (target && isAggregate(*target)) {
      error(expression.range, "C aggregate casts are outside the minimal subset");
    }
    return std::nullopt;
  }
  if (isPointer(*target)) {
    if (!isPointer(operand->type) && !isInteger(operand->type)) {
      error(expression.range, "C pointer cast requires pointer or integer operand");
      return std::nullopt;
    }
    // Core AsExpr is not usable yet.  Preserve the pointer interpretation in
    // compatibility metadata while passing the pointer-sized core value through.
    return ExprResult{std::move(operand->expression), std::move(*target), false,
                      false, 0};
  }
  if (sameType(*target, operand->type)) {
    return ExprResult{std::move(operand->expression), std::move(*target), false,
                      false, 0};
  }
  if (isInteger(*target) && isInteger(operand->type)) {
    auto source = preserveUnsigned(std::move(operand->expression), operand->type);
    return ExprResult{std::make_unique<ast::IntegerCastExpr>(
                          std::move(source), target->abi.byteLength,
                          target->abi.isSigned),
                      std::move(*target), false, false, 0};
  }
  if (isFloating(*target) && isInteger(operand->type)) {
    std::vector<std::unique_ptr<ast::Expr>> arguments;
    arguments.push_back(std::move(operand->expression));
    return ExprResult{std::make_unique<ast::CallExpr>(
                          "to_f" + std::to_string(target->abi.byteLength * 8U),
                          std::move(arguments)),
                      std::move(*target), false, false, 0};
  }
  if (isInteger(*target) && isFloating(operand->type)) {
    std::vector<std::unique_ptr<ast::Expr>> arguments;
    arguments.push_back(std::move(operand->expression));
    const std::string name =
        std::string(target->abi.isSigned ? "to_i" : "to_u") +
        std::to_string(target->abi.byteLength * 8U);
    return ExprResult{std::make_unique<ast::CallExpr>(name, std::move(arguments)),
                      std::move(*target), false, false, 0};
  }
  error(expression.range,
        "C numeric cast requires core to_iN/to_uN/to_fN lowering support");
  return std::nullopt;
}

} // namespace hitsimple::compat::detail
