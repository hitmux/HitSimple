#include "SemaAnalyzer.h"

#include "hitsimple/literal/Literal.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hitsimple::sema {
namespace {

enum ObservedEffect : std::uint32_t {
  ObservedNone = 0,
  ObservedAllocate = 1U << 0U,
  ObservedFree = 1U << 1U,
  ObservedThrow = 1U << 2U,
  ObservedIo = 1U << 3U,
  ObservedUnknown = 1U << 4U,
};

enum class PointerKind : std::uint8_t {
  Local,
  External,
  Unknown,
};

struct PointerRef final {
  PointerKind kind = PointerKind::Unknown;
  std::string object;
  std::optional<std::uint64_t> offset;
};

struct EffectAccess final {
  std::string object;
  hir::EffectAccess access = hir::EffectAccess::Read;
  std::optional<std::uint64_t> offset;
  std::optional<std::uint64_t> byteLength;

  bool operator==(const EffectAccess&) const = default;
};

struct CallSite final {
  std::string callee;
  const std::vector<std::unique_ptr<hir::Expr>>* arguments = nullptr;
};

struct EffectSummary final {
  std::uint32_t flags = ObservedNone;
  std::vector<EffectAccess> accesses;
  std::vector<CallSite> calls;
};

struct Callable final {
  const hir::Function* definition = nullptr;
  const hir::ExternFunction* external = nullptr;
  const hir::EffectContract* contract = nullptr;
  std::vector<std::string> parameterNames;
  std::unordered_map<std::string, std::size_t> parameterIndexes;
  std::unordered_map<std::string, std::string> addressParameterObjects;
};

std::optional<std::uint64_t> integerLiteral(const hir::Expr& expression) {
  if (const auto* integer = dynamic_cast<const hir::IntegerLiteral*>(&expression)) {
    return literal::parseUnsignedIntegerLiteral(integer->value);
  }
  if (const auto* unsignedValue = dynamic_cast<const hir::UnsignedExpr*>(&expression)) {
    return integerLiteral(*unsignedValue->operand);
  }
  if (const auto* cast = dynamic_cast<const hir::IntegerCastExpr*>(&expression)) {
    return integerLiteral(*cast->operand);
  }
  return std::nullopt;
}

bool isExternalStorage(hir::MemoryStorage storage) {
  return storage == hir::MemoryStorage::Global ||
         storage == hir::MemoryStorage::StaticLocal;
}

bool isIoBuiltin(stdlib::BuiltinId builtin) {
  using enum stdlib::BuiltinId;
  switch (builtin) {
  case Get:
  case Put:
  case Print:
  case Printf:
  case Scanf:
  case Fopen:
  case Fclose:
  case Fget:
  case Fput:
  case Fread:
  case Fwrite:
  case Fprintf:
  case Fscanf:
  case Fflush:
  case Fseek:
  case Ftell:
  case Feof:
  case Ferror:
  case Srand:
  case Rand:
  case TimeMs:
  case ClockMs:
  case Exit:
  case Abort:
  case Panic:
  case Assert:
    return true;
  default:
    return false;
  }
}

bool isTerminatingBuiltin(stdlib::BuiltinId builtin) {
  using enum stdlib::BuiltinId;
  return builtin == Exit || builtin == Abort || builtin == Panic || builtin == Assert;
}

void normalize(EffectSummary& summary) {
  std::sort(summary.accesses.begin(), summary.accesses.end(),
            [](const EffectAccess& left, const EffectAccess& right) {
              return std::tie(left.object, left.access, left.offset, left.byteLength) <
                     std::tie(right.object, right.access, right.offset, right.byteLength);
            });
  summary.accesses.erase(std::unique(summary.accesses.begin(), summary.accesses.end()),
                         summary.accesses.end());
}

bool equivalent(const EffectSummary& left, const EffectSummary& right) {
  return left.flags == right.flags && left.accesses == right.accesses;
}

class EffectVerifier final {
public:
  explicit EffectVerifier(const hir::TranslationUnit& unit) : unit_(unit) {
    for (const auto& function : unit_.functions) {
      Callable callable;
      callable.definition = function.get();
      callable.contract = &function->effectContract;
      for (std::size_t index = 0; index < function->parameters.size(); ++index) {
        const auto& parameter = function->parameters[index];
        callable.parameterNames.push_back(parameter.name);
        callable.parameterIndexes.emplace(parameter.name, index);
        if (parameter.templateName == "addr") {
          callable.addressParameterObjects.emplace(parameter.bindingName, parameter.name);
        }
      }
      callables_.emplace(function->name, std::move(callable));
    }
    for (const auto& function : unit_.externFunctions) {
      Callable callable;
      callable.external = &function;
      callable.contract = &function.effectContract;
      callable.parameterNames = function.parameterNames;
      for (std::size_t index = 0; index < callable.parameterNames.size(); ++index) {
        callable.parameterIndexes.emplace(callable.parameterNames[index], index);
      }
      callables_.emplace(function.name, std::move(callable));
    }
  }

