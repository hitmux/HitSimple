#include "safety/StaticSafetyAnalyzerImpl.h"

#include "hitsimple/literal/Literal.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace hitsimple::safety::detail {

std::optional<std::int64_t> constantSignedInteger(const hir::Expr &expression) {
  if (const auto *call = dynamic_cast<const hir::CallExpr *>(&expression);
      call != nullptr && call->builtin == stdlib::BuiltinId::Length &&
      call->arguments.size() == 1U &&
      call->arguments.front()->result.lengthKind == hir::ViewLengthKind::Static) {
    const auto length = call->arguments.front()->result.staticByteLength;
    if (length <= static_cast<std::uint64_t>(
                      std::numeric_limits<std::int64_t>::max())) {
      return static_cast<std::int64_t>(length);
    }
  }
  if (const auto *integer =
          dynamic_cast<const hir::IntegerLiteral *>(&expression)) {
    const auto value = literal::parseUnsignedIntegerLiteral(integer->value);
    if (!value || *value > static_cast<std::uint64_t>(
                               std::numeric_limits<std::int64_t>::max())) {
      return std::nullopt;
    }
    return static_cast<std::int64_t>(*value);
  }

  if (const auto *unary = dynamic_cast<const hir::UnaryExpr *>(&expression)) {
    if (unary->op == "-") {
      if (const auto *integer =
              dynamic_cast<const hir::IntegerLiteral *>(unary->operand.get())) {
        const auto value = literal::parseUnsignedIntegerLiteral(integer->value);
        if (value && *value == static_cast<std::uint64_t>(
                                   std::numeric_limits<std::int64_t>::max()) +
                                   1U) {
          return std::numeric_limits<std::int64_t>::min();
        }
      }
    }
    const auto value = constantSignedInteger(*unary->operand);
    if (!value) {
      return std::nullopt;
    }
    if (unary->op == "-") {
      if (*value == std::numeric_limits<std::int64_t>::min()) {
        return std::nullopt;
      }
      return -*value;
    }
    if (unary->op == "+") {
      return *value;
    }
  }

  if (const auto *cast =
          dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
    const auto value = constantSignedInteger(*cast->operand);
    if (!value || cast->byteLength == 0 || cast->byteLength > 8) {
      return std::nullopt;
    }
    if (cast->byteLength == 8) {
      return cast->isSigned ? value : (*value >= 0 ? value : std::nullopt);
    }
    const auto bits = cast->byteLength * 8U;
    const auto mask = (std::uint64_t{1} << bits) - 1U;
    const auto truncated = static_cast<std::uint64_t>(*value) & mask;
    if (!cast->isSigned) {
      return static_cast<std::int64_t>(truncated);
    }
    const auto signBit = std::uint64_t{1} << (bits - 1U);
    return static_cast<std::int64_t>((truncated ^ signBit) - signBit);
  }
  if (const auto *view =
          dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return constantSignedInteger(*view->operand);
  }

  return std::nullopt;
}

std::optional<std::uint64_t>
constantUnsignedInteger(const hir::Expr &expression) {
  if (const auto *call = dynamic_cast<const hir::CallExpr *>(&expression);
      call != nullptr && call->builtin == stdlib::BuiltinId::Length &&
      call->arguments.size() == 1U &&
      call->arguments.front()->result.lengthKind == hir::ViewLengthKind::Static) {
    return call->arguments.front()->result.staticByteLength;
  }
  if (const auto *integer =
          dynamic_cast<const hir::IntegerLiteral *>(&expression)) {
    return literal::parseUnsignedIntegerLiteral(integer->value);
  }
  if (const auto *unary = dynamic_cast<const hir::UnaryExpr *>(&expression)) {
    if (unary->op == "+") {
      return constantUnsignedInteger(*unary->operand);
    }
  }
  if (const auto *unsignedExpr =
          dynamic_cast<const hir::UnsignedExpr *>(&expression)) {
    return constantUnsignedInteger(*unsignedExpr->operand);
  }
  if (const auto *cast =
          dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
    if (cast->byteLength == 0 || cast->byteLength > 8) {
      return std::nullopt;
    }
    auto value = constantUnsignedInteger(*cast->operand);
    if (!value) {
      const auto signedValue = constantSignedInteger(*cast->operand);
      if (!signedValue) {
        return std::nullopt;
      }
      value = static_cast<std::uint64_t>(*signedValue);
    }
    if (cast->byteLength == 8) {
      return value;
    }
    return *value & ((std::uint64_t{1} << (cast->byteLength * 8U)) - 1U);
  }
  if (const auto *view =
          dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return constantUnsignedInteger(*view->operand);
  }
  return std::nullopt;
}

