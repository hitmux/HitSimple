#pragma once

#include "hitsimple/compat/CCompat.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace hitsimple::compat::detail {

enum class TokenKind {
  End,
  Invalid,
  Identifier,
  Integer,
  Float,
  String,
  Character,
  Newline,
  LParen,
  RParen,
  LBrace,
  RBrace,
  LBracket,
  RBracket,
  Comma,
  Semicolon,
  Colon,
  Question,
  Dot,
  Operator,
};

struct Token {
  TokenKind kind = TokenKind::Invalid;
  std::string lexeme;
  diagnostic::SourceRange range;
};

struct LexResult {
  std::vector<Token> tokens;
  std::vector<diagnostic::Diagnostic> diagnostics;
};

LexResult lexCCompatSource(std::string_view source, std::string fileName);

class Parser {
public:
  Parser(std::vector<Token> tokens, std::string fileName);

  ParseResult parse();

private:
  const Token& current() const;
  const Token& lookAhead(std::size_t count = 1) const;
  bool at(TokenKind kind) const;
  bool at(std::string_view lexeme) const;
  bool consume(TokenKind kind);
  bool consume(std::string_view lexeme);
  bool expect(TokenKind kind, std::string_view description);
  bool expect(std::string_view lexeme, std::string_view description);
  void skipNewlines();
  bool consumeTerminator(bool required = true);
  void recoverDeclaration();
  void recoverStatement();
  void errorHere(std::string message);
  void errorAt(const Token& token, std::string message);

  bool isStorageClass() const;
  bool isTypeStart() const;
  bool isTypeStartAt(std::size_t offset) const;
  bool looksLikeDeclaration() const;
  bool looksLikeStructDefinition() const;
  bool looksLikeCast() const;

  std::optional<CType> parseType();
  std::optional<Declarator> parseDeclarator(bool allowArray = true);
  std::optional<Parameter> parseParameter();
  std::vector<Parameter> parseParameterList(bool& ok);

  std::unique_ptr<Decl> parseTopLevelDeclaration();
  std::unique_ptr<Decl> parseDeclarationAfterPrefix(StorageClass storage,
                                                     CType type);
  std::unique_ptr<StructDecl> parseStructDeclaration();
  std::unique_ptr<TypedefDecl> parseTypedefDeclaration();
  std::unique_ptr<VarDecl> parseVariableAfterPrefix(StorageClass storage,
                                                     CType type,
                                                     Declarator declarator,
                                                     bool requireTerminator);
  std::unique_ptr<FunctionDecl> parseFunctionAfterPrefix(
      StorageClass storage,
      CType returnType,
      Declarator declarator);

  std::unique_ptr<Stmt> parseStatement();
  std::unique_ptr<BlockStmt> parseBlock();
  std::unique_ptr<Stmt> parseIfStatement();
  std::unique_ptr<Stmt> parseWhileStatement();
  std::unique_ptr<Stmt> parseForStatement();
  std::unique_ptr<Stmt> parseReturnStatement();
  std::unique_ptr<Stmt> parseLocalDeclaration(bool requireTerminator = true);
  std::unique_ptr<Stmt> parseExpressionStatement(bool requireTerminator = true);
  std::unique_ptr<BlockStmt> normalizeBlock(std::unique_ptr<Stmt> statement);

  std::unique_ptr<Expr> parseExpression();
  std::unique_ptr<Expr> parseAssignmentExpression();
  std::unique_ptr<Expr> parseConditionalExpression();
  std::unique_ptr<Expr> parseBinaryExpression(int minPrecedence);
  std::unique_ptr<Expr> parseCastExpression();
  std::unique_ptr<Expr> parseUnaryExpression();
  std::unique_ptr<Expr> parsePostfixExpression();
  std::unique_ptr<Expr> parsePrimaryExpression();
  std::vector<std::unique_ptr<Expr>> parseArgumentList(bool& ok);

  static int binaryPrecedence(std::string_view op);
  static bool isAssignmentOperator(std::string_view op);

  std::vector<Token> tokens_;
  std::string fileName_;
  std::size_t cursor_ = 0;
  std::unordered_set<std::string> typedefNames_;
  std::vector<diagnostic::Diagnostic> diagnostics_;
};

} // namespace hitsimple::compat::detail
