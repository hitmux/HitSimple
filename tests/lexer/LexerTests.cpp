#include "support/TestRunner.h"

#include "hitsimple/lexer/Lexer.h"

#include <vector>

using hitsimple::lexer::Lexer;
using hitsimple::lexer::TokenKind;

namespace {

std::vector<hitsimple::lexer::Token> lexAll(std::string_view source) {
  Lexer lexer(source, "test.hs");
  std::vector<hitsimple::lexer::Token> tokens;

  for (;;) {
    tokens.push_back(lexer.next());
    if (tokens.back().kind == TokenKind::End ||
        tokens.back().kind == TokenKind::Invalid) {
      break;
    }
  }

  return tokens;
}

} // namespace

HS_TEST(Lexer_TokenizesMinimalProgram) {
  const auto tokens = lexAll(
      "func main() {\n"
      "    new x[1]\n"
      "    x %d= 42\n"
      "    printf(\"%d\\n\", x)\n"
      "    return 0\n"
      "}\n");

  HS_EXPECT_EQ(tokens[0].kind, TokenKind::KeywordFunc);
  HS_EXPECT_EQ(tokens[1].kind, TokenKind::Identifier);
  HS_EXPECT_EQ(tokens[1].lexeme, "main");
  HS_EXPECT_EQ(tokens[6].kind, TokenKind::KeywordNew);
  HS_EXPECT_EQ(tokens[13].kind, TokenKind::TypedAssignOperator);
  HS_EXPECT_EQ(tokens[13].lexeme, "%d=");
  HS_EXPECT_EQ(tokens[18].kind, TokenKind::String);
  HS_EXPECT_EQ(tokens[18].lexeme, "\"%d\\n\"");
  HS_EXPECT_EQ(tokens[23].kind, TokenKind::KeywordReturn);
  HS_EXPECT_EQ(tokens.back().kind, TokenKind::End);
}

HS_TEST(Lexer_TypedOperatorUsesLongestMatch) {
  const auto tokens = lexAll("x = x %100d+ 1\nx = x %f* 2\nx %d+= 3\n");

  HS_EXPECT_EQ(tokens[3].kind, TokenKind::TypedAdditiveOperator);
  HS_EXPECT_EQ(tokens[3].lexeme, "%100d+");
  HS_EXPECT_EQ(tokens[9].kind, TokenKind::TypedMultiplicativeOperator);
  HS_EXPECT_EQ(tokens[9].lexeme, "%f*");
  HS_EXPECT_EQ(tokens[13].kind, TokenKind::TypedAssignOperator);
  HS_EXPECT_EQ(tokens[13].lexeme, "%d+=");
}

HS_TEST(Lexer_ClassifiesTypedIntegerOperators) {
  const auto tokens = lexAll("x %d= 1 %d+= 2 %d+ 3 %d* 4 %d%= 5\n");

  HS_EXPECT_EQ(tokens[1].kind, TokenKind::TypedAssignOperator);
  HS_EXPECT_EQ(tokens[2].kind, TokenKind::Integer);
  HS_EXPECT_EQ(tokens[3].kind, TokenKind::TypedAssignOperator);
  HS_EXPECT_EQ(tokens[5].kind, TokenKind::TypedAdditiveOperator);
  HS_EXPECT_EQ(tokens[7].kind, TokenKind::TypedMultiplicativeOperator);
  HS_EXPECT_EQ(tokens[9].kind, TokenKind::TypedAssignOperator);
}

