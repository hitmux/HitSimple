#include "LlvmEmitter.h"

#include "safety/StaticSafetyAnalyzer.h"

#include "hitsimple/codegen/LlvmCompatibility.h"
#include "hitsimple/codegen/NativeTarget.h"
#include "hitsimple/literal/Literal.h"

#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace hitsimple::codegen {
namespace {

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

std::optional<std::int64_t>
knownSignedAddressOffset(const hir::Expr &expression) {
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
    const auto value = knownSignedAddressOffset(*unary->operand);
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
  }
  if (const auto *view =
          dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return knownSignedAddressOffset(*view->operand);
  }
  return std::nullopt;
}

std::optional<std::int64_t> addKnownAddressOffset(std::int64_t left,
                                                  std::int64_t right) {
  if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
      (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)) {
    return std::nullopt;
  }
  return left + right;
}

} // namespace

DebugInfoFormat debugInfoFormatForTargetTriple(std::string_view targetTriple) {
  return parseTargetTriple(targetTriple).isOSWindows()
             ? DebugInfoFormat::CodeView
             : DebugInfoFormat::Dwarf;
}

LlvmEmitter::LlvmEmitter(llvm::LLVMContext& context, llvm::Module& module,
                         std::string moduleName, CodegenOptions options)
    : moduleName_(std::move(moduleName)), options_(std::move(options)),
      context_(context), module_(&module), builder_(context_) {
  if (options_.emitDebugInfo) {
    // Clang 18 accepts intrinsic-form debug declarations. LLVM versions that
    // offer the transitional conversion API use it before any IR is emitted.
    useLegacyDebugInfoFormatIfAvailable(*module_);
    initializeDebugInfo();
  }
}

std::vector<diagnostic::Diagnostic>
LlvmEmitter::emit(const hir::TranslationUnit &unit) {
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
      return std::move(diagnostics_);
    }
  }

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
    return std::move(diagnostics_);
  }

  finalizeDebugInfo();
  std::string verifierMessage;
  llvm::raw_string_ostream verifierStream(verifierMessage);
  if (llvm::verifyModule(*module_, &verifierStream)) {
    addDiagnostic("invalid LLVM IR: " + verifierStream.str());
    return std::move(diagnostics_);
  }
  return {};
}

void LlvmEmitter::addDiagnostic(std::string diagnostic) {
  auto emitted =
      diagnostic::Diagnostic::error(diagnostic::Stage::Codegen, std::move(diagnostic));
  emitted.range = currentDiagnosticRange_;
  diagnostics_.push_back(std::move(emitted));
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

std::optional<LlvmEmitter::KnownAddressRange>
LlvmEmitter::knownAddressRange(const hir::Expr &expression) const {
  if (const auto *unsignedExpr =
          dynamic_cast<const hir::UnsignedExpr *>(&expression)) {
    return knownAddressRange(*unsignedExpr->operand);
  }
  if (const auto *cast =
          dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
    const auto pointerBytes = sizeof(void *);
    if (cast->byteLength != pointerBytes ||
        cast->operand->result.lengthKind != hir::ViewLengthKind::Static ||
        cast->operand->result.staticByteLength != pointerBytes) {
      return std::nullopt;
    }
    return knownAddressRange(*cast->operand);
  }
  if (const auto *view =
          dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return knownAddressRange(*view->operand);
  }
  if (const auto *address =
          dynamic_cast<const hir::AddressOfExpr *>(&expression)) {
    const auto offset = static_cast<std::int64_t>(address->offset);
    return KnownAddressRange{
        address->bindingName, offset, offset,
        static_cast<std::uint64_t>(address->offset + address->targetByteLength)};
  }
  if (const auto *binary = dynamic_cast<const hir::BinaryExpr *>(&expression)) {
    const bool addressOffset =
        binary->operationKind == hir::StandardOperationKind::AddressOffset;
    if (!addressOffset && binary->operation != hir::BinaryOperator::Add &&
        binary->operation != hir::BinaryOperator::Subtract) {
      return std::nullopt;
    }
    auto range = knownAddressRange(*binary->left);
    const auto offset = knownSignedAddressOffset(*binary->right);
    if (!range || !offset) {
      return std::nullopt;
    }
    auto signedOffset = *offset;
    if (!addressOffset && binary->operation == hir::BinaryOperator::Subtract) {
      if (signedOffset == std::numeric_limits<std::int64_t>::min()) {
        return std::nullopt;
      }
      signedOffset = -signedOffset;
    }
    const auto adjusted = addKnownAddressOffset(range->offset, signedOffset);
    if (!adjusted) {
      return std::nullopt;
    }
    range->offset = *adjusted;
    return range;
  }
  return std::nullopt;
}

std::optional<LlvmEmitter::KnownAddressRange>
LlvmEmitter::knownViewRange(const hir::Expr &expression) const {
  if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
    const auto offset = static_cast<std::int64_t>(variable->offset);
    return KnownAddressRange{
        variable->bindingName, offset, offset,
        static_cast<std::uint64_t>(variable->offset + variable->byteLength)};
  }
  if (const auto *view = dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return knownViewRange(*view->operand);
  }
  if (const auto *deref = dynamic_cast<const hir::DerefExpr *>(&expression)) {
    return knownAddressRange(*deref->address);
  }
  return knownAddressRange(expression);
}

