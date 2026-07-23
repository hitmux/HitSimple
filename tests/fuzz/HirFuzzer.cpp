#include "FuzzInvariants.h"

#include "hitsimple/hir/HIR.h"
#include "hitsimple/parser/Parser.h"
#include "hitsimple/sema/Sema.h"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<hitsimple::stdlib::StandardHeader> allStandardHeaders() {
  const auto headers = hitsimple::stdlib::allStandardHeaders();
  return {headers.begin(), headers.end()};
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
  if (size > hitsimple::fuzz::maximumInputBytes) {
    return 0;
  }

  const std::string source = hitsimple::fuzz::sourceFromBytes(data, size);
  const auto parsed = hitsimple::parser::parseSource(source, "fuzz.hs");
  hitsimple::fuzz::assertValidDiagnostics(parsed.diagnostics, source.size());
  if (!parsed.unit || !parsed.error.empty() || !parsed.diagnostics.empty()) {
    return 0;
  }

  const auto analyzed = hitsimple::sema::analyze(
      *parsed.unit,
      hitsimple::sema::AnalyzeOptions{true, allStandardHeaders()});
  hitsimple::fuzz::assertValidDiagnostics(analyzed.diagnostics, source.size());
  if (!analyzed.unit || !analyzed.diagnostics.empty()) {
    return 0;
  }

  hitsimple::fuzz::require(hitsimple::hir::verifyHIR(*analyzed.unit).empty());
  std::ostringstream first;
  std::ostringstream second;
  hitsimple::hir::dump(*analyzed.unit, first);
  hitsimple::hir::dump(*analyzed.unit, second);
  hitsimple::fuzz::require(first.str() == second.str());
  hitsimple::fuzz::require(first.str().size() <=
                           hitsimple::fuzz::maximumInputBytes * 128U);
  return 0;
}
