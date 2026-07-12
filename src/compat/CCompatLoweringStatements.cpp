#include "CCompatLoweringInternal.h"

#include <algorithm>
#include <utility>

namespace hitsimple::compat::detail {
namespace {

std::unique_ptr<ast::BlockStmt> asCoreBlock(std::unique_ptr<ast::Stmt> statement) {
  if (!statement) {
    return nullptr;
  }
  std::vector<std::unique_ptr<ast::Stmt>> statements;
  statements.push_back(std::move(statement));
  return std::make_unique<ast::BlockStmt>(std::move(statements));
}

bool isStringLiteral(const Expr* expression) {
  return dynamic_cast<const StringLiteralExpr*>(expression) != nullptr;
}

bool isCharacterArray(const ObjectInfo& object) {
  return object.isArray && object.type.abi.kind == CAbiValueKind::Integer &&
         object.type.abi.byteLength == 1U;
}

} // namespace

std::unique_ptr<ast::BlockStmt> Lowerer::lowerBlock(const BlockStmt& block) {
  pushScope();
  std::vector<std::unique_ptr<ast::Stmt>> statements;
  for (const auto& statement : block.statements) {
    auto lowered = lowerStmt(*statement);
    if (lowered) {
      statements.push_back(std::move(lowered));
    }
  }
  popScope();
  return std::make_unique<ast::BlockStmt>(std::move(statements));
}

std::unique_ptr<ast::Stmt> Lowerer::lowerLocalDeclaration(
    const VarDecl& declaration) {
  if (declaration.storage == StorageClass::Extern) {
    error(declaration.range,
          "block-scope C extern declarations require a core declaration sidecar");
    return nullptr;
  }
  auto object = resolveObject(declaration.type, declaration.declarator);
  if (!object) {
    return nullptr;
  }
  if (!scopes_.empty() && scopes_.back().contains(declaration.declarator.name)) {
    error(declaration.range, "duplicate C local declaration '" +
                                 declaration.declarator.name + "'");
    return nullptr;
  }
  bindObject(declaration.declarator.name, *object);

  std::string templateName = object->isArray ? "bytes" : object->type.coreTemplate;
  std::string length;
  if (object->isArray) {
    length = std::to_string(object->byteLength());
    if (isCharacterArray(*object) &&
        isStringLiteral(declaration.initializer.get())) {
      templateName = "cstr";
    }
  } else {
    length = byteLengthText(object->type.abi.byteLength,
                            options_.pointerByteLength);
  }

  std::unique_ptr<ast::Expr> initializer;
  if (declaration.initializer) {
    if (object->isArray &&
        (!isCharacterArray(*object) ||
         !isStringLiteral(declaration.initializer.get()))) {
      error(declaration.range,
            "only char-array string initializers are supported by the C compatibility layer");
      return nullptr;
    }
    if (isAggregate(object->type)) {
      error(declaration.range,
            "C aggregate initializers require a core aggregate-initializer lowering extension");
      return nullptr;
    }
    auto lowered = lowerExpr(*declaration.initializer);
    if (!lowered) {
      return nullptr;
    }
    lowered = decayArray(std::move(*lowered), declaration.initializer->range);
    if (!lowered) {
      return nullptr;
    }
    initializer = std::move(lowered->expression);
  }
  std::vector<ast::DeclItem> items;
  items.emplace_back(declaration.declarator.name, std::move(length),
                     initializer ? "=" : "", std::move(initializer),
                     std::move(templateName));
  return std::make_unique<ast::VarDeclStmt>(
      declaration.storage == StorageClass::Static ? "static" : "new",
      std::move(items));
}

std::unique_ptr<ast::Stmt> Lowerer::lowerStmt(const Stmt& statement) {
  if (dynamic_cast<const EmptyStmt*>(&statement) != nullptr) {
    return std::make_unique<ast::EmptyStmt>();
  }
  if (const auto* declaration = dynamic_cast<const DeclStmt*>(&statement)) {
    return lowerLocalDeclaration(*declaration->declaration);
  }
  if (const auto* expression = dynamic_cast<const ExprStmt*>(&statement)) {
    auto lowered = lowerExpr(*expression->expression);
    if (!lowered) {
      return nullptr;
    }
    auto* assignment = dynamic_cast<ast::AssignmentExpr*>(lowered->expression.get());
    if (assignment != nullptr) {
      std::unique_ptr<ast::AssignmentExpr> owned(
          static_cast<ast::AssignmentExpr*>(lowered->expression.release()));
      return std::make_unique<ast::AssignStmt>(std::move(owned));
    }
    return std::make_unique<ast::ExprStmt>(std::move(lowered->expression));
  }
  if (const auto* returned = dynamic_cast<const ReturnStmt*>(&statement)) {
    if (!returned->value) {
      return std::make_unique<ast::ReturnStmt>(
          std::vector<std::unique_ptr<ast::Expr>>());
    }
    auto value = lowerExpr(*returned->value);
    if (!value) {
      return nullptr;
    }
    value = decayArray(std::move(*value), returned->value->range);
    if (!value) {
      return nullptr;
    }
    return std::make_unique<ast::ReturnStmt>(std::move(value->expression));
  }
  if (const auto* ifStmt = dynamic_cast<const IfStmt*>(&statement)) {
    auto condition = lowerExpr(*ifStmt->condition);
    const auto lowerBranch = [&](const Stmt& branch) {
      if (const auto* block = dynamic_cast<const BlockStmt*>(&branch)) {
        return lowerBlock(*block);
      }
      return asCoreBlock(lowerStmt(branch));
    };
    auto thenBranch = lowerBranch(*ifStmt->thenBranch);
    std::unique_ptr<ast::BlockStmt> elseBranch;
    if (ifStmt->elseBranch) {
      elseBranch = lowerBranch(*ifStmt->elseBranch);
    }
    if (!condition || !thenBranch || (ifStmt->elseBranch && !elseBranch)) {
      return nullptr;
    }
    condition = decayArray(std::move(*condition), ifStmt->condition->range);
    if (!condition) {
      return nullptr;
    }
    return std::make_unique<ast::IfStmt>(std::move(condition->expression),
                                         std::move(thenBranch),
                                         std::move(elseBranch));
  }
  if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(&statement)) {
    auto condition = lowerExpr(*whileStmt->condition);
    std::unique_ptr<ast::BlockStmt> body;
    if (const auto* block = dynamic_cast<const BlockStmt*>(whileStmt->body.get())) {
      body = lowerBlock(*block);
    } else {
      body = asCoreBlock(lowerStmt(*whileStmt->body));
    }
    if (!condition || !body) {
      return nullptr;
    }
    condition = decayArray(std::move(*condition), whileStmt->condition->range);
    if (!condition) {
      return nullptr;
    }
    return std::make_unique<ast::WhileStmt>(std::move(condition->expression),
                                            std::move(body));
  }
  if (const auto* forStmt = dynamic_cast<const ForStmt*>(&statement)) {
    std::unique_ptr<ast::Stmt> init;
    if (forStmt->init) {
      init = lowerStmt(*forStmt->init);
      if (!init) {
        return nullptr;
      }
    }
    std::unique_ptr<ast::Expr> condition;
    if (forStmt->condition) {
      auto lowered = lowerExpr(*forStmt->condition);
      if (!lowered) {
        return nullptr;
      }
      lowered = decayArray(std::move(*lowered), forStmt->condition->range);
      if (!lowered) {
        return nullptr;
      }
      condition = std::move(lowered->expression);
    } else {
      condition = std::make_unique<ast::IntegerLiteral>("1");
    }
    std::vector<std::unique_ptr<ast::Expr>> post;
    if (forStmt->post) {
      auto lowered = lowerExpr(*forStmt->post);
      if (!lowered) {
        return nullptr;
      }
      post.push_back(std::move(lowered->expression));
    }
    std::unique_ptr<ast::BlockStmt> body;
    if (const auto* block = dynamic_cast<const BlockStmt*>(forStmt->body.get())) {
      body = lowerBlock(*block);
    } else {
      body = asCoreBlock(lowerStmt(*forStmt->body));
    }
    if (!body) {
      return nullptr;
    }
    return std::make_unique<ast::ForStmt>(std::move(init), std::move(condition),
                                          std::move(post), std::move(body));
  }
  if (dynamic_cast<const BreakStmt*>(&statement) != nullptr) {
    return std::make_unique<ast::BreakStmt>();
  }
  if (dynamic_cast<const ContinueStmt*>(&statement) != nullptr) {
    return std::make_unique<ast::ContinueStmt>();
  }
  if (const auto* gotoStmt = dynamic_cast<const GotoStmt*>(&statement)) {
    return std::make_unique<ast::GotoStmt>(gotoStmt->label);
  }
  if (const auto* label = dynamic_cast<const LabelStmt*>(&statement)) {
    auto nested = lowerStmt(*label->statement);
    if (!nested) {
      return nullptr;
    }
    return std::make_unique<ast::LabelStmt>(label->label, std::move(nested));
  }
  if (const auto* block = dynamic_cast<const BlockStmt*>(&statement)) {
    error(block->range,
          "standalone nested C block requires a core scoped-block statement node");
    return nullptr;
  }
  error(statement.range, "unsupported C compatibility statement");
  return nullptr;
}

