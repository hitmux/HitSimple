#include "LlvmEmitter.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Alignment.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace hitsimple::codegen {
namespace {

llvm::Constant *constantByte(llvm::LLVMContext &context, unsigned char value) {
  return llvm::ConstantInt::get(llvm::Type::getInt8Ty(context), value);
}

std::size_t floatVarargByteLength(const hir::Expr &expression) {
  if (const auto *literal = dynamic_cast<const hir::FloatLiteral *>(&expression)) {
    return literal->byteLength;
  }
  if (const auto *binary = dynamic_cast<const hir::FloatBinaryExpr *>(&expression)) {
    return binary->byteLength;
  }
  if (const auto *conversion = dynamic_cast<const hir::ToFloatExpr *>(&expression)) {
    return conversion->byteLength;
  }
  if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
    return variable->byteLength;
  }
  if (const auto *call = dynamic_cast<const hir::CallExpr *>(&expression)) {
    return call->byteLength;
  }
  return 8;
}

std::vector<char> collectPrintfSpecifiers(const std::string &format) {
  std::vector<char> specifiers;
  for (std::size_t index = 0; index < format.size(); ++index) {
    if (format[index] != '%') {
      continue;
    }
    ++index;
    if (index >= format.size()) {
      break;
    }
    if (format[index] == '%') {
      continue;
    }
    while (index < format.size() &&
           std::isdigit(static_cast<unsigned char>(format[index]))) {
      ++index;
    }
    if (index >= format.size()) {
      break;
    }
    specifiers.push_back(format[index]);
  }
  return specifiers;
}

} // namespace

void LlvmEmitter::emit(const hir::Block &block) {
  for (const auto &statement : block.statements) {
    if (builder_.GetInsertBlock()->getTerminator()) {
      if (dynamic_cast<const hir::Label *>(statement.get()) == nullptr) {
        continue;
      }
    }
    emit(*statement);
  }
}

void LlvmEmitter::emit(const hir::Stmt &statement) {
  if (const auto *list = dynamic_cast<const hir::StatementList *>(&statement)) {
    for (const auto &item : list->statements) {
      emit(*item);
      if (!diagnostics_.empty()) {
        return;
      }
    }
    return;
  }

  if (const auto *local = dynamic_cast<const hir::LocalMemory *>(&statement)) {
    emit(*local);
    return;
  }

  if (const auto *store = dynamic_cast<const hir::IntegerStore *>(&statement)) {
    emit(*store);
    return;
  }

  if (const auto *store = dynamic_cast<const hir::FloatStore *>(&statement)) {
    emit(*store);
    return;
  }

  if (const auto *store = dynamic_cast<const hir::StringStore *>(&statement)) {
    emit(*store);
    return;
  }

  if (const auto *store =
          dynamic_cast<const hir::StringCopyStore *>(&statement)) {
    emit(*store);
    return;
  }

  if (const auto *store = dynamic_cast<const hir::BoolStore *>(&statement)) {
    emit(*store);
    return;
  }

  if (const auto *store = dynamic_cast<const hir::PointerStore *>(&statement)) {
    emit(*store);
    return;
  }

  if (const auto *call = dynamic_cast<const hir::Call *>(&statement)) {
    emit(*call);
    return;
  }

  if (const auto *call = dynamic_cast<const hir::UserTemplateOpCall *>(&statement)) {
    emit(*call);
    return;
  }

  if (const auto *call =
          dynamic_cast<const hir::UserTemplateFormatCall *>(&statement)) {
    emit(*call);
    return;
  }

  if (const auto *call =
          dynamic_cast<const hir::MultiReturnCallStore *>(&statement)) {
    emit(*call);
    return;
  }

  if (const auto *call = dynamic_cast<const hir::InputCallStore *>(&statement)) {
    emit(*call);
    return;
  }

  if (const auto *ret = dynamic_cast<const hir::Return *>(&statement)) {
    emit(*ret);
    return;
  }

  if (const auto *ifStmt = dynamic_cast<const hir::If *>(&statement)) {
    emit(*ifStmt);
    return;
  }

  if (const auto *whileStmt = dynamic_cast<const hir::While *>(&statement)) {
    emit(*whileStmt);
    return;
  }

  if (const auto *forStmt = dynamic_cast<const hir::For *>(&statement)) {
    emit(*forStmt);
    return;
  }

  if (dynamic_cast<const hir::Break *>(&statement) != nullptr) {
    emitBreak();
    return;
  }

  if (dynamic_cast<const hir::Continue *>(&statement) != nullptr) {
    emitContinue();
    return;
  }

  if (const auto *gotoStmt = dynamic_cast<const hir::Goto *>(&statement)) {
    emit(*gotoStmt);
    return;
  }

  if (const auto *label = dynamic_cast<const hir::Label *>(&statement)) {
    emit(*label);
    return;
  }

  if (const auto *throwStmt = dynamic_cast<const hir::Throw *>(&statement)) {
    emit(*throwStmt);
    return;
  }

  if (const auto *tryCatch = dynamic_cast<const hir::TryCatch *>(&statement)) {
    emit(*tryCatch);
    return;
  }

  addDiagnostic("unsupported HIR statement");
}

