#include "LlvmEmitter.h"

#include "hitsimple/codegen/LlvmCompatibility.h"
#include "hitsimple/literal/Literal.h"

#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace hitsimple::codegen {
namespace {

std::optional<std::int64_t> constantSignedInteger(const hir::Expr &expression) {
  if (const auto *integer =
          dynamic_cast<const hir::IntegerLiteral *>(&expression)) {
    const auto value = literal::parseUnsignedIntegerLiteral(integer->value);
    if (!value || *value > static_cast<std::uint64_t>(
                               std::numeric_limits<std::int64_t>::max())) {
      return std::nullopt;
    }
    return static_cast<std::int64_t>(*value);
  }

  if (const auto *unary = dynamic_cast<const hir::UnaryExpr *>(&expression)) {
    if (unary->op == "-") {
      if (const auto *integer =
              dynamic_cast<const hir::IntegerLiteral *>(unary->operand.get())) {
        const auto value = literal::parseUnsignedIntegerLiteral(integer->value);
        if (value && *value == static_cast<std::uint64_t>(
                                   std::numeric_limits<std::int64_t>::max()) +
                                   1U) {
          return std::numeric_limits<std::int64_t>::min();
        }
      }
    }
    const auto value = constantSignedInteger(*unary->operand);
    if (!value) {
      return std::nullopt;
    }
    if (unary->op == "-") {
      if (*value == std::numeric_limits<std::int64_t>::min()) {
        return std::nullopt;
      }
      return -*value;
    }
    if (unary->op == "+") {
      return *value;
    }
  }

  if (const auto *cast =
          dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
    const auto value = constantSignedInteger(*cast->operand);
    if (!value || cast->byteLength == 0 || cast->byteLength > 8) {
      return std::nullopt;
    }
    if (cast->byteLength == 8) {
      return cast->isSigned ? value : (*value >= 0 ? value : std::nullopt);
    }
    const auto bits = cast->byteLength * 8U;
    const auto mask = (std::uint64_t{1} << bits) - 1U;
    const auto truncated = static_cast<std::uint64_t>(*value) & mask;
    if (!cast->isSigned) {
      return static_cast<std::int64_t>(truncated);
    }
    const auto signBit = std::uint64_t{1} << (bits - 1U);
    return static_cast<std::int64_t>((truncated ^ signBit) - signBit);
  }
  if (const auto *view =
          dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return constantSignedInteger(*view->operand);
  }

  return std::nullopt;
}

std::optional<std::uint64_t>
constantUnsignedInteger(const hir::Expr &expression) {
  if (const auto *integer =
          dynamic_cast<const hir::IntegerLiteral *>(&expression)) {
    return literal::parseUnsignedIntegerLiteral(integer->value);
  }
  if (const auto *unary = dynamic_cast<const hir::UnaryExpr *>(&expression)) {
    if (unary->op == "+") {
      return constantUnsignedInteger(*unary->operand);
    }
  }
  if (const auto *unsignedExpr =
          dynamic_cast<const hir::UnsignedExpr *>(&expression)) {
    return constantUnsignedInteger(*unsignedExpr->operand);
  }
  if (const auto *cast =
          dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
    if (cast->byteLength == 0 || cast->byteLength > 8) {
      return std::nullopt;
    }
    auto value = constantUnsignedInteger(*cast->operand);
    if (!value) {
      const auto signedValue = constantSignedInteger(*cast->operand);
      if (!signedValue) {
        return std::nullopt;
      }
      value = static_cast<std::uint64_t>(*signedValue);
    }
    if (cast->byteLength == 8) {
      return value;
    }
    return *value & ((std::uint64_t{1} << (cast->byteLength * 8U)) - 1U);
  }
  if (const auto *view =
          dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return constantUnsignedInteger(*view->operand);
  }
  return std::nullopt;
}

std::optional<std::int64_t> addSignedIntegers(std::int64_t left,
                                              std::int64_t right) {
  if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
      (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)) {
    return std::nullopt;
  }
  return left + right;
}

std::optional<std::uint64_t> multiplyUnsignedIntegers(std::uint64_t left,
                                                       std::uint64_t right) {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return std::nullopt;
  }
  return left * right;
}

std::optional<std::int64_t> signedMinimumForByteLength(std::size_t byteLength) {
  if (byteLength == 8) {
    return std::numeric_limits<std::int64_t>::min();
  }
  if (byteLength != 1 && byteLength != 2 && byteLength != 4) {
    return std::nullopt;
  }
  return -(std::int64_t{1} << (byteLength * 8U - 1U));
}

bool hasCAbiAggregateByValue(
    const std::optional<hir::FunctionAbiSignature> &signature) {
  if (!signature || !signature->isCCompatibility) {
    return false;
  }
  const auto hasAggregate = [](const std::vector<hir::AbiType> &types) {
    for (const auto &type : types) {
      if (type.kind == hir::AbiValueKind::Aggregate) {
        return true;
      }
    }
    return false;
  };
  return hasAggregate(signature->parameterTypes) ||
         hasAggregate(signature->returnTypes);
}

bool isX86_64SysVElfTarget(std::string_view targetTriple) {
  const auto target = parseTargetTriple(targetTriple);
  return target.getArch() == llvm::Triple::x86_64 &&
         target.getObjectFormat() == llvm::Triple::ELF && !target.isX32();
}

template <typename ModuleType>
void useLegacyDebugInfoFormatIfAvailable(ModuleType &module) {
  if constexpr (requires(ModuleType &value) {
                  value.setIsNewDbgInfoFormat(false);
                }) {
    module.setIsNewDbgInfoFormat(false);
  }
}

} // namespace

DebugInfoFormat debugInfoFormatForTargetTriple(std::string_view targetTriple) {
  return parseTargetTriple(targetTriple).isOSWindows()
             ? DebugInfoFormat::CodeView
             : DebugInfoFormat::Dwarf;
}

LlvmEmitter::LlvmEmitter(std::string moduleName, CodegenOptions options)
    : moduleName_(std::move(moduleName)), options_(options),
      module_(std::make_unique<llvm::Module>(moduleName_, context_)),
      builder_(context_) {
  const auto targetTriple = options_.targetTriple.empty()
                                ? llvm::sys::getDefaultTargetTriple()
                                : options_.targetTriple;
  setModuleTargetTriple(*module_, targetTriple);
  if (options_.emitDebugInfo) {
    // Clang 18 accepts intrinsic-form debug declarations. LLVM versions that
    // offer the transitional conversion API use it before any IR is emitted.
    useLegacyDebugInfoFormatIfAvailable(*module_);
    initializeDebugInfo();
  }
}

