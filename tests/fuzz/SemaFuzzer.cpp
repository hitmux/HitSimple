#include "FuzzInvariants.h"

#include "hitsimple/hir/HIR.h"
#include "hitsimple/parser/Parser.h"
#include "hitsimple/sema/Sema.h"

#include <cstddef>
#include <cstdint>
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

  hitsimple::fuzz::assertValidAstSourceRanges(*parsed.unit);
  const auto analyzed = hitsimple::sema::analyze(
      *parsed.unit,
      hitsimple::sema::AnalyzeOptions{true, allStandardHeaders()});
  hitsimple::fuzz::assertValidDiagnostics(analyzed.diagnostics, source.size());

  if (analyzed.unit) {
    hitsimple::fuzz::require(analyzed.diagnostics.empty());
    hitsimple::fuzz::require(hitsimple::hir::verifyHIR(*analyzed.unit).empty());
    return 0;
  }

  hitsimple::fuzz::require(!analyzed.diagnostics.empty());
  const auto repeated = hitsimple::sema::analyze(
      *parsed.unit,
      hitsimple::sema::AnalyzeOptions{true, allStandardHeaders()});
  hitsimple::fuzz::assertValidDiagnostics(repeated.diagnostics, source.size());
  hitsimple::fuzz::require(
      hitsimple::fuzz::diagnosticFingerprint(analyzed.diagnostics) ==
      hitsimple::fuzz::diagnosticFingerprint(repeated.diagnostics));
  return 0;
}