void LlvmEmitter::emit(const hir::LocalMemory &local) {
  if (locals_.find(local.bindingName) != locals_.end()) {
    addDiagnostic("duplicate local '" + local.name + "'");
    return;
  }

  auto *storageType =
      llvm::ArrayType::get(builder_.getInt8Ty(), local.byteLength);
  if (local.storage == hir::MemoryStorage::StaticLocal) {
    auto *initializer = llvm::ConstantAggregateZero::get(storageType);
    auto *storage = new llvm::GlobalVariable(*module_, storageType, false,
                                             llvm::GlobalValue::InternalLinkage,
                                             initializer, local.bindingName);
    storage->setAlignment(llvm::Align(alignof(std::max_align_t)));
    locals_.emplace(local.bindingName,
                    Local{storage, storageType, local.byteLength, std::nullopt});
    registerStaticObject(storage, local.byteLength);
    return;
  }

  auto *storage = createFunctionEntryAlloca(storageType, local.bindingName);
  storage->setAlignment(llvm::Align(alignof(std::max_align_t)));
  locals_.emplace(local.bindingName,
                  Local{storage, storageType, local.byteLength, std::nullopt});
  registerLocalObject(storage, local.byteLength);
}

void LlvmEmitter::emit(const hir::IntegerStore &store) {
  const Local *target = nullptr;
  if (const auto local = locals_.find(store.bindingName);
      local != locals_.end()) {
    target = &local->second;
  } else if (const auto global = globals_.find(store.bindingName);
             global != globals_.end()) {
    target = &global->second;
  }
  if (target == nullptr) {
    addDiagnostic("unknown local '" + store.target + "'");
    return;
  }

  auto *value = emitIntegerValue(*store.value, store.targetByteLength);
  if (!value) {
    return;
  }

  builder_.CreateStore(value,
                       bytePointer(target->storageType, target->storage,
                                   store.offset, store.target + ".addr"));
}

void LlvmEmitter::emit(const hir::FloatStore &store) {
  const Local *target = nullptr;
  if (const auto local = locals_.find(store.bindingName);
      local != locals_.end()) {
    target = &local->second;
  } else if (const auto global = globals_.find(store.bindingName);
             global != globals_.end()) {
    target = &global->second;
  }
  if (target == nullptr) {
    addDiagnostic("unknown local '" + store.target + "'");
    return;
  }

  auto *value = emitFloatValue(*store.value, store.targetByteLength);
  if (!value) {
    return;
  }

  builder_.CreateStore(value,
                       bytePointer(target->storageType, target->storage,
                                   store.offset, store.target + ".addr"));
}

void LlvmEmitter::emit(const hir::StringStore &store) {
  const Local *target = nullptr;
  if (const auto local = locals_.find(store.bindingName);
      local != locals_.end()) {
    target = &local->second;
  } else if (const auto global = globals_.find(store.bindingName);
             global != globals_.end()) {
    target = &global->second;
  }
  if (target == nullptr) {
    addDiagnostic("unknown local '" + store.target + "'");
    return;
  }

  const auto decoded = decodeStringLiteral(store.value);
  const std::size_t copiedBytes =
      store.targetByteLength == 0
          ? 0
          : std::min(decoded.size(), store.targetByteLength - 1);

  for (std::size_t index = 0; index < store.targetByteLength; ++index) {
    unsigned char byte = 0;
    if (index < copiedBytes) {
      byte = static_cast<unsigned char>(decoded[index]);
    }
    auto *offset =
        builder_.getInt32(static_cast<std::uint32_t>(store.offset + index));
    auto *pointer =
        builder_.CreateInBoundsGEP(target->storageType, target->storage,
                                   {builder_.getInt32(0), offset}, "str.addr");
    builder_.CreateStore(constantByte(context_, byte), pointer);
  }
}