EmitResult LlvmEmitter::emit(const hir::TranslationUnit &unit) {
  validateSafety(unit);
  if (!diagnostics_.empty()) {
    return EmitResult{"", std::move(diagnostics_)};
  }

  const auto targetTriple = moduleTargetTriple(*module_);
  if (!isX86_64SysVElfTarget(targetTriple)) {
    const auto rejectAggregateSignature =
        [&](std::string_view name,
            const std::optional<hir::FunctionAbiSignature> &signature) {
          if (!hasCAbiAggregateByValue(signature)) {
            return;
          }
          addDiagnostic(
              "C aggregate by-value ABI is only supported for x86_64 SysV "
              "ELF targets; target '" +
              targetTriple + "' is not supported for function '" +
              std::string(name) + "'");
        };
    for (const auto &function : unit.externFunctions) {
      rejectAggregateSignature(function.name, function.abiSignature);
    }
    for (const auto &function : unit.functions) {
      rejectAggregateSignature(function->name, function->abiSignature);
    }
    if (!diagnostics_.empty()) {
      return EmitResult{"", std::move(diagnostics_)};
    }
  }

  if (llvm::InitializeNativeTarget() ||
      llvm::InitializeNativeTargetAsmPrinter()) {
    addDiagnostic("cannot initialize LLVM native target");
    return EmitResult{"", std::move(diagnostics_)};
  }
  std::string targetError;
  const auto *target = resolveTarget(targetTriple, targetError);
  if (target == nullptr) {
    addDiagnostic("cannot resolve LLVM target '" + targetTriple +
                  "': " + targetError);
    return EmitResult{"", std::move(diagnostics_)};
  }
  llvm::TargetOptions targetOptions;
  std::unique_ptr<llvm::TargetMachine> targetMachine(
      createGenericTargetMachine(*target, targetTriple, targetOptions));
  if (!targetMachine) {
    addDiagnostic("cannot create LLVM target machine for '" + targetTriple +
                  "'");
    return EmitResult{"", std::move(diagnostics_)};
  }
  module_->setDataLayout(targetMachine->createDataLayout());

  for (const auto &global : unit.globals) {
    emit(global);
  }

  for (const auto &function : unit.externFunctions) {
    declare(function);
  }

  for (const auto &function : unit.functions) {
    (void)declare(*function);
  }

  if ((unit.globalInit && !unit.globalInit->statements.empty()) ||
      (hasRuntimeSafetyChecks() && !internalGlobals_.empty())) {
    emitGlobalInit(unit.globalInit.get());
  }

  for (const auto &function : unit.functions) {
    emit(*function);
  }

  if (!diagnostics_.empty()) {
    return EmitResult{"", std::move(diagnostics_)};
  }

  std::string verifierMessage;
  llvm::raw_string_ostream verifierStream(verifierMessage);
  if (llvm::verifyModule(*module_, &verifierStream)) {
    addDiagnostic("invalid LLVM IR: " + verifierStream.str());
    return EmitResult{"", std::move(diagnostics_)};
  }

  std::string llvmIr;
  finalizeDebugInfo();
  llvm::raw_string_ostream out(llvmIr);
  module_->print(out, nullptr);
  return EmitResult{out.str(), {}};
}

void LlvmEmitter::addDiagnostic(std::string diagnostic) {
  auto emitted =
      diagnostic::Diagnostic::error(diagnostic::Stage::Codegen, std::move(diagnostic));
  emitted.range = currentDiagnosticRange_;
  diagnostics_.push_back(std::move(emitted));
}

bool LlvmEmitter::hasStaticSafetyChecks() const {
  return options_.safetyMode == SafetyMode::StaticChecked ||
         options_.safetyMode == SafetyMode::Checked;
}

bool LlvmEmitter::hasRuntimeSafetyChecks() const {
  return options_.safetyMode == SafetyMode::Checked;
}

LlvmEmitter::SourceRangeScope::SourceRangeScope(
    LlvmEmitter& emitter, const std::optional<diagnostic::SourceRange>& range)
    : emitter_(emitter), previous_(emitter.currentDiagnosticRange_) {
  if (range) {
    emitter_.currentDiagnosticRange_ = range;
  }
}

LlvmEmitter::SourceRangeScope::~SourceRangeScope() {
  emitter_.currentDiagnosticRange_ = std::move(previous_);
}

LlvmEmitter::RuntimeSourceLocationSuppressionScope::
    RuntimeSourceLocationSuppressionScope(LlvmEmitter& emitter, bool suppress)
    : emitter_(emitter), previous_(emitter.runtimeSourceLocationEmissionSuppressed_) {
  emitter_.runtimeSourceLocationEmissionSuppressed_ = previous_ || suppress;
}

LlvmEmitter::RuntimeSourceLocationSuppressionScope::
    ~RuntimeSourceLocationSuppressionScope() {
  emitter_.runtimeSourceLocationEmissionSuppressed_ = previous_;
}

LlvmEmitter::DebugLocationScope::DebugLocationScope(
    LlvmEmitter &emitter, const std::optional<diagnostic::SourceRange> &range)
    : sourceRangeScope_(emitter, range), emitter_(emitter),
      previous_(emitter.builder_.getCurrentDebugLocation()) {
  if (auto *location = emitter_.debugLocation(range)) {
    emitter_.builder_.SetCurrentDebugLocation(location);
  }
}

LlvmEmitter::DebugLocationScope::~DebugLocationScope() {
  emitter_.builder_.SetCurrentDebugLocation(previous_);
}

void LlvmEmitter::initializeDebugInfo() {
  const std::filesystem::path sourcePath(moduleName_);
  const auto fileName = sourcePath.filename().string();
  const auto directory = sourcePath.has_parent_path()
                             ? sourcePath.parent_path().string()
                             : std::string(".");
  debugBuilder_ = std::make_unique<llvm::DIBuilder>(*module_);
  debugFile_ = debugBuilder_->createFile(
      fileName.empty() ? moduleName_ : fileName, directory);
  debugCompileUnit_ = debugBuilder_->createCompileUnit(
      llvm::dwarf::DW_LANG_C, debugFile_, "hsc", false, "", 0);
  module_->addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                         llvm::DEBUG_METADATA_VERSION);
  if (debugInfoFormatForTargetTriple(moduleTargetTriple(*module_)) ==
      DebugInfoFormat::CodeView) {
    module_->addModuleFlag(llvm::Module::Warning, "CodeView", 1);
  } else {
    module_->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 5);
  }
}

void LlvmEmitter::finalizeDebugInfo() {
  if (debugBuilder_) {
    debugBuilder_->finalize();
  }
}

void LlvmEmitter::beginDebugFunction(const hir::Function &function,
                                     llvm::Function &llvmFunction) {
  if (!debugBuilder_ || !debugFile_) {
    return;
  }
  const auto line = static_cast<unsigned>(
      function.range ? std::max<std::size_t>(function.range->begin.line, 1U)
                     : 1U);
  auto *type = debugBuilder_->createSubroutineType(
      debugBuilder_->getOrCreateTypeArray({}));
  auto *subprogram = debugBuilder_->createFunction(
      debugFile_, function.name, llvmFunction.getName(), debugFile_, line, type,
      line, llvm::DINode::FlagPrototyped, llvm::DISubprogram::SPFlagDefinition);
  llvmFunction.setSubprogram(subprogram);
  debugScope_ = subprogram;
}

