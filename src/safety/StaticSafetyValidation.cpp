#include "safety/StaticSafetyAnalyzerImpl.h"

#include <algorithm>
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
  staticPotentiallyReboundBindings_ = hir::reboundBindings(unit);
  for (const auto &function : unit.functions) {
    if (function->usesViewAbi) {
      auto &copies = staticViewParameterCopies_[function->name];
      for (const auto &parameter : function->parameters) {
        copies.push_back(parameter.viewIsCopy);
      }
    }
  }
  for (const auto &global : unit.globals) {
    staticDefinitionByteLengths_[global.bindingName] = global.byteLength;
    staticGlobalBindings_.insert(global.bindingName);
  }
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
    // A function can execute after another invocation changed a global name.
    // Initializer provenance cannot establish its entry binding address.
    for (const auto &bindingName : staticGlobalBindings_) {
      if (staticPotentiallyReboundBindings_.contains(bindingName)) {
        staticBindingAddressFacts_[bindingName] = std::nullopt;
      }
    }
    for (const auto &parameter : function->parameters) {
      staticDefinitionByteLengths_[parameter.bindingName] = parameter.byteLength;
    }
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
  if (const auto *local = dynamic_cast<const hir::LocalMemory *>(&statement)) {
    staticDefinitionByteLengths_[local->bindingName] = local->byteLength;
    if (local->storage == hir::MemoryStorage::StaticLocal) {
      staticGlobalBindings_.insert(local->bindingName);
      if (staticPotentiallyReboundBindings_.contains(local->bindingName)) {
        staticBindingAddressFacts_[local->bindingName] = std::nullopt;
      }
    } else if (local->storage == hir::MemoryStorage::Local) {
      // Each automatic declaration execution starts with its definition.
      staticBindingAddressFacts_.erase(local->bindingName);
      invalidateStaticBinding(local->bindingName);
    }
    return true;
  }
  if (const auto *rebind = dynamic_cast<const hir::BindingRebind *>(&statement)) {
    validateSafety(*rebind->address);
    recordStaticAddressAssignment("$rebind.address", *rebind->address);
    staticBindingAddressFacts_[rebind->bindingName] =
        staticAddressFacts_["$rebind.address"];
    staticAddressFacts_.erase("$rebind.address");
    // Rebinding itself neither reads nor writes either object's bytes.
    return true;
  }
  if (const auto *capture = dynamic_cast<const hir::CaptureView *>(&statement)) {
    const auto &source = *capture->source;
    if (source.result.isAddressable) {
      validateStaticViewFormation(source);
    } else {
      validateSafety(source);
    }
    recordStaticViewCapture(capture->captureName, source,
                            source.result.isAddressable);
    return true;
  }
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
    recordStaticAddressAssignment("$store.address", *store->value);
    auto addressFact = staticAddressFacts_["$store.address"];
    staticAddressFacts_.erase("$store.address");
    if (store->targetByteLength < sizeof(void *) && addressFact &&
        addressFact->origin != StaticAddressOrigin::Null) {
      // Truncated pointer-valued bytes no longer identify the source object.
      addressFact = std::nullopt;
    }
    const auto key = prepareStaticNamedStore(store->bindingName, store->offset,
                                             store->targetByteLength);
    if (key) {
      staticIntegerValues_[*key] = signedValue;
      staticUnsignedIntegerValues_[*key] = unsignedValue;
      staticAddressFacts_[*key] = addressFact;
      staticCStringTerminations_[*key] = cstrTermination;
    }
    return true;
  }
  if (const auto *store = dynamic_cast<const hir::FloatStore *>(&statement)) {
    validateSafety(*store->value);
    (void)prepareStaticNamedStore(store->bindingName, store->offset,
                                  store->targetByteLength);
    return true;
  }
  if (const auto *store = dynamic_cast<const hir::BoolStore *>(&statement)) {
    validateSafety(*store->value);
    const auto signedValue = staticSignedInteger(*store->value);
    const auto unsignedValue = staticUnsignedInteger(*store->value);
    const auto key = prepareStaticNamedStore(store->bindingName, store->offset,
                                             store->targetByteLength);
    if (key) {
      staticIntegerValues_[*key] = signedValue;
      staticUnsignedIntegerValues_[*key] = unsignedValue;
    }
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
    const auto key = prepareStaticNamedStore(store->bindingName, store->offset,
                                             store->targetByteLength);
    if (key) {
      staticCStringTerminations_[*key] = cstrTermination;
    }
    return true;
  }
  if (const auto *store = dynamic_cast<const hir::StringStore *>(&statement)) {
    const auto key = prepareStaticNamedStore(store->bindingName, store->offset,
                                             store->targetByteLength);
    if (key) {
      staticCStringTerminations_[*key] = true;
    }
    return true;
  }
  if (const auto *store =
          dynamic_cast<const hir::StringCopyStore *>(&statement)) {
    if (store->sourceView) {
      if (store->sourceView->result.templateName == "cstr") {
        validateStaticCStringRead(*store->sourceView);
      } else {
        validateStaticViewFormation(*store->sourceView);
        if (store->targetByteLength > 1 && store->sourceByteLength != 0) {
          validateStaticMemoryAccess(staticViewAddressFact(*store->sourceView),
                                     1, "cstr read");
        }
      }
    }
    if (!store->sourceView) {
      const auto sourceFact = staticBindingAddressFact(store->sourceBindingName,
          store->sourceOffset, store->sourceByteLength);
      validateStaticMemoryAccess(sourceFact,
          store->targetByteLength > 1
              ? std::min<std::size_t>(1, store->sourceByteLength) : 0, "load");
    }
    const auto key = prepareStaticNamedStore(store->bindingName,
        store->targetOffset, store->targetByteLength);
    if (key) {
      // Every successful string assignment writes a destination terminator,
      // including explicit %s= repairing unterminated raw source bytes.
      staticCStringTerminations_[*key] = true;
    }
    return true;
  }
  if (const auto *store = dynamic_cast<const hir::PointerStore *>(&statement)) {
    validateStaticMemoryAccess(staticAddressFact(*store->address),
                               store->targetByteLength, "store");
    validateSafety(*store->address);
    validateSafety(*store->value);
    invalidateStaticFactsOverlapping(staticAddressRange(*store->address),
                                     store->targetByteLength);
    return true;
  }
  if (const auto *call = dynamic_cast<const hir::Call *>(&statement)) {
    const auto arguments = prepareStaticCallArguments(call->builtin,
        call->overloadIndex, call->formatArgumentKinds, call->arguments);
    if ((call->builtin == stdlib::BuiltinId::Free ||
         call->builtin == stdlib::BuiltinId::Realloc) &&
        !arguments.empty()) {
      validateStaticDynamicBase(*arguments.front(), call->callee);
    }
    const auto memoryOperands = validateStaticMemoryOperands(call->builtin,
                                                             arguments);
    for (std::size_t index = memoryOperands; index < arguments.size(); ++index) {
      validateStaticCallArgument(call->builtin, call->overloadIndex,
          call->formatArgumentKinds, index, *arguments[index]);
    }
    if ((call->builtin == stdlib::BuiltinId::Free ||
         call->builtin == stdlib::BuiltinId::Realloc) &&
        !arguments.empty()) {
      if (call->builtin == stdlib::BuiltinId::Free) {
        (void)releaseStaticDynamicObject(*arguments.front());
      } else {
        const auto extent = arguments.size() == 2U
                                ? staticUnsignedInteger(*arguments[1])
                                : std::optional<std::uint64_t>{};
        if (extent && *extent == 0) {
          (void)releaseStaticDynamicObject(*arguments.front());
        } else if (const auto input =
                       staticAddressFact(*arguments.front());
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
      for (const auto &argument : arguments) {
        invalidateStaticFactsOverlapping(staticMemoryOperandRange(*argument),
                                         std::nullopt);
      }
    }
    if ((call->builtin == stdlib::BuiltinId::Memset ||
         call->builtin == stdlib::BuiltinId::Memcpy ||
         call->builtin == stdlib::BuiltinId::Memmove) &&
        arguments.size() == 3U) {
      invalidateStaticFactsOverlapping(
          staticMemoryOperandRange(*arguments[0]),
          staticUnsignedInteger(*arguments[2]));
    }
    if (call->builtin == stdlib::BuiltinId::Fread &&
        arguments.size() == 4U) {
      const auto size = staticUnsignedInteger(*arguments[1]);
      const auto count = staticUnsignedInteger(*arguments[2]);
      const auto byteLength = size && count
                                  ? multiplyUnsignedIntegers(*size, *count)
                                  : std::optional<std::uint64_t>{};
      invalidateStaticFactsOverlapping(
          staticViewRange(*arguments[0]), byteLength);
    }
    if ((call->builtin == stdlib::BuiltinId::Strcpy ||
         call->builtin == stdlib::BuiltinId::Strncpy ||
         call->builtin == stdlib::BuiltinId::Strcat) && !arguments.empty()) {
      invalidateStaticFactsOverlapping(staticViewRange(*arguments[0]), std::nullopt);
    }
    releaseStaticCallArguments(arguments);
    return true;
  }
  if (const auto *call =
          dynamic_cast<const hir::UserTemplateOpCall *>(&statement)) {
    validateStaticUserCallArguments(call->callee, call->arguments);
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
    invalidateStaticGlobalFacts();
    for (const auto &target : call->targets) {
      (void)prepareStaticNamedStore(target.bindingName, 0, target.byteLength);
    }
    return true;
  }
  if (const auto *call =
          dynamic_cast<const hir::InputCallStore *>(&statement)) {
    if (call->file) {
      validateSafety(*call->file);
    }
    validateStaticCStringRead(*call->format);
    for (const auto &target : call->countTargets) {
      (void)prepareStaticNamedStore(target.bindingName, target.offset,
                                    target.byteLength);
    }
    for (const auto &target : call->scanTargets) {
      (void)prepareStaticNamedStore(target.bindingName, target.offset,
                                    target.byteLength);
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
  if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
    validateStaticMemoryAccess(staticBindingAddressFact(variable->bindingName,
        variable->offset, variable->byteLength), variable->byteLength, "load");
    return;
  }
  if (const auto *capture = dynamic_cast<const hir::CapturedViewRef *>(&expression)) {
    const auto location = staticAddressFacts_.find(capture->captureName + ".location");
    if (location != staticAddressFacts_.end()) {
      validateStaticMemoryAccess(location->second,
          capture->result.staticByteLength, "captured View read");
    }
    return;
  }
  if (const auto *deref = dynamic_cast<const hir::DerefExpr *>(&expression)) {
    const auto fact = staticAddressFact(*deref->address);
    // Keep the existing dereference wording for lifetime/null diagnostics.
    validateStaticAddressAccess(fact, "dereference");
    if (fact && fact->origin == StaticAddressOrigin::Null) {
      addDiagnostic("static safety check failed: null address dereference");
    } else if (fact && fact->range) {
      auto rangeOnly = fact;
      rangeOnly->origin = StaticAddressOrigin::NonDynamicObject;
      validateStaticMemoryAccess(rangeOnly, deref->byteLength, "load");
    }
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

    if (binary->viewOffsetBounds) {
      // Indexing/slicing constrains the source View even if a rebound name
      // currently points at a larger live allocation. This also validates
      // formation-only uses such as &name[index] and length(name[index]).
      validateSafety(*binary->left);
      validateSafety(*binary->right);
      const auto offset = staticIndexOffset(*binary->right);
      const auto &bounds = *binary->viewOffsetBounds;
      const auto widePositive = !offset &&
              binary->right->result.staticByteLength > sizeof(std::uint64_t)
          ? staticUnsignedInteger(*binary->right) : std::nullopt;
      if (bounds.resultByteLength > bounds.sourceByteLength ||
          (offset && (*offset < 0 || static_cast<std::uint64_t>(*offset) >
                       bounds.sourceByteLength - bounds.resultByteLength)) ||
          (widePositive && *widePositive >
                           bounds.sourceByteLength - bounds.resultByteLength)) {
        addDiagnostic("static safety check failed: index or slice out of bounds");
      }
      return;
    }
    const auto op = std::string(hir::toString(binary->operation));
    if (binary->operationKind ==
        hir::StandardOperationKind::StandardCStringCompare) {
      std::vector<std::unique_ptr<hir::Expr>> operands;
      for (const auto* operand : {binary->left.get(), binary->right.get()}) {
        validateStaticViewFormation(*operand);
        operands.push_back(captureStaticEvaluatedArgument(*operand,
            operand->result.isAddressable));
      }
      for (const auto& operand : operands) validateStaticCStringRead(*operand);
      releaseStaticCallArguments(operands);
      return;
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
    if (view->templateName != "cstr" &&
        view->operand->result.templateName == "cstr") {
      validateStaticViewFormation(*view->operand);
      validateStaticMemoryAccess(staticViewAddressFact(*view->operand),
                                  view->result.staticByteLength, "load");
    } else {
      validateSafety(*view->operand);
    }
    return;
  }
  if (const auto *test =
          dynamic_cast<const hir::BooleanTestExpr *>(&expression)) {
    validateSafety(*test->operand);
    return;
  }
  if (const auto *call =
          dynamic_cast<const hir::UserTemplateOpCallExpr *>(&expression)) {
    validateStaticUserCallArguments(call->callee, call->arguments);
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
    if (call->builtin == stdlib::BuiltinId::Length) {
      for (const auto &argument : call->arguments) {
        validateStaticViewFormation(*argument);
      }
      return;
    }
    const auto arguments = prepareStaticCallArguments(call->builtin,
        call->overloadIndex, call->formatArgumentKinds, call->arguments);
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
        !arguments.empty()) {
      validateStaticDynamicBase(*arguments.front(), call->callee);
    }
    if (call->builtin == stdlib::BuiltinId::Calloc &&
        arguments.size() == 2U) {
      reportProductOverflow(*arguments[1], *arguments[0],
                            call->callee);
    }
    if ((call->builtin == stdlib::BuiltinId::Fread ||
         call->builtin == stdlib::BuiltinId::Fwrite) &&
        arguments.size() == 4U) {
      reportProductOverflow(*arguments[1], *arguments[2],
                            call->callee);
      const auto size = staticUnsignedValue(*arguments[1]);
      const auto count = staticUnsignedValue(*arguments[2]);
      if (size && count &&
          (*size == 0 ||
           *count <= std::numeric_limits<std::uint64_t>::max() / *size)) {
        const auto range = staticViewRange(*arguments[0]);
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
        arguments.size() == 3U) {
      const auto byteLength = staticUnsignedValue(*arguments[2]);
      const auto destination = staticMemoryOperandRange(*arguments[0]);
      const auto source = call->builtin == stdlib::BuiltinId::Memset
                              ? std::optional<StaticAddressRange>{}
                              : staticMemoryOperandRange(*arguments[1]);
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
        arguments.size() == 1U) {
      const auto value = staticSignedInteger(*arguments.front());
      const auto minimum = signedMinimumForByteLength(call->byteLength);
      if (value && minimum && *value == *minimum) {
        addDiagnostic(
            "static safety check failed: abs of minimum signed value");
      }
    }
    const auto memoryOperands = validateStaticMemoryOperands(call->builtin,
                                                             arguments);
    for (std::size_t index = memoryOperands; index < arguments.size(); ++index) {
      validateStaticCallArgument(call->builtin, call->overloadIndex,
          call->formatArgumentKinds, index, *arguments[index]);
    }
    if (call->builtin == stdlib::BuiltinId::None) {
      invalidateStaticGlobalFacts();
      for (const auto &argument : arguments) {
        invalidateStaticFactsOverlapping(staticMemoryOperandRange(*argument),
                                         std::nullopt);
      }
    }
    if ((call->builtin == stdlib::BuiltinId::Memset ||
         call->builtin == stdlib::BuiltinId::Memcpy ||
         call->builtin == stdlib::BuiltinId::Memmove) &&
        arguments.size() == 3U) {
      invalidateStaticFactsOverlapping(
          staticMemoryOperandRange(*arguments[0]),
          staticUnsignedInteger(*arguments[2]));
    }
    if (call->builtin == stdlib::BuiltinId::Fread &&
        arguments.size() == 4U) {
      const auto size = staticUnsignedInteger(*arguments[1]);
      const auto count = staticUnsignedInteger(*arguments[2]);
      const auto byteLength = size && count
                                  ? multiplyUnsignedIntegers(*size, *count)
                                  : std::optional<std::uint64_t>{};
      invalidateStaticFactsOverlapping(
          staticViewRange(*arguments[0]), byteLength);
    }
    if ((call->builtin == stdlib::BuiltinId::Strcpy ||
         call->builtin == stdlib::BuiltinId::Strncpy ||
         call->builtin == stdlib::BuiltinId::Strcat) && !arguments.empty()) {
      invalidateStaticFactsOverlapping(staticViewRange(*arguments[0]), std::nullopt);
    }
    releaseStaticCallArguments(arguments);
    return;
  }
  if (const auto *view =
          dynamic_cast<const hir::DynamicByteViewExpr *>(&expression)) {
    validateSafety(*view->source);
    if (view->runtimeLength) {
      validateSafety(*view->runtimeLength);
    }
    return;
  }
  if (const auto *swap = dynamic_cast<const hir::ByteSwapExpr *>(&expression)) {
    validateSafety(*swap->source);
    return;
  }
  if (const auto *assignment =
          dynamic_cast<const hir::AssignmentExpr *>(&expression)) {
    for (const auto &store : assignment->stores) {
      validateSafety(*store);
    }
    if (assignment->result) {
      if (assignment->result->result.templateName == "cstr") {
        validateStaticViewFormation(*assignment->result);
        validateStaticMemoryAccess(staticViewAddressFact(*assignment->result),
            assignment->result->result.staticByteLength, "assignment snapshot");
      } else {
        validateSafety(*assignment->result);
      }
    }
    return;
  }
}

void StaticSafetyAnalyzer::validateStaticCStringRead(const hir::Expr &expression) {
  // A scan observes bytes only up to its first zero, not the whole declared
  // extent. The exact terminating-byte position is not currently tracked.
  validateStaticViewFormation(expression);
  validateStaticMemoryAccess(staticViewAddressFact(expression),
      expression.result.lengthKind == hir::ViewLengthKind::Static
          ? std::min<std::size_t>(1, expression.result.staticByteLength) : 1,
      "cstr read");
  if (const auto terminated = staticCStringTerminated(expression);
      terminated && !*terminated) {
    addDiagnostic("static safety check failed: missing terminating byte in cstr");
  }
}

void StaticSafetyAnalyzer::recordStaticViewCapture(
    const std::string& name, const hir::Expr& source, bool retainLocation) {
  capturedRanges_[name] = retainLocation ? staticViewRange(source) : std::nullopt;
  capturedAliases_[name] = retainLocation
      ? staticContentKey(capturedRanges_[name], source.result.staticByteLength)
      : std::nullopt;
  // The location is frozen, while aliased content facts are consulted only
  // when bytes are consumed. Unknown writes preserve location identities.
  staticAddressFacts_[name + ".location"] = retainLocation
      ? staticViewAddressFact(source) : std::nullopt;
  staticIntegerValues_[name] = retainLocation
      ? std::nullopt : staticSignedInteger(source);
  staticUnsignedIntegerValues_[name] = retainLocation
      ? std::nullopt : staticUnsignedInteger(source);
  if (retainLocation) {
    staticAddressFacts_[name] = std::nullopt;
  } else {
    recordStaticAddressAssignment(name, source);
  }
  staticCStringTerminations_[name] = retainLocation
      ? std::nullopt : staticCStringTerminated(source);
}

std::unique_ptr<hir::Expr> StaticSafetyAnalyzer::captureStaticEvaluatedArgument(
  const hir::Expr& source, bool retainLocation, bool rawMemory) {
  const auto name = "$safety.call." + std::to_string(nextStaticCallCaptureId_++);
  recordStaticViewCapture(name, source, retainLocation);
  auto semantics = source.result;
  semantics.isAddressable = retainLocation;
  semantics.isMutableLValue = retainLocation && semantics.isMutableLValue;
  if (rawMemory) {
    // Explicit address arithmetic can retain an integer result category.
    // Remember its raw-address contract after replacing it with a capture.
    semantics = hir::viewSemanticsForTemplate("addr", sizeof(void*));
  }
  auto reference = std::make_unique<hir::CapturedViewRef>(name, semantics);
  reference->range = source.range;
  return reference;
}

void StaticSafetyAnalyzer::releaseStaticCallArguments(
    const std::vector<std::unique_ptr<hir::Expr>>& arguments) {
  for (const auto& argument : arguments) {
    const auto& name = static_cast<const hir::CapturedViewRef&>(*argument).captureName;
    capturedRanges_.erase(name);
    capturedAliases_.erase(name);
    staticAddressFacts_.erase(name);
    staticAddressFacts_.erase(name + ".location");
    staticIntegerValues_.erase(name);
    staticUnsignedIntegerValues_.erase(name);
    staticCStringTerminations_.erase(name);
  }
}

bool StaticSafetyAnalyzer::isStaticCStringArgument(
    stdlib::BuiltinId builtin, std::uint16_t overloadIndex,
    const std::vector<hir::FormatArgKind>& kinds, std::size_t index) const {
  bool stringScan = false;
  if (const auto *spec = stdlib::findBuiltin(builtin)) {
    if (overloadIndex < spec->overloads.size() &&
        index < spec->overloads[overloadIndex].parameterModes.size()) {
      stringScan = spec->overloads[overloadIndex].parameterModes[index] ==
                   stdlib::BuiltinParameterMode::CStrView;
    } else if (index < spec->parameters.size()) {
      stringScan = spec->parameters[index].mode ==
                       stdlib::BuiltinParameterMode::CStrView ||
                   spec->parameters[index].requiresCString;
    }
  }
  if (builtin == stdlib::BuiltinId::Print || builtin == stdlib::BuiltinId::Printf ||
      builtin == stdlib::BuiltinId::Fprintf) {
    const auto formatIndex = builtin == stdlib::BuiltinId::Fprintf ? 1U : 0U;
    stringScan = index == formatIndex ||
        (index > formatIndex && index < kinds.size() &&
         kinds[index] == hir::FormatArgKind::String);
  }
  return stringScan || (builtin == stdlib::BuiltinId::Strcat && index == 0);
}

std::vector<std::unique_ptr<hir::Expr>>
StaticSafetyAnalyzer::prepareStaticCallArguments(
    stdlib::BuiltinId builtin, std::uint16_t overloadIndex,
    const std::vector<hir::FormatArgKind>& kinds,
    const std::vector<std::unique_ptr<hir::Expr>>& arguments) {
  std::size_t memoryCount = 0;
  if (builtin == stdlib::BuiltinId::Memset || builtin == stdlib::BuiltinId::Fread ||
      builtin == stdlib::BuiltinId::Fwrite) memoryCount = 1;
  if (builtin == stdlib::BuiltinId::Memcpy || builtin == stdlib::BuiltinId::Memmove ||
      builtin == stdlib::BuiltinId::Memcmp) memoryCount = 2;
  const bool formatted = builtin == stdlib::BuiltinId::Print ||
      builtin == stdlib::BuiltinId::Printf || builtin == stdlib::BuiltinId::Fprintf;
  const auto formatIndex = builtin == stdlib::BuiltinId::Fprintf ? 1U : 0U;
  std::vector<std::unique_ptr<hir::Expr>> captured;
  captured.resize(arguments.size());
  for (std::size_t position = 0; position < arguments.size(); ++position) {
    // Match the emitter's implementation-chosen order: realloc evaluates its
    // size before its pointer; fprintf forms its format before its file.
    const bool reversedPair =
        (builtin == stdlib::BuiltinId::Realloc && arguments.size() == 2) ||
        (builtin == stdlib::BuiltinId::Fprintf && arguments.size() >= 2);
    const auto index = reversedPair && position < 2 ? 1U - position : position;
    const auto& argument = *arguments[index];
    SourceRangeScope range(*this, argument.range);
    const bool rawMemory = index < memoryCount &&
        builtin != stdlib::BuiltinId::Fread && builtin != stdlib::BuiltinId::Fwrite &&
        isRawMemoryAddress(argument);
    const bool stringDestination = index == 0 &&
        (builtin == stdlib::BuiltinId::Strcpy || builtin == stdlib::BuiltinId::Strcat ||
         builtin == stdlib::BuiltinId::Strncpy);
    const bool formattedView = formatted && index >= formatIndex &&
        (index == formatIndex || index >= kinds.size() ||
         kinds[index] != hir::FormatArgKind::Float);
    const bool view = isStaticCStringArgument(builtin, overloadIndex, kinds, index) ||
        (index < memoryCount && !rawMemory) || stringDestination || formattedView ||
        (builtin == stdlib::BuiltinId::Put && index == 0) ||
        (builtin == stdlib::BuiltinId::Fput && index == 1);
    if (view) {
      validateStaticViewFormation(argument);
    } else {
      // Scalar and low-byte arguments are copied at their evaluation point.
      // View arguments instead retain their location until the callee reads.
      validateStaticCallArgument(builtin, overloadIndex, kinds, index, argument);
    }
    captured[index] = captureStaticEvaluatedArgument(argument,
        view && argument.result.isAddressable, rawMemory);
  }
  return captured;
}

void StaticSafetyAnalyzer::validateStaticCallArgument(
    stdlib::BuiltinId builtin, std::uint16_t overloadIndex,
    const std::vector<hir::FormatArgKind>& kinds, std::size_t index,
    const hir::Expr &argument) {
  const bool character = builtin == stdlib::BuiltinId::IsDigit ||
      builtin == stdlib::BuiltinId::IsAlpha || builtin == stdlib::BuiltinId::IsAlnum ||
      builtin == stdlib::BuiltinId::IsSpace || builtin == stdlib::BuiltinId::IsLower ||
      builtin == stdlib::BuiltinId::IsUpper || builtin == stdlib::BuiltinId::IsXdigit ||
      builtin == stdlib::BuiltinId::IsBlank || builtin == stdlib::BuiltinId::IsControl ||
      builtin == stdlib::BuiltinId::IsPrint || builtin == stdlib::BuiltinId::IsGraph ||
      builtin == stdlib::BuiltinId::IsPunct || builtin == stdlib::BuiltinId::ToUpper ||
      builtin == stdlib::BuiltinId::ToLower;
  if ((builtin == stdlib::BuiltinId::Memset && index == 1) ||
      (character && index == 0) ||
      (builtin == stdlib::BuiltinId::Strchr && index == 1)) {
    validateStaticViewFormation(argument);
    validateStaticMemoryAccess(staticViewAddressFact(argument), 1, "low-byte read");
    return;
  }
  if (isStaticCStringArgument(builtin, overloadIndex, kinds, index)) {
    validateStaticCStringRead(argument);
  } else {
    validateSafety(argument);
  }
}

void StaticSafetyAnalyzer::validateStaticUserCallArguments(
    std::string_view callee,
    const std::vector<std::unique_ptr<hir::Expr>>& arguments) {
  const auto contract = staticViewParameterCopies_.find(std::string(callee));
  const auto isBorrowed = [&contract, this](std::size_t index) {
    return contract != staticViewParameterCopies_.end() &&
           index < contract->second.size() && !contract->second[index];
  };
  std::vector<std::optional<StaticAddressRange>> borrowedRanges(arguments.size());
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    if (isBorrowed(index)) {
      // Passing a writable destination locates it. Only the callee's actual
      // member reads/writes consume bytes, so don't assume a full-View read.
      validateStaticViewFormation(*arguments[index]);
      borrowedRanges[index] = staticViewRange(*arguments[index]);
    } else {
      validateSafety(*arguments[index]);
    }
  }
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    if (isBorrowed(index)) {
      invalidateStaticFactsOverlapping(borrowedRanges[index],
                                       std::nullopt);
    }
  }
}

std::size_t StaticSafetyAnalyzer::validateStaticMemoryOperands(
    stdlib::BuiltinId builtin,
    const std::vector<std::unique_ptr<hir::Expr>>& arguments) {
  std::size_t count = 0;
  std::optional<std::uint64_t> byteLength;
  if (arguments.size() == 3U &&
      (builtin == stdlib::BuiltinId::Memset ||
       builtin == stdlib::BuiltinId::Memcpy ||
       builtin == stdlib::BuiltinId::Memmove ||
       builtin == stdlib::BuiltinId::Memcmp)) {
    count = builtin == stdlib::BuiltinId::Memset ? 1 : 2;
    byteLength = staticUnsignedInteger(*arguments[2]);
  } else if (arguments.size() == 4U &&
             (builtin == stdlib::BuiltinId::Fread ||
              builtin == stdlib::BuiltinId::Fwrite)) {
    count = 1;
    const auto size = staticUnsignedInteger(*arguments[1]);
    const auto number = staticUnsignedInteger(*arguments[2]);
    if (size && number) byteLength = multiplyUnsignedIntegers(*size, *number);
  }
  for (std::size_t index = 0; index < count; ++index) {
    const auto &argument = *arguments[index];
    std::optional<StaticAddressFact> fact;
    const bool rawAddress = builtin != stdlib::BuiltinId::Fread &&
        builtin != stdlib::BuiltinId::Fwrite && isRawMemoryAddress(argument);
    if (rawAddress) {
      validateSafety(argument);
      fact = staticAddressFact(argument);
    } else {
      validateStaticViewFormation(argument);
      fact = staticViewAddressFact(argument);
      if (fact) fact->range = staticViewRange(argument);
    }
    if (byteLength) {
      const auto operation = index == 0 && builtin != stdlib::BuiltinId::Memcmp &&
          builtin != stdlib::BuiltinId::Fwrite ? "store" : "load";
      validateStaticMemoryAccess(fact, *byteLength, operation);
      // A View's declared capacity remains known even when its binding's
      // address provenance is unknown. Raw addr operands have no such bound.
      if (!rawAddress &&
          argument.result.lengthKind == hir::ViewLengthKind::Static &&
          *byteLength > argument.result.staticByteLength &&
          (!fact || !fact->range)) {
        addDiagnostic("static safety check failed: memory " +
                      std::string(operation) + " out of bounds");
      }
    }
  }
  return count;
}

void StaticSafetyAnalyzer::validateStaticViewFormation(const hir::Expr &expression) {
  if (dynamic_cast<const hir::VariableRef *>(&expression) != nullptr ||
      dynamic_cast<const hir::CapturedViewRef *>(&expression) != nullptr) {
    return;
  }
  if (const auto *view = dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    validateStaticViewFormation(*view->operand);
    return;
  }
  if (const auto *deref = dynamic_cast<const hir::DerefExpr *>(&expression)) {
    validateSafety(*deref->address);
    return;
  }
  if (const auto *view = dynamic_cast<const hir::DynamicByteViewExpr *>(&expression)) {
    validateStaticViewFormation(*view->source);
    if (view->runtimeLength) {
      validateSafety(*view->runtimeLength);
    }
    return;
  }
  validateSafety(expression);
}

} // namespace hitsimple::safety::detail