  std::vector<std::string> verify() {
    for (const auto& function : unit_.functions) {
      base_.emplace(function->name, summarizeDirect(*function));
    }
    summaries_ = base_;
    for (std::size_t iteration = 0; iteration < unit_.functions.size() + 1U; ++iteration) {
      auto next = base_;
      for (const auto& function : unit_.functions) {
        auto& summary = next.at(function->name);
        for (const auto& call : base_.at(function->name).calls) {
          mergeCall(summary, callableFor(function->name), call);
        }
        normalize(summary);
      }
      bool changed = false;
      for (const auto& function : unit_.functions) {
        if (!equivalent(next.at(function->name), summaries_.at(function->name))) {
          changed = true;
          break;
        }
      }
      summaries_ = std::move(next);
      if (!changed) {
        break;
      }
    }

    std::vector<std::string> diagnostics;
    for (const auto& function : unit_.functions) {
      validateFunction(*function, summaries_.at(function->name), diagnostics);
      validateNoAliasCalls(function->name, base_.at(function->name), diagnostics);
    }
    return diagnostics;
  }

private:
  const Callable* callableFor(std::string_view name) const {
    const auto found = callables_.find(std::string(name));
    return found == callables_.end() ? nullptr : &found->second;
  }

  PointerRef pointerFor(const hir::Expr& expression, const Callable& callable) const {
    if (const auto* variable = dynamic_cast<const hir::VariableRef*>(&expression)) {
      if (const auto parameter = callable.addressParameterObjects.find(variable->bindingName);
          parameter != callable.addressParameterObjects.end()) {
        return PointerRef{PointerKind::External, parameter->second, 0U};
      }
      return PointerRef{PointerKind::Unknown, {}, std::nullopt};
    }
    if (const auto* address = dynamic_cast<const hir::AddressOfExpr*>(&expression)) {
      if (!isExternalStorage(address->storage)) {
        return PointerRef{PointerKind::Local, address->bindingName,
                          static_cast<std::uint64_t>(address->offset)};
      }
      return PointerRef{PointerKind::External, address->name,
                        static_cast<std::uint64_t>(address->offset)};
    }
    if (const auto* binary = dynamic_cast<const hir::BinaryExpr*>(&expression);
        binary != nullptr && binary->op == "+") {
      auto base = pointerFor(*binary->left, callable);
      const auto offset = integerLiteral(*binary->right);
      if (base.kind == PointerKind::External && base.offset && offset) {
        base.offset = *base.offset + *offset;
      } else if (base.kind == PointerKind::External) {
        base.kind = PointerKind::Unknown;
      }
      return base;
    }
    if (const auto* unsignedValue = dynamic_cast<const hir::UnsignedExpr*>(&expression)) {
      return pointerFor(*unsignedValue->operand, callable);
    }
    if (const auto* view = dynamic_cast<const hir::TemplateViewExpr*>(&expression)) {
      return pointerFor(*view->operand, callable);
    }
    return PointerRef{PointerKind::Unknown, {}, std::nullopt};
  }

