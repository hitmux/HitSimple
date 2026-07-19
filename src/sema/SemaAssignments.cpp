#include "SemaAnalyzer.h"

#include "hitsimple/literal/Literal.h"

#include <cstdint>
#include <limits>
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

std::unique_ptr<hir::Expr>
lowerCompoundPointerValue(std::unique_ptr<hir::Expr> address,
                          std::size_t targetByteLength,
                          std::unique_ptr<hir::Expr> loweredValue,
                          const AssignmentOperator &assignmentOp,
                          std::string_view op,
                          bool unsignedTarget) {
  std::unique_ptr<hir::Expr> left =
      std::make_unique<hir::DerefExpr>(std::move(address), targetByteLength);
  if (unsignedTarget) {
    left = std::make_unique<hir::UnsignedExpr>(std::move(left),
                                               targetByteLength);
  }
  return std::make_unique<hir::BinaryExpr>(
      std::move(left), compoundBinaryOperator(op), std::move(loweredValue),
      assignmentOp.byteLength);
}

} // namespace

std::unique_ptr<hir::Stmt> Analyzer::analyze(const ast::AssignStmt &assign) {
  if (!assign.assignment) {
    addDiagnostic("invalid assignment statement");
    return nullptr;
  }

  auto lowered = lowerAssignmentExpression(*assign.assignment);
  if (!lowered) {
    return nullptr;
  }
  if (lowered->stores.size() == 1U) {
    return std::move(lowered->stores.front());
  }
  return std::make_unique<hir::StatementList>(std::move(lowered->stores));
}

