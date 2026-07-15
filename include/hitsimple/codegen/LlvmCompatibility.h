#pragma once

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Triple.h>

#include <optional>
#include <string>
#include <string_view>

namespace hitsimple::codegen {

inline llvm::Triple parseTargetTriple(std::string_view targetTriple) {
  return llvm::Triple(
      llvm::StringRef(targetTriple.data(), targetTriple.size()));
}

inline void setModuleTargetTriple(llvm::Module& module,
                                  std::string_view targetTriple) {
#if LLVM_VERSION_MAJOR >= 21
  module.setTargetTriple(parseTargetTriple(targetTriple));
#else
  module.setTargetTriple(
      llvm::StringRef(targetTriple.data(), targetTriple.size()));
#endif
}

inline std::string moduleTargetTriple(const llvm::Module& module) {
#if LLVM_VERSION_MAJOR >= 21
  return module.getTargetTriple().str();
#else
  return module.getTargetTriple();
#endif
}

inline const llvm::Target* resolveTarget(std::string_view targetTriple,
                                         std::string& error) {
#if LLVM_VERSION_MAJOR >= 21
  return llvm::TargetRegistry::lookupTarget(parseTargetTriple(targetTriple),
                                            error);
#else
  return llvm::TargetRegistry::lookupTarget(
      llvm::StringRef(targetTriple.data(), targetTriple.size()), error);
#endif
}

inline llvm::TargetMachine*
createGenericTargetMachine(const llvm::Target& target,
                           std::string_view targetTriple,
                           const llvm::TargetOptions& options) {
#if LLVM_VERSION_MAJOR >= 21
  return target.createTargetMachine(parseTargetTriple(targetTriple), "generic",
                                     "", options, std::nullopt);
#else
  return target.createTargetMachine(
      llvm::StringRef(targetTriple.data(), targetTriple.size()), "generic", "",
      options, std::nullopt);
#endif
}

inline llvm::FunctionCallee
declareIntrinsic(llvm::Module& module, llvm::Intrinsic::ID id,
                 llvm::ArrayRef<llvm::Type*> overloadTypes = {}) {
#if LLVM_VERSION_MAJOR >= 21
  return llvm::Intrinsic::getOrInsertDeclaration(&module, id, overloadTypes);
#else
  return llvm::Intrinsic::getDeclaration(&module, id, overloadTypes);
#endif
}

} // namespace hitsimple::codegen
