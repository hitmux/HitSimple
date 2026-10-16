#include "SemaAnalyzer.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace hitsimple::sema {

AnalyzeResult Analyzer::analyze(const ast::TranslationUnit &unit,
                                const AnalyzeOptions &options) {
  currentRange_.reset();
  expressionDepth_ = 0;
  hir::setActiveSourceRange(std::nullopt);
  std::vector<std::unique_ptr<hir::Function>> functions;
  std::vector<hir::ExternFunction> externFunctions;
  scopes_.clear();
  bindingCounts_.clear();
  functions_.clear();
  pendingGlobals_.clear();
  resolvingGlobals_.clear();
  globals_.clear();
  currentFunction_ = nullptr;
  currentParameters_.clear();
  loopDepth_ = 0;
  blockDepth_ = 0;
  labels_.clear();
  pendingGotos_.clear();
  structs_.clear();
  templates_.clear();
  topLevelNames_.clear();
  implOpKeys_.clear();
  implOpInfos_.clear();
  implOpIndexes_.clear();
  implMethodInfos_.clear();
  implMethodIndexes_.clear();
  userTemplateBindings_.clear();
  memberTemplateOverrides_.clear();
  addressFacts_.clear();
  standardHeaders_ = options.standardHeaders;
  cCompatibilityMode_ = options.cCompatibilityMode;
  internalStandardModule_ = options.internalStandardModule;
  beginScope();

  std::vector<hir::StructLayout> structLayouts;
  collectStructLayouts(unit, structLayouts);

  std::vector<hir::ViewTemplate> viewTemplates;
  collectViewTemplates(unit, viewTemplates);

  std::vector<hir::ImplOpBinding> implOps;
  collectImplOps(unit, implOps);
  collectImplMethods(unit);
  if (!result_.diagnostics.empty()) {
    return std::move(result_);
  }

  // Collect names and callable contracts before inspecting initializers.
  // Inferred global lengths are resolved lazily, so forward global references
  // use the same validated expression semantics as ordinary declarations.
  for (const auto *globalNew : unit.globalNews) {
    CurrentRangeGuard rangeGuard(*this, *globalNew);
    if (registerTopLevelName(globalNew->name)) {
      pendingGlobals_.emplace(globalNew->name, globalNew);
    }
  }
  (void)collectFunctionSignatures(unit, externFunctions);
  for (const auto *externVariable : unit.externVariables) {
    CurrentRangeGuard rangeGuard(*this, *externVariable);
    if (!registerTopLevelName(externVariable->name)) {
      continue;
    }
    std::optional<std::size_t> length;
    if (externVariable->length.empty()) {
      length = templateByteLength(externVariable->templateName);
      if (!length) {
        const auto templateName =
            externVariable->templateName.empty()
                ? std::string_view{"none"}
                : std::string_view{externVariable->templateName};
        addDiagnostic("extern variable '" + externVariable->name +
                      "' requires an explicit byte length for template '" +
                      std::string(templateName) + "'");
        continue;
      }
    } else {
      length = parseDeclaredLength(externVariable->length,
                                   externVariable->templateName);
      if (!length) {
        continue;
      }
    }
    if (*length == 0) {
      addDiagnostic("invalid byte length for extern variable '" +
                    externVariable->name + "'");
      continue;
    }
    Symbol symbol;
    if (!declare(externVariable->name, *length, hir::MemoryStorage::Global,
                 externVariable->templateName, symbol)) {
      addDiagnostic("duplicate declaration '" + externVariable->name + "'",
                    currentScopeDeclarationRange(externVariable->name));
      continue;
    }
    globals_.emplace_back(symbol.name, symbol.bindingName, symbol.byteLength,
                          true);
  }
  for (const auto *globalNew : unit.globalNews) {
    (void)lookup(globalNew->name);
  }
  if (!result_.diagnostics.empty()) {
    return std::move(result_);
  }
  // Keep storage order and runtime initializer order source based even when
  // resolving an extent required visiting a later declaration first.
  for (const auto *globalNew : unit.globalNews) {
    const auto *symbol = lookup(globalNew->name);
    if (symbol != nullptr) {
      globals_.emplace_back(symbol->name, symbol->bindingName,
                            symbol->byteLength);
    }
  }

  if (!result_.diagnostics.empty()) {
    return std::move(result_);
  }

  if (!lowerImplOpBodies(functions) || !result_.diagnostics.empty()) {
    return std::move(result_);
  }
  if (!lowerImplMethodBodies(functions) || !result_.diagnostics.empty()) {
    return std::move(result_);
  }

  auto globalInit = lowerGlobalInitializers(unit);
  if (!globalInit || !result_.diagnostics.empty()) {
    return std::move(result_);
  }

  for (const auto &function : unit.functions) {
    functions.push_back(analyze(*function));
  }

  if (!result_.diagnostics.empty()) {
    return std::move(result_);
  }

  result_.unit = std::make_unique<hir::TranslationUnit>(
      std::move(globals_), std::move(structLayouts), std::move(viewTemplates),
      std::move(implOps), std::move(externFunctions), std::move(functions),
      std::move(globalInit));
  auto hirDiagnostics = hir::verifyHIR(*result_.unit);
  if (!hirDiagnostics.empty()) {
    result_.unit.reset();
  }
  result_.diagnostics.insert(result_.diagnostics.end(),
                             std::make_move_iterator(hirDiagnostics.begin()),
                             std::make_move_iterator(hirDiagnostics.end()));
  return std::move(result_);
}

