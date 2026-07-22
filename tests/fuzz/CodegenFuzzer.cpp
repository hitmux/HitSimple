#include "FuzzInvariants.h"

#include "hitsimple/codegen/LLVMCodegen.h"
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
  if (!analyzed.unit || !analyzed.diagnostics.empty()) {
    return 0;
  }

  hitsimple::fuzz::require(hitsimple::hir::verifyHIR(*analyzed.unit).empty());
  const auto emitted =
      hitsimple::codegen::emitLlvmIr(*analyzed.unit, "fuzz.hs");
  hitsimple::fuzz::assertValidDiagnostics(emitted.diagnostics, source.size());
  hitsimple::fuzz::require(emitted.diagnostics.empty());
  hitsimple::fuzz::assertValidLlvmIr(emitted.llvmIr);
  return 0;
}