std::optional<std::int64_t> addSignedIntegers(std::int64_t left,
                                              std::int64_t right) {
  if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
      (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)) {
    return std::nullopt;
  }
  return left + right;
}

std::optional<std::uint64_t> multiplyUnsignedIntegers(std::uint64_t left,
                                                       std::uint64_t right) {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return std::nullopt;
  }
  return left * right;
}

std::optional<std::int64_t> signedMinimumForByteLength(std::size_t byteLength) {
  if (byteLength == 8) {
    return std::numeric_limits<std::int64_t>::min();
  }
  if (byteLength != 1 && byteLength != 2 && byteLength != 4) {
    return std::nullopt;
  }
  return -(std::int64_t{1} << (byteLength * 8U - 1U));
}

std::optional<StaticAddressFact>
StaticSafetyAnalyzer::staticBindingAddressFact(
    std::string_view bindingName, std::size_t offset,
    std::size_t byteLength) const {
  const auto found = staticBindingAddressFacts_.find(std::string(bindingName));
  if (found == staticBindingAddressFacts_.end()) {
    const auto start = static_cast<std::int64_t>(offset);
    return StaticAddressFact{StaticAddressOrigin::NonDynamicObject, 0,
                             offset == 0,
                             StaticAddressRange{std::string(bindingName), start,
                                                start, offset + byteLength}};
  }
  auto fact = found->second;
  if (fact && offset != 0) {
    fact->isBaseAddress = false;
    if (fact->range) {
      const auto adjusted = addSignedIntegers(
          fact->range->offset, static_cast<std::int64_t>(offset));
      if (adjusted) {
        fact->range->offset = *adjusted;
      } else {
        fact->range.reset();
      }
    }
    // Adding a nonzero offset to null yields an unknown invalid address, not
    // a proven null dereference. Checked mode retains the runtime guard.
    if (fact->origin == StaticAddressOrigin::Null) {
      return std::nullopt;
    }
  }
  return fact;
}

std::optional<std::string> StaticSafetyAnalyzer::staticContentKey(
    const std::optional<StaticAddressRange>& range,
    std::size_t byteLength) const {
  if (!range || range->offset != 0) {
    return std::nullopt;
  }
  const auto definition = staticDefinitionByteLengths_.find(range->bindingName);
  if (definition == staticDefinitionByteLengths_.end() ||
      definition->second != byteLength || byteLength > range->upperBound) {
    return std::nullopt;
  }
  return range->bindingName;
}

std::optional<std::string> StaticSafetyAnalyzer::staticContentKey(
    std::string_view bindingName, std::size_t offset,
    std::size_t byteLength) const {
  const auto fact = staticBindingAddressFact(bindingName, offset, byteLength);
  return staticContentKey(fact ? fact->range : std::nullopt, byteLength);
}

void StaticSafetyAnalyzer::validateStaticMemoryAccess(
    const std::optional<StaticAddressFact>& fact, std::uint64_t byteLength,
    std::string_view operation) {
  if (!fact || byteLength == 0) {
    return;
  }
  if (fact->origin == StaticAddressOrigin::Null) {
    addDiagnostic("static safety check failed: null address " +
                  std::string(operation));
    return;
  }
  validateStaticAddressAccess(fact, operation);
  if (fact->range) {
    const auto &range = *fact->range;
    if (range.offset < range.lowerBound || range.offset < 0 ||
        static_cast<std::uint64_t>(range.offset) > range.upperBound ||
        byteLength > range.upperBound - static_cast<std::uint64_t>(range.offset)) {
      addDiagnostic("static safety check failed: memory " +
                    std::string(operation) + " out of bounds");
    }
  }
}