bool Analyzer::declareGlobal(const ast::GlobalNewDecl &declaration) {
  CurrentRangeGuard rangeGuard(*this, declaration);
  if (!resolvingGlobals_.insert(declaration.name).second) {
    addDiagnostic("cyclic global byte-length inference for '" +
                  declaration.name + "'");
    return false;
  }
  // Remove a failed declaration too, so another reference does not repeat
  // the same recursive analysis and diagnostics.
  const auto finish = [this, &declaration](bool succeeded) {
    resolvingGlobals_.erase(declaration.name);
    pendingGlobals_.erase(declaration.name);
    return succeeded;
  };
  if (declaration.templateName == "none") {
    addDiagnostic("declaration template for '" + declaration.name +
                  "' cannot be none");
    return finish(false);
  }
  std::string templateName = declaration.templateName;
  std::size_t length = 0;
  if (!declaration.length.empty()) {
    const auto explicitLength =
        parseDeclaredLength(declaration.length, templateName);
    if (!explicitLength) {
      return finish(false);
    }
    length = *explicitLength;
  } else if (const auto fixedLength = templateByteLength(templateName)) {
    length = *fixedLength;
  }
  Symbol symbol;
  bool reserved = false;
  const auto provisionalLength = length == 0 && declaration.initializer
                                     ? initializerResultContract(*declaration.initializer)
                                     : std::nullopt;
  if (provisionalLength) {
    reserved = declare(declaration.name, *provisionalLength,
                       hir::MemoryStorage::Global, templateName, symbol);
    if (!reserved) {
      addDiagnostic("duplicate declaration '" + declaration.name + "'");
      return finish(false);
    }
  }
  if (length == 0 && declaration.initializer) {
    // Extent resolution is not execution. Restore assignment/address facts;
    // global initializer lowering will revisit the expression in source order.
    const auto savedFacts = addressFacts_;
    auto initializer = analyze(*declaration.initializer);
    addressFacts_ = savedFacts;
    if (!initializer) {
      return finish(false);
    }
    if (initializer->result.lengthKind == hir::ViewLengthKind::Static) {
      length = initializer->result.staticByteLength;
    }
    if (provisionalLength && length != *provisionalLength) {
      addDiagnostic("initializer View length does not match its fixed result contract");
      return finish(false);
    }
    if (templateName.empty() &&
        (initializer->result.category == hir::ViewCategory::Address ||
         initializer->result.category == hir::ViewCategory::Handle)) {
      templateName = initializer->result.templateName;
    }
  }
  if (length == 0) {
    addDiagnostic("invalid byte length for '" + declaration.name + "'");
    return finish(false);
  }
  if (!reserved && !declare(declaration.name, length, hir::MemoryStorage::Global,
                            templateName, symbol)) {
    addDiagnostic("duplicate declaration '" + declaration.name + "'",
                  currentScopeDeclarationRange(declaration.name));
    return finish(false);
  }
  if (reserved) {
    auto &declared = scopes_.front().at(declaration.name);
    declared.templateName = templateName;
    symbol = declared;
  }
  finish(true);
  // Preserve the existing address/handle initializer inference for an
  // explicitly sized name, after making its own storage available to lookup.
  if (symbol.templateName.empty() && declaration.initializer) {
    auto *declared = lookup(declaration.name);
    if (isHandleExpression(*declaration.initializer)) {
      declared->templateName = "handle";
    } else if (operatorTemplateName(*declaration.initializer) == "addr") {
      declared->templateName = "addr";
    }
  }
  return true;
}

