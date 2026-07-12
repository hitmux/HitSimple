#include "CCompatInternal.h"

#include <utility>

namespace hitsimple::compat::detail {
namespace {

bool isUnaryOperator(std::string_view op) {
  return op == "*" || op == "&" || op == "!" || op == "~" || op == "-" ||
         op == "+";
}

} // namespace

bool Parser::looksLikeCast() const {
  if (!at(TokenKind::LParen)) {
    return false;
  }
  std::size_t index = 1;
  while (lookAhead(index).kind == TokenKind::Identifier &&
         (lookAhead(index).lexeme == "const" ||
          lookAhead(index).lexeme == "volatile")) {
    ++index;
  }
  const auto& first = lookAhead(index);
  if (first.kind != TokenKind::Identifier) {
    return false;
  }
  if (first.lexeme == "struct") {
    if (lookAhead(index + 1U).kind != TokenKind::Identifier) {
      return false;
    }
    index += 2U;
  } else if (first.lexeme == "signed") {
    if (lookAhead(index + 1U).lexeme != "char") {
      return false;
    }
    index += 2U;
  } else if (first.lexeme == "unsigned") {
    const auto second = lookAhead(index + 1U).lexeme;
    if (second != "char" && second != "short" && second != "int" &&
        second != "long") {
      return false;
    }
    index += 2U;
    if (second == "long" && lookAhead(index).lexeme == "long") {
      ++index;
    }
    if ((second == "long" || second == "short") &&
        lookAhead(index).lexeme == "int") {
      ++index;
    }
  } else if (first.lexeme == "char" || first.lexeme == "short" ||
             first.lexeme == "int" || first.lexeme == "long" ||
             first.lexeme == "float" || first.lexeme == "double" ||
             first.lexeme == "void" || typedefNames_.contains(first.lexeme)) {
    ++index;
    if (first.lexeme == "long" && lookAhead(index).lexeme == "long") {
      ++index;
    }
    if ((first.lexeme == "long" || first.lexeme == "short") &&
        lookAhead(index).lexeme == "int") {
      ++index;
    }
  } else {
    return false;
  }
  while (lookAhead(index).lexeme == "*") {
    ++index;
  }
  return lookAhead(index).kind == TokenKind::RParen;
}

int Parser::binaryPrecedence(std::string_view op) {
  if (op == "||") {
    return 1;
  }
  if (op == "&&") {
    return 2;
  }
  if (op == "|") {
    return 3;
  }
  if (op == "^") {
    return 4;
  }
  if (op == "&") {
    return 5;
  }
  if (op == "==" || op == "!=") {
    return 6;
  }
  if (op == "<" || op == "<=" || op == ">" || op == ">=") {
    return 7;
  }
  if (op == "<<" || op == ">>") {
    return 8;
  }
  if (op == "+" || op == "-") {
    return 9;
  }
  if (op == "*" || op == "/" || op == "%") {
    return 10;
  }
  return -1;
}

bool Parser::isAssignmentOperator(std::string_view op) {
  return op == "=" || op == "+=" || op == "-=" || op == "*=" ||
         op == "/=" || op == "%=" || op == "<<=" || op == ">>=" ||
         op == "&=" || op == "^=" || op == "|=";
}

std::unique_ptr<Expr> Parser::parseExpression() {
  return parseAssignmentExpression();
}

std::unique_ptr<Expr> Parser::parseAssignmentExpression() {
  auto left = parseConditionalExpression();
  if (!left) {
    return nullptr;
  }
  if (current().kind == TokenKind::Operator &&
      isAssignmentOperator(current().lexeme)) {
    const auto begin = left->range.begin;
    std::string op = current().lexeme;
    ++cursor_;
    auto right = parseAssignmentExpression();
    if (!right) {
      return nullptr;
    }
    return std::make_unique<AssignmentExpr>(
        std::move(left), std::move(op), std::move(right),
        diagnostic::SourceRange{begin, right->range.end});
  }
  return left;
}

std::unique_ptr<Expr> Parser::parseConditionalExpression() {
  auto condition = parseBinaryExpression(1);
  if (!condition) {
    return nullptr;
  }
  if (!consume(TokenKind::Question)) {
    return condition;
  }
  auto thenExpr = parseExpression();
  if (!thenExpr || !expect(TokenKind::Colon, "':' in conditional expression")) {
    return nullptr;
  }
  auto elseExpr = parseConditionalExpression();
  if (!elseExpr) {
    return nullptr;
  }
  return std::make_unique<ConditionalExpr>(
      std::move(condition), std::move(thenExpr), std::move(elseExpr));
}

std::unique_ptr<Expr> Parser::parseBinaryExpression(int minPrecedence) {
  auto left = parseCastExpression();
  if (!left) {
    return nullptr;
  }
  while (current().kind == TokenKind::Operator) {
    const int precedence = binaryPrecedence(current().lexeme);
    if (precedence < minPrecedence) {
      break;
    }
    const auto begin = left->range.begin;
    std::string op = current().lexeme;
    ++cursor_;
    auto right = parseBinaryExpression(precedence + 1);
    if (!right) {
      return nullptr;
    }
    left = std::make_unique<BinaryExpr>(
        std::move(left), std::move(op), std::move(right),
        diagnostic::SourceRange{begin, right->range.end});
  }
  return left;
}

std::unique_ptr<Expr> Parser::parseCastExpression() {
  if (!looksLikeCast()) {
    return parseUnaryExpression();
  }
  const auto begin = current().range.begin;
  consume(TokenKind::LParen);
  auto type = parseType();
  if (!type) {
    return nullptr;
  }
  std::size_t pointerDepth = 0;
  while (consume("*")) {
    ++pointerDepth;
  }
  if (!expect(TokenKind::RParen, "')' after cast type")) {
    return nullptr;
  }
  auto operand = parseCastExpression();
  if (!operand) {
    return nullptr;
  }
  return std::make_unique<CastExpr>(
      std::move(*type), pointerDepth, std::move(operand),
      diagnostic::SourceRange{begin, operand->range.end});
}

std::unique_ptr<Expr> Parser::parseUnaryExpression() {
  if (current().kind == TokenKind::Operator && isUnaryOperator(current().lexeme)) {
    const auto begin = current().range.begin;
    std::string op = current().lexeme;
    ++cursor_;
    auto operand = parseUnaryExpression();
    if (!operand) {
      return nullptr;
    }
    return std::make_unique<UnaryExpr>(
        std::move(op), std::move(operand),
        diagnostic::SourceRange{begin, operand->range.end});
  }
  return parsePostfixExpression();
}

std::vector<std::unique_ptr<Expr>> Parser::parseArgumentList(bool& ok) {
  ok = expect(TokenKind::LParen, "'(' before argument list");
  std::vector<std::unique_ptr<Expr>> arguments;
  skipNewlines();
  if (consume(TokenKind::RParen)) {
    return arguments;
  }
  while (!at(TokenKind::End)) {
    auto argument = parseExpression();
    if (!argument) {
      ok = false;
      return arguments;
    }
    arguments.push_back(std::move(argument));
    skipNewlines();
    if (consume(TokenKind::RParen)) {
      return arguments;
    }
    if (!expect(TokenKind::Comma, "',' between call arguments")) {
      ok = false;
      return arguments;
    }
    skipNewlines();
  }
  errorHere("unterminated call argument list");
  ok = false;
  return arguments;
}

std::unique_ptr<Expr> Parser::parsePostfixExpression() {
  auto expression = parsePrimaryExpression();
  if (!expression) {
    return nullptr;
  }
  while (true) {
    if (consume(TokenKind::LBracket)) {
      const auto begin = expression->range.begin;
      auto index = parseExpression();
      if (!index || !expect(TokenKind::RBracket, "']' after subscript")) {
        return nullptr;
      }
      expression = std::make_unique<IndexExpr>(
          std::move(expression), std::move(index),
          diagnostic::SourceRange{begin, current().range.begin});
      continue;
    }
    if (at(TokenKind::LParen)) {
      const auto begin = expression->range.begin;
      bool argumentsOk = false;
      auto arguments = parseArgumentList(argumentsOk);
      if (!argumentsOk) {
        return nullptr;
      }
      expression = std::make_unique<CallExpr>(
          std::move(expression), std::move(arguments),
          diagnostic::SourceRange{begin, current().range.begin});
      continue;
    }
    if (consume(TokenKind::Dot) || consume("->")) {
      const bool throughPointer = tokens_[cursor_ - 1U].lexeme == "->";
      if (current().kind != TokenKind::Identifier) {
        errorHere("expected member name after '.' or '->'");
        return nullptr;
      }
      const auto begin = expression->range.begin;
      std::string member = current().lexeme;
      ++cursor_;
      expression = std::make_unique<MemberExpr>(
          std::move(expression), std::move(member), throughPointer,
          diagnostic::SourceRange{begin, current().range.begin});
      continue;
    }
    if (at("++") || at("--")) {
      errorHere("C increment and decrement expressions are outside the Standard 16.1 subset");
      return nullptr;
    }
    return expression;
  }
}

std::unique_ptr<Expr> Parser::parsePrimaryExpression() {
  const Token token = current();
  if (consume(TokenKind::Identifier)) {
    if (token.lexeme == "sizeof") {
      if (!expect(TokenKind::LParen, "'(' after sizeof")) {
        return nullptr;
      }
      if (isTypeStart() &&
          (current().lexeme != "struct" || lookAhead(1).kind == TokenKind::Identifier)) {
        auto type = parseType();
        if (!type || !expect(TokenKind::RParen, "')' after sizeof type")) {
          return nullptr;
        }
        return std::make_unique<SizeofExpr>(
            std::move(*type), diagnostic::SourceRange{token.range.begin,
                                                       current().range.begin});
      }
      if (current().kind != TokenKind::Identifier) {
        errorHere("sizeof expects a C type or identifier");
        return nullptr;
      }
      std::string identifier = current().lexeme;
      ++cursor_;
      if (!expect(TokenKind::RParen, "')' after sizeof identifier")) {
        return nullptr;
      }
      return std::make_unique<SizeofExpr>(
          std::move(identifier), diagnostic::SourceRange{token.range.begin,
                                                         current().range.begin});
    }
    return std::make_unique<IdentifierExpr>(token.lexeme, token.range);
  }
  if (consume(TokenKind::Integer)) {
    return std::make_unique<IntegerLiteralExpr>(token.lexeme, token.range);
  }
  if (consume(TokenKind::Float)) {
    return std::make_unique<FloatLiteralExpr>(token.lexeme, token.range);
  }
  if (consume(TokenKind::String)) {
    return std::make_unique<StringLiteralExpr>(token.lexeme, token.range);
  }
  if (consume(TokenKind::Character)) {
    return std::make_unique<CharLiteralExpr>(token.lexeme, token.range);
  }
  if (consume(TokenKind::LParen)) {
    skipNewlines();
    auto expression = parseExpression();
    if (!expression || !expect(TokenKind::RParen, "')' after expression")) {
      return nullptr;
    }
    return expression;
  }
  errorHere("expected C compatibility expression");
  return nullptr;
}

} // namespace hitsimple::compat::detail
