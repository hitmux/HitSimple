#pragma once

#include "hitsimple/diagnostic/SourceLocation.h"

#include <string>
#include <string_view>

namespace hitsimple::lexer {

enum class TokenKind {
  End,
  Invalid,
  Newline,
  Identifier,
  Integer,
  Float,
  Char,
  String,
  KeywordFunc,
  KeywordNew,
  KeywordStatic,
  KeywordExtern,
  KeywordReturn,
  KeywordIf,
  KeywordElse,
  KeywordFor,
  KeywordWhile,
  KeywordBreak,
  KeywordContinue,
  KeywordGoto,
  KeywordTry,
  KeywordCatch,
  KeywordThrow,
  KeywordTrue,
  KeywordFalse,
  KeywordStruct,
  KeywordTemplate,
  KeywordImpl,
  KeywordOp,
  KeywordAs,
  KeywordSelf,
  KeywordMut,
  KeywordSet,
  KeywordNone,
  KeywordSizeof,
  KeywordSwitch,
  KeywordCase,
  KeywordDefault,
  KeywordDo,
  KeywordTypedef,
  KeywordEnum,
  KeywordUnion,
  KeywordConst,
  KeywordVolatile,
  TypedAssignOperator,
  TypedAdditiveOperator,
  TypedMultiplicativeOperator,
  TypedShiftOperator,
  TypedBitwiseOperator,
  TypedPowerOperator,
  Equal,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Power,
  Less,
  Greater,
  LessEqual,
  GreaterEqual,
  EqualEqual,
  BangEqual,
  Bang,
  Ampersand,
  Pipe,
  Caret,
  Tilde,
  AmpersandAmpersand,
  PipePipe,
  ShiftLeft,
  ShiftRight,
  AmpersandEqual,
  PlusPlus,
  MinusMinus,
  Question,
  Arrow,
  Dot,
  Semicolon,
  TemplateMark,
  Colon,
  PreprocessorPrefix,
  LParen,
  RParen,
  LBrace,
  RBrace,
  LBracket,
  RBracket,
  Comma,
};

struct Token {
  TokenKind kind = TokenKind::Invalid;
  std::string lexeme;
  diagnostic::SourceRange range;
  diagnostic::SourceRange generatedRange;
};

std::string_view tokenKindName(TokenKind kind);

} // namespace hitsimple::lexer