bool Lowerer::lowerStruct(const StructDecl& declaration) {
  if (!resolveStruct(declaration.name, declaration.range)) {
    return false;
  }
  const auto& structure = structs_.at(declaration.name);
  std::vector<ast::TemplateMember> members;
  members.reserve(structure.fields.size() * 2U + 1U);
  std::size_t offset = 0;
  std::size_t paddingIndex = 0;
  for (const auto& field : structure.fields) {
    if (field.offset > offset) {
      members.emplace_back("__hitsimple_c_padding_" +
                               std::to_string(paddingIndex++),
                           std::to_string(field.offset - offset), "bytes");
      offset = field.offset;
    }
    const std::string templateName =
        field.object.isArray ? "bytes" : field.object.type.coreTemplate;
    members.emplace_back(field.name, std::to_string(field.object.byteLength()),
                         templateName);
    offset += field.object.byteLength();
  }
  if (structure.byteLength > offset) {
    members.emplace_back("__hitsimple_c_padding_" +
                             std::to_string(paddingIndex),
                         std::to_string(structure.byteLength - offset), "bytes");
  }
  declarations_.push_back(
      std::make_unique<ast::TemplateDecl>(declaration.name, std::move(members)));
  return true;
}

void Lowerer::addLinkage(std::string name,
                         Linkage linkage,
                         bool isFunction,
                         bool isDefinition,
                         std::optional<CAbiType> objectType,
                         std::vector<CAbiType> parameterTypes,
                         std::optional<CAbiType> returnType,
                         const diagnostic::SourceRange& range) {
  linkage_.push_back(LinkageMetadata{std::move(name), linkage_.empty() ? "" : "",
                                     linkage, isFunction, isDefinition,
                                     std::move(objectType),
                                     std::move(parameterTypes),
                                     std::move(returnType), range});
  linkage_.back().coreName = linkage_.back().sourceName;
}

