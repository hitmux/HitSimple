#include "LlvmEmitter.h"

#include "hitsimple/codegen/LlvmCompatibility.h"
#include "hitsimple/literal/Literal.h"
#include "hitsimple/support/Path.h"

#include <llvm/AsmParser/Parser.h>
#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

namespace hitsimple::codegen {
namespace {

std::string_view integerOperatorSuffix(std::string_view op) {
  const std::string_view suffixes[] = {"**", "<<", ">>", "/", "%"};
  for (const auto suffix : suffixes) {
    if (op.ends_with(suffix)) {
      return suffix;
    }
  }
  return op;
}

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
      return -*value;
    }
    if (unary->op == "+") {
      return *value;
    }
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
  return std::nullopt;
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

EmitObjectResult
LlvmEmitter::emitObject(const hir::TranslationUnit &unit,
                        const std::filesystem::path &outputPath) {
  auto irResult = emit(unit);
  if (!irResult.diagnostics.empty()) {
    return EmitObjectResult{std::move(irResult.diagnostics)};
  }

  std::string targetError;
  const auto targetTriple = moduleTargetTriple(*module_);
  const auto *target = resolveTarget(targetTriple, targetError);
  if (target == nullptr) {
    return EmitObjectResult{{diagnostic::Diagnostic::error(
        diagnostic::Stage::Codegen,
        "cannot resolve LLVM target '" + targetTriple + "': " + targetError)}};
  }
  llvm::TargetOptions targetOptions;
  std::unique_ptr<llvm::TargetMachine> targetMachine(
      createGenericTargetMachine(*target, targetTriple, targetOptions));
  if (!targetMachine) {
    return EmitObjectResult{{diagnostic::Diagnostic::error(
        diagnostic::Stage::Codegen,
        "cannot create LLVM target machine for '" + targetTriple + "'")}};
  }

  std::error_code error;
  llvm::raw_fd_ostream output(support::pathToUtf8(outputPath), error,
                              llvm::sys::fs::OF_None);
  if (error) {
    return EmitObjectResult{{diagnostic::Diagnostic::error(
        diagnostic::Stage::Codegen, "cannot open object output '" +
                                        support::pathToUtf8(outputPath) +
                                        "': " + error.message())}};
  }

  llvm::legacy::PassManager passManager;
#if LLVM_VERSION_MAJOR >= 18
  if (targetMachine->addPassesToEmitFile(passManager, output, nullptr,
                                         llvm::CodeGenFileType::ObjectFile)) {
#else
  if (targetMachine->addPassesToEmitFile(passManager, output, nullptr,
                                         llvm::CGFT_ObjectFile)) {
#endif
    return EmitObjectResult{{diagnostic::Diagnostic::error(
        diagnostic::Stage::Codegen,
        "LLVM target cannot emit an object file for '" + targetTriple + "'")}};
  }
  passManager.run(*module_);
  output.flush();
  if (output.has_error()) {
    return EmitObjectResult{{diagnostic::Diagnostic::error(
        diagnostic::Stage::Codegen, "cannot write object output '" +
                                        support::pathToUtf8(outputPath) +
                                        "'")}};
  }
  return EmitObjectResult{};
}

void LlvmEmitter::addDiagnostic(std::string diagnostic) {
  diagnostics_.push_back(diagnostic::Diagnostic::error(
      diagnostic::Stage::Codegen, std::move(diagnostic)));
}

bool LlvmEmitter::hasStaticSafetyChecks() const {
  return options_.safetyMode == SafetyMode::StaticChecked ||
         options_.safetyMode == SafetyMode::Checked;
}

bool LlvmEmitter::hasRuntimeSafetyChecks() const {
  return options_.safetyMode == SafetyMode::Checked;
}

LlvmEmitter::DebugLocationScope::DebugLocationScope(
    LlvmEmitter &emitter, const std::optional<diagnostic::SourceRange> &range)
    : emitter_(emitter), previous_(emitter.builder_.getCurrentDebugLocation()) {
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
  if (const auto *address =
          dynamic_cast<const hir::AddressOfExpr *>(&expression)) {
    const auto offset = static_cast<std::int64_t>(address->offset);
    return StaticAddressRange{address->bindingName, offset, offset,
                              static_cast<std::uint64_t>(
                                  address->offset + address->targetByteLength)};
  }
  const auto *binary = dynamic_cast<const hir::BinaryExpr *>(&expression);
  if (binary == nullptr || binary->op != "+") {
    return std::nullopt;
  }
  const auto base = staticAddressRange(*binary->left);
  const auto offset = constantSignedInteger(*binary->right);
  if (!base || !offset) {
    return std::nullopt;
  }
  auto range = *base;
  range.offset += *offset;
  return range;
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
  return binary != nullptr && binary->op == "+" &&
         targetsRegisteredInternalObject(*binary->left);
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

void LlvmEmitter::validateSafety(const hir::TranslationUnit &unit) {
  if (!hasStaticSafetyChecks()) {
    return;
  }

  staticIntegerValues_.clear();
  staticUnsignedIntegerValues_.clear();
  if (unit.globalInit) {
    validateSafety(*unit.globalInit);
  }
  for (const auto &function : unit.functions) {
    staticIntegerValues_.clear();
    staticUnsignedIntegerValues_.clear();
    validateSafety(*function->body);
  }
}

void LlvmEmitter::validateSafety(const hir::Block &block) {
  for (const auto &statement : block.statements) {
    validateSafety(*statement);
  }
}

void LlvmEmitter::validateSafety(const hir::Stmt &statement) {
  if (const auto *list = dynamic_cast<const hir::StatementList *>(&statement)) {
    for (const auto &child : list->statements) {
      validateSafety(*child);
    }
    return;
  }
  if (const auto *store = dynamic_cast<const hir::IntegerStore *>(&statement)) {
    validateSafety(*store->value);
    staticIntegerValues_[store->bindingName] =
        constantSignedInteger(*store->value);
    staticUnsignedIntegerValues_[store->bindingName] =
        constantUnsignedInteger(*store->value);
    return;
  }
  if (const auto *store = dynamic_cast<const hir::FloatStore *>(&statement)) {
    validateSafety(*store->value);
    staticIntegerValues_[store->bindingName] = std::nullopt;
    return;
  }
  if (const auto *store = dynamic_cast<const hir::BoolStore *>(&statement)) {
    validateSafety(*store->value);
    staticIntegerValues_[store->bindingName] = std::nullopt;
    return;
  }
  if (const auto *store = dynamic_cast<const hir::PointerStore *>(&statement)) {
    auto address = constantSignedInteger(*store->address);
    if (!address) {
      if (const auto *variable =
              dynamic_cast<const hir::VariableRef *>(store->address.get())) {
        const auto found = staticIntegerValues_.find(variable->bindingName);
        if (found != staticIntegerValues_.end()) {
          address = found->second;
        }
      }
    }
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
    validateSafety(*store->address);
    validateSafety(*store->value);
    return;
  }
  if (const auto *call = dynamic_cast<const hir::Call *>(&statement)) {
    if (call->builtin == stdlib::BuiltinId::Free && !call->arguments.empty() &&
        dynamic_cast<const hir::AddressOfExpr *>(call->arguments[0].get()) !=
            nullptr) {
      addDiagnostic("static safety check failed: cannot free static storage");
    }
    for (const auto &argument : call->arguments) {
      validateSafety(*argument);
    }
    return;
  }
  if (const auto *call =
          dynamic_cast<const hir::UserTemplateOpCall *>(&statement)) {
    for (const auto &argument : call->arguments) {
      validateSafety(*argument);
    }
    return;
  }
  if (const auto *call =
          dynamic_cast<const hir::UserTemplateFormatCall *>(&statement)) {
    validateSafety(*call->value);
    if (call->file) {
      validateSafety(*call->file);
    }
    return;
  }
  if (const auto *call =
          dynamic_cast<const hir::MultiReturnCallStore *>(&statement)) {
    for (const auto &argument : call->arguments) {
      validateSafety(*argument);
    }
    return;
  }
  if (const auto *call =
          dynamic_cast<const hir::InputCallStore *>(&statement)) {
    if (call->file) {
      validateSafety(*call->file);
    }
    validateSafety(*call->format);
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
    validateSafety(*ifStmt->thenBlock);
    if (ifStmt->elseBlock) {
      validateSafety(*ifStmt->elseBlock);
    }
    return;
  }
  if (const auto *whileStmt = dynamic_cast<const hir::While *>(&statement)) {
    validateSafety(*whileStmt->condition);
    validateSafety(*whileStmt->body);
    return;
  }
  if (const auto *forStmt = dynamic_cast<const hir::For *>(&statement)) {
    if (forStmt->init) {
      validateSafety(*forStmt->init);
    }
    if (forStmt->condition) {
      validateSafety(*forStmt->condition);
    }
    for (const auto &post : forStmt->post) {
      validateSafety(*post);
    }
    validateSafety(*forStmt->body);
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
    validateSafety(*tryCatch->tryBlock);
    validateSafety(*tryCatch->catchBlock);
    return;
  }
}

void LlvmEmitter::validateSafety(const hir::Expr &expression) {
  if (const auto *deref = dynamic_cast<const hir::DerefExpr *>(&expression)) {
    auto address = constantSignedInteger(*deref->address);
    if (!address) {
      if (const auto *variable =
              dynamic_cast<const hir::VariableRef *>(deref->address.get())) {
        const auto found = staticIntegerValues_.find(variable->bindingName);
        if (found != staticIntegerValues_.end()) {
          address = found->second;
        }
      }
    }
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
    validateSafety(*deref->address);
    return;
  }
  if (const auto *binary = dynamic_cast<const hir::BinaryExpr *>(&expression)) {
    const auto op = integerOperatorSuffix(binary->op);
    const auto right = constantSignedInteger(*binary->right);
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
    validateSafety(*ternary->thenExpr);
    validateSafety(*ternary->elseExpr);
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
  if (const auto *call =
          dynamic_cast<const hir::UserTemplateOpCallExpr *>(&expression)) {
    for (const auto &argument : call->arguments) {
      validateSafety(*argument);
    }
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
      auto value = constantUnsignedInteger(operand);
      if (value) {
        return value;
      }
      const auto *variable = dynamic_cast<const hir::VariableRef *>(&operand);
      if (variable == nullptr) {
        return std::optional<std::uint64_t>{};
      }
      const auto found =
          staticUnsignedIntegerValues_.find(variable->bindingName);
      return found == staticUnsignedIntegerValues_.end()
                 ? std::optional<std::uint64_t>{}
                 : found->second;
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
      auto value = constantSignedInteger(*call->arguments.front());
      if (!value) {
        if (const auto *variable = dynamic_cast<const hir::VariableRef *>(
                call->arguments.front().get())) {
          const auto found = staticIntegerValues_.find(variable->bindingName);
          if (found != staticIntegerValues_.end()) {
            value = found->second;
          }
        }
      }
      const auto minimum = signedMinimumForByteLength(call->byteLength);
      if (value && minimum && *value == *minimum) {
        addDiagnostic(
            "static safety check failed: abs of minimum signed value");
      }
    }
    for (const auto &argument : call->arguments) {
      validateSafety(*argument);
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

EmitObjectResult emitObjectFile(const hir::TranslationUnit &unit,
                                std::string moduleName,
                                const std::filesystem::path &outputPath,
                                CodegenOptions options) {
  return LlvmEmitter(std::move(moduleName), options)
      .emitObject(unit, outputPath);
}

EmitObjectResult emitObjectFile(std::string_view llvmIr,
                                const std::filesystem::path &outputPath,
                                CodegenOptions options) {
  llvm::LLVMContext context;
  llvm::SMDiagnostic parseDiagnostic;
  auto buffer = llvm::MemoryBuffer::getMemBuffer(llvmIr, "hitsimple-merged");
  auto module = llvm::parseAssembly(*buffer, parseDiagnostic, context);
  if (!module) {
    std::string message;
    llvm::raw_string_ostream stream(message);
    parseDiagnostic.print("hsc", stream);
    return EmitObjectResult{{diagnostic::Diagnostic::error(
        diagnostic::Stage::Codegen,
        "cannot parse merged LLVM IR: " + stream.str())}};
  }
  if (!options.targetTriple.empty()) {
    setModuleTargetTriple(*module, options.targetTriple);
  }

  std::string targetError;
  const auto targetTriple = moduleTargetTriple(*module);
  const auto *target = resolveTarget(targetTriple, targetError);
  if (target == nullptr) {
    return EmitObjectResult{{diagnostic::Diagnostic::error(
        diagnostic::Stage::Codegen,
        "cannot resolve LLVM target '" + targetTriple + "': " + targetError)}};
  }
  llvm::TargetOptions targetOptions;
  std::unique_ptr<llvm::TargetMachine> targetMachine(
      createGenericTargetMachine(*target, targetTriple, targetOptions));
  if (!targetMachine) {
    return EmitObjectResult{{diagnostic::Diagnostic::error(
        diagnostic::Stage::Codegen,
        "cannot create LLVM target machine for '" + targetTriple + "'")}};
  }

  std::error_code error;
  llvm::raw_fd_ostream output(support::pathToUtf8(outputPath), error,
                              llvm::sys::fs::OF_None);
  if (error) {
    return EmitObjectResult{{diagnostic::Diagnostic::error(
        diagnostic::Stage::Codegen, "cannot open object output '" +
                                        support::pathToUtf8(outputPath) +
                                        "': " + error.message())}};
  }

  llvm::legacy::PassManager passManager;
#if LLVM_VERSION_MAJOR >= 18
  if (targetMachine->addPassesToEmitFile(passManager, output, nullptr,
                                         llvm::CodeGenFileType::ObjectFile)) {
#else
  if (targetMachine->addPassesToEmitFile(passManager, output, nullptr,
                                         llvm::CGFT_ObjectFile)) {
#endif
    return EmitObjectResult{{diagnostic::Diagnostic::error(
        diagnostic::Stage::Codegen,
        "LLVM target cannot emit an object file for '" + targetTriple + "'")}};
  }
  passManager.run(*module);
  output.flush();
  if (output.has_error()) {
    return EmitObjectResult{{diagnostic::Diagnostic::error(
        diagnostic::Stage::Codegen, "cannot write object output '" +
                                        support::pathToUtf8(outputPath) +
                                        "'")}};
  }
  return EmitObjectResult{};
}

} // namespace hitsimple::codegen
