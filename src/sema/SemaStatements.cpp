#include "SemaAnalyzer.h"

#include "hitsimple/literal/Literal.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <unordered_set>
#include <utility>

namespace hitsimple::sema {
namespace {

bool isFloatTemplate(std::string_view name) {
  return name.size() >= 2 && name.front() == 'f' &&
         parseByteLength(name.substr(1)) != 0;
}

bool isUnsignedTemplate(std::string_view name) {
  return name.size() >= 2 && name.front() == 'u' &&
         parseByteLength(name.substr(1)) != 0;
}

bool isReservedIdentifier(std::string_view name) {
  if (name == "_") {
    return true;
  }
  if (name.size() >= 2 && name.front() == 't') {
    return std::all_of(name.begin() + 1, name.end(), [](char ch) {
      return std::isdigit(static_cast<unsigned char>(ch));
    });
  }
  return false;
}

} // namespace

std::unique_ptr<hir::Stmt> Analyzer::analyze(const ast::Stmt &statement) {
  CurrentRangeGuard rangeGuard(*this, statement);
  if (const auto *decl = dynamic_cast<const ast::NewDecl *>(&statement)) {
    return analyze(*decl);
  }
  if (const auto *decl = dynamic_cast<const ast::VarDeclStmt *>(&statement)) {
    return analyze(*decl);
  }
  if (const auto *assign = dynamic_cast<const ast::AssignStmt *>(&statement)) {
    return analyze(*assign);
  }
  if (const auto *expr = dynamic_cast<const ast::ExprStmt *>(&statement)) {
    return analyze(*expr);
  }
  if (const auto *ret = dynamic_cast<const ast::ReturnStmt *>(&statement)) {
    return analyze(*ret);
  }
  if (const auto *ifStmt = dynamic_cast<const ast::IfStmt *>(&statement)) {
    return analyze(*ifStmt);
  }
  if (const auto *whileStmt =
          dynamic_cast<const ast::WhileStmt *>(&statement)) {
    return analyze(*whileStmt);
  }
  if (const auto *forStmt = dynamic_cast<const ast::ForStmt *>(&statement)) {
    return analyze(*forStmt);
  }
  if (dynamic_cast<const ast::BreakStmt *>(&statement) != nullptr) {
    return analyzeBreak();
  }
  if (dynamic_cast<const ast::ContinueStmt *>(&statement) != nullptr) {
    return analyzeContinue();
  }
  if (const auto *gotoStmt = dynamic_cast<const ast::GotoStmt *>(&statement)) {
    return analyze(*gotoStmt);
  }
  if (const auto *labelStmt =
          dynamic_cast<const ast::LabelStmt *>(&statement)) {
    return analyze(*labelStmt);
  }
  if (const auto *throwStmt =
          dynamic_cast<const ast::ThrowStmt *>(&statement)) {
    return analyze(*throwStmt);
  }
  if (const auto *tryCatch =
          dynamic_cast<const ast::TryCatchStmt *>(&statement)) {
    return analyze(*tryCatch);
  }
  if (const auto *setStmt = dynamic_cast<const ast::SetStmt *>(&statement)) {
    return analyze(*setStmt);
  }

  addDiagnostic("unsupported statement");
  return nullptr;
}

std::unique_ptr<hir::Stmt> Analyzer::analyze(const ast::NewDecl &decl) {
  if (isReservedIdentifier(decl.name)) {
    addDiagnostic("reserved identifier '" + decl.name +
                  "' cannot be used as a declaration name");
    return nullptr;
  }
  const auto length = parseByteLength(decl.length);
  if (length == 0) {
    addDiagnostic("invalid byte length for '" + decl.name + "'");
    return nullptr;
  }

  Symbol symbol;
  if (!declare(decl.name, length, hir::MemoryStorage::Local, symbol)) {
    addDiagnostic("duplicate declaration '" + decl.name + "'",
                  currentScopeDeclarationRange(decl.name));
    return nullptr;
  }
  return std::make_unique<hir::LocalMemory>(symbol.name, symbol.bindingName,
                                            symbol.byteLength, symbol.storage);
}

std::unique_ptr<hir::Stmt> Analyzer::analyze(const ast::VarDeclStmt &decl) {
  if (decl.storage != "new" && decl.storage != "static") {
    addDiagnostic("unsupported declaration storage '" + decl.storage + "'");
    return nullptr;
  }

  std::vector<std::unique_ptr<hir::Stmt>> statements;
  for (const auto &item : decl.items) {
    auto lowered = analyzeDeclItem(item, decl.storage);
    if (lowered) {
      statements.push_back(std::move(lowered));
    }
  }

  if (statements.size() == 1U) {
    return std::move(statements.front());
  }
  return std::make_unique<hir::StatementList>(std::move(statements));
}

std::unique_ptr<hir::Stmt> Analyzer::analyze(const ast::SetStmt &statement) {
  const auto *identifier = dynamic_cast<const ast::IdentifierExpr *>(
      statement.target.get());
  if (identifier == nullptr &&
      dynamic_cast<const ast::MemberExpr *>(statement.target.get()) == nullptr) {
    addDiagnostic("set target must be a name or member chain");
    return nullptr;
  }

  if (identifier == nullptr) {
    std::vector<std::string_view> path;
    const ast::Expr *base = statement.target.get();
    while (const auto *member = dynamic_cast<const ast::MemberExpr *>(base)) {
      path.push_back(member->member);
      base = member->base.get();
    }
    const auto *root = dynamic_cast<const ast::IdentifierExpr *>(base);
    if (root == nullptr) {
      addDiagnostic("set target must be a name or member chain");
      return nullptr;
    }

    const auto reference = resolveMemoryReference(*statement.target);
    if (!reference) {
      return nullptr;
    }
    if (statement.templateName != "none" &&
        statement.templateName != "bytes" && statement.templateName != "cstr") {
      const auto templateLength = templateByteLength(statement.templateName);
      if (!templateLength) {
        addDiagnostic("unknown template '" + statement.templateName + "'");
        return nullptr;
      }
      if (*templateLength != reference->byteLength) {
        addDiagnostic("set template byte length does not match target");
        return nullptr;
      }
    }

    std::reverse(path.begin(), path.end());
    std::string key = reference->bindingName;
    for (const auto segment : path) {
      key += '\x1f';
      key += segment;
    }
    memberTemplateOverrides_[std::move(key)] =
        statement.templateName == "none"
            ? std::optional<std::string>{}
            : std::optional<std::string>{statement.templateName};
    return std::make_unique<hir::StatementList>(
        std::vector<std::unique_ptr<hir::Stmt>>());
  }

  auto *symbol = lookup(identifier->name);
  if (symbol == nullptr) {
    addDiagnostic("use of undeclared variable '" + identifier->name + "'");
    return nullptr;
  }

  if (symbol->templateName == "handle" && statement.templateName != "handle") {
    addDiagnostic("handle value '" + identifier->name +
                  "' cannot be rebound to another template");
    return nullptr;
  }
  if (symbol->templateName != "handle" && statement.templateName == "handle") {
    addDiagnostic("only an existing handle value can be rebound as handle");
    return nullptr;
  }

  if (statement.templateName == "none") {
    symbol->templateName.clear();
    userTemplateBindings_.erase(symbol->bindingName);
    return std::make_unique<hir::StatementList>(
        std::vector<std::unique_ptr<hir::Stmt>>());
  }

  if (statement.templateName != "bytes" && statement.templateName != "cstr" &&
      !templateByteLength(statement.templateName)) {
    addDiagnostic("unknown template '" + statement.templateName + "'");
    return nullptr;
  }
  if (statement.templateName != "bytes" && statement.templateName != "cstr" &&
      *templateByteLength(statement.templateName) != symbol->byteLength) {
    addDiagnostic("set template byte length does not match target");
    return nullptr;
  }
  symbol->templateName = statement.templateName;
  if (templates_.contains(symbol->templateName)) {
    userTemplateBindings_[symbol->bindingName] = symbol->templateName;
  } else {
    userTemplateBindings_.erase(symbol->bindingName);
  }
  return std::make_unique<hir::StatementList>(
      std::vector<std::unique_ptr<hir::Stmt>>());
}

std::unique_ptr<hir::Stmt> Analyzer::analyzeDeclItem(const ast::DeclItem &item,
                                                     std::string_view storage) {
  if (isReservedIdentifier(item.name)) {
    addDiagnostic("reserved identifier '" + item.name +
                  "' cannot be used as a declaration name");
    return nullptr;
  }
  if (item.templateName == "none") {
    addDiagnostic("declaration template for '" + item.name +
                  "' cannot be none");
    return nullptr;
  }

  std::string templateName = item.templateName;
  if (templateName.empty() && item.initializer &&
      isHandleExpression(*item.initializer)) {
    templateName = "handle";
  }
  if (templateName.empty() && item.initializer) {
    const auto initializerTemplate = operatorTemplateName(*item.initializer);
    if (initializerTemplate && *initializerTemplate == "addr") {
      templateName = "addr";
    }
  }

  std::size_t length = 0;
  if (!item.length.empty()) {
    const auto parsedLength = parseDeclaredLength(item.length, templateName);
    if (!parsedLength) {
      return nullptr;
    }
    length = *parsedLength;
  } else if (!templateName.empty()) {
    const auto inferred = templateByteLength(templateName);
    if (inferred) {
      length = *inferred;
    }
  }
  if (length == 0 && item.length.empty() && item.initializer) {
    if (const auto *call = dynamic_cast<const ast::CallExpr *>(
            item.initializer.get());
        call != nullptr && stdlib::isRemovedLegacyName(call->callee)) {
      addDiagnostic("legacy standard library name '" + call->callee +
                    "' is not accepted; use " +
                    std::string(stdlib::replacementForRemovedLegacyName(
                        call->callee)));
      return nullptr;
    }
    const auto inferred = inferByteLength(*item.initializer);
    if (inferred) {
      length = *inferred;
    }
  }
  if (length == 0) {
    addDiagnostic("invalid byte length for '" + item.name + "'");
    return nullptr;
  }
  if (templateName.empty() &&
      floatAssignmentByteLength(item.assignmentOp).has_value()) {
    templateName = "f" + std::to_string(length * 8U);
  }

  const auto memoryStorage = storage == "static"
                                 ? hir::MemoryStorage::StaticLocal
                                 : hir::MemoryStorage::Local;
  Symbol symbol;
  if (!declare(item.name, length, memoryStorage, std::move(templateName), symbol)) {
    addDiagnostic("duplicate declaration '" + item.name + "'",
                  currentScopeDeclarationRange(item.name));
    return nullptr;
  }

  std::vector<std::unique_ptr<hir::Stmt>> statements;
  statements.push_back(std::make_unique<hir::LocalMemory>(
      symbol.name, symbol.bindingName, symbol.byteLength, symbol.storage,
      symbol.templateName));

  // A declaration initializer is an ordinary assignment to freshly-created
  // storage.  Keep it on the same matcher as assignment statements so the
  // standard matrix, conversion plan, and user-template rules cannot drift.
  if (item.initializer) {
    const MemoryReference target{symbol.name, symbol.bindingName,
                                 symbol.byteLength, symbol.storage, 0,
                                 symbol.templateName};
    ast::IdentifierExpr targetExpression(symbol.name);
    auto store = lowerAssignmentTarget(
        targetExpression, item.assignmentOp, isUnsignedTemplate(symbol.templateName),
        *item.initializer, nullptr, &target, "initializer", "target");
    if (!store) {
      return std::make_unique<hir::StatementList>(std::move(statements));
    }
    if (memoryStorage == hir::MemoryStorage::StaticLocal) {
      store->staticInitializationBinding = symbol.bindingName;
    }
    statements.push_back(std::move(store));
    if (statements.size() == 1U) {
      return std::move(statements.front());
    }
    return std::make_unique<hir::StatementList>(std::move(statements));
  }

  if (statements.size() == 1U) {
    return std::move(statements.front());
  }
  return std::make_unique<hir::StatementList>(std::move(statements));
}

std::unique_ptr<hir::Stmt>
Analyzer::analyzeStringInitializer(const ast::DeclItem &item,
                                   const Symbol &target) {
  const auto *string =
      dynamic_cast<const ast::StringLiteral *>(item.initializer.get());
  if (string == nullptr) {
    addDiagnostic("right operand of '%s=' is not a string literal");
    return nullptr;
  }
  const auto decoded = literal::decodeStringLiteral(string->value);
  if (!decoded) {
    addDiagnostic("invalid string literal '" + string->value +
                  "': " + *decoded.error);
    return nullptr;
  }
  return std::make_unique<hir::StringStore>(target.name, target.bindingName,
                                            target.byteLength, target.storage,
                                            string->value);
}

std::unique_ptr<hir::Stmt>
Analyzer::analyzeBoolInitializer(const ast::DeclItem &item,
                                 const Symbol &target) {
  auto value = analyze(*item.initializer);
  if (!value) {
    return nullptr;
  }
  if (!isIntegerExpression(*value)) {
    addDiagnostic("right operand of '%b=' is not an integer expression");
    return nullptr;
  }
  return std::make_unique<hir::BoolStore>(target.name, target.bindingName,
                                          target.byteLength, target.storage,
                                          std::move(value));
}

std::unique_ptr<hir::Stmt> Analyzer::analyze(const ast::ExprStmt &statement) {
  return analyzeExpressionStatementExpr(*statement.expression);
}

std::unique_ptr<hir::Stmt>
Analyzer::analyzeExpressionStatementExpr(const ast::Expr &expression) {
  if (const auto *call = dynamic_cast<const ast::CallExpr *>(&expression)) {
    return analyzeCallStatement(*call);
  }
  if (const auto *call = dynamic_cast<const ast::MethodCallExpr *>(&expression)) {
    auto lowered = lowerImplMethodCall(*call);
    if (!lowered) {
      return nullptr;
    }
    return std::make_unique<hir::UserTemplateOpCall>(
        lowered->method->symbolName, std::move(lowered->arguments),
        lowered->method->returnByteLengths.empty()
            ? 0U
            : lowered->method->returnByteLengths.front());
  }
  if (const auto *assign =
          dynamic_cast<const ast::AssignmentExpr *>(&expression)) {
    auto lowered = lowerAssignmentExpression(*assign);
    if (!lowered) {
      return nullptr;
    }
    if (lowered->stores.size() == 1U) {
      return std::move(lowered->stores.front());
    }
    return std::make_unique<hir::StatementList>(std::move(lowered->stores));
  }
  if (const auto *unary = dynamic_cast<const ast::UnaryExpr *>(&expression)) {
    return analyzeIncrementStatement(*unary);
  }

  addDiagnostic("unsupported expression statement");
  return nullptr;
}

std::unique_ptr<hir::Stmt>
Analyzer::analyzeIncrementStatement(const ast::UnaryExpr &expression) {
  if (expression.op != "post++" && expression.op != "post--") {
    addDiagnostic("unsupported expression statement");
    return nullptr;
  }
  auto target = analyze(*expression.operand);
  if (!target) {
    return nullptr;
  }
  if (!target->result.isAddressable || !target->result.isMutableLValue) {
    addDiagnostic("increment target must be a writable lvalue");
    return nullptr;
  }
  if (!hir::isIntegerNumeric(target->result)) {
    addDiagnostic("increment target must use an integer View");
    return nullptr;
  }
  const auto byteLength = target->result.staticByteLength;
  const bool unsignedTarget =
      target->result.category == hir::ViewCategory::UnsignedInteger ||
      target->result.integerInterpretation == hir::IntegerInterpretation::Unsigned;
  const auto typedOperator = "%" + std::to_string(byteLength) +
                             (unsignedTarget ? "u" : "d") +
                             (expression.op == "post++" ? "+=" : "-=");

  ast::IntegerLiteral one("1");
  return lowerAssignmentTarget(
      *expression.operand, typedOperator, unsignedTarget, one,
      std::make_unique<hir::IntegerLiteral>("1", signedIntegerResult(1)));
}

std::unique_ptr<hir::Stmt> Analyzer::analyze(const ast::ReturnStmt &statement) {
  std::vector<std::unique_ptr<hir::Expr>> values;
  std::vector<std::size_t> byteLengths;
  std::vector<std::string> inferredTemplateNames;
  std::vector<hir::ConversionPlan> conversionPlans;
  if (currentFunction_ != nullptr && currentFunction_->returnsKnown &&
      currentFunction_->returnByteLengths.size() != statement.values.size()) {
    addDiagnostic("return value count does not match function signature");
    return nullptr;
  }
  for (std::size_t index = 0; index < statement.values.size(); ++index) {
    const auto &value = statement.values[index];
    const bool valueIsHandle = isHandleExpression(*value);
    const bool expectsHandle =
        currentFunction_ != nullptr &&
        index < currentFunction_->returnTemplateNames.size() &&
        currentFunction_->returnTemplateNames[index] == "handle";
    if (currentFunction_ != nullptr && currentFunction_->returnsKnown &&
        expectsHandle != valueIsHandle) {
      if (expectsHandle) {
        addDiagnostic("return value must be a handle for the function signature");
      } else {
        addDiagnostic("handle return value requires a handle return signature");
      }
      return nullptr;
    }
    const std::string_view returnTemplate =
        currentFunction_ != nullptr &&
                index < currentFunction_->returnTemplateNames.size()
            ? std::string_view(currentFunction_->returnTemplateNames[index])
            : std::string_view{};
    const bool hasExplicitUserTemplateReturn =
        currentFunction_ != nullptr &&
        index < currentFunction_->returnHasExplicitUserTemplate.size() &&
        currentFunction_->returnHasExplicitUserTemplate[index];
    if (hasExplicitUserTemplateReturn) {
      const auto compatibility = userTemplateViewAssignmentCompatibility(
          returnTemplate, *value);
      if (compatibility ==
          UserTemplateViewAssignmentCompatibility::SourceIsNotUserTemplate) {
        addDiagnostic("user template return requires a user template source");
        return nullptr;
      }
      if (compatibility ==
          UserTemplateViewAssignmentCompatibility::TemplateMismatch) {
        addDiagnostic("user template return requires matching templates");
        return nullptr;
      }
    }
    const bool expectsFloat =
        currentFunction_ != nullptr &&
        index < currentFunction_->returnTemplateNames.size() &&
        isFloatTemplate(currentFunction_->returnTemplateNames[index]);
    const bool expectsUserTemplate =
        currentFunction_ != nullptr &&
        index < currentFunction_->returnTemplateNames.size() &&
        templates_.contains(currentFunction_->returnTemplateNames[index]);
    const auto expectedLength =
        currentFunction_ != nullptr && currentFunction_->returnsKnown
            ? currentFunction_->returnByteLengths[index]
            : std::size_t{0};
    auto lowered = expectsFloat ? analyzeFloatOperand(*value, expectedLength)
                                : analyze(*value);
    if (!lowered) {
      return nullptr;
    }
    if (hasRuntimeDynamicView(*lowered)) {
      addDiagnostic("dynamic View cannot be returned through a fixed function "
                    "signature");
      return nullptr;
    }
    const auto byteLength = lowered->result.lengthKind ==
                                    hir::ViewLengthKind::Static
                                ? lowered->result.staticByteLength
                                : std::size_t{0};
    const bool expectsExactView =
        currentFunction_ != nullptr && currentFunction_->returnsKnown &&
        (returnTemplate == "bytes" || returnTemplate == "cstr" ||
         returnTemplate == "handle");
    if (expectsExactView &&
        (lowered->result.templateName != returnTemplate ||
         byteLength != expectedLength)) {
      addDiagnostic("return value does not exactly match function View "
                    "signature");
      return nullptr;
    }
    if (currentFunction_ != nullptr && currentFunction_->returnsKnown &&
        !expectsFloat && !expectsUserTemplate && !expectsExactView &&
        !isIntegerExpression(*lowered)) {
      addDiagnostic("return value is not an integer expression");
      return nullptr;
    }
    if (expectsUserTemplate &&
        (lowered->result.templateName != returnTemplate ||
         byteLength != expectedLength)) {
      addDiagnostic("return value does not match user template signature");
      return nullptr;
    }
    if (currentFunction_ != nullptr && currentFunction_->returnsKnown &&
        currentFunction_->returnsExplicit) {
      const auto expected = currentFunction_->returnByteLengths[index];
      if (!currentFunction_->isCAbi && byteLength > expected && expected < 8) {
        addDiagnostic("return value byte length does not fit function "
                      "signature");
        return nullptr;
      }
      const auto source = lowered->result;
      const auto destination = fixedResult(std::string(returnTemplate), expected);
      const bool sameViewSemantics =
          source.category == destination.category &&
          source.integerInterpretation == destination.integerInterpretation &&
          source.lengthKind == destination.lengthKind &&
          source.staticByteLength == destination.staticByteLength &&
          source.templateName == destination.templateName;
      const auto conversion = expectsFloat
                                  ? hir::ConversionKind::Floating
                                  : expectsUserTemplate
                                        ? hir::ConversionKind::UserTemplateAssignment
                                        : expectsExactView
                                              ? hir::ConversionKind::Identity
                                        : sameViewSemantics
                                              ? hir::ConversionKind::Identity
                                              : hir::ConversionKind::IntegerWidth;
      conversionPlans.push_back(
          hir::ConversionPlan{conversion, source, destination});
      if (!expectsFloat && !expectsUserTemplate && !expectsExactView &&
          source.staticByteLength != expected) {
        const bool destinationSigned =
            returnTemplate.empty() || returnTemplate == "none" ||
            returnTemplate.starts_with('i');
        lowered = std::make_unique<hir::IntegerCastExpr>(
            std::move(lowered), destinationSigned,
            destinationSigned ? signedIntegerResult(expected)
                              : unsignedIntegerResult(expected));
      }
      byteLengths.push_back(expected);
    } else {
      byteLengths.push_back(byteLength);
    }
    values.push_back(std::move(lowered));
    inferredTemplateNames.push_back(values.back()->result.templateName);
  }
  if (currentFunction_ != nullptr && !currentFunction_->returnsKnown) {
    currentFunction_->returnTemplateNames = std::move(inferredTemplateNames);
    currentFunction_->returnHasExplicitUserTemplate.clear();
    currentFunction_->returnHasExplicitUserTemplate.reserve(
        currentFunction_->returnTemplateNames.size());
    for (const auto &templateName : currentFunction_->returnTemplateNames) {
      currentFunction_->returnHasExplicitUserTemplate.push_back(
          templates_.contains(templateName));
    }
  }
  if (!registerReturnLengths(byteLengths)) {
    return nullptr;
  }
  return std::make_unique<hir::Return>(std::move(values),
                                       std::move(conversionPlans));
}

std::unique_ptr<hir::Stmt> Analyzer::analyze(const ast::IfStmt &statement) {
  auto condition = analyze(*statement.condition);
  if (!condition) {
    return nullptr;
  }
  if (!hir::isBooleanTestable(condition->result)) {
    addDiagnostic("if condition cannot form a View");
    return nullptr;
  }

  const auto entryFacts = addressFacts_;
  auto thenBlock = analyze(*statement.thenBlock);
  const auto thenFacts = addressFacts_;
  addressFacts_ = entryFacts;
  std::unique_ptr<hir::Block> elseBlock;
  if (statement.elseBlock) {
    elseBlock = analyze(*statement.elseBlock);
  }
  const auto elseFacts = addressFacts_;
  mergeAddressFacts(thenFacts, elseFacts);

  return std::make_unique<hir::If>(
      std::make_unique<hir::BooleanTestExpr>(std::move(condition),
                                             booleanResult()),
      std::move(thenBlock), std::move(elseBlock));
}

std::unique_ptr<hir::Stmt> Analyzer::analyze(const ast::WhileStmt &statement) {
  auto condition = analyze(*statement.condition);
  if (!condition) {
    return nullptr;
  }
  if (!hir::isBooleanTestable(condition->result)) {
    addDiagnostic("while condition cannot form a View");
    return nullptr;
  }

  const auto entryFacts = addressFacts_;
  ++loopDepth_;
  auto body = analyze(*statement.body);
  --loopDepth_;
  mergeAddressFacts(entryFacts, addressFacts_);

  return std::make_unique<hir::While>(
      std::make_unique<hir::BooleanTestExpr>(std::move(condition),
                                             booleanResult()),
      std::move(body));
}

std::unique_ptr<hir::Stmt> Analyzer::analyze(const ast::ForStmt &statement) {
  beginScope();
  ++blockDepth_;

  std::unique_ptr<hir::Stmt> init;
  if (statement.init) {
    init = analyze(*statement.init);
  }

  std::unique_ptr<hir::Expr> condition;
  if (statement.condition) {
    condition = analyze(*statement.condition);
    if (!condition) {
      --blockDepth_;
      endScope();
      return nullptr;
    }
    if (!hir::isBooleanTestable(condition->result)) {
      addDiagnostic("for condition cannot form a View");
      --blockDepth_;
      endScope();
      return nullptr;
    }
    condition = std::make_unique<hir::BooleanTestExpr>(
        std::move(condition), booleanResult());
  }

  const auto entryFacts = addressFacts_;
  ++loopDepth_;
  auto body = analyze(*statement.body);
  --loopDepth_;

  std::vector<std::unique_ptr<hir::Stmt>> post;
  for (const auto &expr : statement.post) {
    auto lowered = analyzeExpressionStatementExpr(*expr);
    if (lowered) {
      post.push_back(std::move(lowered));
    }
  }
  mergeAddressFacts(entryFacts, addressFacts_);

  --blockDepth_;
  endScope();
  return std::make_unique<hir::For>(std::move(init), std::move(condition),
                                    std::move(post), std::move(body));
}

std::unique_ptr<hir::Stmt> Analyzer::analyzeBreak() {
  if (loopDepth_ == 0) {
    addDiagnostic("break used outside of a loop");
    return nullptr;
  }
  return std::make_unique<hir::Break>();
}

std::unique_ptr<hir::Stmt> Analyzer::analyzeContinue() {
  if (loopDepth_ == 0) {
    addDiagnostic("continue used outside of a loop");
    return nullptr;
  }
  return std::make_unique<hir::Continue>();
}

std::unique_ptr<hir::Stmt> Analyzer::analyze(const ast::GotoStmt &statement) {
  pendingGotos_.push_back(PendingGoto{statement.label, blockDepth_});
  return std::make_unique<hir::Goto>(statement.label);
}

std::unique_ptr<hir::Stmt> Analyzer::analyze(const ast::LabelStmt &statement) {
  const auto [found, inserted] =
      labels_.emplace(statement.label, LabelInfo{blockDepth_, currentRange_});
  if (!inserted) {
    addDiagnostic("duplicate label '" + statement.label + "'",
                  found->second.declarationRange);
    return nullptr;
  }
  auto lowered = analyze(*statement.statement);
  if (!lowered) {
    return nullptr;
  }
  return std::make_unique<hir::Label>(statement.label, std::move(lowered));
}

std::unique_ptr<hir::Stmt> Analyzer::analyze(const ast::ThrowStmt &statement) {
  if (isHandleExpression(*statement.value)) {
    addDiagnostic("handle values cannot be thrown");
    return nullptr;
  }

  const CatchViewContract *target =
      catchContracts_.empty() ? nullptr : &catchContracts_.back();
  std::string sourceTemplate =
      operatorTemplateName(*statement.value).value_or("");
  if (target != nullptr && templates_.contains(sourceTemplate) &&
      !templates_.contains(target->templateName)) {
    addDiagnostic("throw View template '" + sourceTemplate +
                  "' does not match catch template '" + target->templateName +
                  "'");
    return nullptr;
  }

  if (target == nullptr) {
    auto value = analyze(*statement.value);
    if (!value) {
      return nullptr;
    }
    const auto sourceLength = inferByteLength(*statement.value);
    if (!sourceLength || *sourceLength == 0) {
      addDiagnostic("throw value requires a fixed positive byte length");
      return nullptr;
    }
    if (dynamic_cast<const ast::FloatLiteral *>(statement.value.get()) != nullptr) {
      sourceTemplate = "f" + std::to_string(*sourceLength * 8U);
    }
    return std::make_unique<hir::Throw>(
        nullptr, std::move(sourceTemplate), *sourceLength, "", 0);
  }

  const MemoryReference catchTarget{target->errorName, target->errorBindingName,
                                    target->byteLength,
                                    hir::MemoryStorage::Local, 0,
                                    target->templateName};
  ast::IdentifierExpr catchTargetExpression(target->errorName);
  auto delivery = lowerAssignmentTarget(
      catchTargetExpression, "=", false, *statement.value, nullptr,
      &catchTarget, "throw View", "catch");
  if (!delivery) {
    return nullptr;
  }
  if (target->templateName == "cstr") {
    sourceTemplate = "cstr";
  } else if (dynamic_cast<const ast::FloatLiteral *>(statement.value.get()) !=
             nullptr) {
    sourceTemplate = "f64";
  }

  return std::make_unique<hir::Throw>(
      std::move(delivery), std::move(sourceTemplate), target->byteLength,
      target->templateName, target->byteLength);
}

std::unique_ptr<hir::Stmt>
Analyzer::analyze(const ast::TryCatchStmt &statement) {
  std::optional<std::size_t> errorLength;
  if (statement.errorLength.empty()) {
    errorLength = templateByteLength(statement.errorTemplateName);
    if (!errorLength) {
      addDiagnostic("catch error '" + statement.errorName +
                    "' requires an explicit byte length");
      return nullptr;
    }
  } else {
    errorLength =
        parseDeclaredLength(statement.errorLength, statement.errorTemplateName);
    if (!errorLength) {
      return nullptr;
    }
  }
  if (*errorLength == 0) {
    addDiagnostic("invalid catch error byte length for '" +
                  statement.errorName + "'");
    return nullptr;
  }
  if (const auto templateLength =
          templateByteLength(statement.errorTemplateName);
      templateLength && *templateLength != *errorLength) {
    addDiagnostic("catch error byte length does not match template '" +
                  statement.errorTemplateName + "'");
    return nullptr;
  }

  const std::string errorBindingName = makeBindingName(statement.errorName);
  CatchViewContract contract{statement.errorName, errorBindingName,
                              statement.errorTemplateName, *errorLength};
  catchContracts_.push_back(contract);
  auto tryBlock = analyze(*statement.tryBlock);
  catchContracts_.pop_back();

  beginScope();
  Symbol errorSymbol;
  if (!declare(statement.errorName, *errorLength, hir::MemoryStorage::Local,
               statement.errorTemplateName, errorSymbol, errorBindingName)) {
    addDiagnostic("duplicate catch error '" + statement.errorName + "'",
                  currentScopeDeclarationRange(statement.errorName));
    endScope();
    return nullptr;
  }
  auto catchBlock = analyze(*statement.catchBlock);
  endScope();

  return std::make_unique<hir::TryCatch>(
      std::move(tryBlock), errorSymbol.name, errorSymbol.bindingName,
      errorSymbol.templateName, errorSymbol.byteLength, std::move(catchBlock));
}

} // namespace hitsimple::sema
