#include "LlvmEmitter.h"

#include "hitsimple/codegen/LlvmCompatibility.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/Support/Alignment.h>

#include <algorithm>
#include <cstddef>

namespace hitsimple::codegen {

namespace {

bool expressionUsesF128(const hir::Expr& expression);
bool statementUsesF128(const hir::Stmt& statement);

bool expressionsUseF128(const std::vector<std::unique_ptr<hir::Expr>>& expressions) {
  return std::any_of(expressions.begin(), expressions.end(),
                     [](const auto& expression) {
                       return expression && expressionUsesF128(*expression);
                     });
}

bool statementsUseF128(const std::vector<std::unique_ptr<hir::Stmt>>& statements) {
  return std::any_of(statements.begin(), statements.end(),
                     [](const auto& statement) {
                       return statement && statementUsesF128(*statement);
                     });
}

bool blockUsesF128(const hir::Block& block) {
  return statementsUseF128(block.statements);
}

bool expressionUsesF128(const hir::Expr& expression) {
  if (const auto* variable = dynamic_cast<const hir::VariableRef*>(&expression)) {
    return variable->templateName == "f128";
  }
  if (const auto* literal = dynamic_cast<const hir::FloatLiteral*>(&expression)) {
    return literal->byteLength == 16U;
  }
  if (const auto* dereference = dynamic_cast<const hir::DerefExpr*>(&expression)) {
    return dereference->byteLength == 16U ||
           (dereference->address && expressionUsesF128(*dereference->address));
  }
  if (const auto* binary = dynamic_cast<const hir::BinaryExpr*>(&expression)) {
    return (binary->left && expressionUsesF128(*binary->left)) ||
           (binary->right && expressionUsesF128(*binary->right));
  }
  if (const auto* unary = dynamic_cast<const hir::UnaryExpr*>(&expression)) {
    return unary->operand && expressionUsesF128(*unary->operand);
  }
  if (const auto* ternary = dynamic_cast<const hir::TernaryExpr*>(&expression)) {
    return (ternary->condition && expressionUsesF128(*ternary->condition)) ||
           (ternary->thenExpr && expressionUsesF128(*ternary->thenExpr)) ||
           (ternary->elseExpr && expressionUsesF128(*ternary->elseExpr));
  }
  if (const auto* unsignedExpression =
          dynamic_cast<const hir::UnsignedExpr*>(&expression)) {
    return unsignedExpression->operand &&
           expressionUsesF128(*unsignedExpression->operand);
  }
  if (const auto* cast = dynamic_cast<const hir::IntegerCastExpr*>(&expression)) {
    return cast->operand && expressionUsesF128(*cast->operand);
  }
  if (const auto* view = dynamic_cast<const hir::TemplateViewExpr*>(&expression)) {
    return view->templateName == "f128" ||
           (view->operand && expressionUsesF128(*view->operand));
  }
  if (const auto* call =
          dynamic_cast<const hir::UserTemplateOpCallExpr*>(&expression)) {
    return call->templateName == "f128" || expressionsUseF128(call->arguments);
  }
  if (const auto* binary = dynamic_cast<const hir::FloatBinaryExpr*>(&expression)) {
    return binary->byteLength == 16U ||
           (binary->left && expressionUsesF128(*binary->left)) ||
           (binary->right && expressionUsesF128(*binary->right));
  }
  if (const auto* comparison =
          dynamic_cast<const hir::FloatCompareExpr*>(&expression)) {
    return comparison->operandByteLength == 16U ||
           (comparison->left && expressionUsesF128(*comparison->left)) ||
           (comparison->right && expressionUsesF128(*comparison->right));
  }
  if (const auto* conversion = dynamic_cast<const hir::ToFloatExpr*>(&expression)) {
    return conversion->byteLength == 16U ||
           (conversion->operand && expressionUsesF128(*conversion->operand));
  }
  if (const auto* conversion = dynamic_cast<const hir::ToIntExpr*>(&expression)) {
    return conversion->floatByteLength == 16U ||
           (conversion->operand && expressionUsesF128(*conversion->operand));
  }
  if (const auto* call =
          dynamic_cast<const hir::UserTemplateFormatCallExpr*>(&expression)) {
    return (call->value && expressionUsesF128(*call->value)) ||
           (call->file && expressionUsesF128(*call->file));
  }
  if (const auto* call = dynamic_cast<const hir::CallExpr*>(&expression)) {
    return call->templateName == "f128" ||
           (call->isFloating && call->byteLength == 16U) ||
           expressionsUseF128(call->arguments);
  }
  if (const auto* dynamic =
          dynamic_cast<const hir::DynamicByteViewExpr*>(&expression)) {
    return (dynamic->source && expressionUsesF128(*dynamic->source)) ||
           (dynamic->runtimeLength && expressionUsesF128(*dynamic->runtimeLength));
  }
  if (const auto* byteSwap = dynamic_cast<const hir::ByteSwapExpr*>(&expression)) {
    return byteSwap->source && expressionUsesF128(*byteSwap->source);
  }
  if (const auto* assignment =
          dynamic_cast<const hir::AssignmentExpr*>(&expression)) {
    return statementsUseF128(assignment->stores) ||
           (assignment->result && expressionUsesF128(*assignment->result));
  }
  return false;
}

bool statementUsesF128(const hir::Stmt& statement) {
  if (const auto* local = dynamic_cast<const hir::LocalMemory*>(&statement)) {
    return local->templateName == "f128";
  }
  if (const auto* store = dynamic_cast<const hir::IntegerStore*>(&statement)) {
    return store->value && expressionUsesF128(*store->value);
  }
  if (const auto* store = dynamic_cast<const hir::FloatStore*>(&statement)) {
    return store->targetByteLength == 16U ||
           (store->value && expressionUsesF128(*store->value));
  }
  if (const auto* store = dynamic_cast<const hir::BoolStore*>(&statement)) {
    return store->value && expressionUsesF128(*store->value);
  }
  if (const auto* store = dynamic_cast<const hir::PointerStore*>(&statement)) {
    return (store->address && expressionUsesF128(*store->address)) ||
           (store->value && expressionUsesF128(*store->value));
  }
  if (const auto* call = dynamic_cast<const hir::Call*>(&statement)) {
    return expressionsUseF128(call->arguments);
  }
  if (const auto* call = dynamic_cast<const hir::UserTemplateOpCall*>(&statement)) {
    return expressionsUseF128(call->arguments);
  }
  if (const auto* call =
          dynamic_cast<const hir::UserTemplateFormatCall*>(&statement)) {
    return (call->value && expressionUsesF128(*call->value)) ||
           (call->file && expressionUsesF128(*call->file));
  }
  if (const auto* call =
          dynamic_cast<const hir::MultiReturnCallStore*>(&statement)) {
    return expressionsUseF128(call->arguments);
  }
  if (const auto* call = dynamic_cast<const hir::InputCallStore*>(&statement)) {
    return std::any_of(call->scanTargets.begin(), call->scanTargets.end(),
                       [](const auto& target) {
                         return target.templateName == "f128";
                       }) ||
           (call->file && expressionUsesF128(*call->file)) ||
           (call->format && expressionUsesF128(*call->format));
  }
  if (const auto* returned = dynamic_cast<const hir::Return*>(&statement)) {
    return expressionsUseF128(returned->values);
  }
  if (const auto* conditional = dynamic_cast<const hir::If*>(&statement)) {
    return (conditional->condition && expressionUsesF128(*conditional->condition)) ||
           (conditional->thenBlock && blockUsesF128(*conditional->thenBlock)) ||
           (conditional->elseBlock && blockUsesF128(*conditional->elseBlock));
  }
  if (const auto* loop = dynamic_cast<const hir::While*>(&statement)) {
    return (loop->condition && expressionUsesF128(*loop->condition)) ||
           (loop->body && blockUsesF128(*loop->body));
  }
  if (const auto* loop = dynamic_cast<const hir::For*>(&statement)) {
    return (loop->init && statementUsesF128(*loop->init)) ||
           (loop->condition && expressionUsesF128(*loop->condition)) ||
           statementsUseF128(loop->post) ||
           (loop->body && blockUsesF128(*loop->body));
  }
  if (const auto* list = dynamic_cast<const hir::StatementList*>(&statement)) {
    return statementsUseF128(list->statements);
  }
  if (const auto* label = dynamic_cast<const hir::Label*>(&statement)) {
    return label->statement && statementUsesF128(*label->statement);
  }
  if (const auto* thrown = dynamic_cast<const hir::Throw*>(&statement)) {
    return thrown->delivery && statementUsesF128(*thrown->delivery);
  }
  if (const auto* tryCatch = dynamic_cast<const hir::TryCatch*>(&statement)) {
    return (tryCatch->tryBlock && blockUsesF128(*tryCatch->tryBlock)) ||
           (tryCatch->catchBlock && blockUsesF128(*tryCatch->catchBlock));
  }
  return false;
}

bool functionUsesF128(const hir::Function& function) {
  return function.body && blockUsesF128(*function.body);
}

} // namespace

void LlvmEmitter::emit(const hir::GlobalMemory &global) {
  SourceRangeScope sourceRange(*this, global.range);
  if (globals_.find(global.bindingName) != globals_.end()) {
    addDiagnostic("duplicate global '" + global.name + "'");
    return;
  }

  auto *storageType = global.abiType
                          ? abiTypeFor(*global.abiType)
                          : llvm::ArrayType::get(builder_.getInt8Ty(),
                                                global.byteLength);
  if (storageType == nullptr) {
    return;
  }
  if (global.isExtern) {
    auto *storage =
        new llvm::GlobalVariable(*module_, storageType, false,
                                 llvm::GlobalValue::ExternalLinkage, nullptr,
                                 global.bindingName);
    if (global.abiType) {
      storage->setAlignment(llvm::Align(global.abiType->alignment));
    }
    globals_.emplace(global.bindingName,
                     Local{storage, storageType, global.byteLength,
                           global.abiType,
                           global.abiType ? global.abiType->alignment
                                          : std::size_t{1}});
    return;
  }

  auto *initializer = llvm::Constant::getNullValue(storageType);
  const auto linkage = global.linkage == hir::Linkage::Internal
                           ? llvm::GlobalValue::InternalLinkage
                           : llvm::GlobalValue::ExternalLinkage;
  auto *storage = new llvm::GlobalVariable(*module_, storageType, false,
                                           linkage, initializer,
                                           global.bindingName);
  const auto requestedAlignment = global.abiType
                                      ? global.abiType->alignment
                                      : std::size_t{1};
  storage->setAlignment(llvm::Align(std::max(alignof(std::max_align_t),
                                              requestedAlignment)));
  globals_.emplace(global.bindingName,
                   Local{storage, storageType, global.byteLength,
                         global.abiType,
                         std::max(alignof(std::max_align_t),
                                  requestedAlignment)});
  internalGlobals_.push_back(
      RuntimeObject{storage, storageType, global.byteLength});
}

llvm::Type *
LlvmEmitter::functionReturnType(
    const std::vector<std::size_t> &byteLengths,
    const std::optional<hir::FunctionAbiSignature> &abiSignature) {
  if (abiSignature && abiSignature->returnTypes.size() != byteLengths.size()) {
    addDiagnostic("function ABI return count does not match HIR signature");
    return nullptr;
  }
  if (byteLengths.empty()) {
    return builder_.getVoidTy();
  }
  if (byteLengths.size() == 1U) {
    if (abiSignature) {
      return abiTypeFor(abiSignature->returnTypes.front());
    }
    return integerTypeForByteLength(byteLengths.front());
  }
  std::vector<llvm::Type *> fields;
  for (std::size_t index = 0; index < byteLengths.size(); ++index) {
    const auto byteLength = byteLengths[index];
    auto *fieldType = abiSignature
                          ? abiTypeFor(abiSignature->returnTypes[index])
                          : integerTypeForByteLength(byteLength);
    if (!fieldType) {
      return nullptr;
    }
    fields.push_back(fieldType);
  }
  return llvm::StructType::get(context_, fields);
}

void LlvmEmitter::declare(const hir::ExternFunction &function) {
  if (function.abiSignature &&
      function.abiSignature->parameterTypes.size() !=
          function.parameterByteLengths.size()) {
    addDiagnostic("function ABI parameter count does not match extern function '" +
                  function.name + "'");
    return;
  }
  std::optional<CAbiMemoryPlan> memoryPlan;
  if (function.abiSignature && function.abiSignature->isCCompatibility) {
    memoryPlan = cAbiMemoryPlanFor(*function.abiSignature);
  }
  auto *functionType = cAbiFunctionType(function.abiSignature,
                                        function.parameterByteLengths,
                                        function.returnByteLengths,
                                        memoryPlan ? &*memoryPlan : nullptr);
  if (functionType == nullptr) {
    return;
  }
  rememberCAbiDirectAggregatePlans(function.name, function.abiSignature);
  module_->getOrInsertFunction(function.name, functionType);
  auto *declared = module_->getFunction(function.name);
  if (declared != nullptr && memoryPlan) {
    cAbiMemoryPlans_[function.name] = *memoryPlan;
    applyCAbiMemoryAttributes(*declared, *memoryPlan);
  }
}

llvm::Function *LlvmEmitter::declare(const hir::Function &function) {
  SourceRangeScope sourceRange(*this, function.range);
  if (function.usesViewAbi) {
    if (function.abiSignature) {
      addDiagnostic("invalid internal View ABI function '" + function.name + "'");
      return nullptr;
    }
    std::vector<llvm::Type *> parameters(
        function.parameters.size() +
            (function.viewResultByteLength == 0U ? 0U : 1U),
        builder_.getPtrTy());
    auto *functionType =
        llvm::FunctionType::get(builder_.getVoidTy(), parameters, false);
    if (auto *existing = module_->getFunction(function.name)) {
      return existing;
    }
    return llvm::Function::Create(functionType, llvm::Function::InternalLinkage,
                                  function.name, module_);
  }
  if (function.abiSignature &&
      function.abiSignature->parameterTypes.size() != function.parameters.size()) {
    addDiagnostic("function ABI parameter count does not match function '" +
                  function.name + "'");
    return nullptr;
  }
  std::optional<CAbiMemoryPlan> memoryPlan;
  if (function.abiSignature && function.abiSignature->isCCompatibility) {
    memoryPlan = cAbiMemoryPlanFor(*function.abiSignature);
  }
  std::vector<std::size_t> parameterByteLengths;
  parameterByteLengths.reserve(function.parameters.size());
  for (const auto& parameter : function.parameters) {
    parameterByteLengths.push_back(parameter.byteLength);
  }
  auto *functionType = cAbiFunctionType(function.abiSignature,
                                        parameterByteLengths,
                                        function.returnByteLengths,
                                        memoryPlan ? &*memoryPlan : nullptr);
  if (functionType == nullptr) {
    return nullptr;
  }
  rememberCAbiDirectAggregatePlans(function.name, function.abiSignature);
  if (auto *existing = module_->getFunction(function.name)) {
    return existing;
  }
  const auto linkage = function.linkage == hir::Linkage::Internal
                           ? llvm::Function::InternalLinkage
                           : llvm::Function::ExternalLinkage;
  auto *declared = llvm::Function::Create(functionType, linkage, function.name,
                                          module_);
  if (memoryPlan) {
    cAbiMemoryPlans_[function.name] = *memoryPlan;
    applyCAbiMemoryAttributes(*declared, *memoryPlan);
  }
  return declared;
}

void LlvmEmitter::emitGlobalInit(const hir::Block *block) {
  SourceRangeScope sourceRange(*this, block ? block->range : std::nullopt);
  auto *functionType = llvm::FunctionType::get(builder_.getVoidTy(), false);
  auto *function = llvm::Function::Create(
      functionType, llvm::GlobalValue::InternalLinkage,
      "__hitsimple.global.init", module_);
  auto *entry = llvm::BasicBlock::Create(context_, "entry", function);
  builder_.SetInsertPoint(entry);

  locals_.clear();
  loopTargets_.clear();
  catchTargets_.clear();
  labelBlocks_.clear();
  labelScopeDepths_.clear();
  staticInitializationGuards_.clear();
  const auto previousEntryBlock = functionEntryBlock_;
  const bool previousFrameActive = runtimeFrameActive_;
  const auto previousScopeDepth = runtimeScopeDepth_;
  functionEntryBlock_ = nullptr;
  runtimeFrameActive_ = false;
  runtimeScopeDepth_ = 0;

  if (hasRuntimeSafetyChecks()) {
    functionEntryBlock_ = entry;
    runtimeFrameActive_ = true;
    emitRuntimeFrameEnter();
    auto *body = llvm::BasicBlock::Create(context_, "body", function);
    builder_.CreateBr(body);
    builder_.SetInsertPoint(body);
  }

  for (const auto &object : internalGlobals_) {
    registerStaticObject(object.storage, object.byteLength);
  }

  if (block != nullptr) {
    emit(*block);
  }
  if (!builder_.GetInsertBlock()->getTerminator()) {
    emitRuntimeFrameExit();
    builder_.CreateRetVoid();
  }
  functionEntryBlock_ = previousEntryBlock;
  runtimeFrameActive_ = previousFrameActive;
  runtimeScopeDepth_ = previousScopeDepth;
  if (!diagnostics_.empty()) {
    return;
  }

  auto *priority = builder_.getInt32(65535);
  auto *pointerType = builder_.getPtrTy();
  auto *ctorType = llvm::StructType::get(builder_.getInt32Ty(), pointerType,
                                          pointerType);
  auto *ctor = llvm::ConstantStruct::get(
      ctorType, {priority, function, llvm::ConstantPointerNull::get(pointerType)});
  auto *ctorsType = llvm::ArrayType::get(ctorType, 1);
  auto *ctors = llvm::ConstantArray::get(ctorsType, {ctor});
  (void)new llvm::GlobalVariable(*module_, ctorsType, false,
                                 llvm::GlobalValue::AppendingLinkage, ctors,
                                 "llvm.global_ctors");
}

void LlvmEmitter::emit(const hir::Function &function) {
  SourceRangeScope sourceRange(*this, function.range);
  // llvm-mingw's software-binary128 format/scan path crashes when a checked
  // source-location write is emitted in the same HitSimple function. Keep the
  // isolation strictly to Windows functions that actually use f128; Darwin
  // and all other checked functions retain policy B source locations.
  RuntimeSourceLocationSuppressionScope sourceLocationSuppression(
      *this, parseTargetTriple(moduleTargetTriple(*module_)).isOSWindows() &&
                 functionUsesF128(function));
  auto *llvmFunction = declare(function);
  if (llvmFunction == nullptr) {
    return;
  }

  locals_.clear();
  loopTargets_.clear();
  catchTargets_.clear();
  labelBlocks_.clear();
  labelScopeDepths_.clear();
  staticInitializationGuards_.clear();

  auto *entry = llvm::BasicBlock::Create(context_, "entry", llvmFunction);
  builder_.SetInsertPoint(entry);
  beginDebugFunction(function, *llvmFunction);
  const auto previousEntryBlock = functionEntryBlock_;
  const bool previousFrameActive = runtimeFrameActive_;
  const auto previousScopeDepth = runtimeScopeDepth_;
  const auto *memoryPlan = cAbiMemoryPlan(function.name);
  const auto directParameters =
      cAbiDirectAggregateParameters_.find(function.name);
  if (directParameters != cAbiDirectAggregateParameters_.end() &&
      directParameters->second.size() != function.parameters.size()) {
    addDiagnostic("C ABI direct aggregate parameter plan does not match function '" +
                  function.name + "'");
    return;
  }
  const auto previousSRetStorage = cAbiSRetStorage_;
  const auto previousSRetStorageType = cAbiSRetStorageType_;
  const auto previousViewAbiResultStorage = viewAbiResultStorage_;
  const auto previousViewAbiResultByteLength = viewAbiResultByteLength_;
  functionEntryBlock_ = nullptr;
  runtimeFrameActive_ = false;
  runtimeScopeDepth_ = 0;
  cAbiSRetStorage_ = nullptr;
  cAbiSRetStorageType_ = nullptr;
  viewAbiResultStorage_ = nullptr;
  viewAbiResultByteLength_ = 0;
  if (hasRuntimeSafetyChecks()) {
    functionEntryBlock_ = entry;
    runtimeFrameActive_ = true;
    emitRuntimeFrameEnter();
    auto *body = llvm::BasicBlock::Create(context_, "body", llvmFunction);
    builder_.CreateBr(body);
    builder_.SetInsertPoint(body);
  }
  collectLabels(*function.body, *llvmFunction, 1U);

  if (function.usesViewAbi) {
    auto argument = llvmFunction->arg_begin();
    if (function.viewResultByteLength != 0U) {
      if (argument == llvmFunction->arg_end()) {
        addDiagnostic("internal View ABI function is missing its result address");
        return;
      }
      viewAbiResultStorage_ = &*argument++;
      viewAbiResultByteLength_ = function.viewResultByteLength;
    }
    for (const auto &parameter : function.parameters) {
      if (argument == llvmFunction->arg_end()) {
        addDiagnostic("internal View ABI parameter count does not match function '" +
                      function.name + "'");
        return;
      }
      auto *storageType = llvm::ArrayType::get(builder_.getInt8Ty(),
                                               parameter.byteLength);
      auto *incomingArgument = &*argument++;
      llvm::Value *storage = incomingArgument;
      if (parameter.viewIsCopy) {
        storage = createFunctionEntryAlloca(storageType, parameter.bindingName);
        llvm::cast<llvm::AllocaInst>(storage)->setAlignment(llvm::Align(1));
        builder_.CreateMemCpy(storage, llvm::Align(1), incomingArgument,
                              llvm::Align(1), parameter.byteLength);
        registerLocalObject(storage, parameter.byteLength);
      }
      locals_.emplace(parameter.bindingName,
                      Local{storage, storageType, parameter.byteLength,
                            std::nullopt, std::size_t{1}});
      declareDebugVariable(parameter.name, parameter.range, parameter.byteLength,
                           storage, static_cast<unsigned>(
                               &parameter - function.parameters.data() + 1U));
    }
    if (argument != llvmFunction->arg_end()) {
      addDiagnostic("internal View ABI parameter count does not match function '" +
                    function.name + "'");
      return;
    }
    emit(*function.body);
    if (diagnostics_.empty() && !builder_.GetInsertBlock()->getTerminator()) {
      if (function.viewResultByteLength == 0U) {
        emitRuntimeFrameExit();
        builder_.CreateRetVoid();
      } else {
        addDiagnostic("missing return in internal View ABI function '" +
                      function.name + "'");
      }
    }
    functionEntryBlock_ = previousEntryBlock;
    runtimeFrameActive_ = previousFrameActive;
    runtimeScopeDepth_ = previousScopeDepth;
    cAbiSRetStorage_ = previousSRetStorage;
    cAbiSRetStorageType_ = previousSRetStorageType;
    viewAbiResultStorage_ = previousViewAbiResultStorage;
    viewAbiResultByteLength_ = previousViewAbiResultByteLength;
    return;
  }

  auto argument = llvmFunction->arg_begin();
  if (memoryPlan && memoryPlan->indirectReturn) {
    if (argument == llvmFunction->arg_end()) {
      addDiagnostic("C ABI function is missing its sret parameter");
      functionEntryBlock_ = previousEntryBlock;
      runtimeFrameActive_ = previousFrameActive;
      cAbiSRetStorage_ = previousSRetStorage;
      cAbiSRetStorageType_ = previousSRetStorageType;
      return;
    }
    cAbiSRetStorage_ = &*argument++;
    cAbiSRetStorageType_ = abiTypeFor(*memoryPlan->indirectReturn);
    if (cAbiSRetStorageType_ == nullptr) {
      functionEntryBlock_ = previousEntryBlock;
      runtimeFrameActive_ = previousFrameActive;
      cAbiSRetStorage_ = previousSRetStorage;
      cAbiSRetStorageType_ = previousSRetStorageType;
      return;
    }
  }

  for (std::size_t index = 0; index < function.parameters.size(); ++index) {
    if (argument == llvmFunction->arg_end()) {
      addDiagnostic("C ABI function parameter count does not match function '" +
                    function.name + "'");
      functionEntryBlock_ = previousEntryBlock;
      runtimeFrameActive_ = previousFrameActive;
      cAbiSRetStorage_ = previousSRetStorage;
      cAbiSRetStorageType_ = previousSRetStorageType;
      return;
    }
    const auto &parameter = function.parameters[index];
    const auto abiType = function.abiSignature
                             ? std::optional<hir::AbiType>{
                                   function.abiSignature->parameterTypes[index]}
                             : std::nullopt;
    auto *storageType = abiType ? abiTypeFor(*abiType)
                                : llvm::ArrayType::get(builder_.getInt8Ty(),
                                                      parameter.byteLength);
    if (storageType == nullptr) {
      functionEntryBlock_ = previousEntryBlock;
      runtimeFrameActive_ = previousFrameActive;
      cAbiSRetStorage_ = previousSRetStorage;
      cAbiSRetStorageType_ = previousSRetStorageType;
      return;
    }
    if (memoryPlan && memoryPlan->indirectParameters[index]) {
      locals_.emplace(parameter.bindingName,
                      Local{&*argument++, storageType, parameter.byteLength,
                            abiType, abiType ? abiType->alignment
                                             : std::size_t{1}});
      declareDebugVariable(parameter.name, parameter.range, parameter.byteLength,
                           locals_.at(parameter.bindingName).storage,
                           static_cast<unsigned>(index + 1U));
      continue;
    }
    auto *storage =
        createFunctionEntryAlloca(storageType, parameter.bindingName);
    const auto requestedAlignment = abiType ? abiType->alignment : std::size_t{1};
    storage->setAlignment(llvm::Align(std::max(alignof(std::max_align_t),
                                               requestedAlignment)));
    locals_.emplace(parameter.bindingName,
                    Local{storage, storageType, parameter.byteLength, abiType,
                          std::max(alignof(std::max_align_t),
                                   requestedAlignment)});
    registerLocalObject(storage, parameter.byteLength);
    declareDebugVariable(parameter.name, parameter.range, parameter.byteLength,
                         storage, static_cast<unsigned>(index + 1U));
    const auto directType =
        directParameters == cAbiDirectAggregateParameters_.end()
            ? std::optional<hir::AbiType>{}
            : directParameters->second[index];
    if (directType) {
      const auto directPlan = cAbiDirectPlanFor(*directType);
      if (!directPlan) {
        functionEntryBlock_ = previousEntryBlock;
        runtimeFrameActive_ = previousFrameActive;
        cAbiSRetStorage_ = previousSRetStorage;
        cAbiSRetStorageType_ = previousSRetStorageType;
        return;
      }
      llvm::Value *physical = directPlan->pieces.size() == 1U
                                  ? nullptr
                                  : llvm::UndefValue::get(directPlan->physicalType);
      for (std::size_t pieceIndex = 0;
           pieceIndex < directPlan->pieces.size(); ++pieceIndex) {
        const auto& piece = directPlan->pieces[pieceIndex];
        if (argument == llvmFunction->arg_end() ||
            argument->getType() != piece.type) {
          addDiagnostic("C ABI direct aggregate parameter type does not match function '" +
                        function.name + "'");
          functionEntryBlock_ = previousEntryBlock;
          runtimeFrameActive_ = previousFrameActive;
          cAbiSRetStorage_ = previousSRetStorage;
          cAbiSRetStorageType_ = previousSRetStorageType;
          return;
        }
        if (directPlan->pieces.size() == 1U) {
          physical = &*argument;
        } else {
          physical = builder_.CreateInsertValue(
              physical, &*argument, {static_cast<unsigned>(pieceIndex)},
              parameter.bindingName + ".physical");
        }
        ++argument;
      }
      if (!unpackCAbiDirectValue(physical, storage, storageType, *directType,
                                 *directPlan,
                                 parameter.bindingName + ".direct")) {
        functionEntryBlock_ = previousEntryBlock;
        runtimeFrameActive_ = previousFrameActive;
        cAbiSRetStorage_ = previousSRetStorage;
        cAbiSRetStorageType_ = previousSRetStorageType;
        return;
      }
      continue;
    }
    auto *store = builder_.CreateStore(&*argument++, storage);
    store->setAlignment(llvm::Align(std::max(alignof(std::max_align_t),
                                             requestedAlignment)));
  }

  if (argument != llvmFunction->arg_end()) {
    addDiagnostic("C ABI function parameter count does not match function '" +
                  function.name + "'");
    functionEntryBlock_ = previousEntryBlock;
    runtimeFrameActive_ = previousFrameActive;
    cAbiSRetStorage_ = previousSRetStorage;
    cAbiSRetStorageType_ = previousSRetStorageType;
    return;
  }

  emit(*function.body);

  if (diagnostics_.empty() && !builder_.GetInsertBlock()->getTerminator()) {
    emitRuntimeFrameExit();
    if (function.name == "main" &&
        llvmFunction->getReturnType()->isIntegerTy(32)) {
      builder_.CreateRet(builder_.getInt32(0));
    } else if (function.returnByteLengths.empty()) {
      builder_.CreateRetVoid();
    } else {
      addDiagnostic("missing return in function '" + function.name + "'");
    }
  }
  functionEntryBlock_ = previousEntryBlock;
  runtimeFrameActive_ = previousFrameActive;
  runtimeScopeDepth_ = previousScopeDepth;
  cAbiSRetStorage_ = previousSRetStorage;
  cAbiSRetStorageType_ = previousSRetStorageType;
  viewAbiResultStorage_ = previousViewAbiResultStorage;
  viewAbiResultByteLength_ = previousViewAbiResultByteLength;
}


} // namespace hitsimple::codegen
