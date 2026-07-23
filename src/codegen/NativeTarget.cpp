#include "hitsimple/codegen/NativeTarget.h"

#include "hitsimple/codegen/LlvmCompatibility.h"

#include <llvm/Config/llvm-config.h>
#include <llvm/Support/TargetSelect.h>

#include <memory>
#include <utility>

namespace hitsimple::codegen {

llvm::CodeGenOptLevel toCodeGenOptLevel(OptimizationLevel level) {
  switch (level) {
  case OptimizationLevel::O0:
    return llvm::CodeGenOptLevel::None;
  case OptimizationLevel::O1:
    return llvm::CodeGenOptLevel::Less;
  case OptimizationLevel::O2:
  case OptimizationLevel::Os:
    return llvm::CodeGenOptLevel::Default;
  case OptimizationLevel::O3:
    return llvm::CodeGenOptLevel::Aggressive;
  }
  llvm_unreachable("invalid optimization level");
}

std::optional<NativeTarget>
createNativeTarget(const NativeTargetOptions& options, std::string& error) {
  if (llvm::InitializeNativeTarget() ||
      llvm::InitializeNativeTargetAsmPrinter()) {
    error = "cannot initialize LLVM native target";
    return std::nullopt;
  }

  std::string targetError;
  const auto* target = resolveTarget(options.triple, targetError);
  if (target == nullptr) {
    error = "cannot resolve LLVM target '" + options.triple + "': " +
            targetError;
    return std::nullopt;
  }

  llvm::TargetOptions targetOptions;
#if LLVM_VERSION_MAJOR >= 21
  std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(
      parseTargetTriple(options.triple), options.cpu, options.features,
      targetOptions, options.relocationModel, options.codeModel,
      toCodeGenOptLevel(options.optimization)));
#else
  std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(
      options.triple, options.cpu, options.features, targetOptions,
      options.relocationModel, options.codeModel,
      toCodeGenOptLevel(options.optimization)));
#endif
  if (!machine) {
    error = "cannot create LLVM target machine for '" + options.triple + "'";
    return std::nullopt;
  }

  return NativeTarget{target, std::move(machine)};
}

} // namespace hitsimple::codegen
