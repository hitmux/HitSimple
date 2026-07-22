#include "safety/StaticSafetyAnalyzerImpl.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hitsimple::safety::detail {

void StaticSafetyAnalyzer::validateSafety(const hir::TranslationUnit &unit) {
  if (!options_.enabled) {
    return;
  }

  resetStaticSafetyState();
  if (unit.globalInit) {
    validateSafety(*unit.globalInit);
  }
  const auto globalBaseline = staticSafetyState();
  const auto recordGlobalBindings = [this](const auto &facts) {
    for (const auto &[bindingName, value] : facts) {
      (void)value;
      staticGlobalBindings_.insert(bindingName);
    }
  };
  recordGlobalBindings(globalBaseline.integerValues);
  recordGlobalBindings(globalBaseline.unsignedIntegerValues);
  recordGlobalBindings(globalBaseline.addressFacts);
  for (const auto &function : unit.functions) {
    restoreStaticSafetyState(globalBaseline);
    validateSafety(*function->body);
  }
}

bool StaticSafetyAnalyzer::validateSafety(const hir::Block &block) {
  SourceRangeScope sourceRange(*this, block.range);
  std::vector<std::string> localBindings;
  const auto collectLocalBindings = [&localBindings](const auto &self,
                                                     const hir::Stmt &statement)
      -> void {
    if (const auto *list = dynamic_cast<const hir::StatementList *>(&statement)) {
      for (const auto &item : list->statements) {
        self(self, *item);
      }
      return;
    }
    if (const auto *local = dynamic_cast<const hir::LocalMemory *>(&statement);
        local != nullptr && local->storage == hir::MemoryStorage::Local) {
      localBindings.push_back(local->bindingName);
    }
  };
  for (const auto &statement : block.statements) {
    collectLocalBindings(collectLocalBindings, *statement);
  }
  std::unordered_map<std::string, std::size_t> labelIndexes;
  for (std::size_t index = 0; index < block.statements.size(); ++index) {
    if (const auto *label =
            dynamic_cast<const hir::Label *>(block.statements[index].get())) {
      labelIndexes.emplace(label->label, index);
    }
  }

  if (labelIndexes.empty()) {
    StaticGotoContext context{staticGotoContext_, &labelIndexes, nullptr,
                              nullptr, &localBindings};
    auto *previousContext = staticGotoContext_;
    staticGotoContext_ = &context;
    for (const auto &statement : block.statements) {
      if (!validateSafety(*statement)) {
        staticGotoContext_ = previousContext;
        return false;
      }
    }
    staticGotoContext_ = previousContext;
    expireStaticLocalBindings(localBindings);
    return true;
  }

  // A label is a CFG join point.  Keep one entry state per statement and
  // revisit its successor only when an incoming edge removes a previously
  // known fact.  This both skips statements bypassed by `goto` and reaches a
  // fixed point for back edges without trusting facts from unreachable code.
  std::vector<std::optional<StaticSafetyState>> entryStates(
      block.statements.size());
  std::deque<std::size_t> worklist;
  StaticGotoContext context{staticGotoContext_, &labelIndexes, &entryStates,
                            &worklist, &localBindings};
  auto *previousContext = staticGotoContext_;
  staticGotoContext_ = &context;
  const auto enqueue = [this, &entryStates, &worklist](
                           std::size_t index,
                           const StaticSafetyState &incoming,
                           bool prioritizeFallthrough = false) {
    if (entryStates[index]) {
      const auto merged = mergedStaticSafetyStates(*entryStates[index], incoming);
      if (merged == *entryStates[index]) {
        return;
      }
      entryStates[index] = merged;
    } else {
      entryStates[index] = incoming;
    }
    if (prioritizeFallthrough) {
      worklist.push_front(index);
    } else {
      worklist.push_back(index);
    }
  };

  if (!block.statements.empty()) {
    enqueue(0, staticSafetyState());
  }

  std::optional<StaticSafetyState> exitState;

  while (!worklist.empty()) {
    const auto index = worklist.front();
    worklist.pop_front();
    restoreStaticSafetyState(*entryStates[index]);

    const auto &statement = *block.statements[index];
    if (!validateSafety(statement)) {
      continue;
    }
    const auto outgoing = staticSafetyState();
    if (index + 1U < block.statements.size()) {
      enqueue(index + 1U, outgoing, true);
    } else if (exitState) {
      exitState = mergedStaticSafetyStates(*exitState, outgoing);
    } else {
      exitState = outgoing;
    }
  }

  staticGotoContext_ = previousContext;
  if (!exitState) {
    return false;
  }
  restoreStaticSafetyState(*exitState);
  expireStaticLocalBindings(localBindings);
  return true;
}

