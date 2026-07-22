#include "safety/StaticSafetyAnalyzerImpl.h"

namespace hitsimple::safety {

StaticSafetyResult analyzeStaticSafety(const hir::TranslationUnit& unit,
                                       StaticSafetyOptions options) {
  return detail::StaticSafetyAnalyzer(options).analyze(unit);
}

} // namespace hitsimple::safety
