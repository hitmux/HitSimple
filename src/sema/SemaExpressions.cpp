#include "SemaAnalyzer.h"

#include "hitsimple/literal/Literal.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>

namespace hitsimple::sema {
namespace {

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

bool isLoweredFloatByteLength(std::size_t byteLength) {
  return byteLength == 2 || byteLength == 4 || byteLength == 8 ||
         byteLength == 16;
}

bool isFloatTemplate(std::string_view name) {
  return name == "f16" || name == "f32" || name == "f64" || name == "f128";
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

std::optional<std::size_t>
floatConversionByteLength(stdlib::BuiltinId builtin) {
  using enum stdlib::BuiltinId;
  switch (builtin) {
  case ToF16:
    return 2;
  case ToF32:
    return 4;
  case ToF64:
    return 8;
  case ToF128:
    return 16;
  default:
    return std::nullopt;
  }
}

struct IntegerConversion final {
  std::size_t byteLength = 0;
  bool isUnsigned = false;
};

std::optional<IntegerConversion> integerConversion(stdlib::BuiltinId builtin) {
  using enum stdlib::BuiltinId;
  switch (builtin) {
  case ToI8:
    return IntegerConversion{1, false};
  case ToI16:
    return IntegerConversion{2, false};
  case ToI32:
    return IntegerConversion{4, false};
  case ToI64:
    return IntegerConversion{8, false};
  case ToU8:
    return IntegerConversion{1, true};
  case ToU16:
    return IntegerConversion{2, true};
  case ToU32:
    return IntegerConversion{4, true};
  case ToU64:
    return IntegerConversion{8, true};
  default:
    return std::nullopt;
  }
}

bool isStandardConversion(stdlib::BuiltinId builtin) {
  return floatConversionByteLength(builtin).has_value() ||
         integerConversion(builtin).has_value();
}

bool isLogicalOperator(std::string_view op) { return op == "&&" || op == "||"; }

bool isComparisonOperator(std::string_view op) {
  return op == "==" || op == "!=" || op == "<" || op == "<=" ||
         op == ">" || op == ">=";
}

bool isIntegerArithmeticOperator(std::string_view op) {
  return op == "+" || op == "-" || op == "*" || op == "/" ||
         op == "%" || op == "**" || op == "<<" || op == ">>" ||
         op == "&" || op == "|" || op == "^";
}

bool isStandardIntegerTemplate(std::string_view name) {
  return name == "i8" || name == "i16" || name == "i32" ||
         name == "i64" || name == "u8" || name == "u16" ||
         name == "u32" || name == "u64";
}

bool isIntegerOperandForStandard(const hir::ViewSemantics &semantics) {
  return hir::isIntegerNumeric(semantics) ||
         semantics.category == hir::ViewCategory::Boolean;
}

bool isTypedIntegerOperator(std::string_view op) {
  return op.starts_with('%') && integerByteLengthForOperator(op).has_value();
}

std::size_t typedIntegerComputationLength(std::string_view op,
                                          const hir::Expr &left,
                                          const hir::Expr &right) {
  const auto width = integerByteLengthForOperator(op);
  if (!width) {
    return 0;
  }
  // `%d`/`%u` without an explicit byte count use the maximum operand View
  // length.  The parser stores the marker immediately after `%` in this form.
  const auto marker = op.find_first_of("du");
  if (marker == 1U) {
    return std::max(left.result.staticByteLength,
                    right.result.staticByteLength);
  }
  return *width;
}

std::optional<std::uint64_t>
foldIntegerOperation(std::string_view op, std::uint64_t left,
                     std::uint64_t right, std::size_t byteLength) {
  if (byteLength == 0 || byteLength > 8) {
    return std::nullopt;
  }
  const auto mask = byteLength == 8
                        ? std::numeric_limits<std::uint64_t>::max()
                        : (std::uint64_t{1} << (byteLength * 8U)) - 1U;
  left &= mask;
  right &= mask;
  if (op == "+") return (left + right) & mask;
  if (op == "-") return (left - right) & mask;
  if (op == "*") return (left * right) & mask;
  if (op == "/") {
    return right == 0 ? std::optional<std::uint64_t>{}
                      : std::optional<std::uint64_t>{left / right};
  }
  if (op == "%") {
    return right == 0 ? std::optional<std::uint64_t>{}
                      : std::optional<std::uint64_t>{left % right};
  }
  if (op == "**") {
    std::uint64_t value = 1;
    for (std::uint64_t index = 0; index < right; ++index) {
      value = (value * left) & mask;
    }
    return value;
  }
  if (op == "<<") {
    return right >= byteLength * 8U ? std::optional<std::uint64_t>{}
                                    : std::optional<std::uint64_t>{
                                          (left << right) & mask};
  }
  if (op == ">>") {
    return right >= byteLength * 8U ? std::optional<std::uint64_t>{}
                                    : std::optional<std::uint64_t>{left >> right};
  }
  if (op == "&") return left & right;
  if (op == "|") return left | right;
  if (op == "^") return left ^ right;
  return std::nullopt;
}

std::string_view integerOperatorSymbol(std::string_view op) {
  const std::string_view suffixes[] = {
      "**", "<<", ">>", "==", "!=", "<=", ">=", "+", "-",
      "*",  "/",  "%",  "&",  "|",  "^",  "<",  ">"};
  for (const auto suffix : suffixes) {
    if (op.ends_with(suffix)) {
      return suffix;
    }
  }
  return op;
}

std::optional<std::uint64_t> parseDecimalInteger(std::string_view text) {
  return literal::parseUnsignedIntegerLiteral(text);
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
  ExpressionDepthGuard depthGuard(*this);
  if (!depthGuard.entered()) {
    return nullptr;
  }
  if (const auto *identifier =
          dynamic_cast<const ast::IdentifierExpr *>(&expression)) {
    const auto *symbol = lookup(identifier->name);
    if (symbol == nullptr) {
      addDiagnostic("use of undeclared variable '" + identifier->name + "'");
      return nullptr;
    }
    auto lowered = std::make_unique<hir::VariableRef>(
        symbol->name, symbol->bindingName, symbol->storage,
        fixedResult(symbol->templateName, symbol->byteLength, true, true));
    if (symbol->templateName == "addr") {
      lowered->addressFacts = addressFactsFor(*lowered);
    }
    return lowered;
  }

  if (const auto *integer =
          dynamic_cast<const ast::IntegerLiteral *>(&expression)) {
    const auto byteLength = inferIntegerLiteralByteLength(*integer);
    if (byteLength == 0) {
      addDiagnostic("integer literal '" + integer->value + "' is out of range");
      return nullptr;
    }
    const auto value = literal::parseUnsignedIntegerLiteral(integer->value);
    return std::make_unique<hir::IntegerLiteral>(
        integer->value,
        value && *value > static_cast<std::uint64_t>(
                              std::numeric_limits<std::int64_t>::max())
            ? unsignedIntegerResult(byteLength)
            : signedIntegerResult(byteLength));
  }

  if (const auto *character =
          dynamic_cast<const ast::CharLiteral *>(&expression)) {
    const auto decoded = literal::decodeCharLiteral(character->value);
    if (!decoded) {
      addDiagnostic("invalid character literal '" + character->value +
                    "': " + *decoded.error);
      return nullptr;
    }
    if (decoded.bytes.empty()) {
      addDiagnostic("empty character literal is not allowed");
      return nullptr;
    }
    return std::make_unique<hir::CharacterLiteral>(
        decoded.bytes,
        decoded.bytes.size() == 1U ? unsignedIntegerResult(1)
                                   : rawBytesResult(decoded.bytes.size()));
  }

  if (const auto *boolean =
          dynamic_cast<const ast::BoolLiteral *>(&expression)) {
    return std::make_unique<hir::IntegerLiteral>(boolean->value ? "1" : "0",
                                                 booleanResult());
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
      addDiagnostic(
          "expression template view requires a fixed positive byte length");
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
        addDiagnostic(
            "expression template view byte length does not match template '" +
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
        std::move(operand), asExpr->templateName, isAddressable,
        fixedResult(asExpr->templateName, *byteLength, isAddressable,
                    isAddressable));
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
        string->value, fixedResult("cstr", decoded.bytes.size() + 1));
  }

  if (const auto *floating =
          dynamic_cast<const ast::FloatLiteral *>(&expression)) {
    return std::make_unique<hir::FloatLiteral>(floating->value,
                                               fixedResult("f64", 8));
  }

  if (const auto *sizeofExpr =
          dynamic_cast<const ast::SizeofExpr *>(&expression)) {
    if (const auto byteLength = templateByteLength(sizeofExpr->name)) {
      return std::make_unique<hir::IntegerLiteral>(
          std::to_string(*byteLength), fixedResult("u64", 8));
    }
    const auto *symbol = lookup(sizeofExpr->name);
    if (symbol == nullptr) {
      addDiagnostic("sizeof unknown name '" + sizeofExpr->name + "'");
      return nullptr;
    }
    return std::make_unique<hir::IntegerLiteral>(
        std::to_string(symbol->byteLength), fixedResult("u64", 8));
  }

  if (const auto *call = dynamic_cast<const ast::CallExpr *>(&expression)) {
    if (stdlib::isRemovedLegacyName(call->callee)) {
      addDiagnostic(
          "legacy standard library name '" + call->callee +
          "' is not accepted; use " +
          std::string(stdlib::replacementForRemovedLegacyName(call->callee)));
      return nullptr;
    }
    if (rejectUnavailableStandardBuiltin(*call)) {
      return nullptr;
    }
    const auto builtin = builtinForCall(*call);
    const auto floatingOverloadIndex = [](std::size_t byteLength) {
      switch (byteLength) {
      case 2:
        return std::uint16_t{0};
      case 4:
        return std::uint16_t{1};
      case 8:
        return std::uint16_t{2};
      case 16:
        return std::uint16_t{3};
      default:
        return std::uint16_t{0};
      }
    };
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
              "put", std::move(arguments), false, stdlib::BuiltinId::Put,
              std::vector<hir::FormatArgKind>{}, 0,
              "i32", fixedResult("i32", 4));
        }
        if (const auto templateName =
                operatorTemplateName(*call->arguments[0]);
            templateName && isStandardTemplateWithFormat(*templateName)) {
          auto lowered = lowerStandardTemplatePrintCall(*call->arguments[0],
                                                         *templateName);
          if (!lowered) {
            return nullptr;
          }
          return std::make_unique<hir::CallExpr>(
              std::move(lowered->callee), std::move(lowered->arguments), false,
              lowered->builtin, std::move(lowered->formatArgumentKinds), 0,
              "i32", fixedResult("i32", 4));
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
      if ((builtin == stdlib::BuiltinId::Printf ||
           builtin == stdlib::BuiltinId::Fprintf) &&
          !expressionTemplateName(*call->arguments[formatIndex]) &&
          !isCStringExpression(*call->arguments[formatIndex])) {
        addDiagnostic("function call '" + call->callee + "' argument " +
                      std::to_string(formatIndex + 1U) +
                      " must be a cstr View");
        return nullptr;
      }
      if (call->arguments.size() == formatIndex + 1U) {
        if (const auto templateName =
                expressionTemplateName(*call->arguments[formatIndex])) {
          auto lowered = lowerUserTemplateFormatCall(
              *call, *builtin, *templateName, formatIndex);
          if (!lowered) {
            return nullptr;
          }
          return std::make_unique<hir::UserTemplateFormatCallExpr>(
              std::move(lowered->callee), std::move(lowered->value),
              lowered->sink, std::move(lowered->file),
              fixedResult("i32", lowered->resultByteLength));
        }
      }
      if (expressionTemplateName(*call->arguments[formatIndex])) {
        addDiagnostic(
            "user template formatting requires a direct print(value), "
            "printf(value), or fprintf(file, value) call");
        return nullptr;
      }
      for (std::size_t index = formatIndex + 1U; index < call->arguments.size();
           ++index) {
        if (expressionTemplateName(*call->arguments[index])) {
          addDiagnostic(
              "user template formatting requires a direct print(value), "
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
      return std::make_unique<hir::CallExpr>(
          call->callee, std::move(arguments), false, *builtin,
          std::move(formatArgumentKinds),
          builtinOverloadIndex(*call, *builtin), "i32", fixedResult("i32", 4));
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
                                                   fixedResult("u64", pointerByteLength()));
    }
    if (builtin == stdlib::BuiltinId::Calloc && call->arguments.size() == 2U) {
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
      return std::make_unique<hir::CallExpr>(
          "calloc", std::move(arguments), false, stdlib::BuiltinId::Calloc,
          std::vector<hir::FormatArgKind>{}, 0, "addr",
          fixedResult("addr", pointerByteLength()));
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
            call->callee, std::move(arguments), false,
            stdlib::BuiltinId::ResizeBytes, std::vector<hir::FormatArgKind>{},
            0, "bytes",
            fixedResult("bytes", *resultLength));
      }
      return std::make_unique<hir::DynamicByteViewExpr>(
          hir::DynamicByteViewOperation::ResizeBytes, std::move(operand),
          std::move(length), dynamicBytesResult());
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
            nullptr, dynamicBytesResult());
      }
      if (*length == 1 || *length == 2 || *length == 4 || *length == 8) {
        std::vector<std::unique_ptr<hir::Expr>> arguments;
        arguments.push_back(std::move(operand));
        return std::make_unique<hir::CallExpr>(
            "byte_swap", std::move(arguments), false,
            stdlib::BuiltinId::ByteSwap, std::vector<hir::FormatArgKind>{}, 0,
            "bytes",
            fixedResult("bytes", *length));
      }
      return std::make_unique<hir::ByteSwapExpr>(
          std::move(operand), fixedResult("bytes", *length));
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
      if (isUnsignedExpression(*operand)) {
        addDiagnostic("abs requires a signed integer expression");
        return nullptr;
      }
      const auto length = integerExpressionByteLength(*operand).value_or(4);
      std::uint16_t overloadIndex = 0;
      switch (length) {
      case 1:
        overloadIndex = 0;
        break;
      case 2:
        overloadIndex = 1;
        break;
      case 4:
        overloadIndex = 2;
        break;
      case 8:
        overloadIndex = 3;
        break;
      default:
        addDiagnostic("abs requires an i8, i16, i32, or i64 expression");
        return nullptr;
      }
      std::vector<std::unique_ptr<hir::Expr>> arguments;
      arguments.push_back(std::move(operand));
      return std::make_unique<hir::CallExpr>(
          "abs", std::move(arguments), false, stdlib::BuiltinId::Abs,
          std::vector<hir::FormatArgKind>{}, overloadIndex,
          "i" + std::to_string(length * 8U),
          fixedResult("i" + std::to_string(length * 8U), length));
    }
    if (builtin == stdlib::BuiltinId::Min ||
        builtin == stdlib::BuiltinId::Max) {
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
        const auto templateName = operatorTemplateName(argument);
        return templateName && isFloatTemplate(*templateName);
      };
      const bool leftIsFloating = isFloatingArgument(*call->arguments[0]);
      const bool rightIsFloating = isFloatingArgument(*call->arguments[1]);
      if (leftIsFloating != rightIsFloating) {
        addDiagnostic(call->callee +
                      " arguments must use the same numeric interpretation");
        return nullptr;
      }
      const auto overloadFor =
          [this, &call,
           leftLength](bool floating,
                       bool unsignedInteger) -> std::optional<std::uint16_t> {
        if (floating) {
          switch (*leftLength) {
          case 2:
            return std::uint16_t{8};
          case 4:
            return std::uint16_t{9};
          case 8:
            return std::uint16_t{10};
          case 16:
            return std::uint16_t{11};
          default:
            return std::nullopt;
          }
        }
        const auto base = unsignedInteger ? std::uint16_t{4} : std::uint16_t{0};
        switch (*leftLength) {
        case 1:
          return base;
        case 2:
          return static_cast<std::uint16_t>(base + 1U);
        case 4:
          return static_cast<std::uint16_t>(base + 2U);
        case 8:
          return static_cast<std::uint16_t>(base + 3U);
        default:
          addDiagnostic(call->callee +
                        " requires i8/i16/i32/i64, u8/u16/u32/u64, or "
                        "f16/f32/f64/f128 arguments");
          return std::nullopt;
        }
      };
      if (leftIsFloating) {
        auto left = analyzeFloatOperand(*call->arguments[0], *leftLength);
        auto right = analyzeFloatOperand(*call->arguments[1], *rightLength);
        if (!left || !right) {
          return nullptr;
        }
        const auto overloadIndex = overloadFor(true, false);
        if (!overloadIndex) {
          return nullptr;
        }
        std::vector<std::unique_ptr<hir::Expr>> arguments;
        arguments.push_back(std::move(left));
        arguments.push_back(std::move(right));
        return std::make_unique<hir::CallExpr>(
            call->callee, std::move(arguments), true, *builtin,
            std::vector<hir::FormatArgKind>{}, *overloadIndex,
            "f" + std::to_string(*leftLength * 8U),
            fixedResult("f" + std::to_string(*leftLength * 8U), *leftLength));
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
      const auto unsignedInteger = isUnsignedExpression(*left);
      if (unsignedInteger != isUnsignedExpression(*right)) {
        addDiagnostic(call->callee +
                      " arguments must use the same numeric interpretation");
        return nullptr;
      }
      const auto overloadIndex = overloadFor(false, unsignedInteger);
      if (!overloadIndex) {
        return nullptr;
      }
      std::vector<std::unique_ptr<hir::Expr>> arguments;
      arguments.push_back(std::move(left));
      arguments.push_back(std::move(right));
      return std::make_unique<hir::CallExpr>(
          call->callee, std::move(arguments), false, *builtin,
          std::vector<hir::FormatArgKind>{}, *overloadIndex,
          std::string(unsignedInteger ? "u" : "i") +
              std::to_string(*leftLength * 8U),
          fixedResult(std::string(unsignedInteger ? "u" : "i") +
                          std::to_string(*leftLength * 8U),
                      *leftLength));
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
      const auto sourceTemplate = operatorTemplateName(*call->arguments[0]);
      const bool sourceIsFloating =
          dynamic_cast<const ast::FloatLiteral *>(call->arguments[0].get()) !=
              nullptr ||
          (sourceTemplate && isFloatTemplate(*sourceTemplate));
      std::unique_ptr<hir::Expr> operand =
          sourceIsFloating
              ? analyzeFloatOperand(*call->arguments[0], *sourceLength)
              : analyze(*call->arguments[0]);
      if (!operand) {
        return nullptr;
      }
      if (!sourceIsFloating && !isIntegerExpression(*operand)) {
        addDiagnostic(call->callee +
                      " data must be an integer or floating expression");
        return nullptr;
      }
      const bool sourceUnsigned =
          !sourceIsFloating && isUnsignedExpression(*operand);
      return std::make_unique<hir::ToFloatExpr>(
          std::move(operand), sourceUnsigned, sourceIsFloating,
          fixedResult("f" + std::to_string(targetLength * 8U), targetLength));
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
      const auto sourceTemplate = operatorTemplateName(*call->arguments[0]);
      const bool sourceIsFloating =
          dynamic_cast<const ast::FloatLiteral *>(call->arguments[0].get()) !=
              nullptr ||
          (sourceTemplate && isFloatTemplate(*sourceTemplate));
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
            std::move(operand), *sourceLength, conversion.isUnsigned,
            fixedResult(std::string(conversion.isUnsigned ? "u" : "i") +
                            std::to_string(conversion.byteLength * 8U),
                        conversion.byteLength));
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
          std::move(operand), !conversion.isUnsigned,
          fixedResult(std::string(conversion.isUnsigned ? "u" : "i") +
                          std::to_string(conversion.byteLength * 8U),
                      conversion.byteLength));
    }
    if (builtin && isSupportedFloatMathFunction(*builtin)) {
      if (call->arguments.size() != 1U) {
        addDiagnostic(call->callee + " expects 1 argument");
        return nullptr;
      }
      const auto length = inferByteLength(*call->arguments[0]);
      if (!length || !isLoweredFloatByteLength(*length)) {
        addDiagnostic(call->callee +
                      " requires an f16, f32, f64, or f128 input");
        return nullptr;
      }
      auto operand = analyzeFloatOperand(*call->arguments[0], *length);
      if (!operand) {
        return nullptr;
      }
      std::vector<std::unique_ptr<hir::Expr>> arguments;
      arguments.push_back(std::move(operand));
      return std::make_unique<hir::CallExpr>(
          call->callee, std::move(arguments), true, *builtin,
          std::vector<hir::FormatArgKind>{}, floatingOverloadIndex(*length),
          "f" + std::to_string(*length * 8U),
          fixedResult("f" + std::to_string(*length * 8U), *length));
    }
    if (builtin && isSupportedBinaryFloatMathFunction(*builtin)) {
      if (call->arguments.size() != 2U) {
        addDiagnostic(call->callee + " expects 2 arguments");
        return nullptr;
      }
      const auto length = inferByteLength(*call->arguments[0]);
      if (!length || !isLoweredFloatByteLength(*length)) {
        addDiagnostic(call->callee +
                      " requires an f16, f32, f64, or f128 input");
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
      return std::make_unique<hir::CallExpr>(
          call->callee, std::move(arguments), true, *builtin,
          std::vector<hir::FormatArgKind>{}, floatingOverloadIndex(*length),
          "f" + std::to_string(*length * 8U),
          fixedResult("f" + std::to_string(*length * 8U), *length));
    }
    return analyzeCallExpr(*call);
  }

  if (const auto *call =
          dynamic_cast<const ast::MethodCallExpr *>(&expression)) {
    return analyzeMethodCallExpr(*call);
  }

  if (const auto *assignment =
          dynamic_cast<const ast::AssignmentExpr *>(&expression)) {
    return analyze(*assignment);
  }

  if (const auto reference = resolveMemoryReference(expression)) {
    auto lowered = std::make_unique<hir::VariableRef>(
        reference->name, reference->bindingName, reference->storage,
        reference->offset,
        fixedResult(reference->templateName, reference->byteLength, true, true));
    if (reference->templateName == "addr") {
      lowered->addressFacts = addressFactsFor(*lowered);
    }
    return lowered;
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
    if (integerExpressionByteLength(*address).value_or(0) >
        pointerByteLength()) {
      addDiagnostic("dereference address is wider than pointer length");
      return nullptr;
    }
    return std::make_unique<hir::DerefExpr>(
        std::move(address), fixedResult({}, length, true, true));
  }

  if (const auto *index = dynamic_cast<const ast::IndexExpr *>(&expression)) {
    auto address = lowerIndexAddress(*index);
    if (!address) {
      return nullptr;
    }
    return std::make_unique<hir::DerefExpr>(
        std::move(address), fixedResult({}, 1, true, true));
  }

  if (const auto *slice = dynamic_cast<const ast::SliceExpr *>(&expression)) {
    auto lowered = lowerSlice(*slice);
    if (!lowered) {
      return nullptr;
    }
    return std::make_unique<hir::DerefExpr>(
        std::move(lowered->address),
        fixedResult({}, lowered->byteLength, true, true));
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
  const auto result = lowered->result->result;
  return std::make_unique<hir::AssignmentExpr>(std::move(lowered->stores),
                                               std::move(lowered->result),
                                               result);
}