std::optional<AssignmentLowering>
Analyzer::lowerAssignmentExpression(const ast::AssignmentExpr &assign) {
  if (assign.targets.empty() || assign.values.empty()) {
    addDiagnostic("assignment requires at least one target and one value");
    return std::nullopt;
  }
  if (assign.values.size() == 1U) {
    if (const auto *call =
            dynamic_cast<const ast::CallExpr *>(assign.values.front().get())) {
      if (rejectUnavailableStandardBuiltin(*call)) {
        return std::nullopt;
      }
      const auto builtin = builtinForCall(*call).value_or(stdlib::BuiltinId::None);
      if (builtin == stdlib::BuiltinId::Scanf ||
          builtin == stdlib::BuiltinId::Fscanf) {
        return lowerInputLeftContext(assign, *call);
      }
      const auto found = functions_.find(call->callee);
      if (found != functions_.end() && found->second.returnByteLengths.size() > 1U) {
        const auto &signature = found->second;
        if (assign.targets.size() != signature.returnByteLengths.size()) {
          addDiagnostic("multi-return call result count does not match target "
                        "count");
          return std::nullopt;
        }
        auto arguments = analyzeCallArguments(*call, signature);
        if (!result_.diagnostics.empty()) {
          return std::nullopt;
        }
        std::vector<hir::MultiReturnCallStore::Target> targets;
        const Symbol *lastTarget = nullptr;
        for (std::size_t index = 0; index < assign.targets.size(); ++index) {
          const auto &target = assign.targets[index];
          if (target.op != "=" && target.op != "%d=") {
            addDiagnostic("multi-return call targets only support integer "
                          "assignment");
            return std::nullopt;
          }
          const auto *identifier =
              dynamic_cast<const ast::IdentifierExpr *>(target.target.get());
          if (identifier == nullptr) {
            addDiagnostic("multi-return call targets must be identifiers");
            return std::nullopt;
          }
          if (identifier->name == "_") {
            continue;
          }
          const auto *symbol = lookup(identifier->name);
          if (symbol == nullptr) {
            addDiagnostic("use of undeclared variable '" + identifier->name +
                          "'");
            return std::nullopt;
          }
          if (symbol->byteLength != signature.returnByteLengths[index]) {
            addDiagnostic("multi-return target '" + identifier->name +
                          "' byte length does not match return value");
            return std::nullopt;
          }
          targets.emplace_back(symbol->name, symbol->bindingName,
                               symbol->byteLength, symbol->storage, index);
          lastTarget = symbol;
        }
        AssignmentLowering lowered;
        lowered.stores.push_back(std::make_unique<hir::MultiReturnCallStore>(
            call->callee, std::move(arguments), std::move(targets)));
        if (lastTarget != nullptr) {
          lowered.byteLength = lastTarget->byteLength;
          lowered.result = std::make_unique<hir::VariableRef>(
              lastTarget->name, lastTarget->bindingName,
              lastTarget->byteLength, lastTarget->storage,
              lastTarget->templateName);
        } else {
          lowered.byteLength = 1;
          lowered.result = std::make_unique<hir::IntegerLiteral>("0", 1);
        }
        return lowered;
      }
    }
  }
  if (assign.targets.size() != assign.values.size()) {
    addDiagnostic("assignment target count does not match value count");
    return std::nullopt;
  }

  AssignmentLowering lowered;
  const Symbol *lastTarget = nullptr;
  for (std::size_t index = 0; index < assign.targets.size(); ++index) {
    const auto &target = assign.targets[index];
    const auto &value = *assign.values[index];
    std::unique_ptr<hir::Expr> loweredValue;
    bool deferFloatLowering = floatAssignmentByteLength(target.op).has_value();
    if (target.op == "=") {
      if (const auto *identifier =
              dynamic_cast<const ast::IdentifierExpr *>(target.target.get())) {
        if (const auto *symbol = lookup(identifier->name)) {
          deferFloatLowering =
              isFloatTemplate(symbol->templateName) ||
              dynamic_cast<const ast::FloatLiteral *>(&value) != nullptr;
          if ((isUnsignedTemplate(symbol->templateName) &&
               symbol->byteLength == 8 &&
               dynamic_cast<const ast::IntegerLiteral *>(&value) != nullptr) ||
              (isSignedTemplate(symbol->templateName) &&
               symbol->byteLength == 8 && isI64MinLiteral(value))) {
            deferFloatLowering = true;
          }
        }
      }
    }
    if (target.op != "&=" && !deferFloatLowering) {
      loweredValue = analyze(value);
      if (!loweredValue) {
        return std::nullopt;
      }
    }
    auto store = lowerAssignmentTarget(target, value, std::move(loweredValue));
    if (store) {
      lowered.stores.push_back(std::move(store));
    }
    if (const auto *identifier =
            dynamic_cast<const ast::IdentifierExpr *>(target.target.get())) {
      if (identifier->name != "_") {
        lastTarget = lookup(identifier->name);
      }
    }
  }

  if (lastTarget != nullptr) {
    lowered.byteLength = lastTarget->byteLength;
    lowered.result = std::make_unique<hir::VariableRef>(
        lastTarget->name, lastTarget->bindingName, lastTarget->byteLength,
        lastTarget->storage, lastTarget->templateName);
  } else {
    lowered.byteLength = 1;
    lowered.result = std::make_unique<hir::IntegerLiteral>("0", 1);
  }
  return lowered;
}

