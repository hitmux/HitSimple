#pragma once

#include "hitsimple/diagnostic/Diagnostic.h"
#include "hitsimple/hir/HIR.h"

#include <string>
#include <vector>

namespace hitsimple::codegen {

struct EmitResult {
  std::string llvmIr;
  std::vector<diagnostic::Diagnostic> diagnostics;
};

enum class SafetyMode {
  Unchecked,
  StaticChecked,
  Checked,
};

struct CodegenOptions {
  SafetyMode safetyMode = SafetyMode::Unchecked;
  // An empty value selects LLVM's default target triple.
  std::string targetTriple;
};

EmitResult emitLlvmIr(const hir::TranslationUnit& unit,
                      std::string moduleName,
                      CodegenOptions options = {});

} // namespace hitsimple::codegen
