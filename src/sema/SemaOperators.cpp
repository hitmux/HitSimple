#include "SemaAnalyzer.h"

#include "hitsimple/literal/Literal.h"

#include <charconv>
#include <cstdint>
#include <limits>

namespace hitsimple::sema {

std::size_t parseByteLength(std::string_view text) {
  if (text == "P") {
    return pointerByteLength();
  }

  std::size_t value = 0;
  const auto *begin = text.data();
  const auto *end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc() || result.ptr != end) {
    return 0;
  }
  return value;
}

std::optional<std::size_t> integerByteLengthForOperator(std::string_view op) {
  if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%" ||
      op == "**" || op == "<<" || op == ">>" || op == "&" || op == "|" ||
      op == "^") {
    return 4;
  }
  if (op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" ||
      op == "!=" || op == "&&" || op == "||") {
    return 1;
  }

  if (op.size() < 3 || op.front() != '%') {
    return std::nullopt;
  }

  const std::string_view suffixes[] = {"**", "<<", ">>", "==", "!=",
                                       "<=", ">=", "+",  "-",  "*",
                                       "/",  "%",  "&",  "|",  "^",
                                       "<",  ">"};
  std::size_t suffixLength = 0;
  for (const auto suffix : suffixes) {
    if (op.ends_with(suffix)) {
      suffixLength = suffix.size();
      break;
    }
  }
  if (suffixLength == 0) {
    return std::nullopt;
  }

  const std::size_t marker =
      op.find_last_of("du", op.size() - suffixLength - 1);
  if (marker == std::string_view::npos || marker == 0 ||
      marker + suffixLength >= op.size()) {
    return std::nullopt;
  }

  std::size_t byteLength = 4;
  if (marker > 1) {
    byteLength = parseByteLength(op.substr(1, marker - 1));
    if (byteLength == 0) {
      return std::nullopt;
    }
  }

  if (byteLength != 1 && byteLength != 2 && byteLength != 4 &&
      byteLength != 8) {
    return std::nullopt;
  }
  return byteLength;
}

std::optional<std::size_t> floatByteLengthForOperator(std::string_view op) {
  if (op.size() < 3 || op.front() != '%') {
    return std::nullopt;
  }

  const std::string_view suffixes[] = {"**", "==", "!=", "<=", ">=",
                                       "<",  ">",  "+",  "-",  "*",
                                       "/"};
  std::size_t suffixLength = 0;
  for (const auto suffix : suffixes) {
    if (op.ends_with(suffix)) {
      suffixLength = suffix.size();
      break;
    }
  }
  if (suffixLength == 0) {
    return std::nullopt;
  }

  const std::size_t marker = op.rfind('f', op.size() - suffixLength - 1);
  if (marker == std::string_view::npos || marker == 0 ||
      marker + suffixLength >= op.size()) {
    return std::nullopt;
  }

  std::size_t byteLength = 0;
  if (marker > 1) {
    byteLength = parseByteLength(op.substr(1, marker - 1));
    if (byteLength == 0) {
      return std::nullopt;
    }
  }

  if (byteLength != 0 && byteLength != 2 && byteLength != 4 &&
      byteLength != 8 && byteLength != 16) {
    return std::nullopt;
  }
  return byteLength;
}

std::optional<AssignmentOperator>
integerAssignmentOperator(std::string_view op) {
  if (op == "%d=" || op == "%u=") {
    return AssignmentOperator{4, '\0'};
  }

  if (op.size() < 4 || op.front() != '%' || op.back() != '=') {
    return std::nullopt;
  }

  const std::string_view binaryOp = op.substr(0, op.size() - 1U);
  const std::string_view suffixes[] = {"<<", ">>", "+", "-", "*", "/",
                                       "%",  "&",  "^", "|"};
  std::string_view suffix;
  for (const auto candidate : suffixes) {
    if (binaryOp.ends_with(candidate)) {
      suffix = candidate;
      break;
    }
  }
  if (suffix.empty()) {
    return std::nullopt;
  }

  const std::size_t marker = binaryOp.find_last_of(
      "du", binaryOp.size() - suffix.size() - 1U);
  if (marker == std::string_view::npos || marker == 0) {
    return std::nullopt;
  }

  std::size_t byteLength = 4;
  if (marker > 1) {
    byteLength = parseByteLength(op.substr(1, marker - 1));
    if (byteLength == 0) {
      return std::nullopt;
    }
  }

  if (byteLength != 1 && byteLength != 2 && byteLength != 4 &&
      byteLength != 8) {
    return std::nullopt;
  }
  const char compoundOp = suffix == "<<" ? '<' :
                          suffix == ">>" ? '>' : suffix.front();
  return AssignmentOperator{byteLength, compoundOp};
}

