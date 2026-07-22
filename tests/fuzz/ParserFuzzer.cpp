#include "FuzzInvariants.h"

#include "hitsimple/ast/AST.h"
#include "hitsimple/parser/Parser.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
  if (size > hitsimple::fuzz::maximumInputBytes) {
    return 0;
  }

  const std::string source = hitsimple::fuzz::sourceFromBytes(data, size);
  const auto parsed = hitsimple::parser::parseSource(source, "fuzz.hs");
  hitsimple::fuzz::assertValidDiagnostics(parsed.diagnostics, source.size());
  hitsimple::fuzz::require(parsed.error.size() <=
                           hitsimple::fuzz::maximumInputBytes * 16U);

  if (parsed.unit) {
    hitsimple::fuzz::assertValidAstSourceRanges(*parsed.unit);
    const std::string dump = hitsimple::ast::dumpToString(*parsed.unit);
    hitsimple::fuzz::require(dump.size() <=
                             hitsimple::fuzz::maximumInputBytes * 64U);
  }

  return 0;
}