HS_TEST(Lexer_TokenizesControlFlowKeywords) {
  const auto tokens = lexAll(
      "new static extern func return if else for while continue break goto "
      "try catch throw true false struct template impl op as self mut set none "
      "sizeof effects\n");

  HS_EXPECT_EQ(tokens[0].kind, TokenKind::KeywordNew);
  HS_EXPECT_EQ(tokens[1].kind, TokenKind::KeywordStatic);
  HS_EXPECT_EQ(tokens[2].kind, TokenKind::KeywordExtern);
  HS_EXPECT_EQ(tokens[3].kind, TokenKind::KeywordFunc);
  HS_EXPECT_EQ(tokens[4].kind, TokenKind::KeywordReturn);
  HS_EXPECT_EQ(tokens[5].kind, TokenKind::KeywordIf);
  HS_EXPECT_EQ(tokens[6].kind, TokenKind::KeywordElse);
  HS_EXPECT_EQ(tokens[7].kind, TokenKind::KeywordFor);
  HS_EXPECT_EQ(tokens[8].kind, TokenKind::KeywordWhile);
  HS_EXPECT_EQ(tokens[9].kind, TokenKind::KeywordContinue);
  HS_EXPECT_EQ(tokens[10].kind, TokenKind::KeywordBreak);
  HS_EXPECT_EQ(tokens[11].kind, TokenKind::KeywordGoto);
  HS_EXPECT_EQ(tokens[12].kind, TokenKind::KeywordTry);
  HS_EXPECT_EQ(tokens[13].kind, TokenKind::KeywordCatch);
  HS_EXPECT_EQ(tokens[14].kind, TokenKind::KeywordThrow);
  HS_EXPECT_EQ(tokens[15].kind, TokenKind::KeywordTrue);
  HS_EXPECT_EQ(tokens[16].kind, TokenKind::KeywordFalse);
  HS_EXPECT_EQ(tokens[17].kind, TokenKind::KeywordStruct);
  HS_EXPECT_EQ(tokens[18].kind, TokenKind::KeywordTemplate);
  HS_EXPECT_EQ(tokens[19].kind, TokenKind::KeywordImpl);
  HS_EXPECT_EQ(tokens[20].kind, TokenKind::KeywordOp);
  HS_EXPECT_EQ(tokens[21].kind, TokenKind::KeywordAs);
  HS_EXPECT_EQ(tokens[22].kind, TokenKind::KeywordSelf);
  HS_EXPECT_EQ(tokens[23].kind, TokenKind::KeywordMut);
  HS_EXPECT_EQ(tokens[24].kind, TokenKind::KeywordSet);
  HS_EXPECT_EQ(tokens[25].kind, TokenKind::KeywordNone);
  HS_EXPECT_EQ(tokens[26].kind, TokenKind::KeywordSizeof);
  HS_EXPECT_EQ(tokens[27].kind, TokenKind::KeywordEffects);
}

HS_TEST(Lexer_TokenizesReservedKeywords) {
  const auto tokens = lexAll(
      "switch case default do typedef enum union const volatile\n");

  HS_EXPECT_EQ(tokens[0].kind, TokenKind::KeywordSwitch);
  HS_EXPECT_EQ(tokens[1].kind, TokenKind::KeywordCase);
  HS_EXPECT_EQ(tokens[2].kind, TokenKind::KeywordDefault);
  HS_EXPECT_EQ(tokens[3].kind, TokenKind::KeywordDo);
  HS_EXPECT_EQ(tokens[4].kind, TokenKind::KeywordTypedef);
  HS_EXPECT_EQ(tokens[5].kind, TokenKind::KeywordEnum);
  HS_EXPECT_EQ(tokens[6].kind, TokenKind::KeywordUnion);
  HS_EXPECT_EQ(tokens[7].kind, TokenKind::KeywordConst);
  HS_EXPECT_EQ(tokens[8].kind, TokenKind::KeywordVolatile);
}

HS_TEST(Lexer_TracksSourceLocations) {
  const auto tokens = lexAll("new x[1]\n  return 0\n");

  HS_EXPECT_EQ(tokens[0].range.begin.line, 1U);
  HS_EXPECT_EQ(tokens[0].range.begin.column, 1U);
  HS_EXPECT_EQ(tokens[6].range.begin.line, 2U);
  HS_EXPECT_EQ(tokens[6].range.begin.column, 3U);
}

HS_TEST(Lexer_UnclosedStringProducesInvalidToken) {
  const auto tokens = lexAll("printf(\"unterminated\n");

  HS_EXPECT_EQ(tokens[2].kind, TokenKind::Invalid);
  HS_EXPECT_EQ(tokens[2].range.begin.line, 1U);
  HS_EXPECT_EQ(tokens[2].range.begin.column, 8U);
}

