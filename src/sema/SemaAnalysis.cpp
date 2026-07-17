#include "SemaAnalyzer.h"

#include <utility>

namespace hitsimple::sema {

AnalyzeResult Analyzer::analyze(const ast::TranslationUnit &unit,
                                const AnalyzeOptions &options) {
  currentRange_.reset();
  hir::setActiveSourceRange(std::nullopt);
  std::vector<std::unique_ptr<hir::Function>> functions;
  std::vector<hir::ExternFunction> externFunctions;
  scopes_.clear();
  bindingCounts_.clear();
  functions_.clear();
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
  standardHeaders_ = options.standardHeaders;
  cCompatibilityMode_ = options.cCompatibilityMode;
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

  for (const auto *globalNew : unit.globalNews) {
    CurrentRangeGuard rangeGuard(*this, *globalNew);
    if (!registerTopLevelName(globalNew->name)) {
      continue;
    }
    if (globalNew->templateName == "none") {
      addDiagnostic("declaration template for '" + globalNew->name +
                    "' cannot be none");
      continue;
    }

    std::string templateName = globalNew->templateName;
    if (templateName.empty() && globalNew->initializer &&
        isHandleExpression(*globalNew->initializer)) {
      templateName = "handle";
    }

    std::size_t length = 0;
    if (!globalNew->length.empty()) {
      const auto parsedLength =
          parseDeclaredLength(globalNew->length, templateName);
      if (!parsedLength) {
        continue;
      }
      length = *parsedLength;
    } else if (!templateName.empty()) {
      const auto inferred = templateByteLength(templateName);
      if (inferred) {
        length = *inferred;
      }
    }
    if (length == 0 && globalNew->initializer) {
      const auto inferred = inferByteLength(*globalNew->initializer);
      if (inferred) {
        length = *inferred;
      }
    }
    if (length == 0) {
      addDiagnostic("invalid byte length for '" + globalNew->name + "'");
      continue;
    }

    Symbol symbol;
    if (!declare(globalNew->name, length, hir::MemoryStorage::Global,
                 std::move(templateName), symbol)) {
      addDiagnostic("duplicate declaration '" + globalNew->name + "'");
      continue;
    }
    globals_.emplace_back(symbol.name, symbol.bindingName, symbol.byteLength);
  }
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
      addDiagnostic("duplicate declaration '" + externVariable->name + "'");
      continue;
    }
    globals_.emplace_back(symbol.name, symbol.bindingName, symbol.byteLength,
                          true);
  }
  (void)collectFunctionSignatures(unit, externFunctions);

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
  return std::move(result_);
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
      addDiagnostic("duplicate parameter '" + param.name + "'");
      continue;
    }
    currentParameters_.emplace_back(symbol.name, symbol.bindingName,
                                    symbol.byteLength);
  }

  auto body = analyze(*function.body);
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
        addDiagnostic("duplicate impl parameter '" + param.name + "'");
        continue;
      }
      currentParameters_.emplace_back(symbol.name, symbol.bindingName,
                                      symbol.byteLength);
    }

    auto body = analyze(*op->body);
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
        addDiagnostic("duplicate impl method parameter '" + param.name + "'");
        continue;
      }
      currentParameters_.emplace_back(symbol.name, symbol.bindingName,
                                      symbol.byteLength);
    }

    auto body = analyze(*method->body);
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
    lowered->viewParametersAreCopies = true;
    functions.push_back(std::move(lowered));
  }
  return true;
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
