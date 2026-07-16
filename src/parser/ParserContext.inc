  #include "hitsimple/parser/Parser.h"
  #include "hitsimple/lexer/Lexer.h"

  #include <cctype>
  #include <map>
  #include <optional>
  #include <sstream>
  #include <string_view>
  #include <utility>

  namespace hitsimple::parser {

  class ParseContext {
  public:
    ParseContext(std::string_view source, std::string fileName)
        : ParseContext(source, std::move(fileName), {}) {}

    ParseContext(std::string_view source,
                 std::string fileName,
                 std::vector<diagnostic::SourceLocation> lineOrigins)
        : source_(source), lexer_(source_, fileName, std::move(lineOrigins)),
          fileName_(std::move(fileName)) {}

    BisonParser::symbol_type nextToken() {
      using TokenKind = lexer::TokenKind;

      const auto token = lexer_.next();
      BisonParser::location_type location;
      location.begin.filename = &fileName(token.range.begin.file);
      location.begin.line = static_cast<int>(token.range.begin.line);
      location.begin.column = static_cast<int>(token.range.begin.column);
      location.end.filename = &fileName(token.range.end.file);
      location.end.line = static_cast<int>(token.range.end.line);
      location.end.column = static_cast<int>(token.range.end.column);

      switch (token.kind) {
      case TokenKind::End:
        return BisonParser::make_END(location);
      case TokenKind::Invalid:
        return BisonParser::make_INVALID(token.lexeme, location);
      case TokenKind::Newline:
        return BisonParser::make_NEWLINE(location);
      case TokenKind::Identifier:
        return BisonParser::make_IDENTIFIER(token.lexeme, location);
      case TokenKind::Integer:
        return BisonParser::make_INTEGER(token.lexeme, location);
      case TokenKind::Float:
        return BisonParser::make_FLOAT(token.lexeme, location);
      case TokenKind::Char:
        return BisonParser::make_CHAR(token.lexeme, location);
      case TokenKind::String:
        return BisonParser::make_STRING(token.lexeme, location);
      case TokenKind::KeywordFunc:
        return BisonParser::make_FUNC(location);
      case TokenKind::KeywordNew:
        return BisonParser::make_NEW(location);
      case TokenKind::KeywordExtern:
        return BisonParser::make_EXTERN(location);
      case TokenKind::KeywordStatic:
        return BisonParser::make_STATIC(location);
      case TokenKind::KeywordFor:
        return BisonParser::make_FOR(location);
      case TokenKind::KeywordGoto:
        return BisonParser::make_GOTO(location);
      case TokenKind::KeywordTry:
        return BisonParser::make_TRY(location);
      case TokenKind::KeywordCatch:
        return BisonParser::make_CATCH(location);
      case TokenKind::KeywordThrow:
        return BisonParser::make_THROW(location);
      case TokenKind::KeywordTrue:
        return BisonParser::make_TRUE(location);
      case TokenKind::KeywordFalse:
        return BisonParser::make_FALSE(location);
      case TokenKind::KeywordStruct:
        return BisonParser::make_STRUCT(location);
      case TokenKind::KeywordTemplate:
        return BisonParser::make_TEMPLATE(location);
      case TokenKind::KeywordImpl:
        return BisonParser::make_IMPL(location);
      case TokenKind::KeywordOp:
        return BisonParser::make_OP(location);
      case TokenKind::KeywordAs:
        return BisonParser::make_AS(location);
      case TokenKind::KeywordSelf:
        return BisonParser::make_SELF(location);
      case TokenKind::KeywordMut:
        return BisonParser::make_MUT(location);
      case TokenKind::KeywordSet:
        return BisonParser::make_SET(location);
      case TokenKind::KeywordNone:
        return BisonParser::make_NONE(location);
      case TokenKind::KeywordSizeof:
        return BisonParser::make_SIZEOF(location);
      case TokenKind::KeywordEffects:
        return BisonParser::make_EFFECTS(location);
      case TokenKind::KeywordSwitch:
      case TokenKind::KeywordCase:
      case TokenKind::KeywordDefault:
      case TokenKind::KeywordDo:
      case TokenKind::KeywordTypedef:
      case TokenKind::KeywordEnum:
      case TokenKind::KeywordUnion:
      case TokenKind::KeywordConst:
      case TokenKind::KeywordVolatile:
        return BisonParser::make_INVALID(token.lexeme, location);
      case TokenKind::KeywordReturn:
        return BisonParser::make_RETURN(location);
      case TokenKind::KeywordIf:
        return BisonParser::make_IF(location);
      case TokenKind::KeywordElse:
        return BisonParser::make_ELSE(location);
      case TokenKind::KeywordWhile:
        return BisonParser::make_WHILE(location);
      case TokenKind::KeywordBreak:
        return BisonParser::make_BREAK(location);
      case TokenKind::KeywordContinue:
        return BisonParser::make_CONTINUE(location);
      case TokenKind::TypedAssignOperator:
        return BisonParser::make_TYPED_ASSIGN_OPERATOR(token.lexeme, location);
      case TokenKind::TypedAdditiveOperator:
        return BisonParser::make_TYPED_ADDITIVE_OPERATOR(token.lexeme, location);
      case TokenKind::TypedMultiplicativeOperator:
        return BisonParser::make_TYPED_MULTIPLICATIVE_OPERATOR(token.lexeme, location);
      case TokenKind::TypedShiftOperator:
        return BisonParser::make_TYPED_SHIFT_OPERATOR(token.lexeme, location);
      case TokenKind::TypedBitwiseOperator:
        return BisonParser::make_TYPED_BITWISE_OPERATOR(token.lexeme, location);
      case TokenKind::TypedPowerOperator:
        return BisonParser::make_TYPED_POWER_OPERATOR(token.lexeme, location);
      case TokenKind::Equal:
        return BisonParser::make_EQUAL(location);
      case TokenKind::Plus:
        return BisonParser::make_PLUS(location);
      case TokenKind::Minus:
        return BisonParser::make_MINUS(location);
      case TokenKind::Star:
        return BisonParser::make_STAR(location);
      case TokenKind::Slash:
        return BisonParser::make_SLASH(location);
      case TokenKind::Percent:
        return BisonParser::make_PERCENT(location);
      case TokenKind::Power:
        return BisonParser::make_POWER(location);
      case TokenKind::Less:
        return BisonParser::make_LESS(location);
      case TokenKind::Greater:
        return BisonParser::make_GREATER(location);
      case TokenKind::LessEqual:
        return BisonParser::make_LESS_EQUAL(location);
      case TokenKind::GreaterEqual:
        return BisonParser::make_GREATER_EQUAL(location);
      case TokenKind::EqualEqual:
        return BisonParser::make_EQUAL_EQUAL(location);
      case TokenKind::BangEqual:
        return BisonParser::make_BANG_EQUAL(location);
      case TokenKind::Bang:
        return BisonParser::make_BANG(location);
      case TokenKind::Ampersand:
        return BisonParser::make_AMPERSAND(location);
      case TokenKind::Pipe:
        return BisonParser::make_PIPE(location);
      case TokenKind::Caret:
        return BisonParser::make_CARET(location);
      case TokenKind::Tilde:
        return BisonParser::make_TILDE(location);
      case TokenKind::AmpersandAmpersand:
        return BisonParser::make_AMPERSAND_AMPERSAND(location);
      case TokenKind::PipePipe:
        return BisonParser::make_PIPE_PIPE(location);
      case TokenKind::ShiftLeft:
        return BisonParser::make_SHIFT_LEFT(location);
      case TokenKind::ShiftRight:
        return BisonParser::make_SHIFT_RIGHT(location);
      case TokenKind::AmpersandEqual:
        return BisonParser::make_AMPERSAND_EQUAL(location);
      case TokenKind::PlusPlus:
        return BisonParser::make_PLUS_PLUS(location);
      case TokenKind::MinusMinus:
        return BisonParser::make_MINUS_MINUS(location);
      case TokenKind::Question:
        if (questionStartsTernary(token.generatedRange.end.line,
                                  token.generatedRange.end.column)) {
          return BisonParser::make_QUESTION(location);
        }
        return BisonParser::make_UNSIGNED_QUESTION(location);
      case TokenKind::Dot:
        return BisonParser::make_DOT(location);
      case TokenKind::Colon:
        return BisonParser::make_COLON(location);
      case TokenKind::PreprocessorPrefix:
        return BisonParser::make_INVALID(token.lexeme, location);
      case TokenKind::Arrow:
        return BisonParser::make_ARROW(location);
      case TokenKind::Semicolon:
        return BisonParser::make_SEMICOLON(location);
      case TokenKind::TemplateMark:
        return BisonParser::make_TEMPLATE_MARK(location);
      case TokenKind::LParen:
        return BisonParser::make_LPAREN(location);
      case TokenKind::RParen:
        return BisonParser::make_RPAREN(location);
      case TokenKind::LBrace:
        return BisonParser::make_LBRACE(location);
      case TokenKind::RBrace:
        return BisonParser::make_RBRACE(location);
      case TokenKind::LBracket:
        return BisonParser::make_LBRACKET(location);
      case TokenKind::RBracket:
        return BisonParser::make_RBRACKET(location);
      case TokenKind::Comma:
        return BisonParser::make_COMMA(location);
      }

      return BisonParser::make_INVALID(token.lexeme, location);
    }

    void setUnit(std::unique_ptr<ast::TranslationUnit> unit) {
      unit_ = std::move(unit);
    }

    std::unique_ptr<ast::TranslationUnit> takeUnit() {
      return std::move(unit_);
    }

    void setError(const BisonParser::location_type& location,
                  const std::string& message) {
      if (!error_.empty()) {
        return;
      }

      auto parserDiagnostic = diagnostic::Diagnostic::error(
          diagnostic::Stage::Parser, message);
      parserDiagnostic.range = diagnostic::SourceRange{
          sourceLocation(location.begin), sourceLocation(location.end)};

      std::ostringstream out;
      out << parserDiagnostic.range->begin.file << ':'
          << parserDiagnostic.range->begin.line << ':'
          << parserDiagnostic.range->begin.column << ": " << message;
      error_ = out.str();
      diagnostic_ = std::move(parserDiagnostic);
    }

    const std::string& error() const { return error_; }

    std::optional<diagnostic::Diagnostic> takeDiagnostic() {
      return std::move(diagnostic_);
    }

  private:
    diagnostic::SourceLocation sourceLocation(const position& position) const {
      return diagnostic::SourceLocation{
          position.filename != nullptr ? *position.filename : fileName_,
          static_cast<std::size_t>(position.line),
          static_cast<std::size_t>(position.column)};
    }

    const std::string& fileName(const std::string& fileName) {
      if (fileName.empty()) {
        return fileName_;
      }
      auto [it, inserted] = locationFiles_.emplace(fileName, fileName);
      (void)inserted;
      return it->second;
    }

    bool questionStartsTernary(std::size_t line, std::size_t column) const {
      std::size_t offset = 0;
      std::size_t currentLine = 1;
      std::size_t currentColumn = 1;
      while (offset < source_.size() &&
             (currentLine < line || currentColumn < column)) {
        if (source_[offset] == '\n') {
          ++currentLine;
          currentColumn = 1;
        } else {
          ++currentColumn;
        }
        ++offset;
      }

      while (offset < source_.size() &&
             (source_[offset] == ' ' || source_[offset] == '\t' ||
              source_[offset] == '\r')) {
        ++offset;
      }

      if (offset >= source_.size() || source_[offset] == '\n' ||
          source_[offset] == ')' || source_[offset] == ']' ||
          source_[offset] == '}' || source_[offset] == ';' ||
          source_[offset] == ',') {
        return false;
      }

      const unsigned char ch = static_cast<unsigned char>(source_[offset]);
      if (!(std::isalnum(ch) || source_[offset] == '_' ||
            source_[offset] == '"' || source_[offset] == '\'' ||
            source_[offset] == '(' || source_[offset] == '!' ||
            source_[offset] == '~' || source_[offset] == '-' ||
            source_[offset] == '&' || source_[offset] == '[')) {
        return false;
      }

      for (std::size_t it = offset; it < source_.size() && source_[it] != '\n';
           ++it) {
        if (source_[it] == ':') {
          return true;
        }
      }
      return false;
    }

    std::string source_;
    lexer::Lexer lexer_;
    std::string fileName_;
    std::map<std::string, std::string> locationFiles_;
    std::unique_ptr<ast::TranslationUnit> unit_;
    std::string error_;
    std::optional<diagnostic::Diagnostic> diagnostic_;
  };

  static BisonParser::symbol_type yylex(ParseContext& parseContext) {
    return parseContext.nextToken();
  }

  static ParseResult finishParse(ParseContext& context, int status) {
    ParseResult result;
    if (status == 0) {
      result.unit = context.takeUnit();
      if (result.unit) {
        return result;
      }
    }

    result.error = context.error();
    if (auto parserDiagnostic = context.takeDiagnostic()) {
      result.diagnostics.push_back(std::move(*parserDiagnostic));
    } else {
      result.error = "parse failed";
      result.diagnostics.push_back(diagnostic::Diagnostic::error(
          diagnostic::Stage::Parser, "parse failed"));
    }
    return result;
  }

  ParseResult parseSource(std::string_view source, std::string fileName) {
    ParseContext context(source, std::move(fileName));
    BisonParser parser(context);
    return finishParse(context, parser.parse());
  }

  ParseResult parseSource(
      std::string_view source,
      std::string fileName,
      std::vector<diagnostic::SourceLocation> lineOrigins) {
    ParseContext context(source, std::move(fileName), std::move(lineOrigins));
    BisonParser parser(context);
    return finishParse(context, parser.parse());
  }

  } // namespace hitsimple::parser