HS_TEST(Lexer_SkipsLineAndBlockComments) {
  const auto tokens = lexAll(
      "new x[1] // ignore this\n"
      "/* block\n"
      "   comment */ return 0\n");

  HS_EXPECT_EQ(tokens[0].kind, TokenKind::KeywordNew);
  HS_EXPECT_EQ(tokens[5].kind, TokenKind::Newline);
  HS_EXPECT_EQ(tokens[6].kind, TokenKind::KeywordReturn);
  HS_EXPECT_EQ(tokens[6].range.begin.line, 3U);
  HS_EXPECT_EQ(tokens[6].range.begin.column, 15U);
}

HS_TEST(Lexer_UnclosedBlockCommentProducesInvalidToken) {
  const auto tokens = lexAll("new x[1]\n/* not closed");

  HS_EXPECT_EQ(tokens[6].kind, TokenKind::Invalid);
  HS_EXPECT_EQ(tokens[6].range.begin.line, 2U);
  HS_EXPECT_EQ(tokens[6].range.begin.column, 1U);
}

HS_TEST(Lexer_TokenizesIntegerAndFloatForms) {
  const auto tokens =
      lexAll("0 42 1_000 0xff 0XDEAD_BEEF 0o77 0O77 0b1010_0011 "
             "3.14 2. .5 1e3 1.5e-2 .5E+1\n");

  for (int index = 0; index <= 7; ++index) {
    HS_EXPECT_EQ(tokens[index].kind, TokenKind::Integer);
  }
  for (int index = 8; index <= 13; ++index) {
    HS_EXPECT_EQ(tokens[index].kind, TokenKind::Float);
  }
  HS_EXPECT_EQ(tokens[4].lexeme, "0XDEAD_BEEF");
  HS_EXPECT_EQ(tokens[6].lexeme, "0O77");
  HS_EXPECT_EQ(tokens[13].lexeme, ".5E+1");
}

HS_TEST(Lexer_RejectsInvalidNumericSeparatorsAndDigits) {
  HS_EXPECT_EQ(lexAll("1__2\n")[0].kind, TokenKind::Invalid);
  HS_EXPECT_EQ(lexAll("0b102\n")[0].kind, TokenKind::Invalid);
  HS_EXPECT_EQ(lexAll("078\n")[0].kind, TokenKind::Invalid);
  HS_EXPECT_EQ(lexAll("1e+\n")[0].kind, TokenKind::Invalid);
}

HS_TEST(Lexer_RejectsLegacyLeadingZeroOctal) {
  HS_EXPECT_EQ(lexAll("0755\n")[0].kind, TokenKind::Invalid);
  HS_EXPECT_EQ(lexAll("00\n")[0].kind, TokenKind::Invalid);
  HS_EXPECT_EQ(lexAll("0o755\n")[0].kind, TokenKind::Integer);
}

HS_TEST(Lexer_PreservesU64MaxIntegerToken) {
  const auto tokens = lexAll("18446744073709551615\n");

  HS_EXPECT_EQ(tokens[0].kind, TokenKind::Integer);
  HS_EXPECT_EQ(tokens[0].lexeme, "18446744073709551615");
}

HS_TEST(Lexer_TokenizesCharAndStringLiterals) {
  const auto tokens =
      lexAll("'A' 'AB' '\\n' '\\x41' '\\101' '\\u0041' \"hi\\000\\n\"\n");

  HS_EXPECT_EQ(tokens[0].kind, TokenKind::Char);
  HS_EXPECT_EQ(tokens[1].kind, TokenKind::Char);
  HS_EXPECT_EQ(tokens[2].kind, TokenKind::Char);
  HS_EXPECT_EQ(tokens[3].kind, TokenKind::Char);
  HS_EXPECT_EQ(tokens[4].kind, TokenKind::Char);
  HS_EXPECT_EQ(tokens[5].kind, TokenKind::Char);
  HS_EXPECT_EQ(tokens[6].kind, TokenKind::String);
}

