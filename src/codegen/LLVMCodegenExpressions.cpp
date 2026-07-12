#include "LlvmEmitter.h"

#include "hitsimple/literal/Literal.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Alignment.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>

namespace hitsimple::codegen {
namespace {

std::optional<std::size_t> typedOperatorMarker(std::string_view op) {
  if (!op.starts_with('%')) {
    return std::nullopt;
  }
  const auto marker = op.find_last_of("duf");
  if (marker == std::string_view::npos || marker + 1U >= op.size()) {
    return std::nullopt;
  }
  return marker;
}

std::optional<bool> integerOperationIsUnsigned(std::string_view op) {
  const auto marker = typedOperatorMarker(op);
  if (!marker) {
    return std::nullopt;
  }
  if (op[*marker] == 'u') {
    return true;
  }
  if (op[*marker] == 'd') {
    return false;
  }
  return std::nullopt;
}

bool isUnsignedExpression(const hir::Expr &expression) {
  if (dynamic_cast<const hir::UnsignedExpr *>(&expression) != nullptr) {
    return true;
  }
  if (const auto *cast = dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
    return !cast->isSigned;
  }
  if (const auto *binary = dynamic_cast<const hir::BinaryExpr *>(&expression)) {
    const auto isUnsigned = integerOperationIsUnsigned(binary->op);
    return isUnsigned && *isUnsigned;
  }
  return false;
}

bool isComparison(std::string_view op) {
  return op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" ||
         op == "!=";
}

bool isLogical(std::string_view op) { return op == "&&" || op == "||"; }

bool isCharacterClassifier(stdlib::BuiltinId builtin) {
  using enum stdlib::BuiltinId;
  return builtin == IsDigit || builtin == IsAlpha || builtin == IsAlnum ||
         builtin == IsSpace || builtin == ToUpper || builtin == ToLower;
}

std::optional<llvm::Intrinsic::ID>
unaryFloatMathIntrinsic(stdlib::BuiltinId builtin) {
  if (builtin == stdlib::BuiltinId::FAbs) {
    return llvm::Intrinsic::fabs;
  }
  if (builtin == stdlib::BuiltinId::FSqrt) {
    return llvm::Intrinsic::sqrt;
  }
  if (builtin == stdlib::BuiltinId::FSin) {
    return llvm::Intrinsic::sin;
  }
  if (builtin == stdlib::BuiltinId::FCos) {
    return llvm::Intrinsic::cos;
  }
  if (builtin == stdlib::BuiltinId::FTan) {
    return llvm::Intrinsic::tan;
  }
  if (builtin == stdlib::BuiltinId::FLog) {
    return llvm::Intrinsic::log;
  }
  if (builtin == stdlib::BuiltinId::FExp) {
    return llvm::Intrinsic::exp;
  }
  if (builtin == stdlib::BuiltinId::FFloor) {
    return llvm::Intrinsic::floor;
  }
  if (builtin == stdlib::BuiltinId::FCeil) {
    return llvm::Intrinsic::ceil;
  }
  if (builtin == stdlib::BuiltinId::FRound) {
    return llvm::Intrinsic::round;
  }
  return std::nullopt;
}

std::optional<std::string_view>
f128MathRuntimeSymbol(stdlib::BuiltinId builtin) {
  using enum stdlib::BuiltinId;
  switch (builtin) {
  case FAbs: return "fabsf128";
  case FSqrt: return "sqrtf128";
  case FSin: return "sinf128";
  case FCos: return "cosf128";
  case FTan: return "tanf128";
  case FLog: return "logf128";
  case FExp: return "expf128";
  case FFloor: return "floorf128";
  case FCeil: return "ceilf128";
  case FRound: return "roundf128";
  case FPow: return "powf128";
  default: return std::nullopt;
  }
}

std::size_t expressionByteLength(const hir::Expr &expression) {
  if (const auto *integer =
          dynamic_cast<const hir::IntegerLiteral *>(&expression)) {
    return integer->byteLength;
  }
  if (const auto *variable =
          dynamic_cast<const hir::VariableRef *>(&expression)) {
    return variable->byteLength;
  }
  if (const auto *binary = dynamic_cast<const hir::BinaryExpr *>(&expression)) {
    return binary->byteLength;
  }
  if (const auto *unary = dynamic_cast<const hir::UnaryExpr *>(&expression)) {
    return unary->byteLength;
  }
  if (const auto *ternary =
          dynamic_cast<const hir::TernaryExpr *>(&expression)) {
    return ternary->byteLength;
  }
  if (const auto *unsignedExpr =
          dynamic_cast<const hir::UnsignedExpr *>(&expression)) {
    return unsignedExpr->byteLength;
  }
  if (const auto *cast =
          dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
    return cast->byteLength;
  }
  if (const auto *view = dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return view->byteLength;
  }
  if (const auto *call =
          dynamic_cast<const hir::UserTemplateOpCallExpr *>(&expression)) {
    return call->byteLength;
  }
  if (const auto *call =
          dynamic_cast<const hir::UserTemplateFormatCallExpr *>(&expression)) {
    return call->byteLength;
  }
  if (const auto *floating =
          dynamic_cast<const hir::FloatLiteral *>(&expression)) {
    return floating->byteLength;
  }
  if (const auto *floating =
          dynamic_cast<const hir::FloatBinaryExpr *>(&expression)) {
    return floating->byteLength;
  }
  if (dynamic_cast<const hir::FloatCompareExpr *>(&expression) != nullptr) {
    return 1;
  }
  if (const auto *conversion =
          dynamic_cast<const hir::ToFloatExpr *>(&expression)) {
    return conversion->byteLength;
  }
  if (const auto *conversion =
          dynamic_cast<const hir::ToIntExpr *>(&expression)) {
    return conversion->byteLength;
  }
  if (const auto *assignment =
          dynamic_cast<const hir::AssignmentExpr *>(&expression)) {
    return assignment->byteLength;
  }
  if (const auto *call = dynamic_cast<const hir::CallExpr *>(&expression)) {
    return call->byteLength;
  }
  if (const auto *swap = dynamic_cast<const hir::ByteSwapExpr *>(&expression)) {
    return swap->byteLength;
  }
  return 4;
}

std::string_view operatorSuffix(std::string_view op) {
  if (!op.starts_with('%')) {
    return op;
  }
  const auto marker = typedOperatorMarker(op);
  if (!marker) {
    return op;
  }
  return op.substr(*marker + 1U);
}

} // namespace

llvm::Value *LlvmEmitter::emitConditionValue(const hir::Expr &expression) {
  auto *value = emitIntegerValue(expression, 4);
  if (!value) {
    return nullptr;
  }
  return builder_.CreateICmpNE(value, builder_.getInt32(0), "cond");
}

llvm::Value *LlvmEmitter::emitIntegerValue(const hir::Expr &expression,
                                           std::size_t byteLength) {
  return emitIntegerValue(expression, byteLength, false);
}

llvm::Value *LlvmEmitter::emitIntegerValue(const hir::Expr &expression,
                                           std::size_t byteLength,
                                           bool unsignedInterpretation) {
  auto *integerType = integerTypeForByteLength(byteLength);
  if (!integerType) {
    return nullptr;
  }

  if (dynamic_cast<const hir::DynamicByteViewExpr *>(&expression) != nullptr ||
      dynamic_cast<const hir::ByteSwapExpr *>(&expression) != nullptr ||
      dynamic_cast<const hir::TemplateViewExpr *>(&expression) != nullptr ||
      dynamic_cast<const hir::UserTemplateOpCallExpr *>(&expression) !=
          nullptr ||
      dynamic_cast<const hir::UserTemplateFormatCallExpr *>(&expression) !=
          nullptr) {
    auto view = emitViewValue(expression);
    if (view.data == nullptr || view.length == nullptr) {
      return nullptr;
    }
    if (view.staticLength) {
      if (*view.staticLength == 0) {
        return llvm::ConstantInt::get(integerType, 0);
      }
      auto *sourceType = integerTypeForByteLength(*view.staticLength);
      if (sourceType == nullptr) {
        return nullptr;
      }
      const auto *resultName =
          dynamic_cast<const hir::UserTemplateFormatCallExpr *>(&expression) !=
                  nullptr
              ? "format.result.value"
              : "view.value";
      auto *loaded = builder_.CreateLoad(sourceType, view.data, resultName);
      llvm::cast<llvm::LoadInst>(loaded)->setAlignment(llvm::Align(1));
      if (sourceType == integerType) {
        return loaded;
      }
      if (sourceType->getIntegerBitWidth() < integerType->getIntegerBitWidth()) {
        return unsignedInterpretation
                   ? builder_.CreateZExt(loaded, integerType, "view.zext")
                   : builder_.CreateSExt(loaded, integerType, "view.sext");
      }
      return builder_.CreateTrunc(loaded, integerType, "view.trunc");
    }
    if (hasRuntimeSafetyChecks()) {
      builder_.CreateCall(declareCheckViewLength(),
                          {view.length, builder_.getInt64(byteLength)});
    }
    auto *loaded = builder_.CreateLoad(integerType, view.data, "dynamic.view");
    llvm::cast<llvm::LoadInst>(loaded)->setAlignment(llvm::Align(1));
    return loaded;
  }

  if (const auto *integer =
          dynamic_cast<const hir::IntegerLiteral *>(&expression)) {
    const auto value = literal::parseUnsignedIntegerLiteral(integer->value);
    if (!value) {
      addDiagnostic("invalid integer literal '" + integer->value + "'");
      return nullptr;
    }
    return llvm::ConstantInt::get(integerType, *value, true);
  }

  if (const auto *variable =
          dynamic_cast<const hir::VariableRef *>(&expression)) {
    const Local *storage = nullptr;
    if (const auto local = locals_.find(variable->bindingName);
        local != locals_.end()) {
      storage = &local->second;
    } else if (const auto global = globals_.find(variable->bindingName);
               global != globals_.end()) {
      storage = &global->second;
    }

    if (storage == nullptr) {
      addDiagnostic("unknown local '" + variable->name + "'");
      return nullptr;
    }

    auto *sourceType = integerTypeForByteLength(variable->byteLength);
    if (!sourceType) {
      return nullptr;
    }

    llvm::Value *loaded = nullptr;
    if (storage->abiType &&
        storage->abiType->kind == hir::AbiValueKind::Pointer) {
      auto *pointer = builder_.CreateLoad(
          builder_.getPtrTy(),
          bytePointer(storage->storageType, storage->storage, variable->offset,
                      variable->name + ".addr"),
          variable->name + ".ptr");
      loaded = builder_.CreatePtrToInt(pointer, sourceType,
                                       variable->name + ".value");
    } else if (storage->abiType &&
               storage->abiType->kind == hir::AbiValueKind::Floating) {
      addDiagnostic("cannot use floating ABI object '" + variable->name +
                    "' as an integer");
      return nullptr;
    } else {
      loaded = builder_.CreateLoad(
          sourceType, bytePointer(storage->storageType, storage->storage,
                                  variable->offset, variable->name + ".addr"),
          variable->name + ".value");
    }
    if (sourceType == integerType) {
      return loaded;
    }
    if (sourceType->getIntegerBitWidth() < integerType->getIntegerBitWidth()) {
      if (unsignedInterpretation) {
        return builder_.CreateZExt(loaded, integerType);
      }
      return builder_.CreateSExt(loaded, integerType);
    }
    return builder_.CreateTrunc(loaded, integerType);
  }

  if (const auto *address =
          dynamic_cast<const hir::AddressOfExpr *>(&expression)) {
    const Local *storage = nullptr;
    if (const auto local = locals_.find(address->bindingName);
        local != locals_.end()) {
      storage = &local->second;
    } else if (const auto global = globals_.find(address->bindingName);
               global != globals_.end()) {
      storage = &global->second;
    }
    if (storage == nullptr) {
      addDiagnostic("unknown local '" + address->name + "'");
      return nullptr;
    }
    auto *pointer =
        bytePointer(storage->storageType, storage->storage, address->offset,
                    address->name + ".address");
    return builder_.CreatePtrToInt(pointer, integerType, "ptrtoint");
  }

  if (const auto *deref = dynamic_cast<const hir::DerefExpr *>(&expression)) {
    auto *addressValue = emitIntegerValue(*deref->address, sizeof(void *));
    if (!addressValue) {
      return nullptr;
    }
    auto *sourceType = integerTypeForByteLength(deref->byteLength);
    if (!sourceType) {
      return nullptr;
    }
    auto *pointer = builder_.CreateIntToPtr(addressValue, builder_.getPtrTy(),
                                            "deref.addr");
    if (hasRuntimeSafetyChecks() &&
        !hasKnownStaticAddressRange(*deref->address, deref->byteLength)) {
      builder_.CreateCall(declareCheckLoad(),
                          {pointer, builder_.getInt64(deref->byteLength)});
    }
    auto *loaded = builder_.CreateLoad(sourceType, pointer, "deref.value");
    if (sourceType == integerType) {
      return loaded;
    }
    if (sourceType->getIntegerBitWidth() < integerType->getIntegerBitWidth()) {
      return unsignedInterpretation ? builder_.CreateZExt(loaded, integerType)
                                    : builder_.CreateSExt(loaded, integerType);
    }
    return builder_.CreateTrunc(loaded, integerType);
  }

  if (const auto *comparison =
          dynamic_cast<const hir::FloatCompareExpr *>(&expression)) {
    auto *left = emitFloatValue(*comparison->left,
                                comparison->operandByteLength);
    if (!left) {
      return nullptr;
    }
    auto *right = emitFloatValue(*comparison->right,
                                 comparison->operandByteLength);
    if (!right) {
      return nullptr;
    }

    llvm::Value *result = nullptr;
    const auto op = operatorSuffix(comparison->op);
    if (op == "==") {
      result = builder_.CreateFCmpOEQ(left, right, "fcmptmp");
    } else if (op == "!=") {
      result = builder_.CreateFCmpUNE(left, right, "fcmptmp");
    } else if (op == "<") {
      result = builder_.CreateFCmpOLT(left, right, "fcmptmp");
    } else if (op == "<=") {
      result = builder_.CreateFCmpOLE(left, right, "fcmptmp");
    } else if (op == ">") {
      result = builder_.CreateFCmpOGT(left, right, "fcmptmp");
    } else if (op == ">=") {
      result = builder_.CreateFCmpOGE(left, right, "fcmptmp");
    } else {
      addDiagnostic("unsupported float comparison operator '" + comparison->op +
                    "'");
      return nullptr;
    }

    auto *booleanValue =
        builder_.CreateZExt(result, builder_.getInt8Ty(), "fcmptmp.zext");
    if (booleanValue->getType() == integerType) {
      return booleanValue;
    }
    if (booleanValue->getType()->getIntegerBitWidth() <
        integerType->getIntegerBitWidth()) {
      return builder_.CreateZExt(booleanValue, integerType);
    }
    return builder_.CreateTrunc(booleanValue, integerType);
  }

  if (const auto *binary = dynamic_cast<const hir::BinaryExpr *>(&expression)) {
    if (isLogical(binary->op)) {
      auto *function = builder_.GetInsertBlock()->getParent();
      auto *rightBlock =
          llvm::BasicBlock::Create(context_, "logic.rhs", function);
      auto *mergeBlock =
          llvm::BasicBlock::Create(context_, "logic.end", function);

      auto *leftCondition = emitConditionValue(*binary->left);
      if (!leftCondition) {
        return nullptr;
      }
      auto *leftBlock = builder_.GetInsertBlock();
      if (binary->op == "&&") {
        builder_.CreateCondBr(leftCondition, rightBlock, mergeBlock);
      } else {
        builder_.CreateCondBr(leftCondition, mergeBlock, rightBlock);
      }

      builder_.SetInsertPoint(rightBlock);
      auto *rightCondition = emitConditionValue(*binary->right);
      if (!rightCondition) {
        return nullptr;
      }
      auto *rightValue = builder_.CreateZExt(rightCondition, integerType);
      builder_.CreateBr(mergeBlock);
      rightBlock = builder_.GetInsertBlock();

      builder_.SetInsertPoint(mergeBlock);
      auto *phi = builder_.CreatePHI(integerType, 2, "logic");
      phi->addIncoming(binary->op == "&&"
                           ? llvm::ConstantInt::get(integerType, 0)
                           : llvm::ConstantInt::get(integerType, 1),
                       leftBlock);
      phi->addIncoming(rightValue, rightBlock);
      return phi;
    }

    const auto op = operatorSuffix(binary->op);
    const bool comparison = isComparison(op);
    std::size_t operationByteLength = binary->byteLength;
    if (comparison) {
      operationByteLength =
          std::max({std::size_t{4}, expressionByteLength(*binary->left),
                    expressionByteLength(*binary->right)});
    }

    auto *sourceType = integerTypeForByteLength(operationByteLength);
    if (!sourceType) {
      return nullptr;
    }

    const bool leftUnsigned = isUnsignedExpression(*binary->left);
    const bool rightUnsigned = isUnsignedExpression(*binary->right);
    const bool unsignedOperation = integerOperationIsUnsigned(binary->op)
                                       .value_or(leftUnsigned || rightUnsigned);
    auto *left =
        emitIntegerValue(*binary->left, operationByteLength, leftUnsigned);
    if (!left) {
      return nullptr;
    }
    auto *right = emitIntegerValue(*binary->right, operationByteLength,
                                   rightUnsigned);
    if (!right) {
      return nullptr;
    }

    llvm::Value *result = nullptr;
    if (op == "+") {
      result = builder_.CreateAdd(left, right, "addtmp");
    } else if (op == "-") {
      result = builder_.CreateSub(left, right, "subtmp");
    } else if (op == "*") {
      result = builder_.CreateMul(left, right, "multmp");
    } else if (op == "**") {
      auto *function = builder_.GetInsertBlock()->getParent();
      auto *resultStorage =
          builder_.CreateAlloca(sourceType, nullptr, "pow.result");
      auto *exponentStorage =
          builder_.CreateAlloca(sourceType, nullptr, "pow.exponent");
      builder_.CreateStore(llvm::ConstantInt::get(sourceType, 1),
                           resultStorage);
      builder_.CreateStore(right, exponentStorage);

      auto *conditionBlock =
          llvm::BasicBlock::Create(context_, "pow.cond", function);
      auto *bodyBlock =
          llvm::BasicBlock::Create(context_, "pow.body", function);
      auto *afterBlock =
          llvm::BasicBlock::Create(context_, "pow.end", function);
      builder_.CreateBr(conditionBlock);

      builder_.SetInsertPoint(conditionBlock);
      auto *currentExponent =
          builder_.CreateLoad(sourceType, exponentStorage, "pow.exp");
      auto *hasMore =
          unsignedOperation
              ? builder_.CreateICmpUGT(currentExponent,
                                       llvm::ConstantInt::get(sourceType, 0),
                                       "pow.more")
              : builder_.CreateICmpSGT(currentExponent,
                                       llvm::ConstantInt::get(sourceType, 0),
                                       "pow.more");
      builder_.CreateCondBr(hasMore, bodyBlock, afterBlock);

      builder_.SetInsertPoint(bodyBlock);
      auto *currentResult =
          builder_.CreateLoad(sourceType, resultStorage, "pow.value");
      auto *nextResult = builder_.CreateMul(currentResult, left, "pow.mul");
      builder_.CreateStore(nextResult, resultStorage);
      auto *nextExponent = builder_.CreateSub(
          currentExponent, llvm::ConstantInt::get(sourceType, 1), "pow.dec");
      builder_.CreateStore(nextExponent, exponentStorage);
      builder_.CreateBr(conditionBlock);

      builder_.SetInsertPoint(afterBlock);
      result = builder_.CreateLoad(sourceType, resultStorage, "pow.result");
    } else if (op == "/") {
      result = unsignedOperation ? builder_.CreateUDiv(left, right, "divtmp")
                                 : builder_.CreateSDiv(left, right, "divtmp");
    } else if (op == "%") {
      result = unsignedOperation ? builder_.CreateURem(left, right, "remtmp")
                                 : builder_.CreateSRem(left, right, "remtmp");
    } else if (op == "<<") {
      result = builder_.CreateShl(left, right, "shltmp");
    } else if (op == ">>") {
      result = unsignedOperation ? builder_.CreateLShr(left, right, "shrtmp")
                                 : builder_.CreateAShr(left, right, "shrtmp");
    } else if (op == "&") {
      result = builder_.CreateAnd(left, right, "andtmp");
    } else if (op == "|") {
      result = builder_.CreateOr(left, right, "ortmp");
    } else if (op == "^") {
      result = builder_.CreateXor(left, right, "xortmp");
    } else if (isComparison(op)) {
      if (op == "<") {
        result = unsignedOperation
                     ? builder_.CreateICmpULT(left, right, "cmptmp")
                     : builder_.CreateICmpSLT(left, right, "cmptmp");
      } else if (op == ">") {
        result = unsignedOperation
                     ? builder_.CreateICmpUGT(left, right, "cmptmp")
                     : builder_.CreateICmpSGT(left, right, "cmptmp");
      } else if (op == "<=") {
        result = unsignedOperation
                     ? builder_.CreateICmpULE(left, right, "cmptmp")
                     : builder_.CreateICmpSLE(left, right, "cmptmp");
      } else if (op == ">=") {
        result = unsignedOperation
                     ? builder_.CreateICmpUGE(left, right, "cmptmp")
                     : builder_.CreateICmpSGE(left, right, "cmptmp");
      } else if (op == "==") {
        result = builder_.CreateICmpEQ(left, right, "cmptmp");
      } else {
        result = builder_.CreateICmpNE(left, right, "cmptmp");
      }
      result = builder_.CreateZExt(result, builder_.getInt8Ty());
    } else {
      addDiagnostic("unsupported binary operator '" + binary->op + "'");
      return nullptr;
    }

    auto *resultType = llvm::cast<llvm::IntegerType>(result->getType());
    if (resultType == integerType) {
      return result;
    }
    if (resultType->getIntegerBitWidth() < integerType->getIntegerBitWidth()) {
      const bool zeroExtend = comparison || unsignedOperation;
      return zeroExtend ? builder_.CreateZExt(result, integerType)
                        : builder_.CreateSExt(result, integerType);
    }
    return builder_.CreateTrunc(result, integerType);
  }

  if (const auto *unary = dynamic_cast<const hir::UnaryExpr *>(&expression)) {
    auto *value = emitIntegerValue(*unary->operand, unary->byteLength,
                                   unsignedInterpretation);
    if (!value) {
      return nullptr;
    }

    llvm::Value *result = nullptr;
    if (unary->op == "!") {
      result = builder_.CreateICmpEQ(
          value, llvm::ConstantInt::get(value->getType(), 0), "nottmp");
      result = builder_.CreateZExt(result, integerType);
    } else if (unary->op == "~") {
      result = builder_.CreateNot(value, "bwnottmp");
    } else if (unary->op == "-") {
      result = builder_.CreateNeg(value, "negtmp");
    } else {
      addDiagnostic("unsupported unary operator '" + unary->op + "'");
      return nullptr;
    }

    if (result->getType() == integerType) {
      return result;
    }
    auto *resultType = llvm::cast<llvm::IntegerType>(result->getType());
    if (resultType->getIntegerBitWidth() < integerType->getIntegerBitWidth()) {
      return unsignedInterpretation ? builder_.CreateZExt(result, integerType)
                                    : builder_.CreateSExt(result, integerType);
    }
    return builder_.CreateTrunc(result, integerType);
  }

  if (const auto *ternary =
          dynamic_cast<const hir::TernaryExpr *>(&expression)) {
    auto *function = builder_.GetInsertBlock()->getParent();
    auto *thenBlock =
        llvm::BasicBlock::Create(context_, "ternary.then", function);
    auto *elseBlock =
        llvm::BasicBlock::Create(context_, "ternary.else", function);
    auto *mergeBlock =
        llvm::BasicBlock::Create(context_, "ternary.end", function);

    auto *condition = emitConditionValue(*ternary->condition);
    if (!condition) {
      return nullptr;
    }
    builder_.CreateCondBr(condition, thenBlock, elseBlock);

    builder_.SetInsertPoint(thenBlock);
    auto *thenValue = emitIntegerValue(*ternary->thenExpr, ternary->byteLength,
                                       unsignedInterpretation);
    if (!thenValue) {
      return nullptr;
    }
    builder_.CreateBr(mergeBlock);
    thenBlock = builder_.GetInsertBlock();

    builder_.SetInsertPoint(elseBlock);
    auto *elseValue = emitIntegerValue(*ternary->elseExpr, ternary->byteLength,
                                       unsignedInterpretation);
    if (!elseValue) {
      return nullptr;
    }
    builder_.CreateBr(mergeBlock);
    elseBlock = builder_.GetInsertBlock();

    builder_.SetInsertPoint(mergeBlock);
    auto *phi = builder_.CreatePHI(integerType, 2, "ternary");
    phi->addIncoming(thenValue, thenBlock);
    phi->addIncoming(elseValue, elseBlock);
    return phi;
  }

  if (const auto *unsignedExpr =
          dynamic_cast<const hir::UnsignedExpr *>(&expression)) {
    return emitIntegerValue(*unsignedExpr->operand, byteLength, true);
  }

  if (const auto *cast =
          dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
    const auto sourceByteLength = expressionByteLength(*cast->operand);
    const bool sourceUnsigned = isUnsignedExpression(*cast->operand);
    auto *source =
        emitIntegerValue(*cast->operand, sourceByteLength, sourceUnsigned);
    if (!source) {
      return nullptr;
    }
    auto *castType = integerTypeForByteLength(cast->byteLength);
    if (!castType) {
      return nullptr;
    }

    llvm::Value *castValue = source;
    auto *sourceType = llvm::cast<llvm::IntegerType>(source->getType());
    if (sourceType->getIntegerBitWidth() > castType->getIntegerBitWidth()) {
      castValue = builder_.CreateTrunc(source, castType, "intcast.trunc");
    } else if (sourceType->getIntegerBitWidth() <
               castType->getIntegerBitWidth()) {
      castValue = sourceUnsigned
                      ? builder_.CreateZExt(source, castType, "intcast.zext")
                      : builder_.CreateSExt(source, castType, "intcast.sext");
    }

    if (castType == integerType) {
      return castValue;
    }
    if (castType->getIntegerBitWidth() < integerType->getIntegerBitWidth()) {
      return cast->isSigned
                 ? builder_.CreateSExt(castValue, integerType,
                                       "intcast.result.sext")
                 : builder_.CreateZExt(castValue, integerType,
                                       "intcast.result.zext");
    }
    return builder_.CreateTrunc(castValue, integerType, "intcast.result.trunc");
  }

  if (const auto *conversion =
          dynamic_cast<const hir::ToIntExpr *>(&expression)) {
    auto *floatValue =
        emitFloatValue(*conversion->operand, conversion->floatByteLength);
    if (!floatValue) {
      return nullptr;
    }
    auto *conversionType = integerTypeForByteLength(conversion->byteLength);
    if (!conversionType) {
      return nullptr;
    }
    auto *converted = conversion->isUnsigned
                          ? builder_.CreateFPToUI(floatValue, conversionType,
                                                  "touinttmp")
                          : builder_.CreateFPToSI(floatValue, conversionType,
                                                  "tointtmp");
    if (conversionType == integerType) {
      return converted;
    }
    if (conversionType->getIntegerBitWidth() <
        integerType->getIntegerBitWidth()) {
      return unsignedInterpretation ? builder_.CreateZExt(converted, integerType)
                                    : builder_.CreateSExt(converted, integerType);
    }
    return builder_.CreateTrunc(converted, integerType);
  }

  if (const auto *call = dynamic_cast<const hir::CallExpr *>(&expression)) {
    auto *value = emitCallValue(*call);
    if (!value) {
      return nullptr;
    }
    if (value->getType() == integerType) {
      return value;
    }
    if (value->getType()->isPointerTy()) {
      return builder_.CreatePtrToInt(value, integerType, "call.ptrtoint");
    }
    auto *sourceType = llvm::dyn_cast<llvm::IntegerType>(value->getType());
    if (sourceType == nullptr) {
      addDiagnostic("function call result is not an integer");
      return nullptr;
    }
    if (sourceType->getIntegerBitWidth() < integerType->getIntegerBitWidth()) {
      return unsignedInterpretation ? builder_.CreateZExt(value, integerType)
                                    : builder_.CreateSExt(value, integerType);
    }
    return builder_.CreateTrunc(value, integerType);
  }

  if (const auto *assignment =
          dynamic_cast<const hir::AssignmentExpr *>(&expression)) {
    for (const auto &store : assignment->stores) {
      emit(*store);
      if (!diagnostics_.empty()) {
        return nullptr;
      }
    }
    if (!assignment->result) {
      return llvm::ConstantInt::get(integerType, 0);
    }
    return emitIntegerValue(*assignment->result, byteLength,
                            unsignedInterpretation);
  }

  addDiagnostic("unsupported integer expression");
  return nullptr;
}

llvm::Value *LlvmEmitter::emitPointerValue(const hir::Expr &expression,
                                           std::string_view name) {
  if (const auto *string = dynamic_cast<const hir::StringLiteral *>(&expression)) {
    const auto bytes = decodeStringLiteral(string->value);
    auto *pointer = builder_.CreateGlobalStringPtr(bytes, std::string(name));
    if (hasRuntimeSafetyChecks()) {
      builder_.CreateCall(declareRegisterStaticObject(),
                          {pointer, builder_.getInt64(bytes.size() + 1U)});
    }
    return pointer;
  }
  if (const auto *variable =
          dynamic_cast<const hir::VariableRef *>(&expression)) {
    const Local *storage = nullptr;
    if (const auto local = locals_.find(variable->bindingName);
        local != locals_.end()) {
      storage = &local->second;
    } else if (const auto global = globals_.find(variable->bindingName);
               global != globals_.end()) {
      storage = &global->second;
    }
    if (storage == nullptr) {
      addDiagnostic("unknown local '" + variable->name + "'");
      return nullptr;
    }
    if (storage->abiType &&
        storage->abiType->kind == hir::AbiValueKind::Pointer) {
      return builder_.CreateLoad(
          builder_.getPtrTy(),
          bytePointer(storage->storageType, storage->storage, variable->offset,
                      name),
          std::string(name) + ".value");
    }
    if (variable->byteLength != sizeof(void *)) {
      return bytePointer(storage->storageType, storage->storage,
                         variable->offset, name);
    }
  }
  if (dynamic_cast<const hir::DynamicByteViewExpr *>(&expression) != nullptr ||
      dynamic_cast<const hir::ByteSwapExpr *>(&expression) != nullptr) {
    return emitViewValue(expression).data;
  }
  if (const auto *call = dynamic_cast<const hir::CallExpr *>(&expression)) {
    auto *value = emitCallValue(*call);
    if (value == nullptr) {
      return nullptr;
    }
    if (value->getType()->isPointerTy()) {
      return value;
    }
    if (value->getType()->isIntegerTy()) {
      return builder_.CreateIntToPtr(value, builder_.getPtrTy(),
                                     std::string(name));
    }
    addDiagnostic("function call result is not a pointer");
    return nullptr;
  }
  auto *address = emitIntegerValue(expression, sizeof(void *));
  if (!address) {
    return nullptr;
  }
  return builder_.CreateIntToPtr(address, builder_.getPtrTy(), std::string(name));
}

ViewValue LlvmEmitter::emitUserTemplateOpCall(
    std::string_view calleeName,
    const std::vector<std::unique_ptr<hir::Expr>> &callArguments,
    std::size_t resultByteLength) {
  auto *callee = module_->getFunction(std::string(calleeName));
  const bool hasResult = resultByteLength != 0U;
  if (callee == nullptr || !callee->getReturnType()->isVoidTy() ||
      callee->arg_size() != callArguments.size() + (hasResult ? 1U : 0U)) {
    addDiagnostic("invalid internal impl op call '" + std::string(calleeName) +
                  "'");
    return {};
  }
  std::vector<llvm::Value *> arguments;
  arguments.reserve(callArguments.size() + (hasResult ? 1U : 0U));
  llvm::Type *storageType = nullptr;
  llvm::AllocaInst *result = nullptr;
  if (hasResult) {
    storageType = llvm::ArrayType::get(builder_.getInt8Ty(), resultByteLength);
    result = createFunctionEntryAlloca(storageType, "implop.result");
    result->setAlignment(llvm::Align(1));
    registerLocalObject(result, resultByteLength);
    arguments.push_back(firstBytePointer(storageType, result));
  }
  for (const auto &argument : callArguments) {
    const auto view = emitViewValue(*argument);
    if (view.data == nullptr || view.length == nullptr) {
      return {};
    }
    arguments.push_back(view.data);
  }
  builder_.CreateCall(callee, arguments);
  if (!hasResult) {
    return {};
  }
  return ViewValue{firstBytePointer(storageType, result),
                   builder_.getInt64(resultByteLength), resultByteLength};
}

ViewValue LlvmEmitter::emitViewValue(const hir::Expr &expression) {
  auto makeStorage = [&](llvm::Type *type, llvm::Value *value,
                         std::size_t byteLength,
                         std::string_view name) -> ViewValue {
    if (type == nullptr || value == nullptr) {
      return {};
    }
    auto *storage = builder_.CreateAlloca(type, nullptr, std::string(name));
    auto *store = builder_.CreateStore(value, storage);
    store->setAlignment(llvm::Align(1));
    if (hasRuntimeSafetyChecks()) {
      builder_.CreateCall(declareRegisterLocalObject(),
                          {storage, builder_.getInt64(byteLength)});
    }
    return ViewValue{storage, builder_.getInt64(byteLength), byteLength};
  };

  if (const auto *view = dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return emitViewValue(*view->operand);
  }

  if (const auto *call =
          dynamic_cast<const hir::UserTemplateOpCallExpr *>(&expression)) {
    return emitUserTemplateOpCall(call->callee, call->arguments,
                                  call->byteLength);
  }

  if (const auto *call =
          dynamic_cast<const hir::UserTemplateFormatCallExpr *>(&expression)) {
    return emitUserTemplateFormatCall(call->callee, *call->value, call->sink,
                                      call->file.get(), call->byteLength);
  }

  if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
    const Local *storage = nullptr;
    if (const auto local = locals_.find(variable->bindingName);
        local != locals_.end()) {
      storage = &local->second;
    } else if (const auto global = globals_.find(variable->bindingName);
               global != globals_.end()) {
      storage = &global->second;
    }
    if (storage == nullptr) {
      addDiagnostic("unknown local '" + variable->name + "'");
      return {};
    }
    return ViewValue{bytePointer(storage->storageType, storage->storage,
                                 variable->offset, variable->name + ".view"),
                     builder_.getInt64(variable->byteLength),
                     variable->byteLength};
  }