  void addAccess(EffectSummary& summary, PointerRef pointer,
                 hir::EffectAccess access, std::optional<std::uint64_t> byteLength) const {
    if (pointer.kind == PointerKind::Local) {
      return;
    }
    if (pointer.kind != PointerKind::External) {
      summary.flags |= ObservedUnknown;
      return;
    }
    summary.accesses.push_back(
        EffectAccess{std::move(pointer.object), access, pointer.offset, byteLength});
  }

  void addStore(EffectSummary& summary, std::string_view name,
                hir::MemoryStorage storage, std::size_t offset,
                std::size_t byteLength) const {
    if (isExternalStorage(storage)) {
      summary.accesses.push_back(EffectAccess{std::string(name), hir::EffectAccess::Write,
                                              static_cast<std::uint64_t>(offset),
                                              static_cast<std::uint64_t>(byteLength)});
    }
  }

  void visitBuiltin(EffectSummary& summary, const Callable& callable,
                    stdlib::BuiltinId builtin,
                    const std::vector<std::unique_ptr<hir::Expr>>& arguments) const {
    using enum stdlib::BuiltinId;
    const auto argumentAccess = [&](std::size_t index, hir::EffectAccess access,
                                    std::optional<std::uint64_t> length) {
      if (index < arguments.size()) {
        addAccess(summary, pointerFor(*arguments[index], callable), access, length);
      } else {
        summary.flags |= ObservedUnknown;
      }
    };
    const auto lengthArgument = [&](std::size_t index) -> std::optional<std::uint64_t> {
      return index < arguments.size() ? integerLiteral(*arguments[index]) : std::nullopt;
    };
    switch (builtin) {
    case Alloc:
    case Calloc:
      summary.flags |= ObservedAllocate;
      break;
    case Realloc:
      summary.flags |= ObservedAllocate | ObservedFree;
      break;
    case Free:
      summary.flags |= ObservedFree;
      break;
    case Memset:
      argumentAccess(0U, hir::EffectAccess::Write, lengthArgument(2U));
      break;
    case Memcpy:
    case Memmove:
      argumentAccess(0U, hir::EffectAccess::Write, lengthArgument(2U));
      argumentAccess(1U, hir::EffectAccess::Read, lengthArgument(2U));
      break;
    case Memcmp:
      argumentAccess(0U, hir::EffectAccess::Read, lengthArgument(2U));
      argumentAccess(1U, hir::EffectAccess::Read, lengthArgument(2U));
      break;
    case Strlen:
    case Strchr:
      argumentAccess(0U, hir::EffectAccess::Read, std::nullopt);
      break;
    case Strcmp:
      argumentAccess(0U, hir::EffectAccess::Read, std::nullopt);
      argumentAccess(1U, hir::EffectAccess::Read, std::nullopt);
      break;
    case Strcpy:
    case Strcat:
      argumentAccess(0U, hir::EffectAccess::Write, std::nullopt);
      argumentAccess(1U, hir::EffectAccess::Read, std::nullopt);
      break;
    case Strncpy:
      argumentAccess(0U, hir::EffectAccess::Write, lengthArgument(2U));
      argumentAccess(1U, hir::EffectAccess::Read, lengthArgument(2U));
      break;
    case Fread:
      argumentAccess(0U, hir::EffectAccess::Write, std::nullopt);
      break;
    case Fwrite:
      argumentAccess(0U, hir::EffectAccess::Read, std::nullopt);
      break;
    default:
      break;
    }
    if (isIoBuiltin(builtin)) {
      summary.flags |= ObservedIo;
    }
    if (isTerminatingBuiltin(builtin)) {
      summary.flags |= ObservedThrow;
    }
  }

