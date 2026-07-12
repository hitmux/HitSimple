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

std::string LlvmEmitter::decodeStringLiteral(std::string_view text) {
  const auto decoded = literal::decodeStringLiteral(text);
  if (!decoded) {
    return {};
  }
  return decoded.bytes;
}

} // namespace hitsimple::codegen