  if (const auto *deref = dynamic_cast<const hir::DerefExpr *>(&expression)) {
    auto *address = emitIntegerValue(*deref->address, sizeof(void *));
    if (address == nullptr) {
      return {};
    }
    return ViewValue{builder_.CreateIntToPtr(address, builder_.getPtrTy(),
                                              "deref.view"),
                     builder_.getInt64(deref->byteLength), deref->byteLength};
  }

  if (const auto *dynamic =
          dynamic_cast<const hir::DynamicByteViewExpr *>(&expression)) {
    auto source = emitViewValue(*dynamic->source);
    if (source.data == nullptr || source.length == nullptr) {
      return {};
    }
    if (dynamic->operation == hir::DynamicByteViewOperation::ResizeBytes) {
      if (!dynamic->runtimeLength) {
        addDiagnostic("dynamic resize_bytes is missing its length expression");
        return {};
      }
      auto *resultLength =
          emitIntegerValue(*dynamic->runtimeLength, sizeof(void *), true);
      if (resultLength == nullptr) {
        return {};
      }
      auto *result = builder_.CreateAlloca(builder_.getInt8Ty(), resultLength,
                                            "view.resize.bytes");
      if (hasRuntimeSafetyChecks()) {
        builder_.CreateCall(declareRegisterLocalObject(),
                            {result, resultLength});
      }
      builder_.CreateMemSet(result, builder_.getInt8(0), resultLength,
                            llvm::Align(1));
      auto *sourceShorter = builder_.CreateICmpULT(source.length, resultLength,
                                                    "view.copy.shorter");
      auto *copyLength = builder_.CreateSelect(sourceShorter, source.length,
                                               resultLength, "view.copy.length");
      builder_.CreateMemCpy(result, llvm::Align(1), source.data, llvm::Align(1),
                            copyLength);
      std::optional<std::size_t> staticLength;
      if (const auto *literal = dynamic_cast<const hir::IntegerLiteral *>(
              dynamic->runtimeLength.get())) {
        const auto parsed = literal::parseUnsignedIntegerLiteral(literal->value);
        if (parsed && *parsed <= std::numeric_limits<std::size_t>::max()) {
          staticLength = static_cast<std::size_t>(*parsed);
        }
      }
      return ViewValue{result, resultLength, staticLength};
    }

    auto *result = builder_.CreateAlloca(builder_.getInt8Ty(), source.length,
                                          "view.swap.bytes");
    if (hasRuntimeSafetyChecks()) {
      builder_.CreateCall(declareRegisterLocalObject(),
                          {result, source.length});
    }
    builder_.CreateCall(declareReverseBytes(),
                        {result, source.data, source.length});
    return ViewValue{result, source.length, std::nullopt};
  }