llvm::DILocation *LlvmEmitter::debugLocation(
    const std::optional<diagnostic::SourceRange> &range) {
  if (!debugBuilder_ || !debugScope_) {
    return nullptr;
  }
  const auto line = static_cast<unsigned>(
      range ? std::max<std::size_t>(range->begin.line, 1U) : 1U);
  const auto column = static_cast<unsigned>(
      range ? std::max<std::size_t>(range->begin.column, 1U) : 1U);
  return llvm::DILocation::get(context_, line, column, debugScope_);
}

void LlvmEmitter::declareDebugVariable(
    std::string_view name, const std::optional<diagnostic::SourceRange> &range,
    std::size_t byteLength, llvm::Value *storage, unsigned argumentIndex) {
  if (!debugBuilder_ || !debugFile_ || !debugScope_ || storage == nullptr) {
    return;
  }
  const auto line = static_cast<unsigned>(
      range ? std::max<std::size_t>(range->begin.line, 1U) : 1U);
  auto *byteType = debugBuilder_->createBasicType(
      "u8", 8, llvm::dwarf::DW_ATE_unsigned_char);
  llvm::Metadata *subranges[] = {debugBuilder_->getOrCreateSubrange(
      0, static_cast<std::int64_t>(byteLength))};
  auto *type = debugBuilder_->createArrayType(
      byteLength * 8U, 8, byteType, debugBuilder_->getOrCreateArray(subranges));
  llvm::DILocalVariable *variable = nullptr;
  if (argumentIndex == 0) {
    variable = debugBuilder_->createAutoVariable(debugScope_, name, debugFile_,
                                                 line, type, true);
  } else {
    variable = debugBuilder_->createParameterVariable(
        debugScope_, name, argumentIndex, debugFile_, line, type, true);
  }
  debugBuilder_->insertDeclare(storage, variable,
                               debugBuilder_->createExpression(),
                               debugLocation(range), builder_.GetInsertBlock());
}

std::optional<LlvmEmitter::StaticAddressRange>
LlvmEmitter::staticAddressRange(const hir::Expr &expression) const {
  const auto fact = staticAddressFact(expression);
  return fact ? fact->range : std::nullopt;
}

std::optional<LlvmEmitter::StaticAddressRange>
LlvmEmitter::staticViewRange(const hir::Expr &expression) const {
  if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
    const auto offset = static_cast<std::int64_t>(variable->offset);
    return StaticAddressRange{variable->bindingName, offset, offset,
                              static_cast<std::uint64_t>(
                                  variable->offset + variable->byteLength)};
  }
  if (const auto *view = dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return staticViewRange(*view->operand);
  }
  if (const auto *deref = dynamic_cast<const hir::DerefExpr *>(&expression)) {
    return staticAddressRange(*deref->address);
  }
  return staticAddressRange(expression);
}

std::optional<LlvmEmitter::StaticAddressRange>
LlvmEmitter::staticMemoryOperandRange(const hir::Expr &expression) const {
  const auto isRawAddress = [&expression] {
    if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
      return variable->templateName == "addr";
    }
    if (const auto *view = dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
      return view->templateName == "addr";
    }
    if (const auto *call = dynamic_cast<const hir::CallExpr *>(&expression)) {
      return call->templateName == "addr";
    }
    return dynamic_cast<const hir::AddressOfExpr *>(&expression) != nullptr;
  };
  return isRawAddress() ? staticAddressRange(expression)
                        : staticViewRange(expression);
}

bool LlvmEmitter::targetsRegisteredInternalObject(
    const hir::Expr &expression) const {
  if (const auto *address =
          dynamic_cast<const hir::AddressOfExpr *>(&expression)) {
    if (locals_.find(address->bindingName) != locals_.end()) {
      return true;
    }

    const auto global = globals_.find(address->bindingName);
    if (global == globals_.end()) {
      return false;
    }
    const auto *storage =
        llvm::dyn_cast<llvm::GlobalVariable>(global->second.storage);
    return storage != nullptr && !storage->isDeclaration();
  }

  const auto *binary = dynamic_cast<const hir::BinaryExpr *>(&expression);
  if (binary != nullptr &&
      binary->operationKind == hir::StandardOperationKind::AddressOffset) {
    return targetsRegisteredInternalObject(*binary->left);
  }
  if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
    const auto fact = staticAddressFact(*variable);
    return fact && fact->origin == StaticAddressOrigin::NonDynamicObject &&
           fact->range.has_value();
  }
  return false;
}

bool LlvmEmitter::hasKnownStaticAddressRange(const hir::Expr &expression,
                                             std::size_t byteLength) const {
  const auto range = staticAddressRange(expression);
  if (!range || range->offset < range->lowerBound ||
      !targetsRegisteredInternalObject(expression)) {
    return false;
  }
  return static_cast<std::uint64_t>(range->offset) + byteLength <=
         range->upperBound;
}

void LlvmEmitter::resetStaticSafetyState() {
  staticIntegerValues_.clear();
  staticUnsignedIntegerValues_.clear();
  staticAddressFacts_.clear();
  staticDynamicObjectStates_.clear();
  staticGlobalBindings_.clear();
  nextStaticDynamicObjectId_ = 0;
}

LlvmEmitter::StaticSafetyState LlvmEmitter::staticSafetyState() const {
  return StaticSafetyState{staticIntegerValues_, staticUnsignedIntegerValues_,
                           staticAddressFacts_, staticDynamicObjectStates_,
                           nextStaticDynamicObjectId_};
}

void LlvmEmitter::restoreStaticSafetyState(const StaticSafetyState &state) {
  staticIntegerValues_ = state.integerValues;
  staticUnsignedIntegerValues_ = state.unsignedIntegerValues;
  staticAddressFacts_ = state.addressFacts;
  staticDynamicObjectStates_ = state.dynamicObjectStates;
  // Object identifiers are allocation identities. Do not reuse one after
  // restoring a control-flow snapshot that has already observed later IDs.
  nextStaticDynamicObjectId_ =
      std::max(nextStaticDynamicObjectId_, state.nextDynamicObjectId);
}