std::unique_ptr<hir::Block>
Analyzer::lowerGlobalInitializers(const ast::TranslationUnit &unit) {
  std::vector<std::unique_ptr<hir::Stmt>> statements;
  for (const auto *globalNew : unit.globalNews) {
    CurrentRangeGuard rangeGuard(*this, *globalNew);
    if (!globalNew->initializer) {
      continue;
    }

    ast::AssignmentTarget target(
        std::make_unique<ast::IdentifierExpr>(globalNew->name),
        globalNew->assignmentOp);
    auto value = analyze(*globalNew->initializer);
    if (!value) {
      return nullptr;
    }
    auto store = lowerAssignmentTarget(target, *globalNew->initializer,
                                       std::move(value));
    if (!store || !result_.diagnostics.empty()) {
      return nullptr;
    }
    statements.push_back(std::move(store));
  }
  return std::make_unique<hir::Block>(std::move(statements));
}

std::unique_ptr<hir::Function>
Analyzer::analyze(const ast::FunctionDecl &function) {
  CurrentRangeGuard rangeGuard(*this, function);
  while (scopes_.size() > 1U) {
    endScope();
  }

  auto found = functions_.find(function.name);
  if (found == functions_.end()) {
    addDiagnostic("internal error: missing function signature for '" +
                  function.name + "'");
    return nullptr;
  }
  currentFunction_ = &found->second;
  currentParameters_.clear();
  loopDepth_ = 0;
  catchContracts_.clear();
  blockDepth_ = 0;
  labels_.clear();
  pendingGotos_.clear();
  beginScope();
  for (const auto &param : function.params) {
    std::optional<std::size_t> length;
    if (param.length.empty()) {
      length = param.templateName.empty()
                   ? std::optional<std::size_t>{4}
                   : templateByteLength(param.templateName);
    } else {
      length = parseDeclaredLength(param.length, param.templateName);
    }
    if (!length || *length == 0) {
      addDiagnostic("invalid parameter byte length for '" + param.name + "'");
      continue;
    }
    Symbol symbol;
    if (!declare(param.name, *length, hir::MemoryStorage::Local,
                 param.templateName, symbol)) {
      addDiagnostic("duplicate parameter '" + param.name + "'",
                    currentScopeDeclarationRange(param.name));
      continue;
    }
    currentParameters_.emplace_back(symbol.name, symbol.bindingName,
                                    fixedResult(symbol.templateName,
                                                symbol.byteLength));
  }

  auto body = analyzeFunctionBody(*function.body, function.returns);
  for (const auto &pending : pendingGotos_) {
    const auto label = labels_.find(pending.label);
    if (label == labels_.end()) {
      addDiagnostic("goto references unknown label '" + pending.label + "'");
      continue;
    }
    if (label->second.blockDepth > pending.blockDepth) {
      addDiagnostic("goto into an inner block is not supported for label '" +
                    pending.label + "'");
    }
  }
  endScope();
  auto parameters = std::move(currentParameters_);
  currentParameters_.clear();
  auto returns = currentFunction_->returnByteLengths;
  const auto abi = currentFunction_->isCAbi ? cAbiSignature(*currentFunction_)
                                            : floatingAbiSignature(*currentFunction_);
  currentFunction_ = nullptr;
  auto lowered = std::make_unique<hir::Function>(
      function.name, std::move(parameters), std::move(returns), std::move(body));
  if (abi) {
    lowered->abiSignature = *abi;
  }
  return lowered;
}