std::optional<std::string> StaticSafetyAnalyzer::prepareStaticNamedStore(
    std::string_view bindingName, std::size_t offset, std::size_t byteLength) {
  const auto fact = staticBindingAddressFact(bindingName, offset, byteLength);
  validateStaticMemoryAccess(fact, byteLength, "store");
  const auto range = fact ? fact->range : std::nullopt;
  const auto key = staticContentKey(range, byteLength);
  invalidateStaticFactsOverlapping(range, byteLength);
  return key;
}

std::optional<StaticAddressRange>
StaticSafetyAnalyzer::staticAddressRange(const hir::Expr &expression) const {
  const auto fact = staticAddressFact(expression);
  return fact ? fact->range : std::nullopt;
}

std::optional<StaticAddressFact> StaticSafetyAnalyzer::staticViewAddressFact(
    const hir::Expr &expression) const {
  if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
    return staticBindingAddressFact(variable->bindingName, variable->offset,
                                    variable->byteLength);
  }
  if (const auto *deref = dynamic_cast<const hir::DerefExpr *>(&expression)) {
    return staticAddressFact(*deref->address);
  }
  if (const auto *view = dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return staticViewAddressFact(*view->operand);
  }
  if (const auto *capture = dynamic_cast<const hir::CapturedViewRef *>(&expression)) {
    const auto found = staticAddressFacts_.find(capture->captureName + ".location");
    return found == staticAddressFacts_.end() ? std::nullopt : found->second;
  }
  return std::nullopt;
}

std::optional<StaticAddressRange>
StaticSafetyAnalyzer::staticViewRange(const hir::Expr &expression) const {
  std::optional<StaticAddressRange> range;
  if (const auto *capture = dynamic_cast<const hir::CapturedViewRef *>(&expression)) {
    const auto found = capturedRanges_.find(capture->captureName);
    range = found == capturedRanges_.end() ? std::nullopt : found->second;
  } else if (const auto *variable =
                 dynamic_cast<const hir::VariableRef *>(&expression)) {
    const auto fact = staticBindingAddressFact(
        variable->bindingName, variable->offset, variable->byteLength);
    range = fact ? fact->range : std::nullopt;
  } else if (const auto *view =
                 dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    range = staticViewRange(*view->operand);
  } else if (const auto *deref =
                 dynamic_cast<const hir::DerefExpr *>(&expression)) {
    range = staticAddressRange(*deref->address);
  } else if (dynamic_cast<const hir::UnsignedExpr *>(&expression) != nullptr) {
    return std::nullopt;
  } else {
    // Pointer-valued expressions describe the addressed region, rather than
    // the pointer's own fixed-width View. Do not impose a pointer-width bound.
    return staticAddressRange(expression);
  }

  // Every located View has its own visible extent, even when rebinding or a
  // dereference reaches a larger allocation. Captures and reinterpretations
  // retain that restriction rather than recovering the backing capacity.
  if (range && range->offset >= 0 &&
      expression.result.lengthKind == hir::ViewLengthKind::Static) {
    const auto start = static_cast<std::uint64_t>(range->offset);
    range->lowerBound = std::max(range->lowerBound, range->offset);
    const auto length = expression.result.staticByteLength;
    if (length <= std::numeric_limits<std::uint64_t>::max() - start) {
      range->upperBound = std::min(range->upperBound, start + length);
    }
  }
  return range;
}