std::optional<FixedViewAssignmentLowering>
Analyzer::lowerFixedViewAssignment(
    const MemoryReference &target, const ast::Expr &value,
    std::unique_ptr<hir::Expr> loweredValue,
    std::string_view lengthDiagnosticSubject,
    std::string_view targetLengthSubject) {
  const bool isFloating =
      isFloatTemplate(target.templateName) ||
      dynamic_cast<const ast::FloatLiteral *>(&value) != nullptr;
  if (isFloating) {
    if (!isLoweredFloatByteLength(target.byteLength)) {
      addDiagnostic("float byte length " + std::to_string(target.byteLength) +
                    " is not supported yet for '" + target.name + "'");
      return std::nullopt;
    }
    auto floatingValue = analyzeFloatOperand(value, target.byteLength);
    if (!floatingValue) {
      return std::nullopt;
    }
    const auto sourceLength = inferByteLength(value);
    if (!sourceLength || *sourceLength == 0) {
      addDiagnostic(std::string(lengthDiagnosticSubject) +
                    " requires a fixed positive byte length");
      return std::nullopt;
    }
    std::string sourceTemplate = operatorTemplateName(value).value_or("");
    if (dynamic_cast<const ast::FloatLiteral *>(&value) != nullptr) {
      sourceTemplate = "f" + std::to_string(*sourceLength * 8U);
    }
    return FixedViewAssignmentLowering{std::move(floatingValue),
                                       std::move(sourceTemplate),
                                       target.byteLength, true};
  }

  if (isSignedTemplate(target.templateName) && target.byteLength == 8 &&
      isI64MinLiteral(value)) {
    return FixedViewAssignmentLowering{lowerI64MinLiteral(), "", 8, false};
  }

  if (!loweredValue) {
    loweredValue = analyze(value);
    if (!loweredValue) {
      return std::nullopt;
    }
  }

  if (templates_.contains(target.templateName)) {
    const auto compatibility = userTemplateViewAssignmentCompatibility(
        target.templateName, value);
    if (compatibility ==
        UserTemplateViewAssignmentCompatibility::SourceIsNotUserTemplate) {
      addDiagnostic("user template assignment requires a user template source");
      return std::nullopt;
    }
    if (compatibility == UserTemplateViewAssignmentCompatibility::TemplateMismatch) {
      addDiagnostic("default user template assignment requires matching templates");
      return std::nullopt;
    }
  }

  auto sourceLength = inferByteLength(value);
  if (!sourceLength) {
    sourceLength = integerExpressionByteLength(*loweredValue);
  }
  if (!sourceLength || *sourceLength == 0) {
    addDiagnostic(std::string(lengthDiagnosticSubject) +
                  " requires a fixed positive byte length");
    return std::nullopt;
  }
  std::string sourceTemplate = operatorTemplateName(value).value_or("");

  if (const auto *integer = dynamic_cast<const ast::IntegerLiteral *>(&value)) {
    const bool fits = isUnsignedTemplate(target.templateName)
                          ? unsignedIntegerFits(*integer, target.byteLength)
                          : integerFits(*integer, target.byteLength);
    if (!fits) {
      addDiagnostic("integer literal '" + integer->value +
                    "' does not fit in target '" + target.name + "'");
      return std::nullopt;
    }
  }

  if (const auto *character = dynamic_cast<const ast::CharLiteral *>(&value)) {
    const auto decoded = literal::decodeCharLiteral(character->value);
    if (!decoded) {
      addDiagnostic("invalid character literal '" + character->value +
                    "': " + *decoded.error);
      return std::nullopt;
    }
    if (decoded.bytes.size() > target.byteLength) {
      addDiagnostic("character literal byte length does not fit target '" +
                    target.name + "'");
      return std::nullopt;
    }
  }

  if (isFloatTemplate(sourceTemplate) && *sourceLength != target.byteLength) {
    addDiagnostic(std::string(lengthDiagnosticSubject) + " byte length " +
                  std::to_string(*sourceLength) +
                  " does not match " + std::string(targetLengthSubject) +
                  " byte length " +
                  std::to_string(target.byteLength));
    return std::nullopt;
  }
  if (!isIntegerExpression(*loweredValue)) {
    addDiagnostic("right operand of '=' is not an integer expression");
    return std::nullopt;
  }

  if (*sourceLength != target.byteLength) {
    loweredValue = std::make_unique<hir::IntegerCastExpr>(
        std::move(loweredValue), target.byteLength, true);
  }
  return FixedViewAssignmentLowering{std::move(loweredValue),
                                     std::move(sourceTemplate),
                                     target.byteLength, false};
}