  if (const auto *swap = dynamic_cast<const hir::ByteSwapExpr *>(&expression)) {
    auto source = emitViewValue(*swap->source);
    if (source.data == nullptr || source.length == nullptr) {
      return {};
    }
    auto *result = builder_.CreateAlloca(builder_.getInt8Ty(),
                                          builder_.getInt64(swap->byteLength),
                                          "view.swap.bytes");
    if (hasRuntimeSafetyChecks()) {
      builder_.CreateCall(declareRegisterLocalObject(),
                          {result, builder_.getInt64(swap->byteLength)});
    }
    if (swap->byteLength == 1 || swap->byteLength == 2 ||
        swap->byteLength == 4 || swap->byteLength == 8 ||
        swap->byteLength == 16) {
      auto *type = integerTypeForByteLength(swap->byteLength);
      if (type == nullptr) {
        return {};
      }
      llvm::Value *value =
          builder_.CreateLoad(type, source.data, "view.swap.input");
      llvm::cast<llvm::LoadInst>(value)->setAlignment(llvm::Align(1));
      if (swap->byteLength != 1) {
        auto *intrinsic = llvm::Intrinsic::getDeclaration(
            module_.get(), llvm::Intrinsic::bswap, {type});
        value = builder_.CreateCall(intrinsic, {value}, "view.swap.bswap");
      }
      auto *store = builder_.CreateStore(value, result);
      store->setAlignment(llvm::Align(1));
    } else {
      builder_.CreateCall(declareReverseBytes(),
                          {result, source.data,
                           builder_.getInt64(swap->byteLength)});
    }
    return ViewValue{result, builder_.getInt64(swap->byteLength),
                     swap->byteLength};
  }

