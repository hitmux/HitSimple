#include "hitsimple/codegen/SanitizerPipeline.h"

#include <llvm/IR/Attributes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Transforms/Instrumentation/AddressSanitizer.h>

namespace hitsimple::codegen {

void addSanitizerAttributes(llvm::Module &module,
                            SanitizerInstrumentation sanitizer) {
  if (sanitizer != SanitizerInstrumentation::Address) {
    return;
  }

  for (auto &function : module) {
    if (!function.isDeclaration()) {
      function.addFnAttr(llvm::Attribute::SanitizeAddress);
    }
  }
}

void registerSanitizerPasses(llvm::ModulePassManager &pipeline,
                             SanitizerInstrumentation sanitizer) {
  if (sanitizer != SanitizerInstrumentation::Address) {
    return;
  }

  pipeline.addPass(
      llvm::AddressSanitizerPass(llvm::AddressSanitizerOptions{}));
}

} // namespace hitsimple::codegen