bool StaticSafetyAnalyzer::validateSafety(const hir::Stmt &statement) {
  SourceRangeScope sourceRange(*this, statement.range);
  if (const auto *list = dynamic_cast<const hir::StatementList *>(&statement)) {
    for (const auto &child : list->statements) {
      if (!validateSafety(*child)) {
        return false;
      }
    }
    return true;
  }
  if (const auto *store = dynamic_cast<const hir::IntegerStore *>(&statement)) {
    validateSafety(*store->value);
    const auto signedValue = staticSignedInteger(*store->value);
    const auto unsignedValue = staticUnsignedInteger(*store->value);
    std::optional<bool> cstrTermination;
    if (const auto *character =
            dynamic_cast<const hir::CharacterLiteral *>(store->value.get());
        character != nullptr && character->byteLength == store->targetByteLength &&
        character->bytes.size() == store->targetByteLength) {
      cstrTermination = character->bytes.find('\0') != std::string::npos;
    }
    recordStaticAddressAssignment(store->bindingName, *store->value);
    const auto addressFact = staticAddressFacts_[store->bindingName];
    invalidateStaticBinding(store->bindingName);
    staticIntegerValues_[store->bindingName] = signedValue;
    staticUnsignedIntegerValues_[store->bindingName] = unsignedValue;
    staticAddressFacts_[store->bindingName] = addressFact;
    staticCStringTerminations_[store->bindingName] = cstrTermination;
    return true;
  }
  if (const auto *store = dynamic_cast<const hir::FloatStore *>(&statement)) {
    validateSafety(*store->value);
    invalidateStaticBinding(store->bindingName);
    return true;
  }
  if (const auto *store = dynamic_cast<const hir::BoolStore *>(&statement)) {
    validateSafety(*store->value);
    const auto signedValue = staticSignedInteger(*store->value);
    const auto unsignedValue = staticUnsignedInteger(*store->value);
    invalidateStaticBinding(store->bindingName);
    staticIntegerValues_[store->bindingName] = signedValue;
    staticUnsignedIntegerValues_[store->bindingName] = unsignedValue;
    return true;
  }
  if (const auto *store =
          dynamic_cast<const hir::ViewCopyStore *>(&statement)) {
    validateSafety(*store->value);
    auto cstrTermination = staticCStringTerminated(*store->value);
    if (const auto *character =
            dynamic_cast<const hir::CharacterLiteral *>(store->value.get());
        character != nullptr && character->byteLength == store->targetByteLength &&
        character->bytes.size() == store->targetByteLength) {
      cstrTermination = character->bytes.find('\0') != std::string::npos;
    }
    invalidateStaticBinding(store->bindingName);
    staticCStringTerminations_[store->bindingName] = cstrTermination;
    return true;
  }
  if (const auto *store = dynamic_cast<const hir::StringStore *>(&statement)) {
    invalidateStaticBinding(store->bindingName);
    staticCStringTerminations_[store->bindingName] = true;
    return true;
  }
  if (const auto *store =
          dynamic_cast<const hir::StringCopyStore *>(&statement)) {
    const auto source = staticCStringTerminations_.find(store->sourceBindingName);
    const auto cstrTermination = source == staticCStringTerminations_.end()
                                     ? std::optional<bool>{}
                                     : source->second;
    invalidateStaticBinding(store->bindingName);
    staticCStringTerminations_[store->bindingName] = cstrTermination;
    return true;
  }
  if (const auto *store = dynamic_cast<const hir::PointerStore *>(&statement)) {
    const auto address = staticSignedInteger(*store->address);
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
    validateStaticAddressAccess(*store->address, "store");
    validateSafety(*store->address);
    validateSafety(*store->value);
    invalidateStaticFactsOverlapping(staticAddressRange(*store->address),
                                     store->targetByteLength);
    return true;
  }
  if (const auto *call = dynamic_cast<const hir::Call *>(&statement)) {
    if ((call->builtin == stdlib::BuiltinId::Free ||
         call->builtin == stdlib::BuiltinId::Realloc) &&
        !call->arguments.empty()) {
      validateStaticDynamicBase(*call->arguments.front(), call->callee);
    }
    for (const auto &argument : call->arguments) {
      validateSafety(*argument);
    }
    if ((call->builtin == stdlib::BuiltinId::Free ||
         call->builtin == stdlib::BuiltinId::Realloc) &&
        !call->arguments.empty()) {
      if (call->builtin == stdlib::BuiltinId::Free) {
        (void)releaseStaticDynamicObject(*call->arguments.front());
      } else {
        const auto extent = call->arguments.size() == 2U
                                ? staticUnsignedInteger(*call->arguments[1])
                                : std::optional<std::uint64_t>{};
        if (extent && *extent == 0) {
          (void)releaseStaticDynamicObject(*call->arguments.front());
        } else if (const auto input =
                       staticAddressFact(*call->arguments.front());
                   input && input->origin == StaticAddressOrigin::DynamicObject) {
          const auto state = staticDynamicObjectStates_.find(input->dynamicObjectId);
          if (state != staticDynamicObjectStates_.end()) {
            state->second = StaticDynamicObjectState::Unknown;
          }
        }
      }
    }
    if (call->builtin == stdlib::BuiltinId::None) {
      invalidateStaticGlobalFacts();
      for (const auto &argument : call->arguments) {
        invalidateStaticFactsOverlapping(staticMemoryOperandRange(*argument),
                                         std::nullopt);
      }
    }
    if ((call->builtin == stdlib::BuiltinId::Memset ||
         call->builtin == stdlib::BuiltinId::Memcpy ||
         call->builtin == stdlib::BuiltinId::Memmove) &&
        call->arguments.size() == 3U) {
      invalidateStaticFactsOverlapping(
          staticMemoryOperandRange(*call->arguments[0]),
          staticUnsignedInteger(*call->arguments[2]));
    }
    if (call->builtin == stdlib::BuiltinId::Fread &&
        call->arguments.size() == 4U) {
      const auto size = staticUnsignedInteger(*call->arguments[1]);
      const auto count = staticUnsignedInteger(*call->arguments[2]);
      const auto byteLength = size && count
                                  ? multiplyUnsignedIntegers(*size, *count)
                                  : std::optional<std::uint64_t>{};
      invalidateStaticFactsOverlapping(
          staticMemoryOperandRange(*call->arguments[0]), byteLength);
    }
    return true;
  }
  if (const auto *call =
          dynamic_cast<const hir::UserTemplateOpCall *>(&statement)) {
    for (const auto &argument : call->arguments) {
      validateSafety(*argument);
    }
    if (!call->arguments.empty()) {
      invalidateStaticFactsOverlapping(
          staticMemoryOperandRange(*call->arguments.front()), std::nullopt);
    }
    invalidateStaticGlobalFacts();
    return true;
  }
  if (const auto *call =
          dynamic_cast<const hir::UserTemplateFormatCall *>(&statement)) {
    validateSafety(*call->value);
    if (call->file) {
      validateSafety(*call->file);
    }
    invalidateStaticGlobalFacts();
    return true;
  }
  if (const auto *call =
          dynamic_cast<const hir::MultiReturnCallStore *>(&statement)) {
    for (const auto &argument : call->arguments) {
      validateSafety(*argument);
    }
    for (const auto &target : call->targets) {
      invalidateStaticBinding(target.bindingName);
    }
    invalidateStaticGlobalFacts();
    return true;
  }
  if (const auto *call =
          dynamic_cast<const hir::InputCallStore *>(&statement)) {
    if (call->file) {
      validateSafety(*call->file);
    }
    validateSafety(*call->format);
    for (const auto &target : call->countTargets) {
      invalidateStaticBinding(target.bindingName);
    }
    for (const auto &target : call->scanTargets) {
      invalidateStaticBinding(target.bindingName);
    }
    return true;
  }
  if (const auto *ret = dynamic_cast<const hir::Return *>(&statement)) {
    for (const auto &value : ret->values) {
      validateSafety(*value);
    }
    return false;
  }
  if (const auto *ifStmt = dynamic_cast<const hir::If *>(&statement)) {
    validateSafety(*ifStmt->condition);
    const auto condition = staticBooleanValue(*ifStmt->condition);
    if (condition) {
      if (*condition) {
        return validateSafety(*ifStmt->thenBlock);
      } else if (ifStmt->elseBlock) {
        return validateSafety(*ifStmt->elseBlock);
      }
      return true;
    }

    const auto entry = staticSafetyState();
    const bool thenFallsThrough = validateSafety(*ifStmt->thenBlock);
    const auto thenState = staticSafetyState();
    restoreStaticSafetyState(entry);
    bool elseFallsThrough = true;
    if (ifStmt->elseBlock) {
      elseFallsThrough = validateSafety(*ifStmt->elseBlock);
    }
    const auto elseState = staticSafetyState();
    if (thenFallsThrough && elseFallsThrough) {
      mergeStaticSafetyStates(thenState, elseState);
      return true;
    }
    if (thenFallsThrough) {
      restoreStaticSafetyState(thenState);
      return true;
    }
    if (elseFallsThrough) {
      restoreStaticSafetyState(elseState);
      return true;
    }
    return false;
  }
  if (const auto *whileStmt = dynamic_cast<const hir::While *>(&statement)) {
    validateSafety(*whileStmt->condition);
    const auto condition = staticBooleanValue(*whileStmt->condition);
    if (condition && !*condition) {
      return true;
    }
    const auto entry = staticSafetyState();
    std::optional<StaticSafetyState> breakState;
    std::optional<StaticSafetyState> continueState;
    StaticLoopContext loop{staticLoopContext_, staticGotoContext_, &breakState,
                           &continueState};
    auto *previousLoop = staticLoopContext_;
    staticLoopContext_ = &loop;
    const bool bodyFallsThrough = validateSafety(*whileStmt->body);
    const auto bodyState = staticSafetyState();
    staticLoopContext_ = previousLoop;
    restoreStaticSafetyState(entry);
    std::optional<StaticSafetyState> exitState;
    if (!condition || !*condition) {
      exitState = entry;
    }
    if (breakState) {
      exitState = exitState ? mergedStaticSafetyStates(*exitState, *breakState)
                            : *breakState;
    }
    if (bodyFallsThrough || continueState) {
      const auto nextIteration = bodyFallsThrough
                                     ? (continueState
                                            ? mergedStaticSafetyStates(
                                                  bodyState, *continueState)
                                            : bodyState)
                                     : *continueState;
      if (!condition || !*condition) {
        exitState = exitState
                        ? mergedStaticSafetyStates(*exitState,
                                                   mergedStaticSafetyStates(
                                                       entry, nextIteration))
                        : mergedStaticSafetyStates(entry, nextIteration);
      }
    }
    if (exitState) {
      restoreStaticSafetyState(*exitState);
    }
    return true;
  }
  if (const auto *forStmt = dynamic_cast<const hir::For *>(&statement)) {
    if (forStmt->init) {
      if (!validateSafety(*forStmt->init)) {
        return false;
      }
    }
    if (forStmt->condition) {
      validateSafety(*forStmt->condition);
    }
    if (forStmt->condition) {
      if (const auto condition = staticBooleanValue(*forStmt->condition);
          condition && !*condition) {
        return true;
      }
    }
    const auto entry = staticSafetyState();
    bool bodyFallsThrough = validateSafety(*forStmt->body);
    if (bodyFallsThrough) {
      for (const auto &post : forStmt->post) {
        if (!validateSafety(*post)) {
          bodyFallsThrough = false;
          break;
        }
      }
    }
    const auto bodyState = staticSafetyState();
    restoreStaticSafetyState(entry);
    if (bodyFallsThrough) {
      mergeStaticSafetyStates(entry, bodyState);
    }
    return true;
  }
  if (const auto *gotoStmt = dynamic_cast<const hir::Goto *>(&statement)) {
    (void)enqueueStaticGoto(gotoStmt->label, staticSafetyState());
    return false;
  }
  if (const auto *label = dynamic_cast<const hir::Label *>(&statement)) {
    return validateSafety(*label->statement);
  }
  if (const auto *throwStmt = dynamic_cast<const hir::Throw *>(&statement)) {
    if (throwStmt->delivery) {
      validateSafety(*throwStmt->delivery);
    }
    return false;
  }
  if (const auto *tryCatch = dynamic_cast<const hir::TryCatch *>(&statement)) {
    const auto entry = staticSafetyState();
    const bool tryFallsThrough = validateSafety(*tryCatch->tryBlock);
    const auto tryState = staticSafetyState();
    restoreStaticSafetyState(entry);
    const bool catchFallsThrough = validateSafety(*tryCatch->catchBlock);
    const auto catchState = staticSafetyState();
    if (tryFallsThrough && catchFallsThrough) {
      mergeStaticSafetyStates(tryState, catchState);
      return true;
    }
    if (tryFallsThrough) {
      restoreStaticSafetyState(tryState);
      return true;
    }
    if (catchFallsThrough) {
      restoreStaticSafetyState(catchState);
      return true;
    }
    return false;
  }
  if (dynamic_cast<const hir::Break *>(&statement) != nullptr) {
    if (staticLoopContext_ != nullptr && staticLoopContext_->breakState != nullptr) {
      const auto outgoing =
          exitStaticScopes(staticLoopContext_->exitScope, staticSafetyState());
      auto &target = *staticLoopContext_->breakState;
      target = target ? mergedStaticSafetyStates(*target, outgoing) : outgoing;
    }
    return false;
  }
  if (dynamic_cast<const hir::Continue *>(&statement) != nullptr) {
    if (staticLoopContext_ != nullptr &&
        staticLoopContext_->continueState != nullptr) {
      const auto outgoing =
          exitStaticScopes(staticLoopContext_->exitScope, staticSafetyState());
      auto &target = *staticLoopContext_->continueState;
      target = target ? mergedStaticSafetyStates(*target, outgoing) : outgoing;
    }
    return false;
  }
  return true;
}

