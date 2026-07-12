#include "CCompatInternal.h"

#include <cctype>
#include <utility>

namespace hitsimple::compat::detail {
namespace {

class Lexer {
public:
  Lexer(std::string_view source, std::string fileName)
      : source_(source), fileName_(std::move(fileName)) {}

  LexResult lex() {
    while (!atEnd()) {
      skipHorizontalSpaceAndComments();
      if (atEnd()) {
        break;
      }

      const auto begin = location();
      const char ch = peek();
      if (ch == '\n' || ch == '\r') {
        consumeNewline();
        add(TokenKind::Newline, "\n", begin);
        continue;
      }
      if (isIdentifierStart(ch)) {
        lexIdentifier(begin);
        continue;
      }
      if (std::isdigit(static_cast<unsigned char>(ch))) {
        lexNumber(begin);
        continue;
      }
      if (ch == '\"' || ch == '\'') {
        lexQuoted(begin, ch);
        continue;
      }
      if (lexPunctuation(begin)) {
        continue;
      }

      const std::string invalid(1, consume());
      add(TokenKind::Invalid, invalid, begin);
      addDiagnostic(begin, location(), "invalid C compatibility token '" +
                                            invalid + "'");
    }

    const auto end = location();
    tokens_.push_back(Token{TokenKind::End, "", {end, end}});
    return LexResult{std::move(tokens_), std::move(diagnostics_)};
  }

private:
  bool atEnd() const { return offset_ >= source_.size(); }

  char peek(std::size_t lookAhead = 0) const {
    const auto index = offset_ + lookAhead;
    return index < source_.size() ? source_[index] : '\0';
  }

  diagnostic::SourceLocation location() const {
    return diagnostic::SourceLocation{fileName_, line_, column_};
  }

  char consume() {
    const char ch = source_[offset_++];
    ++column_;
    return ch;
  }

  void consumeNewline() {
    if (peek() == '\r') {
      ++offset_;
      if (peek() == '\n') {
        ++offset_;
      }
    } else {
      ++offset_;
    }
    ++line_;
    column_ = 1;
  }

  static bool isIdentifierStart(char ch) {
    const auto byte = static_cast<unsigned char>(ch);
    return std::isalpha(byte) != 0 || ch == '_';
  }

  static bool isIdentifierContinue(char ch) {
    const auto byte = static_cast<unsigned char>(ch);
    return std::isalnum(byte) != 0 || ch == '_';
  }

  void add(TokenKind kind,
           std::string lexeme,
           const diagnostic::SourceLocation& begin) {
    tokens_.push_back(Token{kind, std::move(lexeme), {begin, location()}});
  }

  void addDiagnostic(const diagnostic::SourceLocation& begin,
                     const diagnostic::SourceLocation& end,
                     std::string message) {
    auto diagnostic =
        hitsimple::diagnostic::Diagnostic::error(diagnostic::Stage::Parser,
                                                 std::move(message));
    diagnostic.range = diagnostic::SourceRange{begin, end};
    diagnostics_.push_back(std::move(diagnostic));
  }

  void skipHorizontalSpaceAndComments() {
    for (;;) {
      while (!atEnd() && (peek() == ' ' || peek() == '\t' || peek() == '\f' ||
                          peek() == '\v')) {
        consume();
      }
      if (peek() == '/' && peek(1) == '/') {
        consume();
        consume();
        while (!atEnd() && peek() != '\n' && peek() != '\r') {
          consume();
        }
        continue;
      }
      if (peek() == '/' && peek(1) == '*') {
        const auto begin = location();
        consume();
        consume();
        bool closed = false;
        while (!atEnd()) {
          if (peek() == '*' && peek(1) == '/') {
            consume();
            consume();
            closed = true;
            break;
          }
          if (peek() == '\n' || peek() == '\r') {
            consumeNewline();
          } else {
            consume();
          }
        }
        if (!closed) {
          addDiagnostic(begin, location(), "unterminated block comment");
        }
        continue;
      }
      return;
    }
  }

  void lexIdentifier(const diagnostic::SourceLocation& begin) {
    const std::size_t start = offset_;
    consume();
    while (!atEnd() && isIdentifierContinue(peek())) {
      consume();
    }
    add(TokenKind::Identifier,
        std::string(source_.substr(start, offset_ - start)), begin);
  }

