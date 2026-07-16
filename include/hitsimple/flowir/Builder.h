#pragma once

#include "hitsimple/diagnostic/Diagnostic.h"
#include "hitsimple/flowir/FlowIR.h"
#include "hitsimple/hir/HIR.h"

#include <optional>
#include <vector>

namespace hitsimple::flowir {

struct BuildResult final {
  std::optional<Module> module;
  std::vector<diagnostic::Diagnostic> diagnostics;
};

BuildResult build(const hir::TranslationUnit &unit);

} // namespace hitsimple::flowir
