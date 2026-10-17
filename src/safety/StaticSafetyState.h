#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace hitsimple::safety::detail {

struct StaticAddressRange {
  std::string bindingName;
  std::int64_t lowerBound = 0;
  std::int64_t offset = 0;
  std::uint64_t upperBound = 0;

  bool operator==(const StaticAddressRange&) const = default;
};

enum class StaticAddressOrigin {
  Null,
  DynamicObject,
  NonDynamicObject,
  TemporaryObject,
  ExpiredTemporaryObject,
  ExpiredLocalObject,
};

enum class StaticDynamicObjectState {
  Live,
  Freed,
  Unknown,
};

struct StaticAddressFact {
  StaticAddressOrigin origin = StaticAddressOrigin::Null;
  std::size_t dynamicObjectId = 0;
  bool isBaseAddress = false;
  std::optional<StaticAddressRange> range;
  // A search result can retain its source lifetime without a known byte offset.
  std::optional<std::string> lifetimeBindingName = std::nullopt;

  bool operator==(const StaticAddressFact&) const = default;
};

struct StaticSafetyState {
  std::unordered_map<std::string, std::optional<std::int64_t>> integerValues;
  std::unordered_map<std::string, std::optional<std::uint64_t>>
      unsignedIntegerValues;
  std::unordered_map<std::string, std::optional<StaticAddressFact>>
      addressFacts;
  // A name's current locating capability is distinct from pointer-valued
  // bytes held at its definition storage. An explicit unknown must survive joins.
  std::unordered_map<std::string, std::optional<StaticAddressFact>>
      bindingAddressFacts;
  std::unordered_map<std::string, std::optional<bool>> cstrTerminations;
  std::unordered_map<std::size_t, StaticDynamicObjectState>
      dynamicObjectStates;
  std::size_t nextDynamicObjectId = 0;

  bool operator==(const StaticSafetyState&) const = default;
};

struct StaticGotoContext {
  StaticGotoContext* parent = nullptr;
  const std::unordered_map<std::string, std::size_t>* labelIndexes = nullptr;
  std::vector<std::optional<StaticSafetyState>>* entryStates = nullptr;
  std::deque<std::size_t>* worklist = nullptr;
  const std::vector<std::string>* localBindings = nullptr;
};

struct StaticLoopContext {
  StaticLoopContext* parent = nullptr;
  StaticGotoContext* exitScope = nullptr;
  std::optional<StaticSafetyState>* breakState = nullptr;
  std::optional<StaticSafetyState>* continueState = nullptr;
};

struct StaticCatchContext {
  StaticCatchContext* parent = nullptr;
  StaticGotoContext* exitScope = nullptr;
  std::optional<StaticSafetyState>* throwState = nullptr;
};

} // namespace hitsimple::safety::detail