HS_TEST(Lexer_RejectsUnclosedAndInvalidCharLiterals) {
  HS_EXPECT_EQ(lexAll("'unterminated\n")[0].kind, TokenKind::Invalid);
  HS_EXPECT_EQ(lexAll("''\n")[0].kind, TokenKind::Invalid);
  HS_EXPECT_EQ(lexAll("'\\q'\n")[0].kind, TokenKind::Invalid);
}

HS_TEST(Lexer_RejectsUnicodeSurrogateEscape) {
  HS_EXPECT_EQ(lexAll("\"\\uD800\"\n")[0].kind, TokenKind::Invalid);
  HS_EXPECT_EQ(lexAll("'\\uDFFF'\n")[0].kind, TokenKind::Invalid);
  HS_EXPECT_EQ(lexAll("\"\\uE000\"\n")[0].kind, TokenKind::String);
}

HS_TEST(Lexer_TokenizesOperatorsPunctuationAndPreprocessorPrefix) {
  const auto tokens =
      lexAll("+ - * / % ** < > <= >= == != ! && || & | ^ ~ << >> = &= "
             "++ -- ? -> . ; : $ ( ) [ ] { } ,\n");

  HS_EXPECT_EQ(tokens[0].kind, TokenKind::Plus);
  HS_EXPECT_EQ(tokens[1].kind, TokenKind::Minus);
  HS_EXPECT_EQ(tokens[2].kind, TokenKind::Star);
  HS_EXPECT_EQ(tokens[3].kind, TokenKind::Slash);
  HS_EXPECT_EQ(tokens[4].kind, TokenKind::Percent);
  HS_EXPECT_EQ(tokens[5].kind, TokenKind::Power);
  HS_EXPECT_EQ(tokens[6].kind, TokenKind::Less);
  HS_EXPECT_EQ(tokens[7].kind, TokenKind::Greater);
  HS_EXPECT_EQ(tokens[8].kind, TokenKind::LessEqual);
  HS_EXPECT_EQ(tokens[9].kind, TokenKind::GreaterEqual);
  HS_EXPECT_EQ(tokens[10].kind, TokenKind::EqualEqual);
  HS_EXPECT_EQ(tokens[11].kind, TokenKind::BangEqual);
  HS_EXPECT_EQ(tokens[12].kind, TokenKind::Bang);
  HS_EXPECT_EQ(tokens[13].kind, TokenKind::AmpersandAmpersand);
  HS_EXPECT_EQ(tokens[14].kind, TokenKind::PipePipe);
  HS_EXPECT_EQ(tokens[15].kind, TokenKind::Ampersand);
  HS_EXPECT_EQ(tokens[16].kind, TokenKind::Pipe);
  HS_EXPECT_EQ(tokens[17].kind, TokenKind::Caret);
  HS_EXPECT_EQ(tokens[18].kind, TokenKind::Tilde);
  HS_EXPECT_EQ(tokens[19].kind, TokenKind::ShiftLeft);
  HS_EXPECT_EQ(tokens[20].kind, TokenKind::ShiftRight);
  HS_EXPECT_EQ(tokens[21].kind, TokenKind::Equal);
  HS_EXPECT_EQ(tokens[22].kind, TokenKind::AmpersandEqual);
  HS_EXPECT_EQ(tokens[23].kind, TokenKind::PlusPlus);
  HS_EXPECT_EQ(tokens[24].kind, TokenKind::MinusMinus);
  HS_EXPECT_EQ(tokens[25].kind, TokenKind::Question);
  HS_EXPECT_EQ(tokens[26].kind, TokenKind::Arrow);
  HS_EXPECT_EQ(tokens[27].kind, TokenKind::Dot);
  HS_EXPECT_EQ(tokens[28].kind, TokenKind::Semicolon);
  HS_EXPECT_EQ(tokens[29].kind, TokenKind::Colon);
  HS_EXPECT_EQ(tokens[30].kind, TokenKind::PreprocessorPrefix);
}

