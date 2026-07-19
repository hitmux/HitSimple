#pragma once

#include "hitsimple/ast/AST.h"
#include "hitsimple/diagnostic/Diagnostic.h"
#include "hitsimple/hir/HIR.h"
#include "hitsimple/stdlib/StandardLibrary.h"

#include <memory>
#include <string>
#include <vector>

namespace hitsimple::sema {

struct AnalyzeResult {
  std::unique_ptr<hir::TranslationUnit> unit;
  std::vector<diagnostic::Diagnostic> diagnostics;
};

struct AnalyzeOptions {
  bool requireMain = true;
  std::vector<stdlib::StandardHeader> standardHeaders;
  bool cCompatibilityMode = false;
  bool internalStandardModule = false;
};

AnalyzeResult analyze(const ast::TranslationUnit& unit,
                      AnalyzeOptions options = {});

} // namespace hitsimple::sema