void LlvmEmitter::mergeStaticSafetyStates(const StaticSafetyState &left,
                                          const StaticSafetyState &right) {
  const auto retainCommonFacts = [](const auto &leftFacts,
                                    const auto &rightFacts) {
    using FactMap = std::decay_t<decltype(leftFacts)>;
    FactMap result;
    for (const auto &[bindingName, leftValue] : leftFacts) {
      const auto rightValue = rightFacts.find(bindingName);
      if (rightValue != rightFacts.end() && leftValue && rightValue->second &&
          *leftValue == *rightValue->second) {
        result.emplace(bindingName, leftValue);
      }
    }
    return result;
  };

  staticIntegerValues_ =
      retainCommonFacts(left.integerValues, right.integerValues);
  staticUnsignedIntegerValues_ =
      retainCommonFacts(left.unsignedIntegerValues, right.unsignedIntegerValues);
  staticAddressFacts_ =
      retainCommonFacts(left.addressFacts, right.addressFacts);

  staticDynamicObjectStates_.clear();
  for (const auto &[bindingName, fact] : staticAddressFacts_) {
    (void)bindingName;
    if (!fact || fact->origin != StaticAddressOrigin::DynamicObject) {
      continue;
    }
    const auto leftState = left.dynamicObjectStates.find(fact->dynamicObjectId);
    const auto rightState =
        right.dynamicObjectStates.find(fact->dynamicObjectId);
    if (leftState == left.dynamicObjectStates.end() ||
        rightState == right.dynamicObjectStates.end()) {
      continue;
    }
    staticDynamicObjectStates_[fact->dynamicObjectId] =
        leftState->second == rightState->second ? leftState->second
                                                : StaticDynamicObjectState::Unknown;
  }
  nextStaticDynamicObjectId_ = std::max(
      {nextStaticDynamicObjectId_, left.nextDynamicObjectId,
       right.nextDynamicObjectId});
}

void LlvmEmitter::invalidateStaticBinding(std::string_view bindingName) {
  const auto key = std::string(bindingName);
  staticIntegerValues_[key] = std::nullopt;
  staticUnsignedIntegerValues_[key] = std::nullopt;
  staticAddressFacts_[key] = std::nullopt;
}

void LlvmEmitter::invalidateStaticFactsOverlapping(
    const std::optional<StaticAddressRange> &range,
    std::optional<std::uint64_t> byteLength) {
  if (!range || (byteLength && *byteLength == 0)) {
    return;
  }

  // Static facts are keyed by definition binding. Every range originating at
  // this binding overlaps it, even when the write is through an address or a
  // standard-library memory operation rather than a direct assignment.
  invalidateStaticBinding(range->bindingName);
}

void LlvmEmitter::invalidateStaticGlobalFacts() {
  for (const auto &bindingName : staticGlobalBindings_) {
    invalidateStaticBinding(bindingName);
  }
}

std::optional<std::int64_t>
LlvmEmitter::staticSignedInteger(const hir::Expr &expression) const {
  if (const auto value = constantSignedInteger(expression)) {
    return value;
  }
  if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
    const auto found = staticIntegerValues_.find(variable->bindingName);
    return found == staticIntegerValues_.end() ? std::nullopt : found->second;
  }
  if (const auto *unary = dynamic_cast<const hir::UnaryExpr *>(&expression)) {
    const auto value = staticSignedInteger(*unary->operand);
    if (!value) {
      return std::nullopt;
    }
    if (unary->op == "+") {
      return value;
    }
    if (unary->op == "-" &&
        *value != std::numeric_limits<std::int64_t>::min()) {
      return -*value;
    }
    return std::nullopt;
  }
  if (const auto *cast =
          dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
    if (cast->byteLength == 0 || cast->byteLength > 8) {
      return std::nullopt;
    }
    const auto value = staticSignedInteger(*cast->operand);
    if (!value) {
      return std::nullopt;
    }
    if (cast->byteLength == 8) {
      return cast->isSigned ? value : (*value >= 0 ? value : std::nullopt);
    }
    const auto bits = cast->byteLength * 8U;
    const auto mask = (std::uint64_t{1} << bits) - 1U;
    const auto truncated = static_cast<std::uint64_t>(*value) & mask;
    if (!cast->isSigned) {
      return static_cast<std::int64_t>(truncated);
    }
    const auto signBit = std::uint64_t{1} << (bits - 1U);
    return static_cast<std::int64_t>((truncated ^ signBit) - signBit);
  }
  if (const auto *view =
          dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return staticSignedInteger(*view->operand);
  }
  return std::nullopt;
}

std::optional<std::uint64_t>
LlvmEmitter::staticUnsignedInteger(const hir::Expr &expression) const {
  if (const auto value = constantUnsignedInteger(expression)) {
    return value;
  }
  if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
    const auto found = staticUnsignedIntegerValues_.find(variable->bindingName);
    return found == staticUnsignedIntegerValues_.end() ? std::nullopt
                                                        : found->second;
  }
  if (const auto *unsignedExpr =
          dynamic_cast<const hir::UnsignedExpr *>(&expression)) {
    return staticUnsignedInteger(*unsignedExpr->operand);
  }
  if (const auto *cast =
          dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
    if (cast->byteLength == 0 || cast->byteLength > 8) {
      return std::nullopt;
    }
    auto value = staticUnsignedInteger(*cast->operand);
    if (!value) {
      const auto signedValue = staticSignedInteger(*cast->operand);
      if (!signedValue) {
        return std::nullopt;
      }
      value = static_cast<std::uint64_t>(*signedValue);
    }
    if (cast->byteLength == 8) {
      return value;
    }
    return *value & ((std::uint64_t{1} << (cast->byteLength * 8U)) - 1U);
  }
  if (const auto *view =
          dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return staticUnsignedInteger(*view->operand);
  }
  return std::nullopt;
}

std::optional<bool>
LlvmEmitter::staticBooleanValue(const hir::Expr &expression) const {
  if (const auto *test =
          dynamic_cast<const hir::BooleanTestExpr *>(&expression)) {
    return staticBooleanValue(*test->operand);
  }
  if (const auto *unary = dynamic_cast<const hir::UnaryExpr *>(&expression)) {
    if (unary->op == "!") {
      const auto value = staticBooleanValue(*unary->operand);
      return value ? std::optional<bool>{!*value} : std::nullopt;
    }
  }
  if (const auto value = staticSignedInteger(expression)) {
    return *value != 0;
  }
  if (const auto value = staticUnsignedInteger(expression)) {
    return *value != 0;
  }
  return std::nullopt;
}