  void visitExpr(EffectSummary& summary, const Callable& callable,
                 const hir::Expr& expression) const {
    if (const auto* dereference = dynamic_cast<const hir::DerefExpr*>(&expression)) {
      addAccess(summary, pointerFor(*dereference->address, callable),
                hir::EffectAccess::Read,
                static_cast<std::uint64_t>(dereference->byteLength));
      visitExpr(summary, callable, *dereference->address);
    } else if (const auto* binary = dynamic_cast<const hir::BinaryExpr*>(&expression)) {
      visitExpr(summary, callable, *binary->left);
      visitExpr(summary, callable, *binary->right);
    } else if (const auto* unary = dynamic_cast<const hir::UnaryExpr*>(&expression)) {
      visitExpr(summary, callable, *unary->operand);
    } else if (const auto* ternary = dynamic_cast<const hir::TernaryExpr*>(&expression)) {
      visitExpr(summary, callable, *ternary->condition);
      visitExpr(summary, callable, *ternary->thenExpr);
      visitExpr(summary, callable, *ternary->elseExpr);
    } else if (const auto* unsignedValue = dynamic_cast<const hir::UnsignedExpr*>(&expression)) {
      visitExpr(summary, callable, *unsignedValue->operand);
    } else if (const auto* cast = dynamic_cast<const hir::IntegerCastExpr*>(&expression)) {
      visitExpr(summary, callable, *cast->operand);
    } else if (const auto* view = dynamic_cast<const hir::TemplateViewExpr*>(&expression)) {
      visitExpr(summary, callable, *view->operand);
    } else if (const auto* binary = dynamic_cast<const hir::FloatBinaryExpr*>(&expression)) {
      visitExpr(summary, callable, *binary->left);
      visitExpr(summary, callable, *binary->right);
    } else if (const auto* compare = dynamic_cast<const hir::FloatCompareExpr*>(&expression)) {
      visitExpr(summary, callable, *compare->left);
      visitExpr(summary, callable, *compare->right);
    } else if (const auto* conversion = dynamic_cast<const hir::ToFloatExpr*>(&expression)) {
      visitExpr(summary, callable, *conversion->operand);
    } else if (const auto* conversion = dynamic_cast<const hir::ToIntExpr*>(&expression)) {
      visitExpr(summary, callable, *conversion->operand);
    } else if (const auto* dynamic = dynamic_cast<const hir::DynamicByteViewExpr*>(&expression)) {
      visitExpr(summary, callable, *dynamic->source);
      if (dynamic->runtimeLength) visitExpr(summary, callable, *dynamic->runtimeLength);
    } else if (const auto* swap = dynamic_cast<const hir::ByteSwapExpr*>(&expression)) {
      visitExpr(summary, callable, *swap->source);
    } else if (const auto* assignment = dynamic_cast<const hir::AssignmentExpr*>(&expression)) {
      for (const auto& store : assignment->stores) visitStmt(summary, callable, *store);
      visitExpr(summary, callable, *assignment->result);
    } else if (const auto* call = dynamic_cast<const hir::CallExpr*>(&expression)) {
      for (const auto& argument : call->arguments) visitExpr(summary, callable, *argument);
      if (call->builtin == stdlib::BuiltinId::None) {
        summary.calls.push_back(CallSite{call->callee, &call->arguments});
      } else {
        visitBuiltin(summary, callable, call->builtin, call->arguments);
      }
    } else if (const auto* call = dynamic_cast<const hir::UserTemplateOpCallExpr*>(&expression)) {
      for (const auto& argument : call->arguments) visitExpr(summary, callable, *argument);
      summary.calls.push_back(CallSite{call->callee, &call->arguments});
    } else if (const auto* format = dynamic_cast<const hir::UserTemplateFormatCallExpr*>(&expression)) {
      visitExpr(summary, callable, *format->value);
      if (format->file) visitExpr(summary, callable, *format->file);
      summary.flags |= ObservedIo;
    }
  }