void LlvmEmitter::emit(const hir::StringCopyStore &store) {
  const Local *target = nullptr;
  if (const auto local = locals_.find(store.bindingName);
      local != locals_.end()) {
    target = &local->second;
  } else if (const auto global = globals_.find(store.bindingName);
             global != globals_.end()) {
    target = &global->second;
  }
  const Local *source = nullptr;
  if (const auto local = locals_.find(store.sourceBindingName);
      local != locals_.end()) {
    source = &local->second;
  } else if (const auto global = globals_.find(store.sourceBindingName);
             global != globals_.end()) {
    source = &global->second;
  }
  if (target == nullptr) {
    addDiagnostic("unknown local '" + store.target + "'");
    return;
  }
  if (source == nullptr) {
    addDiagnostic("unknown local '" + store.source + "'");
    return;
  }

  auto *activeStorage = builder_.CreateAlloca(builder_.getInt1Ty(), nullptr,
                                              "str.copy.active");
  builder_.CreateStore(builder_.getTrue(), activeStorage);
  for (std::size_t index = 0; index < store.targetByteLength; ++index) {
    llvm::Value *byte = constantByte(context_, 0);
    auto *active = builder_.CreateLoad(builder_.getInt1Ty(), activeStorage,
                                       "str.copying");
    if (index + 1 < store.targetByteLength && index < store.sourceByteLength) {
      auto *sourceOffset =
          builder_.getInt32(static_cast<std::uint32_t>(
              store.sourceOffset + index));
      auto *sourcePointer = builder_.CreateInBoundsGEP(
          source->storageType, source->storage,
          {builder_.getInt32(0), sourceOffset}, "str.src");
      auto *sourceByte =
          builder_.CreateLoad(builder_.getInt8Ty(), sourcePointer, "str.byte");
      byte = builder_.CreateSelect(active, sourceByte, constantByte(context_, 0),
                                   "str.copy.byte");
      auto *nonZero = builder_.CreateICmpNE(sourceByte, constantByte(context_, 0),
                                            "str.nonzero");
      builder_.CreateStore(builder_.CreateAnd(active, nonZero), activeStorage);
    }
    auto *targetOffset =
        builder_.getInt32(static_cast<std::uint32_t>(
            store.targetOffset + index));
    auto *targetPointer = builder_.CreateInBoundsGEP(
        target->storageType, target->storage,
        {builder_.getInt32(0), targetOffset}, "str.dst");
    builder_.CreateStore(byte, targetPointer);
  }
}

void LlvmEmitter::emit(const hir::BoolStore &store) {
  const Local *target = nullptr;
  if (const auto local = locals_.find(store.bindingName);
      local != locals_.end()) {
    target = &local->second;
  } else if (const auto global = globals_.find(store.bindingName);
             global != globals_.end()) {
    target = &global->second;
  }
  if (target == nullptr) {
    addDiagnostic("unknown local '" + store.target + "'");
    return;
  }

  auto *value = emitIntegerValue(*store.value, 4);
  if (!value) {
    return;
  }
  auto *nonZero = builder_.CreateICmpNE(
      value, llvm::ConstantInt::get(value->getType(), 0), "bool.nonzero");
  auto *normalized = builder_.CreateZExt(nonZero, builder_.getInt8Ty());

  for (std::size_t index = 0; index < store.targetByteLength; ++index) {
    auto *offset =
        builder_.getInt32(static_cast<std::uint32_t>(store.offset + index));
    auto *pointer =
        builder_.CreateInBoundsGEP(target->storageType, target->storage,
                                   {builder_.getInt32(0), offset}, "bool.addr");
    builder_.CreateStore(index == 0 ? normalized : constantByte(context_, 0),
                         pointer);
  }
}

void LlvmEmitter::emit(const hir::PointerStore &store) {
  auto *addressValue = emitIntegerValue(*store.address, sizeof(void *));
  if (!addressValue) {
    return;
  }
  auto *targetType = integerTypeForByteLength(store.targetByteLength);
  if (!targetType) {
    return;
  }
  auto *value = emitIntegerValue(*store.value, store.targetByteLength);
  if (!value) {
    return;
  }
  auto *pointer = builder_.CreateIntToPtr(addressValue, builder_.getPtrTy(),
                                          "deref.store.addr");
  if (hasRuntimeSafetyChecks() &&
      !hasKnownStaticAddressRange(*store.address, store.targetByteLength)) {
    builder_.CreateCall(declareCheckStore(),
                        {pointer, builder_.getInt64(store.targetByteLength)});
  }
  builder_.CreateStore(value, pointer);
}