std::optional<LlvmEmitter::StaticAddressFact>
LlvmEmitter::staticAddressFact(const hir::Expr &expression) const {
  if (const auto *unsignedExpr =
          dynamic_cast<const hir::UnsignedExpr *>(&expression)) {
    return staticAddressFact(*unsignedExpr->operand);
  }
  if (const auto *cast =
          dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
    const auto pointerBytes = sizeof(void *);
    if (cast->byteLength != pointerBytes ||
        cast->operand->result.lengthKind != hir::ViewLengthKind::Static ||
        cast->operand->result.staticByteLength != pointerBytes) {
      return std::nullopt;
    }
    return staticAddressFact(*cast->operand);
  }
  if (const auto *view =
          dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return staticAddressFact(*view->operand);
  }
  if (const auto *address =
          dynamic_cast<const hir::AddressOfExpr *>(&expression)) {
    const auto offset = static_cast<std::int64_t>(address->offset);
    return StaticAddressFact{
        StaticAddressOrigin::NonDynamicObject,
        0,
        address->offset == 0,
        StaticAddressRange{address->bindingName, offset, offset,
                           static_cast<std::uint64_t>(address->offset +
                                                      address->targetByteLength)}};
  }
  if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
    const auto found = staticAddressFacts_.find(variable->bindingName);
    return found == staticAddressFacts_.end() ? std::nullopt : found->second;
  }
  if (const auto *binary = dynamic_cast<const hir::BinaryExpr *>(&expression)) {
    const bool addressOffset =
        binary->operationKind == hir::StandardOperationKind::AddressOffset;
    if (!addressOffset && binary->operation != hir::BinaryOperator::Add &&
        binary->operation != hir::BinaryOperator::Subtract) {
      return std::nullopt;
    }
    auto base = staticAddressFact(*binary->left);
    const auto offset = staticSignedInteger(*binary->right);
    if (base && offset) {
      auto signedOffset = *offset;
      if (!addressOffset && binary->operation == hir::BinaryOperator::Subtract) {
        if (signedOffset == std::numeric_limits<std::int64_t>::min()) {
          base->range.reset();
          return base;
        }
        signedOffset = -signedOffset;
      }
      base->isBaseAddress = false;
      if (base->range) {
        const auto adjusted =
            addSignedIntegers(base->range->offset, signedOffset);
        if (adjusted) {
          base->range->offset = *adjusted;
        } else {
          base->range.reset();
        }
      }
    } else if (base) {
      base->range.reset();
    }
    return base;
  }
  const auto value = staticSignedInteger(expression);
  if (value && *value == 0) {
    return StaticAddressFact{StaticAddressOrigin::Null, 0, false, std::nullopt};
  }
  return std::nullopt;
}

void LlvmEmitter::validateStaticDynamicBase(const hir::Expr &expression,
                                            std::string_view operation) {
  const auto fact = staticAddressFact(expression);
  if (!fact || fact->origin == StaticAddressOrigin::Null) {
    return;
  }
  if (fact->origin != StaticAddressOrigin::DynamicObject) {
    addDiagnostic("static safety check failed: " + std::string(operation) +
                  " requires a dynamic-object base address");
    return;
  }
  if (!fact->isBaseAddress) {
    addDiagnostic("static safety check failed: " + std::string(operation) +
                  " requires a dynamic-object base address");
    return;
  }
  const auto state = staticDynamicObjectStates_.find(fact->dynamicObjectId);
  if (state == staticDynamicObjectStates_.end() ||
      state->second == StaticDynamicObjectState::Freed) {
    addDiagnostic("static safety check failed: " +
                  (operation == "free" ? std::string("double free")
                                       : std::string("invalid reallocation of "
                                                     "released dynamic object")));
  }
}

bool LlvmEmitter::releaseStaticDynamicObject(const hir::Expr &expression) {
  const auto fact = staticAddressFact(expression);
  if (!fact || fact->origin != StaticAddressOrigin::DynamicObject ||
      !fact->isBaseAddress) {
    return false;
  }
  const auto state = staticDynamicObjectStates_.find(fact->dynamicObjectId);
  if (state == staticDynamicObjectStates_.end() ||
      state->second != StaticDynamicObjectState::Live) {
    return false;
  }
  state->second = StaticDynamicObjectState::Freed;
  return true;
}

void LlvmEmitter::recordStaticAddressAssignment(std::string_view bindingName,
                                                const hir::Expr &value) {
  const auto recordDynamicObject = [this, bindingName](
                                       std::optional<std::uint64_t> extent) {
    const auto dynamicObjectId = nextStaticDynamicObjectId_++;
    staticDynamicObjectStates_[dynamicObjectId] = StaticDynamicObjectState::Live;
    staticAddressFacts_[std::string(bindingName)] =
        StaticAddressFact{
            StaticAddressOrigin::DynamicObject,
            dynamicObjectId,
            true,
            extent ? std::optional<StaticAddressRange>{StaticAddressRange{
                         "dynamic:" + std::to_string(dynamicObjectId), 0, 0,
                         *extent}}
                   : std::nullopt};
  };

  if (const auto *call = dynamic_cast<const hir::CallExpr *>(&value)) {
    if (call->builtin == stdlib::BuiltinId::Alloc ||
        call->builtin == stdlib::BuiltinId::Calloc) {
      std::optional<std::uint64_t> extent;
      if (call->addressFacts && call->addressFacts->knownExtent) {
        extent = static_cast<std::uint64_t>(*call->addressFacts->knownExtent);
      }
      if (!extent && call->builtin == stdlib::BuiltinId::Alloc &&
          call->arguments.size() == 1U) {
        extent = staticUnsignedInteger(*call->arguments.front());
      }
      if (!extent && call->builtin == stdlib::BuiltinId::Calloc &&
          call->arguments.size() == 2U) {
        const auto count = staticUnsignedInteger(*call->arguments[0]);
        const auto size = staticUnsignedInteger(*call->arguments[1]);
        if (count && size) {
          extent = multiplyUnsignedIntegers(*count, *size);
        }
      }
      recordDynamicObject(extent);
      return;
    }
    if (call->builtin == stdlib::BuiltinId::Realloc &&
        !call->arguments.empty()) {
      std::optional<std::uint64_t> extent;
      if (call->addressFacts && call->addressFacts->knownExtent) {
        extent = static_cast<std::uint64_t>(*call->addressFacts->knownExtent);
      } else if (call->arguments.size() == 2U) {
        extent = staticUnsignedInteger(*call->arguments[1]);
      }
      const auto input = staticAddressFact(*call->arguments.front());
      if (extent && *extent == 0) {
        (void)releaseStaticDynamicObject(*call->arguments.front());
      } else if (input && input->origin == StaticAddressOrigin::DynamicObject) {
        const auto state = staticDynamicObjectStates_.find(input->dynamicObjectId);
        if (state != staticDynamicObjectStates_.end()) {
          state->second = StaticDynamicObjectState::Unknown;
        }
      }

      // realloc may fail, leaving its input object live.  Merging that path
      // with a successful reallocation leaves both the old object's lifetime
      // and the returned address unknown.
      staticAddressFacts_[std::string(bindingName)] = std::nullopt;
      return;
    }
  }

  staticAddressFacts_[std::string(bindingName)] = staticAddressFact(value);
}

void LlvmEmitter::validateStaticAddressAccess(const hir::Expr &expression,
                                              std::string_view operation) {
  const auto fact = staticAddressFact(expression);
  if (!fact || fact->origin != StaticAddressOrigin::DynamicObject) {
    return;
  }
  const auto state = staticDynamicObjectStates_.find(fact->dynamicObjectId);
  if (state != staticDynamicObjectStates_.end() &&
      state->second == StaticDynamicObjectState::Freed) {
    addDiagnostic("static safety check failed: use after free " +
                  std::string(operation));
  }
}

void LlvmEmitter::validateSafety(const hir::TranslationUnit &unit) {
  if (!hasStaticSafetyChecks()) {
    return;
  }

  resetStaticSafetyState();
  if (unit.globalInit) {
    validateSafety(*unit.globalInit);
  }
  const auto globalBaseline = staticSafetyState();
  const auto recordGlobalBindings = [this](const auto &facts) {
    for (const auto &[bindingName, value] : facts) {
      (void)value;
      staticGlobalBindings_.insert(bindingName);
    }
  };
  recordGlobalBindings(globalBaseline.integerValues);
  recordGlobalBindings(globalBaseline.unsignedIntegerValues);
  recordGlobalBindings(globalBaseline.addressFacts);
  for (const auto &function : unit.functions) {
    restoreStaticSafetyState(globalBaseline);
    validateSafety(*function->body);
  }
}

