#include "LlvmEmitter.h"

#include <llvm/IR/Attributes.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/Alignment.h>

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <utility>

namespace hitsimple::codegen {
namespace {

bool isIndirectCAbiAggregate(const hir::AbiType& type) {
  return type.kind == hir::AbiValueKind::Aggregate && type.byteLength > 16U;
}

enum class CAbiEightbyteClass {
  None,
  Integer,
  Sse,
  Memory,
};

struct CAbiFloatLeaf final {
  std::size_t offset = 0;
  std::size_t byteLength = 0;
};

struct CAbiScalarLeaf final {
  hir::AbiValueKind kind = hir::AbiValueKind::Integer;
  std::size_t offset = 0;
  std::size_t byteLength = 0;
};

struct CAbiClassification final {
  std::array<CAbiEightbyteClass, 2> classes = {
      CAbiEightbyteClass::None, CAbiEightbyteClass::None};
  std::vector<CAbiFloatLeaf> floatLeaves;
  std::vector<CAbiScalarLeaf> scalarLeaves;
  std::size_t byteLength = 0;
};

CAbiEightbyteClass mergeClass(CAbiEightbyteClass left,
                              CAbiEightbyteClass right) {
  if (left == CAbiEightbyteClass::None) {
    return right;
  }
  if (right == CAbiEightbyteClass::None || left == right) {
    return left;
  }
  if (left == CAbiEightbyteClass::Memory ||
      right == CAbiEightbyteClass::Memory) {
    return CAbiEightbyteClass::Memory;
  }
  return CAbiEightbyteClass::Integer;
}

bool classifyCAbiValue(const hir::AbiType& type, std::size_t offset,
                       CAbiClassification& classification) {
  if (type.elementCount == 0U || type.byteLength == 0U ||
      type.byteLength > std::numeric_limits<std::size_t>::max() /
                            type.elementCount) {
    return false;
  }
  if (type.elementCount != 1U) {
    hir::AbiType element = type;
    element.elementCount = 1U;
    for (std::size_t index = 0; index < type.elementCount; ++index) {
      const auto elementOffset = offset + index * type.byteLength;
      if (elementOffset < offset ||
          !classifyCAbiValue(element, elementOffset, classification)) {
        return false;
      }
    }
    return true;
  }

  if (type.alignment == 0U || offset % type.alignment != 0U ||
      offset > classification.byteLength ||
      type.byteLength > classification.byteLength - offset) {
    return false;
  }

  if (type.kind == hir::AbiValueKind::Aggregate) {
    if (type.aggregateFields.empty() ||
        type.aggregateFields.size() != type.aggregateFieldOffsets.size()) {
      return false;
    }
    for (std::size_t index = 0; index < type.aggregateFields.size(); ++index) {
      const auto fieldOffset = type.aggregateFieldOffsets[index];
      if (fieldOffset > type.byteLength || offset >
          std::numeric_limits<std::size_t>::max() - fieldOffset ||
          !classifyCAbiValue(type.aggregateFields[index], offset + fieldOffset,
                             classification)) {
        return false;
      }
    }
    return true;
  }

  if (type.kind != hir::AbiValueKind::Integer &&
      type.kind != hir::AbiValueKind::Pointer &&
      type.kind != hir::AbiValueKind::Floating) {
    return false;
  }
  const auto firstEightbyte = offset / 8U;
  const auto lastEightbyte = (offset + type.byteLength - 1U) / 8U;
  if (lastEightbyte >= classification.classes.size()) {
    return false;
  }
  const auto valueClass = type.kind == hir::AbiValueKind::Floating
                              ? CAbiEightbyteClass::Sse
                              : CAbiEightbyteClass::Integer;
  for (std::size_t index = firstEightbyte; index <= lastEightbyte; ++index) {
    classification.classes[index] =
        mergeClass(classification.classes[index], valueClass);
  }
  if (type.kind == hir::AbiValueKind::Floating) {
    classification.floatLeaves.push_back({offset, type.byteLength});
  }
  classification.scalarLeaves.push_back({type.kind, offset, type.byteLength});
  return true;
}

std::optional<llvm::Type *> integerPieceType(
    llvm::LLVMContext& context, const CAbiClassification& classification,
    std::size_t offset, std::size_t byteLength) {
  if (offset > std::numeric_limits<std::size_t>::max() - byteLength) {
    return std::nullopt;
  }
  const auto pieceEnd = offset + byteLength;
  std::vector<CAbiScalarLeaf> leaves;
  for (const auto& leaf : classification.scalarLeaves) {
    if (leaf.offset > std::numeric_limits<std::size_t>::max() - leaf.byteLength) {
      return std::nullopt;
    }
    const auto leafEnd = leaf.offset + leaf.byteLength;
    if (leafEnd <= offset || leaf.offset >= pieceEnd) {
      continue;
    }
    if (leaf.offset < offset || leafEnd > pieceEnd) {
      return std::nullopt;
    }
    leaves.push_back(leaf);
  }
  if (leaves.size() == 1U && leaves.front().offset == offset) {
    const auto& leaf = leaves.front();
    if (leaf.kind == hir::AbiValueKind::Integer) {
      if (leaf.byteLength > std::numeric_limits<unsigned>::max() / 8U) {
        return std::nullopt;
      }
      return llvm::IntegerType::get(
          context, static_cast<unsigned>(leaf.byteLength * 8U));
    }
    if (leaf.kind == hir::AbiValueKind::Pointer &&
        leaf.byteLength == sizeof(void *)) {
      return llvm::PointerType::get(context, 0U);
    }
  }
  if (leaves.empty() ||
      byteLength > std::numeric_limits<unsigned>::max() / 8U) {
    return std::nullopt;
  }
  return llvm::IntegerType::get(context, static_cast<unsigned>(byteLength * 8U));
}

std::optional<llvm::Type *> ssePieceType(
    llvm::LLVMContext& context, const CAbiClassification& classification,
    std::size_t offset, std::size_t byteLength) {
  if (offset > std::numeric_limits<std::size_t>::max() - byteLength) {
    return std::nullopt;
  }
  const auto pieceEnd = offset + byteLength;
  std::vector<CAbiFloatLeaf> leaves;
  for (const auto& leaf : classification.floatLeaves) {
    if (leaf.offset >= offset &&
        leaf.offset <= std::numeric_limits<std::size_t>::max() - leaf.byteLength &&
        leaf.offset + leaf.byteLength <= pieceEnd) {
      leaves.push_back(leaf);
    }
  }
  if (leaves.empty()) {
    return std::nullopt;
  }
  std::sort(leaves.begin(), leaves.end(),
            [](const CAbiFloatLeaf& left, const CAbiFloatLeaf& right) {
              return left.offset < right.offset;
            });
  const auto elementByteLength = leaves.front().byteLength;
  std::size_t cursor = offset;
  for (const auto& leaf : leaves) {
    if (leaf.byteLength != elementByteLength || leaf.offset != cursor) {
      return std::nullopt;
    }
    cursor += leaf.byteLength;
  }
  // A final SSE eightbyte may end in aggregate tail padding.  The leaves must
  // still begin at the piece offset and remain contiguous; only the suffix is
  // allowed to be padding.
  if (cursor > pieceEnd) {
    return std::nullopt;
  }
  llvm::Type *elementType = nullptr;
  switch (elementByteLength) {
  case 4:
    elementType = llvm::Type::getFloatTy(context);
    break;
  case 8:
    elementType = llvm::Type::getDoubleTy(context);
    break;
  default:
    return std::nullopt;
  }
  if (leaves.size() == 1U) {
    return elementType;
  }
  if (leaves.size() > std::numeric_limits<unsigned>::max()) {
    return std::nullopt;
  }
  return llvm::FixedVectorType::get(elementType,
                                    static_cast<unsigned>(leaves.size()));
}

llvm::Align abiAlignment(const hir::AbiType& type) {
  return llvm::Align(std::max<std::size_t>(type.alignment, 1U));
}

} // namespace

llvm::Type *LlvmEmitter::cAbiPhysicalTypeFor(const hir::AbiType& type) {
  if (type.kind == hir::AbiValueKind::Aggregate && type.byteLength <= 16U) {
    const auto plan = cAbiDirectPlanFor(type);
    return plan ? plan->physicalType : nullptr;
  }
  return abiTypeFor(type);
}

std::optional<LlvmEmitter::CAbiDirectPlan>
LlvmEmitter::cAbiDirectPlanFor(const hir::AbiType& type) {
  if (type.kind != hir::AbiValueKind::Aggregate || type.byteLength == 0U ||
      type.byteLength > 16U || type.elementCount != 1U) {
    return std::nullopt;
  }
  if (abiTypeFor(type) == nullptr) {
    return std::nullopt;
  }

  CAbiClassification classification;
  classification.byteLength = type.byteLength;
  if (!classifyCAbiValue(type, 0U, classification)) {
    addDiagnostic("unsupported C aggregate ABI classification");
    return std::nullopt;
  }

  const auto pieceCount = (type.byteLength + 7U) / 8U;
  CAbiDirectPlan plan;
  plan.pieces.reserve(pieceCount);
  std::vector<llvm::Type *> physicalFields;
  physicalFields.reserve(pieceCount);
  for (std::size_t index = 0; index < pieceCount; ++index) {
    const auto offset = index * 8U;
    const auto pieceByteLength = std::min<std::size_t>(8U, type.byteLength - offset);
    llvm::Type *pieceType = nullptr;
    switch (classification.classes[index]) {
    case CAbiEightbyteClass::Integer: {
      const auto integerType =
          integerPieceType(context_, classification, offset, pieceByteLength);
      if (!integerType) {
        addDiagnostic("unsupported C aggregate INTEGER ABI piece");
        return std::nullopt;
      }
      pieceType = *integerType;
      break;
    }
    case CAbiEightbyteClass::Sse: {
      const auto sseType =
          ssePieceType(context_, classification, offset, pieceByteLength);
      if (!sseType) {
        addDiagnostic("unsupported C aggregate SSE ABI piece");
        return std::nullopt;
      }
      pieceType = *sseType;
      break;
    }
    case CAbiEightbyteClass::None:
    case CAbiEightbyteClass::Memory:
      addDiagnostic("unsupported C aggregate ABI classification");
      return std::nullopt;
    }
    plan.pieces.push_back(CAbiDirectPiece{pieceType, offset});
    physicalFields.push_back(pieceType);
  }
  if (plan.pieces.empty()) {
    addDiagnostic("unsupported empty C aggregate ABI classification");
    return std::nullopt;
  }
  plan.physicalType = plan.pieces.size() == 1U
                          ? plan.pieces.front().type
                          : llvm::StructType::get(context_, physicalFields, false);
  return plan;
}

void LlvmEmitter::rememberCAbiDirectAggregatePlans(
    std::string_view name,
    const std::optional<hir::FunctionAbiSignature>& signature) {
  const auto key = std::string(name);
  if (!signature || !signature->isCCompatibility) {
    cAbiDirectAggregateParameters_.erase(key);
    cAbiDirectAggregateReturns_.erase(key);
    return;
  }

  std::vector<std::optional<hir::AbiType>> parameters(
      signature->parameterTypes.size());
  bool hasDirectParameter = false;
  for (std::size_t index = 0; index < signature->parameterTypes.size(); ++index) {
    const auto& parameter = signature->parameterTypes[index];
    if (parameter.kind == hir::AbiValueKind::Aggregate &&
        !isIndirectCAbiAggregate(parameter) && cAbiDirectPlanFor(parameter)) {
      parameters[index] = parameter;
      hasDirectParameter = true;
    }
  }
  if (hasDirectParameter) {
    cAbiDirectAggregateParameters_[key] = std::move(parameters);
  } else {
    cAbiDirectAggregateParameters_.erase(key);
  }

  if (signature->returnTypes.size() == 1U) {
    const auto& returnType = signature->returnTypes.front();
    if (returnType.kind == hir::AbiValueKind::Aggregate &&
        !isIndirectCAbiAggregate(returnType) && cAbiDirectPlanFor(returnType)) {
      cAbiDirectAggregateReturns_[key] = returnType;
      return;
    }
  }
  cAbiDirectAggregateReturns_.erase(key);
}

llvm::Value *LlvmEmitter::packCAbiDirectValue(
    llvm::Value *storage, llvm::Type *storageType,
    const hir::AbiType& logicalType, const CAbiDirectPlan& plan,
    std::string_view name) {
  if (storage == nullptr || storageType == nullptr ||
      logicalType.elementCount != 1U || logicalType.byteLength == 0U ||
      plan.physicalType == nullptr || plan.pieces.empty()) {
    addDiagnostic("invalid direct C aggregate ABI value");
    return nullptr;
  }

  llvm::Value *physical = plan.pieces.size() == 1U
                              ? nullptr
                              : llvm::UndefValue::get(plan.physicalType);
  for (std::size_t index = 0; index < plan.pieces.size(); ++index) {
    const auto& piece = plan.pieces[index];
    const auto storeSize = module_->getDataLayout().getTypeStoreSize(piece.type);
    if (storeSize.isScalable() || piece.offset > logicalType.byteLength ||
        storeSize.getFixedValue() > logicalType.byteLength - piece.offset) {
      addDiagnostic("direct C aggregate ABI piece exceeds logical storage");
      return nullptr;
    }
    auto *loaded = builder_.CreateLoad(
        piece.type, bytePointer(storageType, storage, piece.offset,
                                std::string(name) + ".piece.addr"),
        std::string(name) + ".piece");
    loaded->setAlignment(
        llvm::commonAlignment(abiAlignment(logicalType), piece.offset));
    if (plan.pieces.size() == 1U) {
      physical = loaded;
    } else {
      physical = builder_.CreateInsertValue(
          physical, loaded, {static_cast<unsigned>(index)},
          std::string(name) + ".physical");
    }
  }
  return physical;
}

bool LlvmEmitter::unpackCAbiDirectValue(
    llvm::Value *value, llvm::Value *storage, llvm::Type *storageType,
    const hir::AbiType& logicalType, const CAbiDirectPlan& plan,
    std::string_view name) {
  if (value == nullptr || storage == nullptr || storageType == nullptr ||
      value->getType() != plan.physicalType || logicalType.elementCount != 1U ||
      logicalType.byteLength == 0U || plan.pieces.empty()) {
    addDiagnostic("invalid direct C aggregate ABI result");
    return false;
  }

  for (std::size_t index = 0; index < plan.pieces.size(); ++index) {
    const auto& piece = plan.pieces[index];
    const auto storeSize = module_->getDataLayout().getTypeStoreSize(piece.type);
    if (storeSize.isScalable() || piece.offset > logicalType.byteLength ||
        storeSize.getFixedValue() > logicalType.byteLength - piece.offset) {
      addDiagnostic("direct C aggregate ABI piece exceeds logical storage");
      return false;
    }
    auto *pieceValue = plan.pieces.size() == 1U
                           ? value
                           : builder_.CreateExtractValue(
                                 value, {static_cast<unsigned>(index)},
                                 std::string(name) + ".piece");
    auto *store = builder_.CreateStore(
        pieceValue, bytePointer(storageType, storage, piece.offset,
                                std::string(name) + ".piece.addr"));
    store->setAlignment(
        llvm::commonAlignment(abiAlignment(logicalType), piece.offset));
  }
  return true;
}

std::optional<LlvmEmitter::CAbiMemoryPlan>
LlvmEmitter::cAbiMemoryPlanFor(
    const hir::FunctionAbiSignature& signature) {
  if (!signature.isCCompatibility) {
    return std::nullopt;
  }
  CAbiMemoryPlan plan;
  if (signature.returnTypes.size() == 1U &&
      isIndirectCAbiAggregate(signature.returnTypes.front())) {
    plan.indirectReturn = signature.returnTypes.front();
  }
  plan.indirectParameters.resize(signature.parameterTypes.size());
  bool hasIndirectParameter = false;
  for (std::size_t index = 0; index < signature.parameterTypes.size(); ++index) {
    if (isIndirectCAbiAggregate(signature.parameterTypes[index])) {
      plan.indirectParameters[index] = signature.parameterTypes[index];
      hasIndirectParameter = true;
    }
  }
  if (!plan.indirectReturn && !hasIndirectParameter) {
    return std::nullopt;
  }
  return plan;
}

llvm::FunctionType *LlvmEmitter::cAbiFunctionType(
    const std::optional<hir::FunctionAbiSignature>& signature,
    const std::vector<std::size_t>& parameterByteLengths,
    const std::vector<std::size_t>& returnByteLengths,
    const CAbiMemoryPlan *memoryPlan) {
  if (!signature || !signature->isCCompatibility) {
    if (signature &&
        (signature->parameterTypes.size() != parameterByteLengths.size() ||
         signature->returnTypes.size() != returnByteLengths.size())) {
      addDiagnostic("function ABI signature does not match the core function "
                    "signature");
      return nullptr;
    }
    std::vector<llvm::Type *> parameters;
    parameters.reserve(parameterByteLengths.size());
    for (std::size_t index = 0; index < parameterByteLengths.size(); ++index) {
      auto *parameter = signature
                            ? abiTypeFor(signature->parameterTypes[index])
                            : integerTypeForByteLength(parameterByteLengths[index]);
      if (parameter == nullptr) {
        return nullptr;
      }
      parameters.push_back(parameter);
    }
    auto *returnType = functionReturnType(returnByteLengths, signature);
    return returnType == nullptr ? nullptr
                                : llvm::FunctionType::get(returnType,
                                                          parameters, false);
  }
  if (signature->parameterTypes.size() != parameterByteLengths.size() ||
      signature->returnTypes.size() != returnByteLengths.size()) {
    addDiagnostic("C ABI signature does not match the core function signature");
    return nullptr;
  }

  std::vector<llvm::Type *> parameters;
  parameters.reserve(signature->parameterTypes.size() +
                     (memoryPlan && memoryPlan->indirectReturn ? 1U : 0U));
  if (memoryPlan && memoryPlan->indirectReturn) {
    parameters.push_back(builder_.getPtrTy());
  }
  for (std::size_t index = 0; index < signature->parameterTypes.size(); ++index) {
    if (memoryPlan && memoryPlan->indirectParameters[index]) {
      parameters.push_back(builder_.getPtrTy());
      continue;
    }
    const auto& logicalType = signature->parameterTypes[index];
    if (logicalType.kind == hir::AbiValueKind::Aggregate) {
      const auto plan = cAbiDirectPlanFor(logicalType);
      if (!plan) {
        addDiagnostic("unsupported direct C aggregate parameter ABI");
        return nullptr;
      }
      for (const auto& piece : plan->pieces) {
        parameters.push_back(piece.type);
      }
      continue;
    }
    auto *parameter = cAbiPhysicalTypeFor(logicalType);
    if (parameter == nullptr) {
      return nullptr;
    }
    parameters.push_back(parameter);
  }

  llvm::Type *returnType = nullptr;
  if (memoryPlan && memoryPlan->indirectReturn) {
    returnType = builder_.getVoidTy();
  } else if (signature->returnTypes.empty()) {
    returnType = builder_.getVoidTy();
  } else if (signature->returnTypes.size() == 1U) {
    returnType = cAbiPhysicalTypeFor(signature->returnTypes.front());
  } else {
    addDiagnostic("C compatibility functions cannot use multiple return values");
    return nullptr;
  }
  return returnType == nullptr ? nullptr
                               : llvm::FunctionType::get(returnType,
                                                         parameters, false);
}

void LlvmEmitter::applyCAbiMemoryAttributes(llvm::Function& function,
                                            const CAbiMemoryPlan& plan) {
  unsigned argumentIndex = 0;
  const auto directParameters =
      cAbiDirectAggregateParameters_.find(function.getName().str());
  if (plan.indirectReturn) {
    auto *storageType = abiTypeFor(*plan.indirectReturn);
    if (storageType == nullptr) {
      return;
    }
    function.addParamAttr(
        argumentIndex,
        llvm::Attribute::getWithStructRetType(context_, storageType));
    function.addParamAttr(argumentIndex, llvm::Attribute::NoAlias);
    function.addParamAttr(
        argumentIndex,
        llvm::Attribute::getWithAlignment(
            context_, abiAlignment(*plan.indirectReturn)));
    ++argumentIndex;
  }
  for (std::size_t index = 0; index < plan.indirectParameters.size(); ++index) {
    const auto& parameter = plan.indirectParameters[index];
    if (parameter) {
      auto *storageType = abiTypeFor(*parameter);
      if (storageType == nullptr) {
        return;
      }
      function.addParamAttr(
          argumentIndex,
          llvm::Attribute::getWithByValType(context_, storageType));
      function.addParamAttr(
          argumentIndex,
          llvm::Attribute::getWithAlignment(context_, abiAlignment(*parameter)));
    }
    if (directParameters != cAbiDirectAggregateParameters_.end() &&
        index < directParameters->second.size() &&
        directParameters->second[index]) {
      const auto directPlan = cAbiDirectPlanFor(*directParameters->second[index]);
      if (!directPlan) {
        return;
      }
      argumentIndex += static_cast<unsigned>(directPlan->pieces.size());
    } else {
      ++argumentIndex;
    }
  }
}

void LlvmEmitter::applyCAbiMemoryAttributes(llvm::CallBase& call,
                                            const CAbiMemoryPlan& plan) {
  unsigned argumentIndex = 0;
  const auto* calledFunction = call.getCalledFunction();
  const auto directParameters =
      calledFunction == nullptr
          ? cAbiDirectAggregateParameters_.end()
          : cAbiDirectAggregateParameters_.find(calledFunction->getName().str());
  if (plan.indirectReturn) {
    auto *storageType = abiTypeFor(*plan.indirectReturn);
    if (storageType == nullptr) {
      return;
    }
    call.addParamAttr(argumentIndex,
                      llvm::Attribute::getWithStructRetType(context_,
                                                            storageType));
    call.addParamAttr(argumentIndex, llvm::Attribute::NoAlias);
    call.addParamAttr(
        argumentIndex,
        llvm::Attribute::getWithAlignment(
            context_, abiAlignment(*plan.indirectReturn)));
    ++argumentIndex;
  }
  for (std::size_t index = 0; index < plan.indirectParameters.size(); ++index) {
    const auto& parameter = plan.indirectParameters[index];
    if (parameter) {
      auto *storageType = abiTypeFor(*parameter);
      if (storageType == nullptr) {
        return;
      }
      call.addParamAttr(argumentIndex,
                        llvm::Attribute::getWithByValType(context_, storageType));
      call.addParamAttr(
          argumentIndex,
          llvm::Attribute::getWithAlignment(context_, abiAlignment(*parameter)));
    }
    if (directParameters != cAbiDirectAggregateParameters_.end() &&
        index < directParameters->second.size() &&
        directParameters->second[index]) {
      const auto directPlan = cAbiDirectPlanFor(*directParameters->second[index]);
      if (!directPlan) {
        return;
      }
      argumentIndex += static_cast<unsigned>(directPlan->pieces.size());
    } else {
      ++argumentIndex;
    }
  }
}

const LlvmEmitter::CAbiMemoryPlan *
LlvmEmitter::cAbiMemoryPlan(std::string_view name) const {
  const auto found = cAbiMemoryPlans_.find(std::string(name));
  return found == cAbiMemoryPlans_.end() ? nullptr : &found->second;
}

llvm::Value *LlvmEmitter::emitCAbiMemoryCall(
    std::string_view name,
    const std::vector<std::unique_ptr<hir::Expr>>& callArguments,
    llvm::Function& callee,
    const CAbiMemoryPlan& plan) {
  if (callArguments.size() != plan.indirectParameters.size()) {
    addDiagnostic("function argument count does not match declaration for '" +
                  std::string(name) + "'");
    return nullptr;
  }

  std::vector<llvm::Value *> arguments;
  arguments.reserve(callArguments.size() + (plan.indirectReturn ? 1U : 0U));
  const auto directParameters = cAbiDirectAggregateParameters_.find(std::string(name));
  if (directParameters != cAbiDirectAggregateParameters_.end() &&
      directParameters->second.size() != callArguments.size()) {
    addDiagnostic("C ABI direct aggregate parameter plan does not match function '" +
                  std::string(name) + "'");
    return nullptr;
  }
  llvm::AllocaInst *returnStorage = nullptr;
  llvm::Type *returnStorageType = nullptr;
  if (plan.indirectReturn) {
    returnStorageType = abiTypeFor(*plan.indirectReturn);
    if (returnStorageType == nullptr) {
      return nullptr;
    }
    returnStorage = createFunctionEntryAlloca(returnStorageType,
                                               std::string(name) + ".sret");
    returnStorage->setAlignment(abiAlignment(*plan.indirectReturn));
    arguments.push_back(returnStorage);
  }

  unsigned physicalIndex = plan.indirectReturn ? 1U : 0U;
  for (std::size_t index = 0; index < callArguments.size(); ++index) {
    if (plan.indirectParameters[index]) {
      const auto& aggregateType = *plan.indirectParameters[index];
      auto *storageType = abiTypeFor(aggregateType);
      if (storageType == nullptr) {
        return nullptr;
      }
      auto *value = emitValueForType(*callArguments[index], storageType,
                                     std::string(name) + ".byval");
      if (value == nullptr) {
        return nullptr;
      }
      auto *storage = createFunctionEntryAlloca(storageType,
                                                 std::string(name) + ".byval.tmp");
      storage->setAlignment(abiAlignment(aggregateType));
      auto *store = builder_.CreateStore(value, storage);
      store->setAlignment(abiAlignment(aggregateType));
      arguments.push_back(storage);
    } else {
      const auto directType =
          directParameters == cAbiDirectAggregateParameters_.end()
              ? std::optional<hir::AbiType>{}
              : directParameters->second[index];
      if (directType) {
        const auto directPlan = cAbiDirectPlanFor(*directType);
        auto *storageType = abiTypeFor(*directType);
        if (!directPlan || storageType == nullptr ||
            physicalIndex + directPlan->pieces.size() > callee.arg_size()) {
          addDiagnostic("invalid direct C aggregate argument ABI for function '" +
                        std::string(name) + "'");
          return nullptr;
        }
        auto *value = emitValueForType(*callArguments[index], storageType,
                                       std::string(name) + ".direct.arg");
        if (value == nullptr) {
          return nullptr;
        }
        auto *storage = createFunctionEntryAlloca(
            storageType, std::string(name) + ".direct.arg.tmp");
        storage->setAlignment(abiAlignment(*directType));
        auto *store = builder_.CreateStore(value, storage);
        store->setAlignment(abiAlignment(*directType));
        auto *physical = packCAbiDirectValue(
            storage, storageType, *directType, *directPlan,
            std::string(name) + ".direct.arg");
        if (physical == nullptr) {
          return nullptr;
        }
        for (std::size_t pieceIndex = 0;
             pieceIndex < directPlan->pieces.size(); ++pieceIndex) {
          const auto& piece = directPlan->pieces[pieceIndex];
          if (callee.getFunctionType()->getParamType(
                  physicalIndex + static_cast<unsigned>(pieceIndex)) != piece.type) {
            addDiagnostic("C ABI direct aggregate argument type does not match function '" +
                          std::string(name) + "'");
            return nullptr;
          }
          arguments.push_back(
              directPlan->pieces.size() == 1U
                  ? physical
                  : builder_.CreateExtractValue(
                        physical, {static_cast<unsigned>(pieceIndex)},
                        std::string(name) + ".direct.arg.piece"));
        }
        physicalIndex += static_cast<unsigned>(directPlan->pieces.size());
        continue;
      }
      if (physicalIndex >= callee.arg_size()) {
        addDiagnostic("function argument count does not match declaration for '" +
                      std::string(name) + "'");
        return nullptr;
      }
      auto *parameterType = callee.getFunctionType()->getParamType(physicalIndex);
      auto *value = emitValueForType(*callArguments[index], parameterType,
                                     std::string(name) + ".arg");
      if (value == nullptr) {
        return nullptr;
      }
      arguments.push_back(value);
    }
    ++physicalIndex;
  }
  if (physicalIndex != callee.arg_size()) {
    addDiagnostic("function argument count does not match declaration for '" +
                  std::string(name) + "'");
    return nullptr;
  }

  auto *result = builder_.CreateCall(
      &callee, arguments,
      callee.getReturnType()->isVoidTy() ? "" : std::string(name) + ".ret");
  applyCAbiMemoryAttributes(*result, plan);
  if (returnStorage != nullptr) {
    return builder_.CreateLoad(returnStorageType, returnStorage,
                               std::string(name) + ".aggregate.ret");
  }
  const auto directReturn = cAbiDirectAggregateReturns_.find(std::string(name));
  if (directReturn == cAbiDirectAggregateReturns_.end()) {
    return result;
  }
  const auto returnPlan = cAbiDirectPlanFor(directReturn->second);
  auto *storageType = abiTypeFor(directReturn->second);
  if (!returnPlan || storageType == nullptr ||
      result->getType() != returnPlan->physicalType) {
    addDiagnostic("invalid direct C aggregate return ABI for function '" +
                  std::string(name) + "'");
    return nullptr;
  }
  auto *storage = createFunctionEntryAlloca(
      storageType, std::string(name) + ".direct.ret.tmp");
  storage->setAlignment(abiAlignment(directReturn->second));
  if (!unpackCAbiDirectValue(result, storage, storageType, directReturn->second,
                             *returnPlan,
                             std::string(name) + ".direct.ret")) {
    return nullptr;
  }
  auto *logical = builder_.CreateLoad(storageType, storage,
                                      std::string(name) + ".aggregate.ret");
  logical->setAlignment(abiAlignment(directReturn->second));
  return logical;
}

llvm::Value *LlvmEmitter::emitCAbiDirectCall(
    std::string_view name,
    const std::vector<std::unique_ptr<hir::Expr>>& callArguments,
    llvm::Function& callee) {
  const auto directParameters = cAbiDirectAggregateParameters_.find(std::string(name));
  if (directParameters != cAbiDirectAggregateParameters_.end() &&
      directParameters->second.size() != callArguments.size()) {
    addDiagnostic("C ABI direct aggregate parameter plan does not match function '" +
                  std::string(name) + "'");
    return nullptr;
  }

  std::vector<llvm::Value *> arguments;
  unsigned physicalIndex = 0;
  for (std::size_t index = 0; index < callArguments.size(); ++index) {
    const auto directType =
        directParameters == cAbiDirectAggregateParameters_.end()
            ? std::optional<hir::AbiType>{}
            : directParameters->second[index];
    if (directType) {
      const auto directPlan = cAbiDirectPlanFor(*directType);
      auto *storageType = abiTypeFor(*directType);
      if (!directPlan || storageType == nullptr ||
          physicalIndex + directPlan->pieces.size() > callee.arg_size()) {
        addDiagnostic("invalid direct C aggregate argument ABI for function '" +
                      std::string(name) + "'");
        return nullptr;
      }
      auto *value = emitValueForType(*callArguments[index], storageType,
                                     std::string(name) + ".direct.arg");
      if (value == nullptr) {
        return nullptr;
      }
      auto *storage = createFunctionEntryAlloca(
          storageType, std::string(name) + ".direct.arg.tmp");
      storage->setAlignment(abiAlignment(*directType));
      auto *store = builder_.CreateStore(value, storage);
      store->setAlignment(abiAlignment(*directType));
      auto *physical = packCAbiDirectValue(
          storage, storageType, *directType, *directPlan,
          std::string(name) + ".direct.arg");
      if (physical == nullptr) {
        return nullptr;
      }
      for (std::size_t pieceIndex = 0;
           pieceIndex < directPlan->pieces.size(); ++pieceIndex) {
        const auto& piece = directPlan->pieces[pieceIndex];
        if (callee.getFunctionType()->getParamType(
                physicalIndex + static_cast<unsigned>(pieceIndex)) != piece.type) {
          addDiagnostic("C ABI direct aggregate argument type does not match function '" +
                        std::string(name) + "'");
          return nullptr;
        }
        arguments.push_back(
            directPlan->pieces.size() == 1U
                ? physical
                : builder_.CreateExtractValue(
                      physical, {static_cast<unsigned>(pieceIndex)},
                      std::string(name) + ".direct.arg.piece"));
      }
      physicalIndex += static_cast<unsigned>(directPlan->pieces.size());
      continue;
    }

    if (physicalIndex >= callee.arg_size()) {
      addDiagnostic("function argument count does not match declaration for '" +
                    std::string(name) + "'");
      return nullptr;
    }
    auto *parameterType = callee.getFunctionType()->getParamType(physicalIndex++);
    auto *value = emitValueForType(*callArguments[index], parameterType,
                                   std::string(name) + ".arg");
    if (value == nullptr) {
      return nullptr;
    }
    arguments.push_back(value);
  }
  if (physicalIndex != callee.arg_size()) {
    addDiagnostic("function argument count does not match declaration for '" +
                  std::string(name) + "'");
    return nullptr;
  }

  auto *result = builder_.CreateCall(
      &callee, arguments,
      callee.getReturnType()->isVoidTy() ? "" : std::string(name) + ".ret");
  const auto directReturn = cAbiDirectAggregateReturns_.find(std::string(name));
  if (directReturn == cAbiDirectAggregateReturns_.end()) {
    return result;
  }

  const auto returnPlan = cAbiDirectPlanFor(directReturn->second);
  auto *storageType = abiTypeFor(directReturn->second);
  if (!returnPlan || storageType == nullptr ||
      result->getType() != returnPlan->physicalType) {
    addDiagnostic("invalid direct C aggregate return ABI for function '" +
                  std::string(name) + "'");
    return nullptr;
  }
  auto *storage = createFunctionEntryAlloca(
      storageType, std::string(name) + ".direct.ret.tmp");
  storage->setAlignment(abiAlignment(directReturn->second));
  if (!unpackCAbiDirectValue(result, storage, storageType, directReturn->second,
                             *returnPlan,
                             std::string(name) + ".direct.ret")) {
    return nullptr;
  }
  auto *logical = builder_.CreateLoad(storageType, storage,
                                      std::string(name) + ".aggregate.ret");
  logical->setAlignment(abiAlignment(directReturn->second));
  return logical;
}

} // namespace hitsimple::codegen