llvm::Value *LlvmEmitter::emitFormatOutput(
    const std::vector<std::unique_ptr<hir::Expr>> &arguments,
    const std::vector<hir::FormatArgKind> &formatArgumentKinds,
    stdlib::BuiltinId builtin, std::string_view calleeName) {
  const bool hasFile = builtin == stdlib::BuiltinId::Fprintf;
  const std::size_t formatIndex = hasFile ? 1U : 0U;
  if (arguments.size() <= formatIndex) {
    addDiagnostic("format output is missing its format argument");
    return nullptr;
  }
  if (!formatArgumentKinds.empty() &&
      formatArgumentKinds.size() != arguments.size()) {
    addDiagnostic("format output argument kinds do not match call arguments");
    return nullptr;
  }
  auto *ptrTy = builder_.getPtrTy();
  auto *i32Ty = builder_.getInt32Ty();
  auto *i64Ty = builder_.getInt64Ty();
  std::vector<char> specifiers;
  if (const auto *format = dynamic_cast<const hir::StringLiteral *>(
          arguments[formatIndex].get())) {
    specifiers = collectPrintfSpecifiers(decodeStringLiteral(format->value));
  }
  auto *format = emitPointerValue(*arguments[formatIndex],
                                  std::string(calleeName) + ".format");
  if (format == nullptr) {
    return nullptr;
  }
  llvm::Value *file = llvm::ConstantPointerNull::get(builder_.getPtrTy());
  if (hasFile) {
    file = emitPointerValue(*arguments[0], "fprintf.file");
    if (file == nullptr) {
      return nullptr;
    }
  }
  const auto argumentCount = arguments.size() - formatIndex - 1U;
  auto *descriptorType = llvm::StructType::get(context_, {ptrTy, i64Ty, i32Ty});
  llvm::Value *descriptors = llvm::ConstantPointerNull::get(ptrTy);
  llvm::AllocaInst *descriptorStorage = nullptr;
  if (argumentCount != 0) {
    auto *descriptorArray = llvm::ArrayType::get(descriptorType, argumentCount);
    descriptorStorage = builder_.CreateAlloca(descriptorArray, nullptr,
                                              "format.arguments");
    descriptors = builder_.CreateInBoundsGEP(
        descriptorArray, descriptorStorage,
        {builder_.getInt32(0), builder_.getInt32(0)}, "format.arguments.ptr");
  }
  for (std::size_t index = 0; index < argumentCount; ++index) {
    const char specifier = index < specifiers.size() ? specifiers[index] : '\0';
    const auto argumentIndex = formatIndex + 1U + index;
    const auto kind = argumentIndex < formatArgumentKinds.size()
                          ? formatArgumentKinds[argumentIndex]
                          : (specifier == 'f' ? hir::FormatArgKind::Float
                                              : (specifier == 's'
                                                     ? hir::FormatArgKind::String
                                                     : hir::FormatArgKind::Bytes));
    const auto &argument = *arguments[formatIndex + 1U + index];
    ViewValue view;
    if (kind == hir::FormatArgKind::Float) {
      const auto byteLength = floatVarargByteLength(argument);
      auto *value = emitFloatValue(argument, byteLength);
      auto *type = floatTypeForByteLength(byteLength);
      if (value == nullptr || type == nullptr) {
        return nullptr;
      }
      auto *storage = builder_.CreateAlloca(type, nullptr, "format.float");
      auto *store = builder_.CreateStore(value, storage);
      store->setAlignment(llvm::Align(1));
      if (hasRuntimeSafetyChecks()) {
        builder_.CreateCall(declareRegisterLocalObject(),
                            {storage, builder_.getInt64(byteLength)});
      }
      view = ViewValue{storage, builder_.getInt64(byteLength), byteLength};
    } else {
      view = emitViewValue(argument);
    }
    if (view.data == nullptr || view.length == nullptr) {
      return nullptr;
    }
    auto *entry = builder_.CreateInBoundsGEP(
        llvm::ArrayType::get(descriptorType, argumentCount), descriptorStorage,
        {builder_.getInt32(0), builder_.getInt32(static_cast<unsigned>(index))},
        "format.argument");
    builder_.CreateStore(view.data,
                         builder_.CreateStructGEP(descriptorType, entry, 0));
    builder_.CreateStore(view.length,
                         builder_.CreateStructGEP(descriptorType, entry, 1));
    builder_.CreateStore(builder_.getInt32(static_cast<unsigned>(kind)),
                         builder_.CreateStructGEP(descriptorType, entry, 2));
  }
  auto callee = declareCFunction("hs_format_output", i32Ty,
                                 {ptrTy, ptrTy, ptrTy, i64Ty, i32Ty});
  return builder_.CreateCall(
      callee, {file, format, descriptors, builder_.getInt64(argumentCount),
               builder_.getInt32(hasRuntimeSafetyChecks() ? 1 : 0)},
      std::string(calleeName) + ".ret");
}