void LlvmEmitter::validateSafety(const hir::Block &block) {
  SourceRangeScope sourceRange(*this, block.range);
  for (const auto &statement : block.statements) {
    validateSafety(*statement);
  }
}

void LlvmEmitter::validateSafety(const hir::Stmt &statement) {
  SourceRangeScope sourceRange(*this, statement.range);
  if (const auto *list = dynamic_cast<const hir::StatementList *>(&statement)) {
    for (const auto &child : list->statements) {
      validateSafety(*child);
    }
    return;
  }
  if (const auto *store = dynamic_cast<const hir::IntegerStore *>(&statement)) {
    validateSafety(*store->value);
    const auto signedValue = staticSignedInteger(*store->value);
    const auto unsignedValue = staticUnsignedInteger(*store->value);
    recordStaticAddressAssignment(store->bindingName, *store->value);
    const auto addressFact = staticAddressFacts_[store->bindingName];
    invalidateStaticBinding(store->bindingName);
    staticIntegerValues_[store->bindingName] = signedValue;
    staticUnsignedIntegerValues_[store->bindingName] = unsignedValue;
    staticAddressFacts_[store->bindingName] = addressFact;
    return;
  }
  if (const auto *store = dynamic_cast<const hir::FloatStore *>(&statement)) {
    validateSafety(*store->value);
    invalidateStaticBinding(store->bindingName);
    return;
  }
  if (const auto *store = dynamic_cast<const hir::BoolStore *>(&statement)) {
    validateSafety(*store->value);
    const auto signedValue = staticSignedInteger(*store->value);
    const auto unsignedValue = staticUnsignedInteger(*store->value);
    invalidateStaticBinding(store->bindingName);
    staticIntegerValues_[store->bindingName] = signedValue;
    staticUnsignedIntegerValues_[store->bindingName] = unsignedValue;
    return;
  }
  if (const auto *store =
          dynamic_cast<const hir::ViewCopyStore *>(&statement)) {
    validateSafety(*store->value);
    invalidateStaticBinding(store->bindingName);
    return;
  }
  if (const auto *store = dynamic_cast<const hir::StringStore *>(&statement)) {
    invalidateStaticBinding(store->bindingName);
    return;
  }
  if (const auto *store =
          dynamic_cast<const hir::StringCopyStore *>(&statement)) {
    invalidateStaticBinding(store->bindingName);
    return;
  }
  if (const auto *store = dynamic_cast<const hir::PointerStore *>(&statement)) {
    const auto address = staticSignedInteger(*store->address);
    if (address && *address == 0) {
      addDiagnostic("static safety check failed: null address store");
    }
    if (const auto range = staticAddressRange(*store->address)) {
      if (range->offset < range->lowerBound ||
          static_cast<std::uint64_t>(range->offset) + store->targetByteLength >
              range->upperBound) {
        addDiagnostic("static safety check failed: memory store out of bounds");
      }
    }
    validateStaticAddressAccess(*store->address, "store");
    validateSafety(*store->address);
    validateSafety(*store->value);
    invalidateStaticFactsOverlapping(staticAddressRange(*store->address),
                                     store->targetByteLength);
    return;
  }
  if (const auto *call = dynamic_cast<const hir::Call *>(&statement)) {
    if ((call->builtin == stdlib::BuiltinId::Free ||
         call->builtin == stdlib::BuiltinId::Realloc) &&
        !call->arguments.empty()) {
      validateStaticDynamicBase(*call->arguments.front(), call->callee);
    }
    for (const auto &argument : call->arguments) {
      validateSafety(*argument);
    }
    if ((call->builtin == stdlib::BuiltinId::Free ||
         call->builtin == stdlib::BuiltinId::Realloc) &&
        !call->arguments.empty()) {
      if (call->builtin == stdlib::BuiltinId::Free) {
        (void)releaseStaticDynamicObject(*call->arguments.front());
      } else {
        const auto extent = call->arguments.size() == 2U
                                ? staticUnsignedInteger(*call->arguments[1])
                                : std::optional<std::uint64_t>{};
        if (extent && *extent == 0) {
          (void)releaseStaticDynamicObject(*call->arguments.front());
        } else if (const auto input =
                       staticAddressFact(*call->arguments.front());
                   input && input->origin == StaticAddressOrigin::DynamicObject) {
          const auto state = staticDynamicObjectStates_.find(input->dynamicObjectId);
          if (state != staticDynamicObjectStates_.end()) {
            state->second = StaticDynamicObjectState::Unknown;
          }
        }
      }
    }
    if (call->builtin == stdlib::BuiltinId::None) {
      invalidateStaticGlobalFacts();
      for (const auto &argument : call->arguments) {
        invalidateStaticFactsOverlapping(staticMemoryOperandRange(*argument),
                                         std::nullopt);
      }
    }
    if ((call->builtin == stdlib::BuiltinId::Memset ||
         call->builtin == stdlib::BuiltinId::Memcpy ||
         call->builtin == stdlib::BuiltinId::Memmove) &&
        call->arguments.size() == 3U) {
      invalidateStaticFactsOverlapping(
          staticMemoryOperandRange(*call->arguments[0]),
          staticUnsignedInteger(*call->arguments[2]));
    }
    if (call->builtin == stdlib::BuiltinId::Fread &&
        call->arguments.size() == 4U) {
      const auto size = staticUnsignedInteger(*call->arguments[1]);
      const auto count = staticUnsignedInteger(*call->arguments[2]);
      const auto byteLength = size && count
                                  ? multiplyUnsignedIntegers(*size, *count)
                                  : std::optional<std::uint64_t>{};
      invalidateStaticFactsOverlapping(
          staticMemoryOperandRange(*call->arguments[0]), byteLength);
    }
    return;
  }
  if (const auto *call =
          dynamic_cast<const hir::UserTemplateOpCall *>(&statement)) {
    for (const auto &argument : call->arguments) {
      validateSafety(*argument);
    }
    invalidateStaticGlobalFacts();
    return;
  }
  if (const auto *call =
          dynamic_cast<const hir::UserTemplateFormatCall *>(&statement)) {
    validateSafety(*call->value);
    if (call->file) {
      validateSafety(*call->file);
    }
    invalidateStaticGlobalFacts();
    return;
  }
  if (const auto *call =
          dynamic_cast<const hir::MultiReturnCallStore *>(&statement)) {
    for (const auto &argument : call->arguments) {
      validateSafety(*argument);
    }
    for (const auto &target : call->targets) {
      invalidateStaticBinding(target.bindingName);
    }
    invalidateStaticGlobalFacts();
    return;
  }
  if (const auto *call =
          dynamic_cast<const hir::InputCallStore *>(&statement)) {
    if (call->file) {
      validateSafety(*call->file);
    }
    validateSafety(*call->format);
    for (const auto &target : call->countTargets) {
      invalidateStaticBinding(target.bindingName);
    }
    for (const auto &target : call->scanTargets) {
      invalidateStaticBinding(target.bindingName);
    }
    return;
  }
  if (const auto *ret = dynamic_cast<const hir::Return *>(&statement)) {
    for (const auto &value : ret->values) {
      validateSafety(*value);
    }
    return;
  }
  if (const auto *ifStmt = dynamic_cast<const hir::If *>(&statement)) {
    validateSafety(*ifStmt->condition);
    const auto condition = staticBooleanValue(*ifStmt->condition);
    if (condition) {
      if (*condition) {
        validateSafety(*ifStmt->thenBlock);
      } else if (ifStmt->elseBlock) {
        validateSafety(*ifStmt->elseBlock);
      }
      return;
    }

    const auto entry = staticSafetyState();
    validateSafety(*ifStmt->thenBlock);
    const auto thenState = staticSafetyState();
    restoreStaticSafetyState(entry);
    if (ifStmt->elseBlock) {
      validateSafety(*ifStmt->elseBlock);
    }
    const auto elseState = staticSafetyState();
    mergeStaticSafetyStates(thenState, elseState);
    return;
  }
  if (const auto *whileStmt = dynamic_cast<const hir::While *>(&statement)) {
    validateSafety(*whileStmt->condition);
    if (const auto condition = staticBooleanValue(*whileStmt->condition);
        condition && !*condition) {
      return;
    }
    const auto entry = staticSafetyState();
    validateSafety(*whileStmt->body);
    const auto bodyState = staticSafetyState();
    restoreStaticSafetyState(entry);
    mergeStaticSafetyStates(entry, bodyState);
    return;
  }
  if (const auto *forStmt = dynamic_cast<const hir::For *>(&statement)) {
    if (forStmt->init) {
      validateSafety(*forStmt->init);
    }
    if (forStmt->condition) {
      validateSafety(*forStmt->condition);
    }
    if (forStmt->condition) {
      if (const auto condition = staticBooleanValue(*forStmt->condition);
          condition && !*condition) {
        return;
      }
    }
    const auto entry = staticSafetyState();
    validateSafety(*forStmt->body);
    for (const auto &post : forStmt->post) {
      validateSafety(*post);
    }
    const auto bodyState = staticSafetyState();
    restoreStaticSafetyState(entry);
    mergeStaticSafetyStates(entry, bodyState);
    return;
  }
  if (const auto *label = dynamic_cast<const hir::Label *>(&statement)) {
    validateSafety(*label->statement);
    return;
  }
  if (const auto *throwStmt = dynamic_cast<const hir::Throw *>(&statement)) {
    if (throwStmt->delivery) {
      validateSafety(*throwStmt->delivery);
    }
    return;
  }
  if (const auto *tryCatch = dynamic_cast<const hir::TryCatch *>(&statement)) {
    const auto entry = staticSafetyState();
    validateSafety(*tryCatch->tryBlock);
    const auto tryState = staticSafetyState();
    restoreStaticSafetyState(entry);
    validateSafety(*tryCatch->catchBlock);
    const auto catchState = staticSafetyState();
    mergeStaticSafetyStates(tryState, catchState);
    return;
  }
}

