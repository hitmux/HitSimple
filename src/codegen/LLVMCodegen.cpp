#include "LlvmEmitter.h"

#include "hitsimple/codegen/LlvmCompatibility.h"
#include "hitsimple/literal/Literal.h"

#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Host.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string_view>
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
    if (!value ||
        *value > static_cast<std::uint64_t>(
                     std::numeric_limits<std::int64_t>::max())) {
      return std::nullopt;
    }
    return static_cast<std::int64_t>(*value);
  }

  if (const auto *unary = dynamic_cast<const hir::UnaryExpr *>(&expression)) {
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

bool hasCAbiAggregateByValue(
    const std::optional<hir::FunctionAbiSignature>& signature) {
  if (!signature || !signature->isCCompatibility) {
    return false;
  }
  const auto hasAggregate = [](const std::vector<hir::AbiType>& types) {
    for (const auto& type : types) {
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

} // namespace

LlvmEmitter::LlvmEmitter(std::string moduleName, CodegenOptions options)
    : moduleName_(std::move(moduleName)), options_(options),
      module_(std::make_unique<llvm::Module>(moduleName_, context_)),
      builder_(context_) {
  const auto targetTriple = options_.targetTriple.empty()
                                ? llvm::sys::getDefaultTargetTriple()
                                : options_.targetTriple;
  setModuleTargetTriple(*module_, targetTriple);
  if (options_.emitDebugInfo) {
    // Clang 18 accepts intrinsic-form debug declarations but not LLVM 19's
    // printed #dbg_declare records.
    module_->setNewDbgInfoFormatFlag(false);
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
            const std::optional<hir::FunctionAbiSignature>& signature) {
          if (!hasCAbiAggregateByValue(signature)) {
            return;
          }
          addDiagnostic(
              "C aggregate by-value ABI is only supported for x86_64 SysV "
              "ELF targets; target '" +
              targetTriple + "' is not supported for function '" +
              std::string(name) + "'");
        };
    for (const auto& function : unit.externFunctions) {
      rejectAggregateSignature(function.name, function.abiSignature);
    }
    for (const auto& function : unit.functions) {
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
    addDiagnostic("cannot resolve LLVM target '" + targetTriple + "': " +
                  targetError);
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
    LlvmEmitter& emitter,
    const std::optional<diagnostic::SourceRange>& range)
    : emitter_(emitter), previous_(emitter.builder_.getCurrentDebugLocation()) {
  if (auto* location = emitter_.debugLocation(range)) {
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
  debugFile_ = debugBuilder_->createFile(fileName.empty() ? moduleName_ : fileName,
                                         directory);
  debugCompileUnit_ = debugBuilder_->createCompileUnit(
      llvm::dwarf::DW_LANG_C, debugFile_, "hsc", false, "", 0);
  module_->addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                         llvm::DEBUG_METADATA_VERSION);
  module_->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 5);
}

void LlvmEmitter::finalizeDebugInfo() {
  if (debugBuilder_) {
    debugBuilder_->finalize();
  }
}

void LlvmEmitter::beginDebugFunction(const hir::Function& function,
                                     llvm::Function& llvmFunction) {
  if (!debugBuilder_ || !debugFile_) {
    return;
  }
  const auto line = static_cast<unsigned>(
      function.range ? std::max<std::size_t>(function.range->begin.line, 1U) : 1U);
  auto* type = debugBuilder_->createSubroutineType(
      debugBuilder_->getOrCreateTypeArray({}));
  auto* subprogram = debugBuilder_->createFunction(
      debugFile_, function.name, llvmFunction.getName(), debugFile_, line, type,
      line, llvm::DINode::FlagPrototyped,
      llvm::DISubprogram::SPFlagDefinition);
  llvmFunction.setSubprogram(subprogram);
  debugScope_ = subprogram;
}

llvm::DILocation* LlvmEmitter::debugLocation(
    const std::optional<diagnostic::SourceRange>& range) {
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
    std::string_view name,
    const std::optional<diagnostic::SourceRange>& range,
    std::size_t byteLength, llvm::Value* storage, unsigned argumentIndex) {
  if (!debugBuilder_ || !debugFile_ || !debugScope_ || storage == nullptr) {
    return;
  }
  const auto line = static_cast<unsigned>(
      range ? std::max<std::size_t>(range->begin.line, 1U) : 1U);
  auto* byteType = debugBuilder_->createBasicType(
      "u8", 8, llvm::dwarf::DW_ATE_unsigned_char);
  llvm::Metadata* subranges[] = {
      debugBuilder_->getOrCreateSubrange(0, static_cast<std::int64_t>(byteLength))};
  auto* type = debugBuilder_->createArrayType(
      byteLength * 8U, 8, byteType,
      debugBuilder_->getOrCreateArray(subranges));
  llvm::DILocalVariable* variable = nullptr;
  if (argumentIndex == 0) {
    variable = debugBuilder_->createAutoVariable(debugScope_, name, debugFile_,
                                                  line, type, true);
  } else {
    variable = debugBuilder_->createParameterVariable(
        debugScope_, name, argumentIndex, debugFile_, line, type, true);
  }
  debugBuilder_->insertDeclare(storage, variable,
                               debugBuilder_->createExpression(),
                               debugLocation(range),
                               builder_.GetInsertBlock());
}

std::optional<LlvmEmitter::StaticAddressRange>
LlvmEmitter::staticAddressRange(const hir::Expr &expression) const {
  if (const auto *address =
          dynamic_cast<const hir::AddressOfExpr *>(&expression)) {
    const auto offset = static_cast<std::int64_t>(address->offset);
    return StaticAddressRange{
        offset, offset,
        static_cast<std::uint64_t>(address->offset + address->targetByteLength)};
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

bool LlvmEmitter::hasKnownStaticAddressRange(
    const hir::Expr &expression, std::size_t byteLength) const {
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
  if (unit.globalInit) {
    validateSafety(*unit.globalInit);
  }
  for (const auto &function : unit.functions) {
    staticIntegerValues_.clear();
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
    staticIntegerValues_[store->bindingName] = constantSignedInteger(*store->value);
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
  if (const auto *ternary = dynamic_cast<const hir::TernaryExpr *>(&expression)) {
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

EmitResult emitLlvmIr(const hir::TranslationUnit &unit,
                      std::string moduleName, CodegenOptions options) {
  return LlvmEmitter(std::move(moduleName), options).emit(unit);
}

} // namespace hitsimple::codegen