ViewValue LlvmEmitter::emitUserTemplateFormatCall(
    std::string_view calleeName, const hir::Expr &value,
    hir::FormatOutputSink sink, const hir::Expr *file,
    std::size_t resultByteLength) {
  auto *callee = module_->getFunction(std::string(calleeName));
  if (callee == nullptr || !callee->getReturnType()->isVoidTy() ||
      callee->arg_size() != 3U || resultByteLength != 4U) {
    addDiagnostic("invalid internal user format call '" +
                  std::string(calleeName) + "'");
    return {};
  }

  const auto source = emitViewValue(value);
  if (source.data == nullptr || source.length == nullptr) {
    return {};
  }

  auto *resultType =
      llvm::ArrayType::get(builder_.getInt8Ty(), resultByteLength);
  auto *result = createFunctionEntryAlloca(resultType, "format.result");
  result->setAlignment(llvm::Align(1));
  if (hasRuntimeSafetyChecks()) {
    builder_.CreateCall(declareRegisterLocalObject(),
                        {result, builder_.getInt64(resultByteLength)});
  }

  auto *ptrTy = builder_.getPtrTy();
  llvm::Value *sinkValue = nullptr;
  switch (sink) {
  case hir::FormatOutputSink::Stdout:
    sinkValue = emitStdoutFile();
    break;
  case hir::FormatOutputSink::File:
    if (file == nullptr) {
      addDiagnostic("user format call is missing its file sink");
      return {};
    }
    sinkValue = emitPointerValue(*file, "format.file");
    break;
  }
  if (sinkValue == nullptr) {
    return {};
  }

  auto *sinkStorage = createFunctionEntryAlloca(ptrTy, "format.sink");
  sinkStorage->setAlignment(llvm::Align(alignof(void *)));
  builder_.CreateStore(sinkValue, sinkStorage);
  if (hasRuntimeSafetyChecks()) {
    builder_.CreateCall(declareRegisterLocalObject(),
                        {sinkStorage, builder_.getInt64(sizeof(void *))});
  }

  builder_.CreateCall(callee,
                      {firstBytePointer(resultType, result), source.data,
                       sinkStorage});
  return ViewValue{firstBytePointer(resultType, result),
                   builder_.getInt64(resultByteLength), resultByteLength};
}

void LlvmEmitter::emit(const hir::Call &call) {
  auto *ptrTy = builder_.getPtrTy();
  auto *i32Ty = builder_.getInt32Ty();
  auto *i64Ty = builder_.getInt64Ty();

  if (call.builtin == stdlib::BuiltinId::Free) {
    auto *address = emitIntegerValue(*call.arguments[0], sizeof(void *));
    if (!address) {
      return;
    }
    auto *pointer = builder_.CreateIntToPtr(address, builder_.getPtrTy(),
                                            "free.ptr");
    builder_.CreateCall(
        hasRuntimeSafetyChecks() ? declareCheckedFree() : declareFree(),
        {pointer});
    return;
  }

  if (call.builtin == stdlib::BuiltinId::Memset) {
    auto *address = emitPointerValue(*call.arguments[0], "memset.dst");
    auto *value = emitIntegerValue(*call.arguments[1], 4, true);
    auto *length = emitIntegerValue(*call.arguments[2], sizeof(void *), true);
    if (!address || !value || !length) {
      return;
    }
    auto callee = declareCFunction(hasRuntimeSafetyChecks() ? "hs_memset"
                                                             : "memset",
                                   ptrTy, {ptrTy, i32Ty, i64Ty});
    builder_.CreateCall(callee, {address, value, length});
    return;
  }

  if (call.builtin == stdlib::BuiltinId::Put) {
    auto source = emitViewValue(*call.arguments[0]);
    if (source.data == nullptr || source.length == nullptr) {
      return;
    }
    if (hasRuntimeSafetyChecks()) {
      auto callee = declareCFunction("hs_put", i32Ty, {ptrTy, i64Ty});
      builder_.CreateCall(callee, {source.data, source.length});
      return;
    }
    auto callee = declareCFunction("fwrite", i64Ty, {ptrTy, i64Ty, i64Ty, ptrTy});
    auto *stdoutPointer = emitStdoutFile();
    builder_.CreateCall(callee, {source.data, builder_.getInt64(1),
                                 source.length, stdoutPointer});
    return;
  }

  if (call.builtin == stdlib::BuiltinId::Srand) {
    auto *seed = emitIntegerValue(*call.arguments[0], 4, true);
    if (!seed) {
      return;
    }
    auto callee = declareCFunction("srand", builder_.getVoidTy(), {i32Ty});
    builder_.CreateCall(callee, {seed});
    return;
  }

  if (call.builtin == stdlib::BuiltinId::Exit) {
    auto *code = emitIntegerValue(*call.arguments[0], 4);
    if (!code) {
      return;
    }
    auto callee = declareCFunction("exit", builder_.getVoidTy(), {i32Ty});
    builder_.CreateCall(callee, {code});
    builder_.CreateUnreachable();
    return;
  }

  if (call.builtin == stdlib::BuiltinId::Abort) {
    auto callee = declareCFunction("abort", builder_.getVoidTy(), {});
    builder_.CreateCall(callee, {});
    builder_.CreateUnreachable();
    return;
  }

  if (call.builtin == stdlib::BuiltinId::Assert) {
    auto *condition = emitIntegerValue(*call.arguments[0], 4);
    auto *code = emitIntegerValue(*call.arguments[1], 4);
    if (!condition || !code) {
      return;
    }
    auto callee =
        declareCFunction("hs_assert", builder_.getVoidTy(), {i32Ty, i32Ty});
    builder_.CreateCall(callee, {condition, code});
    return;
  }

  if (call.builtin == stdlib::BuiltinId::Panic) {
    auto *code = emitIntegerValue(*call.arguments[0], 4);
    if (!code) {
      return;
    }
    auto callee = declareCFunction("hs_panic", builder_.getVoidTy(), {i32Ty});
    builder_.CreateCall(callee, {code});
    builder_.CreateUnreachable();
    return;
  }

  if (call.builtin == stdlib::BuiltinId::Printf ||
      call.builtin == stdlib::BuiltinId::Print) {
    (void)emitFormatOutput(call.arguments, call.formatArgumentKinds,
                           call.builtin, call.callee);
    return;
  }

  if (call.builtin == stdlib::BuiltinId::Fprintf) {
    (void)emitFormatOutput(call.arguments, call.formatArgumentKinds,
                           call.builtin, call.callee);
    return;
  }

  auto *callee = module_->getFunction(call.callee);
  if (callee == nullptr) {
    addDiagnostic("unknown function '" + call.callee + "'");
    return;
  }
  if (const auto *memoryPlan = cAbiMemoryPlan(call.callee)) {
    (void)emitCAbiMemoryCall(call.callee, call.arguments, *callee,
                              *memoryPlan);
    return;
  }
  if (cAbiDirectAggregateParameters_.contains(call.callee) ||
      cAbiDirectAggregateReturns_.contains(call.callee)) {
    (void)emitCAbiDirectCall(call.callee, call.arguments, *callee);
    return;
  }
  std::vector<llvm::Value *> arguments;
  if (callee->arg_size() != call.arguments.size()) {
    addDiagnostic("function argument count does not match declaration for '" +
                  call.callee + "'");
    return;
  }
  for (std::size_t index = 0; index < call.arguments.size(); ++index) {
    auto *parameterType = callee->getFunctionType()->getParamType(index);
    auto *value = emitValueForType(*call.arguments[index], parameterType,
                                   call.callee + ".arg");
    if (!value) {
      return;
    }
    arguments.push_back(value);
  }
  builder_.CreateCall(callee, arguments);
}

