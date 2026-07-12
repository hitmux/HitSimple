#include "hitsimple/lexer/Lexer.h"

#include <cstddef>
#include <cstring>
#include <string>

namespace hitsimple::lexer {
namespace {

bool isDigit(char ch) { return ch >= '0' && ch <= '9'; }

bool isOctalDigit(char ch) { return ch >= '0' && ch <= '7'; }

bool isBinaryDigit(char ch) { return ch == '0' || ch == '1'; }

bool isHexDigit(char ch) {
  return isDigit(ch) || (ch >= 'a' && ch <= 'f') ||
         (ch >= 'A' && ch <= 'F');
}

int hexValue(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return 10 + ch - 'a';
  }
  return 10 + ch - 'A';
}

bool isUnicodeSurrogateEscape(const char* it) {
  const int value = (hexValue(it[1]) << 12) | (hexValue(it[2]) << 8) |
                    (hexValue(it[3]) << 4) | hexValue(it[4]);
  return value >= 0xD800 && value <= 0xDFFF;
}

bool isAlpha(char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

bool isIdentContinue(char ch) {
  return isAlpha(ch) || isDigit(ch) || ch == '_';
}

bool isNumberContinue(char ch) {
  return isIdentContinue(ch) || ch == '.';
}

bool startsElseKeyword(const char* it, const char* limit) {
  return it + 4 <= limit && std::strncmp(it, "else", 4) == 0 &&
         (it + 4 == limit || !isIdentContinue(it[4]));
}

bool validSeparatedDigits(std::string_view text, bool (*isValidDigit)(char)) {
  if (text.empty() || text.front() == '_' || text.back() == '_') {
    return false;
  }

  bool previousUnderscore = false;
  for (const char ch : text) {
    if (ch == '_') {
      if (previousUnderscore) {
        return false;
      }
      previousUnderscore = true;
      continue;
    }
    if (!isValidDigit(ch)) {
      return false;
    }
    previousUnderscore = false;
  }
  return true;
}

const char* consumeDigits(const char* it,
                          const char* limit,
                          bool (*isValidDigit)(char)) {
  while (it != limit && (isValidDigit(*it) || *it == '_')) {
    ++it;
  }
  return it;
}

bool isSimpleEscape(char ch) {
  switch (ch) {
  case 'n':
  case 't':
  case 'r':
  case '\\':
  case '\'':
  case '"':
    return true;
  default:
    return false;
  }
}

TokenKind classifyTypedOperator(std::string_view lexeme) {
  if (lexeme.empty()) {
    return TokenKind::Invalid;
  }

  switch (lexeme.back()) {
  case '=':
    return TokenKind::TypedAssignOperator;
  case '<':
  case '>':
    return TokenKind::TypedShiftOperator;
  case '&':
  case '|':
  case '^':
    return TokenKind::TypedBitwiseOperator;
  case '+':
  case '-':
    return TokenKind::TypedAdditiveOperator;
  case '*':
    if (lexeme.size() >= 2 && lexeme[lexeme.size() - 2] == '*') {
      return TokenKind::TypedPowerOperator;
    }
    return TokenKind::TypedMultiplicativeOperator;
  case '/':
  case '%':
    return TokenKind::TypedMultiplicativeOperator;
  default:
    return TokenKind::Invalid;
  }
}

} // namespace

Lexer::Lexer(std::string_view source, std::string fileName)
    : Lexer(source, std::move(fileName), {}) {}

Lexer::Lexer(std::string_view source,
             std::string fileName,
             std::vector<diagnostic::SourceLocation> lineOrigins)
    : source_(source), fileName_(std::move(fileName)),
      lineOrigins_(std::move(lineOrigins)) {
  source_.append(8, '\0');
  cursor_ = source_.data();
  limit_ = source_.data() + source.size();
}

diagnostic::SourceLocation Lexer::mapLocation(std::size_t line,
                                              std::size_t column) const {
  if (line > 0 && line <= lineOrigins_.size() &&
      diagnostic::hasFile(lineOrigins_[line - 1])) {
    auto location = lineOrigins_[line - 1];
    location.column = column;
    return location;
  }

  return diagnostic::SourceLocation{fileName_, line, column};
}

Token Lexer::makeToken(TokenKind kind, const char* begin, const char* end) const {
  Token token;
  token.kind = kind;
  token.lexeme.assign(begin, end);
  token.generatedRange.begin.file = fileName_;
  token.generatedRange.begin.line = line_;
  token.generatedRange.begin.column = column_;
  token.range.begin = mapLocation(line_, column_);

  std::size_t endLine = line_;
  std::size_t endColumn = column_;
  for (const char* it = begin; it != end; ++it) {
    if (*it == '\n') {
      ++endLine;
      endColumn = 1;
    } else {
      ++endColumn;
    }
  }

  token.generatedRange.end.file = fileName_;
  token.generatedRange.end.line = endLine;
  token.generatedRange.end.column = endColumn;
  token.range.end = mapLocation(endLine, endColumn);
  return token;
}

Token Lexer::scanComment() {
  const char* begin = cursor_;
  if (cursor_ + 1 < limit_ && cursor_[1] == '/') {
    const char* it = cursor_ + 2;
    while (it != limit_ && *it != '\n') {
      ++it;
    }
    advanceLocation(begin, it);
    cursor_ = it;
    return next();
  }

  const char* it = cursor_ + 2;
  while (it < limit_) {
    if (it + 1 < limit_ && it[0] == '*' && it[1] == '/') {
      it += 2;
      advanceLocation(begin, it);
      cursor_ = it;
      return next();
    }
    ++it;
  }

  Token token = makeToken(TokenKind::Invalid, begin, limit_);
  advanceLocation(begin, limit_);
  cursor_ = limit_;
  return token;
}

Token Lexer::scanNumber() {
  const char* begin = cursor_;
  const char* it = cursor_;
  bool valid = true;
  bool isFloat = false;

  if (*it == '.') {
    isFloat = true;
    ++it;
    const char* fractionBegin = it;
    it = consumeDigits(it, limit_, isDigit);
    valid = validSeparatedDigits(std::string_view(fractionBegin,
                                                  it - fractionBegin),
                                  isDigit);
  } else if (*it == '0' && it + 1 < limit_ &&
             (it[1] == 'x' || it[1] == 'X')) {
    it += 2;
    const char* digitsBegin = it;
    it = consumeDigits(it, limit_, isHexDigit);
    valid = validSeparatedDigits(std::string_view(digitsBegin,
                                                  it - digitsBegin),
                                  isHexDigit);
  } else if (*it == '0' && it + 1 < limit_ &&
             (it[1] == 'b' || it[1] == 'B')) {
    it += 2;
    const char* digitsBegin = it;
    it = consumeDigits(it, limit_, isBinaryDigit);
    valid = validSeparatedDigits(std::string_view(digitsBegin,
                                                  it - digitsBegin),
                                  isBinaryDigit);
  } else if (*it == '0' && it + 1 < limit_ &&
             (it[1] == 'o' || it[1] == 'O')) {
    it += 2;
    const char* digitsBegin = it;
    it = consumeDigits(it, limit_, isOctalDigit);
    valid = validSeparatedDigits(std::string_view(digitsBegin,
                                                  it - digitsBegin),
                                  isOctalDigit);
  } else {
    const bool leadingZero = *it == '0';
    it = consumeDigits(it, limit_, isDigit);
    valid = validSeparatedDigits(std::string_view(begin, it - begin), isDigit);
    if (leadingZero && it != begin + 1) {
      valid = false;
    }

    if (it != limit_ && *it == '.') {
      isFloat = true;
      ++it;
      const char* fractionBegin = it;
      it = consumeDigits(it, limit_, isDigit);
      if (fractionBegin != it) {
        valid = valid &&
                validSeparatedDigits(std::string_view(fractionBegin,
                                                      it - fractionBegin),
                                      isDigit);
      }
    }
  }

  if (it != limit_ && (*it == 'e' || *it == 'E')) {
    isFloat = true;
    ++it;
    if (it != limit_ && (*it == '+' || *it == '-')) {
      ++it;
    }
    const char* exponentBegin = it;
    it = consumeDigits(it, limit_, isDigit);
    valid = valid &&
            validSeparatedDigits(std::string_view(exponentBegin,
                                                  it - exponentBegin),
                                  isDigit);
  }

  while (it != limit_ && isNumberContinue(*it)) {
    valid = false;
    ++it;
  }

  Token token =
      makeToken(valid ? (isFloat ? TokenKind::Float : TokenKind::Integer)
                      : TokenKind::Invalid,
                begin, it);
  advanceLocation(begin, it);
  cursor_ = it;
  return token;
}

void Lexer::advanceLocation(const char* begin, const char* end) {
  for (const char* it = begin; it != end; ++it) {
    if (*it == '\n') {
      ++line_;
      column_ = 1;
    } else {
      ++column_;
    }
  }
}

Token Lexer::scanQuotedLiteral(char quote, TokenKind kind) {
  const char* begin = cursor_;
  const char* it = cursor_ + 1;
  bool hasContent = false;

  while (it != limit_) {
    const char ch = *it;
    if (ch == '\n') {
      Token token = makeToken(TokenKind::Invalid, begin, it);
      advanceLocation(begin, it);
      cursor_ = it;
      return token;
    }

    if (ch == '\\') {
      ++it;
      if (it == limit_ || *it == '\n') {
        Token token = makeToken(TokenKind::Invalid, begin, it);
        advanceLocation(begin, it);
        cursor_ = it;
        return token;
      }

      bool validEscape = isSimpleEscape(*it);
      if (*it == 'x') {
        validEscape = it + 2 < limit_ && isHexDigit(it[1]) && isHexDigit(it[2]);
        it += validEscape ? 2 : 0;
      } else if (*it == 'u') {
        validEscape = it + 4 < limit_ && isHexDigit(it[1]) &&
                      isHexDigit(it[2]) && isHexDigit(it[3]) &&
                      isHexDigit(it[4]);
        if (validEscape && isUnicodeSurrogateEscape(it)) {
          validEscape = false;
        }
        it += validEscape ? 4 : 0;
      } else if (isOctalDigit(*it)) {
        validEscape = true;
        if (it + 1 < limit_ && isOctalDigit(it[1])) {
          ++it;
        }
        if (it + 1 < limit_ && isOctalDigit(it[1])) {
          ++it;
        }
      }

      if (!validEscape) {
        Token token = makeToken(TokenKind::Invalid, begin, it + 1);
        advanceLocation(begin, it + 1);
        cursor_ = it + 1;
        return token;
      }
      hasContent = true;
    } else if (ch == quote) {
      ++it;
      Token token = makeToken(kind == TokenKind::Char && !hasContent
                                  ? TokenKind::Invalid
                                  : kind,
                              begin, it);
      advanceLocation(begin, it);
      cursor_ = it;
      return token;
    } else {
      hasContent = true;
    }

    ++it;
  }

  Token token = makeToken(TokenKind::Invalid, begin, it);
  advanceLocation(begin, it);
  cursor_ = it;
  return token;
}

Token Lexer::scanChar() { return scanQuotedLiteral('\'', TokenKind::Char); }

Token Lexer::scanString() { return scanQuotedLiteral('"', TokenKind::String); }

Token Lexer::next() {
  for (;;) {
    const char* tokenBegin = cursor_;

    if (cursor_ == limit_) {
      return makeToken(TokenKind::End, cursor_, cursor_);
    }

    if (*cursor_ == '"') {
      return scanString();
    }
    if (*cursor_ == '\'') {
      return scanChar();
    }
    if (isDigit(*cursor_) ||
        (*cursor_ == '.' && cursor_ + 1 < limit_ && isDigit(cursor_[1]))) {
      return scanNumber();
    }
    if (*cursor_ == '/' && cursor_ + 1 < limit_ &&
        (cursor_[1] == '/' || cursor_[1] == '*')) {
      return scanComment();
    }

    const char* YYMARKER = nullptr;
    /*!re2c
      re2c:define:YYCTYPE = char;
      re2c:yyfill:enable = 0;
      re2c:define:YYCURSOR = cursor_;
      re2c:define:YYLIMIT = limit_;
      re2c:define:YYMARKER = YYMARKER;

      wsp = [ \t\r\f\v]+;
      nl = "\n";
      id = [A-Za-z_][A-Za-z0-9_]*;
      typed_op = "%" [0-9]* ("d" ("=" | ("+" | "-" | "*" | "/" | "%" | "**" | "<<" | ">>" | "&" | "|" | "^") "="?) | "f" ("=" | ("+" | "-" | "*" | "/" | "**")))
               | "%" [0-9]* "f" ("=" | ("+" | "-" | "*" | "/" | "**") "="?)
               | "%" [sb] "=";

      wsp { advanceLocation(tokenBegin, cursor_); continue; }
      nl {
        const char *after = cursor_;
        while (after < limit_ &&
               (*after == ' ' || *after == '\t' || *after == '\r' ||
                *after == '\f' || *after == '\v' || *after == '\n')) {
          ++after;
        }
        if (startsElseKeyword(after, limit_)) {
          advanceLocation(tokenBegin, after);
          cursor_ = after;
          continue;
        }
        Token token = makeToken(TokenKind::Newline, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "func" {
        Token token = makeToken(TokenKind::KeywordFunc, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "new" {
        Token token = makeToken(TokenKind::KeywordNew, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "static" {
        Token token = makeToken(TokenKind::KeywordStatic, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "extern" {
        Token token = makeToken(TokenKind::KeywordExtern, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "return" {
        Token token = makeToken(TokenKind::KeywordReturn, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "if" {
        Token token = makeToken(TokenKind::KeywordIf, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "else" {
        Token token = makeToken(TokenKind::KeywordElse, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "for" {
        Token token = makeToken(TokenKind::KeywordFor, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "while" {
        Token token = makeToken(TokenKind::KeywordWhile, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "break" {
        Token token = makeToken(TokenKind::KeywordBreak, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "continue" {
        Token token = makeToken(TokenKind::KeywordContinue, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "goto" {
        Token token = makeToken(TokenKind::KeywordGoto, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "try" {
        Token token = makeToken(TokenKind::KeywordTry, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "catch" {
        Token token = makeToken(TokenKind::KeywordCatch, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "throw" {
        Token token = makeToken(TokenKind::KeywordThrow, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "true" {
        Token token = makeToken(TokenKind::KeywordTrue, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "false" {
        Token token = makeToken(TokenKind::KeywordFalse, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "struct" {
        Token token = makeToken(TokenKind::KeywordStruct, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "template" {
        Token token = makeToken(TokenKind::KeywordTemplate, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "impl" {
        Token token = makeToken(TokenKind::KeywordImpl, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "op" {
        Token token = makeToken(TokenKind::KeywordOp, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "as" {
        Token token = makeToken(TokenKind::KeywordAs, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "self" {
        Token token = makeToken(TokenKind::KeywordSelf, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "mut" {
        Token token = makeToken(TokenKind::KeywordMut, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "set" {
        Token token = makeToken(TokenKind::KeywordSet, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "none" {
        Token token = makeToken(TokenKind::KeywordNone, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "sizeof" {
        Token token = makeToken(TokenKind::KeywordSizeof, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "switch" {
        Token token = makeToken(TokenKind::KeywordSwitch, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "case" {
        Token token = makeToken(TokenKind::KeywordCase, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "default" {
        Token token = makeToken(TokenKind::KeywordDefault, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "do" {
        Token token = makeToken(TokenKind::KeywordDo, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "typedef" {
        Token token = makeToken(TokenKind::KeywordTypedef, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "enum" {
        Token token = makeToken(TokenKind::KeywordEnum, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "union" {
        Token token = makeToken(TokenKind::KeywordUnion, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "const" {
        Token token = makeToken(TokenKind::KeywordConst, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "volatile" {
        Token token = makeToken(TokenKind::KeywordVolatile, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      typed_op {
        Token token = makeToken(classifyTypedOperator(
                                    std::string_view(tokenBegin,
                                                     cursor_ - tokenBegin)),
                                tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      id {
        Token token = makeToken(TokenKind::Identifier, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "==" {
        Token token = makeToken(TokenKind::EqualEqual, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "=" {
        Token token = makeToken(TokenKind::Equal, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "++" {
        Token token = makeToken(TokenKind::PlusPlus, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "+" {
        Token token = makeToken(TokenKind::Plus, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "--" {
        Token token = makeToken(TokenKind::MinusMinus, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "->" {
        Token token = makeToken(TokenKind::Arrow, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "-" {
        Token token = makeToken(TokenKind::Minus, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "**" {
        Token token = makeToken(TokenKind::Power, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "*" {
        Token token = makeToken(TokenKind::Star, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "/" {
        Token token = makeToken(TokenKind::Slash, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "%" {
        Token token = makeToken(TokenKind::Percent, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "<=" {
        Token token = makeToken(TokenKind::LessEqual, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "<<" {
        Token token = makeToken(TokenKind::ShiftLeft, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "<" {
        Token token = makeToken(TokenKind::Less, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      ">=" {
        Token token = makeToken(TokenKind::GreaterEqual, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      ">>" {
        Token token = makeToken(TokenKind::ShiftRight, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      ">" {
        Token token = makeToken(TokenKind::Greater, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "!=" {
        Token token = makeToken(TokenKind::BangEqual, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "!" {
        Token token = makeToken(TokenKind::Bang, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "&&" {
        Token token = makeToken(TokenKind::AmpersandAmpersand, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "&=" {
        Token token = makeToken(TokenKind::AmpersandEqual, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "&" {
        Token token = makeToken(TokenKind::Ampersand, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "||" {
        Token token = makeToken(TokenKind::PipePipe, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "|" {
        Token token = makeToken(TokenKind::Pipe, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "^" {
        Token token = makeToken(TokenKind::Caret, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "~" {
        Token token = makeToken(TokenKind::Tilde, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "?" {
        Token token = makeToken(TokenKind::Question, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "." {
        Token token = makeToken(TokenKind::Dot, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      ";" {
        const char *next = cursor_;
        while (next < limit_ && (*next == ' ' || *next == '\t')) {
          ++next;
        }
        const bool templateMark =
            tokenBegin > source_.data() &&
            (tokenBegin[-1] == ' ' || tokenBegin[-1] == '\t') &&
            next < limit_ && (isAlpha(*next) || *next == '_');
        Token token = makeToken(templateMark ? TokenKind::TemplateMark
                                             : TokenKind::Semicolon,
                                tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      ":" {
        Token token = makeToken(TokenKind::Colon, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "$" {
        Token token = makeToken(TokenKind::PreprocessorPrefix, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "(" {
        Token token = makeToken(TokenKind::LParen, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      ")" {
        Token token = makeToken(TokenKind::RParen, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "{" {
        Token token = makeToken(TokenKind::LBrace, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "}" {
        Token token = makeToken(TokenKind::RBrace, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "[" {
        Token token = makeToken(TokenKind::LBracket, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "]" {
        Token token = makeToken(TokenKind::RBracket, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      "," {
        Token token = makeToken(TokenKind::Comma, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
      * {
        Token token = makeToken(TokenKind::Invalid, tokenBegin, cursor_);
        advanceLocation(tokenBegin, cursor_);
        return token;
      }
    */

    return makeToken(TokenKind::Invalid, tokenBegin, cursor_);
  }
}

} // namespace hitsimple::lexer
