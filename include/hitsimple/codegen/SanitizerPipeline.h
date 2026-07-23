#pragma once

#include "hitsimple/codegen/OptimizationPipeline.h"

#include <llvm/IR/PassManager.h>

namespace llvm {
class Module;
} // namespace llvm

namespace hitsimple::codegen {

// Marks functions and appends the LLVM sanitizer passes required by the native
// object-emission pipeline. The module pass must run before the default
// optimizer: otherwise it may delete a statically-provable invalid access
// before AddressSanitizer can preserve and diagnose it.
void addSanitizerAttributes(llvm::Module &module,
                            SanitizerInstrumentation sanitizer);
void registerSanitizerPasses(llvm::ModulePassManager &pipeline,
                             SanitizerInstrumentation sanitizer);

} // namespace hitsimple::codegen
