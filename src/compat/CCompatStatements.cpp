#include "CCompatInternal.h"

#include <utility>

namespace hitsimple::compat::detail {

std::unique_ptr<BlockStmt> Parser::normalizeBlock(std::unique_ptr<Stmt> statement) {
  if (!statement) {
    return nullptr;
  }
  if (dynamic_cast<BlockStmt*>(statement.get()) != nullptr) {
    return std::unique_ptr<BlockStmt>(static_cast<BlockStmt*>(statement.release()));
  }
  std::vector<std::unique_ptr<Stmt>> statements;
  statements.push_back(std::move(statement));
  return std::make_unique<BlockStmt>(std::move(statements));
}

std::unique_ptr<BlockStmt> Parser::parseBlock() {
  const auto begin = current().range.begin;
  if (!expect(TokenKind::LBrace, "'{' to start block")) {
    return nullptr;
  }
  std::vector<std::unique_ptr<Stmt>> statements;
  skipNewlines();
  while (!at(TokenKind::End) && !at(TokenKind::RBrace)) {
    const auto before = cursor_;
    auto statement = parseStatement();
    if (statement) {
      statements.push_back(std::move(statement));
    }
    if (cursor_ == before) {
      errorHere("unable to recover while parsing C statement");
      ++cursor_;
    }
    skipNewlines();
  }
  if (!expect(TokenKind::RBrace, "'}' to close block")) {
    return nullptr;
  }
  return std::make_unique<BlockStmt>(
      std::move(statements), diagnostic::SourceRange{begin, current().range.begin});
}

std::unique_ptr<Stmt> Parser::parseIfStatement() {
  const auto begin = current().range.begin;
  consume("if");
  if (!expect(TokenKind::LParen, "'(' after if")) {
    return nullptr;
  }
  skipNewlines();
  auto condition = parseExpression();
  if (!condition || !expect(TokenKind::RParen, "')' after if condition")) {
    return nullptr;
  }
  skipNewlines();
  auto thenBranch = parseStatement();
  if (!thenBranch) {
    return nullptr;
  }
  std::unique_ptr<Stmt> elseBranch;
  skipNewlines();
  if (consume("else")) {
    skipNewlines();
    elseBranch = parseStatement();
    if (!elseBranch) {
      return nullptr;
    }
  }
  return std::make_unique<IfStmt>(
      std::move(condition), std::move(thenBranch), std::move(elseBranch),
      diagnostic::SourceRange{begin, current().range.begin});
}

std::unique_ptr<Stmt> Parser::parseWhileStatement() {
  const auto begin = current().range.begin;
  consume("while");
  if (!expect(TokenKind::LParen, "'(' after while")) {
    return nullptr;
  }
  skipNewlines();
  auto condition = parseExpression();
  if (!condition || !expect(TokenKind::RParen, "')' after while condition")) {
    return nullptr;
  }
  skipNewlines();
  auto body = parseStatement();
  if (!body) {
    return nullptr;
  }
  return std::make_unique<WhileStmt>(
      std::move(condition), std::move(body),
      diagnostic::SourceRange{begin, current().range.begin});
}

std::unique_ptr<Stmt> Parser::parseForStatement() {
  const auto begin = current().range.begin;
  consume("for");
  if (!expect(TokenKind::LParen, "'(' after for")) {
    return nullptr;
  }
  skipNewlines();
  std::unique_ptr<Stmt> init;
  if (!at(TokenKind::Semicolon)) {
    if (looksLikeDeclaration()) {
      init = parseLocalDeclaration(false);
    } else {
      init = parseExpressionStatement(false);
    }
    if (!init) {
      return nullptr;
    }
  }
  if (!expect(TokenKind::Semicolon, "';' after for initializer")) {
    return nullptr;
  }
  skipNewlines();
  std::unique_ptr<Expr> condition;
  if (!at(TokenKind::Semicolon)) {
    condition = parseExpression();
    if (!condition) {
      return nullptr;
    }
  }
  if (!expect(TokenKind::Semicolon, "';' after for condition")) {
    return nullptr;
  }
  skipNewlines();
  std::unique_ptr<Expr> post;
  if (!at(TokenKind::RParen)) {
    post = parseExpression();
    if (!post) {
      return nullptr;
    }
  }
  if (!expect(TokenKind::RParen, "')' after for clause")) {
    return nullptr;
  }
  skipNewlines();
  auto body = parseStatement();
  if (!body) {
    return nullptr;
  }
  return std::make_unique<ForStmt>(
      std::move(init), std::move(condition), std::move(post), std::move(body),
      diagnostic::SourceRange{begin, current().range.begin});
}

std::unique_ptr<Stmt> Parser::parseReturnStatement() {
  const auto begin = current().range.begin;
  consume("return");
  std::unique_ptr<Expr> value;
  if (!at(TokenKind::Semicolon) && !at(TokenKind::Newline) &&
      !at(TokenKind::RBrace)) {
    value = parseExpression();
    if (!value) {
      return nullptr;
    }
  }
  if (!consumeTerminator()) {
    return nullptr;
  }
  return std::make_unique<ReturnStmt>(
      std::move(value), diagnostic::SourceRange{begin, current().range.begin});
}

std::unique_ptr<Stmt> Parser::parseLocalDeclaration(bool requireTerminator) {
  const auto begin = current().range.begin;
  if (at("typedef")) {
    errorHere("typedef declarations are only supported at translation-unit scope");
    return nullptr;
  }
  StorageClass storage = StorageClass::None;
  if (isStorageClass()) {
    storage = at("static") ? StorageClass::Static : StorageClass::Extern;
    ++cursor_;
  }
  auto type = parseType();
  if (!type) {
    return nullptr;
  }
  auto declarator = parseDeclarator();
  if (!declarator || at(TokenKind::LParen)) {
    if (at(TokenKind::LParen)) {
      errorHere("nested C function declarations are not supported");
    }
    return nullptr;
  }
  auto declaration = parseVariableAfterPrefix(storage, std::move(*type),
                                              std::move(*declarator),
                                              requireTerminator);
  if (!declaration) {
    return nullptr;
  }
  return std::make_unique<DeclStmt>(
      std::move(declaration), diagnostic::SourceRange{begin, current().range.begin});
}

std::unique_ptr<Stmt> Parser::parseExpressionStatement(bool requireTerminator) {
  const auto begin = current().range.begin;
  auto expression = parseExpression();
  if (!expression) {
    return nullptr;
  }
  if (!consumeTerminator(requireTerminator)) {
    return nullptr;
  }
  return std::make_unique<ExprStmt>(
      std::move(expression), diagnostic::SourceRange{begin, current().range.begin});
}

std::unique_ptr<Stmt> Parser::parseStatement() {
  if (consume(TokenKind::Semicolon) || consume(TokenKind::Newline)) {
    return std::make_unique<EmptyStmt>();
  }
  if (at(TokenKind::LBrace)) {
    return parseBlock();
  }
  if (at("if")) {
    return parseIfStatement();
  }
  if (at("while")) {
    return parseWhileStatement();
  }
  if (at("for")) {
    return parseForStatement();
  }
  if (at("return")) {
    return parseReturnStatement();
  }
  if (at("break")) {
    const auto begin = current().range.begin;
    ++cursor_;
    if (!consumeTerminator()) {
      return nullptr;
    }
    return std::make_unique<BreakStmt>(diagnostic::SourceRange{begin,
                                                                 current().range.begin});
  }
  if (at("continue")) {
    const auto begin = current().range.begin;
    ++cursor_;
    if (!consumeTerminator()) {
      return nullptr;
    }
    return std::make_unique<ContinueStmt>(diagnostic::SourceRange{begin,
                                                                    current().range.begin});
  }
  if (at("goto")) {
    const auto begin = current().range.begin;
    ++cursor_;
    if (current().kind != TokenKind::Identifier) {
      errorHere("expected label after goto");
      return nullptr;
    }
    std::string label = current().lexeme;
    ++cursor_;
    if (!consumeTerminator()) {
      return nullptr;
    }
    return std::make_unique<GotoStmt>(
        std::move(label), diagnostic::SourceRange{begin, current().range.begin});
  }
  if (current().kind == TokenKind::Identifier && lookAhead(1).kind == TokenKind::Colon) {
    const auto begin = current().range.begin;
    std::string label = current().lexeme;
    ++cursor_;
    ++cursor_;
    skipNewlines();
    auto statement = parseStatement();
    if (!statement) {
      return nullptr;
    }
    return std::make_unique<LabelStmt>(
        std::move(label), std::move(statement),
        diagnostic::SourceRange{begin, current().range.begin});
  }
  if (at("do") || at("switch") || at("case") || at("default")) {
    errorHere("C statement is outside the Standard 16.1 compatibility subset");
    return nullptr;
  }
  if (looksLikeDeclaration()) {
    return parseLocalDeclaration();
  }
  return parseExpressionStatement();
}

} // namespace hitsimple::compat::detail
