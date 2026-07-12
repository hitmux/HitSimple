#include "CCompatLoweringInternal.h"

#include "hitsimple/literal/Literal.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <utility>

namespace hitsimple::compat::detail {
namespace {

TypeInfo integerType(std::size_t bytes, bool isSigned) {
  const auto bits = bytes * 8U;
  CAbiType abi{CAbiValueKind::Integer, bytes, isSigned, ""};
  abi.alignment = bytes;
  return TypeInfo{std::move(abi),
                  std::string(isSigned ? "i" : "u") + std::to_string(bits),
                  nullptr};
}

TypeInfo floatingType(std::size_t bytes) {
  CAbiType abi{CAbiValueKind::Floating, bytes, true, ""};
  abi.alignment = bytes;
  return TypeInfo{std::move(abi), "f" + std::to_string(bytes * 8U), nullptr};
}

std::string stripFloatSuffix(std::string value) {
  if (!value.empty() && (value.back() == 'f' || value.back() == 'F')) {
    value.pop_back();
  }
  return value;
}

struct CIntegerLiteral {
  std::string value;
  std::size_t byteLength = 0;
  bool isSigned = true;
};

std::optional<unsigned int> digitValue(char ch) {
  if (ch >= '0' && ch <= '9') {
    return static_cast<unsigned int>(ch - '0');
  }
  if (ch >= 'a' && ch <= 'f') {
    return static_cast<unsigned int>(ch - 'a') + 10U;
  }
  if (ch >= 'A' && ch <= 'F') {
    return static_cast<unsigned int>(ch - 'A') + 10U;
  }
  return std::nullopt;
}

std::optional<CIntegerLiteral> parseCIntegerLiteral(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }

  unsigned int base = 10;
  std::size_t offset = 0;
  bool nonDecimal = false;
  if (text.size() >= 2U && text[0] == '0') {
    const char prefix = text[1];
    if (prefix == 'x' || prefix == 'X') {
      base = 16;
      offset = 2;
      nonDecimal = true;
    } else if (prefix == 'o' || prefix == 'O') {
      base = 8;
      offset = 2;
      nonDecimal = true;
    } else if (prefix == 'b' || prefix == 'B') {
      base = 2;
      offset = 2;
      nonDecimal = true;
    } else {
      base = 8;
      offset = 0;
      nonDecimal = true;
    }
  }

  const std::size_t digitsBegin = offset;
  bool sawDigit = false;
  bool previousUnderscore = false;
  while (offset < text.size()) {
    const char ch = text[offset];
    if (ch == '_') {
      if (!sawDigit || previousUnderscore) {
        return std::nullopt;
      }
      previousUnderscore = true;
      ++offset;
      continue;
    }
    const auto digit = digitValue(ch);
    if (!digit || *digit >= base) {
      break;
    }
    sawDigit = true;
    previousUnderscore = false;
    ++offset;
  }
  if (offset == digitsBegin || !sawDigit || previousUnderscore) {
    return std::nullopt;
  }

  const std::size_t suffixBegin = offset;
  bool hasUnsignedSuffix = false;
  unsigned int longSuffixCount = 0;
  for (; offset < text.size(); ++offset) {
    const char suffix = static_cast<char>(
        std::tolower(static_cast<unsigned char>(text[offset])));
    if (suffix == 'u' && !hasUnsignedSuffix) {
      hasUnsignedSuffix = true;
      continue;
    }
    if (suffix == 'l' && longSuffixCount < 2U) {
      ++longSuffixCount;
      continue;
    }
    return std::nullopt;
  }

  const std::string value(text.substr(0, suffixBegin));
  const auto parsed = literal::parseUnsignedIntegerLiteral(value);
  if (!parsed) {
    return std::nullopt;
  }

  const auto valueFitsI32 =
      *parsed <= static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
  const auto valueFitsU32 =
      *parsed <= static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
  const auto valueFitsI64 =
      *parsed <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());