HS_TEST(Lexer_ClassifiesExtendedTypedOperatorsByPrecedenceGroup) {
  const auto tokens =
      lexAll("%d<< %100d>> %d& %d| %d^ %d** %f** %4f+ %s= %b=\n");

  HS_EXPECT_EQ(tokens[0].kind, TokenKind::TypedShiftOperator);
  HS_EXPECT_EQ(tokens[1].kind, TokenKind::TypedShiftOperator);
  HS_EXPECT_EQ(tokens[2].kind, TokenKind::TypedBitwiseOperator);
  HS_EXPECT_EQ(tokens[3].kind, TokenKind::TypedBitwiseOperator);
  HS_EXPECT_EQ(tokens[4].kind, TokenKind::TypedBitwiseOperator);
  HS_EXPECT_EQ(tokens[5].kind, TokenKind::TypedPowerOperator);
  HS_EXPECT_EQ(tokens[6].kind, TokenKind::TypedPowerOperator);
  HS_EXPECT_EQ(tokens[7].kind, TokenKind::TypedAdditiveOperator);
  HS_EXPECT_EQ(tokens[8].kind, TokenKind::TypedAssignOperator);
  HS_EXPECT_EQ(tokens[9].kind, TokenKind::TypedAssignOperator);
}

HS_TEST(Lexer_KeepsTemplateMarkDistinctFromSemicolon) {
  const auto tokens = lexAll("new p[s2] ;Pair\n");

  HS_EXPECT_EQ(tokens[0].kind, TokenKind::KeywordNew);
  HS_EXPECT_EQ(tokens[5].kind, TokenKind::TemplateMark);
  HS_EXPECT_EQ(tokens[5].lexeme, ";");
  HS_EXPECT_EQ(tokens[6].kind, TokenKind::Identifier);
  HS_EXPECT_EQ(tokens[6].lexeme, "Pair");
}

HS_TEST(Lexer_DoesNotExpandCommentTextIntoTokens) {
  const auto tokens = lexAll(
      "new x[1] /* %d+= return */\n"
      "return x\n");

  HS_EXPECT_EQ(tokens[0].kind, TokenKind::KeywordNew);
  HS_EXPECT_EQ(tokens[5].kind, TokenKind::Newline);
  HS_EXPECT_EQ(tokens[6].kind, TokenKind::KeywordReturn);
  HS_EXPECT_EQ(tokens[7].kind, TokenKind::Identifier);
}

HS_TEST(Lexer_TokenizesAddressAndSliceOperators) {
  const auto tokens = lexAll("ptr &= &x\nx[1:+2] = [4]*ptr\n");

  HS_EXPECT_EQ(tokens[0].kind, TokenKind::Identifier);
  HS_EXPECT_EQ(tokens[1].kind, TokenKind::AmpersandEqual);
  HS_EXPECT_EQ(tokens[2].kind, TokenKind::Ampersand);
  HS_EXPECT_EQ(tokens[8].kind, TokenKind::Colon);
  HS_EXPECT_EQ(tokens[9].kind, TokenKind::Plus);
  HS_EXPECT_EQ(tokens[13].kind, TokenKind::LBracket);
  HS_EXPECT_EQ(tokens[15].kind, TokenKind::RBracket);
  HS_EXPECT_EQ(tokens[16].kind, TokenKind::Star);
}

HS_TEST(Lexer_PreservesEscapesInStringLexeme) {
  const auto tokens = lexAll("\"\\\\\\\"\\n\\x41\\101\"\n");

  HS_EXPECT_EQ(tokens[0].kind, TokenKind::String);
  HS_EXPECT_EQ(tokens[0].lexeme, "\"\\\\\\\"\\n\\x41\\101\"");
}

HS_TEST(Lexer_ReportsInvalidUnknownCharacterLocation) {
  const auto tokens = lexAll("new x[1]\n @\n");

  HS_EXPECT_EQ(tokens[5].kind, TokenKind::Newline);
  HS_EXPECT_EQ(tokens[6].kind, TokenKind::Invalid);
  HS_EXPECT_EQ(tokens[6].range.begin.line, 2U);
  HS_EXPECT_EQ(tokens[6].range.begin.column, 2U);
}