  if (const auto *string = dynamic_cast<const hir::StringLiteral *>(&expression)) {
    const auto bytes = decodeStringLiteral(string->value);
    auto *pointer = builder_.CreateGlobalStringPtr(bytes, "view.string");
    if (hasRuntimeSafetyChecks()) {
      builder_.CreateCall(declareRegisterStaticObject(),
                          {pointer, builder_.getInt64(bytes.size() + 1U)});
    }
    return ViewValue{pointer, builder_.getInt64(bytes.size() + 1U),
                     bytes.size() + 1U};
  }

  const auto byteLength = expressionByteLength(expression);
  if (byteLength == 0) {
    addDiagnostic("cannot materialize a zero-length fixed View");
    return {};
  }
  if (const auto *floating = dynamic_cast<const hir::FloatLiteral *>(&expression)) {
    return makeStorage(floatTypeForByteLength(floating->byteLength),
                       emitFloatValue(expression, floating->byteLength),
                       floating->byteLength, "view.float");
  }
  if (const auto *call = dynamic_cast<const hir::CallExpr *>(&expression);
      call != nullptr && call->isFloating) {
    return makeStorage(floatTypeForByteLength(call->byteLength),
                       emitFloatValue(expression, call->byteLength),
                       call->byteLength, "view.float");
  }
  return makeStorage(integerTypeForByteLength(byteLength),
                     emitIntegerValue(expression, byteLength, true),
                     byteLength, "view.value");
}