  if (hasUnsignedSuffix && longSuffixCount != 0U) {
    return CIntegerLiteral{value, 8, false};
  }
  if (longSuffixCount != 0U) {
    if (!valueFitsI64 && !nonDecimal) {
      return std::nullopt;
    }
    return CIntegerLiteral{value, 8, valueFitsI64};
  }
  if (hasUnsignedSuffix) {
    return CIntegerLiteral{value, valueFitsU32 ? 4U : 8U, false};
  }
  if (valueFitsI32) {
    return CIntegerLiteral{value, 4, true};
  }
  if (nonDecimal && valueFitsU32) {
    return CIntegerLiteral{value, 4, false};
  }
  if (valueFitsI64) {
    return CIntegerLiteral{value, 8, true};
  }
  if (nonDecimal) {
    return CIntegerLiteral{value, 8, false};
  }
  return std::nullopt;
}

} // namespace

std::optional<ExprResult> Lowerer::decayArray(
    ExprResult value, const diagnostic::SourceRange& range) {
  if (!value.isArray) {
    return value;
  }
  if (!value.expression) {
    error(range, "internal error while decaying C array expression");
    return std::nullopt;
  }
  auto pointer = makePointer(value.type, options_.pointerByteLength);
  if (value.arrayBackedByAddress) {
    return ExprResult{std::move(value.expression), std::move(pointer), false,
                      false, 0};
  }
  auto expression = std::make_unique<ast::UnaryExpr>("&", std::move(value.expression));
  return ExprResult{std::move(expression), std::move(pointer), false, false, 0};
}

std::optional<ExprResult> Lowerer::lowerExpr(const Expr& expression) {
  if (const auto* identifier = dynamic_cast<const IdentifierExpr*>(&expression)) {
    const auto object = lookupObject(identifier->name);
    if (!object) {
      error(identifier->range, "use of undeclared C compatibility object '" +
                               identifier->name + "'");
      return std::nullopt;
    }
    return ExprResult{std::make_unique<ast::IdentifierExpr>(identifier->name),
                      object->type, true, object->isArray, object->arrayCount};
  }
  if (const auto* integer = dynamic_cast<const IntegerLiteralExpr*>(&expression)) {
    const auto parsed = parseCIntegerLiteral(integer->value);
    if (!parsed) {
      error(integer->range,
            "invalid or unsupported C integer literal '" + integer->value + "'");
      return std::nullopt;
    }
    std::unique_ptr<ast::Expr> core =
        std::make_unique<ast::IntegerLiteral>(parsed->value);
    if (!parsed->isSigned) {
      core = std::make_unique<ast::UnsignedExpr>(std::move(core),
                                                 parsed->byteLength);
    }
    return ExprResult{std::move(core),
                      integerType(parsed->byteLength, parsed->isSigned),
                      false, false, 0};
  }
  if (const auto* floating = dynamic_cast<const FloatLiteralExpr*>(&expression)) {
    const bool isSingle = !floating->value.empty() &&
                          (floating->value.back() == 'f' || floating->value.back() == 'F');
    return ExprResult{std::make_unique<ast::FloatLiteral>(
                          stripFloatSuffix(floating->value)),
                      floatingType(isSingle ? 4U : 8U), false, false, 0};
  }
  if (const auto* string = dynamic_cast<const StringLiteralExpr*>(&expression)) {
    auto character = integerType(1, false);
    return ExprResult{std::make_unique<ast::StringLiteral>(string->value),
                      makePointer(std::move(character), options_.pointerByteLength),
                      false, false, 0};
  }
  if (const auto* character = dynamic_cast<const CharLiteralExpr*>(&expression)) {
    return ExprResult{std::make_unique<ast::CharLiteral>(character->value),
                      integerType(1, false), false, false, 0};
  }
  if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expression)) {
    return lowerUnary(*unary);
  }
  if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
    return lowerBinary(*binary);
  }
  if (const auto* conditional = dynamic_cast<const ConditionalExpr*>(&expression)) {
    auto condition = lowerExpr(*conditional->condition);
    auto thenExpr = lowerExpr(*conditional->thenExpr);
    auto elseExpr = lowerExpr(*conditional->elseExpr);
    if (!condition || !thenExpr || !elseExpr) {
      return std::nullopt;
    }
    thenExpr = decayArray(std::move(*thenExpr), conditional->thenExpr->range);
    elseExpr = decayArray(std::move(*elseExpr), conditional->elseExpr->range);
    if (!thenExpr || !elseExpr || !sameType(thenExpr->type, elseExpr->type)) {
      error(conditional->range,
            "C conditional expression branches require the same lowered type");
      return std::nullopt;
    }
    return ExprResult{std::make_unique<ast::TernaryExpr>(
                          std::move(condition->expression),
                          std::move(thenExpr->expression),
                          std::move(elseExpr->expression)),
                      thenExpr->type, false, false, 0};
  }
  if (const auto* assignment = dynamic_cast<const AssignmentExpr*>(&expression)) {
    return lowerAssignment(*assignment);
  }
  if (const auto* cast = dynamic_cast<const CastExpr*>(&expression)) {
    return lowerCast(*cast);
  }
  if (const auto* sizeofExpr = dynamic_cast<const SizeofExpr*>(&expression)) {
    return lowerSizeof(*sizeofExpr);
  }
  if (const auto* index = dynamic_cast<const IndexExpr*>(&expression)) {
    return lowerIndex(*index);
  }
  if (const auto* call = dynamic_cast<const CallExpr*>(&expression)) {
    return lowerCall(*call);
  }
  if (const auto* member = dynamic_cast<const MemberExpr*>(&expression)) {
    return lowerMember(*member, false);
  }
  error(expression.range, "unsupported C compatibility expression");
  return std::nullopt;
}