bool Lowerer::lowerGlobal(const VarDecl& declaration) {
  auto object = resolveObject(declaration.type, declaration.declarator);
  if (!object) {
    return false;
  }

  if (declaration.storage == StorageClass::Extern && declaration.initializer) {
    error(declaration.range,
          "C extern variable declarations cannot have an initializer in the minimal compatibility subset");
    return false;
  }

  bindObject(declaration.declarator.name, *object);
  auto objectAbi = object->type.abi;
  objectAbi.elementCount = object->isArray ? object->arrayCount : 1;

  std::string templateName =
      object->isArray ? "bytes" : object->type.coreTemplate;
  std::string length;
  if (object->isArray) {
    length = std::to_string(object->byteLength());
    if (isCharacterArray(*object) &&
        isStringLiteral(declaration.initializer.get())) {
      templateName = "cstr";
    }
  } else {
    length = byteLengthText(object->type.abi.byteLength,
                            options_.pointerByteLength);
  }

  std::unique_ptr<ast::Expr> initializer;
  if (declaration.initializer) {
    if (object->isArray &&
        (!isCharacterArray(*object) ||
         !isStringLiteral(declaration.initializer.get()))) {
      error(declaration.range,
            "only char-array string initializers are supported by the C compatibility layer");
      return false;
    }
    if (isAggregate(object->type)) {
      error(declaration.range,
            "C aggregate initializers require a core aggregate-initializer lowering extension");
      return false;
    }
    auto lowered = lowerExpr(*declaration.initializer);
    if (!lowered) {
      return false;
    }
    lowered = decayArray(std::move(*lowered), declaration.initializer->range);
    if (!lowered) {
      return false;
    }
    initializer = std::move(lowered->expression);
  }

  if (declaration.storage == StorageClass::Extern) {
    declarations_.push_back(
        std::make_unique<ast::ExternVarDecl>(declaration.declarator.name, length,
                                             std::move(templateName)));
    addLinkage(declaration.declarator.name, Linkage::External, false, false,
               std::move(objectAbi), {}, std::nullopt, declaration.range);
    return true;
  }
  declarations_.push_back(
      std::make_unique<ast::GlobalNewDecl>(
          declaration.declarator.name, std::move(length),
          std::move(templateName), initializer ? "=" : "",
          std::move(initializer)));
  addLinkage(declaration.declarator.name,
             declaration.storage == StorageClass::Static ? Linkage::Internal
                                                          : Linkage::External,
             false, true, std::move(objectAbi), {}, std::nullopt,
             declaration.range);
  return true;
}

