#include "LlvmEmitter.h"

#include "hitsimple/literal/Literal.h"

#include <cstdint>

namespace hitsimple::codegen {

llvm::Value *LlvmEmitter::firstBytePointer(llvm::Type *storageType,
                                           llvm::Value *storage) {
  if (!storageType->isArrayTy()) {
    return builder_.CreateInBoundsGEP(builder_.getInt8Ty(), storage,
                                      builder_.getInt32(0), "addr");
  }
  auto *zero = builder_.getInt32(0);
  return builder_.CreateInBoundsGEP(storageType, storage, {zero, zero}, "addr");
}

llvm::Value *LlvmEmitter::bytePointer(llvm::Type *storageType,
                                      llvm::Value *storage,
                                      std::size_t offset,
                                      std::string_view name) {
  if (!storageType->isArrayTy()) {
    return builder_.CreateInBoundsGEP(
        builder_.getInt8Ty(), storage,
        builder_.getInt64(static_cast<std::uint64_t>(offset)),
        std::string(name));
  }
  auto *zero = builder_.getInt32(0);
  auto *byteOffset = builder_.getInt32(static_cast<std::uint32_t>(offset));
  return builder_.CreateInBoundsGEP(storageType, storage, {zero, byteOffset},
                                    std::string(name));
}

llvm::AllocaInst *LlvmEmitter::createFunctionEntryAlloca(
    llvm::Type *storageType, std::string_view name) {
  if (functionEntryBlock_ == nullptr ||
      functionEntryBlock_->getTerminator() == nullptr) {
    return builder_.CreateAlloca(storageType, nullptr, std::string(name));
  }

  llvm::IRBuilder<> entryBuilder(context_);
  entryBuilder.SetInsertPoint(functionEntryBlock_->getTerminator());
  return entryBuilder.CreateAlloca(storageType, nullptr, std::string(name));
}

void LlvmEmitter::registerLocalObject(llvm::Value *storage,
                                      std::size_t byteLength) {
  if (!hasRuntimeSafetyChecks() || storage == nullptr || byteLength == 0) {
    return;
  }

  if (functionEntryBlock_ != nullptr &&
      functionEntryBlock_->getTerminator() != nullptr) {
    llvm::IRBuilder<> entryBuilder(context_);
    entryBuilder.SetInsertPoint(functionEntryBlock_->getTerminator());
    entryBuilder.CreateCall(declareRegisterLocalObject(),
                            {storage, entryBuilder.getInt64(byteLength)});
    return;
  }

  builder_.CreateCall(declareRegisterLocalObject(),
                      {storage, builder_.getInt64(byteLength)});
}

void LlvmEmitter::registerStaticObject(llvm::Value *storage,
                                       std::size_t byteLength) {
  if (!hasRuntimeSafetyChecks() || storage == nullptr || byteLength == 0) {
    return;
  }
  builder_.CreateCall(declareRegisterStaticObject(),
                      {storage, builder_.getInt64(byteLength)});
}

void LlvmEmitter::emitRuntimeFrameEnter() {
  if (hasRuntimeSafetyChecks()) {
    builder_.CreateCall(declareRuntimeFrameEnter());
  }
}

void LlvmEmitter::emitRuntimeFrameExit() {
  if (hasRuntimeSafetyChecks() && runtimeFrameActive_) {
    builder_.CreateCall(declareRuntimeFrameExit());
  }
}

llvm::Value *LlvmEmitter::effectObjectPointer(const hir::Function &function,
                                              std::string_view object) {
  for (const auto &parameter : function.parameters) {
    if (parameter.name != object) {
      continue;
    }
    if (parameter.templateName != "addr") {
      addDiagnostic("effect contract object '" + std::string(object) +
                    "' is not an addr parameter");
      return nullptr;
    }
    const auto local = locals_.find(parameter.bindingName);
    if (local == locals_.end()) {
      addDiagnostic("effect contract parameter '" + std::string(object) +
                    "' is unavailable during code generation");
      return nullptr;
    }
    auto *integerType = integerTypeForByteLength(parameter.byteLength);
    if (integerType == nullptr) {
      return nullptr;
    }
    auto *value = builder_.CreateLoad(integerType, local->second.storage,
                                      std::string(object) + ".effect.addr");
    return builder_.CreateIntToPtr(value, builder_.getPtrTy(),
                                   std::string(object) + ".effect.ptr");
  }
  const auto global = globals_.find(std::string(object));
  if (global == globals_.end()) {
    addDiagnostic("effect contract object '" + std::string(object) +
                  "' is unavailable during code generation");
    return nullptr;
  }
  return firstBytePointer(global->second.storageType, global->second.storage);
}

llvm::Value *LlvmEmitter::effectRangeLength(const hir::Function &function,
                                            const hir::EffectRange &range) {
  if (range.range == "all") {
    const auto global = globals_.find(range.object);
    if (global == globals_.end()) {
      addDiagnostic("effect range 'all' requires static storage object '" +
                    range.object + "'");
      return nullptr;
    }
    return builder_.getInt64(global->second.byteLength);
  }
  if (const auto literal = literal::parseUnsignedIntegerLiteral(range.range)) {
    return builder_.getInt64(*literal);
  }
  for (const auto &parameter : function.parameters) {
    if (parameter.name != range.range) {
      continue;
    }
    const auto local = locals_.find(parameter.bindingName);
    auto *integerType = integerTypeForByteLength(parameter.byteLength);
    if (local == locals_.end() || integerType == nullptr) {
      addDiagnostic("effect range parameter '" + range.range +
                    "' is unavailable during code generation");
      return nullptr;
    }
    auto *value = builder_.CreateLoad(integerType, local->second.storage,
                                      range.range + ".effect.length");
    const auto bits = integerType->getBitWidth();
    if (bits < 64U) {
      return builder_.CreateZExt(value, builder_.getInt64Ty(),
                                 range.range + ".effect.length.wide");
    }
    if (bits > 64U) {
      return builder_.CreateTrunc(value, builder_.getInt64Ty(),
                                  range.range + ".effect.length.narrow");
    }
    return value;
  }
  addDiagnostic("effect range '" + range.range + "' is unavailable during code generation");
  return nullptr;
}

void LlvmEmitter::emitEffectContractEnter(const hir::Function &function) {
  if (!hasRuntimeSafetyChecks() || !function.effectContract.isExplicit ||
      (function.effectContract.flags & hir::EffectUnknown) != 0U) {
    return;
  }
  constexpr std::uint32_t pure = 1U << 0U;
  constexpr std::uint32_t readonly = 1U << 1U;
  constexpr std::uint32_t nothrow = 1U << 2U;
  constexpr std::uint32_t strict = 1U << 3U;
  std::uint32_t flags = strict;
  if ((function.effectContract.flags & hir::EffectPure) != 0U) flags |= pure;
  if ((function.effectContract.flags & hir::EffectReadonly) != 0U) flags |= readonly;
  if ((function.effectContract.flags & hir::EffectNothrow) != 0U ||
      (function.effectContract.flags & hir::EffectPure) != 0U) flags |= nothrow;
  builder_.CreateCall(declareEffectContractEnter(), {builder_.getInt32(flags)});
  effectContractActive_ = true;

  for (const auto &range : function.effectContract.ranges) {
    auto *object = effectObjectPointer(function, range.object);
    auto *length = effectRangeLength(function, range);
    if (object == nullptr || length == nullptr) {
      return;
    }
    const auto access = range.access == hir::EffectAccess::Read ? 0U : 1U;
    builder_.CreateCall(declareEffectContractAddRange(),
                        {object, length, builder_.getInt32(access)});
  }

  for (const auto &[left, right] : function.effectContract.noAlias) {
    for (const auto &leftRange : function.effectContract.ranges) {
      if (leftRange.object != left) continue;
      for (const auto &rightRange : function.effectContract.ranges) {
        if (rightRange.object != right) continue;
        auto *leftObject = effectObjectPointer(function, leftRange.object);
        auto *leftLength = effectRangeLength(function, leftRange);
        auto *rightObject = effectObjectPointer(function, rightRange.object);
        auto *rightLength = effectRangeLength(function, rightRange);
        if (leftObject == nullptr || leftLength == nullptr || rightObject == nullptr ||
            rightLength == nullptr) {
          return;
        }
        builder_.CreateCall(declareEffectNoAliasCheck(),
                            {leftObject, leftLength, rightObject, rightLength});
      }
    }
  }
}

void LlvmEmitter::emitEffectContractExit() {
  if (hasRuntimeSafetyChecks() && effectContractActive_) {
    builder_.CreateCall(declareEffectContractExit());
  }
}

void LlvmEmitter::emitEffectRead(llvm::Value *pointer, llvm::Value *byteLength) {
  if (hasRuntimeSafetyChecks()) {
    builder_.CreateCall(declareEffectReadCheck(), {pointer, byteLength});
  }
}

void LlvmEmitter::emitEffectWrite(llvm::Value *pointer, llvm::Value *byteLength) {
  if (hasRuntimeSafetyChecks()) {
    builder_.CreateCall(declareEffectWriteCheck(), {pointer, byteLength});
  }
}

void LlvmEmitter::emitEffectEvent(std::uint32_t event) {
  if (hasRuntimeSafetyChecks()) {
    builder_.CreateCall(declareEffectEventCheck(), {builder_.getInt32(event)});
  }
}

llvm::FunctionCallee LlvmEmitter::declarePrintf() {
  auto *printfType = llvm::FunctionType::get(builder_.getInt32Ty(),
                                             {builder_.getPtrTy()}, true);
  return module_->getOrInsertFunction("printf", printfType);
}

llvm::FunctionCallee
LlvmEmitter::declareCFunction(std::string_view name,
                              llvm::Type *returnType,
                              std::vector<llvm::Type *> parameters,
                              bool variadic) {
  auto *type = llvm::FunctionType::get(returnType, parameters, variadic);
  return module_->getOrInsertFunction(std::string(name), type);
}

llvm::FunctionCallee LlvmEmitter::declareMalloc() {
  auto *type = llvm::FunctionType::get(builder_.getPtrTy(),
                                       {builder_.getInt64Ty()}, false);
  return module_->getOrInsertFunction("malloc", type);
}

llvm::FunctionCallee LlvmEmitter::declareCalloc() {
  auto *type = llvm::FunctionType::get(
      builder_.getPtrTy(), {builder_.getInt64Ty(), builder_.getInt64Ty()},
      false);
  return module_->getOrInsertFunction("calloc", type);
}

llvm::FunctionCallee LlvmEmitter::declareRealloc() {
  auto *type = llvm::FunctionType::get(
      builder_.getPtrTy(), {builder_.getPtrTy(), builder_.getInt64Ty()},
      false);
  return module_->getOrInsertFunction("realloc", type);
}

llvm::FunctionCallee LlvmEmitter::declareFree() {
  auto *type =
      llvm::FunctionType::get(builder_.getVoidTy(), {builder_.getPtrTy()},
                              false);
  return module_->getOrInsertFunction("free", type);
}

llvm::FunctionCallee LlvmEmitter::declareCheckedAlloc() {
  auto *type = llvm::FunctionType::get(builder_.getPtrTy(),
                                       {builder_.getInt64Ty()}, false);
  return module_->getOrInsertFunction("hs_alloc", type);
}

llvm::FunctionCallee LlvmEmitter::declareCheckedCalloc() {
  auto *type = llvm::FunctionType::get(builder_.getPtrTy(),
                                       {builder_.getInt64Ty(),
                                        builder_.getInt64Ty()}, false);
  return module_->getOrInsertFunction("hs_calloc", type);
}

llvm::FunctionCallee LlvmEmitter::declareCheckedRealloc() {
  auto *type = llvm::FunctionType::get(
      builder_.getPtrTy(), {builder_.getPtrTy(), builder_.getInt64Ty()},
      false);
  return module_->getOrInsertFunction("hs_realloc", type);
}

llvm::FunctionCallee LlvmEmitter::declareCheckedFree() {
  auto *type =
      llvm::FunctionType::get(builder_.getVoidTy(), {builder_.getPtrTy()},
                              false);
  return module_->getOrInsertFunction("hs_free", type);
}

llvm::FunctionCallee LlvmEmitter::declareCheckLoad() {
  auto *type = llvm::FunctionType::get(
      builder_.getVoidTy(), {builder_.getPtrTy(), builder_.getInt64Ty()},
      false);
  return module_->getOrInsertFunction("hs_check_load", type);
}

llvm::FunctionCallee LlvmEmitter::declareCheckStore() {
  auto *type = llvm::FunctionType::get(
      builder_.getVoidTy(), {builder_.getPtrTy(), builder_.getInt64Ty()},
      false);
  return module_->getOrInsertFunction("hs_check_store", type);
}

llvm::FunctionCallee LlvmEmitter::declareCheckViewLength() {
  auto *type = llvm::FunctionType::get(
      builder_.getVoidTy(), {builder_.getInt64Ty(), builder_.getInt64Ty()},
      false);
  return module_->getOrInsertFunction("hs_check_view_length", type);
}

llvm::FunctionCallee LlvmEmitter::declareReverseBytes() {
  auto *type = llvm::FunctionType::get(
      builder_.getVoidTy(),
      {builder_.getPtrTy(), builder_.getPtrTy(), builder_.getInt64Ty()},
      false);
  return module_->getOrInsertFunction("hs_reverse_bytes", type);
}

llvm::FunctionCallee LlvmEmitter::declareRegisterLocalObject() {
  auto *type = llvm::FunctionType::get(
      builder_.getVoidTy(), {builder_.getPtrTy(), builder_.getInt64Ty()},
      false);
  return module_->getOrInsertFunction("hs_register_local", type);
}

llvm::FunctionCallee LlvmEmitter::declareRegisterStaticObject() {
  auto *type = llvm::FunctionType::get(
      builder_.getVoidTy(), {builder_.getPtrTy(), builder_.getInt64Ty()},
      false);
  return module_->getOrInsertFunction("hs_register_static", type);
}

llvm::FunctionCallee LlvmEmitter::declareRuntimeFrameEnter() {
  auto *type = llvm::FunctionType::get(builder_.getVoidTy(), {}, false);
  return module_->getOrInsertFunction("hs_frame_enter", type);
}

llvm::FunctionCallee LlvmEmitter::declareRuntimeFrameExit() {
  auto *type = llvm::FunctionType::get(builder_.getVoidTy(), {}, false);
  return module_->getOrInsertFunction("hs_frame_exit", type);
}

llvm::FunctionCallee LlvmEmitter::declareEffectContractEnter() {
  auto *type = llvm::FunctionType::get(builder_.getVoidTy(), {builder_.getInt32Ty()}, false);
  return module_->getOrInsertFunction("hs_effect_enter", type);
}

llvm::FunctionCallee LlvmEmitter::declareEffectContractExit() {
  auto *type = llvm::FunctionType::get(builder_.getVoidTy(), {}, false);
  return module_->getOrInsertFunction("hs_effect_exit", type);
}

llvm::FunctionCallee LlvmEmitter::declareEffectContractAddRange() {
  auto *type = llvm::FunctionType::get(
      builder_.getVoidTy(), {builder_.getPtrTy(), builder_.getInt64Ty(), builder_.getInt32Ty()}, false);
  return module_->getOrInsertFunction("hs_effect_add_range", type);
}

llvm::FunctionCallee LlvmEmitter::declareEffectNoAliasCheck() {
  auto *type = llvm::FunctionType::get(builder_.getVoidTy(),
                                       {builder_.getPtrTy(), builder_.getInt64Ty(),
                                        builder_.getPtrTy(), builder_.getInt64Ty()}, false);
  return module_->getOrInsertFunction("hs_effect_check_noalias", type);
}

llvm::FunctionCallee LlvmEmitter::declareEffectReadCheck() {
  auto *type = llvm::FunctionType::get(builder_.getVoidTy(),
                                       {builder_.getPtrTy(), builder_.getInt64Ty()}, false);
  return module_->getOrInsertFunction("hs_effect_check_read", type);
}

llvm::FunctionCallee LlvmEmitter::declareEffectWriteCheck() {
  auto *type = llvm::FunctionType::get(builder_.getVoidTy(),
                                       {builder_.getPtrTy(), builder_.getInt64Ty()}, false);
  return module_->getOrInsertFunction("hs_effect_check_write", type);
}

llvm::FunctionCallee LlvmEmitter::declareEffectEventCheck() {
  auto *type = llvm::FunctionType::get(builder_.getVoidTy(), {builder_.getInt32Ty()}, false);
  return module_->getOrInsertFunction("hs_effect_event", type);
}

std::string LlvmEmitter::decodeStringLiteral(std::string_view text) {
  const auto decoded = literal::decodeStringLiteral(text);
  if (!decoded) {
    return {};
  }
  return decoded.bytes;
}

} // namespace hitsimple::codegen