std::optional<std::size_t> floatAssignmentByteLength(std::string_view op) {
  if (op == "%f=") {
    return 0;
  }
  if (op.size() < 4 || op.front() != '%' || op.back() != '=') {
    return std::nullopt;
  }

  const std::size_t marker = op.rfind('f', op.size() - 2);
  if (marker == std::string_view::npos || marker == 0 ||
      marker + 1 != op.size() - 1) {
    return std::nullopt;
  }

  const auto byteLength = parseByteLength(op.substr(1, marker - 1));
  if (byteLength != 2 && byteLength != 4 && byteLength != 8 &&
      byteLength != 16) {
    return std::nullopt;
  }
  return byteLength;
}

bool isDivisionOperator(std::string_view op) {
  return op.ends_with('/') || op.ends_with('%');
}

bool isDivisionOperator(char op) { return op == '/' || op == '%'; }

std::string compoundBinaryOperator(std::string_view assignmentOp) {
  std::string binaryOp(assignmentOp.substr(0, assignmentOp.size() - 1));
  return binaryOp;
}

namespace {

bool isFloatCallExpression(const hir::Expr &expression) {
  const auto *call = dynamic_cast<const hir::CallExpr *>(&expression);
  return call != nullptr && call->isFloating;
}

bool isFloatTemplateView(const hir::Expr &expression) {
  const auto *view = dynamic_cast<const hir::TemplateViewExpr *>(&expression);
  if (view == nullptr) {
    return false;
  }
  return view->templateName == "f16" || view->templateName == "f32" ||
         view->templateName == "f64" || view->templateName == "f128";
}

} // namespace

bool isIntegerExpression(const hir::Expr &expression) {
  if (isFloatCallExpression(expression) || isFloatTemplateView(expression)) {
    return false;
  }
  return dynamic_cast<const hir::IntegerLiteral *>(&expression) != nullptr ||
         dynamic_cast<const hir::VariableRef *>(&expression) != nullptr ||
         dynamic_cast<const hir::BinaryExpr *>(&expression) != nullptr ||
         dynamic_cast<const hir::UnaryExpr *>(&expression) != nullptr ||
         dynamic_cast<const hir::TernaryExpr *>(&expression) != nullptr ||
         dynamic_cast<const hir::UnsignedExpr *>(&expression) != nullptr ||
         dynamic_cast<const hir::IntegerCastExpr *>(&expression) != nullptr ||
         dynamic_cast<const hir::TemplateViewExpr *>(&expression) != nullptr ||
         dynamic_cast<const hir::UserTemplateOpCallExpr *>(&expression) !=
             nullptr ||
         dynamic_cast<const hir::UserTemplateFormatCallExpr *>(&expression) !=
             nullptr ||
         dynamic_cast<const hir::AddressOfExpr *>(&expression) != nullptr ||
         dynamic_cast<const hir::DerefExpr *>(&expression) != nullptr ||
         dynamic_cast<const hir::AssignmentExpr *>(&expression) != nullptr ||
         dynamic_cast<const hir::CallExpr *>(&expression) != nullptr ||
         dynamic_cast<const hir::DynamicByteViewExpr *>(&expression) !=
             nullptr ||
         dynamic_cast<const hir::ByteSwapExpr *>(&expression) != nullptr ||
         dynamic_cast<const hir::FloatCompareExpr *>(&expression) != nullptr ||
         dynamic_cast<const hir::ToIntExpr *>(&expression) != nullptr;
}

bool hasRuntimeDynamicView(const hir::Expr &expression) {
  const auto *view = dynamic_cast<const hir::DynamicByteViewExpr *>(&expression);
  if (view == nullptr) {
    return false;
  }
  if (view->operation != hir::DynamicByteViewOperation::ResizeBytes ||
      view->runtimeLength == nullptr) {
    return true;
  }
  return dynamic_cast<const hir::IntegerLiteral *>(view->runtimeLength.get()) ==
         nullptr;
}

bool isUnsignedExpression(const hir::Expr &expression) {
  if (const auto *cast = dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
    return !cast->isSigned;
  }
  return dynamic_cast<const hir::UnsignedExpr *>(&expression) != nullptr;
}

bool isFloatExpression(const hir::Expr &expression) {
  return dynamic_cast<const hir::FloatLiteral *>(&expression) != nullptr ||
         dynamic_cast<const hir::VariableRef *>(&expression) != nullptr ||
         dynamic_cast<const hir::FloatBinaryExpr *>(&expression) != nullptr ||
         dynamic_cast<const hir::ToFloatExpr *>(&expression) != nullptr ||
         isFloatTemplateView(expression) ||
         isFloatCallExpression(expression);
}