  void lexNumber(const diagnostic::SourceLocation& begin) {
    const std::size_t start = offset_;
    bool floating = false;
    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
      consume();
      consume();
      while (std::isxdigit(static_cast<unsigned char>(peek())) != 0 ||
             peek() == '_') {
        consume();
      }
    } else if (peek() == '0' && (peek(1) == 'o' || peek(1) == 'O')) {
      consume();
      consume();
      while ((peek() >= '0' && peek() <= '7') || peek() == '_') {
        consume();
      }
    } else if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
      consume();
      consume();
      while (peek() == '0' || peek() == '1' || peek() == '_') {
        consume();
      }
    } else {
      while (std::isdigit(static_cast<unsigned char>(peek())) != 0 ||
             peek() == '_') {
        consume();
      }
      if (peek() == '.') {
        floating = true;
        consume();
        while (std::isdigit(static_cast<unsigned char>(peek())) != 0 ||
               peek() == '_') {
          consume();
        }
      }
      if (peek() == 'e' || peek() == 'E') {
        floating = true;
        consume();
        if (peek() == '+' || peek() == '-') {
          consume();
        }
        while (std::isdigit(static_cast<unsigned char>(peek())) != 0 ||
               peek() == '_') {
          consume();
        }
      }
    }
    while (std::isalpha(static_cast<unsigned char>(peek())) != 0) {
      const char suffix = consume();
      floating = floating || suffix == 'f' || suffix == 'F';
    }
    add(floating ? TokenKind::Float : TokenKind::Integer,
        std::string(source_.substr(start, offset_ - start)), begin);
  }

  void lexQuoted(const diagnostic::SourceLocation& begin, char quote) {
    const std::size_t start = offset_;
    consume();
    bool terminated = false;
    while (!atEnd()) {
      if (peek() == '\\') {
        consume();
        if (!atEnd() && peek() != '\n' && peek() != '\r') {
          consume();
        }
        continue;
      }
      if (peek() == quote) {
        consume();
        terminated = true;
        break;
      }
      if (peek() == '\n' || peek() == '\r') {
        break;
      }
      consume();
    }
    const auto text = std::string(source_.substr(start, offset_ - start));
    if (!terminated) {
      add(TokenKind::Invalid, text, begin);
      addDiagnostic(begin, location(), "unterminated character or string literal");
      return;
    }
    add(quote == '\"' ? TokenKind::String : TokenKind::Character, text, begin);
  }

  bool lexPunctuation(const diagnostic::SourceLocation& begin) {
    struct MultiChar {
      std::string_view text;
    };
    static constexpr MultiChar multiChar[] = {
        {">>="}, {"<<="}, {"->"}, {"++"}, {"--"}, {"&&"}, {"||"},
        {"=="},  {"!="},  {"<="}, {">="}, {"+="}, {"-="}, {"*="},
        {"/="},  {"%="},  {"&="}, {"|="}, {"^="}, {"<<"}, {">>"},
    };
    for (const auto candidate : multiChar) {
      if (source_.substr(offset_, candidate.text.size()) == candidate.text) {
        for (std::size_t index = 0; index < candidate.text.size(); ++index) {
          consume();
        }
        add(TokenKind::Operator, std::string(candidate.text), begin);
        return true;
      }
    }

    const char ch = peek();
    const TokenKind kind = [&] {
      switch (ch) {
      case '(':
        return TokenKind::LParen;
      case ')':
        return TokenKind::RParen;
      case '{':
        return TokenKind::LBrace;
      case '}':
        return TokenKind::RBrace;
      case '[':
        return TokenKind::LBracket;
      case ']':
        return TokenKind::RBracket;
      case ',':
        return TokenKind::Comma;
      case ';':
        return TokenKind::Semicolon;
      case ':':
        return TokenKind::Colon;
      case '?':
        return TokenKind::Question;
      case '.':
        return TokenKind::Dot;
      case '+':
      case '-':
      case '*':
      case '/':
      case '%':
      case '<':
      case '>':
      case '!':
      case '~':
      case '&':
      case '|':
      case '^':
      case '=':
        return TokenKind::Operator;
      default:
        return TokenKind::Invalid;
      }
    }();
    if (kind == TokenKind::Invalid) {
      return false;
    }
    const std::string text(1, consume());
    add(kind, text, begin);
    return true;
  }

  std::string_view source_;
  std::string fileName_;
  std::size_t offset_ = 0;
  std::size_t line_ = 1;
  std::size_t column_ = 1;
  std::vector<Token> tokens_;
  std::vector<diagnostic::Diagnostic> diagnostics_;
};

} // namespace

LexResult lexCCompatSource(std::string_view source, std::string fileName) {
  return Lexer(source, std::move(fileName)).lex();
}

} // namespace hitsimple::compat::detail