std::unique_ptr<hir::Expr>
Analyzer::analyzeCallExpr(const ast::CallExpr &call) {
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
    addDiagnostic("function call '" + call.callee +
                  "' does not return a value");
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
  auto loweredCall = std::make_unique<hir::CallExpr>(
      call.callee, std::move(arguments), isFloating, signature.builtin,
      std::vector<hir::FormatArgKind>{},
      builtinOverloadIndex(call, signature.builtin),
      signature.returnTemplateNames.empty()
          ? std::string{}
          : signature.returnTemplateNames.front(),
      fixedResult(signature.returnTemplateNames.empty()
                      ? std::string{}
                      : signature.returnTemplateNames.front(),
                  signature.returnByteLengths.front()));
  loweredCall->argumentPlans.clear();
  loweredCall->argumentPlans.reserve(loweredCall->arguments.size());
  for (std::size_t index = 0; index < loweredCall->arguments.size(); ++index) {
    const auto *integerCast = dynamic_cast<const hir::IntegerCastExpr *>(
        loweredCall->arguments[index].get());
    const auto &source = integerCast != nullptr
                             ? integerCast->operand->result
                             : loweredCall->arguments[index]->result;
    const auto destination = fixedResult(signature.parameterTemplateNames[index],
                                         signature.parameterByteLengths[index]);
    const bool sameViewSemantics =
        source.category == destination.category &&
        source.integerInterpretation == destination.integerInterpretation &&
        source.lengthKind == destination.lengthKind &&
        source.staticByteLength == destination.staticByteLength &&
        source.templateName == destination.templateName;
    const auto kind = source.category == hir::ViewCategory::Floating
                          ? hir::ConversionKind::Floating
                          : integerCast != nullptr || !sameViewSemantics
                                ? hir::ConversionKind::IntegerWidth
                                : hir::ConversionKind::Identity;
    loweredCall->argumentPlans.push_back(
        hir::ConversionPlan{kind, source, destination});
  }
  return loweredCall;
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
    addDiagnostic("no matching impl method '" + call.method +
                  "' for template '" + *receiverTemplate + "'");
    return std::nullopt;
  }

  std::vector<std::unique_ptr<hir::Expr>> arguments;
  arguments.reserve(call.arguments.size() + 1U);
  const auto lowerArgument = [this,
                              &arguments](const ast::Expr &argument,
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
      addDiagnostic(
          "dynamic View cannot be passed to a fixed impl method parameter");
      return false;
    }
    if (lowered->result.lengthKind != hir::ViewLengthKind::Static ||
        lowered->result.staticByteLength != expectedLength ||
        lowered->result.templateName != templateName) {
      addDiagnostic("method argument must exactly match parameter template and byte length");
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
      lowered->method->returnTemplateNames.front(),
      fixedResult(lowered->method->returnTemplateNames.front(),
                  lowered->method->returnByteLengths.front()));
}

