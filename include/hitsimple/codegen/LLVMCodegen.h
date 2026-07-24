#pragma once

#include "hitsimple/codegen/NativeTarget.h"
#include "hitsimple/diagnostic/Diagnostic.h"
#include "hitsimple/hir/HIR.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hitsimple::codegen {

struct ModuleEmitResult final {
  // Keep the context before the module so that the module is destroyed first.
  std::unique_ptr<llvm::LLVMContext> context;
  std::unique_ptr<llvm::Module> module;
  // The machine establishes the module DataLayout before LLVM IR emission and
  // is reused by the native optimization and object-emission stages.
  NativeTarget nativeTarget;
  std::vector<diagnostic::Diagnostic> diagnostics;

  bool ok() const {
    return module != nullptr && nativeTarget.machine != nullptr &&
           diagnostics.empty();
  }
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
  OptimizationLevel optimization = OptimizationLevel::O2;
  bool emitDebugInfo = false;
};

ModuleEmitResult emitLlvmModule(const hir::TranslationUnit& unit,
                                std::string moduleName,
                                CodegenOptions options = {});

DebugInfoFormat debugInfoFormatForTargetTriple(std::string_view targetTriple);

} // namespace hitsimple::codegen