void LlvmEmitter::emit(const hir::UserTemplateOpCall &call) {
  (void)emitUserTemplateOpCall(call.callee, call.arguments,
                               call.resultByteLength);
}

void LlvmEmitter::emit(const hir::UserTemplateFormatCall &call) {
  (void)emitUserTemplateFormatCall(call.callee, *call.value, call.sink,
                                   call.file.get(), call.resultByteLength);
}

void LlvmEmitter::emit(const hir::MultiReturnCallStore &call) {
  auto *callee = module_->getFunction(call.callee);
  if (callee == nullptr) {
    addDiagnostic("unknown function '" + call.callee + "'");
    return;
  }
  std::vector<llvm::Value *> arguments;
  if (callee->arg_size() != call.arguments.size()) {
    addDiagnostic("function argument count does not match declaration for '" +
                  call.callee + "'");
    return;
  }
  for (std::size_t index = 0; index < call.arguments.size(); ++index) {
    auto *parameterType = callee->getFunctionType()->getParamType(index);
    auto *value = emitValueForType(*call.arguments[index], parameterType,
                                   call.callee + ".arg");
    if (!value) {
      return;
    }
    arguments.push_back(value);
  }
  auto *result = builder_.CreateCall(callee, arguments, call.callee + ".ret");
  for (const auto &targetInfo : call.targets) {
    const Local *target = nullptr;
    if (const auto local = locals_.find(targetInfo.bindingName);
        local != locals_.end()) {
      target = &local->second;
    } else if (const auto global = globals_.find(targetInfo.bindingName);
               global != globals_.end()) {
      target = &global->second;
    }
    if (target == nullptr) {
      addDiagnostic("unknown local '" + targetInfo.name + "'");
      return;
    }
    auto *value = builder_.CreateExtractValue(
        result, {static_cast<unsigned>(targetInfo.returnIndex)}, "ret.item");
    builder_.CreateStore(value,
                         firstBytePointer(target->storageType, target->storage));
  }
}