std::unique_ptr<hir::Stmt> Analyzer::lowerAssignmentTarget(
    const ast::AssignmentTarget &assignmentTarget, const ast::Expr &value,
    std::unique_ptr<hir::Expr> loweredValue) {
  return lowerAssignmentTarget(*assignmentTarget.target, assignmentTarget.op,
                               assignmentTarget.unsignedTarget, value,
                               std::move(loweredValue));
}

std::unique_ptr<hir::Stmt> Analyzer::lowerAssignmentTarget(
    const ast::Expr &target, std::string_view op, bool unsignedTarget,
    const ast::Expr &value, std::unique_ptr<hir::Expr> loweredValue,
    const MemoryReference *directTarget,
    std::string_view lengthDiagnosticSubject,
    std::string_view targetLengthSubject) {
  if (const auto *identifier =
          dynamic_cast<const ast::IdentifierExpr *>(&target)) {
    if (identifier->name == "_") {
      return nullptr;
    }
  }

  std::string targetName;
  std::optional<MemoryReference> reference;
  if (directTarget != nullptr) {
    targetName = directTarget->name;
    reference = *directTarget;
  } else if (const auto *identifier =
          dynamic_cast<const ast::IdentifierExpr *>(&target)) {
    targetName = identifier->name;
  } else {
    reference = resolveAddressableReference(target);
    if (reference) {
      targetName = reference->name;
    } else if (!result_.diagnostics.empty()) {
      return nullptr;
    } else {
      if (const auto *index =
            dynamic_cast<const ast::IndexExpr *>(&target)) {
        const auto assignmentOp =
            op == "="
                ? std::optional<AssignmentOperator>{AssignmentOperator{1, '\0'}}
                : integerAssignmentOperator(op);
        if (!assignmentOp) {
          addDiagnostic("unsupported index assignment operator '" +
                        std::string(op) + "'");
          return nullptr;
        }
        auto address = lowerIndexAddress(*index);
        if (!address) {
          return nullptr;
        }
        if (isHandleExpression(value)) {
          addDiagnostic("handle value may only be assigned to a handle target");
          return nullptr;
        }
        if (!isIntegerExpression(*loweredValue)) {
          addDiagnostic("index assignment value is not an integer expression");
          return nullptr;
        }
        if (assignmentOp->compoundOp != '\0') {
          if (isDivisionOperator(assignmentOp->compoundOp)) {
            if (const auto *integer =
                    dynamic_cast<const ast::IntegerLiteral *>(&value)) {
              if (integer->value == "0") {
                addDiagnostic("division by zero in assignment expression");
                return nullptr;
              }
            }
          }
          auto readAddress = lowerIndexAddress(*index);
          if (!readAddress) {
            return nullptr;
          }
          loweredValue = lowerCompoundPointerValue(
              std::move(readAddress), 1, std::move(loweredValue),
              *assignmentOp, op, unsignedTarget);
        }
        return std::make_unique<hir::PointerStore>(
            std::move(address), 1, std::move(loweredValue));
      }
      if (const auto *slice =
              dynamic_cast<const ast::SliceExpr *>(&target)) {
        const auto assignmentOp =
            op == "="
                ? std::optional<AssignmentOperator>{AssignmentOperator{0, '\0'}}
                : integerAssignmentOperator(op);
        if (!assignmentOp) {
          addDiagnostic("unsupported slice assignment operator '" +
                        std::string(op) + "'");
          return nullptr;
        }
        auto lowered = lowerSlice(*slice);
        if (!lowered) {
          return nullptr;
        }
        if (isHandleExpression(value)) {
          addDiagnostic("handle value may only be assigned to a handle target");
          return nullptr;
        }
        if (!isIntegerExpression(*loweredValue)) {
          addDiagnostic("slice assignment value is not an integer expression");
          return nullptr;
        }
        if (assignmentOp->compoundOp != '\0') {
          if (isDivisionOperator(assignmentOp->compoundOp)) {
            if (const auto *integer =
                    dynamic_cast<const ast::IntegerLiteral *>(&value)) {
              if (integer->value == "0") {
                addDiagnostic("division by zero in assignment expression");
                return nullptr;
              }
            }
          }
          auto readLowered = lowerSlice(*slice);
          if (!readLowered) {
            return nullptr;
          }
          loweredValue = lowerCompoundPointerValue(
              std::move(readLowered->address), lowered->byteLength,
              std::move(loweredValue), *assignmentOp, op, unsignedTarget);
        }
        return std::make_unique<hir::PointerStore>(
            std::move(lowered->address), lowered->byteLength,
            std::move(loweredValue));
      }
      if (dynamic_cast<const ast::MemberExpr *>(&target) !=
          nullptr) {
        addDiagnostic("member assignment targets are not supported yet");
        return nullptr;
      }
      if (const auto *deref =
              dynamic_cast<const ast::DerefExpr *>(&target)) {
        if (isHandleExpression(*deref->address)) {
          addDiagnostic("handle values cannot be dereferenced");
          return nullptr;
        }
        const auto assignmentOp =
            op == "="
                ? std::optional<AssignmentOperator>{AssignmentOperator{0, '\0'}}
                : integerAssignmentOperator(op);
        if (!assignmentOp) {
          addDiagnostic("unsupported dereference assignment operator '" +
                        std::string(op) + "'");
          return nullptr;
        }
        const auto length = parseByteLength(deref->length);
        if (length == 0) {
          addDiagnostic("invalid dereference byte length");
          return nullptr;
        }
        auto address = analyze(*deref->address);
        if (!address) {
          return nullptr;
        }
        if (!isIntegerExpression(*address)) {
          addDiagnostic("dereference address is not an integer expression");
          return nullptr;
        }
        if (const auto *literalAddress =
                dynamic_cast<const hir::IntegerLiteral *>(address.get())) {
          if (literal::parseUnsignedIntegerLiteral(literalAddress->value) == 0) {
            addDiagnostic("null address dereference is not allowed");
            return nullptr;
          }
        }
        if (integerExpressionByteLength(*address).value_or(0) >
            pointerByteLength()) {
          addDiagnostic("dereference address is wider than pointer length");
          return nullptr;
        }
        if (isHandleExpression(value)) {
          addDiagnostic("handle value may only be assigned to a handle target");
          return nullptr;
        }
        if (!isIntegerExpression(*loweredValue)) {
          addDiagnostic("dereference assignment value is not an integer expression");
          return nullptr;
        }
        if (assignmentOp->compoundOp != '\0') {
          if (isDivisionOperator(assignmentOp->compoundOp)) {
            if (const auto *integer =
                    dynamic_cast<const ast::IntegerLiteral *>(&value)) {
              if (integer->value == "0") {
                addDiagnostic("division by zero in assignment expression");
                return nullptr;
              }
            }
          }
          auto readAddress = analyze(*deref->address);
          if (!readAddress) {
            return nullptr;
          }
          if (!isIntegerExpression(*readAddress)) {
            addDiagnostic("dereference address is not an integer expression");
            return nullptr;
          }
          if (integerExpressionByteLength(*readAddress).value_or(0) >
              pointerByteLength()) {
            addDiagnostic("dereference address is wider than pointer length");
            return nullptr;
          }
          loweredValue = lowerCompoundPointerValue(
              std::move(readAddress), length, std::move(loweredValue),
              *assignmentOp, op, unsignedTarget);
        }
        return std::make_unique<hir::PointerStore>(
            std::move(address), length, std::move(loweredValue));
      }
      if (dynamic_cast<const ast::DerefExpr *>(&target) !=
          nullptr) {
        return nullptr;
      }
      addDiagnostic("complex assignment targets are not supported yet");
      return nullptr;
    }
  }

  MemoryReference targetRef;
  if (reference) {
    targetRef = *reference;
  } else {
    const auto *target = lookup(targetName);
    if (target == nullptr) {
      addDiagnostic("use of undeclared variable '" + targetName + "'");
      return nullptr;
    }
    targetRef = MemoryReference{target->name, target->bindingName,
                                target->byteLength, target->storage, 0,
                                target->templateName};
  }

  const bool targetIsHandle = targetRef.templateName == "handle";
  const bool valueIsHandle = isHandleExpression(value);
  if (targetIsHandle) {
    if (op != "=") {
      addDiagnostic("handle target '" + targetName +
                    "' only supports default assignment");
      return nullptr;
    }
    if (!valueIsHandle) {
      addDiagnostic("handle target '" + targetName +
                    "' can only be assigned from a handle value");
      return nullptr;
    }
    if (!loweredValue) {
      loweredValue = analyze(value);
      if (!loweredValue) {
        return nullptr;
      }
    }
    return std::make_unique<hir::IntegerStore>(
        targetRef.name, targetRef.bindingName, targetRef.byteLength,
        targetRef.storage, targetRef.offset, std::move(loweredValue));
  }
  if (valueIsHandle) {
    addDiagnostic("handle value may only be assigned to a handle target");
    return nullptr;
  }

  if (op == "&=") {
    if (targetRef.byteLength != pointerByteLength()) {
      addDiagnostic("address rebinding target '" + targetName +
                    "' must be pointer-sized");
      return nullptr;
    }
    auto address = analyze(value);
    if (!address) {
      return nullptr;
    }
    if (!isIntegerExpression(*address) ||
        integerExpressionByteLength(*address).value_or(0) !=
            pointerByteLength()) {
      addDiagnostic("right operand of '&=' is not a pointer-sized expression");
      return nullptr;
    }
    return std::make_unique<hir::IntegerStore>(
        targetRef.name, targetRef.bindingName, targetRef.byteLength,
        targetRef.storage, targetRef.offset, std::move(address));
  }

  if (op == "=" && templates_.contains(targetRef.templateName)) {
    auto fixed = lowerFixedViewAssignment(
        targetRef, value, std::move(loweredValue), lengthDiagnosticSubject,
        targetLengthSubject);
    if (!fixed) {
      return nullptr;
    }
    loweredValue = std::move(fixed->value);
    const std::string key = "=|" + targetRef.templateName + "|" +
                            targetRef.templateName;
    if (const auto found = implOpIndexes_.find(key);
        found != implOpIndexes_.end()) {
      const auto &info = implOpInfos_[found->second];
      if (!loweredValue || info.returnByteLengths.size() != 1U) {
        addDiagnostic("internal error: invalid assignment impl op");
        return nullptr;
      }
      std::vector<std::unique_ptr<hir::Expr>> arguments;
      arguments.push_back(std::make_unique<hir::VariableRef>(
          targetRef.name, targetRef.bindingName, targetRef.byteLength,
          targetRef.storage, targetRef.offset, targetRef.templateName));
      arguments.push_back(std::move(loweredValue));
      return std::make_unique<hir::UserTemplateOpCall>(
          info.symbolName, std::move(arguments), info.returnByteLengths.front());
    }
  }

  if (op == "%s=" || (op == "=" && targetRef.templateName == "cstr")) {
    if (const auto *string = dynamic_cast<const ast::StringLiteral *>(&value)) {
      const auto decoded = literal::decodeStringLiteral(string->value);
      if (!decoded) {
        addDiagnostic("invalid string literal '" + string->value +
                      "': " + *decoded.error);
        return nullptr;
      }
      return std::make_unique<hir::StringStore>(
          targetRef.name, targetRef.bindingName, targetRef.byteLength,
          targetRef.storage, targetRef.offset, string->value);
    }
    if (!loweredValue) {
      loweredValue = analyze(value);
      if (!loweredValue) {
        return nullptr;
      }
    }
    if (const auto *source =
            dynamic_cast<const hir::VariableRef *>(loweredValue.get())) {
      return std::make_unique<hir::StringCopyStore>(
          targetRef.name, targetRef.bindingName, targetRef.byteLength,
          targetRef.storage, targetRef.offset, source->name,
          source->bindingName, source->byteLength, source->storage,
          source->offset);
    }
    addDiagnostic("right operand of '" + std::string(op) +
                  "' is not a string source");
    return nullptr;
  }

  if (op == "%b=") {
    if (!isIntegerExpression(*loweredValue)) {
      addDiagnostic("right operand of '%b=' is not an integer expression");
      return nullptr;
    }
    return std::make_unique<hir::BoolStore>(targetRef.name, targetRef.bindingName,
                                            targetRef.byteLength,
                                            targetRef.storage,
                                            targetRef.offset,
                                            std::move(loweredValue));
  }

  if (op == "=" && !templates_.contains(targetRef.templateName)) {
    auto fixed = lowerFixedViewAssignment(
        targetRef, value, std::move(loweredValue), lengthDiagnosticSubject,
        targetLengthSubject);
    if (!fixed) {
      return nullptr;
    }
    if (fixed->isFloating) {
      return std::make_unique<hir::FloatStore>(
          targetRef.name, targetRef.bindingName, targetRef.byteLength,
          targetRef.storage, targetRef.offset, std::move(fixed->value));
    }
    return std::make_unique<hir::IntegerStore>(
        targetRef.name, targetRef.bindingName, targetRef.byteLength,
        targetRef.storage, targetRef.offset, std::move(fixed->value));
  }

  const auto floatLength = floatAssignmentByteLength(op);
  if (floatLength || (op == "=" &&
                      (isFloatTemplate(targetRef.templateName) ||
                       dynamic_cast<const ast::FloatLiteral *>(&value) !=
                           nullptr))) {
    const auto targetLength =
        !floatLength || *floatLength == 0 ? targetRef.byteLength : *floatLength;
    if (!isLoweredFloatByteLength(targetLength)) {
      addDiagnostic("float byte length " + std::to_string(targetLength) +
                    " is not supported yet for '" + targetName + "'");
      return nullptr;
    }
    if (targetLength != targetRef.byteLength) {
      addDiagnostic("float assignment byte length does not match target '" +
                    targetName + "'");
      return nullptr;
    }
    auto floatValue = analyzeFloatOperand(value, targetRef.byteLength);
    if (!floatValue) {
      return nullptr;
    }
    return std::make_unique<hir::FloatStore>(
        targetRef.name, targetRef.bindingName, targetRef.byteLength,
        targetRef.storage, targetRef.offset, std::move(floatValue));
  }

  const auto assignmentOp =
      op == "=" ? std::optional<AssignmentOperator>{AssignmentOperator{
                    targetRef.byteLength, '\0'}}
                : integerAssignmentOperator(op);
  if (!assignmentOp) {
    addDiagnostic("unsupported assignment operator '" + std::string(op) + "'");
    return nullptr;
  }

  if (assignmentOp->compoundOp != '\0') {
    if (isDivisionOperator(assignmentOp->compoundOp)) {
      if (const auto *integer =
              dynamic_cast<const ast::IntegerLiteral *>(&value)) {
        if (integer->value == "0") {
          addDiagnostic("division by zero in assignment expression");
          return nullptr;
        }
      }
    }
    if (!isIntegerExpression(*loweredValue)) {
      addDiagnostic("right operand of '" + std::string(op) +
                    "' is not an integer expression");
      return nullptr;
    }
    std::unique_ptr<hir::Expr> left = std::make_unique<hir::VariableRef>(
        targetRef.name, targetRef.bindingName, targetRef.byteLength,
        targetRef.storage, targetRef.offset, targetRef.templateName);
    if (unsignedTarget) {
      left = std::make_unique<hir::UnsignedExpr>(std::move(left),
                                                 targetRef.byteLength);
    }
    auto compoundValue = std::make_unique<hir::BinaryExpr>(
        std::move(left), compoundBinaryOperator(op), std::move(loweredValue),
        assignmentOp->byteLength);
    return std::make_unique<hir::IntegerStore>(
        targetRef.name, targetRef.bindingName, targetRef.byteLength,
        targetRef.storage, targetRef.offset,
        std::move(compoundValue));
  }

  if (const auto *integer =
          dynamic_cast<const ast::IntegerLiteral *>(&value)) {
    const bool fits = isUnsignedTemplate(targetRef.templateName)
                          ? unsignedIntegerFits(*integer, targetRef.byteLength)
                          : integerFits(*integer, targetRef.byteLength);
    if (!fits) {
      addDiagnostic("integer literal '" + integer->value +
                    "' does not fit in target '" + targetName + "'");
      return nullptr;
    }
    if (isUnsignedTemplate(targetRef.templateName) &&
        targetRef.byteLength == 8) {
      return std::make_unique<hir::IntegerStore>(
          targetRef.name, targetRef.bindingName, targetRef.byteLength,
          targetRef.storage, targetRef.offset,
          std::make_unique<hir::IntegerLiteral>(integer->value,
                                                targetRef.byteLength));
    }
  }

  if (isSignedTemplate(targetRef.templateName) && targetRef.byteLength == 8 &&
      isI64MinLiteral(value)) {
    return std::make_unique<hir::IntegerStore>(
        targetRef.name, targetRef.bindingName, targetRef.byteLength,
        targetRef.storage, targetRef.offset, lowerI64MinLiteral());
  }

  if (const auto *character = dynamic_cast<const ast::CharLiteral *>(&value)) {
    const auto decoded = literal::decodeCharLiteral(character->value);
    if (!decoded) {
      addDiagnostic("invalid character literal '" + character->value +
                    "': " + *decoded.error);
      return nullptr;
    }
    if (decoded.bytes.size() > targetRef.byteLength) {
      addDiagnostic("character literal byte length does not fit target '" +
                    targetName + "'");
      return nullptr;
    }
  }

  if (!isIntegerExpression(*loweredValue)) {
    addDiagnostic("right operand of '" + std::string(op) +
                  "' is not an integer expression");
    return nullptr;
  }

  return std::make_unique<hir::IntegerStore>(targetRef.name, targetRef.bindingName,
                                             targetRef.byteLength,
                                             targetRef.storage,
                                             targetRef.offset,
                                             std::move(loweredValue));
}