llvm::Value *LlvmEmitter::emitValueForType(const hir::Expr &expression,
                                           llvm::Type *type,
                                           std::string_view name) {
  if (auto *integer = llvm::dyn_cast<llvm::IntegerType>(type)) {
    return emitIntegerValue(expression, integer->getBitWidth() / 8U);
  }
  if (type->isPointerTy()) {
    return emitPointerValue(expression, name);
  }
  std::size_t floatByteLength = 0;
  if (type->isHalfTy()) {
    floatByteLength = 2;
  } else if (type->isFloatTy()) {
    floatByteLength = 4;
  } else if (type->isDoubleTy()) {
    floatByteLength = 8;
  } else if (type->isFP128Ty()) {
    floatByteLength = 16;
  }
  if (floatByteLength != 0) {
    return emitFloatValue(expression, floatByteLength);
  }
  if (type->isVectorTy()) {
    if (const auto *variable =
            dynamic_cast<const hir::VariableRef *>(&expression)) {
      const Local *storage = nullptr;
      if (const auto local = locals_.find(variable->bindingName);
          local != locals_.end()) {
        storage = &local->second;
      } else if (const auto global = globals_.find(variable->bindingName);
                 global != globals_.end()) {
        storage = &global->second;
      }
      if (storage == nullptr) {
        addDiagnostic("unknown aggregate storage '" + variable->name + "'");
        return nullptr;
      }
      return builder_.CreateLoad(
          type,
          bytePointer(storage->storageType, storage->storage, variable->offset,
                      std::string(name) + ".vector.addr"),
          std::string(name) + ".vector");
    }
    if (const auto *call = dynamic_cast<const hir::CallExpr *>(&expression)) {
      auto *value = emitCallValue(*call);
      if (value != nullptr && value->getType() == type) {
        return value;
      }
      if (value != nullptr) {
        addDiagnostic("function call result does not match vector C ABI type");
      }
      return nullptr;
    }
    addDiagnostic("unsupported vector C ABI expression for '" +
                  std::string(name) + "'");
    return nullptr;
  }
  if (type->isStructTy() || type->isArrayTy()) {
    if (const auto *variable =
            dynamic_cast<const hir::VariableRef *>(&expression)) {
      const Local *storage = nullptr;
      if (const auto local = locals_.find(variable->bindingName);
          local != locals_.end()) {
        storage = &local->second;
      } else if (const auto global = globals_.find(variable->bindingName);
                 global != globals_.end()) {
        storage = &global->second;
      }
      if (storage == nullptr) {
        addDiagnostic("unknown aggregate storage '" + variable->name + "'");
        return nullptr;
      }
      if (storage->storageType == type && variable->offset == 0) {
        return builder_.CreateLoad(type, storage->storage,
                                   std::string(name) + ".aggregate");
      }
      return builder_.CreateLoad(
          type,
          bytePointer(storage->storageType, storage->storage, variable->offset,
                      std::string(name) + ".aggregate.addr"),
          std::string(name) + ".aggregate");
    }
    if (const auto *call = dynamic_cast<const hir::CallExpr *>(&expression)) {
      auto *value = emitCallValue(*call);
      if (value != nullptr && value->getType() == type) {
        return value;
      }
      if (value != nullptr &&
          (value->getType()->isVectorTy() || value->getType()->isIntegerTy()) &&
          module_->getDataLayout().getTypeStoreSize(value->getType()) ==
              module_->getDataLayout().getTypeStoreSize(type)) {
        auto *storage = createFunctionEntryAlloca(type,
                                                  std::string(name) + ".vector.tmp");
        builder_.CreateStore(value, storage);
        return builder_.CreateLoad(type, storage,
                                   std::string(name) + ".aggregate");
      }
      if (value != nullptr) {
        addDiagnostic("function call result does not match aggregate ABI type");
      }
      return nullptr;
    }
    addDiagnostic("unsupported aggregate ABI expression for '" +
                  std::string(name) + "'");
    return nullptr;
  }
  addDiagnostic("unsupported ABI value type for '" + std::string(name) + "'");
  return nullptr;
}

