#include "LlvmEmitter.h"

#include "hitsimple/literal/Literal.h"

#include <cstdint>
#include <filesystem>
#include <algorithm>

namespace hitsimple::codegen {

llvm::Align LlvmEmitter::alignmentAt(std::size_t baseAlignment,
                                     std::size_t offset) const {
  return llvm::commonAlignment(
      llvm::Align(std::max<std::size_t>(baseAlignment, 1U)), offset);
}

llvm::Value *LlvmEmitter::firstBytePointer(llvm::Type *storageType,
                                           llvm::Value *storage) {
  if (!storageType->isArrayTy()) {
    return storage;
  }
  auto *zero = builder_.getInt32(0);
  return builder_.CreateInBoundsGEP(storageType, storage, {zero, zero}, "addr");
}

llvm::Value *LlvmEmitter::bytePointer(llvm::Type *storageType,
                                      llvm::Value *storage,
                                      std::size_t offset,
                                      std::string_view name) {
  if (!storageType->isArrayTy()) {
    if (offset == 0U) {
      return storage;
    }
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
    (void)emitCheckedRuntimeCall(
        entryBuilder, declareRegisterLocalObject(),
        {storage, entryBuilder.getInt64(byteLength)});
    return;
  }

  (void)emitCheckedRuntimeCall(declareRegisterLocalObject(),
                               {storage, builder_.getInt64(byteLength)});
}

void LlvmEmitter::registerStaticObject(llvm::Value *storage,
                                       std::size_t byteLength) {
  if (!hasRuntimeSafetyChecks() || storage == nullptr || byteLength == 0) {
    return;
  }
  (void)emitCheckedRuntimeCall(declareRegisterStaticObject(),
                               {storage, builder_.getInt64(byteLength)});
}

void LlvmEmitter::emitRuntimeFrameEnter() {
  if (hasRuntimeSafetyChecks()) {
    (void)emitCheckedRuntimeCall(declareRuntimeFrameEnter(), {});
  }
}

void LlvmEmitter::emitRuntimeFrameExit() {
  if (hasRuntimeSafetyChecks() && runtimeFrameActive_) {
    (void)emitCheckedRuntimeCall(declareRuntimeFrameExit(), {});
  }
}

void LlvmEmitter::emitRuntimeSourceLocation() {
  emitRuntimeSourceLocation(builder_);
}

void LlvmEmitter::emitRuntimeSourceLocation(llvm::IRBuilder<> &builder) {
  if (!hasRuntimeSafetyChecks() || runtimeSourceLocationEmissionSuppressed_ ||
      !currentDiagnosticRange_ ||
      currentDiagnosticRange_->begin.file.empty()) {
    return;
  }

  const auto path = std::filesystem::path(currentDiagnosticRange_->begin.file)
                        .lexically_normal()
                        .generic_string();
  if (path.empty() || path == ".") {
    return;
  }

  const auto [file, inserted] = runtimeSourceFilePointers_.try_emplace(path);
  if (inserted) {
    file->second = builder_.CreateGlobalStringPtr(path, "runtime.source.file");
  }

  auto callee = declareCFunction("hs_set_source_location",
                                 builder.getVoidTy(),
                                 {builder.getPtrTy(), builder.getInt64Ty(),
                                  builder.getInt64Ty()});
  builder.CreateCall(
      callee,
      {file->second,
       builder.getInt64(static_cast<std::uint64_t>(
           currentDiagnosticRange_->begin.line)),
       builder.getInt64(static_cast<std::uint64_t>(
           currentDiagnosticRange_->begin.column))});
}

llvm::CallInst *LlvmEmitter::emitCheckedRuntimeCall(
    llvm::FunctionCallee callee, llvm::ArrayRef<llvm::Value *> arguments,
    std::string_view name) {
  return emitCheckedRuntimeCall(builder_, callee, arguments, name);
}

llvm::CallInst *LlvmEmitter::emitCheckedRuntimeCall(
    llvm::IRBuilder<> &builder, llvm::FunctionCallee callee,
    llvm::ArrayRef<llvm::Value *> arguments, std::string_view name) {
  if (!hasRuntimeSafetyChecks() || runtimeSourceLocationEmissionSuppressed_ ||
      !currentDiagnosticRange_ ||
      currentDiagnosticRange_->begin.file.empty()) {
    if (name.empty()) {
      return builder.CreateCall(callee, arguments);
    }
    return builder.CreateCall(callee, arguments, std::string(name));
  }

  const auto path = std::filesystem::path(currentDiagnosticRange_->begin.file)
                        .lexically_normal()
                        .generic_string();
  if (path.empty() || path == ".") {
    if (name.empty()) {
      return builder.CreateCall(callee, arguments);
    }
    return builder.CreateCall(callee, arguments, std::string(name));
  }

  const auto [file, inserted] = runtimeSourceFilePointers_.try_emplace(path);
  if (inserted) {
    file->second = builder.CreateGlobalStringPtr(path, "runtime.source.file");
  }

  auto *wrapper = llvm::Function::Create(
      callee.getFunctionType(), llvm::GlobalValue::InternalLinkage,
      "hs.runtime.location." + std::to_string(runtimeSourceWrapperCount_++),
      module_.get());
  wrapper->addFnAttr(llvm::Attribute::NoInline);
  auto *entry = llvm::BasicBlock::Create(context_, "entry", wrapper);
  llvm::IRBuilder<> wrapperBuilder(entry);
  auto sourceLocation = declareCFunction(
      "hs_set_source_location", wrapperBuilder.getVoidTy(),
      {wrapperBuilder.getPtrTy(), wrapperBuilder.getInt64Ty(),
       wrapperBuilder.getInt64Ty()});
  wrapperBuilder.CreateCall(
      sourceLocation,
      {file->second,
       wrapperBuilder.getInt64(static_cast<std::uint64_t>(
           currentDiagnosticRange_->begin.line)),
       wrapperBuilder.getInt64(static_cast<std::uint64_t>(
           currentDiagnosticRange_->begin.column))});

  std::vector<llvm::Value *> wrapperArguments;
  wrapperArguments.reserve(wrapper->arg_size());
  for (auto &argument : wrapper->args()) {
    wrapperArguments.push_back(&argument);
  }
  auto *runtimeCall = wrapperBuilder.CreateCall(callee, wrapperArguments);
  if (callee.getFunctionType()->getReturnType()->isVoidTy()) {
    wrapperBuilder.CreateRetVoid();
  } else {
    wrapperBuilder.CreateRet(runtimeCall);
  }

  if (name.empty()) {
    return builder.CreateCall(wrapper, arguments);
  }
  return builder.CreateCall(wrapper, arguments, std::string(name));
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

llvm::FunctionCallee LlvmEmitter::declareViewAnyNonZero() {
  auto *type = llvm::FunctionType::get(
      builder_.getInt32Ty(), {builder_.getPtrTy(), builder_.getInt64Ty()},
      false);
  return module_->getOrInsertFunction("hs_view_any_nonzero", type);
}

llvm::FunctionCallee LlvmEmitter::declareCheckedDivisionByZero() {
  auto *type = llvm::FunctionType::get(builder_.getVoidTy(), {}, false);
  return module_->getOrInsertFunction("hs_checked_division_by_zero", type);
}

llvm::FunctionCallee LlvmEmitter::declareCheckedNegativeShift() {
  auto *type = llvm::FunctionType::get(builder_.getVoidTy(), {}, false);
  return module_->getOrInsertFunction("hs_checked_negative_shift", type);
}

llvm::FunctionCallee LlvmEmitter::declareCheckedNegativeExponent() {
  auto *type = llvm::FunctionType::get(builder_.getVoidTy(), {}, false);
  return module_->getOrInsertFunction("hs_checked_negative_exponent", type);
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