void StaticSafetyAnalyzer::validateSafety(const hir::Expr &expression) {
  SourceRangeScope sourceRange(*this, expression.range);
  if (const auto *deref = dynamic_cast<const hir::DerefExpr *>(&expression)) {
    const auto address = staticSignedInteger(*deref->address);
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
    validateStaticAddressAccess(*deref->address, "dereference");
    validateSafety(*deref->address);
    return;
  }
  if (const auto *binary = dynamic_cast<const hir::BinaryExpr *>(&expression)) {
    if (binary->operation == hir::BinaryOperator::LogicalAnd ||
        binary->operation == hir::BinaryOperator::LogicalOr) {
      validateSafety(*binary->left);
      const auto left = staticBooleanValue(*binary->left);
      const bool shortCircuits =
          left && ((binary->operation == hir::BinaryOperator::LogicalAnd &&
                    !*left) ||
                   (binary->operation == hir::BinaryOperator::LogicalOr &&
                    *left));
      if (shortCircuits) {
        return;
      }
      if (left) {
        validateSafety(*binary->right);
        return;
      }

      const auto skippedRight = staticSafetyState();
      validateSafety(*binary->right);
      const auto evaluatedRight = staticSafetyState();
      mergeStaticSafetyStates(skippedRight, evaluatedRight);
      return;
    }

    const auto op = std::string(hir::toString(binary->operation));
    if (binary->operationKind ==
        hir::StandardOperationKind::StandardCStringCompare) {
      const auto validateCStringOperand = [this](const hir::Expr &operand) {
        if (const auto terminated = staticCStringTerminated(operand);
            terminated && !*terminated) {
          addDiagnostic(
              "static safety check failed: missing terminating byte in cstr");
        }
      };
      validateCStringOperand(*binary->left);
      validateCStringOperand(*binary->right);
    }
    const auto right = staticSignedInteger(*binary->right);
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
    const auto condition = staticBooleanValue(*ternary->condition);
    if (condition) {
      validateSafety(*(*condition ? ternary->thenExpr : ternary->elseExpr));
      return;
    }
    const auto entry = staticSafetyState();
    validateSafety(*ternary->thenExpr);
    const auto thenState = staticSafetyState();
    restoreStaticSafetyState(entry);
    validateSafety(*ternary->elseExpr);
    const auto elseState = staticSafetyState();
    mergeStaticSafetyStates(thenState, elseState);
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
  if (const auto *test =
          dynamic_cast<const hir::BooleanTestExpr *>(&expression)) {
    validateSafety(*test->operand);
    return;
  }
  if (const auto *call =
          dynamic_cast<const hir::UserTemplateOpCallExpr *>(&expression)) {
    for (const auto &argument : call->arguments) {
      validateSafety(*argument);
    }
    invalidateStaticGlobalFacts();
    return;
  }
  if (const auto *call =
          dynamic_cast<const hir::UserTemplateFormatCallExpr *>(&expression)) {
    validateSafety(*call->value);
    if (call->file) {
      validateSafety(*call->file);
    }
    invalidateStaticGlobalFacts();
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
      return staticUnsignedInteger(operand);
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
    if (call->builtin == stdlib::BuiltinId::Realloc &&
        !call->arguments.empty()) {
      validateStaticDynamicBase(*call->arguments.front(), call->callee);
    }
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
  if (options_.runtimeChecksEnabled && call->builtin == stdlib::BuiltinId::Abs &&
        call->arguments.size() == 1U) {
      const auto value = staticSignedInteger(*call->arguments.front());
      const auto minimum = signedMinimumForByteLength(call->byteLength);
      if (value && minimum && *value == *minimum) {
        addDiagnostic(
            "static safety check failed: abs of minimum signed value");
      }
    }
    for (const auto &argument : call->arguments) {
      validateSafety(*argument);
    }
    if (call->builtin == stdlib::BuiltinId::None) {
      invalidateStaticGlobalFacts();
      for (const auto &argument : call->arguments) {
        invalidateStaticFactsOverlapping(staticMemoryOperandRange(*argument),
                                         std::nullopt);
      }
    }
    if ((call->builtin == stdlib::BuiltinId::Memset ||
         call->builtin == stdlib::BuiltinId::Memcpy ||
         call->builtin == stdlib::BuiltinId::Memmove) &&
        call->arguments.size() == 3U) {
      invalidateStaticFactsOverlapping(
          staticMemoryOperandRange(*call->arguments[0]),
          staticUnsignedInteger(*call->arguments[2]));
    }
    if (call->builtin == stdlib::BuiltinId::Fread &&
        call->arguments.size() == 4U) {
      const auto size = staticUnsignedInteger(*call->arguments[1]);
      const auto count = staticUnsignedInteger(*call->arguments[2]);
      const auto byteLength = size && count
                                  ? multiplyUnsignedIntegers(*size, *count)
                                  : std::optional<std::uint64_t>{};
      invalidateStaticFactsOverlapping(
          staticMemoryOperandRange(*call->arguments[0]), byteLength);
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

} // namespace hitsimple::safety::detail