std::optional<LlvmEmitter::KnownAddressRange>
LlvmEmitter::knownMemoryOperandRange(const hir::Expr &expression) const {
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
  return isRawAddress() ? knownAddressRange(expression)
                        : knownViewRange(expression);
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
  if (const auto *unsignedExpr =
          dynamic_cast<const hir::UnsignedExpr *>(&expression)) {
    return targetsRegisteredInternalObject(*unsignedExpr->operand);
  }
  if (const auto *cast =
          dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
    return targetsRegisteredInternalObject(*cast->operand);
  }
  if (const auto *view =
          dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return targetsRegisteredInternalObject(*view->operand);
  }
  if (const auto *binary = dynamic_cast<const hir::BinaryExpr *>(&expression);
      binary != nullptr &&
      binary->operationKind == hir::StandardOperationKind::AddressOffset) {
    return targetsRegisteredInternalObject(*binary->left);
  }
  return false;
}

bool LlvmEmitter::hasKnownStaticAddressRange(const hir::Expr &expression,
                                             std::size_t byteLength) const {
  const auto range = knownAddressRange(expression);
  if (!range || range->offset < range->lowerBound ||
      !targetsRegisteredInternalObject(expression)) {
    return false;
  }
  return static_cast<std::uint64_t>(range->offset) + byteLength <=
         range->upperBound;
}


ModuleEmitResult emitLlvmModule(const hir::TranslationUnit &unit,
                                std::string moduleName,
                                CodegenOptions options) {
  auto hirDiagnostics = hir::verifyHIR(unit);
  if (!hirDiagnostics.empty()) {
    return {{}, {}, std::move(hirDiagnostics)};
  }
  const safety::StaticSafetyOptions safetyOptions{
      options.safetyMode == SafetyMode::StaticChecked ||
          options.safetyMode == SafetyMode::Checked,
      options.safetyMode == SafetyMode::Checked};
  auto safetyResult = safety::analyzeStaticSafety(unit, safetyOptions);
  if (!safetyResult.diagnostics.empty()) {
    return {{}, {}, std::move(safetyResult.diagnostics)};
  }

  auto context = std::make_unique<llvm::LLVMContext>();
  auto module = std::make_unique<llvm::Module>(moduleName, *context);
  const auto targetTriple = options.targetTriple.empty()
                                ? llvm::sys::getDefaultTargetTriple()
                                : options.targetTriple;
  setModuleTargetTriple(*module, targetTriple);

  NativeTargetOptions nativeTargetOptions;
  nativeTargetOptions.triple = targetTriple;
  std::string nativeTargetError;
  const auto nativeTarget =
      createNativeTarget(nativeTargetOptions, nativeTargetError);
  if (!nativeTarget) {
    return {{}, {}, {diagnostic::Diagnostic::error(
                        diagnostic::Stage::Codegen, std::move(nativeTargetError))}};
  }
  module->setDataLayout(nativeTarget->machine->createDataLayout());

  LlvmEmitter emitter(*context, *module, std::move(moduleName), options);
  auto diagnostics = emitter.emit(unit);
  if (!diagnostics.empty()) {
    return {{}, {}, std::move(diagnostics)};
  }
  return {std::move(context), std::move(module), {}};
}

} // namespace hitsimple::codegen
