#include "FuzzInvariants.h"

#include "hitsimple/lexer/Lexer.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
  if (size > hitsimple::fuzz::maximumInputBytes) {
    return 0;
  }

  const std::string source = hitsimple::fuzz::sourceFromBytes(data, size);
  hitsimple::lexer::Lexer lexer(source, "fuzz.hs");

  for (std::size_t tokenCount = 0;; ++tokenCount) {
    hitsimple::fuzz::require(tokenCount <= source.size() + 1U);
    const auto token = lexer.next();
    hitsimple::fuzz::assertValidToken(token);
    if (token.kind == hitsimple::lexer::TokenKind::End) {
      break;
    }
  }

  return 0;
}
