#include "hitsimple/stdlib/StandardLibraryModules.h"

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace hitsimple::stdlib {
namespace {

std::size_t bootstrapByteLength(BuiltinBootstrapType type) {
  switch (type) {
  case BuiltinBootstrapType::Void: return 0;
  case BuiltinBootstrapType::Pointer: return sizeof(void*);
  case BuiltinBootstrapType::Bytes1: return 1;
  case BuiltinBootstrapType::Bytes2: return 2;
  case BuiltinBootstrapType::Bytes4: return 4;
  case BuiltinBootstrapType::Bytes8: return 8;
  case BuiltinBootstrapType::Bytes16: return 16;
  }
  return 0;
}

BuiltinProvider selectedProvider(const BuiltinSpec& spec,
                                 BuiltinProviderSelection selection) {
  if (selection == BuiltinProviderSelection::Reference &&
      spec.referenceProvider != BuiltinProvider::None) {
    return spec.referenceProvider;
  }
  return spec.provider;
}

void collectModule(std::string_view id, std::vector<std::string>& ordered,
                   std::unordered_set<std::string>& visiting,
                   std::unordered_set<std::string>& emitted) {
  if (emitted.contains(std::string(id))) {
    return;
  }
  if (!visiting.insert(std::string(id)).second) {
    return;
  }
  const auto* module = findSourceModule(id);
  if (module == nullptr) {
    return;
  }
  for (const auto dependency : module->dependencies) {
    collectModule(dependency, ordered, visiting, emitted);
  }
  visiting.erase(std::string(id));
  emitted.insert(std::string(id));
  ordered.emplace_back(id);
}

std::vector<std::size_t> parameterByteLengths(const BuiltinSpec& spec) {
  std::vector<std::size_t> result;
  result.reserve(spec.parameters.size());
  for (const auto& parameter : spec.parameters) {
    result.push_back(bootstrapByteLength(parameter.bootstrapType));
  }
  return result;
}

std::vector<std::size_t> resultByteLengths(const BuiltinSpec& spec) {
  std::vector<std::size_t> result;
  result.reserve(spec.results.size());
  for (const auto& item : spec.results) {
    result.push_back(bootstrapByteLength(item.bootstrapType));
  }
  return result;
}

class ProviderSelector final {
public:
  ProviderSelector(hir::TranslationUnit& unit, BuiltinProviderSelection selection)
      : unit_(unit), selection_(selection) {
    for (const auto& function : unit_.externFunctions) {
      externs_.insert(function.name);
    }
  }

  std::vector<std::string> run() {
    if (unit_.globalInit) {
      visit(*unit_.globalInit);
    }
    for (const auto& function : unit_.functions) {
      visit(*function->body);
    }
    return modules_;
  }

private:
  void select(BuiltinId builtin, BuiltinProvider& provider,
              std::string& callee) {
    if (builtin == BuiltinId::None) {
      return;
    }
    const auto* spec = findBuiltin(builtin);
    if (spec == nullptr) {
      return;
    }
    provider = selectedProvider(*spec, selection_);
    if (provider != BuiltinProvider::CoreHs) {
      return;
    }
    if (spec->sourceModule.empty() || spec->implementationSymbol.empty()) {
      return;
    }
    callee = std::string(spec->implementationSymbol);
    if (externs_.insert(callee).second) {
      unit_.externFunctions.emplace_back(callee, parameterByteLengths(*spec),
                                         resultByteLengths(*spec));
    }
    collectModule(spec->sourceModule, modules_, visitingModules_, emittedModules_);
  }

  void visit(hir::Expr& expression) {
    if (auto* binary = dynamic_cast<hir::BinaryExpr*>(&expression)) {
      visit(*binary->left);
      visit(*binary->right);
    } else if (auto* unary = dynamic_cast<hir::UnaryExpr*>(&expression)) {
      visit(*unary->operand);
    } else if (auto* ternary = dynamic_cast<hir::TernaryExpr*>(&expression)) {
      visit(*ternary->condition);
      visit(*ternary->thenExpr);
      visit(*ternary->elseExpr);
    } else if (auto* unsignedExpr = dynamic_cast<hir::UnsignedExpr*>(&expression)) {
      visit(*unsignedExpr->operand);
    } else if (auto* cast = dynamic_cast<hir::IntegerCastExpr*>(&expression)) {
      visit(*cast->operand);
    } else if (auto* view = dynamic_cast<hir::TemplateViewExpr*>(&expression)) {
      visit(*view->operand);
    } else if (auto* call = dynamic_cast<hir::UserTemplateOpCallExpr*>(&expression)) {
      for (auto& argument : call->arguments) visit(*argument);
    } else if (auto* binary = dynamic_cast<hir::FloatBinaryExpr*>(&expression)) {
      visit(*binary->left);
      visit(*binary->right);
    } else if (auto* comparison = dynamic_cast<hir::FloatCompareExpr*>(&expression)) {
      visit(*comparison->left);
      visit(*comparison->right);
    } else if (auto* conversion = dynamic_cast<hir::ToFloatExpr*>(&expression)) {
      visit(*conversion->operand);
    } else if (auto* conversion = dynamic_cast<hir::ToIntExpr*>(&expression)) {
      visit(*conversion->operand);
    } else if (auto* format = dynamic_cast<hir::UserTemplateFormatCallExpr*>(&expression)) {
      visit(*format->value);
      if (format->file) visit(*format->file);
    } else if (auto* call = dynamic_cast<hir::CallExpr*>(&expression)) {
      select(call->builtin, call->provider, call->callee);
      for (auto& argument : call->arguments) visit(*argument);
    } else if (auto* dynamic = dynamic_cast<hir::DynamicByteViewExpr*>(&expression)) {
      visit(*dynamic->source);
      if (dynamic->runtimeLength) visit(*dynamic->runtimeLength);
    } else if (auto* byteSwap = dynamic_cast<hir::ByteSwapExpr*>(&expression)) {
      visit(*byteSwap->source);
    } else if (auto* assignment = dynamic_cast<hir::AssignmentExpr*>(&expression)) {
      for (auto& store : assignment->stores) visit(*store);
      if (assignment->result) visit(*assignment->result);
    } else if (auto* dereference = dynamic_cast<hir::DerefExpr*>(&expression)) {
      visit(*dereference->address);
    }
  }

  void visit(hir::Stmt& statement) {
    if (auto* list = dynamic_cast<hir::StatementList*>(&statement)) {
      for (auto& item : list->statements) visit(*item);
    } else if (auto* store = dynamic_cast<hir::IntegerStore*>(&statement)) {
      visit(*store->value);
    } else if (auto* store = dynamic_cast<hir::FloatStore*>(&statement)) {
      visit(*store->value);
    } else if (auto* store = dynamic_cast<hir::BoolStore*>(&statement)) {
      visit(*store->value);
    } else if (auto* store = dynamic_cast<hir::PointerStore*>(&statement)) {
      visit(*store->address);
      visit(*store->value);
    } else if (auto* call = dynamic_cast<hir::Call*>(&statement)) {
      select(call->builtin, call->provider, call->callee);
      for (auto& argument : call->arguments) visit(*argument);
    } else if (auto* call = dynamic_cast<hir::UserTemplateOpCall*>(&statement)) {
      for (auto& argument : call->arguments) visit(*argument);
    } else if (auto* call = dynamic_cast<hir::UserTemplateFormatCall*>(&statement)) {
      visit(*call->value);
      if (call->file) visit(*call->file);
    } else if (auto* call = dynamic_cast<hir::MultiReturnCallStore*>(&statement)) {
      for (auto& argument : call->arguments) visit(*argument);
    } else if (auto* call = dynamic_cast<hir::InputCallStore*>(&statement)) {
      if (call->file) visit(*call->file);
      visit(*call->format);
    } else if (auto* ret = dynamic_cast<hir::Return*>(&statement)) {
      for (auto& value : ret->values) visit(*value);
    } else if (auto* conditional = dynamic_cast<hir::If*>(&statement)) {
      visit(*conditional->condition);
      visit(*conditional->thenBlock);
      if (conditional->elseBlock) visit(*conditional->elseBlock);
    } else if (auto* loop = dynamic_cast<hir::While*>(&statement)) {
      visit(*loop->condition);
      visit(*loop->body);
    } else if (auto* loop = dynamic_cast<hir::For*>(&statement)) {
      if (loop->init) visit(*loop->init);
      if (loop->condition) visit(*loop->condition);
      for (auto& post : loop->post) visit(*post);
      visit(*loop->body);
    } else if (auto* label = dynamic_cast<hir::Label*>(&statement)) {
      visit(*label->statement);
    } else if (auto* thrown = dynamic_cast<hir::Throw*>(&statement)) {
      if (thrown->delivery) {
        visit(*thrown->delivery);
      }
    } else if (auto* tryCatch = dynamic_cast<hir::TryCatch*>(&statement)) {
      visit(*tryCatch->tryBlock);
      visit(*tryCatch->catchBlock);
    }
  }

  void visit(hir::Block& block) {
    for (auto& statement : block.statements) visit(*statement);
  }

  hir::TranslationUnit& unit_;
  BuiltinProviderSelection selection_;
  std::unordered_set<std::string> externs_;
  std::unordered_set<std::string> visitingModules_;
  std::unordered_set<std::string> emittedModules_;
  std::vector<std::string> modules_;
};

} // namespace

std::vector<std::string> selectStandardLibraryProviders(
    hir::TranslationUnit& unit, BuiltinProviderSelection selection) {
  return ProviderSelector(unit, selection).run();
}

} // namespace hitsimple::stdlib
