#include "LlvmEmitter.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>

namespace hitsimple::codegen {

void LlvmEmitter::emit(const hir::If &ifStmt) {
  auto *function = builder_.GetInsertBlock()->getParent();
  auto *thenBlock = llvm::BasicBlock::Create(context_, "if.then", function);
  auto *elseBlock = llvm::BasicBlock::Create(context_, "if.else", function);
  auto *mergeBlock = llvm::BasicBlock::Create(context_, "if.end", function);

  auto *condition = emitConditionValue(*ifStmt.condition);
  if (!condition) {
    return;
  }
  builder_.CreateCondBr(condition, thenBlock, elseBlock);

  builder_.SetInsertPoint(thenBlock);
  emit(*ifStmt.thenBlock);
  if (!builder_.GetInsertBlock()->getTerminator()) {
    builder_.CreateBr(mergeBlock);
  }

  builder_.SetInsertPoint(elseBlock);
  if (ifStmt.elseBlock) {
    emit(*ifStmt.elseBlock);
  }
  if (!builder_.GetInsertBlock()->getTerminator()) {
    builder_.CreateBr(mergeBlock);
  }

  builder_.SetInsertPoint(mergeBlock);
}

void LlvmEmitter::emit(const hir::While &whileStmt) {
  auto *function = builder_.GetInsertBlock()->getParent();
  auto *conditionBlock =
      llvm::BasicBlock::Create(context_, "while.cond", function);
  auto *bodyBlock = llvm::BasicBlock::Create(context_, "while.body", function);
  auto *afterBlock = llvm::BasicBlock::Create(context_, "while.end", function);

  builder_.CreateBr(conditionBlock);

  builder_.SetInsertPoint(conditionBlock);
  auto *condition = emitConditionValue(*whileStmt.condition);
  if (!condition) {
    return;
  }
  builder_.CreateCondBr(condition, bodyBlock, afterBlock);

  loopTargets_.push_back(LoopTargets{afterBlock, conditionBlock});
  builder_.SetInsertPoint(bodyBlock);
  emit(*whileStmt.body);
  if (!builder_.GetInsertBlock()->getTerminator()) {
    builder_.CreateBr(conditionBlock);
  }
  loopTargets_.pop_back();

  builder_.SetInsertPoint(afterBlock);
}

void LlvmEmitter::emit(const hir::For &forStmt) {
  auto *function = builder_.GetInsertBlock()->getParent();
  auto *conditionBlock =
      llvm::BasicBlock::Create(context_, "for.cond", function);
  auto *bodyBlock = llvm::BasicBlock::Create(context_, "for.body", function);
  auto *postBlock = llvm::BasicBlock::Create(context_, "for.post", function);
  auto *afterBlock = llvm::BasicBlock::Create(context_, "for.end", function);

  if (forStmt.init) {
    emit(*forStmt.init);
    if (!diagnostics_.empty()) {
      return;
    }
  }
  if (!builder_.GetInsertBlock()->getTerminator()) {
    builder_.CreateBr(conditionBlock);
  }

  builder_.SetInsertPoint(conditionBlock);
  if (forStmt.condition) {
    auto *condition = emitConditionValue(*forStmt.condition);
    if (!condition) {
      return;
    }
    builder_.CreateCondBr(condition, bodyBlock, afterBlock);
  } else {
    builder_.CreateBr(bodyBlock);
  }

  loopTargets_.push_back(LoopTargets{afterBlock, postBlock});
  builder_.SetInsertPoint(bodyBlock);
  emit(*forStmt.body);
  if (!builder_.GetInsertBlock()->getTerminator()) {
    builder_.CreateBr(postBlock);
  }

  builder_.SetInsertPoint(postBlock);
  for (const auto &post : forStmt.post) {
    emit(*post);
    if (!diagnostics_.empty()) {
      loopTargets_.pop_back();
      return;
    }
    if (builder_.GetInsertBlock()->getTerminator()) {
      break;
    }
  }
  if (!builder_.GetInsertBlock()->getTerminator()) {
    builder_.CreateBr(conditionBlock);
  }
  loopTargets_.pop_back();

  builder_.SetInsertPoint(afterBlock);
}

void LlvmEmitter::emit(const hir::Goto &gotoStmt) {
  const auto found = labelBlocks_.find(gotoStmt.label);
  if (found == labelBlocks_.end()) {
    addDiagnostic("unknown label '" + gotoStmt.label + "'");
    return;
  }
  builder_.CreateBr(found->second);
}

void LlvmEmitter::emit(const hir::Label &label) {
  const auto found = labelBlocks_.find(label.label);
  if (found == labelBlocks_.end()) {
    addDiagnostic("unknown label '" + label.label + "'");
    return;
  }
  if (!builder_.GetInsertBlock()->getTerminator()) {
    builder_.CreateBr(found->second);
  }
  builder_.SetInsertPoint(found->second);
  emit(*label.statement);
}

void LlvmEmitter::emit(const hir::Throw &throwStmt) {
  if (catchTargets_.empty()) {
    auto exit = declareCFunction("exit", builder_.getVoidTy(),
                                 {builder_.getInt32Ty()});
    builder_.CreateCall(exit, {builder_.getInt32(1)});
    builder_.CreateUnreachable();
    return;
  }

  const auto &target = catchTargets_.back();
  if (throwStmt.targetByteLength != target.errorByteLength ||
      throwStmt.sourceByteLength != throwStmt.targetByteLength) {
    addDiagnostic("invalid throw/catch View byte length contract");
    return;
  }
  if (!throwStmt.delivery) {
    addDiagnostic("missing throw/catch View delivery");
    return;
  }
  emit(*throwStmt.delivery);
  if (builder_.GetInsertBlock()->getTerminator()) {
    return;
  }
  builder_.CreateBr(target.catchBlock);
}

void LlvmEmitter::emit(const hir::TryCatch &tryCatch) {
  auto *function = builder_.GetInsertBlock()->getParent();
  auto *tryBlock = llvm::BasicBlock::Create(context_, "try.body", function);
  auto *catchBlock = llvm::BasicBlock::Create(context_, "try.catch", function);
  auto *afterBlock = llvm::BasicBlock::Create(context_, "try.end", function);

  auto *errorStorageType =
      llvm::ArrayType::get(builder_.getInt8Ty(), tryCatch.errorByteLength);
  auto *errorStorage = createFunctionEntryAlloca(errorStorageType,
                                                  tryCatch.errorBindingName);
  locals_.emplace(tryCatch.errorBindingName,
                  Local{errorStorage, errorStorageType,
                        tryCatch.errorByteLength, std::nullopt});
  registerLocalObject(errorStorage, tryCatch.errorByteLength);

  builder_.CreateBr(tryBlock);

  catchTargets_.push_back(CatchTarget{catchBlock, errorStorageType,
                                      errorStorage,
                                      tryCatch.errorByteLength});
  builder_.SetInsertPoint(tryBlock);
  emit(*tryCatch.tryBlock);
  catchTargets_.pop_back();
  if (!builder_.GetInsertBlock()->getTerminator()) {
    builder_.CreateBr(afterBlock);
  }

  builder_.SetInsertPoint(catchBlock);
  emit(*tryCatch.catchBlock);
  if (!builder_.GetInsertBlock()->getTerminator()) {
    builder_.CreateBr(afterBlock);
  }

  builder_.SetInsertPoint(afterBlock);
}

void LlvmEmitter::emitBreak() {
  if (loopTargets_.empty()) {
    addDiagnostic("break used outside of a loop");
    return;
  }
  builder_.CreateBr(loopTargets_.back().breakBlock);
}

void LlvmEmitter::emitContinue() {
  if (loopTargets_.empty()) {
    addDiagnostic("continue used outside of a loop");
    return;
  }
  builder_.CreateBr(loopTargets_.back().continueBlock);
}

void LlvmEmitter::collectLabels(const hir::Block &block,
                                llvm::Function &function) {
  for (const auto &statement : block.statements) {
    collectLabels(*statement, function);
  }
}

void LlvmEmitter::collectLabels(const hir::Stmt &statement,
                                llvm::Function &function) {
  if (const auto *list = dynamic_cast<const hir::StatementList *>(&statement)) {
    for (const auto &item : list->statements) {
      collectLabels(*item, function);
    }
    return;
  }
  if (const auto *ifStmt = dynamic_cast<const hir::If *>(&statement)) {
    collectLabels(*ifStmt->thenBlock, function);
    if (ifStmt->elseBlock) {
      collectLabels(*ifStmt->elseBlock, function);
    }
    return;
  }
  if (const auto *whileStmt = dynamic_cast<const hir::While *>(&statement)) {
    collectLabels(*whileStmt->body, function);
    return;
  }
  if (const auto *forStmt = dynamic_cast<const hir::For *>(&statement)) {
    if (forStmt->init) {
      collectLabels(*forStmt->init, function);
    }
    for (const auto &post : forStmt->post) {
      collectLabels(*post, function);
    }
    collectLabels(*forStmt->body, function);
    return;
  }
  if (const auto *label = dynamic_cast<const hir::Label *>(&statement)) {
    if (labelBlocks_.find(label->label) == labelBlocks_.end()) {
      auto *block =
          llvm::BasicBlock::Create(context_, "label." + label->label, &function);
      labelBlocks_.emplace(label->label, block);
    }
    collectLabels(*label->statement, function);
    return;
  }
  if (const auto *tryCatch = dynamic_cast<const hir::TryCatch *>(&statement)) {
    collectLabels(*tryCatch->tryBlock, function);
    collectLabels(*tryCatch->catchBlock, function);
  }
}


} // namespace hitsimple::codegen