bool Lowerer::lowerFunction(const FunctionDecl& declaration) {
  const auto signature = functions_.find(declaration.declarator.name);
  if (signature == functions_.end()) {
    return false;
  }
  const auto& info = signature->second;
  std::vector<ast::Param> parameters;
  std::vector<CAbiType> abiParameters;
  pushScope();
  bool valid = true;
  for (const auto& parameter : declaration.parameters) {
    if (parameter.isVoidMarker) {
      continue;
    }
    auto object = resolveObject(parameter.type, parameter.declarator);
    if (!object) {
      valid = false;
      continue;
    }
    TypeInfo type = object->type;
    if (object->isArray) {
      type = makePointer(std::move(type), options_.pointerByteLength);
    }
    bindObject(parameter.declarator.name, ObjectInfo{type, false, 0});
    parameters.emplace_back(parameter.declarator.name,
                            byteLengthText(type.abi.byteLength,
                                           options_.pointerByteLength),
                            type.coreTemplate);
    abiParameters.push_back(type.abi);
  }
  if (!options_.allowHostFloatExternAbi &&
      (isFloating(info.returnType) ||
       std::any_of(abiParameters.begin(), abiParameters.end(),
                   [](const CAbiType& type) {
                     return type.kind == CAbiValueKind::Floating;
                   }))) {
    error(declaration.range,
          "C float/double function ABI is disabled; enable host C ABI lowering before compiling this signature");
    valid = false;
  }
  std::unique_ptr<ast::BlockStmt> body;
  if (valid && declaration.body) {
    body = lowerBlock(*declaration.body);
  }
  popScope();
  if (!valid || (declaration.body && !body)) {
    return false;
  }

  std::vector<ast::ReturnItem> returns;
  if (!isVoid(info.returnType)) {
    returns.emplace_back("", byteLengthText(info.returnType.abi.byteLength,
                                              options_.pointerByteLength),
                         info.returnType.coreTemplate);
  }
  const Linkage linkage = info.linkage;
  if (!declaration.body) {
    declarations_.push_back(std::make_unique<ast::ExternFunctionDecl>(
        declaration.declarator.name, std::move(parameters), std::move(returns)));
    addLinkage(declaration.declarator.name, linkage, true, false, std::nullopt,
               std::move(abiParameters), info.returnType.abi, declaration.range);
    return true;
  }
  declarations_.push_back(std::make_unique<ast::FunctionDecl>(
      declaration.declarator.name, std::move(parameters), std::move(returns),
      std::move(body)));
  addLinkage(declaration.declarator.name, linkage, true, true, std::nullopt,
             std::move(abiParameters), info.returnType.abi, declaration.range);
  return true;
}

bool Lowerer::lowerTopLevel(const Decl& declaration) {
  if (const auto* structDecl = dynamic_cast<const StructDecl*>(&declaration)) {
    return lowerStruct(*structDecl);
  }
  if (dynamic_cast<const TypedefDecl*>(&declaration) != nullptr) {
    return true;
  }
  if (const auto* variable = dynamic_cast<const VarDecl*>(&declaration)) {
    return lowerGlobal(*variable);
  }
  if (const auto* function = dynamic_cast<const FunctionDecl*>(&declaration)) {
    if (!function->isDefinition()) {
      const auto found = functions_.find(function->declarator.name);
      if (found != functions_.end() && found->second.isDefinition) {
        return true;
      }
    }
    return lowerFunction(*function);
  }
  error(declaration.range, "unsupported C top-level declaration");
  return false;
}

} // namespace hitsimple::compat::detail
