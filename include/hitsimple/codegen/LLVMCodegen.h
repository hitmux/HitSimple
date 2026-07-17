#pragma once

#include "hitsimple/diagnostic/Diagnostic.h"
#include "hitsimple/hir/HIR.h"

#include <string>
#include <string_view>
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

enum class DebugInfoFormat {
  Dwarf,
  CodeView,
};

struct CodegenOptions {
  SafetyMode safetyMode = SafetyMode::Unchecked;
  // An empty value selects LLVM's default target triple.
  std::string targetTriple;
  bool emitDebugInfo = false;
};

EmitResult emitLlvmIr(const hir::TranslationUnit& unit,
                      std::string moduleName,
                      CodegenOptions options = {});

DebugInfoFormat debugInfoFormatForTargetTriple(std::string_view targetTriple);

} // namespace hitsimple::codegen