std::unique_ptr<hir::Expr>
Analyzer::analyze(const ast::BinaryExpr &expression) {
  auto left = analyze(*expression.left);
  auto right = analyze(*expression.right);
  if (!left || !right) {
    return nullptr;
  }

  if (isLogicalOperator(expression.op)) {
    if (!hir::isBooleanTestable(left->result) ||
        !hir::isBooleanTestable(right->result)) {
      addDiagnostic("logical operator operands must form Views");
      return nullptr;
    }
    return std::make_unique<hir::BinaryExpr>(
        std::make_unique<hir::BooleanTestExpr>(std::move(left), booleanResult()),
        expression.op,
        std::make_unique<hir::BooleanTestExpr>(std::move(right), booleanResult()),
        booleanResult());
  }

  const auto leftTemplate = left->result.templateName;
  const auto rightTemplate = right->result.templateName;
  const bool leftUser = !leftTemplate.empty() && templates_.contains(leftTemplate);
  const bool rightUser = !rightTemplate.empty() && templates_.contains(rightTemplate);
  if (leftUser || rightUser) {
    const std::string key = expression.op + "|" + leftTemplate + "|" +
                            rightTemplate;
    if (const auto found = implOpIndexes_.find(key);
        found != implOpIndexes_.end()) {
      const auto &info = implOpInfos_[found->second];
      if (info.returnByteLengths.size() != 1U ||
          info.returnTemplateNames.size() != 1U) {
        addDiagnostic("internal error: invalid impl op result");
        return nullptr;
      }
      std::vector<std::unique_ptr<hir::Expr>> arguments;
      arguments.push_back(std::move(left));
      arguments.push_back(std::move(right));
      return std::make_unique<hir::UserTemplateOpCallExpr>(
          info.symbolName, std::move(arguments), info.returnTemplateNames.front(),
          fixedResult(info.returnTemplateNames.front(),
                      info.returnByteLengths.front()));
    }
    addDiagnostic("ordinary operation has no applicable user-template candidate");
    return nullptr;
  }

  if (!isFloatBinaryOperator(expression.op) &&
      (left->result.category == hir::ViewCategory::Floating ||
       right->result.category == hir::ViewCategory::Floating)) {
    if (left->result.category != hir::ViewCategory::Floating ||
        right->result.category != hir::ViewCategory::Floating ||
        leftTemplate != rightTemplate) {
      addDiagnostic("ordinary floating operation requires the same template");
      return nullptr;
    }
    if (!isComparisonOperator(expression.op) &&
        !isIntegerArithmeticOperator(expression.op)) {
      addDiagnostic("unsupported floating operation '" + expression.op + "'");
      return nullptr;
    }
    if (expression.op == "%" || expression.op == "<<" ||
        expression.op == ">>" || expression.op == "&" ||
        expression.op == "|" || expression.op == "^") {
      addDiagnostic("ordinary floating operation is not defined for '" +
                    expression.op + "'");
      return nullptr;
    }
    const auto byteLength = left->result.staticByteLength;
    if (isComparisonOperator(expression.op)) {
      return std::make_unique<hir::FloatCompareExpr>(
          std::move(left), expression.op, std::move(right), byteLength,
          booleanResult());
    }
    return std::make_unique<hir::FloatBinaryExpr>(
        std::move(left), expression.op, std::move(right),
        fixedResult(leftTemplate, byteLength));
  }

  if (isFloatBinaryOperator(expression.op)) {
    const auto typedLength = floatByteLengthForOperator(expression.op);
    if (!typedLength || !isFloatExpression(*left) || !isFloatExpression(*right)) {
      addDiagnostic("typed floating operation requires floating operands");
      return nullptr;
    }
    const auto byteLength = *typedLength == 0U
                                ? std::max(left->result.staticByteLength,
                                           right->result.staticByteLength)
                                : *typedLength;
    if (!isLoweredFloatByteLength(byteLength)) {
      addDiagnostic("float byte length is not supported");
      return nullptr;
    }
    if (isFloatComparisonOperator(expression.op)) {
      return std::make_unique<hir::FloatCompareExpr>(
          std::move(left), expression.op, std::move(right), byteLength,
          booleanResult());
    }
    return std::make_unique<hir::FloatBinaryExpr>(
        std::move(left), expression.op, std::move(right),
        fixedResult("f" + std::to_string(byteLength * 8U), byteLength));
  }

  const bool typed = isTypedIntegerOperator(expression.op);
  const auto op = typed ? integerOperatorSymbol(expression.op) : expression.op;
  if (!isIntegerArithmeticOperator(op) && !isComparisonOperator(op)) {
    addDiagnostic("unsupported binary operator '" + expression.op + "'");
    return nullptr;
  }
  if (isDivisionOperator(op)) {
    if (const auto *integer = dynamic_cast<const ast::IntegerLiteral *>(
            expression.right.get());
        integer != nullptr && parseDecimalInteger(integer->value) == 0U) {
      addDiagnostic("division by zero in binary expression");
      return nullptr;
    }
  }

  hir::StandardOperationKind candidate = hir::StandardOperationKind::Legacy;
  hir::ViewSemantics result = booleanResult();
  std::size_t byteLength = 1;
  if (typed) {
    const bool leftInteger = hir::isIntegerNumeric(left->result);
    const bool rightInteger = hir::isIntegerNumeric(right->result);
    // The C compatibility lowering represents scaled pointer arithmetic as
    // an explicit typed integer operation.  Core syntax must use postfix `?`
    // before entering this path; compatibility syntax has already established
    // the corresponding pointer-arithmetic contract.
    const bool compatibilityPointerArithmetic =
        cCompatibilityMode_ &&
        ((left->result.category == hir::ViewCategory::Address && rightInteger) ||
         (right->result.category == hir::ViewCategory::Address && leftInteger));
    if ((!leftInteger || !rightInteger) && !compatibilityPointerArithmetic) {
      addDiagnostic("typed integer operation requires integer operands");
      return nullptr;
    }
    byteLength = typedIntegerComputationLength(expression.op, *left, *right);
    if (byteLength == 0U) {
      addDiagnostic("typed integer operation has no computation width");
      return nullptr;
    }
    candidate = hir::StandardOperationKind::UntemplatedInteger;
    result = isComparisonOperator(op)
                 ? booleanResult()
                 : (expression.op.find('u') != std::string::npos
                        ? unsignedIntegerResult(byteLength)
                        : signedIntegerResult(byteLength));
  } else if (leftTemplate.empty() && rightTemplate.empty() &&
             hir::isIntegerNumeric(left->result) &&
             hir::isIntegerNumeric(right->result)) {
    byteLength = std::max(left->result.staticByteLength,
                          right->result.staticByteLength);
    candidate = hir::StandardOperationKind::UntemplatedInteger;
    result = isComparisonOperator(op)
                 ? booleanResult()
                 : ((isUnsignedExpression(*left) || isUnsignedExpression(*right))
                        ? unsignedIntegerResult(byteLength)
                        : signedIntegerResult(byteLength));
  } else if ((isStandardIntegerTemplate(leftTemplate) ||
              isStandardIntegerTemplate(rightTemplate))) {
    const auto &selectedTemplate = isStandardIntegerTemplate(leftTemplate)
                                       ? leftTemplate
                                       : rightTemplate;
    const auto &otherTemplate = isStandardIntegerTemplate(leftTemplate)
                                    ? rightTemplate
                                    : leftTemplate;
    const auto &other = isStandardIntegerTemplate(leftTemplate) ? *right : *left;
    if ((!otherTemplate.empty() && otherTemplate != selectedTemplate) ||
        !isIntegerOperandForStandard(other.result)) {
      addDiagnostic("ordinary integer operation requires the same template");
      return nullptr;
    }
    byteLength = isStandardIntegerTemplate(leftTemplate)
                     ? left->result.staticByteLength
                     : right->result.staticByteLength;
    candidate = hir::StandardOperationKind::StandardInteger;
    result = isComparisonOperator(op) ? booleanResult()
                                      : fixedResult(selectedTemplate, byteLength);
  } else if (left->result.category == hir::ViewCategory::Boolean ||
             right->result.category == hir::ViewCategory::Boolean) {
    if (left->result.category != hir::ViewCategory::Boolean ||
        right->result.category != hir::ViewCategory::Boolean ||
        !isComparisonOperator(op) || (op != "==" && op != "!=")) {
      addDiagnostic("bool only supports == and != ordinary operations");
      return nullptr;
    }
    candidate = hir::StandardOperationKind::StandardBoolean;
  } else if (left->result.category == hir::ViewCategory::Address ||
             right->result.category == hir::ViewCategory::Address) {
    const bool compatible =
        (left->result.category == hir::ViewCategory::Address &&
         (right->result.category == hir::ViewCategory::Address ||
          (rightTemplate.empty() && hir::isIntegerNumeric(right->result)))) ||
        (right->result.category == hir::ViewCategory::Address &&
         leftTemplate.empty() && hir::isIntegerNumeric(left->result));
    if (!compatible || (op != "==" && op != "!=")) {
      addDiagnostic("addr only supports == and != ordinary operations");
      return nullptr;
    }
    candidate = hir::StandardOperationKind::StandardAddress;
  } else if (left->result.category == hir::ViewCategory::Handle ||
             right->result.category == hir::ViewCategory::Handle) {
    if (left->result.category != hir::ViewCategory::Handle ||
        right->result.category != hir::ViewCategory::Handle ||
        (op != "==" && op != "!=")) {
      addDiagnostic("handle only supports same-template == and != comparisons");
      return nullptr;
    }
    candidate = hir::StandardOperationKind::StandardHandle;
  } else if (left->result.category == hir::ViewCategory::Bytes ||
             right->result.category == hir::ViewCategory::Bytes) {
    if (left->result.category != hir::ViewCategory::Bytes ||
        right->result.category != hir::ViewCategory::Bytes ||
        !isComparisonOperator(op)) {
      addDiagnostic("bytes only supports comparison ordinary operations");
      return nullptr;
    }
    candidate = hir::StandardOperationKind::StandardBytesCompare;
  } else if (left->result.category == hir::ViewCategory::CString ||
             right->result.category == hir::ViewCategory::CString) {
    if (left->result.category != hir::ViewCategory::CString ||
        right->result.category != hir::ViewCategory::CString ||
        !isComparisonOperator(op)) {
      addDiagnostic("cstr only supports comparison ordinary operations");
      return nullptr;
    }
    candidate = hir::StandardOperationKind::StandardCStringCompare;
  } else {
    addDiagnostic("ordinary operation has no applicable standard candidate");
    return nullptr;
  }

  // `addr? +/- integer` is an explicit byte-address calculation.  Its View
  // remains an integer observation unless an assignment rebinds it to `addr`,
  // but later phases must retain the resolved address-offset operation rather
  // than recover it from the source spelling.
  if ((op == "+" || op == "-") && hir::isIntegerNumeric(right->result)) {
    if (const auto *unsignedLeft =
            dynamic_cast<const hir::UnsignedExpr *>(left.get());
        unsignedLeft != nullptr && unsignedLeft->operand != nullptr &&
        dynamic_cast<const hir::AddressOfExpr *>(unsignedLeft->operand.get()) !=
            nullptr) {
      candidate = hir::StandardOperationKind::AddressOffset;
    }
  }

  if (candidate == hir::StandardOperationKind::AddressOffset && op == "-") {
    right = std::make_unique<hir::UnaryExpr>(
        "-", std::move(right), signedIntegerResult(pointerByteLength()));
  }

  if (const auto *leftLiteral = dynamic_cast<const hir::IntegerLiteral *>(left.get());
      leftLiteral != nullptr) {
    if (const auto *rightLiteral =
            dynamic_cast<const hir::IntegerLiteral *>(right.get());
        rightLiteral != nullptr && candidate != hir::StandardOperationKind::StandardBytesCompare &&
        candidate != hir::StandardOperationKind::StandardCStringCompare) {
      const auto leftValue = literal::parseUnsignedIntegerLiteral(leftLiteral->value);
      const auto rightValue = literal::parseUnsignedIntegerLiteral(rightLiteral->value);
      if (!leftValue || !rightValue) {
        addDiagnostic("invalid integer literal in constant expression");
        return nullptr;
      }
      if (isComparisonOperator(op)) {
        const bool unsignedComparison = isUnsignedExpression(*left) ||
                                        isUnsignedExpression(*right);
        bool comparison = false;
        if (op == "==") comparison = *leftValue == *rightValue;
        else if (op == "!=") comparison = *leftValue != *rightValue;
        else if (unsignedComparison) {
          if (op == "<") comparison = *leftValue < *rightValue;
          else if (op == "<=") comparison = *leftValue <= *rightValue;
          else if (op == ">") comparison = *leftValue > *rightValue;
          else comparison = *leftValue >= *rightValue;
        } else {
          const auto signedLeft = static_cast<std::int64_t>(*leftValue);
          const auto signedRight = static_cast<std::int64_t>(*rightValue);
          if (op == "<") comparison = signedLeft < signedRight;
          else if (op == "<=") comparison = signedLeft <= signedRight;
          else if (op == ">") comparison = signedLeft > signedRight;
          else comparison = signedLeft >= signedRight;
        }
        return std::make_unique<hir::IntegerLiteral>(comparison ? "1" : "0",
                                                     booleanResult());
      }
      const auto folded = foldIntegerOperation(op, *leftValue, *rightValue,
                                               byteLength);
      if (!folded) {
        addDiagnostic("invalid constant integer operation");
        return nullptr;
      }
      return std::make_unique<hir::IntegerLiteral>(std::to_string(*folded),
                                                   result);
    }
  }
  return std::make_unique<hir::BinaryExpr>(std::move(left), expression.op,
                                           std::move(right), result, candidate);
}