bool Analyzer::lowerImplOpBodies(
    std::vector<std::unique_ptr<hir::Function>> &functions) {
  for (const auto &info : implOpInfos_) {
    const auto *op = info.declaration;
    if (op == nullptr || info.returnByteLengths.size() != 1U) {
      addDiagnostic("internal error: invalid impl op metadata");
      return false;
    }
    CurrentRangeGuard rangeGuard(*this, *op->body);

    while (scopes_.size() > 1U) {
      endScope();
    }

    FunctionSignature signature;
    signature.name = info.symbolName;
    signature.returnByteLengths = info.returnByteLengths;
    signature.returnTemplateNames = info.returnTemplateNames;
    signature.returnHasExplicitUserTemplate =
        info.returnHasExplicitUserTemplate;
    signature.returnsKnown = true;
    signature.returnsExplicit = true;
    signature.parameterByteLengths.reserve(op->params.size());
    signature.parameterTemplateNames.reserve(op->params.size());
    for (const auto &param : op->params) {
      const auto byteLength = templateByteLength(param.templateName);
      if (!byteLength || *byteLength == 0) {
        addDiagnostic("internal error: invalid impl parameter template '" +
                      param.templateName + "'");
        return false;
      }
      signature.parameterByteLengths.push_back(*byteLength);
      signature.parameterTemplateNames.push_back(param.templateName);
    }

    currentFunction_ = &signature;
    currentParameters_.clear();
    loopDepth_ = 0;
    catchContracts_.clear();
    blockDepth_ = 0;
    labels_.clear();
    pendingGotos_.clear();
    beginScope();
    for (std::size_t index = 0; index < op->params.size(); ++index) {
      const auto &param = op->params[index];
      Symbol symbol;
      if (!declare(param.name, signature.parameterByteLengths[index],
                   hir::MemoryStorage::Local, param.templateName, symbol)) {
        addDiagnostic("duplicate impl parameter '" + param.name + "'",
                      currentScopeDeclarationRange(param.name));
        continue;
      }
      currentParameters_.emplace_back(symbol.name, symbol.bindingName,
                                      fixedResult(symbol.templateName,
                                                  symbol.byteLength));
    }

    auto body = analyzeFunctionBody(*op->body, op->returns);
    for (const auto &pending : pendingGotos_) {
      const auto label = labels_.find(pending.label);
      if (label == labels_.end()) {
        addDiagnostic("goto references unknown label '" + pending.label + "'");
      } else if (label->second.blockDepth > pending.blockDepth) {
        addDiagnostic("goto into an inner block is not supported for label '" +
                      pending.label + "'");
      }
    }
    endScope();
    auto parameters = std::move(currentParameters_);
    currentParameters_.clear();
    currentFunction_ = nullptr;

    if (!body || !result_.diagnostics.empty()) {
      return false;
    }

    auto lowered = std::make_unique<hir::Function>(
        info.symbolName, std::move(parameters), info.returnByteLengths,
        std::move(body));
    lowered->linkage = hir::Linkage::Internal;
    lowered->usesViewAbi = true;
    lowered->viewResultByteLength = info.returnByteLengths.front();
    for (std::size_t index = 0; index < lowered->parameters.size(); ++index) {
      lowered->parameters[index].viewIsCopy = op->op != "=" || index != 0U;
    }
    functions.push_back(std::move(lowered));
  }
  return true;
}

bool Analyzer::lowerImplMethodBodies(
    std::vector<std::unique_ptr<hir::Function>> &functions) {
  for (const auto &info : implMethodInfos_) {
    const auto *method = info.declaration;
    if (method == nullptr || info.returnByteLengths.size() > 1U ||
        info.returnByteLengths.size() != info.returnTemplateNames.size()) {
      addDiagnostic("internal error: invalid impl method metadata");
      return false;
    }
    CurrentRangeGuard rangeGuard(*this, *method);

    while (scopes_.size() > 1U) {
      endScope();
    }

    FunctionSignature signature;
    signature.name = info.symbolName;
    signature.parameterByteLengths = info.parameterByteLengths;
    signature.parameterTemplateNames = info.parameterTemplateNames;
    signature.returnByteLengths = info.returnByteLengths;
    signature.returnTemplateNames = info.returnTemplateNames;
    signature.returnHasExplicitUserTemplate.reserve(method->returns.size());
    for (const auto &item : method->returns) {
      signature.returnHasExplicitUserTemplate.push_back(
          !item.templateName.empty() && templates_.contains(item.templateName));
    }
    signature.returnsKnown = true;
    signature.returnsExplicit = true;

    currentFunction_ = &signature;
    currentParameters_.clear();
    loopDepth_ = 0;
    catchContracts_.clear();
    blockDepth_ = 0;
    labels_.clear();
    pendingGotos_.clear();
    beginScope();
    for (std::size_t index = 0; index < method->params.size(); ++index) {
      const auto &param = method->params[index];
      Symbol symbol;
      if (!declare(param.name, signature.parameterByteLengths[index],
                   hir::MemoryStorage::Local, param.templateName, symbol)) {
        addDiagnostic("duplicate impl method parameter '" + param.name + "'",
                      currentScopeDeclarationRange(param.name));
        continue;
      }
      currentParameters_.emplace_back(symbol.name, symbol.bindingName,
                                      fixedResult(symbol.templateName,
                                                  symbol.byteLength));
    }

    auto body = analyzeFunctionBody(*method->body, method->returns);
    for (const auto &pending : pendingGotos_) {
      const auto label = labels_.find(pending.label);
      if (label == labels_.end()) {
        addDiagnostic("goto references unknown label '" + pending.label + "'");
      } else if (label->second.blockDepth > pending.blockDepth) {
        addDiagnostic("goto into an inner block is not supported for label '" +
                      pending.label + "'");
      }
    }
    endScope();
    auto parameters = std::move(currentParameters_);
    currentParameters_.clear();
    currentFunction_ = nullptr;

    if (!body || !result_.diagnostics.empty()) {
      return false;
    }

    auto lowered = std::make_unique<hir::Function>(
        info.symbolName, std::move(parameters), info.returnByteLengths,
        std::move(body));
    lowered->linkage = hir::Linkage::Internal;
    lowered->usesViewAbi = true;
    lowered->viewResultByteLength =
        info.returnByteLengths.empty() ? 0U : info.returnByteLengths.front();
    functions.push_back(std::move(lowered));
  }
  return true;
}