bool StaticSafetyAnalyzer::isRawMemoryAddress(const hir::Expr &expression) const {
  if (hir::isAddressValue(expression.result)) return true;
  if (const auto *view = dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return view->templateName == "addr";
  }
  if (const auto *call = dynamic_cast<const hir::CallExpr *>(&expression)) {
    return call->templateName == "addr";
  }
  if (dynamic_cast<const hir::AddressOfExpr *>(&expression)) return true;
  if (const auto *binary = dynamic_cast<const hir::BinaryExpr *>(&expression)) {
    if (binary->operationKind == hir::StandardOperationKind::AddressOffset) {
      return true;
    }
    return (binary->operation == hir::BinaryOperator::Add ||
            binary->operation == hir::BinaryOperator::Subtract) &&
           isRawMemoryAddress(*binary->left);
  }
  return false;
}

std::optional<StaticAddressRange>
StaticSafetyAnalyzer::staticMemoryOperandRange(const hir::Expr &expression) const {
  return isRawMemoryAddress(expression) ? staticAddressRange(expression)
                                        : staticViewRange(expression);
}


std::optional<std::int64_t> StaticSafetyAnalyzer::staticIndexOffset(
    const hir::Expr &expression) const {
  if (expression.result.lengthKind != hir::ViewLengthKind::Static ||
      expression.result.staticByteLength == 0) {
    return std::nullopt;
  }
  const auto length = expression.result.staticByteLength;
  auto bits = staticUnsignedInteger(expression);
  if (!bits) {
    const auto signedValue = staticSignedInteger(expression);
    if (!signedValue) return std::nullopt;
    if (length >= 8) return signedValue;
    bits = static_cast<std::uint64_t>(*signedValue);
  }
  if (length > 8) {
    return *bits <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
        ? std::optional<std::int64_t>{static_cast<std::int64_t>(*bits)} : std::nullopt;
  }
  if (length == 8) {
    constexpr auto sign = std::uint64_t{1} << 63;
    return *bits < sign ? static_cast<std::int64_t>(*bits)
        : std::numeric_limits<std::int64_t>::min() +
              static_cast<std::int64_t>(*bits - sign);
  }
  const auto mask = (std::uint64_t{1} << (length * 8U)) - 1U;
  const auto value = *bits & mask;
  const auto sign = std::uint64_t{1} << (length * 8U - 1U);
  return value < sign ? static_cast<std::int64_t>(value)
                     : -static_cast<std::int64_t>(mask - value + 1U);
}

std::optional<std::int64_t>
StaticSafetyAnalyzer::staticSignedInteger(const hir::Expr &expression) const {
  if (const auto *capture = dynamic_cast<const hir::CapturedViewRef *>(&expression)) {
    const auto alias = capturedAliases_.find(capture->captureName);
    if (alias != capturedAliases_.end() && alias->second) {
      const auto found = staticIntegerValues_.find(*alias->second);
      return found == staticIntegerValues_.end() ? std::nullopt : found->second;
    }
    const auto found = staticIntegerValues_.find(capture->captureName);
    return found == staticIntegerValues_.end() ? std::nullopt : found->second;
  }
  if (const auto value = constantSignedInteger(expression)) {
    return value;
  }
  if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
    const auto key = staticContentKey(variable->bindingName, variable->offset,
                                      variable->byteLength);
    if (!key) {
      return std::nullopt;
    }
    const auto found = staticIntegerValues_.find(*key);
    return found == staticIntegerValues_.end() ? std::nullopt : found->second;
  }
  if (const auto *deref = dynamic_cast<const hir::DerefExpr *>(&expression)) {
    const auto key = staticContentKey(staticAddressRange(*deref->address),
                                      deref->byteLength);
    if (!key) {
      return std::nullopt;
    }
    const auto found = staticIntegerValues_.find(*key);
    return found == staticIntegerValues_.end() ? std::nullopt : found->second;
  }
  if (const auto *unary = dynamic_cast<const hir::UnaryExpr *>(&expression)) {
    const auto value = staticSignedInteger(*unary->operand);
    if (!value) {
      return std::nullopt;
    }
    if (unary->op == "+") {
      return value;
    }
    if (unary->op == "-" &&
        *value != std::numeric_limits<std::int64_t>::min()) {
      return -*value;
    }
    return std::nullopt;
  }
  if (const auto *cast =
          dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
    if (cast->byteLength == 0 || cast->byteLength > 8) {
      return std::nullopt;
    }
    const auto value = staticSignedInteger(*cast->operand);
    if (!value) {
      return std::nullopt;
    }
    if (cast->byteLength == 8) {
      return cast->isSigned ? value : (*value >= 0 ? value : std::nullopt);
    }
    const auto bits = cast->byteLength * 8U;
    const auto mask = (std::uint64_t{1} << bits) - 1U;
    const auto truncated = static_cast<std::uint64_t>(*value) & mask;
    if (!cast->isSigned) {
      return static_cast<std::int64_t>(truncated);
    }
    const auto signBit = std::uint64_t{1} << (bits - 1U);
    return static_cast<std::int64_t>((truncated ^ signBit) - signBit);
  }
  if (const auto *view =
          dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return staticSignedInteger(*view->operand);
  }
  return std::nullopt;
}