  void visitStmt(EffectSummary& summary, const Callable& callable,
                 const hir::Stmt& statement) const {
    if (const auto* list = dynamic_cast<const hir::StatementList*>(&statement)) {
      for (const auto& item : list->statements) visitStmt(summary, callable, *item);
    } else if (const auto* store = dynamic_cast<const hir::IntegerStore*>(&statement)) {
      addStore(summary, store->target, store->storage, store->offset, store->targetByteLength);
      visitExpr(summary, callable, *store->value);
    } else if (const auto* store = dynamic_cast<const hir::FloatStore*>(&statement)) {
      addStore(summary, store->target, store->storage, store->offset, store->targetByteLength);
      visitExpr(summary, callable, *store->value);
    } else if (const auto* store = dynamic_cast<const hir::StringStore*>(&statement)) {
      addStore(summary, store->target, store->storage, store->offset, store->targetByteLength);
    } else if (const auto* store = dynamic_cast<const hir::StringCopyStore*>(&statement)) {
      addStore(summary, store->target, store->targetStorage, store->targetOffset,
               store->targetByteLength);
      if (isExternalStorage(store->sourceStorage)) {
        summary.accesses.push_back(EffectAccess{store->source, hir::EffectAccess::Read,
                                                static_cast<std::uint64_t>(store->sourceOffset),
                                                static_cast<std::uint64_t>(store->sourceByteLength)});
      }
    } else if (const auto* store = dynamic_cast<const hir::BoolStore*>(&statement)) {
      addStore(summary, store->target, store->storage, store->offset, store->targetByteLength);
      visitExpr(summary, callable, *store->value);
    } else if (const auto* store = dynamic_cast<const hir::PointerStore*>(&statement)) {
      addAccess(summary, pointerFor(*store->address, callable), hir::EffectAccess::Write,
                static_cast<std::uint64_t>(store->targetByteLength));
      visitExpr(summary, callable, *store->address);
      visitExpr(summary, callable, *store->value);
    } else if (const auto* call = dynamic_cast<const hir::Call*>(&statement)) {
      for (const auto& argument : call->arguments) visitExpr(summary, callable, *argument);
      if (call->builtin == stdlib::BuiltinId::None) {
        summary.calls.push_back(CallSite{call->callee, &call->arguments});
      } else {
        visitBuiltin(summary, callable, call->builtin, call->arguments);
      }
    } else if (const auto* call = dynamic_cast<const hir::UserTemplateOpCall*>(&statement)) {
      for (const auto& argument : call->arguments) visitExpr(summary, callable, *argument);
      summary.calls.push_back(CallSite{call->callee, &call->arguments});
    } else if (const auto* format = dynamic_cast<const hir::UserTemplateFormatCall*>(&statement)) {
      visitExpr(summary, callable, *format->value);
      if (format->file) visitExpr(summary, callable, *format->file);
      summary.flags |= ObservedIo;
    } else if (const auto* call = dynamic_cast<const hir::MultiReturnCallStore*>(&statement)) {
      for (const auto& argument : call->arguments) visitExpr(summary, callable, *argument);
      summary.calls.push_back(CallSite{call->callee, &call->arguments});
      for (const auto& target : call->targets) {
        addStore(summary, target.name, target.storage, 0U, target.byteLength);
      }
    } else if (const auto* input = dynamic_cast<const hir::InputCallStore*>(&statement)) {
      if (input->file) visitExpr(summary, callable, *input->file);
      if (input->format) visitExpr(summary, callable, *input->format);
      summary.flags |= ObservedIo;
      for (const auto& target : input->countTargets) {
        addStore(summary, target.name, target.storage, target.offset, target.byteLength);
      }
      for (const auto& target : input->scanTargets) {
        addStore(summary, target.name, target.storage, target.offset, target.byteLength);
      }
    } else if (const auto* ret = dynamic_cast<const hir::Return*>(&statement)) {
      for (const auto& value : ret->values) visitExpr(summary, callable, *value);
    } else if (const auto* conditional = dynamic_cast<const hir::If*>(&statement)) {
      visitExpr(summary, callable, *conditional->condition);
      visitBlock(summary, callable, *conditional->thenBlock);
      if (conditional->elseBlock) visitBlock(summary, callable, *conditional->elseBlock);
    } else if (const auto* loop = dynamic_cast<const hir::While*>(&statement)) {
      visitExpr(summary, callable, *loop->condition);
      visitBlock(summary, callable, *loop->body);
    } else if (const auto* loop = dynamic_cast<const hir::For*>(&statement)) {
      if (loop->init) visitStmt(summary, callable, *loop->init);
      if (loop->condition) visitExpr(summary, callable, *loop->condition);
      for (const auto& post : loop->post) visitStmt(summary, callable, *post);
      visitBlock(summary, callable, *loop->body);
    } else if (const auto* label = dynamic_cast<const hir::Label*>(&statement)) {
      visitStmt(summary, callable, *label->statement);
    } else if (const auto* throwStmt = dynamic_cast<const hir::Throw*>(&statement)) {
      summary.flags |= ObservedThrow;
      if (throwStmt->delivery) visitStmt(summary, callable, *throwStmt->delivery);
    } else if (const auto* tryCatch = dynamic_cast<const hir::TryCatch*>(&statement)) {
      visitBlock(summary, callable, *tryCatch->tryBlock);
      visitBlock(summary, callable, *tryCatch->catchBlock);
    }
  }

