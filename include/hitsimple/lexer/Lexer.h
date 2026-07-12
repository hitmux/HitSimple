#pragma once

#include "hitsimple/lexer/Token.h"

#include <string>
#include <string_view>
#include <vector>

namespace hitsimple::lexer {

class Lexer {
public:
  Lexer(std::string_view source, std::string fileName);
  Lexer(std::string_view source,
        std::string fileName,
        std::vector<diagnostic::SourceLocation> lineOrigins);

  Token next();

private:
  Token makeToken(TokenKind kind, const char* begin, const char* end) const;
  diagnostic::SourceLocation mapLocation(std::size_t line,
                                          std::size_t column) const;
  Token scanChar();
  Token scanComment();
  Token scanNumber();
  Token scanString();
  Token scanQuotedLiteral(char quote, TokenKind kind);
  void advanceLocation(const char* begin, const char* end);

  std::string source_;
  std::string fileName_;
  std::vector<diagnostic::SourceLocation> lineOrigins_;
  const char* cursor_;
  const char* limit_;
  std::size_t line_ = 1;
  std::size_t column_ = 1;
};

} // namespace hitsimple::lexer
