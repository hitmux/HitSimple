#pragma once

#include "hitsimple/ast/AST.h"
#include "hitsimple/diagnostic/Diagnostic.h"
#include "hitsimple/lexer/Token.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hitsimple::fuzz {

inline constexpr std::size_t maximumInputBytes = 64U * 1024U;

std::string sourceFromBytes(const std::uint8_t *data, std::size_t size);

void require(bool condition);
void assertValidToken(const lexer::Token &token);
void assertValidDiagnostics(
    const std::vector<diagnostic::Diagnostic> &diagnostics,
    std::size_t inputSize);
void assertValidAstSourceRanges(const ast::TranslationUnit &unit);
void assertValidLlvmIr(std::string_view llvmIr);
std::string
diagnosticFingerprint(const std::vector<diagnostic::Diagnostic> &diagnostics);

} // namespace hitsimple::fuzz
