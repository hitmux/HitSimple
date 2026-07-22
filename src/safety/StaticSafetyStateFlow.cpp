#include "safety/StaticSafetyAnalyzerImpl.h"

#include <algorithm>
#include <type_traits>

namespace hitsimple::safety::detail {

void StaticSafetyAnalyzer::resetStaticSafetyState() {
  staticIntegerValues_.clear();
  staticUnsignedIntegerValues_.clear();
  staticAddressFacts_.clear();
  staticCStringTerminations_.clear();
  staticDynamicObjectStates_.clear();
  staticGlobalBindings_.clear();
  nextStaticDynamicObjectId_ = 0;
}

StaticSafetyState StaticSafetyAnalyzer::staticSafetyState() const {
  return StaticSafetyState{staticIntegerValues_, staticUnsignedIntegerValues_,
                           staticAddressFacts_, staticCStringTerminations_,
                           staticDynamicObjectStates_, nextStaticDynamicObjectId_};
}

void StaticSafetyAnalyzer::restoreStaticSafetyState(const StaticSafetyState &state) {
  staticIntegerValues_ = state.integerValues;
  staticUnsignedIntegerValues_ = state.unsignedIntegerValues;
  staticAddressFacts_ = state.addressFacts;
  staticCStringTerminations_ = state.cstrTerminations;
  staticDynamicObjectStates_ = state.dynamicObjectStates;
  // Object identifiers are allocation identities. Do not reuse one after
  // restoring a control-flow snapshot that has already observed later IDs.
  nextStaticDynamicObjectId_ =
      std::max(nextStaticDynamicObjectId_, state.nextDynamicObjectId);
}

StaticSafetyState StaticSafetyAnalyzer::mergedStaticSafetyStates(
    const StaticSafetyState &left, const StaticSafetyState &right) const {
  const auto retainCommonFacts = [](const auto &leftFacts,
                                    const auto &rightFacts) {
    using FactMap = std::decay_t<decltype(leftFacts)>;
    FactMap result;
    for (const auto &[bindingName, leftValue] : leftFacts) {
      const auto rightValue = rightFacts.find(bindingName);
      if (rightValue != rightFacts.end() && leftValue && rightValue->second &&
          *leftValue == *rightValue->second) {
        result.emplace(bindingName, leftValue);
      }
    }
    return result;
  };

  StaticSafetyState result;
  result.integerValues =
      retainCommonFacts(left.integerValues, right.integerValues);
  result.unsignedIntegerValues = retainCommonFacts(left.unsignedIntegerValues,
                                                    right.unsignedIntegerValues);
  result.addressFacts =
      retainCommonFacts(left.addressFacts, right.addressFacts);
  result.cstrTerminations =
      retainCommonFacts(left.cstrTerminations, right.cstrTerminations);

  for (const auto &[bindingName, fact] : result.addressFacts) {
    (void)bindingName;
    if (!fact || fact->origin != StaticAddressOrigin::DynamicObject) {
      continue;
    }
    const auto leftState = left.dynamicObjectStates.find(fact->dynamicObjectId);
    const auto rightState =
        right.dynamicObjectStates.find(fact->dynamicObjectId);
    if (leftState == left.dynamicObjectStates.end() ||
        rightState == right.dynamicObjectStates.end()) {
      continue;
    }
    result.dynamicObjectStates[fact->dynamicObjectId] =
        leftState->second == rightState->second ? leftState->second
                                                : StaticDynamicObjectState::Unknown;
  }
  result.nextDynamicObjectId =
      std::max(left.nextDynamicObjectId, right.nextDynamicObjectId);
  return result;
}

void StaticSafetyAnalyzer::mergeStaticSafetyStates(const StaticSafetyState &left,
                                          const StaticSafetyState &right) {
  restoreStaticSafetyState(mergedStaticSafetyStates(left, right));
}

bool StaticSafetyAnalyzer::enqueueStaticGoto(std::string_view label,
                                    const StaticSafetyState &incoming) {
  for (auto *context = staticGotoContext_; context != nullptr;
       context = context->parent) {
    const auto target = context->labelIndexes->find(std::string(label));
    if (target == context->labelIndexes->end()) {
      continue;
    }

    const auto outgoing = exitStaticScopes(context, incoming);

    auto &entry = (*context->entryStates)[target->second];
    if (entry) {
      const auto merged = mergedStaticSafetyStates(*entry, outgoing);
      if (merged == *entry) {
        return true;
      }
      entry = merged;
    } else {
      entry = outgoing;
    }
    context->worklist->push_back(target->second);
    return true;
  }
  return false;
}

StaticSafetyState StaticSafetyAnalyzer::exitStaticScopes(
    StaticGotoContext *target, const StaticSafetyState &incoming) {
  auto outgoing = incoming;
  for (auto *exited = staticGotoContext_; exited != target;
       exited = exited->parent) {
    restoreStaticSafetyState(outgoing);
    if (exited->localBindings != nullptr) {
      expireStaticLocalBindings(*exited->localBindings);
    }
    outgoing = staticSafetyState();
  }
  return outgoing;
}

void StaticSafetyAnalyzer::invalidateStaticBinding(std::string_view bindingName) {
  const auto key = std::string(bindingName);
  staticIntegerValues_[key] = std::nullopt;
  staticUnsignedIntegerValues_[key] = std::nullopt;
  staticAddressFacts_[key] = std::nullopt;
  staticCStringTerminations_[key] = std::nullopt;
}

void StaticSafetyAnalyzer::expireStaticLocalBindings(
    const std::vector<std::string> &bindingNames) {
  for (const auto &bindingName : bindingNames) {
    for (auto &[candidate, fact] : staticAddressFacts_) {
      (void)candidate;
      if (fact && fact->range && fact->range->bindingName == bindingName) {
        fact->origin = StaticAddressOrigin::ExpiredLocalObject;
        fact->isBaseAddress = false;
      }
    }
    staticIntegerValues_.erase(bindingName);
    staticUnsignedIntegerValues_.erase(bindingName);
    staticAddressFacts_.erase(bindingName);
    staticCStringTerminations_.erase(bindingName);
  }
}

void StaticSafetyAnalyzer::invalidateStaticFactsOverlapping(
    const std::optional<StaticAddressRange> &range,
    std::optional<std::uint64_t> byteLength) {
  if (!range || (byteLength && *byteLength == 0)) {
    return;
  }

  // Static facts are keyed by definition binding. Every range originating at
  // this binding overlaps it, even when the write is through an address or a
  // standard-library memory operation rather than a direct assignment.
  invalidateStaticBinding(range->bindingName);
}

void StaticSafetyAnalyzer::invalidateStaticGlobalFacts() {
  for (const auto &bindingName : staticGlobalBindings_) {
    invalidateStaticBinding(bindingName);
  }
}

} // namespace hitsimple::safety::detail