void LlvmEmitter::validateSafety(const hir::Expr &expression) {
  SourceRangeScope sourceRange(*this, expression.range);
  if (const auto *deref = dynamic_cast<const hir::DerefExpr *>(&expression)) {
    const auto address = staticSignedInteger(*deref->address);
    if (address && *address == 0) {
      addDiagnostic("static safety check failed: null address dereference");
    }
    if (const auto range = staticAddressRange(*deref->address)) {
      if (range->offset < range->lowerBound ||
          static_cast<std::uint64_t>(range->offset) + deref->byteLength >
              range->upperBound) {
        addDiagnostic("static safety check failed: memory load out of bounds");
      }
    }
    validateStaticAddressAccess(*deref->address, "dereference");
    validateSafety(*deref->address);
    return;
  }
  if (const auto *binary = dynamic_cast<const hir::BinaryExpr *>(&expression)) {
    const auto op = std::string(hir::toString(binary->operation));
    const auto right = staticSignedInteger(*binary->right);
    if ((op == "/" || op == "%") && right && *right == 0) {
      addDiagnostic("static safety check failed: division by zero");
    }
    if ((op == "<<" || op == ">>") && right && *right < 0) {
      addDiagnostic("static safety check failed: negative shift count");
    }
    if (op == "**" && right && *right < 0) {
      addDiagnostic("static safety check failed: negative exponent");
    }
    validateSafety(*binary->left);
    validateSafety(*binary->right);
    return;
  }
  if (const auto *binary =
          dynamic_cast<const hir::FloatBinaryExpr *>(&expression)) {
    validateSafety(*binary->left);
    validateSafety(*binary->right);
    return;
  }
  if (const auto *comparison =
          dynamic_cast<const hir::FloatCompareExpr *>(&expression)) {
    validateSafety(*comparison->left);
    validateSafety(*comparison->right);
    return;
  }
  if (const auto *unary = dynamic_cast<const hir::UnaryExpr *>(&expression)) {
    validateSafety(*unary->operand);
    return;
  }
  if (const auto *ternary =
          dynamic_cast<const hir::TernaryExpr *>(&expression)) {
    validateSafety(*ternary->condition);
    const auto condition = staticBooleanValue(*ternary->condition);
    if (condition) {
      validateSafety(*(*condition ? ternary->thenExpr : ternary->elseExpr));
      return;
    }
    const auto entry = staticSafetyState();
    validateSafety(*ternary->thenExpr);
    const auto thenState = staticSafetyState();
    restoreStaticSafetyState(entry);
    validateSafety(*ternary->elseExpr);
    const auto elseState = staticSafetyState();
    mergeStaticSafetyStates(thenState, elseState);
    return;
  }
  if (const auto *unsignedExpr =
          dynamic_cast<const hir::UnsignedExpr *>(&expression)) {
    validateSafety(*unsignedExpr->operand);
    return;
  }
  if (const auto *cast =
          dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
    validateSafety(*cast->operand);
    return;
  }
  if (const auto *view =
          dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    validateSafety(*view->operand);
    return;
  }
  if (const auto *test =
          dynamic_cast<const hir::BooleanTestExpr *>(&expression)) {
    validateSafety(*test->operand);
    return;
  }
  if (const auto *call =
          dynamic_cast<const hir::UserTemplateOpCallExpr *>(&expression)) {
    for (const auto &argument : call->arguments) {
      validateSafety(*argument);
    }
    invalidateStaticGlobalFacts();
    return;
  }
  if (const auto *call =
          dynamic_cast<const hir::UserTemplateFormatCallExpr *>(&expression)) {
    validateSafety(*call->value);
    if (call->file) {
      validateSafety(*call->file);
    }
    return;
  }
  if (const auto *conversion =
          dynamic_cast<const hir::ToFloatExpr *>(&expression)) {
    validateSafety(*conversion->operand);
    return;
  }
  if (const auto *conversion =
          dynamic_cast<const hir::ToIntExpr *>(&expression)) {
    validateSafety(*conversion->operand);
    return;
  }
  if (const auto *call = dynamic_cast<const hir::CallExpr *>(&expression)) {
    const auto staticUnsignedValue = [this](const hir::Expr &operand) {
      return staticUnsignedInteger(operand);
    };
    const auto reportProductOverflow = [&staticUnsignedValue,
                                        this](const hir::Expr &size,
                                              const hir::Expr &count,
                                              std::string_view callee) {
      const auto sizeValue = staticUnsignedValue(size);
      const auto countValue = staticUnsignedValue(count);
      if (sizeValue && countValue && *sizeValue != 0 &&
          *countValue >
              std::numeric_limits<std::uint64_t>::max() / *sizeValue) {
        addDiagnostic("static safety check failed: " + std::string(callee) +
                      " size overflow");
      }
    };
    if (call->builtin == stdlib::BuiltinId::Realloc &&
        !call->arguments.empty()) {
      validateStaticDynamicBase(*call->arguments.front(), call->callee);
    }
    if (call->builtin == stdlib::BuiltinId::Calloc &&
        call->arguments.size() == 2U) {
      reportProductOverflow(*call->arguments[1], *call->arguments[0],
                            call->callee);
    }
    if ((call->builtin == stdlib::BuiltinId::Fread ||
         call->builtin == stdlib::BuiltinId::Fwrite) &&
        call->arguments.size() == 4U) {
      reportProductOverflow(*call->arguments[1], *call->arguments[2],
                            call->callee);
      const auto size = staticUnsignedValue(*call->arguments[1]);
      const auto count = staticUnsignedValue(*call->arguments[2]);
      if (size && count &&
          (*size == 0 ||
           *count <= std::numeric_limits<std::uint64_t>::max() / *size)) {
        const auto range = staticViewRange(*call->arguments[0]);
        const auto byteLength = *size * *count;
        if (range &&
            (range->offset < range->lowerBound ||
             static_cast<std::uint64_t>(range->offset) > range->upperBound ||
             byteLength > range->upperBound -
                              static_cast<std::uint64_t>(range->offset))) {
          addDiagnostic("static safety check failed: " + call->callee +
                        (call->builtin == stdlib::BuiltinId::Fread
                             ? " destination range out of bounds"
                             : " source range out of bounds"));
        }
      }
    }
    if ((call->builtin == stdlib::BuiltinId::Memset ||
         call->builtin == stdlib::BuiltinId::Memcpy ||
         call->builtin == stdlib::BuiltinId::Memmove ||
         call->builtin == stdlib::BuiltinId::Memcmp) &&
        call->arguments.size() == 3U) {
      const auto byteLength = staticUnsignedValue(*call->arguments[2]);
      const auto destination = staticMemoryOperandRange(*call->arguments[0]);
      const auto source = call->builtin == stdlib::BuiltinId::Memset
                              ? std::optional<StaticAddressRange>{}
                              : staticMemoryOperandRange(*call->arguments[1]);
      const auto isOutOfBounds =
          [&byteLength](const std::optional<StaticAddressRange> &range) {
            return range && byteLength &&
                   (range->offset < range->lowerBound ||
                    static_cast<std::uint64_t>(range->offset) >
                        range->upperBound ||
                    *byteLength >
                        range->upperBound -
                            static_cast<std::uint64_t>(range->offset));
          };
      if (isOutOfBounds(destination)) {
        addDiagnostic("static safety check failed: " + call->callee +
                      " destination range out of bounds");
      }
      if (isOutOfBounds(source)) {
        addDiagnostic("static safety check failed: " + call->callee +
                      " source range out of bounds");
      }
      if (call->builtin == stdlib::BuiltinId::Memcpy && byteLength &&
          *byteLength != 0 && destination && source &&
          destination->bindingName == source->bindingName &&
          destination->offset >= 0 && source->offset >= 0) {
        const auto destinationOffset =
            static_cast<std::uint64_t>(destination->offset);
        const auto sourceOffset = static_cast<std::uint64_t>(source->offset);
        if (*byteLength <=
                std::numeric_limits<std::uint64_t>::max() - destinationOffset &&
            *byteLength <=
                std::numeric_limits<std::uint64_t>::max() - sourceOffset &&
            destinationOffset < sourceOffset + *byteLength &&
            sourceOffset < destinationOffset + *byteLength) {
          addDiagnostic(
              "static safety check failed: overlapping memcpy ranges");
        }
      }
  }
  if (hasRuntimeSafetyChecks() && call->builtin == stdlib::BuiltinId::Abs &&
        call->arguments.size() == 1U) {
      const auto value = staticSignedInteger(*call->arguments.front());
      const auto minimum = signedMinimumForByteLength(call->byteLength);
      if (value && minimum && *value == *minimum) {
        addDiagnostic(
            "static safety check failed: abs of minimum signed value");
      }
    }
    for (const auto &argument : call->arguments) {
      validateSafety(*argument);
    }
    if (call->builtin == stdlib::BuiltinId::None) {
      invalidateStaticGlobalFacts();
      for (const auto &argument : call->arguments) {
        invalidateStaticFactsOverlapping(staticMemoryOperandRange(*argument),
                                         std::nullopt);
      }
    }
    if ((call->builtin == stdlib::BuiltinId::Memset ||
         call->builtin == stdlib::BuiltinId::Memcpy ||
         call->builtin == stdlib::BuiltinId::Memmove) &&
        call->arguments.size() == 3U) {
      invalidateStaticFactsOverlapping(
          staticMemoryOperandRange(*call->arguments[0]),
          staticUnsignedInteger(*call->arguments[2]));
    }
    if (call->builtin == stdlib::BuiltinId::Fread &&
        call->arguments.size() == 4U) {
      const auto size = staticUnsignedInteger(*call->arguments[1]);
      const auto count = staticUnsignedInteger(*call->arguments[2]);
      const auto byteLength = size && count
                                  ? multiplyUnsignedIntegers(*size, *count)
                                  : std::optional<std::uint64_t>{};
      invalidateStaticFactsOverlapping(
          staticMemoryOperandRange(*call->arguments[0]), byteLength);
    }
    return;
  }
  if (const auto *assignment =
          dynamic_cast<const hir::AssignmentExpr *>(&expression)) {
    for (const auto &store : assignment->stores) {
      validateSafety(*store);
    }
    if (assignment->result) {
      validateSafety(*assignment->result);
    }
    return;
  }
}

EmitResult emitLlvmIr(const hir::TranslationUnit &unit, std::string moduleName,
                      CodegenOptions options) {
  return LlvmEmitter(std::move(moduleName), options).emit(unit);
}

} // namespace hitsimple::codegen
