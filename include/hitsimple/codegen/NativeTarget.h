#pragma once

#include "hitsimple/codegen/OptimizationPipeline.h"

#include <llvm/Target/TargetMachine.h>

#include <memory>
#include <optional>
#include <string>

namespace llvm {
class Target;
}

namespace hitsimple::codegen {

struct NativeTargetOptions final {
  std::string triple;
  std::string cpu = "generic";
  std::string features;
  OptimizationLevel optimization = OptimizationLevel::O2;
  std::optional<llvm::Reloc::Model> relocationModel = llvm::Reloc::PIC_;
  std::optional<llvm::CodeModel::Model> codeModel;
};

struct NativeTarget final {
  const llvm::Target* target = nullptr;
  std::unique_ptr<llvm::TargetMachine> machine;
};

std::optional<NativeTarget>
createNativeTarget(const NativeTargetOptions& options, std::string& error);

llvm::CodeGenOptLevel toCodeGenOptLevel(OptimizationLevel level);

} // namespace hitsimple::codegen
