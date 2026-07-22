#include "safety/StaticSafetyAnalyzerImpl.h"

#include "hitsimple/literal/Literal.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace hitsimple::safety::detail {

std::optional<std::int64_t> constantSignedInteger(const hir::Expr &expression) {
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

std::optional<StaticAddressRange>
StaticSafetyAnalyzer::staticAddressRange(const hir::Expr &expression) const {
  const auto fact = staticAddressFact(expression);
  return fact ? fact->range : std::nullopt;
}

std::optional<StaticAddressRange>
StaticSafetyAnalyzer::staticViewRange(const hir::Expr &expression) const {
  if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
    const auto offset = static_cast<std::int64_t>(variable->offset);
    return StaticAddressRange{variable->bindingName, offset, offset,
                              static_cast<std::uint64_t>(
                                  variable->offset + variable->byteLength)};
  }
  if (const auto *view = dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return staticViewRange(*view->operand);
  }
  if (const auto *deref = dynamic_cast<const hir::DerefExpr *>(&expression)) {
    return staticAddressRange(*deref->address);
  }
  return staticAddressRange(expression);
}

std::optional<StaticAddressRange>
StaticSafetyAnalyzer::staticMemoryOperandRange(const hir::Expr &expression) const {
  const auto isRawAddress = [&expression] {
    if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
      return variable->templateName == "addr";
    }
    if (const auto *view = dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
      return view->templateName == "addr";
    }
    if (const auto *call = dynamic_cast<const hir::CallExpr *>(&expression)) {
      return call->templateName == "addr";
    }
    return dynamic_cast<const hir::AddressOfExpr *>(&expression) != nullptr;
  };
  return isRawAddress() ? staticAddressRange(expression)
                        : staticViewRange(expression);
}


std::optional<std::int64_t>
StaticSafetyAnalyzer::staticSignedInteger(const hir::Expr &expression) const {
  if (const auto value = constantSignedInteger(expression)) {
    return value;
  }
  if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
    const auto found = staticIntegerValues_.find(variable->bindingName);
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
  if (const auto value = constantUnsignedInteger(expression)) {
    return value;
  }
  if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
    const auto found = staticUnsignedIntegerValues_.find(variable->bindingName);
    return found == staticUnsignedIntegerValues_.end() ? std::nullopt
                                                        : found->second;
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
      return std::nullopt;
    }
    return staticAddressFact(*cast->operand);
  }
  if (const auto *view =
          dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
    return staticAddressFact(*view->operand);
  }
  if (const auto *address =
          dynamic_cast<const hir::AddressOfExpr *>(&expression)) {
    const auto offset = static_cast<std::int64_t>(address->offset);
    return StaticAddressFact{
        StaticAddressOrigin::NonDynamicObject,
        0,
        address->offset == 0,
        StaticAddressRange{address->bindingName, offset, offset,
                           static_cast<std::uint64_t>(address->offset +
                                                      address->targetByteLength)}};
  }
  if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
    const auto found = staticAddressFacts_.find(variable->bindingName);
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
    const auto offset = staticSignedInteger(*binary->right);
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
  const auto fact = staticAddressFact(expression);
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
    const auto found = staticCStringTerminations_.find(variable->bindingName);
    return found == staticCStringTerminations_.end() ? std::nullopt
                                                       : found->second;
  }
  return std::nullopt;
}

} // namespace hitsimple::safety::detail