void LlvmEmitter::emit(const hir::InputCallStore &call) {
  auto *ptrTy = builder_.getPtrTy();
  auto *i32Ty = builder_.getInt32Ty();
  auto *i64Ty = builder_.getInt64Ty();
  llvm::Value *file = llvm::ConstantPointerNull::get(builder_.getPtrTy());

  if (call.builtin == stdlib::BuiltinId::Fscanf) {
    if (!call.file) {
      addDiagnostic("fscanf input call is missing a file handle");
      return;
    }
    file = emitPointerValue(*call.file, "fscanf.file");
    if (file == nullptr) {
      return;
    }
  } else if (call.builtin != stdlib::BuiltinId::Scanf) {
    addDiagnostic("unknown input call '" + call.callee + "'");
    return;
  }

  auto *format = emitPointerValue(*call.format, call.callee + ".format");
  if (format == nullptr) {
    return;
  }

  auto targetPointer = [&](const hir::InputCallStore::Target &target,
                           std::string_view name) -> llvm::Value * {
    const Local *storage = nullptr;
    if (const auto local = locals_.find(target.bindingName);
        local != locals_.end()) {
      storage = &local->second;
    } else if (const auto global = globals_.find(target.bindingName);
               global != globals_.end()) {
      storage = &global->second;
    }
    if (storage == nullptr) {
      addDiagnostic("unknown local '" + target.name + "'");
      return nullptr;
    }
    auto *pointer = bytePointer(storage->storageType, storage->storage,
                                target.offset, name);
    return pointer;
  };

  const auto targetCount = call.scanTargets.size();
  auto *descriptorType = llvm::StructType::get(context_, {ptrTy, i64Ty, i32Ty});
  llvm::Value *descriptors = llvm::ConstantPointerNull::get(ptrTy);
  llvm::AllocaInst *descriptorStorage = nullptr;
  if (targetCount != 0) {
    auto *descriptorArray = llvm::ArrayType::get(descriptorType, targetCount);
    descriptorStorage = builder_.CreateAlloca(descriptorArray, nullptr,
                                              "scan.targets");
    descriptors = builder_.CreateInBoundsGEP(
        descriptorArray, descriptorStorage,
        {builder_.getInt32(0), builder_.getInt32(0)}, "scan.targets.ptr");
  }
  for (std::size_t index = 0; index < targetCount; ++index) {
    const auto &target = call.scanTargets[index];
    auto *pointer = targetPointer(target, call.callee + ".target");
    if (pointer == nullptr) {
      return;
    }
    auto *entry = builder_.CreateInBoundsGEP(
        llvm::ArrayType::get(descriptorType, targetCount), descriptorStorage,
        {builder_.getInt32(0), builder_.getInt32(static_cast<unsigned>(index))},
        "scan.target");
    auto *data = builder_.CreateStructGEP(descriptorType, entry, 0);
    auto *capacity = builder_.CreateStructGEP(descriptorType, entry, 1);
    auto *kind = builder_.CreateStructGEP(descriptorType, entry, 2);
    std::int32_t kindValue = 0;
    if (target.templateName == "f16" || target.templateName == "f32" ||
        target.templateName == "f64" || target.templateName == "f128") {
      kindValue = 1;
    } else if (target.templateName == "addr" ||
               target.templateName == "handle") {
      kindValue = 2;
    } else if (target.templateName == "cstr") {
      kindValue = 3;
    }
    builder_.CreateStore(pointer, data);
    builder_.CreateStore(builder_.getInt64(target.byteLength), capacity);
    builder_.CreateStore(builder_.getInt32(kindValue), kind);
  }

  auto callee = declareCFunction("hs_scan_input", i32Ty,
                                 {ptrTy, ptrTy, ptrTy, i64Ty, i32Ty});
  auto *count = builder_.CreateCall(
      callee, {file, format, descriptors, builder_.getInt64(targetCount),
               builder_.getInt32(hasRuntimeSafetyChecks() ? 1 : 0)},
      call.callee + ".count");
  for (const auto &target : call.countTargets) {
    auto *pointer = targetPointer(target, call.callee + ".count.target");
    if (pointer == nullptr) {
      return;
    }
    builder_.CreateStore(count, pointer);
  }
}