std::optional<ExprResult> Lowerer::lowerLvalue(const Expr& expression) {
  if (dynamic_cast<const IdentifierExpr*>(&expression) != nullptr) {
    auto result = lowerExpr(expression);
    if (!result) {
      return std::nullopt;
    }
    if (result->isArray) {
      error(expression.range, "C array object is not an assignable scalar lvalue");
      return std::nullopt;
    }
    return result;
  }
  if (const auto* index = dynamic_cast<const IndexExpr*>(&expression)) {
    return lowerIndex(*index);
  }
  if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expression);
      unary != nullptr && unary->op == "*") {
    return lowerUnary(*unary);
  }
  if (const auto* member = dynamic_cast<const MemberExpr*>(&expression)) {
    return lowerMember(*member, true);
  }
  error(expression.range, "C assignment target is not an lvalue in the compatibility subset");
  return std::nullopt;
}

std::optional<ExprResult> Lowerer::lowerSizeof(const SizeofExpr& expression) {
  std::size_t byteLength = 0;
  if (expression.type) {
    auto type = resolveType(*expression.type);
    if (!type || isVoid(*type)) {
      if (type && isVoid(*type)) {
        error(expression.range, "sizeof(void) is not available in the C compatibility subset");
      }
      return std::nullopt;
    }
    if (isAggregate(*type) && !ensureStructComplete(*type, expression.range)) {
      return std::nullopt;
    }
    byteLength = type->abi.byteLength;
  } else {
    const auto object = lookupObject(expression.identifier);
    if (!object) {
      error(expression.range, "sizeof references unknown C object '" +
                                expression.identifier + "'");
      return std::nullopt;
    }
    byteLength = object->byteLength();
  }
  if (byteLength == 0) {
    error(expression.range, "sizeof requires a statically sized C type or object");
    return std::nullopt;
  }
  const auto resultType = integerType(options_.pointerByteLength, false);
  return ExprResult{std::make_unique<ast::IntegerLiteral>(
                        std::to_string(byteLength)),
                    resultType, false, false, 0};
}