std::optional<std::uint64_t>
StaticSafetyAnalyzer::staticUnsignedInteger(const hir::Expr &expression) const {
  if (const auto *capture = dynamic_cast<const hir::CapturedViewRef *>(&expression)) {
    const auto alias = capturedAliases_.find(capture->captureName);
    if (alias != capturedAliases_.end() && alias->second) {
      const auto found = staticUnsignedIntegerValues_.find(*alias->second);
      return found == staticUnsignedIntegerValues_.end() ? std::nullopt : found->second;
    }
    const auto found = staticUnsignedIntegerValues_.find(capture->captureName);
    return found == staticUnsignedIntegerValues_.end() ? std::nullopt : found->second;
  }
  if (const auto value = constantUnsignedInteger(expression)) {
    return value;
  }
  if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
    const auto key = staticContentKey(variable->bindingName, variable->offset,
                                      variable->byteLength);
    if (!key) {
      return std::nullopt;
    }
    const auto found = staticUnsignedIntegerValues_.find(*key);
    return found == staticUnsignedIntegerValues_.end() ? std::nullopt
                                                        : found->second;
  }
  if (const auto *deref = dynamic_cast<const hir::DerefExpr *>(&expression)) {
    const auto key = staticContentKey(staticAddressRange(*deref->address),
                                      deref->byteLength);
    if (!key) {
      return std::nullopt;
    }
    const auto found = staticUnsignedIntegerValues_.find(*key);
    return found == staticUnsignedIntegerValues_.end() ? std::nullopt : found->second;
  }
  if (const auto *unsignedExpr =
          dynamic_cast<const hir::UnsignedExpr *>(&expression)) {
    return staticUnsignedInteger(*unsignedExpr->operand);
  }
  if (const auto *cast =
          dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
    if (cast->byteLength == 0 || cast->byteLength > 8) {
      return std::nullopt;
    }
    auto value = staticUnsignedInteger(*cast->operand);
    if (!value) {
      const auto signedValue = staticSignedInteger(*cast->operand);
      if (!signedValue) {
        return std::nullopt;
      }
      value = static_cast<std::uint64_t>(*signedValue);
    }
    if (cast->byteLength == 8) {
      return value;
    }
    return *value & ((std::uint64_t{1} << (cast->byteLength * 8U)) - 1U);
  }
  if (const auto *view =
          dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return staticUnsignedInteger(*view->operand);
  }
  return std::nullopt;
}

std::optional<bool>
StaticSafetyAnalyzer::staticBooleanValue(const hir::Expr &expression) const {
  if (const auto *test =
          dynamic_cast<const hir::BooleanTestExpr *>(&expression)) {
    return staticBooleanValue(*test->operand);
  }
  if (const auto *unary = dynamic_cast<const hir::UnaryExpr *>(&expression)) {
    if (unary->op == "!") {
      const auto value = staticBooleanValue(*unary->operand);
      return value ? std::optional<bool>{!*value} : std::nullopt;
    }
  }
  if (const auto value = staticSignedInteger(expression)) {
    return *value != 0;
  }
  if (const auto value = staticUnsignedInteger(expression)) {
    return *value != 0;
  }
  return std::nullopt;
}

