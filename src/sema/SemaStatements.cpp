#include "SemaAnalyzer.h"

#include "hitsimple/literal/Literal.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <utility>

namespace hitsimple::sema {
namespace {

bool isLoweredFloatByteLength(std::size_t byteLength) {
  return byteLength == 2 || byteLength == 4 || byteLength == 8 ||
         byteLength == 16;
}

bool isFloatTemplate(std::string_view name) {
  return name.size() >= 2 && name.front() == 'f' &&
         parseByteLength(name.substr(1)) != 0;
}

bool isUnsignedTemplate(std::string_view name) {
  return name.size() >= 2 && name.front() == 'u' &&
         parseByteLength(name.substr(1)) != 0;
}

bool isSignedTemplate(std::string_view name) {
  return name.size() >= 2 && name.front() == 'i' &&
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

bool isI64MinLiteral(const ast::Expr &expr) {
  const auto *unary = dynamic_cast<const ast::UnaryExpr *>(&expr);
  if (unary == nullptr || unary->op != "-") {
    return false;
  }
  const auto *integer =
      dynamic_cast<const ast::IntegerLiteral *>(unary->operand.get());
  return integer != nullptr && integer->value == "9223372036854775808";
}

std::unique_ptr<hir::Expr> lowerI64MinLiteral() {
  return std::make_unique<hir::UnaryExpr>(
      "-", std::make_unique<hir::IntegerLiteral>("9223372036854775808", 8), 8);
}

bool unsignedIntegerFits(const ast::IntegerLiteral &integer,
                         std::size_t byteLength) {
  const auto parsed = literal::parseUnsignedIntegerLiteral(integer.value);
  if (!parsed || byteLength == 0 || byteLength > 8) {
    return false;
  }
  if (byteLength == 8) {
    return true;
  }
  const auto max = (std::uint64_t{1} << (byteLength * 8U)) - 1U;
  return *parsed <= max;
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
    addDiagnostic("duplicate declaration '" + decl.name + "'");
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
    addDiagnostic("duplicate declaration '" + item.name + "'");
    return nullptr;
  }

  std::vector<std::unique_ptr<hir::Stmt>> statements;
  statements.push_back(std::make_unique<hir::LocalMemory>(
      symbol.name, symbol.bindingName, symbol.byteLength, symbol.storage,
      symbol.templateName));

  if (item.initializer) {
    const bool initializerIsHandle = isHandleExpression(*item.initializer);
    if (symbol.templateName == "handle") {
      if (item.assignmentOp != "=") {
        addDiagnostic("handle target '" + item.name +
                      "' only supports default assignment");
        return std::make_unique<hir::StatementList>(std::move(statements));
      }
      if (!initializerIsHandle) {
        addDiagnostic("handle target '" + item.name +
                      "' can only be assigned from a handle value");
        return std::make_unique<hir::StatementList>(std::move(statements));
      }
      auto value = analyze(*item.initializer);
      if (value) {
        statements.push_back(std::make_unique<hir::IntegerStore>(
            symbol.name, symbol.bindingName, symbol.byteLength, symbol.storage,
            std::move(value)));
      }
      if (statements.size() == 1U) {
        return std::move(statements.front());
      }
      return std::make_unique<hir::StatementList>(std::move(statements));
    }
    if (initializerIsHandle) {
      addDiagnostic("handle value may only be assigned to a handle target");
      return std::make_unique<hir::StatementList>(std::move(statements));
    }
    if (item.assignmentOp == "&=") {
      if (symbol.byteLength != pointerByteLength()) {
        addDiagnostic("address rebinding target '" + item.name +
                      "' must be pointer-sized");
        return std::make_unique<hir::StatementList>(std::move(statements));
      }
      auto address = analyze(*item.initializer);
      if (!address) {
        return std::make_unique<hir::StatementList>(std::move(statements));
      }
      if (!isIntegerExpression(*address) ||
          integerExpressionByteLength(*address).value_or(0) !=
              pointerByteLength()) {
        addDiagnostic("right operand of '&=' is not a pointer-sized expression");
        return std::make_unique<hir::StatementList>(std::move(statements));
      }
      statements.push_back(std::make_unique<hir::IntegerStore>(
          symbol.name, symbol.bindingName, symbol.byteLength, symbol.storage,
          std::move(address)));
      if (statements.size() == 1U) {
        return std::move(statements.front());
      }
      return std::make_unique<hir::StatementList>(std::move(statements));
    }

    if (item.assignmentOp == "%s=" ||
        (item.assignmentOp == "=" && symbol.templateName == "cstr")) {
      auto stringStore = analyzeStringInitializer(item, symbol);
      if (stringStore) {
        statements.push_back(std::move(stringStore));
      }
      if (statements.size() == 1U) {
        return std::move(statements.front());
      }
      return std::make_unique<hir::StatementList>(std::move(statements));
    }

    if (item.assignmentOp == "%b=") {
      auto boolStore = analyzeBoolInitializer(item, symbol);
      if (boolStore) {
        statements.push_back(std::move(boolStore));
      }
      if (statements.size() == 1U) {
        return std::move(statements.front());
      }
      return std::make_unique<hir::StatementList>(std::move(statements));
    }

    if (const auto floatLength = floatAssignmentByteLength(item.assignmentOp);
        floatLength || (item.assignmentOp == "=" &&
                        (isFloatTemplate(symbol.templateName) ||
                         dynamic_cast<const ast::FloatLiteral *>(
                             item.initializer.get()) != nullptr))) {
      const auto targetLength =
          !floatLength || *floatLength == 0 ? symbol.byteLength : *floatLength;
      if (!isLoweredFloatByteLength(targetLength)) {
        addDiagnostic("float byte length " + std::to_string(targetLength) +
                      " is not supported yet for '" + item.name + "'");
        return std::make_unique<hir::StatementList>(std::move(statements));
      }
      if (targetLength != symbol.byteLength) {
        addDiagnostic("float initializer byte length does not match target '" +
                      item.name + "'");
        return std::make_unique<hir::StatementList>(std::move(statements));
      }
      auto value = analyzeFloatOperand(*item.initializer, symbol.byteLength);
      if (value) {
        statements.push_back(std::make_unique<hir::FloatStore>(
            symbol.name, symbol.bindingName, symbol.byteLength, symbol.storage,
            std::move(value)));
      }
      if (statements.size() == 1U) {
        return std::move(statements.front());
      }
      return std::make_unique<hir::StatementList>(std::move(statements));
    }

    if (item.assignmentOp != "=" &&
        !integerAssignmentOperator(item.assignmentOp)) {
      addDiagnostic("unsupported declaration initializer operator '" +
                    item.assignmentOp + "'");
      return std::make_unique<hir::StatementList>(std::move(statements));
    }
    if (const auto *integer =
            dynamic_cast<const ast::IntegerLiteral *>(item.initializer.get())) {
      const bool fits = isUnsignedTemplate(symbol.templateName)
                            ? unsignedIntegerFits(*integer, symbol.byteLength)
                            : integerFits(*integer, symbol.byteLength);
      if (!fits) {
        addDiagnostic("integer literal '" + integer->value +
                      "' does not fit in target '" + item.name + "'");
        return std::make_unique<hir::StatementList>(std::move(statements));
      }
      if (isUnsignedTemplate(symbol.templateName) &&
          symbol.byteLength == 8) {
        statements.push_back(std::make_unique<hir::IntegerStore>(
            symbol.name, symbol.bindingName, symbol.byteLength, symbol.storage,
            std::make_unique<hir::IntegerLiteral>(integer->value,
                                                  symbol.byteLength)));
        if (statements.size() == 1U) {
          return std::move(statements.front());
        }
        return std::make_unique<hir::StatementList>(std::move(statements));
      }
    }
    if (isSignedTemplate(symbol.templateName) && symbol.byteLength == 8 &&
        isI64MinLiteral(*item.initializer)) {
      statements.push_back(std::make_unique<hir::IntegerStore>(
          symbol.name, symbol.bindingName, symbol.byteLength, symbol.storage,
          lowerI64MinLiteral()));
      if (statements.size() == 1U) {
        return std::move(statements.front());
      }
      return std::make_unique<hir::StatementList>(std::move(statements));
    }
    if (const auto *character =
            dynamic_cast<const ast::CharLiteral *>(item.initializer.get())) {
      const auto decoded = literal::decodeCharLiteral(character->value);
      if (!decoded) {
        addDiagnostic("invalid character literal '" + character->value +
                      "': " + *decoded.error);
        return std::make_unique<hir::StatementList>(std::move(statements));
      }
      if (decoded.bytes.size() > symbol.byteLength) {
        addDiagnostic("character literal byte length does not fit target '" +
                      item.name + "'");
        return std::make_unique<hir::StatementList>(std::move(statements));
      }
    }
    auto value = analyze(*item.initializer);
    if (value) {
      statements.push_back(std::make_unique<hir::IntegerStore>(
          symbol.name, symbol.bindingName, symbol.byteLength, symbol.storage,
          std::move(value)));
    }
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
  const auto reference = resolveAddressableReference(*expression.operand);
  if (reference) {
    const auto &templateName = reference->templateName;
    if (isFloatTemplate(templateName) || templateName == "bool" ||
        templateName == "addr" || templateName == "handle" ||
        templateName == "cstr" || templateName == "bytes" ||
        templates_.contains(templateName)) {
      addDiagnostic("increment target must use an integer View");
      return nullptr;
    }
  } else if (!result_.diagnostics.empty()) {
    return nullptr;
  }

  ast::IntegerLiteral one("1");
  return lowerAssignmentTarget(
      *expression.operand, expression.op == "post++" ? "%d+=" : "%d-=",
      false, one, std::make_unique<hir::IntegerLiteral>("1", 1));
}

std::unique_ptr<hir::Stmt> Analyzer::analyze(const ast::ReturnStmt &statement) {
  std::vector<std::unique_ptr<hir::Expr>> values;
  std::vector<std::size_t> byteLengths;
  std::vector<bool> handleValues;
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
    if (!expectsFloat && !isIntegerExpression(*lowered)) {
      addDiagnostic("return value is not an integer expression");
      return nullptr;
    }
    const auto byteLength = expectsFloat
                                ? floatExpressionByteLength(*lowered).value_or(0)
                                : integerExpressionByteLength(*lowered).value_or(4);
    if (currentFunction_ != nullptr && currentFunction_->returnsKnown &&
        currentFunction_->returnsExplicit) {
      const auto expected = currentFunction_->returnByteLengths[index];
      if (byteLength > expected && expected < 8) {
        addDiagnostic("return value byte length does not fit function "
                      "signature");
        return nullptr;
      }
      byteLengths.push_back(expected);
    } else {
      byteLengths.push_back(byteLength);
    }
    values.push_back(std::move(lowered));
    handleValues.push_back(valueIsHandle);
  }
  if (currentFunction_ != nullptr && !currentFunction_->returnsKnown) {
    currentFunction_->returnTemplateNames.clear();
    currentFunction_->returnTemplateNames.reserve(handleValues.size());
    for (const bool valueIsHandle : handleValues) {
      currentFunction_->returnTemplateNames.push_back(
          valueIsHandle ? "handle" : "");
    }
  }
  if (!registerReturnLengths(byteLengths)) {
    return nullptr;
  }
  return std::make_unique<hir::Return>(std::move(values));
}

std::unique_ptr<hir::Stmt> Analyzer::analyze(const ast::IfStmt &statement) {
  if (isHandleExpression(*statement.condition)) {
    addDiagnostic("handle values cannot be used as conditions");
    return nullptr;
  }
  auto condition = analyze(*statement.condition);
  if (!condition) {
    return nullptr;
  }
  if (!isIntegerExpression(*condition)) {
    addDiagnostic("if condition is not an integer expression");
    return nullptr;
  }

  auto thenBlock = analyze(*statement.thenBlock);
  std::unique_ptr<hir::Block> elseBlock;
  if (statement.elseBlock) {
    elseBlock = analyze(*statement.elseBlock);
  }

  return std::make_unique<hir::If>(std::move(condition), std::move(thenBlock),
                                   std::move(elseBlock));
}

std::unique_ptr<hir::Stmt> Analyzer::analyze(const ast::WhileStmt &statement) {
  if (isHandleExpression(*statement.condition)) {
    addDiagnostic("handle values cannot be used as conditions");
    return nullptr;
  }
  auto condition = analyze(*statement.condition);
  if (!condition) {
    return nullptr;
  }
  if (!isIntegerExpression(*condition)) {
    addDiagnostic("while condition is not an integer expression");
    return nullptr;
  }

  ++loopDepth_;
  auto body = analyze(*statement.body);
  --loopDepth_;

  return std::make_unique<hir::While>(std::move(condition), std::move(body));
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
    if (isHandleExpression(*statement.condition)) {
      addDiagnostic("handle values cannot be used as conditions");
      --blockDepth_;
      endScope();
      return nullptr;
    }
    condition = analyze(*statement.condition);
    if (!condition) {
      --blockDepth_;
      endScope();
      return nullptr;
    }
    if (!isIntegerExpression(*condition)) {
      addDiagnostic("for condition is not an integer expression");
      --blockDepth_;
      endScope();
      return nullptr;
    }
  }

  std::vector<std::unique_ptr<hir::Stmt>> post;
  for (const auto &expr : statement.post) {
    auto lowered = analyzeExpressionStatementExpr(*expr);
    if (lowered) {
      post.push_back(std::move(lowered));
    }
  }

  ++loopDepth_;
  auto body = analyze(*statement.body);
  --loopDepth_;

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
  if (!labels_.emplace(statement.label, LabelInfo{blockDepth_}).second) {
    addDiagnostic("duplicate label '" + statement.label + "'");
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
    addDiagnostic("duplicate catch error '" + statement.errorName + "'");
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