std::unique_ptr<hir::Stmt>
Analyzer::analyzeStringAssign(const ast::AssignStmt &assign,
                              const Symbol &target) {
  const auto *string =
      dynamic_cast<const ast::StringLiteral *>(assign.value.get());
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
Analyzer::analyzeBoolAssign(const ast::AssignStmt &assign,
                            const Symbol &target) {
  auto value = analyze(*assign.value);
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

std::unique_ptr<hir::Stmt>
Analyzer::analyzeCompoundAssign(const ast::AssignStmt &assign,
                                const Symbol &target,
                                const AssignmentOperator &assignmentOp) {
  if (isDivisionOperator(assignmentOp.compoundOp)) {
    if (const auto *integer =
            dynamic_cast<const ast::IntegerLiteral *>(assign.value.get())) {
      if (integer->value == "0") {
        addDiagnostic("division by zero in assignment expression");
        return nullptr;
      }
    }
  }

  auto right = analyze(*assign.value);
  if (!right) {
    return nullptr;
  }
  if (!isIntegerExpression(*right)) {
    addDiagnostic("right operand of '" + assign.op +
                  "' is not an integer expression");
    return nullptr;
  }

  auto left = std::make_unique<hir::VariableRef>(
      target.name, target.bindingName, target.byteLength, target.storage,
      target.templateName);
  auto value = std::make_unique<hir::BinaryExpr>(
      std::move(left), compoundBinaryOperator(assign.op), std::move(right),
      assignmentOp.byteLength);

  return std::make_unique<hir::IntegerStore>(target.name, target.bindingName,
                                             target.byteLength, target.storage,
                                             std::move(value));
}


} // namespace hitsimple::sema