std::optional<StaticAddressFact>
StaticSafetyAnalyzer::staticAddressFact(const hir::Expr &expression) const {
  if (const auto *capture = dynamic_cast<const hir::CapturedViewRef *>(&expression)) {
    const auto alias = capturedAliases_.find(capture->captureName);
    if (alias != capturedAliases_.end() && alias->second) {
      const auto found = staticAddressFacts_.find(*alias->second);
      return found == staticAddressFacts_.end() ? std::nullopt : found->second;
    }
    const auto found = staticAddressFacts_.find(capture->captureName);
    return found == staticAddressFacts_.end() ? std::nullopt : found->second;
  }
  if (const auto *unsignedExpr =
          dynamic_cast<const hir::UnsignedExpr *>(&expression)) {
    return staticAddressFact(*unsignedExpr->operand);
  }
  if (const auto *cast =
          dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
    const auto pointerBytes = sizeof(void *);
    if (cast->byteLength != pointerBytes ||
        cast->operand->result.lengthKind != hir::ViewLengthKind::Static ||
        cast->operand->result.staticByteLength != pointerBytes) {
      const auto value = staticUnsignedInteger(expression);
      return value && *value == 0
          ? std::optional<StaticAddressFact>{StaticAddressFact{
                StaticAddressOrigin::Null, 0, false, std::nullopt}}
          : std::nullopt;
    }
    return staticAddressFact(*cast->operand);
  }
  if (const auto *view =
          dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return staticAddressFact(*view->operand);
  }
  if (const auto *address =
          dynamic_cast<const hir::AddressOfExpr *>(&expression)) {
    auto fact = staticBindingAddressFact(address->bindingName, address->offset,
                                         address->targetByteLength);
    if (fact && fact->range && fact->range->offset >= 0) {
      const auto start = static_cast<std::uint64_t>(fact->range->offset);
      fact->range->lowerBound = std::max(fact->range->lowerBound,
                                        fact->range->offset);
      if (address->targetByteLength <=
          std::numeric_limits<std::uint64_t>::max() - start) {
        fact->range->upperBound = std::min(fact->range->upperBound,
                                          start + address->targetByteLength);
      }
    }
    return fact;
  }
  if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
    const auto key = staticContentKey(variable->bindingName, variable->offset,
                                      variable->byteLength);
    if (!key) {
      return std::nullopt;
    }
    const auto found = staticAddressFacts_.find(*key);
    return found == staticAddressFacts_.end() ? std::nullopt : found->second;
  }
  if (const auto *deref = dynamic_cast<const hir::DerefExpr *>(&expression)) {
    const auto key = staticContentKey(staticAddressRange(*deref->address),
                                      deref->byteLength);
    if (!key) {
      return std::nullopt;
    }
    const auto found = staticAddressFacts_.find(*key);
    return found == staticAddressFacts_.end() ? std::nullopt : found->second;
  }
  if (const auto *binary = dynamic_cast<const hir::BinaryExpr *>(&expression)) {
    const bool addressOffset =
        binary->operationKind == hir::StandardOperationKind::AddressOffset;
    if (!addressOffset && binary->operation != hir::BinaryOperator::Add &&
        binary->operation != hir::BinaryOperator::Subtract) {
      return std::nullopt;
    }
    auto base = staticAddressFact(*binary->left);
    const auto offset = binary->viewOffsetBounds
        ? staticIndexOffset(*binary->right) : staticSignedInteger(*binary->right);
    if (base && offset) {
      auto signedOffset = *offset;
      if (!addressOffset && binary->operation == hir::BinaryOperator::Subtract) {
        if (signedOffset == std::numeric_limits<std::int64_t>::min()) {
          base->range.reset();
          return base;
        }
        signedOffset = -signedOffset;
      }
      base->isBaseAddress = false;
      if (base->range) {
        const auto adjusted =
            addSignedIntegers(base->range->offset, signedOffset);
        if (adjusted) {
          base->range->offset = *adjusted;
        } else {
          base->range.reset();
        }
      }
    } else if (base) {
      base->range.reset();
    }
    return base;
  }
  const auto value = staticSignedInteger(expression);
  if (value && *value == 0) {
    return StaticAddressFact{StaticAddressOrigin::Null, 0, false, std::nullopt};
  }
  return std::nullopt;
}