std::unique_ptr<hir::Expr> Analyzer::analyze(const ast::UnaryExpr &expression) {
  if (expression.op == "&") {
    if (const auto *index =
            dynamic_cast<const ast::IndexExpr *>(expression.operand.get())) {
      return lowerIndexAddress(*index);
    }
    if (const auto *slice =
            dynamic_cast<const ast::SliceExpr *>(expression.operand.get())) {
      auto lowered = lowerSlice(*slice);
      return lowered ? std::move(lowered->address) : nullptr;
    }
    if (const auto *deref =
            dynamic_cast<const ast::DerefExpr *>(expression.operand.get())) {
      return analyze(*deref->address);
    }
    const auto reference = resolveAddressableReference(*expression.operand);
    if (!reference) {
      return nullptr;
    }
    return std::make_unique<hir::AddressOfExpr>(
        reference->name, reference->bindingName, reference->byteLength,
        reference->storage, reference->offset,
        fixedResult("addr", pointerByteLength()));
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
  if (expression.op == "!") {
    if (!hir::isBooleanTestable(operand->result)) {
      addDiagnostic("operand of '!' cannot form a View");
      return nullptr;
    }
    return std::make_unique<hir::UnaryExpr>(
        expression.op,
        std::make_unique<hir::BooleanTestExpr>(std::move(operand),
                                               booleanResult()),
        booleanResult());
  }
  if (!hir::isIntegerNumeric(operand->result)) {
    addDiagnostic("operand of '" + expression.op + "' is not an integer View");
    return nullptr;
  }

  const auto byteLength = operand->result.staticByteLength;
  const bool negatedUnsignedLiteral =
      expression.op == "-" &&
      dynamic_cast<const ast::IntegerLiteral *>(expression.operand.get()) !=
          nullptr;
  const auto result = operand->result.templateName.empty()
                          ? ((isUnsignedExpression(*operand) &&
                              !negatedUnsignedLiteral)
                                 ? unsignedIntegerResult(byteLength)
                                 : signedIntegerResult(byteLength))
                          : fixedResult(operand->result.templateName, byteLength);
  return std::make_unique<hir::UnaryExpr>(expression.op, std::move(operand),
                                          result);
}

std::unique_ptr<hir::Expr>
Analyzer::analyze(const ast::TernaryExpr &expression) {
  auto condition = analyze(*expression.condition);
  if (!condition) {
    return nullptr;
  }
  if (!hir::isBooleanTestable(condition->result)) {
    addDiagnostic("ternary condition cannot form a View");
    return nullptr;
  }

  auto thenExpr = analyze(*expression.thenExpr);
  if (!thenExpr) {
    return nullptr;
  }
  auto elseExpr = analyze(*expression.elseExpr);
  if (!elseExpr) {
    return nullptr;
  }
  const auto sameBranchView = [](const hir::ViewSemantics &left,
                                 const hir::ViewSemantics &right) {
    return left.category == right.category &&
           left.integerInterpretation == right.integerInterpretation &&
           left.lengthKind == right.lengthKind &&
           left.staticByteLength == right.staticByteLength &&
           left.templateName == right.templateName;
  };
  if (!sameBranchView(thenExpr->result, elseExpr->result)) {
    addDiagnostic("ternary branches must have the same template and byte length");
    return nullptr;
  }
  auto result = thenExpr->result;
  result.isAddressable = false;
  result.isMutableLValue = false;
  return std::make_unique<hir::TernaryExpr>(
      std::make_unique<hir::BooleanTestExpr>(std::move(condition),
                                             booleanResult()),
                                            std::move(thenExpr),
                                            std::move(elseExpr), result);
}

std::unique_ptr<hir::Expr>
Analyzer::analyze(const ast::UnsignedExpr &expression) {
  if (expression.byteLength != 0) {
    if (const auto *integer = dynamic_cast<const ast::IntegerLiteral *>(
            expression.operand.get())) {
      const auto value = literal::parseUnsignedIntegerLiteral(integer->value);
      const auto fits =
          value &&
          (expression.byteLength == 8 ||
           (expression.byteLength < 8 &&
            *value < (std::uint64_t{1} << (expression.byteLength * 8U))));
      if (!fits) {
        addDiagnostic("unsigned integer literal '" + integer->value +
                      "' does not fit its compatibility width");
        return nullptr;
      }
      return std::make_unique<hir::UnsignedExpr>(
          std::make_unique<hir::IntegerLiteral>(integer->value,
                                                signedIntegerResult(expression.byteLength)),
          unsignedIntegerResult(expression.byteLength));
    }
  }
  auto operand = analyze(*expression.operand);
  if (!operand) {
    return nullptr;
  }
  // The C compatibility translator may retain an unsigned-result marker
  // around a comparison or logical expression.  Core comparisons already
  // produce the canonical bool View, so the marker has no further semantic
  // effect and must not turn that bool into an integer View.
  if (cCompatibilityMode_ &&
      operand->result.category == hir::ViewCategory::Boolean) {
    return operand;
  }
  if (!hir::isIntegerNumeric(operand->result) &&
      operand->result.category != hir::ViewCategory::Address) {
    addDiagnostic("operand of unsigned interpretation is not an integer "
                  "expression");
    return nullptr;
  }
  const auto byteLength =
      expression.byteLength != 0
          ? expression.byteLength
          : integerExpressionByteLength(*operand).value_or(4);
  return std::make_unique<hir::UnsignedExpr>(std::move(operand),
                                             unsignedIntegerResult(byteLength));
}

std::unique_ptr<hir::Expr>
Analyzer::analyze(const ast::IntegerCastExpr &expression) {
  if (expression.byteLength != 1 && expression.byteLength != 2 &&
      expression.byteLength != 4 && expression.byteLength != 8) {
    addDiagnostic("integer cast target length must be 1, 2, 4, or 8 bytes");
    return nullptr;
  }
  auto operand = analyze(*expression.operand);
  if (!operand) {
    return nullptr;
  }
  if (!hir::isIntegerNumeric(operand->result)) {
    addDiagnostic("integer cast operand is not an integer expression");
    return nullptr;
  }
  return std::make_unique<hir::IntegerCastExpr>(
      std::move(operand), expression.isSigned,
      fixedResult(std::string(expression.isSigned ? "i" : "u") +
                      std::to_string(expression.byteLength * 8U),
                  expression.byteLength));
}

std::unique_ptr<hir::Expr>
Analyzer::analyzeFloatOperand(const ast::Expr &expression,
                              std::size_t byteLength) {
  CurrentRangeGuard rangeGuard(*this, expression);
  if (const auto *floating =
          dynamic_cast<const ast::FloatLiteral *>(&expression)) {
    return std::make_unique<hir::FloatLiteral>(
        floating->value,
        fixedResult("f" + std::to_string(byteLength * 8U), byteLength));
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
          std::move(left), binary->op, std::move(right),
          fixedResult("f" + std::to_string(byteLength * 8U), byteLength));
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
