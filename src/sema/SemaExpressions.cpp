#include "SemaAnalyzer.h"

#include "hitsimple/literal/Literal.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>

namespace hitsimple::sema {
namespace {

bool isIntegerBinaryOperator(std::string_view op) {
  return integerByteLengthForOperator(op).has_value();
}

bool isFloatBinaryOperator(std::string_view op) {
  return floatByteLengthForOperator(op).has_value();
}

bool isFloatComparisonOperator(std::string_view op) {
  const bool comparison = op.ends_with("==") || op.ends_with("!=") ||
                          op.ends_with("<=") || op.ends_with(">=") ||
                          (op.ends_with("<") && !op.ends_with("<<")) ||
                          (op.ends_with(">") && !op.ends_with(">>"));
  return comparison &&
         (!op.starts_with('%') || op.find('f') != std::string_view::npos);
}

std::optional<std::size_t> floatStandardByteLength(std::size_t byteLength) {
  if (byteLength <= 2) {
    return 2;
  }
  if (byteLength <= 4) {
    return 4;
  }
  if (byteLength <= 8) {
    return 8;
  }
  if (byteLength <= 16) {
    return 16;
  }
  return std::nullopt;
}

bool isLoweredFloatByteLength(std::size_t byteLength) {
  return byteLength == 2 || byteLength == 4 || byteLength == 8 ||
         byteLength == 16;
}

bool isFloatTemplate(std::string_view name) {
  return name == "f16" || name == "f32" || name == "f64" ||
         name == "f128";
}

bool isSupportedFloatMathFunction(stdlib::BuiltinId builtin) {
  using enum stdlib::BuiltinId;
  switch (builtin) {
  case FAbs:
  case FSqrt:
  case FSin:
  case FCos:
  case FTan:
  case FLog:
  case FExp:
  case FFloor:
  case FCeil:
  case FRound:
    return true;
  default:
    return false;
  }
}

bool isSupportedBinaryFloatMathFunction(stdlib::BuiltinId builtin) {
  return builtin == stdlib::BuiltinId::FPow;
}

std::optional<std::size_t> floatConversionByteLength(stdlib::BuiltinId builtin) {
  using enum stdlib::BuiltinId;
  switch (builtin) {
  case ToF16: return 2;
  case ToF32: return 4;
  case ToF64: return 8;
  case ToF128: return 16;
  default: return std::nullopt;
  }
}

struct IntegerConversion final {
  std::size_t byteLength = 0;
  bool isUnsigned = false;
};

std::optional<IntegerConversion>
integerConversion(stdlib::BuiltinId builtin) {
  using enum stdlib::BuiltinId;
  switch (builtin) {
  case ToI8: return IntegerConversion{1, false};
  case ToI16: return IntegerConversion{2, false};
  case ToI32: return IntegerConversion{4, false};
  case ToI64: return IntegerConversion{8, false};
  case ToU8: return IntegerConversion{1, true};
  case ToU16: return IntegerConversion{2, true};
  case ToU32: return IntegerConversion{4, true};
  case ToU64: return IntegerConversion{8, true};
  default: return std::nullopt;
  }
}

bool isStandardConversion(stdlib::BuiltinId builtin) {
  return floatConversionByteLength(builtin).has_value() ||
         integerConversion(builtin).has_value();
}

bool isRelationalOperator(std::string_view op) {
  if (op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" ||
      op == "!=") {
    return true;
  }
  if (op.ends_with("<<") || op.ends_with(">>")) {
    return false;
  }
  return op.starts_with('%') &&
         (op.ends_with("==") || op.ends_with("!=") || op.ends_with("<=") ||
          op.ends_with(">=") || op.ends_with("<") || op.ends_with(">"));
}

bool isLogicalOperator(std::string_view op) { return op == "&&" || op == "||"; }

std::string_view integerOperatorSymbol(std::string_view op) {
  const std::string_view suffixes[] = {"**", "<<", ">>", "==", "!=",
                                       "<=", ">=", "+",  "-",  "*",
                                       "/",  "%",  "&",  "|",  "^",
                                       "<",  ">"};
  for (const auto suffix : suffixes) {
    if (op.ends_with(suffix)) {
      return suffix;
    }
  }
  return op;
}

std::uint64_t signedMaxForByteLength(std::size_t byteLength) {
  if (byteLength >= 8) {
    return static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  }
  return (std::uint64_t{1} << (byteLength * 8U - 1U)) - 1U;
}

std::optional<std::uint64_t> parseDecimalInteger(std::string_view text) {
  return literal::parseUnsignedIntegerLiteral(text);
}

bool isZeroIntegerLiteral(const ast::Expr &expression) {
  const auto *integer = dynamic_cast<const ast::IntegerLiteral *>(&expression);
  if (integer == nullptr) {
    return false;
  }
  const auto value = parseDecimalInteger(integer->value);
  return value && *value == 0;
}

std::optional<std::uint64_t> power(std::uint64_t base, std::uint64_t exponent) {
  std::uint64_t value = 1;
  for (std::uint64_t index = 0; index < exponent; ++index) {
    if (base != 0 &&
        value > std::numeric_limits<std::uint64_t>::max() / base) {
      return std::nullopt;
    }
    value *= base;
  }
  return value;
}

std::optional<std::size_t> integerLiteralArgument(const ast::CallExpr &call,
                                                  std::size_t index) {
  if (index >= call.arguments.size()) {
    return std::nullopt;
  }
  const auto *integer =
      dynamic_cast<const ast::IntegerLiteral *>(call.arguments[index].get());
  if (integer == nullptr) {
    return std::nullopt;
  }
  const auto value = parseDecimalInteger(integer->value);
  if (!value) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(*value);
}

} // namespace

std::unique_ptr<hir::Expr> Analyzer::analyze(const ast::Expr &expression) {
  CurrentRangeGuard rangeGuard(*this, expression);
  if (const auto *identifier =
          dynamic_cast<const ast::IdentifierExpr *>(&expression)) {
    const auto *symbol = lookup(identifier->name);
    if (symbol == nullptr) {
      addDiagnostic("use of undeclared variable '" + identifier->name + "'");
      return nullptr;
    }
    return std::make_unique<hir::VariableRef>(
        symbol->name, symbol->bindingName, symbol->byteLength, symbol->storage);
  }

  if (const auto *integer =
          dynamic_cast<const ast::IntegerLiteral *>(&expression)) {
    const auto byteLength = inferIntegerLiteralByteLength(*integer);
    if (byteLength == 0) {
      addDiagnostic("integer literal '" + integer->value +
                    "' is out of range");
      return nullptr;
    }
    return std::make_unique<hir::IntegerLiteral>(
        integer->value, byteLength);
  }

  if (const auto *character =
          dynamic_cast<const ast::CharLiteral *>(&expression)) {
    const auto decoded = literal::decodeCharLiteral(character->value);
    if (!decoded) {
      addDiagnostic("invalid character literal '" + character->value +
                    "': " + *decoded.error);
      return nullptr;
    }
    const auto value = literal::bytesToUnsignedInteger(decoded.bytes);
    if (!value) {
      addDiagnostic("character literal is too long for integer lowering");
      return nullptr;
    }
    return std::make_unique<hir::IntegerLiteral>(
        std::to_string(*value), decoded.bytes.size());
  }

  if (const auto *boolean =
          dynamic_cast<const ast::BoolLiteral *>(&expression)) {
    return std::make_unique<hir::IntegerLiteral>(boolean->value ? "1" : "0", 1);
  }

  if (const auto *binary = dynamic_cast<const ast::BinaryExpr *>(&expression)) {
    return analyze(*binary);
  }

  if (const auto *unary = dynamic_cast<const ast::UnaryExpr *>(&expression)) {
    return analyze(*unary);
  }

  if (const auto *ternary =
          dynamic_cast<const ast::TernaryExpr *>(&expression)) {
    return analyze(*ternary);
  }

  if (const auto *unsignedExpr =
          dynamic_cast<const ast::UnsignedExpr *>(&expression)) {
    return analyze(*unsignedExpr);
  }

  if (const auto *cast =
          dynamic_cast<const ast::IntegerCastExpr *>(&expression)) {
    return analyze(*cast);
  }

  if (const auto *asExpr = dynamic_cast<const ast::AsExpr *>(&expression)) {
    const auto byteLength = inferByteLength(*asExpr->operand);
    if (!byteLength || *byteLength == 0) {
      addDiagnostic("expression template view requires a fixed positive byte length");
      return nullptr;
    }
    if (asExpr->templateName != "none" && asExpr->templateName != "bytes" &&
        asExpr->templateName != "cstr") {
      const auto templateLength = templateByteLength(asExpr->templateName);
      if (!templateLength) {
        addDiagnostic("unknown template '" + asExpr->templateName + "'");
        return nullptr;
      }
      if (*templateLength != *byteLength) {
        addDiagnostic("expression template view byte length does not match template '" +
                      asExpr->templateName + "'");
        return nullptr;
      }
    }

    auto operand = analyze(*asExpr->operand);
    if (!operand) {
      return nullptr;
    }
    const bool isAddressable =
        dynamic_cast<const hir::VariableRef *>(operand.get()) != nullptr ||
        dynamic_cast<const hir::DerefExpr *>(operand.get()) != nullptr ||
        dynamic_cast<const hir::TemplateViewExpr *>(operand.get()) != nullptr;
    return std::make_unique<hir::TemplateViewExpr>(
        std::move(operand), *byteLength, asExpr->templateName, isAddressable);
  }

  if (const auto *string =
          dynamic_cast<const ast::StringLiteral *>(&expression)) {
    const auto decoded = literal::decodeStringLiteral(string->value);
    if (!decoded) {
      addDiagnostic("invalid string literal '" + string->value +
                    "': " + *decoded.error);
      return nullptr;
    }
    return std::make_unique<hir::StringLiteral>(
        string->value, decoded.bytes.size() + 1);
  }

  if (const auto *floating =
          dynamic_cast<const ast::FloatLiteral *>(&expression)) {
    return std::make_unique<hir::FloatLiteral>(floating->value, 8);
  }

  if (const auto *sizeofExpr =
          dynamic_cast<const ast::SizeofExpr *>(&expression)) {
    const auto structIt = structs_.find(sizeofExpr->name);
    if (structIt != structs_.end()) {
      return std::make_unique<hir::IntegerLiteral>(
          std::to_string(structIt->second.byteLength), 8);
    }
    const auto *symbol = lookup(sizeofExpr->name);
    if (symbol == nullptr) {
      addDiagnostic("sizeof unknown name '" + sizeofExpr->name + "'");
      return nullptr;
    }
    return std::make_unique<hir::IntegerLiteral>(
        std::to_string(symbol->byteLength), 8);
  }

  if (const auto *call = dynamic_cast<const ast::CallExpr *>(&expression)) {
    if (stdlib::isRemovedLegacyName(call->callee)) {
      addDiagnostic("legacy standard library name '" + call->callee +
                    "' is not accepted; use " +
                    std::string(stdlib::replacementForRemovedLegacyName(
                        call->callee)));
      return nullptr;
    }
    if (rejectUnavailableStandardBuiltin(*call)) {
      return nullptr;
    }
    const auto builtin = builtinForCall(*call);
    if (const auto found = functions_.find(call->callee);
        found != functions_.end() &&
        !validateHandleCallArguments(*call, found->second)) {
      return nullptr;
    }
    if (builtin == stdlib::BuiltinId::Length ||
        builtin == stdlib::BuiltinId::ResizeBytes ||
        builtin == stdlib::BuiltinId::ByteSwap ||
        (builtin && isStandardConversion(*builtin))) {
      for (const auto &argument : call->arguments) {
        if (isHandleExpression(*argument)) {
          addDiagnostic("handle values cannot be used with '" + call->callee +
                        "'");
          return nullptr;
        }
      }
    }
    if (builtin == stdlib::BuiltinId::Print ||
        builtin == stdlib::BuiltinId::Printf ||
        builtin == stdlib::BuiltinId::Fprintf) {
      if (builtin == stdlib::BuiltinId::Print && call->arguments.size() == 1U) {
        if (const auto *as =
                dynamic_cast<const ast::AsExpr *>(call->arguments[0].get());
            as != nullptr && as->templateName == "none") {
          auto value = analyze(*as->operand);
          if (!value) {
            return nullptr;
          }
          std::vector<std::unique_ptr<hir::Expr>> arguments;
          arguments.push_back(std::move(value));
          return std::make_unique<hir::CallExpr>(
              "put", std::move(arguments), 4, false, stdlib::BuiltinId::Put);
        }
      }
      const std::size_t formatIndex =
          builtin == stdlib::BuiltinId::Fprintf ? 1U : 0U;
      if (call->arguments.size() <= formatIndex) {
        addDiagnostic(call->callee + " expects a format argument");
        return nullptr;
      }
      if (builtin == stdlib::BuiltinId::Fprintf &&
          !isHandleExpression(*call->arguments.front())) {
        addDiagnostic("fprintf file argument must be a handle");
        return nullptr;
      }
      if (call->arguments.size() == formatIndex + 1U) {
        if (const auto templateName =
                expressionTemplateName(*call->arguments[formatIndex])) {
          auto lowered = lowerUserTemplateFormatCall(*call, *builtin,
                                                     *templateName, formatIndex);
          if (!lowered) {
            return nullptr;
          }
          return std::make_unique<hir::UserTemplateFormatCallExpr>(
              std::move(lowered->callee), std::move(lowered->value),
              lowered->sink, std::move(lowered->file),
              lowered->resultByteLength);
        }
      }
      if (expressionTemplateName(*call->arguments[formatIndex])) {
        addDiagnostic("user template formatting requires a direct print(value), "
                      "printf(value), or fprintf(file, value) call");
        return nullptr;
      }
      for (std::size_t index = formatIndex + 1U;
           index < call->arguments.size(); ++index) {
        if (expressionTemplateName(*call->arguments[index])) {
          addDiagnostic("user template formatting requires a direct print(value), "
                        "printf(value), or fprintf(file, value) call");
          return nullptr;
        }
      }
      std::vector<std::unique_ptr<hir::Expr>> arguments;
      std::vector<hir::FormatArgKind> formatArgumentKinds;
      for (const auto &argument : call->arguments) {
        auto lowered = analyze(*argument);
        if (!lowered) {
          return nullptr;
        }
        formatArgumentKinds.push_back(formatArgumentKind(*argument, *lowered));
        arguments.push_back(std::move(lowered));
      }
      return std::make_unique<hir::CallExpr>(call->callee, std::move(arguments),
                                             4, false, *builtin,
                                             std::move(formatArgumentKinds));
    }
    if (builtin == stdlib::BuiltinId::Length) {
      if (call->arguments.size() != 1U) {
        addDiagnostic("length expects 1 argument");
        return nullptr;
      }
      const auto length = inferByteLength(*call->arguments[0]);
      if (!length) {
        addDiagnostic("length argument has no known byte length");
        return nullptr;
      }
      return std::make_unique<hir::IntegerLiteral>(std::to_string(*length),
                                                   pointerByteLength());
    }
    if (builtin == stdlib::BuiltinId::Calloc &&
        call->arguments.size() == 2U) {
      std::vector<std::unique_ptr<hir::Expr>> arguments;
      for (const auto &argument : call->arguments) {
        auto lowered = analyze(*argument);
        if (!lowered) {
          return nullptr;
        }
        if (!isIntegerExpression(*lowered) ||
            integerExpressionByteLength(*lowered).value_or(0) >
                pointerByteLength()) {
          addDiagnostic("calloc arguments must be pointer-sized integers");
          return nullptr;
        }
        arguments.push_back(std::move(lowered));
      }
      return std::make_unique<hir::CallExpr>("calloc", std::move(arguments),
                                             pointerByteLength(), false,
                                             stdlib::BuiltinId::Calloc);
    }
    if (builtin == stdlib::BuiltinId::ResizeBytes) {
      if (call->arguments.size() != 2U) {
        addDiagnostic(call->callee + " expects 2 arguments");
        return nullptr;
      }
      const auto resultLength = integerLiteralArgument(*call, 1);
      auto operand = analyze(*call->arguments[0]);
      if (!operand) {
        return nullptr;
      }
      auto length = analyze(*call->arguments[1]);
      if (!length) {
        return nullptr;
      }
      if (!isIntegerExpression(*length) ||
          integerExpressionByteLength(*length).value_or(0) >
              pointerByteLength()) {
        addDiagnostic(call->callee + " length must be a pointer-sized integer");
        return nullptr;
      }
      if (resultLength && (*resultLength == 1 || *resultLength == 2 ||
                           *resultLength == 4 || *resultLength == 8)) {
        std::vector<std::unique_ptr<hir::Expr>> arguments;
        arguments.push_back(std::move(operand));
        arguments.push_back(std::move(length));
        return std::make_unique<hir::CallExpr>(
            call->callee, std::move(arguments), *resultLength, false,
            stdlib::BuiltinId::ResizeBytes);
      }
      return std::make_unique<hir::DynamicByteViewExpr>(
          hir::DynamicByteViewOperation::ResizeBytes, std::move(operand),
          std::move(length));
    }
    if (builtin == stdlib::BuiltinId::ByteSwap) {
      if (call->arguments.size() != 1U) {
        addDiagnostic("byte_swap expects 1 argument");
        return nullptr;
      }
      auto operand = analyze(*call->arguments[0]);
      if (!operand) {
        return nullptr;
      }
      const auto length = integerExpressionByteLength(*operand);
      if (!length) {
        return std::make_unique<hir::DynamicByteViewExpr>(
            hir::DynamicByteViewOperation::ByteSwap, std::move(operand),
            nullptr);
      }
      if (*length == 1 || *length == 2 || *length == 4 || *length == 8) {
        std::vector<std::unique_ptr<hir::Expr>> arguments;
        arguments.push_back(std::move(operand));
        return std::make_unique<hir::CallExpr>("byte_swap", std::move(arguments),
                                               *length, false,
                                               stdlib::BuiltinId::ByteSwap);
      }
      return std::make_unique<hir::ByteSwapExpr>(std::move(operand), *length);
    }
    if (builtin == stdlib::BuiltinId::Abs) {
      if (call->arguments.size() != 1U) {
        addDiagnostic("abs expects 1 argument");
        return nullptr;
      }
      auto operand = analyze(*call->arguments[0]);
      if (!operand) {
        return nullptr;
      }
      if (!isIntegerExpression(*operand)) {
        addDiagnostic("abs data is not an integer expression");
        return nullptr;
      }
      const auto length = integerExpressionByteLength(*operand).value_or(4);
      std::vector<std::unique_ptr<hir::Expr>> arguments;
      arguments.push_back(std::move(operand));
      return std::make_unique<hir::CallExpr>("abs", std::move(arguments),
                                             length, false,
                                             stdlib::BuiltinId::Abs);
    }
    if (builtin == stdlib::BuiltinId::Min || builtin == stdlib::BuiltinId::Max) {
      if (call->arguments.size() != 2U) {
        addDiagnostic(call->callee + " expects 2 arguments");
        return nullptr;
      }
      const auto leftLength = inferByteLength(*call->arguments[0]);
      const auto rightLength = inferByteLength(*call->arguments[1]);
      if (!leftLength || !rightLength || *leftLength != *rightLength) {
        addDiagnostic(call->callee +
                      " arguments must have the same byte length");
        return nullptr;
      }
      const auto isFloatingArgument = [this](const ast::Expr &argument) {
        if (dynamic_cast<const ast::FloatLiteral *>(&argument) != nullptr) {
          return true;
        }
        const auto *identifier = dynamic_cast<const ast::IdentifierExpr *>(&argument);
        if (identifier == nullptr) {
          return false;
        }
        const auto *symbol = lookup(identifier->name);
        return symbol != nullptr && isFloatTemplate(symbol->templateName);
      };
      const bool leftIsFloating = isFloatingArgument(*call->arguments[0]);
      const bool rightIsFloating = isFloatingArgument(*call->arguments[1]);
      if (leftIsFloating != rightIsFloating) {
        addDiagnostic(call->callee +
                      " arguments must use the same numeric interpretation");
        return nullptr;
      }
      if (leftIsFloating) {
        auto left = analyzeFloatOperand(*call->arguments[0], *leftLength);
        auto right = analyzeFloatOperand(*call->arguments[1], *rightLength);
        if (!left || !right) {
          return nullptr;
        }
        std::vector<std::unique_ptr<hir::Expr>> arguments;
        arguments.push_back(std::move(left));
        arguments.push_back(std::move(right));
        return std::make_unique<hir::CallExpr>(call->callee, std::move(arguments),
                                               *leftLength, true, *builtin);
      }
      auto left = analyze(*call->arguments[0]);
      auto right = analyze(*call->arguments[1]);
      if (!left || !right) {
        return nullptr;
      }
      if (!isIntegerExpression(*left) || !isIntegerExpression(*right)) {
        addDiagnostic(call->callee + " arguments must be numeric expressions");
        return nullptr;
      }
      std::vector<std::unique_ptr<hir::Expr>> arguments;
      arguments.push_back(std::move(left));
      arguments.push_back(std::move(right));
      return std::make_unique<hir::CallExpr>(
          call->callee, std::move(arguments), *leftLength, false, *builtin);
    }
    if (builtin && floatConversionByteLength(*builtin)) {
      if (call->arguments.size() != 1U) {
        addDiagnostic(call->callee + " expects 1 argument");
        return nullptr;
      }
      const auto targetLength = *floatConversionByteLength(*builtin);
      const auto sourceLength = inferByteLength(*call->arguments[0]);
      if (!sourceLength) {
        addDiagnostic(call->callee + " argument has no known byte length");
        return nullptr;
      }
      bool sourceIsFloating =
          dynamic_cast<const ast::FloatLiteral *>(call->arguments[0].get()) !=
          nullptr;
      if (const auto *identifier = dynamic_cast<const ast::IdentifierExpr *>(
              call->arguments[0].get())) {
        if (const auto *symbol = lookup(identifier->name)) {
          sourceIsFloating = isFloatTemplate(symbol->templateName);
        }
      }
      std::unique_ptr<hir::Expr> operand = sourceIsFloating
                                               ? analyzeFloatOperand(*call->arguments[0], *sourceLength)
                                               : analyze(*call->arguments[0]);
      if (!operand) {
        return nullptr;
      }
      if (!sourceIsFloating && !isIntegerExpression(*operand)) {
        addDiagnostic(call->callee + " data must be an integer or floating expression");
        return nullptr;
      }
      const bool sourceUnsigned =
          !sourceIsFloating && isUnsignedExpression(*operand);
      return std::make_unique<hir::ToFloatExpr>(
          std::move(operand), targetLength, sourceUnsigned, sourceIsFloating);
    }
    if (builtin && integerConversion(*builtin)) {
      if (call->arguments.size() != 1U) {
        addDiagnostic(call->callee + " expects 1 argument");
        return nullptr;
      }
      const auto conversion = *integerConversion(*builtin);
      const auto sourceLength = inferByteLength(*call->arguments[0]);
      if (!sourceLength) {
        addDiagnostic(call->callee + " argument has no known byte length");
        return nullptr;
      }
      bool sourceIsFloating =
          dynamic_cast<const ast::FloatLiteral *>(call->arguments[0].get()) !=
          nullptr;
      if (const auto *identifier = dynamic_cast<const ast::IdentifierExpr *>(
              call->arguments[0].get())) {
        if (const auto *symbol = lookup(identifier->name)) {
          sourceIsFloating = isFloatTemplate(symbol->templateName);
        }
      }
      if (sourceIsFloating) {
        if (!isLoweredFloatByteLength(*sourceLength)) {
          addDiagnostic(call->callee +
                        " requires an f16, f32, f64, or f128 input");
          return nullptr;
        }
        auto operand = analyzeFloatOperand(*call->arguments[0], *sourceLength);
        if (!operand) {
          return nullptr;
        }
        return std::make_unique<hir::ToIntExpr>(
            std::move(operand), *sourceLength, conversion.byteLength,
            conversion.isUnsigned);
      }
      auto operand = analyze(*call->arguments[0]);
      if (!operand) {
        return nullptr;
      }
      if (!isIntegerExpression(*operand)) {
        addDiagnostic(call->callee +
                      " requires an integer, floating, or address input");
        return nullptr;
      }
      return std::make_unique<hir::IntegerCastExpr>(
          std::move(operand), conversion.byteLength, !conversion.isUnsigned);
    }
    if (builtin && isSupportedFloatMathFunction(*builtin)) {
      if (call->arguments.size() != 1U) {
        addDiagnostic(call->callee + " expects 1 argument");
        return nullptr;
      }
      const auto length = inferByteLength(*call->arguments[0]);
      if (!length || !isLoweredFloatByteLength(*length)) {
        addDiagnostic(call->callee + " requires an f16, f32, f64, or f128 input");
        return nullptr;
      }
      auto operand = analyzeFloatOperand(*call->arguments[0], *length);
      if (!operand) {
        return nullptr;
      }
      std::vector<std::unique_ptr<hir::Expr>> arguments;
      arguments.push_back(std::move(operand));
      return std::make_unique<hir::CallExpr>(call->callee,
                                             std::move(arguments), *length,
                                             true, *builtin);
    }
    if (builtin && isSupportedBinaryFloatMathFunction(*builtin)) {
      if (call->arguments.size() != 2U) {
        addDiagnostic(call->callee + " expects 2 arguments");
        return nullptr;
      }
      const auto length = inferByteLength(*call->arguments[0]);
      if (!length || !isLoweredFloatByteLength(*length)) {
        addDiagnostic(call->callee + " requires an f16, f32, f64, or f128 input");
        return nullptr;
      }
      auto base = analyzeFloatOperand(*call->arguments[0], *length);
      auto exponent = analyzeFloatOperand(*call->arguments[1], *length);
      if (!base || !exponent) {
        return nullptr;
      }
      std::vector<std::unique_ptr<hir::Expr>> arguments;
      arguments.push_back(std::move(base));
      arguments.push_back(std::move(exponent));
      return std::make_unique<hir::CallExpr>(call->callee,
                                             std::move(arguments), *length,
                                             true, *builtin);
    }
    return analyzeCallExpr(*call);
  }

  if (const auto *call = dynamic_cast<const ast::MethodCallExpr *>(&expression)) {
    return analyzeMethodCallExpr(*call);
  }

  if (const auto *assignment =
          dynamic_cast<const ast::AssignmentExpr *>(&expression)) {
    return analyze(*assignment);
  }

  if (const auto reference = resolveMemoryReference(expression)) {
    return std::make_unique<hir::VariableRef>(
        reference->name, reference->bindingName, reference->byteLength,
        reference->storage, reference->offset);
  }

  if (const auto *deref = dynamic_cast<const ast::DerefExpr *>(&expression)) {
    if (isHandleExpression(*deref->address)) {
      addDiagnostic("handle values cannot be dereferenced");
      return nullptr;
    }
    const auto length = parseByteLength(deref->length);
    if (length == 0) {
      addDiagnostic("invalid dereference byte length");
      return nullptr;
    }
    auto address = analyze(*deref->address);
    if (!address) {
      return nullptr;
    }
    if (!isIntegerExpression(*address)) {
      addDiagnostic("dereference address is not an integer expression");
      return nullptr;
    }
    if (const auto *literalAddress =
            dynamic_cast<const hir::IntegerLiteral *>(address.get())) {
      if (literal::parseUnsignedIntegerLiteral(literalAddress->value) == 0) {
        addDiagnostic("null address dereference is not allowed");
        return nullptr;
      }
    }
    if (integerExpressionByteLength(*address).value_or(0) > pointerByteLength()) {
      addDiagnostic("dereference address is wider than pointer length");
      return nullptr;
    }
    return std::make_unique<hir::DerefExpr>(std::move(address), length);
  }

  if (const auto *index = dynamic_cast<const ast::IndexExpr *>(&expression)) {
    auto address = lowerIndexAddress(*index);
    if (!address) {
      return nullptr;
    }
    return std::make_unique<hir::DerefExpr>(std::move(address), 1);
  }

  if (const auto *slice = dynamic_cast<const ast::SliceExpr *>(&expression)) {
    auto lowered = lowerSlice(*slice);
    if (!lowered) {
      return nullptr;
    }
    return std::make_unique<hir::DerefExpr>(std::move(lowered->address),
                                            lowered->byteLength);
  }

  addDiagnostic("unsupported expression");
  return nullptr;
}

std::unique_ptr<hir::Expr>
Analyzer::analyze(const ast::AssignmentExpr &expression) {
  auto lowered = lowerAssignmentExpression(expression);
  if (!lowered) {
    return nullptr;
  }
  return std::make_unique<hir::AssignmentExpr>(std::move(lowered->stores),
                                               std::move(lowered->result),
                                               lowered->byteLength);
}

std::unique_ptr<hir::Expr> Analyzer::analyzeCallExpr(const ast::CallExpr &call) {
  const auto found = functions_.find(call.callee);
  if (found == functions_.end()) {
    if (isUnsupportedStandardFunction(call.callee)) {
      addDiagnostic("standard library function '" + call.callee +
                    "' is not supported yet");
      return nullptr;
    }
    addDiagnostic("unsupported function call '" + call.callee + "'");
    return nullptr;
  }
  const auto &signature = found->second;
  if (!signature.returnsKnown) {
    addDiagnostic("function call '" + call.callee +
                  "' requires a known return signature");
    return nullptr;
  }
  if (signature.returnByteLengths.empty()) {
    addDiagnostic("function call '" + call.callee + "' does not return a value");
    return nullptr;
  }
  if (signature.returnByteLengths.size() != 1U) {
    addDiagnostic("multi-return function call '" + call.callee +
                  "' cannot be used as a single expression");
    return nullptr;
  }
  auto arguments = analyzeCallArguments(call, signature);
  if (!result_.diagnostics.empty()) {
    return nullptr;
  }
  const bool isFloating =
      !signature.returnTemplateNames.empty() &&
      isFloatTemplate(signature.returnTemplateNames.front());
  return std::make_unique<hir::CallExpr>(call.callee, std::move(arguments),
                                         signature.returnByteLengths.front(),
                                         isFloating, signature.builtin,
                                         std::vector<hir::FormatArgKind>{},
                                         signature.returnTemplateNames.empty()
                                             ? std::string{}
                                             : signature.returnTemplateNames.front());
}

std::optional<MethodCallLowering>
Analyzer::lowerImplMethodCall(const ast::MethodCallExpr &call) {
  const auto receiverTemplate = expressionTemplateName(*call.receiver);
  if (!receiverTemplate) {
    addDiagnostic("method receiver does not have a user template");
    return std::nullopt;
  }

  std::vector<std::string> parameterTemplateNames;
  parameterTemplateNames.reserve(call.arguments.size() + 1U);
  parameterTemplateNames.push_back(*receiverTemplate);
  for (const auto &argument : call.arguments) {
    const auto argumentTemplate = operatorTemplateName(*argument);
    if (!argumentTemplate) {
      addDiagnostic("method argument requires an explicit template");
      return std::nullopt;
    }
    parameterTemplateNames.push_back(*argumentTemplate);
  }

  const auto *method =
      findImplMethod(*receiverTemplate, call.method, parameterTemplateNames);
  if (method == nullptr) {
    addDiagnostic("no matching impl method '" + call.method + "' for template '" +
                  *receiverTemplate + "'");
    return std::nullopt;
  }

  std::vector<std::unique_ptr<hir::Expr>> arguments;
  arguments.reserve(call.arguments.size() + 1U);
  const auto lowerArgument = [this, &arguments](
                                 const ast::Expr &argument,
                                 std::string_view templateName,
                                 std::size_t expectedLength) -> bool {
    const auto actualLength = inferByteLength(argument);
    if (!actualLength || *actualLength != expectedLength) {
      addDiagnostic("method argument byte length does not match template '" +
                    std::string(templateName) + "'");
      return false;
    }
    auto lowered = isFloatTemplate(templateName)
                       ? analyzeFloatOperand(argument, expectedLength)
                       : analyze(argument);
    if (!lowered) {
      return false;
    }
    if (hasRuntimeDynamicView(*lowered)) {
      addDiagnostic("dynamic View cannot be passed to a fixed impl method parameter");
      return false;
    }
    if (!isIntegerExpression(*lowered) &&
        !isFloatTemplate(templateName)) {
      addDiagnostic("method argument is not a fixed View value");
      return false;
    }
    arguments.push_back(std::move(lowered));
    return true;
  };

  if (!lowerArgument(*call.receiver, method->parameterTemplateNames.front(),
                     method->parameterByteLengths.front())) {
    return std::nullopt;
  }
  for (std::size_t index = 0; index < call.arguments.size(); ++index) {
    if (!lowerArgument(*call.arguments[index],
                       method->parameterTemplateNames[index + 1U],
                       method->parameterByteLengths[index + 1U])) {
      return std::nullopt;
    }
  }

  return MethodCallLowering{method, std::move(arguments)};
}

std::unique_ptr<hir::Expr>
Analyzer::analyzeMethodCallExpr(const ast::MethodCallExpr &call) {
  auto lowered = lowerImplMethodCall(call);
  if (!lowered) {
    return nullptr;
  }
  if (lowered->method->returnByteLengths.empty()) {
    addDiagnostic("impl method '" + call.method + "' does not return a value");
    return nullptr;
  }
  return std::make_unique<hir::UserTemplateOpCallExpr>(
      lowered->method->symbolName, std::move(lowered->arguments),
      lowered->method->returnByteLengths.front(),
      lowered->method->returnTemplateNames.front());
}

std::unique_ptr<hir::Expr>
Analyzer::analyze(const ast::BinaryExpr &expression) {
  const bool leftIsHandle = isHandleExpression(*expression.left);
  const bool rightIsHandle = isHandleExpression(*expression.right);
  if (leftIsHandle || rightIsHandle) {
    const bool comparesTwoHandles = leftIsHandle && rightIsHandle;
    const bool comparesHandleWithNull =
        (leftIsHandle && isZeroIntegerLiteral(*expression.right)) ||
        (rightIsHandle && isZeroIntegerLiteral(*expression.left));
    if ((expression.op != "==" && expression.op != "!=") ||
        (!comparesTwoHandles && !comparesHandleWithNull)) {
      addDiagnostic("handle values only support == and != comparisons");
      return nullptr;
    }
  }

  const auto leftTemplate = operatorTemplateName(*expression.left);
  const auto rightTemplate = operatorTemplateName(*expression.right);
  if (leftTemplate || rightTemplate) {
    const bool usesUserTemplate =
        (leftTemplate && templates_.contains(*leftTemplate)) ||
        (rightTemplate && templates_.contains(*rightTemplate));
    if (leftTemplate && rightTemplate) {
      const std::string key =
          expression.op + "|" + *leftTemplate + "|" + *rightTemplate;
      if (const auto found = implOpIndexes_.find(key);
          found != implOpIndexes_.end()) {
        const auto &info = implOpInfos_[found->second];
        auto left = analyze(*expression.left);
        auto right = analyze(*expression.right);
        if (!left || !right || info.returnByteLengths.size() != 1U ||
            info.returnTemplateNames.size() != 1U) {
          return nullptr;
        }
        std::vector<std::unique_ptr<hir::Expr>> arguments;
        arguments.push_back(std::move(left));
        arguments.push_back(std::move(right));
        return std::make_unique<hir::UserTemplateOpCallExpr>(
            info.symbolName, std::move(arguments),
            info.returnByteLengths.front(), info.returnTemplateNames.front());
      }
    }
    if (usesUserTemplate) {
      addDiagnostic(leftTemplate && rightTemplate
                        ? "user template binary operator requires a matching impl op"
                        : "user template binary operator requires both operands to use templates");
      return nullptr;
    }
  }

  if (isFloatComparisonOperator(expression.op) &&
      !expression.op.starts_with('%')) {
    auto leftProbe = analyze(*expression.left);
    auto rightProbe = analyze(*expression.right);
    if (!leftProbe || !rightProbe) {
      return nullptr;
    }
    const auto hasFloatInterpretation = [this](const ast::Expr& operand) {
      if (dynamic_cast<const ast::FloatLiteral*>(&operand) != nullptr) {
        return true;
      }
      const auto templateName = operatorTemplateName(operand);
      return templateName && isFloatTemplate(*templateName);
    };
    const bool leftFloating = hasFloatInterpretation(*expression.left);
    const bool rightFloating = hasFloatInterpretation(*expression.right);
    if (leftFloating || rightFloating) {
      if (!leftFloating || !rightFloating) {
        addDiagnostic("both operands of float comparison '" + expression.op +
                      "' must be floating expressions");
        return nullptr;
      }
      const auto leftLength = floatExpressionByteLength(*leftProbe).value_or(8);
      const auto rightLength =
          floatExpressionByteLength(*rightProbe).value_or(8);
      const auto inferred =
          floatStandardByteLength(std::max(leftLength, rightLength));
      if (!inferred) {
        addDiagnostic("float comparison '" + expression.op +
                      "' cannot infer a supported byte length");
        return nullptr;
      }
      auto left = analyzeFloatOperand(*expression.left, *inferred);
      auto right = analyzeFloatOperand(*expression.right, *inferred);
      if (!left || !right) {
        return nullptr;
      }
      return std::make_unique<hir::FloatCompareExpr>(
          std::move(left), expression.op, std::move(right), *inferred);
    }
  }

  if (isFloatBinaryOperator(expression.op)) {
    const auto typedLength = floatByteLengthForOperator(expression.op);
    if (!typedLength) {
      addDiagnostic("unsupported float operator '" + expression.op + "'");
      return nullptr;
    }

    auto leftProbe = analyze(*expression.left);
    if (!leftProbe) {
      return nullptr;
    }
    if (!isFloatExpression(*leftProbe)) {
      addDiagnostic("left operand of '" + expression.op +
                    "' is not a float expression");
      return nullptr;
    }

    auto rightProbe = analyze(*expression.right);
    if (!rightProbe) {
      return nullptr;
    }
    if (!isFloatExpression(*rightProbe)) {
      addDiagnostic("right operand of '" + expression.op +
                    "' is not a float expression");
      return nullptr;
    }

    std::size_t byteLength = *typedLength;
    if (byteLength == 0) {
      const auto leftLength = floatExpressionByteLength(*leftProbe).value_or(8);
      const auto rightLength =
          floatExpressionByteLength(*rightProbe).value_or(8);
      const auto inferred =
          floatStandardByteLength(std::max(leftLength, rightLength));
      if (!inferred) {
        addDiagnostic("float operator '" + expression.op +
                      "' cannot infer a supported byte length");
        return nullptr;
      }
      byteLength = *inferred;
    }
    if (!isLoweredFloatByteLength(byteLength)) {
      addDiagnostic("float byte length " + std::to_string(byteLength) +
                    " is not supported yet");
      return nullptr;
    }

    auto left = analyzeFloatOperand(*expression.left, byteLength);
    if (!left) {
      return nullptr;
    }
    auto right = analyzeFloatOperand(*expression.right, byteLength);
    if (!right) {
      return nullptr;
    }
    if (isFloatComparisonOperator(expression.op)) {
      return std::make_unique<hir::FloatCompareExpr>(
          std::move(left), expression.op, std::move(right), byteLength);
    }
    return std::make_unique<hir::FloatBinaryExpr>(
        std::move(left), expression.op, std::move(right), byteLength);
  }

  if (!isIntegerBinaryOperator(expression.op)) {
    addDiagnostic("unsupported binary operator '" + expression.op + "'");
    return nullptr;
  }

  if (isDivisionOperator(expression.op)) {
    if (const auto *integer =
            dynamic_cast<const ast::IntegerLiteral *>(expression.right.get())) {
      if (parseDecimalInteger(integer->value) == 0) {
        addDiagnostic("division by zero in binary expression");
        return nullptr;
      }
    }
  }

  auto left = analyze(*expression.left);
  if (!left) {
    return nullptr;
  }

  auto right = analyze(*expression.right);
  if (!right) {
    return nullptr;
  }

  if (!isIntegerExpression(*left)) {
    addDiagnostic("left operand of '" + expression.op +
                  "' is not an integer expression");
    return nullptr;
  }
  if (!isIntegerExpression(*right)) {
    addDiagnostic("right operand of '" + expression.op +
                  "' is not an integer expression");
    return nullptr;
  }

  const bool fixedWidthOp = expression.op.starts_with('%');
  const bool booleanOp =
      isRelationalOperator(expression.op) || isLogicalOperator(expression.op);
  std::size_t byteLength = 1;
  if (!booleanOp) {
    const auto typedLength = integerByteLengthForOperator(expression.op);
    if (fixedWidthOp) {
      byteLength = *typedLength;
    } else {
      const auto leftLength = integerExpressionByteLength(*left).value_or(4);
      const auto rightLength = integerExpressionByteLength(*right).value_or(4);
      byteLength = std::max({std::size_t{4}, leftLength, rightLength});
    }
  }

  if (fixedWidthOp) {
    const auto *leftLiteral = dynamic_cast<const hir::IntegerLiteral *>(left.get());
    const auto *rightLiteral =
        dynamic_cast<const hir::IntegerLiteral *>(right.get());
    if (leftLiteral != nullptr && rightLiteral != nullptr) {
      const auto leftValue =
          literal::parseUnsignedIntegerLiteral(leftLiteral->value);
      const auto rightValue =
          literal::parseUnsignedIntegerLiteral(rightLiteral->value);
      if (!leftValue || !rightValue) {
        addDiagnostic("invalid integer literal in constant expression");
        return nullptr;
      }

      const auto op = integerOperatorSymbol(expression.op);
      std::optional<std::uint64_t> folded;
      if (op == "+") {
        if (*leftValue <= std::numeric_limits<std::uint64_t>::max() -
                              *rightValue) {
          folded = *leftValue + *rightValue;
        }
      } else if (op == "-") {
        if (*leftValue >= *rightValue) {
          folded = *leftValue - *rightValue;
        }
      } else if (op == "*") {
        if (*rightValue == 0 ||
            *leftValue <= std::numeric_limits<std::uint64_t>::max() /
                              *rightValue) {
          folded = *leftValue * *rightValue;
        }
      } else if (op == "/") {
        folded = *leftValue / *rightValue;
      } else if (op == "%") {
        folded = *leftValue % *rightValue;
      } else if (op == "**") {
        folded = power(*leftValue, *rightValue);
      } else if (op == "<<") {
        if (*rightValue < 64U) {
          folded = *leftValue << *rightValue;
        }
      } else if (op == ">>") {
        if (*rightValue < 64U) {
          folded = *leftValue >> *rightValue;
        }
      } else if (op == "&") {
        folded = *leftValue & *rightValue;
      } else if (op == "|") {
        folded = *leftValue | *rightValue;
      } else if (op == "^") {
        folded = *leftValue ^ *rightValue;
      }

      if (!folded || *folded > signedMaxForByteLength(byteLength)) {
        addDiagnostic("constant expression overflows " +
                      std::to_string(byteLength) + "-byte integer");
        return nullptr;
      }
      return std::make_unique<hir::IntegerLiteral>(std::to_string(*folded),
                                                   byteLength);
    }
  }

  return std::make_unique<hir::BinaryExpr>(std::move(left), expression.op,
                                           std::move(right), byteLength);
}

std::unique_ptr<hir::Expr> Analyzer::analyze(const ast::UnaryExpr &expression) {
  if (isHandleExpression(*expression.operand)) {
    addDiagnostic("handle values do not support unary operator '" +
                  expression.op + "'");
    return nullptr;
  }
  if (expression.op == "&") {
    const auto reference = resolveAddressableReference(*expression.operand);
    if (!reference) {
      return nullptr;
    }
    return std::make_unique<hir::AddressOfExpr>(
        reference->name, reference->bindingName, reference->byteLength,
        reference->storage, reference->offset, pointerByteLength());
  }
  if (expression.op == "++" || expression.op == "--" ||
      expression.op == "post++" || expression.op == "post--") {
    addDiagnostic("increment expressions are not supported yet");
    return nullptr;
  }
  if (expression.op != "!" && expression.op != "~" && expression.op != "-") {
    addDiagnostic("unsupported unary operator '" + expression.op + "'");
    return nullptr;
  }

  auto operand = analyze(*expression.operand);
  if (!operand) {
    return nullptr;
  }
  if (!isIntegerExpression(*operand)) {
    addDiagnostic("operand of '" + expression.op +
                  "' is not an integer expression");
    return nullptr;
  }

  std::size_t byteLength = 1;
  if (expression.op != "!") {
    byteLength = std::max(std::size_t{4},
                          integerExpressionByteLength(*operand).value_or(4));
  }
  return std::make_unique<hir::UnaryExpr>(expression.op, std::move(operand),
                                          byteLength);
}

std::unique_ptr<hir::Expr>
Analyzer::analyze(const ast::TernaryExpr &expression) {
  if (isHandleExpression(*expression.condition) ||
      isHandleExpression(*expression.thenExpr) ||
      isHandleExpression(*expression.elseExpr)) {
    addDiagnostic("handle values cannot be used in ternary expressions");
    return nullptr;
  }
  auto condition = analyze(*expression.condition);
  if (!condition) {
    return nullptr;
  }
  if (!isIntegerExpression(*condition)) {
    addDiagnostic("ternary condition is not an integer expression");
    return nullptr;
  }

  auto thenExpr = analyze(*expression.thenExpr);
  if (!thenExpr) {
    return nullptr;
  }
  if (!isIntegerExpression(*thenExpr)) {
    addDiagnostic("then branch of ternary expression is not an integer "
                  "expression");
    return nullptr;
  }

  auto elseExpr = analyze(*expression.elseExpr);
  if (!elseExpr) {
    return nullptr;
  }
  if (!isIntegerExpression(*elseExpr)) {
    addDiagnostic("else branch of ternary expression is not an integer "
                  "expression");
    return nullptr;
  }

  const auto thenLength = integerExpressionByteLength(*thenExpr).value_or(4);
  const auto elseLength = integerExpressionByteLength(*elseExpr).value_or(4);
  const auto byteLength = std::max({std::size_t{4}, thenLength, elseLength});
  return std::make_unique<hir::TernaryExpr>(std::move(condition),
                                            std::move(thenExpr),
                                            std::move(elseExpr), byteLength);
}

std::unique_ptr<hir::Expr>
Analyzer::analyze(const ast::UnsignedExpr &expression) {
  if (isHandleExpression(*expression.operand)) {
    addDiagnostic("handle values cannot be interpreted as unsigned integers");
    return nullptr;
  }
  if (expression.byteLength != 0) {
    if (const auto *integer =
            dynamic_cast<const ast::IntegerLiteral *>(expression.operand.get())) {
      const auto value = literal::parseUnsignedIntegerLiteral(integer->value);
      const auto fits = value &&
                        (expression.byteLength == 8 ||
                         (expression.byteLength < 8 &&
                          *value < (std::uint64_t{1}
                                    << (expression.byteLength * 8U))));
      if (!fits) {
        addDiagnostic("unsigned integer literal '" + integer->value +
                      "' does not fit its compatibility width");
        return nullptr;
      }
      return std::make_unique<hir::UnsignedExpr>(
          std::make_unique<hir::IntegerLiteral>(integer->value,
                                                 expression.byteLength),
          expression.byteLength);
    }
  }
  auto operand = analyze(*expression.operand);
  if (!operand) {
    return nullptr;
  }
  if (!isIntegerExpression(*operand)) {
    addDiagnostic("operand of unsigned interpretation is not an integer "
                  "expression");
    return nullptr;
  }
  const auto byteLength = expression.byteLength != 0
                              ? expression.byteLength
                              : integerExpressionByteLength(*operand).value_or(4);
  return std::make_unique<hir::UnsignedExpr>(std::move(operand), byteLength);
}

std::unique_ptr<hir::Expr>
Analyzer::analyze(const ast::IntegerCastExpr &expression) {
  if (expression.byteLength != 1 && expression.byteLength != 2 &&
      expression.byteLength != 4 && expression.byteLength != 8) {
    addDiagnostic("integer cast target length must be 1, 2, 4, or 8 bytes");
    return nullptr;
  }
  if (isHandleExpression(*expression.operand)) {
    addDiagnostic("handle values cannot be cast to C integers");
    return nullptr;
  }
  auto operand = analyze(*expression.operand);
  if (!operand) {
    return nullptr;
  }
  if (!isIntegerExpression(*operand)) {
    addDiagnostic("integer cast operand is not an integer expression");
    return nullptr;
  }
  return std::make_unique<hir::IntegerCastExpr>(
      std::move(operand), expression.byteLength, expression.isSigned);
}

std::unique_ptr<hir::Expr>
Analyzer::analyzeFloatOperand(const ast::Expr &expression,
                              std::size_t byteLength) {
  CurrentRangeGuard rangeGuard(*this, expression);
  if (const auto *floating =
          dynamic_cast<const ast::FloatLiteral *>(&expression)) {
    return std::make_unique<hir::FloatLiteral>(floating->value, byteLength);
  }

  if (const auto *binary = dynamic_cast<const ast::BinaryExpr *>(&expression)) {
    if (isFloatBinaryOperator(binary->op) &&
        !isFloatComparisonOperator(binary->op)) {
      const auto typedLength = floatByteLengthForOperator(binary->op);
      if (!typedLength) {
        addDiagnostic("unsupported float operator '" + binary->op + "'");
        return nullptr;
      }
      const auto resultLength = *typedLength == 0 ? byteLength : *typedLength;
      if (resultLength != byteLength) {
        addDiagnostic("float operand byte length " +
                      std::to_string(resultLength) +
                      " does not match required byte length " +
                      std::to_string(byteLength));
        return nullptr;
      }

      auto left = analyzeFloatOperand(*binary->left, byteLength);
      if (!left) {
        return nullptr;
      }
      auto right = analyzeFloatOperand(*binary->right, byteLength);
      if (!right) {
        return nullptr;
      }
      return std::make_unique<hir::FloatBinaryExpr>(
          std::move(left), binary->op, std::move(right), byteLength);
    }
  }

  auto operand = analyze(expression);
  if (!operand) {
    return nullptr;
  }
  if (!isFloatExpression(*operand)) {
    addDiagnostic("float operand is not a float expression");
    return nullptr;
  }
  const auto operandLength = floatExpressionByteLength(*operand).value_or(0);
  if (operandLength != byteLength) {
    addDiagnostic("float operand byte length " + std::to_string(operandLength) +
                  " does not match required byte length " +
                  std::to_string(byteLength));
    return nullptr;
  }
  return operand;
}

} // namespace hitsimple::sema