void LlvmEmitter::emit(const hir::Return &ret) {
  auto *function = builder_.GetInsertBlock()->getParent();
  if (viewAbiResultStorage_ != nullptr) {
    if (ret.values.size() != 1U || viewAbiResultByteLength_ == 0U) {
      addDiagnostic("internal impl op must return exactly one fixed View");
      return;
    }
    auto value = emitViewValue(*ret.values.front());
    if (value.data == nullptr || !value.staticLength) {
      addDiagnostic("internal impl op return View does not match its signature");
      return;
    }
    if (*value.staticLength != viewAbiResultByteLength_) {
      // The internal ABI returns a fixed-width View, so materialize scalar
      // values at the declared result width before copying them to the caller.
      auto *storageType = llvm::ArrayType::get(
          builder_.getInt8Ty(), viewAbiResultByteLength_);
      auto *storage = createFunctionEntryAlloca(storageType, "implop.return");
      storage->setAlignment(llvm::Align(1));
      registerLocalObject(storage, viewAbiResultByteLength_);
      builder_.CreateMemSet(storage, builder_.getInt8(0),
                            viewAbiResultByteLength_, llvm::Align(1));
      const auto copyLength =
          std::min(*value.staticLength, viewAbiResultByteLength_);
      if (copyLength != 0U) {
        builder_.CreateMemCpy(storage, llvm::Align(1), value.data,
                              llvm::Align(1), copyLength);
      }
      value = ViewValue{storage, builder_.getInt64(viewAbiResultByteLength_),
                        viewAbiResultByteLength_};
    }
    builder_.CreateMemCpy(viewAbiResultStorage_, llvm::Align(1), value.data,
                          llvm::Align(1), viewAbiResultByteLength_);
    emitRuntimeFrameExit();
    builder_.CreateRetVoid();
    return;
  }
  if (cAbiSRetStorage_ != nullptr) {
    if (ret.values.size() != 1U || cAbiSRetStorageType_ == nullptr) {
      addDiagnostic("C ABI sret function must return exactly one aggregate value");
      return;
    }
    auto *value = emitValueForType(*ret.values.front(), cAbiSRetStorageType_,
                                   "return");
    if (value == nullptr) {
      return;
    }
    builder_.CreateStore(value, cAbiSRetStorage_);
    emitRuntimeFrameExit();
    builder_.CreateRetVoid();
    return;
  }
  const auto directReturn =
      cAbiDirectAggregateReturns_.find(function->getName().str());
  if (directReturn != cAbiDirectAggregateReturns_.end()) {
    if (ret.values.size() != 1U) {
      addDiagnostic("direct C aggregate function must return exactly one value");
      return;
    }
    const auto directPlan = cAbiDirectPlanFor(directReturn->second);
    auto *storageType = abiTypeFor(directReturn->second);
    if (!directPlan || storageType == nullptr ||
        function->getReturnType() != directPlan->physicalType) {
      addDiagnostic("invalid direct C aggregate return ABI");
      return;
    }
    auto *value = emitValueForType(*ret.values.front(), storageType, "return");
    if (value == nullptr) {
      return;
    }
    auto *storage = createFunctionEntryAlloca(storageType, "return.direct.tmp");
    const auto alignment =
        llvm::Align(std::max<std::size_t>(directReturn->second.alignment, 1U));
    storage->setAlignment(alignment);
    auto *store = builder_.CreateStore(value, storage);
    store->setAlignment(alignment);
    auto *physical = packCAbiDirectValue(storage, storageType,
                                          directReturn->second, *directPlan,
                                          "return.direct");
    if (physical == nullptr) {
      return;
    }
    emitRuntimeFrameExit();
    builder_.CreateRet(physical);
    return;
  }
  if (ret.values.empty()) {
    auto *returnType = function->getFunctionType()->getReturnType();
    if (returnType->isVoidTy()) {
      emitRuntimeFrameExit();
      builder_.CreateRetVoid();
    } else if (function->getName() == "main" &&
               returnType->isIntegerTy(32)) {
      emitRuntimeFrameExit();
      builder_.CreateRet(builder_.getInt32(0));
    } else {
      addDiagnostic("function return type is not void");
    }
    return;
  }
  if (ret.values.size() == 1U) {
    auto *returnType = function->getFunctionType()->getReturnType();
    auto *value = emitValueForType(*ret.values.front(), returnType, "return");
    if (!value) {
      return;
    }
    emitRuntimeFrameExit();
    builder_.CreateRet(value);
    return;
  }

  auto *returnType = function->getFunctionType()->getReturnType();
  llvm::Value *aggregate = llvm::UndefValue::get(returnType);
  for (std::size_t index = 0; index < ret.values.size(); ++index) {
    auto *structType = llvm::dyn_cast<llvm::StructType>(returnType);
    if (structType == nullptr) {
      addDiagnostic("function return type is not a struct");
      return;
    }
    auto *fieldType = structType->getElementType(static_cast<unsigned>(index));
    auto *value = emitValueForType(*ret.values[index], fieldType, "return");
    if (!value) {
      return;
    }
    aggregate = builder_.CreateInsertValue(
        aggregate, value, {static_cast<unsigned>(index)}, "ret.aggregate");
  }
  emitRuntimeFrameExit();
  builder_.CreateRet(aggregate);
}


} // namespace hitsimple::codegen