  void visitBlock(EffectSummary& summary, const Callable& callable,
                  const hir::Block& block) const {
    for (const auto& statement : block.statements) visitStmt(summary, callable, *statement);
  }

  EffectSummary summarizeDirect(const hir::Function& function) const {
    EffectSummary summary;
    visitBlock(summary, *callableFor(function.name), *function.body);
    normalize(summary);
    return summary;
  }

  EffectSummary externalSummary(const Callable& callable) const {
    EffectSummary summary;
    const auto& contract = *callable.contract;
    if ((contract.flags & hir::EffectUnknown) != 0U) {
      summary.flags |= ObservedUnknown;
      return summary;
    }
    if ((contract.flags & hir::EffectAllocates) != 0U) summary.flags |= ObservedAllocate;
    if ((contract.flags & hir::EffectFrees) != 0U) summary.flags |= ObservedFree;
    if ((contract.flags & hir::EffectThrows) != 0U) summary.flags |= ObservedThrow;
    if ((contract.flags & hir::EffectIo) != 0U) summary.flags |= ObservedIo;
    for (const auto& range : contract.ranges) {
      const auto length = range.range == "all" ? std::nullopt
                                                 : literal::parseUnsignedIntegerLiteral(range.range);
      summary.accesses.push_back(EffectAccess{range.object, range.access, 0U, length});
    }
    normalize(summary);
    return summary;
  }

  void mergeCall(EffectSummary& into, const Callable* caller,
                 const CallSite& call) const {
    const auto* callee = callableFor(call.callee);
    if (caller == nullptr || callee == nullptr || call.arguments == nullptr) {
      into.flags |= ObservedUnknown;
      return;
    }
    const auto& source = callee->definition ? summaries_.at(callee->definition->name)
                                             : externalSummary(*callee);
    into.flags |= source.flags;
    for (const auto& access : source.accesses) {
      EffectAccess mapped = access;
      if (const auto parameter = callee->parameterIndexes.find(access.object);
          parameter != callee->parameterIndexes.end()) {
        if (parameter->second >= call.arguments->size()) {
          into.flags |= ObservedUnknown;
          continue;
        }
        const auto pointer = pointerFor(*call.arguments->at(parameter->second), *caller);
        if (pointer.kind == PointerKind::Local) continue;
        if (pointer.kind != PointerKind::External || !pointer.offset) {
          into.flags |= ObservedUnknown;
          continue;
        }
        mapped.object = pointer.object;
        if (mapped.offset) mapped.offset = *mapped.offset + *pointer.offset;
      }
      into.accesses.push_back(std::move(mapped));
    }
  }

  bool allowsRange(const hir::EffectContract& contract,
                   const EffectAccess& access) const {
    for (const auto& range : contract.ranges) {
      if (range.object != access.object || range.access != access.access) continue;
      if (range.range == "all") return true;
      const auto declared = literal::parseUnsignedIntegerLiteral(range.range);
      if (!declared || !access.offset || !access.byteLength) return true;
      if (*access.offset <= *declared && *access.byteLength <= *declared - *access.offset) {
        return true;
      }
    }
    return false;
  }