void StaticSafetyAnalyzer::validateStaticDynamicBase(const hir::Expr &expression,
                                            std::string_view operation) {
  const auto fact = staticAddressFact(expression);
  if (!fact || fact->origin == StaticAddressOrigin::Null) {
    return;
  }
  if (fact->origin != StaticAddressOrigin::DynamicObject) {
    addDiagnostic("static safety check failed: " + std::string(operation) +
                  " requires a dynamic-object base address");
    return;
  }
  if (!fact->isBaseAddress) {
    addDiagnostic("static safety check failed: " + std::string(operation) +
                  " requires a dynamic-object base address");
    return;
  }
  const auto state = staticDynamicObjectStates_.find(fact->dynamicObjectId);
  if (state == staticDynamicObjectStates_.end() ||
      state->second == StaticDynamicObjectState::Freed) {
    addDiagnostic("static safety check failed: " +
                  (operation == "free" ? std::string("double free")
                                       : std::string("invalid reallocation of "
                                                     "released dynamic object")));
  }
}

bool StaticSafetyAnalyzer::releaseStaticDynamicObject(const hir::Expr &expression) {
  const auto fact = staticAddressFact(expression);
  if (!fact || fact->origin != StaticAddressOrigin::DynamicObject ||
      !fact->isBaseAddress) {
    return false;
  }
  const auto state = staticDynamicObjectStates_.find(fact->dynamicObjectId);
  if (state == staticDynamicObjectStates_.end() ||
      state->second != StaticDynamicObjectState::Live) {
    return false;
  }
  state->second = StaticDynamicObjectState::Freed;
  return true;
}

void StaticSafetyAnalyzer::recordStaticAddressAssignment(std::string_view bindingName,
                                                const hir::Expr &value) {
  const auto recordDynamicObject = [this, bindingName](
                                       std::optional<std::uint64_t> extent) {
    const auto dynamicObjectId = nextStaticDynamicObjectId_++;
    staticDynamicObjectStates_[dynamicObjectId] = StaticDynamicObjectState::Live;
    staticAddressFacts_[std::string(bindingName)] =
        StaticAddressFact{
            StaticAddressOrigin::DynamicObject,
            dynamicObjectId,
            true,
            extent ? std::optional<StaticAddressRange>{StaticAddressRange{
                         "dynamic:" + std::to_string(dynamicObjectId), 0, 0,
                         *extent}}
                   : std::nullopt};
  };

  if (const auto *call = dynamic_cast<const hir::CallExpr *>(&value)) {
    if (call->builtin == stdlib::BuiltinId::Alloc ||
        call->builtin == stdlib::BuiltinId::Calloc) {
      std::optional<std::uint64_t> extent;
      if (call->addressFacts && call->addressFacts->knownExtent) {
        extent = static_cast<std::uint64_t>(*call->addressFacts->knownExtent);
      }
      if (!extent && call->builtin == stdlib::BuiltinId::Alloc &&
          call->arguments.size() == 1U) {
        extent = staticUnsignedInteger(*call->arguments.front());
      }
      if (!extent && call->builtin == stdlib::BuiltinId::Calloc &&
          call->arguments.size() == 2U) {
        const auto count = staticUnsignedInteger(*call->arguments[0]);
        const auto size = staticUnsignedInteger(*call->arguments[1]);
        if (count && size) {
          extent = multiplyUnsignedIntegers(*count, *size);
        }
      }
      recordDynamicObject(extent);
      return;
    }
    if (call->builtin == stdlib::BuiltinId::Realloc &&
        !call->arguments.empty()) {
      std::optional<std::uint64_t> extent;
      if (call->addressFacts && call->addressFacts->knownExtent) {
        extent = static_cast<std::uint64_t>(*call->addressFacts->knownExtent);
      } else if (call->arguments.size() == 2U) {
        extent = staticUnsignedInteger(*call->arguments[1]);
      }
      const auto input = staticAddressFact(*call->arguments.front());
      if (extent && *extent == 0) {
        (void)releaseStaticDynamicObject(*call->arguments.front());
      } else if (input && input->origin == StaticAddressOrigin::DynamicObject) {
        const auto state = staticDynamicObjectStates_.find(input->dynamicObjectId);
        if (state != staticDynamicObjectStates_.end()) {
          state->second = StaticDynamicObjectState::Unknown;
        }
      }

      // realloc may fail, leaving its input object live.  Merging that path
      // with a successful reallocation leaves both the old object's lifetime
      // and the returned address unknown.
      staticAddressFacts_[std::string(bindingName)] = std::nullopt;
      return;
    }
  }

  staticAddressFacts_[std::string(bindingName)] = staticAddressFact(value);
}

