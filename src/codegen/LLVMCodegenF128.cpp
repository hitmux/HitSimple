#include "LlvmEmitter.h"

namespace hitsimple::codegen {
namespace {

bool hasMatchingF128Arguments(const std::vector<llvm::Value *> &arguments,
                              const std::vector<bool> &f128Arguments) {
  return arguments.size() == f128Arguments.size();
}

} // namespace

llvm::Value *LlvmEmitter::emitF128ResultCall(
    std::string_view symbol, std::vector<llvm::Value *> arguments,
    const std::vector<bool> &f128Arguments, std::string_view name) {
  if (!hasMatchingF128Arguments(arguments, f128Arguments)) {
    addDiagnostic("internal f128 runtime argument mismatch");
    return nullptr;
  }

  auto *f128Type = floatTypeForByteLength(16);
  if (!f128Type) {
    return nullptr;
  }
  if (!usesSoftwareF128()) {
    std::vector<llvm::Type *> parameterTypes;
    parameterTypes.reserve(arguments.size());
    for (auto *argument : arguments) {
      parameterTypes.push_back(argument->getType());
    }
    auto callee = declareCFunction(symbol, f128Type, parameterTypes);
    return builder_.CreateCall(callee, arguments, std::string(name));
  }

  auto *resultStorage =
      createFunctionEntryAlloca(f128Type, std::string(name) + ".storage");
  std::vector<llvm::Value *> runtimeArguments{resultStorage};
  std::vector<llvm::Type *> parameterTypes{builder_.getPtrTy()};
  runtimeArguments.reserve(arguments.size() + 1U);
  parameterTypes.reserve(arguments.size() + 1U);
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    auto *argument = arguments[index];
    if (f128Arguments[index]) {
      auto *storage = createFunctionEntryAlloca(
          f128Type, std::string(name) + ".arg" + std::to_string(index));
      builder_.CreateStore(argument, storage);
      runtimeArguments.push_back(storage);
      parameterTypes.push_back(builder_.getPtrTy());
    } else {
      runtimeArguments.push_back(argument);
      parameterTypes.push_back(argument->getType());
    }
  }
  auto callee = declareCFunction(symbol, builder_.getVoidTy(), parameterTypes);
  builder_.CreateCall(callee, runtimeArguments);
  return builder_.CreateLoad(f128Type, resultStorage, std::string(name));
}

llvm::Value *
LlvmEmitter::emitF128ScalarCall(std::string_view symbol, llvm::Type *returnType,
                                std::vector<llvm::Value *> arguments,
                                const std::vector<bool> &f128Arguments,
                                std::string_view name) {
  if (!hasMatchingF128Arguments(arguments, f128Arguments)) {
    addDiagnostic("internal f128 runtime argument mismatch");
    return nullptr;
  }

  std::vector<llvm::Value *> runtimeArguments;
  std::vector<llvm::Type *> parameterTypes;
  runtimeArguments.reserve(arguments.size());
  parameterTypes.reserve(arguments.size());
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    auto *argument = arguments[index];
    if (usesSoftwareF128() && f128Arguments[index]) {
      auto *storage = createFunctionEntryAlloca(argument->getType(),
                                                std::string(name) + ".arg" +
                                                    std::to_string(index));
      builder_.CreateStore(argument, storage);
      runtimeArguments.push_back(storage);
      parameterTypes.push_back(builder_.getPtrTy());
    } else {
      runtimeArguments.push_back(argument);
      parameterTypes.push_back(argument->getType());
    }
  }
  auto callee = declareCFunction(symbol, returnType, parameterTypes);
  return builder_.CreateCall(callee, runtimeArguments, std::string(name));
}

} // namespace hitsimple::codegen