std::optional<ExprResult> Lowerer::lowerCall(const CallExpr& expression) {
  const auto* callee = dynamic_cast<const IdentifierExpr*>(expression.callee.get());
  if (callee == nullptr) {
    error(expression.range,
          "function-pointer calls are outside the Standard 16.1 compatibility subset");
    return std::nullopt;
  }
  const auto found = functions_.find(callee->name);
  if (found == functions_.end()) {
    error(callee->range, "C call requires a known function declaration for '" +
                             callee->name + "'");
    return std::nullopt;
  }
  const auto& signature = found->second;
  if (signature.parameters.size() != expression.arguments.size()) {
    error(expression.range, "C call argument count does not match declaration for '" +
                                callee->name + "'");
    return std::nullopt;
  }
  std::vector<std::unique_ptr<ast::Expr>> arguments;
  arguments.reserve(expression.arguments.size());
  for (const auto& argument : expression.arguments) {
    auto lowered = lowerExpr(*argument);
    if (!lowered) {
      return std::nullopt;
    }
    lowered = decayArray(std::move(*lowered), argument->range);
    if (!lowered) {
      return std::nullopt;
    }
    if (isInteger(lowered->type) && !lowered->type.abi.isSigned &&
        dynamic_cast<ast::UnsignedExpr*>(lowered->expression.get()) == nullptr) {
      lowered->expression = std::make_unique<ast::UnsignedExpr>(
          std::move(lowered->expression), lowered->type.abi.byteLength);
    }
    arguments.push_back(std::move(lowered->expression));
  }
  return ExprResult{std::make_unique<ast::CallExpr>(callee->name,
                                                     std::move(arguments)),
                    signature.returnType, false, false, 0};
}

std::optional<ExprResult> Lowerer::lowerMember(const MemberExpr& expression,
                                                bool requireLvalue) {
  auto base = lowerExpr(*expression.base);
  if (!base) {
    return std::nullopt;
  }
  TypeInfo structureType;
  if (expression.throughPointer) {
    base = decayArray(std::move(*base), expression.base->range);
    if (!base || !isPointer(base->type) || base->type.pointee == nullptr ||
        !isAggregate(*base->type.pointee)) {
      error(expression.range, "C '->' member access requires a pointer to struct");
      return std::nullopt;
    }
    structureType = *base->type.pointee;
    if (!ensureStructComplete(structureType, expression.range)) {
      return std::nullopt;
    }
  } else {
    if (base->isArray || !isAggregate(base->type)) {
      error(expression.range, "C '.' member access requires a non-array struct object");
      return std::nullopt;
    }
    structureType = base->type;
  }
  if (!ensureStructComplete(structureType, expression.range)) {
    return std::nullopt;
  }
  const auto found = structs_.find(structureType.abi.aggregateName);
  if (found == structs_.end()) {
    error(expression.range, "C member base has no known struct layout");
    return std::nullopt;
  }
  const auto member = std::find_if(
      found->second.fields.begin(), found->second.fields.end(),
      [&](const FieldInfo& candidate) { return candidate.name == expression.member; });
  if (member == found->second.fields.end()) {
    error(expression.range, "unknown C struct member '" + expression.member + "'");
    return std::nullopt;
  }
  if (expression.throughPointer) {
    std::unique_ptr<ast::Expr> address = std::move(base->expression);
    if (member->offset != 0) {
      address = std::make_unique<ast::BinaryExpr>(
          std::move(address), "%" + std::to_string(options_.pointerByteLength) + "d+",
          std::make_unique<ast::IntegerLiteral>(std::to_string(member->offset)));
    }
    if (member->object.isArray) {
      return ExprResult{std::move(address), member->object.type, false, true,
                        member->object.arrayCount, true};
    }
    return ExprResult{std::make_unique<ast::DerefExpr>(
                          std::to_string(member->object.byteLength()),
                          std::move(address)),
                      member->object.type, requireLvalue, false, 0};
  }
  if (dynamic_cast<ast::IdentifierExpr*>(base->expression.get()) == nullptr) {
    error(expression.range,
          "nested C member access requires a core member-chain lowering extension");
    return std::nullopt;
  }
  auto core = std::make_unique<ast::MemberExpr>(std::move(base->expression),
                                                 expression.member);
  return ExprResult{std::move(core), member->object.type, requireLvalue,
                    member->object.isArray, member->object.arrayCount};
}

} // namespace hitsimple::compat::detail