llvm::Value *LlvmEmitter::emitCallValue(const hir::CallExpr &call) {
  auto *ptrTy = builder_.getPtrTy();
  auto *i32Ty = builder_.getInt32Ty();
  auto *i64Ty = builder_.getInt64Ty();
  const auto builtin = call.builtin;

  if (builtin == stdlib::BuiltinId::Print ||
      builtin == stdlib::BuiltinId::Printf ||
      builtin == stdlib::BuiltinId::Fprintf) {
    return emitFormatOutput(call.arguments, call.formatArgumentKinds, builtin,
                            call.callee);
  }
  if (builtin == stdlib::BuiltinId::Memset) {
    auto *address = emitPointerValue(*call.arguments[0], "memset.dst");
    auto *value = emitIntegerValue(*call.arguments[1], 4, true);
    auto *length = emitIntegerValue(*call.arguments[2], sizeof(void *), true);
    if (address == nullptr || value == nullptr || length == nullptr) {
      return nullptr;
    }
    auto callee = declareCFunction(hasRuntimeSafetyChecks() ? "hs_memset"
                                                             : "memset",
                                   ptrTy, {ptrTy, i32Ty, i64Ty});
    auto *result = builder_.CreateCall(callee, {address, value, length},
                                       "memset.ptr");
    return builder_.CreatePtrToInt(result, i64Ty, "memset.addr");
  }
  if (builtin == stdlib::BuiltinId::Put) {
    auto source = emitViewValue(*call.arguments[0]);
    if (source.data == nullptr || source.length == nullptr) {
      return nullptr;
    }
    if (hasRuntimeSafetyChecks()) {
      auto callee = declareCFunction("hs_put", i32Ty, {ptrTy, i64Ty});
      return builder_.CreateCall(callee, {source.data, source.length}, "put.ret");
    }
    auto callee =
        declareCFunction("fwrite", i64Ty, {ptrTy, i64Ty, i64Ty, ptrTy});
    auto *stdoutPointer = builder_.CreateLoad(
        ptrTy, module_->getOrInsertGlobal("stdout", ptrTy));
    auto *written = builder_.CreateCall(
        callee, {source.data, builder_.getInt64(1), source.length, stdoutPointer},
        "put.count");
    return builder_.CreateTrunc(written, i32Ty, "put.ret");
  }

  if (builtin == stdlib::BuiltinId::ResizeBytes) {
    return emitIntegerValue(*call.arguments[0], call.byteLength, true);
  }
  if (builtin == stdlib::BuiltinId::ByteSwap) {
    auto *value = emitIntegerValue(*call.arguments[0], call.byteLength, true);
    if (!value || call.byteLength == 1) {
      return value;
    }
    auto *intrinsic = llvm::Intrinsic::getDeclaration(
        module_.get(), llvm::Intrinsic::bswap,
        {integerTypeForByteLength(call.byteLength)});
    return builder_.CreateCall(intrinsic, {value}, "bswap");
  }
  if (builtin == stdlib::BuiltinId::Alloc) {
    auto *size = emitIntegerValue(*call.arguments[0], sizeof(void *));
    if (!size) {
      return nullptr;
    }
    auto *pointer = builder_.CreateCall(
        hasRuntimeSafetyChecks() ? declareCheckedAlloc() : declareMalloc(),
        {size}, "alloc.ptr");
    return builder_.CreatePtrToInt(pointer, builder_.getInt64Ty(), "alloc.addr");
  }
  if (builtin == stdlib::BuiltinId::Calloc) {
    auto *count = emitIntegerValue(*call.arguments[0], sizeof(void *));
    auto *size = emitIntegerValue(*call.arguments[1], sizeof(void *));
    if (!count || !size) {
      return nullptr;
    }
    auto *pointer =
        hasRuntimeSafetyChecks()
            ? builder_.CreateCall(declareCheckedCalloc(), {count, size},
                                  "calloc.ptr")
            : builder_.CreateCall(declareCalloc(), {count, size},
                                  "calloc.ptr");
    return builder_.CreatePtrToInt(pointer, builder_.getInt64Ty(),
                                   "calloc.addr");
  }
  if (builtin == stdlib::BuiltinId::Realloc) {
    auto *address = emitIntegerValue(*call.arguments[0], sizeof(void *));
    auto *size = emitIntegerValue(*call.arguments[1], sizeof(void *));
    if (!address || !size) {
      return nullptr;
    }
    auto *pointer =
        builder_.CreateIntToPtr(address, builder_.getPtrTy(), "realloc.in");
    auto *result = builder_.CreateCall(
        hasRuntimeSafetyChecks() ? declareCheckedRealloc() : declareRealloc(),
        {pointer, size}, "realloc.ptr");
    return builder_.CreatePtrToInt(result, builder_.getInt64Ty(),
                                   "realloc.addr");
  }
  if (builtin == stdlib::BuiltinId::Memcpy ||
      builtin == stdlib::BuiltinId::Memmove) {
    auto *dst = emitPointerValue(*call.arguments[0], "mem.dst");
    auto *src = emitPointerValue(*call.arguments[1], "mem.src");
    auto *length = emitIntegerValue(*call.arguments[2], sizeof(void *), true);
    if (!dst || !src || !length) {
      return nullptr;
    }
    const auto calleeName = hasRuntimeSafetyChecks()
                                ? "hs_" + call.callee
                                : call.callee;
    auto callee = declareCFunction(calleeName, ptrTy, {ptrTy, ptrTy, i64Ty});
    auto *result = builder_.CreateCall(callee, {dst, src, length},
                                       call.callee + ".ptr");
    return builder_.CreatePtrToInt(result, i64Ty, call.callee + ".addr");
  }
  if (builtin == stdlib::BuiltinId::Memcmp) {
    auto *left = emitPointerValue(*call.arguments[0], "memcmp.left");
    auto *right = emitPointerValue(*call.arguments[1], "memcmp.right");
    auto *length = emitIntegerValue(*call.arguments[2], sizeof(void *), true);
    if (!left || !right || !length) {
      return nullptr;
    }
    auto callee = declareCFunction(hasRuntimeSafetyChecks() ? "hs_memcmp"
                                                             : "memcmp",
                                   i32Ty, {ptrTy, ptrTy, i64Ty});
    return builder_.CreateCall(callee, {left, right, length}, "memcmp.ret");
  }
  if (builtin == stdlib::BuiltinId::Strlen) {
    auto *str = emitPointerValue(*call.arguments[0], "strlen.str");
    if (!str) {
      return nullptr;
    }
    auto callee = declareCFunction(hasRuntimeSafetyChecks() ? "hs_strlen"
                                                             : "strlen",
                                   i64Ty, {ptrTy});
    return builder_.CreateCall(callee, {str}, "strlen.ret");
  }
  if (builtin == stdlib::BuiltinId::Strcmp) {
    auto *left = emitPointerValue(*call.arguments[0], "strcmp.left");
    auto *right = emitPointerValue(*call.arguments[1], "strcmp.right");
    if (!left || !right) {
      return nullptr;
    }
    auto callee = declareCFunction(hasRuntimeSafetyChecks() ? "hs_strcmp"
                                                             : "strcmp",
                                   i32Ty, {ptrTy, ptrTy});
    return builder_.CreateCall(callee, {left, right}, "strcmp.ret");
  }
  if (builtin == stdlib::BuiltinId::Strcpy ||
      builtin == stdlib::BuiltinId::Strcat) {
    auto *dst = emitPointerValue(*call.arguments[0], "str.dst");
    auto *src = emitPointerValue(*call.arguments[1], "str.src");
    if (!dst || !src) {
      return nullptr;
    }
    const auto calleeName = hasRuntimeSafetyChecks()
                                ? "hs_" + call.callee
                                : call.callee;
    auto callee = declareCFunction(calleeName, ptrTy, {ptrTy, ptrTy});
    auto *result = builder_.CreateCall(callee, {dst, src}, call.callee + ".ptr");
    return builder_.CreatePtrToInt(result, i64Ty, call.callee + ".addr");
  }
  if (builtin == stdlib::BuiltinId::Strncpy) {
    auto *dst = emitPointerValue(*call.arguments[0], "strncpy.dst");
    auto *src = emitPointerValue(*call.arguments[1], "strncpy.src");
    auto *count = emitIntegerValue(*call.arguments[2], sizeof(void *), true);
    if (!dst || !src || !count) {
      return nullptr;
    }
    auto callee = declareCFunction(hasRuntimeSafetyChecks() ? "hs_strncpy"
                                                             : "strncpy",
                                   ptrTy, {ptrTy, ptrTy, i64Ty});
    auto *result = builder_.CreateCall(callee, {dst, src, count}, "strncpy.ptr");
    return builder_.CreatePtrToInt(result, i64Ty, "strncpy.addr");
  }
  if (builtin == stdlib::BuiltinId::Strchr) {
    auto *str = emitPointerValue(*call.arguments[0], "strchr.str");
    auto *byte = emitIntegerValue(*call.arguments[1], 4, true);
    if (!str || !byte) {
      return nullptr;
    }
    auto callee = declareCFunction(hasRuntimeSafetyChecks() ? "hs_strchr"
                                                             : "strchr",
                                   ptrTy, {ptrTy, i32Ty});
    auto *result = builder_.CreateCall(callee, {str, byte}, "strchr.ptr");
    return builder_.CreatePtrToInt(result, i64Ty, "strchr.addr");
  }
  if (builtin == stdlib::BuiltinId::Fopen) {
    auto *name = emitPointerValue(*call.arguments[0], "fopen.name");
    auto *mode = emitPointerValue(*call.arguments[1], "fopen.mode");
    if (!name || !mode) {
      return nullptr;
    }
    auto callee = declareCFunction("fopen", ptrTy, {ptrTy, ptrTy});
    auto *result = builder_.CreateCall(callee, {name, mode}, "fopen.ptr");
    return builder_.CreatePtrToInt(result, i64Ty, "fopen.handle");
  }
  if (builtin == stdlib::BuiltinId::Fclose ||
      builtin == stdlib::BuiltinId::Fflush ||
      builtin == stdlib::BuiltinId::Feof ||
      builtin == stdlib::BuiltinId::Ferror) {
    auto *file = emitPointerValue(*call.arguments[0], call.callee + ".file");
    if (!file) {
      return nullptr;
    }
    auto callee = declareCFunction(call.callee, i32Ty, {ptrTy});
    return builder_.CreateCall(callee, {file}, call.callee + ".ret");
  }
  if (builtin == stdlib::BuiltinId::Get) {
    auto callee = declareCFunction("getchar", i32Ty, {});
    return builder_.CreateCall(callee, {}, "get.ret");
  }
  if (builtin == stdlib::BuiltinId::Fget) {
    auto *file = emitPointerValue(*call.arguments[0], "fget.file");
    if (!file) {
      return nullptr;
    }
    auto callee = declareCFunction("fgetc", i32Ty, {ptrTy});
    return builder_.CreateCall(callee, {file}, "fget.ret");
  }
  if (builtin == stdlib::BuiltinId::Fput) {
    auto *file = emitPointerValue(*call.arguments[0], "fput.file");
    auto source = emitViewValue(*call.arguments[1]);
    if (source.data == nullptr || source.length == nullptr || !file) {
      return nullptr;
    }
    if (hasRuntimeSafetyChecks()) {
      auto callee = declareCFunction("hs_fput", i32Ty, {ptrTy, ptrTy, i64Ty});
      return builder_.CreateCall(callee, {file, source.data, source.length},
                                 "fput.ret");
    }
    auto callee = declareCFunction("fwrite", i64Ty, {ptrTy, i64Ty, i64Ty, ptrTy});
    auto *written = builder_.CreateCall(callee,
                                        {source.data, builder_.getInt64(1),
                                         source.length, file}, "fput.count");
    return builder_.CreateTrunc(written, i32Ty, "fput.ret");
  }
  if (builtin == stdlib::BuiltinId::Fread ||
      builtin == stdlib::BuiltinId::Fwrite) {
    auto *buffer = emitPointerValue(*call.arguments[0], call.callee + ".buffer");
    auto *size = emitIntegerValue(*call.arguments[1], sizeof(void *), true);
    auto *count = emitIntegerValue(*call.arguments[2], sizeof(void *), true);
    auto *file = emitPointerValue(*call.arguments[3], call.callee + ".file");
    if (!buffer || !size || !count || !file) {
      return nullptr;
    }
    const auto calleeName = hasRuntimeSafetyChecks()
                                ? "hs_" + call.callee
                                : call.callee;
    auto callee = declareCFunction(calleeName, i64Ty,
                                   {ptrTy, i64Ty, i64Ty, ptrTy});
    return builder_.CreateCall(callee, {buffer, size, count, file},
                               call.callee + ".ret");
  }
  if (builtin == stdlib::BuiltinId::Fseek) {
    auto *file = emitPointerValue(*call.arguments[0], "fseek.file");
    auto *offset = emitIntegerValue(*call.arguments[1], sizeof(void *));
    auto *origin = emitIntegerValue(*call.arguments[2], 4);
    if (!file || !offset || !origin) {
      return nullptr;
    }
    auto callee = declareCFunction("fseek", i32Ty, {ptrTy, i64Ty, i32Ty});
    return builder_.CreateCall(callee, {file, offset, origin}, "fseek.ret");
  }
  if (builtin == stdlib::BuiltinId::Ftell) {
    auto *file = emitPointerValue(*call.arguments[0], "ftell.file");
    if (!file) {
      return nullptr;
    }
    auto callee = declareCFunction("ftell", i64Ty, {ptrTy});
    return builder_.CreateCall(callee, {file}, "ftell.ret");
  }
  if (builtin == stdlib::BuiltinId::Rand) {
    auto callee = declareCFunction("rand", i32Ty, {});
    return builder_.CreateCall(callee, {}, "rand.ret");
  }
  if (builtin == stdlib::BuiltinId::TimeMs ||
      builtin == stdlib::BuiltinId::ClockMs) {
    auto callee = declareCFunction("hs_" + call.callee, i64Ty, {});
    return builder_.CreateCall(callee, {}, call.callee + ".ret");
  }
  if (builtin == stdlib::BuiltinId::Abs) {
    auto *value = emitIntegerValue(*call.arguments[0], call.byteLength);
    if (!value) {
      return nullptr;
    }
    auto *zero = llvm::ConstantInt::get(value->getType(), 0);
    auto *negated = builder_.CreateNeg(value, "abs.neg");
    auto *negative = builder_.CreateICmpSLT(value, zero, "abs.isneg");
    return builder_.CreateSelect(negative, negated, value, "abs.ret");
  }
  if (builtin == stdlib::BuiltinId::Min || builtin == stdlib::BuiltinId::Max) {
    if (call.isFloating) {
      auto *left = emitFloatValue(*call.arguments[0], call.byteLength);
      auto *right = emitFloatValue(*call.arguments[1], call.byteLength);
      if (!left || !right) {
        return nullptr;
      }
      auto *chooseLeft = builtin == stdlib::BuiltinId::Min
                             ? builder_.CreateFCmpOLT(left, right, "min.cmp")
                             : builder_.CreateFCmpOGT(left, right, "max.cmp");
      return builder_.CreateSelect(chooseLeft, left, right,
                                   call.callee + ".ret");
    }
    auto *left = emitIntegerValue(*call.arguments[0], call.byteLength);
    auto *right = emitIntegerValue(*call.arguments[1], call.byteLength);
    if (!left || !right) {
      return nullptr;
    }
    const bool isUnsigned = isUnsignedExpression(*call.arguments[0]);
    auto *chooseLeft = builtin == stdlib::BuiltinId::Min
                           ? (isUnsigned ? builder_.CreateICmpULT(left, right, "min.cmp")
                                         : builder_.CreateICmpSLT(left, right, "min.cmp"))
                           : (isUnsigned ? builder_.CreateICmpUGT(left, right, "max.cmp")
                                         : builder_.CreateICmpSGT(left, right, "max.cmp"));
    return builder_.CreateSelect(chooseLeft, left, right, call.callee + ".ret");
  }
  if (isCharacterClassifier(builtin)) {
    auto *value = emitIntegerValue(*call.arguments[0], 4, true);
    if (!value) {
      return nullptr;
    }
    auto *byte = builder_.CreateAnd(
        value, llvm::ConstantInt::get(i32Ty, 0xff), "ctype.byte");
    const auto inRange = [this, i32Ty](llvm::Value *input, unsigned lower,
                                       unsigned upper, std::string_view name) {
      auto *atLeast = builder_.CreateICmpUGE(
          input, llvm::ConstantInt::get(i32Ty, lower),
          std::string(name) + ".lower");
      auto *atMost = builder_.CreateICmpULE(
          input, llvm::ConstantInt::get(i32Ty, upper),
          std::string(name) + ".upper");
      return builder_.CreateAnd(atLeast, atMost, std::string(name) + ".match");
    };
    auto *isDigit = inRange(byte, '0', '9', "ctype.digit");
    auto *isUpper = inRange(byte, 'A', 'Z', "ctype.upper");
    auto *isLower = inRange(byte, 'a', 'z', "ctype.lower");
    if (builtin == stdlib::BuiltinId::IsDigit) {
      return isDigit;
    }
    if (builtin == stdlib::BuiltinId::IsAlpha) {
      return builder_.CreateOr(isUpper, isLower, "ctype.alpha");
    }
    if (builtin == stdlib::BuiltinId::IsAlnum) {
      return builder_.CreateOr(isDigit,
                               builder_.CreateOr(isUpper, isLower,
                                                 "ctype.alpha"),
                               "ctype.alnum");
    }
    if (builtin == stdlib::BuiltinId::IsSpace) {
      auto *control = inRange(byte, '\t', '\r', "ctype.control_space");
      auto *space = builder_.CreateICmpEQ(
          byte, llvm::ConstantInt::get(i32Ty, ' '), "ctype.space");
      return builder_.CreateOr(control, space, "ctype.is_space");
    }
    const auto delta = builtin == stdlib::BuiltinId::ToUpper ? -32 : 32;
    auto *adjusted = builder_.CreateAdd(
        byte, llvm::ConstantInt::getSigned(i32Ty, delta),
        builtin == stdlib::BuiltinId::ToUpper ? "ctype.to_upper.adjust"
                                              : "ctype.to_lower.adjust");
    auto *matches = builtin == stdlib::BuiltinId::ToUpper ? isLower : isUpper;
    return builder_.CreateSelect(
        matches, adjusted, byte,
        builtin == stdlib::BuiltinId::ToUpper ? "ctype.to_upper"
                                              : "ctype.to_lower");
  }

  auto *callee = module_->getFunction(call.callee);
  if (callee == nullptr) {
    addDiagnostic("unknown function '" + call.callee + "'");
    return nullptr;
  }
  if (const auto *memoryPlan = cAbiMemoryPlan(call.callee)) {
    return emitCAbiMemoryCall(call.callee, call.arguments, *callee, *memoryPlan);
  }
  if (cAbiDirectAggregateParameters_.contains(call.callee) ||
      cAbiDirectAggregateReturns_.contains(call.callee)) {
    return emitCAbiDirectCall(call.callee, call.arguments, *callee);
  }
  std::vector<llvm::Value *> arguments;
  if (callee->arg_size() != call.arguments.size()) {
    addDiagnostic("function argument count does not match declaration for '" +
                  call.callee + "'");
    return nullptr;
  }
  for (std::size_t index = 0; index < call.arguments.size(); ++index) {
    auto *parameterType = callee->getFunctionType()->getParamType(index);
    auto *value = emitValueForType(*call.arguments[index], parameterType,
                                   call.callee + ".arg");
    if (!value) {
      return nullptr;
    }
    arguments.push_back(value);
  }
  return builder_.CreateCall(callee, arguments, call.callee + ".ret");
}