  void validateFunction(const hir::Function& function, const EffectSummary& summary,
                        std::vector<std::string>& diagnostics) const {
    const auto& contract = function.effectContract;
    if (!contract.isExplicit || (contract.flags & hir::EffectUnknown) != 0U) return;
    const auto report = [&](std::string_view effect) {
      diagnostics.push_back("effect contract of '" + function.name +
                            "' does not cover " + std::string(effect));
    };
    if ((summary.flags & ObservedAllocate) != 0U &&
        (contract.flags & hir::EffectAllocates) == 0U) report("allocation");
    if ((summary.flags & ObservedFree) != 0U &&
        (contract.flags & hir::EffectFrees) == 0U) report("deallocation");
    if ((summary.flags & ObservedIo) != 0U &&
        (contract.flags & hir::EffectIo) == 0U) report("I/O");
    if ((summary.flags & ObservedThrow) != 0U &&
        ((contract.flags & hir::EffectThrows) == 0U ||
         (contract.flags & hir::EffectPure) != 0U)) report("throw");
    for (const auto& access : summary.accesses) {
      if (!allowsRange(contract, access)) {
        report(access.access == hir::EffectAccess::Read ? "an external read"
                                                        : "an external write");
      }
    }
    if ((contract.flags & hir::EffectPure) != 0U &&
        (!summary.accesses.empty() ||
         (summary.flags & (ObservedAllocate | ObservedFree | ObservedThrow |
                           ObservedIo)) != 0U)) {
      report("the pure promise");
    }
    if ((contract.flags & hir::EffectReadonly) != 0U) {
      const bool writes = std::any_of(summary.accesses.begin(), summary.accesses.end(),
                                      [](const EffectAccess& access) {
                                        return access.access == hir::EffectAccess::Write;
                                      });
      if (writes ||
          (summary.flags & (ObservedAllocate | ObservedFree | ObservedIo)) != 0U) {
        report("the readonly promise");
      }
    }
    if ((contract.flags & hir::EffectNothrow) != 0U &&
        (summary.flags & ObservedThrow) != 0U) {
      report("the nothrow promise");
    }
  }

  void validateNoAliasCalls(std::string_view callerName, const EffectSummary& summary,
                            std::vector<std::string>& diagnostics) const {
    const auto* caller = callableFor(callerName);
    if (caller == nullptr) return;
    for (const auto& call : summary.calls) {
      const auto* callee = callableFor(call.callee);
      if (callee == nullptr || callee->contract == nullptr || call.arguments == nullptr) continue;
      for (const auto& [left, right] : callee->contract->noAlias) {
        const auto leftIndex = callee->parameterIndexes.find(left);
        const auto rightIndex = callee->parameterIndexes.find(right);
        if (leftIndex == callee->parameterIndexes.end() ||
            rightIndex == callee->parameterIndexes.end() ||
            leftIndex->second >= call.arguments->size() || rightIndex->second >= call.arguments->size()) {
          continue;
        }
        const auto leftPointer = pointerFor(*call.arguments->at(leftIndex->second), *caller);
        const auto rightPointer = pointerFor(*call.arguments->at(rightIndex->second), *caller);
        if (leftPointer.kind == PointerKind::Unknown || rightPointer.kind == PointerKind::Unknown ||
            leftPointer.object != rightPointer.object || leftPointer.offset != rightPointer.offset) {
          continue;
        }
        const auto hasPositiveRange = [&](std::string_view object) {
          return std::any_of(callee->contract->ranges.begin(), callee->contract->ranges.end(),
                             [object](const hir::EffectRange& range) {
                               if (range.object != object) return false;
                               const auto length = literal::parseUnsignedIntegerLiteral(range.range);
                               return length && *length != 0U;
                             });
        };
        if (hasPositiveRange(left) && hasPositiveRange(right)) {
          diagnostics.push_back("call to '" + call.callee + "' in '" +
                                std::string(callerName) +
                                "' violates its noalias contract");
        }
      }
    }
  }

  const hir::TranslationUnit& unit_;
  std::unordered_map<std::string, Callable> callables_;
  std::unordered_map<std::string, EffectSummary> base_;
  std::unordered_map<std::string, EffectSummary> summaries_;
};

} // namespace

void Analyzer::verifyEffectContracts(const hir::TranslationUnit& unit) {
  EffectVerifier verifier(unit);
  for (auto& diagnostic : verifier.verify()) {
    addDiagnostic(std::move(diagnostic));
  }
}

} // namespace hitsimple::sema
