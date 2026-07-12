#include "CCompatInternal.h"

#include <limits>
#include <utility>

namespace hitsimple::compat::detail {
namespace {

bool isQualifier(std::string_view text) {
  return text == "const" || text == "volatile";
}

bool isBuiltinType(std::string_view text) {
  return text == "char" || text == "signed" || text == "unsigned" ||
         text == "short" || text == "int" || text == "long" ||
         text == "float" || text == "double" || text == "void";
}

std::optional<std::size_t> parseArrayCount(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }

  std::size_t offset = 0;
  unsigned int base = 10;
  if (text.size() >= 2U && text[0] == '0') {
    const char prefix = text[1];
    if (prefix == 'x' || prefix == 'X') {
      base = 16;
      offset = 2;
    } else if (prefix == 'o' || prefix == 'O') {
      base = 8;
      offset = 2;
    } else if (prefix == 'b' || prefix == 'B') {
      base = 2;
      offset = 2;
    }
  }

  std::size_t value = 0;
  bool sawDigit = false;
  bool previousUnderscore = false;
  for (; offset < text.size(); ++offset) {
    const char ch = text[offset];
    if (ch == '_') {
      if (!sawDigit || previousUnderscore) {
        return std::nullopt;
      }
      previousUnderscore = true;
      continue;
    }

    unsigned int digit = 0;
    if (ch >= '0' && ch <= '9') {
      digit = static_cast<unsigned int>(ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
      digit = static_cast<unsigned int>(ch - 'a') + 10U;
    } else if (ch >= 'A' && ch <= 'F') {
      digit = static_cast<unsigned int>(ch - 'A') + 10U;
    } else {
      return std::nullopt;
    }
    if (digit >= base ||
        value > (std::numeric_limits<std::size_t>::max() - digit) / base) {
      return std::nullopt;
    }
    value = value * base + digit;
    sawDigit = true;
    previousUnderscore = false;
  }
  if (!sawDigit || previousUnderscore) {
    return std::nullopt;
  }
  return value;
}

} // namespace

Parser::Parser(std::vector<Token> tokens, std::string fileName)
    : tokens_(std::move(tokens)), fileName_(std::move(fileName)) {}

const Token& Parser::current() const { return lookAhead(0); }

const Token& Parser::lookAhead(std::size_t count) const {
  const auto index = cursor_ + count;
  return tokens_[index < tokens_.size() ? index : tokens_.size() - 1U];
}

bool Parser::at(TokenKind kind) const { return current().kind == kind; }

bool Parser::at(std::string_view lexeme) const {
  return current().lexeme == lexeme;
}

bool Parser::consume(TokenKind kind) {
  if (!at(kind)) {
    return false;
  }
  ++cursor_;
  return true;
}

bool Parser::consume(std::string_view lexeme) {
  if (!at(lexeme)) {
    return false;
  }
  ++cursor_;
  return true;
}

bool Parser::expect(TokenKind kind, std::string_view description) {
  if (consume(kind)) {
    return true;
  }
  errorHere("expected " + std::string(description));
  return false;
}

bool Parser::expect(std::string_view lexeme, std::string_view description) {
  if (consume(lexeme)) {
    return true;
  }
  errorHere("expected " + std::string(description));
  return false;
}

void Parser::skipNewlines() {
  while (consume(TokenKind::Newline)) {
  }
}

bool Parser::consumeTerminator(bool required) {
  if (consume(TokenKind::Semicolon)) {
    skipNewlines();
    return true;
  }
  if (at(TokenKind::Newline)) {
    skipNewlines();
    return true;
  }
  if (!required || at(TokenKind::RBrace) || at(TokenKind::End)) {
    return true;
  }
  errorHere("expected ';' or newline after declaration or statement");
  return false;
}

void Parser::recoverDeclaration() {
  while (!at(TokenKind::End) && !at(TokenKind::Semicolon) &&
         !at(TokenKind::Newline) && !at(TokenKind::RBrace)) {
    ++cursor_;
  }
  consumeTerminator(false);
}

void Parser::recoverStatement() { recoverDeclaration(); }

void Parser::errorHere(std::string message) { errorAt(current(), std::move(message)); }

void Parser::errorAt(const Token& token, std::string message) {
  auto diagnostic = diagnostic::Diagnostic::error(diagnostic::Stage::Parser,
                                                   std::move(message));
  diagnostic.range = token.range;
  diagnostics_.push_back(std::move(diagnostic));
}

bool Parser::isStorageClass() const {
  return at("static") || at("extern");
}

bool Parser::isTypeStartAt(std::size_t offset) const {
  const auto& token = lookAhead(offset);
  if (token.kind != TokenKind::Identifier) {
    return false;
  }
  if (isQualifier(token.lexeme) || isBuiltinType(token.lexeme) ||
      token.lexeme == "struct") {
    return true;
  }
  if (typedefNames_.contains(token.lexeme)) {
    return true;
  }
  return lookAhead(offset + 1U).kind == TokenKind::Identifier;
}

bool Parser::isTypeStart() const { return isTypeStartAt(0); }

bool Parser::looksLikeDeclaration() const {
  return isStorageClass() || at("typedef") || isTypeStart();
}

bool Parser::looksLikeStructDefinition() const {
  return at("struct") && lookAhead(1).kind == TokenKind::Identifier &&
         lookAhead(2).kind == TokenKind::LBrace;
}

std::optional<CType> Parser::parseType() {
  const auto begin = current().range.begin;
  CType type;
  while (at(TokenKind::Identifier) && isQualifier(current().lexeme)) {
    if (current().lexeme == "const") {
      type.isConst = true;
    } else {
      type.isVolatile = true;
    }
    ++cursor_;
  }
  if (current().kind != TokenKind::Identifier) {
    errorHere("expected C type");
    return std::nullopt;
  }

  const std::string base = current().lexeme;
  ++cursor_;
  const auto expectSecond = [&](std::string_view expected) -> bool {
    if (at(expected)) {
      ++cursor_;
      return true;
    }
    errorHere("expected '" + std::string(expected) + "' after '" + base + "'");
    return false;
  };
  if (base == "char") {
    type.base = BaseType::Char;
  } else if (base == "signed") {
    if (!expectSecond("char")) {
      return std::nullopt;
    }
    type.base = BaseType::SignedChar;
  } else if (base == "unsigned") {
    if (at("char")) {
      ++cursor_;
      type.base = BaseType::UnsignedChar;
    } else if (at("short")) {
      ++cursor_;
      type.base = BaseType::UnsignedShort;
      if (at("int")) {
        ++cursor_;
      }
    } else if (at("int")) {
      ++cursor_;
      type.base = BaseType::UnsignedInt;
    } else if (at("long")) {
      ++cursor_;
      if (at("long")) {
        ++cursor_;
        type.base = BaseType::UnsignedLongLong;
      } else {
        type.base = BaseType::UnsignedLong;
      }
      if (at("int")) {
        ++cursor_;
      }
    } else {
      errorHere("expected C integer type after 'unsigned'");
      return std::nullopt;
    }
  } else if (base == "short") {
    type.base = BaseType::Short;
    if (at("int")) {
      ++cursor_;
    }
  } else if (base == "int") {
    type.base = BaseType::Int;
  } else if (base == "long") {
    if (at("long")) {
      ++cursor_;
      type.base = BaseType::LongLong;
    } else {
      type.base = BaseType::Long;
    }
    if (at("int")) {
      ++cursor_;
    }
  } else if (base == "float") {
    type.base = BaseType::Float;
  } else if (base == "double") {
    type.base = BaseType::Double;
  } else if (base == "void") {
    type.base = BaseType::Void;
  } else if (base == "struct") {
    if (current().kind != TokenKind::Identifier) {
      errorHere("expected struct name after 'struct'");
      return std::nullopt;
    }
    type.base = BaseType::Struct;
    type.name = current().lexeme;
    ++cursor_;
  } else {
    type.base = BaseType::TypedefName;
    type.name = base;
  }
  type.range = {begin, lookAhead(0).range.begin};
  return type;
}

std::optional<Declarator> Parser::parseDeclarator(bool allowArray) {
  const auto begin = current().range.begin;
  Declarator declarator;
  while (consume("*")) {
    ++declarator.pointerDepth;
  }
  if (at(TokenKind::LParen)) {
    errorHere("parenthesized C declarators are not supported");
    return std::nullopt;
  }
  if (current().kind != TokenKind::Identifier) {
    errorHere("expected identifier in C declarator");
    return std::nullopt;
  }
  declarator.name = current().lexeme;
  ++cursor_;
  if (consume(TokenKind::LBracket)) {
    if (!allowArray) {
      errorHere("array declarator is not valid here");
      return std::nullopt;
    }
    if (current().kind != TokenKind::Integer) {
      errorHere("array declarator requires an integer element count");
      return std::nullopt;
    }
    const auto count = parseArrayCount(current().lexeme);
    if (!count) {
      errorHere("array declarator requires a decimal integer element count");
      return std::nullopt;
    }
    declarator.arrayCount = *count;
    ++cursor_;
    if (!expect(TokenKind::RBracket, "']' after array element count")) {
      return std::nullopt;
    }
    if (at(TokenKind::LBracket)) {
      errorHere("multidimensional C arrays are not supported");
      return std::nullopt;
    }
  }
  declarator.range = {begin, lookAhead(0).range.begin};
  return declarator;
}

std::optional<Parameter> Parser::parseParameter() {
  const auto begin = current().range.begin;
  if (at("void") && lookAhead(1).kind == TokenKind::RParen) {
    ++cursor_;
    Parameter parameter;
    parameter.isVoidMarker = true;
    parameter.type.base = BaseType::Void;
    parameter.range = {begin, current().range.begin};
    return parameter;
  }
  auto type = parseType();
  if (!type) {
    return std::nullopt;
  }
  auto declarator = parseDeclarator();
  if (!declarator) {
    return std::nullopt;
  }
  return Parameter{std::move(*type), std::move(*declarator), false,
                   {begin, current().range.begin}};
}

std::vector<Parameter> Parser::parseParameterList(bool& ok) {
  ok = expect(TokenKind::LParen, "'(' before parameter list");
  std::vector<Parameter> parameters;
  skipNewlines();
  if (consume(TokenKind::RParen)) {
    return parameters;
  }
  while (!at(TokenKind::End)) {
    auto parameter = parseParameter();
    if (!parameter) {
      ok = false;
      return parameters;
    }
    if (parameter->isVoidMarker && !parameters.empty()) {
      errorHere("void parameter-list marker must be the only parameter");
      ok = false;
      return parameters;
    }
    parameters.push_back(std::move(*parameter));
    skipNewlines();
    if (consume(TokenKind::RParen)) {
      return parameters;
    }
    if (!expect(TokenKind::Comma, "',' between parameters")) {
      ok = false;
      return parameters;
    }
    skipNewlines();
  }
  errorHere("unterminated parameter list");
  ok = false;
  return parameters;
}

std::unique_ptr<Decl> Parser::parseTopLevelDeclaration() {
  if (looksLikeStructDefinition()) {
    return parseStructDeclaration();
  }
  if (consume("typedef")) {
    return parseTypedefDeclaration();
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
  return parseDeclarationAfterPrefix(storage, std::move(*type));
}

std::unique_ptr<Decl> Parser::parseDeclarationAfterPrefix(StorageClass storage,
                                                          CType type) {
  auto declarator = parseDeclarator();
  if (!declarator) {
    return nullptr;
  }
  if (at(TokenKind::LParen)) {
    if (declarator->arrayCount) {
      errorHere("function declarator cannot have an array suffix");
      return nullptr;
    }
    return parseFunctionAfterPrefix(storage, std::move(type),
                                    std::move(*declarator));
  }
  return parseVariableAfterPrefix(storage, std::move(type),
                                  std::move(*declarator), true);
}

std::unique_ptr<StructDecl> Parser::parseStructDeclaration() {
  const auto begin = current().range.begin;
  consume("struct");
  if (current().kind != TokenKind::Identifier) {
    errorHere("expected struct name");
    return nullptr;
  }
  std::string name = current().lexeme;
  ++cursor_;
  if (!expect(TokenKind::LBrace, "'{' after struct name")) {
    return nullptr;
  }
  skipNewlines();
  std::vector<FieldDecl> fields;
  while (!at(TokenKind::End) && !at(TokenKind::RBrace)) {
    const auto fieldBegin = current().range.begin;
    auto type = parseType();
    if (!type) {
      recoverDeclaration();
      continue;
    }
    auto declarator = parseDeclarator();
    if (!declarator) {
      recoverDeclaration();
      continue;
    }
    if (!consumeTerminator()) {
      recoverDeclaration();
      continue;
    }
    fields.push_back(FieldDecl{std::move(*type), std::move(*declarator),
                               {fieldBegin, current().range.begin}});
  }
  if (!expect(TokenKind::RBrace, "'}' after struct fields")) {
    return nullptr;
  }
  consumeTerminator(false);
  return std::make_unique<StructDecl>(std::move(name), std::move(fields),
                                      diagnostic::SourceRange{begin,
                                                              current().range.begin});
}

std::unique_ptr<TypedefDecl> Parser::parseTypedefDeclaration() {
  const auto begin = lookAhead(0).range.begin;
  auto type = parseType();
  if (!type) {
    return nullptr;
  }
  auto declarator = parseDeclarator();
  if (!declarator) {
    return nullptr;
  }
  if (!consumeTerminator()) {
    return nullptr;
  }
  typedefNames_.insert(declarator->name);
  return std::make_unique<TypedefDecl>(
      std::move(*type), std::move(*declarator),
      diagnostic::SourceRange{begin, current().range.begin});
}

std::unique_ptr<VarDecl> Parser::parseVariableAfterPrefix(
    StorageClass storage,
    CType type,
    Declarator declarator,
    bool requireTerminator) {
  const auto begin = type.range.begin;
  std::unique_ptr<Expr> initializer;
  if (consume("=")) {
    initializer = parseExpression();
    if (!initializer) {
      return nullptr;
    }
  }
  if (!consumeTerminator(requireTerminator)) {
    return nullptr;
  }
  return std::make_unique<VarDecl>(
      storage, std::move(type), std::move(declarator), std::move(initializer),
      diagnostic::SourceRange{begin, current().range.begin});
}

std::unique_ptr<FunctionDecl> Parser::parseFunctionAfterPrefix(
    StorageClass storage,
    CType returnType,
    Declarator declarator) {
  const auto begin = returnType.range.begin;
  bool parametersOk = false;
  auto parameters = parseParameterList(parametersOk);
  if (!parametersOk) {
    return nullptr;
  }
  std::unique_ptr<BlockStmt> body;
  skipNewlines();
  if (at(TokenKind::LBrace)) {
    body = parseBlock();
    if (!body) {
      return nullptr;
    }
    consumeTerminator(false);
  } else if (!consumeTerminator()) {
    return nullptr;
  }
  return std::make_unique<FunctionDecl>(
      storage, std::move(returnType), std::move(declarator),
      std::move(parameters), std::move(body),
      diagnostic::SourceRange{begin, current().range.begin});
}

ParseResult Parser::parse() {
  std::vector<std::unique_ptr<Decl>> declarations;
  skipNewlines();
  while (!at(TokenKind::End)) {
    if (consume(TokenKind::Semicolon)) {
      skipNewlines();
      continue;
    }
    auto declaration = parseTopLevelDeclaration();
    if (declaration) {
      declarations.push_back(std::move(declaration));
    } else {
      recoverDeclaration();
    }
    skipNewlines();
  }
  ParseResult result;
  result.diagnostics = std::move(diagnostics_);
  if (result.diagnostics.empty()) {
    result.unit = std::make_unique<TranslationUnit>(std::move(declarations));
  }
  return result;
}

ParseResult parseCCompatSource(std::string_view source, std::string fileName) {
  auto lexed = lexCCompatSource(source, fileName);
  Parser parser(std::move(lexed.tokens), std::move(fileName));
  auto result = parser.parse();
  if (!lexed.diagnostics.empty()) {
    result.diagnostics.insert(result.diagnostics.begin(),
                              std::make_move_iterator(lexed.diagnostics.begin()),
                              std::make_move_iterator(lexed.diagnostics.end()));
    result.unit.reset();
  }
  return result;
}

} // namespace hitsimple::compat::detail

namespace hitsimple::compat {

ParseResult parseCCompatSource(std::string_view source, std::string fileName) {
  return detail::parseCCompatSource(source, std::move(fileName));
}

} // namespace hitsimple::compat