llvm::Value *LlvmEmitter::emitFloatValue(const hir::Expr &expression,
                                         std::size_t byteLength) {
  auto *floatType = floatTypeForByteLength(byteLength);
  if (!floatType) {
    return nullptr;
  }

  if (dynamic_cast<const hir::TemplateViewExpr *>(&expression) != nullptr) {
    const auto view = emitViewValue(expression);
    if (view.data == nullptr || view.staticLength != byteLength) {
      addDiagnostic("temporary template view does not match requested float width");
      return nullptr;
    }
    auto *loaded = builder_.CreateLoad(floatType, view.data, "view.float");
    llvm::cast<llvm::LoadInst>(loaded)->setAlignment(llvm::Align(1));
    return loaded;
  }

  if (const auto *floating =
          dynamic_cast<const hir::FloatLiteral *>(&expression)) {
    std::string text = floating->value;
    text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
    if (byteLength == 16) {
      auto *literal = builder_.CreateGlobalStringPtr(text, "f128.literal");
      auto callee = declareCFunction("hs_f128_literal", floatType,
                                     {builder_.getPtrTy()});
      return builder_.CreateCall(callee, {literal}, "f128.literal.value");
    }
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() + text.size()) {
      return llvm::ConstantFP::get(floatType, value);
    }
    addDiagnostic("invalid float literal '" + floating->value + "'");
    return nullptr;
  }

  if (const auto *variable =
          dynamic_cast<const hir::VariableRef *>(&expression)) {
    const Local *storage = nullptr;
    if (const auto local = locals_.find(variable->bindingName);
        local != locals_.end()) {
      storage = &local->second;
    } else if (const auto global = globals_.find(variable->bindingName);
               global != globals_.end()) {
      storage = &global->second;
    }

    if (storage == nullptr) {
      addDiagnostic("unknown local '" + variable->name + "'");
      return nullptr;
    }

    auto *sourceType = floatTypeForByteLength(variable->byteLength);
    if (!sourceType) {
      return nullptr;
    }
    auto *loaded = builder_.CreateLoad(
        sourceType, bytePointer(storage->storageType, storage->storage,
                                variable->offset, variable->name + ".addr"),
        variable->name + ".float");
    if (sourceType == floatType) {
      return loaded;
    }
    if (variable->byteLength < byteLength) {
      return builder_.CreateFPExt(loaded, floatType);
    }
    return builder_.CreateFPTrunc(loaded, floatType);
  }

  if (const auto *binary =
          dynamic_cast<const hir::FloatBinaryExpr *>(&expression)) {
    auto *left = emitFloatValue(*binary->left, binary->byteLength);
    if (!left) {
      return nullptr;
    }
    auto *right = emitFloatValue(*binary->right, binary->byteLength);
    if (!right) {
      return nullptr;
    }

    const auto op = operatorSuffix(binary->op);
    if (op == "+") {
      return builder_.CreateFAdd(left, right, "faddtmp");
    }
    if (op == "-") {
      return builder_.CreateFSub(left, right, "fsubtmp");
    }
    if (op == "*") {
      return builder_.CreateFMul(left, right, "fmultmp");
    }
    if (op == "/") {
      return builder_.CreateFDiv(left, right, "fdivtmp");
    }
    if (op == "**") {
      if (binary->byteLength == 16) {
        auto callee = declareCFunction("powf128", floatType,
                                       {floatType, floatType});
        return builder_.CreateCall(callee, {left, right}, "fpowtmp");
      }
      auto *powFunction = llvm::Intrinsic::getDeclaration(
          module_.get(), llvm::Intrinsic::pow, {floatType});
      return builder_.CreateCall(powFunction, {left, right}, "fpowtmp");
    }

    addDiagnostic("unsupported float operator '" + binary->op + "'");
    return nullptr;
  }

  if (const auto *conversion =
          dynamic_cast<const hir::ToFloatExpr *>(&expression)) {
    if (conversion->sourceIsFloating) {
      const auto sourceLength = expressionByteLength(*conversion->operand);
      auto *source = emitFloatValue(*conversion->operand, sourceLength);
      if (!source) {
        return nullptr;
      }
      if (source->getType() == floatType) {
        return source;
      }
      return sourceLength < byteLength
                 ? builder_.CreateFPExt(source, floatType, "tofloat.fpext")
                 : builder_.CreateFPTrunc(source, floatType, "tofloat.fptrunc");
    }
    auto *integerValue = emitIntegerValue(
        *conversion->operand, expressionByteLength(*conversion->operand));
    if (!integerValue) {
      return nullptr;
    }
    return conversion->sourceUnsigned
               ? builder_.CreateUIToFP(integerValue, floatType, "toufloattmp")
               : builder_.CreateSIToFP(integerValue, floatType, "tofloattmp");
  }

  if (const auto *call = dynamic_cast<const hir::CallExpr *>(&expression)) {
    if (const auto intrinsicId = unaryFloatMathIntrinsic(call->builtin)) {
      auto *operand = emitFloatValue(*call->arguments[0], call->byteLength);
      if (!operand) {
        return nullptr;
      }
      if (call->byteLength == 2) {
        auto *f32Ty = builder_.getFloatTy();
        auto *extended = builder_.CreateFPExt(operand, f32Ty, "f16.math.fpext");
        auto *intrinsic = llvm::Intrinsic::getDeclaration(
            module_.get(), *intrinsicId, {f32Ty});
        auto *result = builder_.CreateCall(intrinsic, {extended},
                                           call->callee + ".f32");
        return builder_.CreateFPTrunc(result, floatType, "f16.math.fptrunc");
      }
      if (call->byteLength == 16) {
        auto symbol = f128MathRuntimeSymbol(call->builtin);
        if (!symbol) {
          addDiagnostic("missing f128 runtime symbol for '" + call->callee + "'");
          return nullptr;
        }
        auto callee = declareCFunction(*symbol, floatType, {floatType});
        return builder_.CreateCall(callee, {operand}, call->callee + ".ret");
      }
      auto *intrinsic = llvm::Intrinsic::getDeclaration(
          module_.get(), *intrinsicId, {floatType});
      return builder_.CreateCall(intrinsic, {operand}, call->callee + ".ret");
    }
    if (call->builtin == stdlib::BuiltinId::FPow) {
      auto *base = emitFloatValue(*call->arguments[0], call->byteLength);
      auto *exponent = emitFloatValue(*call->arguments[1], call->byteLength);
      if (!base || !exponent) {
        return nullptr;
      }
      if (call->byteLength == 2) {
        auto *f32Ty = builder_.getFloatTy();
        auto *extendedBase = builder_.CreateFPExt(base, f32Ty, "f16.pow.base");
        auto *extendedExponent =
            builder_.CreateFPExt(exponent, f32Ty, "f16.pow.exponent");
        auto *intrinsic = llvm::Intrinsic::getDeclaration(
            module_.get(), llvm::Intrinsic::pow, {f32Ty});
        auto *result = builder_.CreateCall(intrinsic,
                                           {extendedBase, extendedExponent},
                                           "f16.pow.f32");
        return builder_.CreateFPTrunc(result, floatType, "f16.pow.fptrunc");
      }
      if (call->byteLength == 16) {
        auto callee = declareCFunction("powf128", floatType,
                                       {floatType, floatType});
        return builder_.CreateCall(callee, {base, exponent}, "f_pow.ret");
      }
      auto *intrinsic = llvm::Intrinsic::getDeclaration(
          module_.get(), llvm::Intrinsic::pow, {floatType});
      return builder_.CreateCall(intrinsic, {base, exponent}, "f_pow.ret");
    }
    auto *value = emitCallValue(*call);
    if (value == nullptr) {
      return nullptr;
    }
    if (!value->getType()->isFloatingPointTy()) {
      addDiagnostic("function call result is not floating point");
      return nullptr;
    }
    if (value->getType() == floatType) {
      return value;
    }
    if (call->byteLength < byteLength) {
      return builder_.CreateFPExt(value, floatType, "call.fpext");
    }
    return builder_.CreateFPTrunc(value, floatType, "call.fptrunc");
  }

  addDiagnostic("unsupported float expression");
  return nullptr;
}