std::optional<std::size_t>
integerExpressionByteLength(const hir::Expr &expression) {
  if (const auto *integer =
          dynamic_cast<const hir::IntegerLiteral *>(&expression)) {
    return integer->byteLength;
  }
  if (const auto *variable =
          dynamic_cast<const hir::VariableRef *>(&expression)) {
    return variable->byteLength;
  }
  if (const auto *binary = dynamic_cast<const hir::BinaryExpr *>(&expression)) {
    return binary->byteLength;
  }
  if (dynamic_cast<const hir::FloatCompareExpr *>(&expression) != nullptr) {
    return 1;
  }
  if (const auto *unary = dynamic_cast<const hir::UnaryExpr *>(&expression)) {
    return unary->byteLength;
  }
  if (const auto *ternary =
          dynamic_cast<const hir::TernaryExpr *>(&expression)) {
    return ternary->byteLength;
  }
  if (const auto *unsignedExpr =
          dynamic_cast<const hir::UnsignedExpr *>(&expression)) {
    return unsignedExpr->byteLength;
  }
  if (const auto *cast =
          dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
    return cast->byteLength;
  }
  if (const auto *view = dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return view->byteLength;
  }
  if (const auto *call =
          dynamic_cast<const hir::UserTemplateOpCallExpr *>(&expression)) {
    return call->byteLength;
  }
  if (const auto *call =
          dynamic_cast<const hir::UserTemplateFormatCallExpr *>(&expression)) {
    return call->byteLength;
  }
  if (const auto *address =
          dynamic_cast<const hir::AddressOfExpr *>(&expression)) {
    return address->byteLength;
  }
  if (const auto *deref = dynamic_cast<const hir::DerefExpr *>(&expression)) {
    return deref->byteLength;
  }
  if (const auto *conversion =
          dynamic_cast<const hir::ToIntExpr *>(&expression)) {
    return conversion->byteLength;
  }
  if (const auto *assignment =
          dynamic_cast<const hir::AssignmentExpr *>(&expression)) {
    return assignment->byteLength;
  }
  if (const auto *call = dynamic_cast<const hir::CallExpr *>(&expression)) {
    return call->byteLength;
  }
  if (const auto *view =
          dynamic_cast<const hir::DynamicByteViewExpr *>(&expression)) {
    if (view->operation == hir::DynamicByteViewOperation::ResizeBytes &&
        view->runtimeLength != nullptr) {
      const auto *length = dynamic_cast<const hir::IntegerLiteral *>(
          view->runtimeLength.get());
      if (length != nullptr) {
        const auto parsed = literal::parseUnsignedIntegerLiteral(length->value);
        if (parsed && *parsed <= std::numeric_limits<std::size_t>::max()) {
          return static_cast<std::size_t>(*parsed);
        }
      }
    }
  }
  if (const auto *swap = dynamic_cast<const hir::ByteSwapExpr *>(&expression)) {
    return swap->byteLength;
  }
  return std::nullopt;
}

std::optional<std::size_t>
floatExpressionByteLength(const hir::Expr &expression) {
  if (const auto *floating =
          dynamic_cast<const hir::FloatLiteral *>(&expression)) {
    return floating->byteLength;
  }
  if (const auto *variable =
          dynamic_cast<const hir::VariableRef *>(&expression)) {
    return variable->byteLength;
  }
  if (const auto *binary =
          dynamic_cast<const hir::FloatBinaryExpr *>(&expression)) {
    return binary->byteLength;
  }
  if (const auto *conversion =
          dynamic_cast<const hir::ToFloatExpr *>(&expression)) {
    return conversion->byteLength;
  }
  if (const auto *view = dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return isFloatTemplateView(expression)
               ? std::optional<std::size_t>{view->byteLength}
               : std::nullopt;
  }
  if (isFloatCallExpression(expression)) {
    return dynamic_cast<const hir::CallExpr *>(&expression)->byteLength;
  }
  return std::nullopt;
}

bool integerFits(const ast::IntegerLiteral &integer, std::size_t byteLength) {
  if (byteLength == 0 || byteLength > 8) {
    return false;
  }

  const auto parsed = literal::parseUnsignedIntegerLiteral(integer.value);
  if (!parsed) {
    return false;
  }
  const auto value = *parsed;

  if (byteLength == 8) {
    return value <=
           static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  }

  const std::uint64_t maxValue =
      (std::uint64_t{1} << (byteLength * 8U - 1U)) - 1U;
  return value <= maxValue;
}

std::size_t inferIntegerLiteralByteLength(const ast::IntegerLiteral &integer) {
  const auto parsed = literal::parseUnsignedIntegerLiteral(integer.value);
  if (!parsed) {
    return 0;
  }
  const auto value = *parsed;

  if (value <= 0x7fU) {
    return 1;
  }
  if (value <= 0x7fffU) {
    return 2;
  }
  if (value <= 0x7fffffffU) {
    return 4;
  }
  if (value > static_cast<std::uint64_t>(
                  std::numeric_limits<std::int64_t>::max())) {
    return 0;
  }
  return 8;
}

std::size_t inferStringLiteralByteLength(std::string_view text) {
  const auto decoded = literal::decodeStringLiteral(text);
  if (!decoded) {
    return 0;
  }
  return decoded.bytes.size() + 1;
}

std::size_t pointerByteLength() { return sizeof(void *); }

} // namespace hitsimple::sema