std::unique_ptr<hir::Block> Analyzer::analyzeFunctionBody(
    const ast::BlockStmt &block,
    const std::vector<ast::ReturnItem> &returnItems) {
  CurrentRangeGuard rangeGuard(*this, block);
  currentReturnStorage_.clear();
  currentReturnHasAnonymous_ = false;
  std::vector<std::unique_ptr<hir::Stmt>> statements;
  const bool hasNamedReturn = std::any_of(
      returnItems.begin(), returnItems.end(),
      [](const ast::ReturnItem &item) { return !item.name.empty(); });
  if (hasNamedReturn) {
    for (std::size_t index = 0; index < returnItems.size(); ++index) {
      const auto &item = returnItems[index];
      const auto length = currentFunction_->returnByteLengths[index];
      Symbol symbol;
      if (!item.name.empty()) {
        if (!declare(item.name, length, hir::MemoryStorage::Local,
                     item.templateName, symbol)) {
          addDiagnostic("duplicate named return '" + item.name + "'",
                        currentScopeDeclarationRange(item.name));
          continue;
        }
      } else {
        // Mixed signatures need private slots too: explicit return assignment
        // proceeds in item order, and a later expression may observe an earlier
        // named slot. These compiler-only names never enter lexical lookup.
        currentReturnHasAnonymous_ = true;
        const auto name = "$return." + std::to_string(index);
        symbol = Symbol{name, makeBindingName(name), length,
                        hir::MemoryStorage::Local, item.templateName,
                        currentRange_};
      }
      currentReturnStorage_.push_back(symbol);
      // Standard 12.2: return storage is uninitialized until explicitly written.
      statements.push_back(std::make_unique<hir::LocalMemory>(
          symbol.name, symbol.bindingName, symbol.byteLength, symbol.storage,
          symbol.templateName));
    }
  }
  // Parameters and named returns share the function-body lexical scope.
  // Ordinary nested blocks continue to create their own scopes in analyze().
  ++blockDepth_;
  if (result_.diagnostics.empty()) {
    for (const auto &statement : block.statements) {
      auto lowered = analyze(*statement);
      if (lowered) {
        statements.push_back(std::move(lowered));
      }
    }
  }
  --blockDepth_;
  currentReturnStorage_.clear();
  currentReturnHasAnonymous_ = false;
  return std::make_unique<hir::Block>(std::move(statements));
}

std::unique_ptr<hir::Block> Analyzer::analyze(const ast::BlockStmt &block) {
  CurrentRangeGuard rangeGuard(*this, block);
  beginScope();
  ++blockDepth_;
  std::vector<std::unique_ptr<hir::Stmt>> statements;
  for (const auto &statement : block.statements) {
    auto lowered = analyze(*statement);
    if (lowered) {
      statements.push_back(std::move(lowered));
    }
  }
  --blockDepth_;
  endScope();
  return std::make_unique<hir::Block>(std::move(statements));
}

} // namespace hitsimple::sema
