#pragma once

#include "hitsimple/diagnostic/Diagnostic.h"
#include "hitsimple/hir/HIR.h"

#include <vector>

namespace hitsimple::safety {

struct StaticSafetyOptions {
  bool enabled = false;
  bool runtimeChecksEnabled = false;
};

struct StaticSafetyResult {
  std::vector<diagnostic::Diagnostic> diagnostics;
};

StaticSafetyResult analyzeStaticSafety(const hir::TranslationUnit& unit,
                                       StaticSafetyOptions options);

} // namespace hitsimple::safety