void StaticSafetyAnalyzer::validateStaticAddressAccess(const hir::Expr &expression,
                                              std::string_view operation) {
  validateStaticAddressAccess(staticAddressFact(expression), operation);
}

void StaticSafetyAnalyzer::validateStaticAddressAccess(
    const std::optional<StaticAddressFact> &fact, std::string_view operation) {
  if (!fact) {
    return;
  }
  if (fact->origin == StaticAddressOrigin::ExpiredLocalObject) {
    addDiagnostic("static safety check failed: use after scope exit " +
                  std::string(operation));
    return;
  }
  if (fact->origin != StaticAddressOrigin::DynamicObject) {
    return;
  }
  const auto state = staticDynamicObjectStates_.find(fact->dynamicObjectId);
  if (state != staticDynamicObjectStates_.end() &&
      state->second == StaticDynamicObjectState::Freed) {
    addDiagnostic("static safety check failed: use after free " +
                  std::string(operation));
  }
}

std::optional<bool>
StaticSafetyAnalyzer::staticCStringTerminated(const hir::Expr &expression) const {
  if (const auto *assignment = dynamic_cast<const hir::AssignmentExpr *>(&expression);
      assignment != nullptr && assignment->result &&
      assignment->result->result.templateName == "cstr" &&
      assignment->result->result.staticByteLength == expression.result.staticByteLength) {
    // Assignment stores have already been analyzed at this evaluation point.
    // The result snapshots those bytes with its retained extent and template.
    return staticCStringTerminated(*assignment->result);
  }
  if (const auto *capture = dynamic_cast<const hir::CapturedViewRef *>(&expression)) {
    const auto alias = capturedAliases_.find(capture->captureName);
    if (alias != capturedAliases_.end() && alias->second) {
      const auto found = staticCStringTerminations_.find(*alias->second);
      return found == staticCStringTerminations_.end() ? std::nullopt : found->second;
    }
    const auto found = staticCStringTerminations_.find(capture->captureName);
    return found == staticCStringTerminations_.end() ? std::nullopt : found->second;
  }
  if (dynamic_cast<const hir::StringLiteral *>(&expression) != nullptr) {
    return true;
  }
  if (const auto *view =
          dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return staticCStringTerminated(*view->operand);
  }
  if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
    if (variable->templateName != "cstr") {
      return std::nullopt;
    }
    const auto key = staticContentKey(variable->bindingName, variable->offset,
                                      variable->byteLength);
    if (!key) {
      return std::nullopt;
    }
    const auto found = staticCStringTerminations_.find(*key);
    return found == staticCStringTerminations_.end() ? std::nullopt
                                                       : found->second;
  }
  if (const auto *deref = dynamic_cast<const hir::DerefExpr *>(&expression)) {
    const auto key = staticContentKey(staticAddressRange(*deref->address),
                                      deref->byteLength);
    if (!key) {
      return std::nullopt;
    }
    const auto found = staticCStringTerminations_.find(*key);
    return found == staticCStringTerminations_.end() ? std::nullopt : found->second;
  }
  return std::nullopt;
}

} // namespace hitsimple::safety::detail