llvm::IntegerType *
LlvmEmitter::integerTypeForByteLength(std::size_t byteLength) {
  if (byteLength == 0 ||
      byteLength > std::numeric_limits<unsigned>::max() / 8U) {
    addDiagnostic("unsupported integer byte length " +
                  std::to_string(byteLength));
    return nullptr;
  }
  return llvm::IntegerType::get(context_,
                                static_cast<unsigned>(byteLength * 8U));
}

llvm::Type *LlvmEmitter::floatTypeForByteLength(std::size_t byteLength) {
  switch (byteLength) {
  case 2:
    return builder_.getHalfTy();
  case 4:
    return builder_.getFloatTy();
  case 8:
    return builder_.getDoubleTy();
  case 16:
    return llvm::Type::getFP128Ty(context_);
  default:
    addDiagnostic("unsupported float byte length " +
                  std::to_string(byteLength));
    return nullptr;
  }
}

llvm::Type *LlvmEmitter::abiTypeFor(const hir::AbiType &type) {
  llvm::Type *elementType = nullptr;
  switch (type.kind) {
  case hir::AbiValueKind::Integer:
    elementType = integerTypeForByteLength(type.byteLength);
    break;
  case hir::AbiValueKind::Floating:
    elementType = floatTypeForByteLength(type.byteLength);
    break;
  case hir::AbiValueKind::Pointer:
    if (type.byteLength != sizeof(void *)) {
      addDiagnostic("pointer ABI byte length does not match the host target");
      return nullptr;
    }
    elementType = builder_.getPtrTy();
    break;
  case hir::AbiValueKind::Aggregate: {
    if (type.byteLength == 0 || type.alignment == 0 ||
        type.aggregateFields.empty() ||
        type.aggregateFields.size() != type.aggregateFieldOffsets.size()) {
      addDiagnostic("invalid aggregate ABI layout");
      return nullptr;
    }
    std::vector<llvm::Type *> fields;
    for (std::size_t index = 0; index < type.aggregateFields.size(); ++index) {
      const auto expectedOffset = type.aggregateFieldOffsets[index];
      const auto& field = type.aggregateFields[index];
      if (field.elementCount == 0 ||
          field.byteLength > std::numeric_limits<std::size_t>::max() /
                                 field.elementCount ||
          expectedOffset > type.byteLength) {
        addDiagnostic("invalid aggregate ABI field layout");
        return nullptr;
      }
      auto *fieldType = abiTypeFor(field);
      if (fieldType == nullptr) {
        return nullptr;
      }
      const auto fieldLength = field.byteLength * field.elementCount;
      if (fieldLength > type.byteLength - expectedOffset) {
        addDiagnostic("aggregate ABI field exceeds object size");
        return nullptr;
      }
      fields.push_back(fieldType);
    }
    // Keep C padding implicit.  Materializing padding as byte-array fields
    // changes LLVM's aggregate classification and breaks the host C ABI for
    // by-value parameters and returns.
    auto *structType = llvm::StructType::get(context_, fields, false);
    const auto *layout = module_->getDataLayout().getStructLayout(structType);
    if (layout->getSizeInBytes().getFixedValue() != type.byteLength ||
        layout->getAlignment().value() != type.alignment) {
      addDiagnostic("aggregate ABI layout does not match the host target");
      return nullptr;
    }
    for (std::size_t index = 0; index < fields.size(); ++index) {
      if (layout->getElementOffset(static_cast<unsigned>(index)).getFixedValue() !=
          type.aggregateFieldOffsets[index]) {
        addDiagnostic("aggregate ABI field offset does not match the host target");
        return nullptr;
      }
    }
    elementType = structType;
    break;
  }
  }
  if (elementType == nullptr) {
    return nullptr;
  }
  if (type.elementCount == 0) {
    addDiagnostic("ABI array element count cannot be zero");
    return nullptr;
  }
  if (type.elementCount == 1) {
    return elementType;
  }
  return llvm::ArrayType::get(elementType, type.elementCount);
}

} // namespace hitsimple::codegen
