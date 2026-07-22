#include "hitsimple/hir/HIR.h"

#include "hitsimple/literal/Literal.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <unordered_map>
#include <utility>

namespace hitsimple::hir {

namespace {
thread_local std::optional<diagnostic::SourceRange> activeRange;

std::optional<std::size_t> staticAllocationExtent(const Expr &expression) {
  if (const auto *literal = dynamic_cast<const IntegerLiteral *>(&expression)) {
    const auto value = literal::parseUnsignedIntegerLiteral(literal->value);
    if (value && *value <= std::numeric_limits<std::size_t>::max()) {
      return static_cast<std::size_t>(*value);
    }
    return std::nullopt;
  }
  if (const auto *unsignedValue =
          dynamic_cast<const UnsignedExpr *>(&expression)) {
    return staticAllocationExtent(*unsignedValue->operand);
  }
  if (const auto *cast = dynamic_cast<const IntegerCastExpr *>(&expression)) {
    return staticAllocationExtent(*cast->operand);
  }
  if (const auto *view = dynamic_cast<const TemplateViewExpr *>(&expression)) {
    return staticAllocationExtent(*view->operand);
  }
  return std::nullopt;
}

std::optional<AddressFacts> addressFactsFor(const Expr &expression) {
  if (const auto *address = dynamic_cast<const AddressOfExpr *>(&expression)) {
    return address->facts;
  }
  if (const auto *variable = dynamic_cast<const VariableRef *>(&expression)) {
    return variable->addressFacts;
  }
  if (const auto *binary = dynamic_cast<const BinaryExpr *>(&expression)) {
    return binary->addressFacts;
  }
  if (const auto *call = dynamic_cast<const CallExpr *>(&expression)) {
    return call->addressFacts;
  }
  if (const auto *unsignedValue =
          dynamic_cast<const UnsignedExpr *>(&expression)) {
    return addressFactsFor(*unsignedValue->operand);
  }
  if (const auto *cast = dynamic_cast<const IntegerCastExpr *>(&expression)) {
    return addressFactsFor(*cast->operand);
  }
  if (const auto *view = dynamic_cast<const TemplateViewExpr *>(&expression)) {
    return addressFactsFor(*view->operand);
  }
  return std::nullopt;
}

std::string_view binaryOperatorSuffix(std::string_view op) {
  if (!op.starts_with('%')) {
    return op;
  }
  const auto marker = op.find_last_of("duf");
  return marker == std::string_view::npos || marker + 1U >= op.size()
             ? op
             : op.substr(marker + 1U);
}

BinaryOperator binaryOperatorFor(std::string_view op) {
  op = binaryOperatorSuffix(op);
  if (op == "+") return BinaryOperator::Add;
  if (op == "-") return BinaryOperator::Subtract;
  if (op == "*") return BinaryOperator::Multiply;
  if (op == "/") return BinaryOperator::Divide;
  if (op == "%") return BinaryOperator::Modulo;
  if (op == "**") return BinaryOperator::Power;
  if (op == "<<") return BinaryOperator::ShiftLeft;
  if (op == ">>") return BinaryOperator::ShiftRight;
  if (op == "&") return BinaryOperator::BitAnd;
  if (op == "|") return BinaryOperator::BitOr;
  if (op == "^") return BinaryOperator::BitXor;
  if (op == "==") return BinaryOperator::Equal;
  if (op == "!=") return BinaryOperator::NotEqual;
  if (op == "<") return BinaryOperator::Less;
  if (op == "<=") return BinaryOperator::LessEqual;
  if (op == ">") return BinaryOperator::Greater;
  if (op == ">=") return BinaryOperator::GreaterEqual;
  if (op == "&&") return BinaryOperator::LogicalAnd;
  if (op == "||") return BinaryOperator::LogicalOr;
  return BinaryOperator::Unknown;
}

std::optional<std::size_t>
standardTemplateByteLength(std::string_view templateName) {
  if (templateName == "i8" || templateName == "u8") return 1U;
  if (templateName == "i16" || templateName == "u16" ||
      templateName == "f16") {
    return 2U;
  }
  if (templateName == "i32" || templateName == "u32" ||
      templateName == "f32") {
    return 4U;
  }
  if (templateName == "i64" || templateName == "u64" ||
      templateName == "f64") {
    return 8U;
  }
  if (templateName == "f128") return 16U;
  if (templateName == "bool") return 1U;
  return std::nullopt;
}

bool isComparisonOperation(BinaryOperator operation) {
  return operation == BinaryOperator::Equal ||
         operation == BinaryOperator::NotEqual ||
         operation == BinaryOperator::Less ||
         operation == BinaryOperator::LessEqual ||
         operation == BinaryOperator::Greater ||
         operation == BinaryOperator::GreaterEqual;
}

bool isBooleanResultOperation(BinaryOperator operation) {
  return isComparisonOperation(operation) ||
         operation == BinaryOperator::LogicalAnd ||
         operation == BinaryOperator::LogicalOr;
}
} // namespace

void setActiveSourceRange(std::optional<diagnostic::SourceRange> range) {
  activeRange = std::move(range);
}

std::optional<diagnostic::SourceRange> activeSourceRange() {
  return activeRange;
}

std::string_view toString(ViewCategory category) {
  switch (category) {
  case ViewCategory::SignedInteger: return "signed-integer";
  case ViewCategory::UnsignedInteger: return "unsigned-integer";
  case ViewCategory::UntemplatedInteger: return "untemplated-integer";
  case ViewCategory::Floating: return "floating";
  case ViewCategory::Boolean: return "boolean";
  case ViewCategory::Address: return "address";
  case ViewCategory::Handle: return "handle";
  case ViewCategory::Bytes: return "bytes";
  case ViewCategory::CString: return "cstring";
  case ViewCategory::UserTemplate: return "user-template";
  case ViewCategory::RawBytes: return "raw-bytes";
  }
  return "unknown";
}

std::string_view toString(IntegerInterpretation interpretation) {
  switch (interpretation) {
  case IntegerInterpretation::None: return "none";
  case IntegerInterpretation::Signed: return "signed";
  case IntegerInterpretation::Unsigned: return "unsigned";
  case IntegerInterpretation::RawOnly: return "raw-only";
  }
  return "unknown";
}

std::string_view toString(ViewLengthKind kind) {
  switch (kind) {
  case ViewLengthKind::Static: return "static";
  case ViewLengthKind::Dynamic: return "dynamic";
  }
  return "unknown";
}

ViewSemantics staticViewSemantics(ViewCategory category,
                                  IntegerInterpretation interpretation,
                                  std::size_t byteLength,
                                  std::string templateName,
                                  bool isAddressable,
                                  bool isMutableLValue) {
  return ViewSemantics{category, interpretation, ViewLengthKind::Static,
                       byteLength, std::move(templateName), isAddressable,
                       isMutableLValue};
}

ViewSemantics dynamicViewSemantics(ViewCategory category,
                                   std::string templateName) {
  return ViewSemantics{category, IntegerInterpretation::None,
                       ViewLengthKind::Dynamic, 0, std::move(templateName),
                       false, false};
}

ViewSemantics viewSemanticsForTemplate(
    std::string templateName, std::size_t byteLength, bool isAddressable,
    bool isMutableLValue,
    IntegerInterpretation untemplatedInterpretation) {
  if (templateName == "i8" || templateName == "i16" ||
      templateName == "i32" || templateName == "i64") {
    return staticViewSemantics(ViewCategory::SignedInteger,
                               IntegerInterpretation::Signed, byteLength,
                               std::move(templateName), isAddressable,
                               isMutableLValue);
  }
  if (templateName == "u8" || templateName == "u16" ||
      templateName == "u32" || templateName == "u64") {
    return staticViewSemantics(ViewCategory::UnsignedInteger,
                               IntegerInterpretation::Unsigned, byteLength,
                               std::move(templateName), isAddressable,
                               isMutableLValue);
  }
  if (templateName == "f16" || templateName == "f32" ||
      templateName == "f64" || templateName == "f128") {
    return staticViewSemantics(ViewCategory::Floating,
                               IntegerInterpretation::None, byteLength,
                               std::move(templateName), isAddressable,
                               isMutableLValue);
  }
  if (templateName == "bool") {
    return staticViewSemantics(ViewCategory::Boolean,
                               IntegerInterpretation::None, byteLength,
                               std::move(templateName), isAddressable,
                               isMutableLValue);
  }
  if (templateName == "addr") {
    return staticViewSemantics(ViewCategory::Address,
                               IntegerInterpretation::None, byteLength,
                               std::move(templateName), isAddressable,
                               isMutableLValue);
  }
  if (templateName == "handle") {
    return staticViewSemantics(ViewCategory::Handle,
                               IntegerInterpretation::None, byteLength,
                               std::move(templateName), isAddressable,
                               isMutableLValue);
  }
  if (templateName == "bytes") {
    return staticViewSemantics(ViewCategory::Bytes,
                               IntegerInterpretation::None, byteLength,
                               std::move(templateName), isAddressable,
                               isMutableLValue);
  }
  if (templateName == "cstr") {
    return staticViewSemantics(ViewCategory::CString,
                               IntegerInterpretation::None, byteLength,
                               std::move(templateName), isAddressable,
                               isMutableLValue);
  }
  if (templateName.empty() || templateName == "none") {
    return staticViewSemantics(ViewCategory::UntemplatedInteger,
                               untemplatedInterpretation, byteLength, {},
                               isAddressable, isMutableLValue);
  }
  return staticViewSemantics(ViewCategory::UserTemplate,
                             IntegerInterpretation::None, byteLength,
                             std::move(templateName), isAddressable,
                             isMutableLValue);
}

ViewSemantics booleanTestResultSemantics() {
  return staticViewSemantics(ViewCategory::Boolean,
                             IntegerInterpretation::None, 1, "bool");
}

bool isFixedView(const ViewSemantics &semantics) {
  return semantics.lengthKind == ViewLengthKind::Static;
}

bool isIntegerNumeric(const ViewSemantics &semantics) {
  return semantics.category == ViewCategory::SignedInteger ||
         semantics.category == ViewCategory::UnsignedInteger ||
         semantics.category == ViewCategory::UntemplatedInteger;
}

bool isFloatingNumeric(const ViewSemantics &semantics) {
  return semantics.category == ViewCategory::Floating;
}

bool isBooleanTestable(const ViewSemantics &semantics) {
  return semantics.lengthKind == ViewLengthKind::Dynamic ||
         semantics.staticByteLength != 0;
}

bool isAddressValue(const ViewSemantics &semantics) {
  return semantics.category == ViewCategory::Address;
}

bool isRawOnly(const ViewSemantics &semantics) {
  return semantics.integerInterpretation == IntegerInterpretation::RawOnly;
}

std::string_view toString(StandardOperationKind kind) {
  switch (kind) {
  case StandardOperationKind::Legacy:
    return "legacy";
  case StandardOperationKind::UntemplatedInteger:
    return "untemplated-integer";
  case StandardOperationKind::StandardInteger:
    return "standard-integer";
  case StandardOperationKind::StandardBoolean:
    return "standard-bool";
  case StandardOperationKind::StandardAddress:
    return "standard-addr";
  case StandardOperationKind::StandardHandle:
    return "standard-handle";
  case StandardOperationKind::StandardBytesCompare:
    return "standard-bytes-compare";
  case StandardOperationKind::StandardCStringCompare:
    return "standard-cstr-compare";
  case StandardOperationKind::AddressOffset:
    return "address-offset";
  }
  return "unknown";
}

std::string_view toString(BinaryOperator op) {
  switch (op) {
  case BinaryOperator::Unknown: return "unknown";
  case BinaryOperator::Add: return "+";
  case BinaryOperator::Subtract: return "-";
  case BinaryOperator::Multiply: return "*";
  case BinaryOperator::Divide: return "/";
  case BinaryOperator::Modulo: return "%";
  case BinaryOperator::Power: return "**";
  case BinaryOperator::ShiftLeft: return "<<";
  case BinaryOperator::ShiftRight: return ">>";
  case BinaryOperator::BitAnd: return "&";
  case BinaryOperator::BitOr: return "|";
  case BinaryOperator::BitXor: return "^";
  case BinaryOperator::Equal: return "==";
  case BinaryOperator::NotEqual: return "!=";
  case BinaryOperator::Less: return "<";
  case BinaryOperator::LessEqual: return "<=";
  case BinaryOperator::Greater: return ">";
  case BinaryOperator::GreaterEqual: return ">=";
  case BinaryOperator::LogicalAnd: return "&&";
  case BinaryOperator::LogicalOr: return "||";
  }
  return "unknown";
}

std::string_view toString(AddressOrigin origin) {
  switch (origin) {
  case AddressOrigin::LocalObject:
    return "local-object";
  case AddressOrigin::StaticObject:
    return "static-object";
  case AddressOrigin::GlobalObject:
    return "global-object";
  case AddressOrigin::DynamicAllocation:
    return "dynamic-allocation";
  case AddressOrigin::ExternalObject:
    return "external-object";
  case AddressOrigin::PointerDerived:
    return "pointer-derived";
  case AddressOrigin::OpaqueInteger:
    return "opaque-integer";
  }
  return "unknown";
}

std::string_view toString(ConversionKind kind) {
  switch (kind) {
  case ConversionKind::Identity:
    return "identity";
  case ConversionKind::IntegerWidth:
    return "integer-width";
  case ConversionKind::Floating:
    return "floating";
  case ConversionKind::BooleanNormalize:
    return "boolean-normalize";
  case ConversionKind::ByteCopy:
    return "byte-copy";
  case ConversionKind::CStringCopy:
    return "cstr-copy";
  case ConversionKind::UserTemplateAssignment:
    return "user-template-assignment";
  }
  return "unknown";
}

Expr::Expr(ViewSemantics result)
    : range(activeSourceRange()), result(std::move(result)) {}

Stmt::Stmt() : range(activeSourceRange()) {}

std::string_view toString(MemoryStorage storage) {
  switch (storage) {
  case MemoryStorage::Global:
    return "global";
  case MemoryStorage::Local:
    return "local";
  case MemoryStorage::StaticLocal:
    return "static";
  }
  return "unknown";
}

std::string_view toString(Linkage linkage) {
  switch (linkage) {
  case Linkage::External:
    return "external";
  case Linkage::Internal:
    return "internal";
  }
  return "unknown";
}

std::string_view toString(AbiValueKind kind) {
  switch (kind) {
  case AbiValueKind::Integer:
    return "integer";
  case AbiValueKind::Floating:
    return "floating";
  case AbiValueKind::Pointer:
    return "pointer";
  case AbiValueKind::Aggregate:
    return "aggregate";
  }
  return "unknown";
}

std::string_view toString(DynamicByteViewOperation operation) {
  switch (operation) {
  case DynamicByteViewOperation::ResizeBytes:
    return "resize_bytes";
  case DynamicByteViewOperation::ByteSwap:
    return "byte_swap";
  }
  return "unknown";
}

std::string_view toString(FormatOutputSink sink) {
  switch (sink) {
  case FormatOutputSink::Stdout:
    return "stdout";
  case FormatOutputSink::File:
    return "file";
  }
  return "unknown";
}

IntegerLiteral::IntegerLiteral(std::string value, ViewSemantics result)
    : Expr(std::move(result)), value(std::move(value)),
      byteLength(this->result.staticByteLength) {}

CharacterLiteral::CharacterLiteral(std::string bytes, ViewSemantics result)
    : Expr(std::move(result)), bytes(std::move(bytes)),
      byteLength(this->result.staticByteLength) {}

StringLiteral::StringLiteral(std::string value, ViewSemantics result)
    : Expr(std::move(result)), value(std::move(value)),
      byteLength(this->result.staticByteLength) {}

FloatLiteral::FloatLiteral(std::string value, ViewSemantics result)
    : Expr(std::move(result)), value(std::move(value)),
      byteLength(this->result.staticByteLength) {}

VariableRef::VariableRef(std::string name, ViewSemantics result)
    : Expr(std::move(result)), name(std::move(name)), bindingName(this->name),
      byteLength(this->result.staticByteLength),
      templateName(this->result.templateName) {}

VariableRef::VariableRef(std::string name, std::string bindingName,
                         MemoryStorage storage, ViewSemantics result)
    : Expr(std::move(result)), name(std::move(name)),
      bindingName(std::move(bindingName)),
      byteLength(this->result.staticByteLength), storage(storage),
      templateName(this->result.templateName) {}

VariableRef::VariableRef(std::string name, std::string bindingName,
                         MemoryStorage storage, std::size_t offset,
                         ViewSemantics result)
    : Expr(std::move(result)), name(std::move(name)),
      bindingName(std::move(bindingName)),
      byteLength(this->result.staticByteLength), storage(storage), offset(offset),
      templateName(this->result.templateName) {}

AddressOfExpr::AddressOfExpr(std::string name, std::string bindingName,
                             std::size_t targetByteLength,
                             MemoryStorage storage, std::size_t offset,
                             ViewSemantics result)
    : Expr(std::move(result)), name(std::move(name)), bindingName(std::move(bindingName)),
      targetByteLength(targetByteLength), storage(storage), offset(offset),
      byteLength(this->result.staticByteLength),
      facts{storage == MemoryStorage::Local
                ? AddressOrigin::LocalObject
                : (storage == MemoryStorage::StaticLocal
                       ? AddressOrigin::StaticObject
                       : AddressOrigin::GlobalObject),
            targetByteLength, std::nullopt, offset == 0} {}

DerefExpr::DerefExpr(std::unique_ptr<Expr> address, ViewSemantics result)
    : Expr(std::move(result)), address(std::move(address)),
      byteLength(this->result.staticByteLength) {}

BooleanTestExpr::BooleanTestExpr(std::unique_ptr<Expr> operand,
                                 ViewSemantics result)
    : Expr(std::move(result)), operand(std::move(operand)) {}

BinaryExpr::BinaryExpr(std::unique_ptr<Expr> left, std::string op,
                       std::unique_ptr<Expr> right, ViewSemantics result,
                       StandardOperationKind operationKind)
    : Expr(std::move(result)), left(std::move(left)), op(std::move(op)),
      right(std::move(right)), byteLength(this->result.staticByteLength),
      operationKind(operationKind), operation(binaryOperatorFor(this->op)) {
  if (this->op.starts_with('%')) {
    const auto marker = this->op.find_last_of("duf");
    if (marker != std::string::npos) {
      typedIntegerInterpretation = this->op[marker] == 'u'
                                       ? IntegerInterpretation::Unsigned
                                       : (this->op[marker] == 'd'
                                              ? IntegerInterpretation::Signed
                                              : IntegerInterpretation::None);
    }
  }
  if (this->operationKind == StandardOperationKind::AddressOffset) {
    const auto baseFacts = addressFactsFor(*this->left);
    addressFacts = AddressFacts{
        AddressOrigin::PointerDerived,
        baseFacts ? baseFacts->knownExtent : std::nullopt,
        baseFacts ? baseFacts->knownAlignment : std::nullopt, false};
  }
}

UnaryExpr::UnaryExpr(std::string op, std::unique_ptr<Expr> operand,
                     ViewSemantics result)
    : Expr(std::move(result)), op(std::move(op)), operand(std::move(operand)),
      byteLength(this->result.staticByteLength) {}

TernaryExpr::TernaryExpr(std::unique_ptr<Expr> condition,
                         std::unique_ptr<Expr> thenExpr,
                         std::unique_ptr<Expr> elseExpr, ViewSemantics result)
    : Expr(std::move(result)), condition(std::move(condition)),
      thenExpr(std::move(thenExpr)), elseExpr(std::move(elseExpr)),
      byteLength(this->result.staticByteLength) {}

UnsignedExpr::UnsignedExpr(std::unique_ptr<Expr> operand,
                           ViewSemantics result)
    : Expr(std::move(result)), operand(std::move(operand)),
      byteLength(this->result.staticByteLength) {}

IntegerCastExpr::IntegerCastExpr(std::unique_ptr<Expr> operand,
                                 bool isSigned, ViewSemantics result)
    : Expr(std::move(result)), operand(std::move(operand)),
      byteLength(this->result.staticByteLength), isSigned(isSigned) {}

TemplateViewExpr::TemplateViewExpr(std::unique_ptr<Expr> operand,
                                   std::string /*templateName*/,
                                   bool /*isAddressable*/, ViewSemantics result)
    : Expr(std::move(result)), operand(std::move(operand)),
      byteLength(this->result.staticByteLength),
      templateName(this->result.templateName),
      isAddressable(this->result.isAddressable) {}

UserTemplateOpCallExpr::UserTemplateOpCallExpr(
    std::string callee, std::vector<std::unique_ptr<Expr>> arguments,
    std::string templateName, ViewSemantics result)
    : Expr(std::move(result)), callee(std::move(callee)),
      arguments(std::move(arguments)), byteLength(this->result.staticByteLength),
      templateName(std::move(templateName)) {}

FloatBinaryExpr::FloatBinaryExpr(std::unique_ptr<Expr> left, std::string op,
                                 std::unique_ptr<Expr> right,
                                 ViewSemantics result)
    : Expr(std::move(result)), left(std::move(left)), op(std::move(op)),
      right(std::move(right)), byteLength(this->result.staticByteLength) {}

FloatCompareExpr::FloatCompareExpr(std::unique_ptr<Expr> left, std::string op,
                                   std::unique_ptr<Expr> right,
                                   std::size_t operandByteLength,
                                   ViewSemantics result)
    : Expr(std::move(result)), left(std::move(left)), op(std::move(op)),
      right(std::move(right)), operandByteLength(operandByteLength) {}

ToFloatExpr::ToFloatExpr(std::unique_ptr<Expr> operand, bool sourceUnsigned,
                         bool sourceIsFloating, ViewSemantics result)
    : Expr(std::move(result)), operand(std::move(operand)),
      byteLength(this->result.staticByteLength),
      sourceUnsigned(sourceUnsigned), sourceIsFloating(sourceIsFloating) {}

ToIntExpr::ToIntExpr(std::unique_ptr<Expr> operand, std::size_t floatByteLength,
                     bool isUnsigned, ViewSemantics result)
    : Expr(std::move(result)), operand(std::move(operand)),
      floatByteLength(floatByteLength), byteLength(this->result.staticByteLength),
      isUnsigned(isUnsigned) {}

CallExpr::CallExpr(std::string callee,
                   std::vector<std::unique_ptr<Expr>> arguments,
                   bool isFloating,
                   stdlib::BuiltinId builtin,
                   std::vector<FormatArgKind> formatArgumentKinds,
                   std::uint16_t overloadIndex, std::string templateName,
                   ViewSemantics result)
    : Expr(std::move(result)), callee(std::move(callee)),
      arguments(std::move(arguments)), byteLength(this->result.staticByteLength),
      isFloating(isFloating), builtin(builtin),
      provider(stdlib::builtinCallMetadata(builtin, overloadIndex).provider),
      returnMode(stdlib::builtinCallMetadata(builtin, overloadIndex).returnMode),
      overloadIndex(overloadIndex),
      formatArgumentKinds(std::move(formatArgumentKinds)),
      templateName(std::move(templateName)) {
  argumentPlans.reserve(this->arguments.size());
  for (const auto &argument : this->arguments) {
    argumentPlans.push_back(
        ConversionPlan{ConversionKind::Identity, argument->result, argument->result});
  }
  if (this->builtin == stdlib::BuiltinId::Alloc ||
      this->builtin == stdlib::BuiltinId::Calloc ||
      this->builtin == stdlib::BuiltinId::Realloc) {
    std::optional<std::size_t> extent;
    if (this->builtin == stdlib::BuiltinId::Alloc &&
        this->arguments.size() == 1U) {
      extent = staticAllocationExtent(*this->arguments[0]);
    } else if (this->builtin == stdlib::BuiltinId::Calloc &&
               this->arguments.size() == 2U) {
      const auto count = staticAllocationExtent(*this->arguments[0]);
      const auto size = staticAllocationExtent(*this->arguments[1]);
      if (count && size &&
          (*size == 0 || *count <=
                              std::numeric_limits<std::size_t>::max() / *size)) {
        extent = *count * *size;
      }
    } else if (this->builtin == stdlib::BuiltinId::Realloc &&
               this->arguments.size() == 2U) {
      extent = staticAllocationExtent(*this->arguments[1]);
    }
    addressFacts = AddressFacts{AddressOrigin::DynamicAllocation, extent,
                                std::nullopt, true};
  }
}

UserTemplateFormatCallExpr::UserTemplateFormatCallExpr(
    std::string callee, std::unique_ptr<Expr> value, FormatOutputSink sink,
    std::unique_ptr<Expr> file, ViewSemantics result)
    : Expr(std::move(result)), callee(std::move(callee)), value(std::move(value)),
      sink(sink), file(std::move(file)), byteLength(this->result.staticByteLength) {}

UserTemplateOpCall::UserTemplateOpCall(
    std::string callee, std::vector<std::unique_ptr<Expr>> arguments,
    std::size_t resultByteLength)
    : callee(std::move(callee)), arguments(std::move(arguments)),
      resultByteLength(resultByteLength) {}

UserTemplateFormatCall::UserTemplateFormatCall(
    std::string callee, std::unique_ptr<Expr> value, FormatOutputSink sink,
    std::unique_ptr<Expr> file, std::size_t resultByteLength)
    : callee(std::move(callee)), value(std::move(value)), sink(sink),
      file(std::move(file)), resultByteLength(resultByteLength) {}

DynamicByteViewExpr::DynamicByteViewExpr(
    DynamicByteViewOperation operation, std::unique_ptr<Expr> source,
    std::unique_ptr<Expr> runtimeLength, ViewSemantics result)
    : Expr(std::move(result)), operation(operation), source(std::move(source)),
      runtimeLength(std::move(runtimeLength)) {}

ByteSwapExpr::ByteSwapExpr(std::unique_ptr<Expr> source, ViewSemantics result)
    : Expr(std::move(result)), source(std::move(source)),
      byteLength(this->result.staticByteLength) {}

AssignmentExpr::AssignmentExpr(std::vector<std::unique_ptr<Stmt>> stores,
                               std::unique_ptr<Expr> result,
                               ViewSemantics semantics)
    : Expr(std::move(semantics)), stores(std::move(stores)),
      result(std::move(result)), byteLength(this->Expr::result.staticByteLength) {}

ViewCopyStore::ViewCopyStore(std::string target, std::string bindingName,
                             std::size_t targetByteLength,
                             MemoryStorage storage, std::size_t offset,
                             std::unique_ptr<Expr> value,
                             ConversionPlan conversionPlan)
    : target(std::move(target)), bindingName(std::move(bindingName)),
      targetByteLength(targetByteLength), storage(storage), offset(offset),
      value(std::move(value)), conversionPlan(std::move(conversionPlan)) {}

StatementList::StatementList(std::vector<std::unique_ptr<Stmt>> statements)
    : statements(std::move(statements)) {}

GlobalMemory::GlobalMemory(std::string name, std::string bindingName,
                           std::size_t byteLength)
    : name(std::move(name)), bindingName(std::move(bindingName)),
      range(activeSourceRange()), byteLength(byteLength) {}

GlobalMemory::GlobalMemory(std::string name, std::string bindingName,
                           std::size_t byteLength, bool isExtern)
    : name(std::move(name)), bindingName(std::move(bindingName)),
      range(activeSourceRange()), byteLength(byteLength), isExtern(isExtern) {}

StructMemberLayout::StructMemberLayout(std::string name,
                                       std::size_t byteLength,
                                       std::size_t offset)
    : name(std::move(name)), byteLength(byteLength), offset(offset) {}

StructLayout::StructLayout(std::string name,
                           std::vector<StructMemberLayout> members,
                           std::size_t byteLength)
    : name(std::move(name)), members(std::move(members)),
      byteLength(byteLength) {}

ViewMember::ViewMember(std::string name, std::size_t byteLength,
                       std::size_t offset, std::string templateName)
    : name(std::move(name)), byteLength(byteLength), offset(offset),
      templateName(std::move(templateName)) {}

ViewTemplate::ViewTemplate(std::string name, std::vector<ViewMember> members,
                           std::size_t byteLength)
    : name(std::move(name)), members(std::move(members)),
      byteLength(byteLength) {}

ImplOpParam::ImplOpParam(std::string name, std::string templateName,
                         bool isMutable)
    : name(std::move(name)), templateName(std::move(templateName)),
      isMutable(isMutable) {}

ImplOpBinding::ImplOpBinding(std::string implTemplate, std::string op,
                             std::string symbolName,
                             std::vector<ImplOpParam> params,
                             std::vector<std::size_t> returnByteLengths)
    : implTemplate(std::move(implTemplate)), op(std::move(op)),
      symbolName(std::move(symbolName)),
      params(std::move(params)),
      returnByteLengths(std::move(returnByteLengths)) {}

Parameter::Parameter(std::string name, std::string bindingName,
                     std::size_t byteLength)
    : name(std::move(name)), bindingName(std::move(bindingName)),
      range(activeSourceRange()), byteLength(byteLength),
      valueSemantics(staticViewSemantics(ViewCategory::RawBytes,
                                         IntegerInterpretation::RawOnly,
                                         byteLength)) {}

Parameter::Parameter(std::string name, std::string bindingName,
                     ViewSemantics valueSemantics)
    : name(std::move(name)), bindingName(std::move(bindingName)),
      range(activeSourceRange()), byteLength(valueSemantics.staticByteLength),
      valueSemantics(std::move(valueSemantics)) {}

ExternFunction::ExternFunction(std::string name,
                               std::vector<std::size_t> parameterByteLengths,
                               std::vector<std::size_t> returnByteLengths)
    : name(std::move(name)),
      parameterByteLengths(std::move(parameterByteLengths)),
      returnByteLengths(std::move(returnByteLengths)) {}

LocalMemory::LocalMemory(std::string name, std::size_t byteLength)
    : name(std::move(name)), bindingName(this->name), byteLength(byteLength) {}

LocalMemory::LocalMemory(std::string name, std::string bindingName,
                         std::size_t byteLength, MemoryStorage storage)
    : name(std::move(name)), bindingName(std::move(bindingName)),
      byteLength(byteLength), storage(storage) {}

LocalMemory::LocalMemory(std::string name, std::string bindingName,
                         std::size_t byteLength, MemoryStorage storage,
                         std::string templateName)
    : name(std::move(name)), bindingName(std::move(bindingName)),
      byteLength(byteLength), storage(storage),
      templateName(std::move(templateName)) {}

IntegerStore::IntegerStore(std::string target, std::size_t targetByteLength,
                           std::unique_ptr<Expr> value)
    : target(std::move(target)), bindingName(this->target),
      targetByteLength(targetByteLength), value(std::move(value)) {}

IntegerStore::IntegerStore(std::string target, std::string bindingName,
                           std::size_t targetByteLength, MemoryStorage storage,
                           std::unique_ptr<Expr> value)
    : target(std::move(target)), bindingName(std::move(bindingName)),
      targetByteLength(targetByteLength), storage(storage),
      value(std::move(value)) {}

IntegerStore::IntegerStore(std::string target, std::string bindingName,
                           std::size_t targetByteLength, MemoryStorage storage,
                           std::size_t offset, std::unique_ptr<Expr> value)
    : target(std::move(target)), bindingName(std::move(bindingName)),
      targetByteLength(targetByteLength), storage(storage), offset(offset),
      value(std::move(value)) {}

FloatStore::FloatStore(std::string target, std::string bindingName,
                       std::size_t targetByteLength, MemoryStorage storage,
                       std::unique_ptr<Expr> value)
    : target(std::move(target)), bindingName(std::move(bindingName)),
      targetByteLength(targetByteLength), storage(storage),
      value(std::move(value)) {}

FloatStore::FloatStore(std::string target, std::string bindingName,
                       std::size_t targetByteLength, MemoryStorage storage,
                       std::size_t offset, std::unique_ptr<Expr> value)
    : target(std::move(target)), bindingName(std::move(bindingName)),
      targetByteLength(targetByteLength), storage(storage), offset(offset),
      value(std::move(value)) {}

StringStore::StringStore(std::string target, std::string bindingName,
                         std::size_t targetByteLength, MemoryStorage storage,
                         std::string value)
    : target(std::move(target)), bindingName(std::move(bindingName)),
      targetByteLength(targetByteLength), storage(storage),
      value(std::move(value)) {}

StringStore::StringStore(std::string target, std::string bindingName,
                         std::size_t targetByteLength, MemoryStorage storage,
                         std::size_t offset, std::string value)
    : target(std::move(target)), bindingName(std::move(bindingName)),
      targetByteLength(targetByteLength), storage(storage), offset(offset),
      value(std::move(value)) {}

StringCopyStore::StringCopyStore(std::string target, std::string bindingName,
                                 std::size_t targetByteLength,
                                 MemoryStorage targetStorage,
                                 std::size_t targetOffset,
                                 std::string source,
                                 std::string sourceBindingName,
                                 std::size_t sourceByteLength,
                                 MemoryStorage sourceStorage,
                                 std::size_t sourceOffset)
    : target(std::move(target)), bindingName(std::move(bindingName)),
      targetByteLength(targetByteLength), targetStorage(targetStorage),
      targetOffset(targetOffset),
      source(std::move(source)),
      sourceBindingName(std::move(sourceBindingName)),
      sourceByteLength(sourceByteLength), sourceStorage(sourceStorage),
      sourceOffset(sourceOffset) {}

BoolStore::BoolStore(std::string target, std::string bindingName,
                     std::size_t targetByteLength, MemoryStorage storage,
                     std::unique_ptr<Expr> value)
    : target(std::move(target)), bindingName(std::move(bindingName)),
      targetByteLength(targetByteLength), storage(storage),
      value(std::move(value)) {}

BoolStore::BoolStore(std::string target, std::string bindingName,
                     std::size_t targetByteLength, MemoryStorage storage,
                     std::size_t offset, std::unique_ptr<Expr> value)
    : target(std::move(target)), bindingName(std::move(bindingName)),
      targetByteLength(targetByteLength), storage(storage), offset(offset),
      value(std::move(value)) {}

PointerStore::PointerStore(std::unique_ptr<Expr> address,
                           std::size_t targetByteLength,
                           std::unique_ptr<Expr> value)
    : address(std::move(address)), targetByteLength(targetByteLength),
      value(std::move(value)) {}

Call::Call(std::string callee, std::vector<std::unique_ptr<Expr>> arguments,
           stdlib::BuiltinId builtin,
           std::vector<FormatArgKind> formatArgumentKinds,
           std::uint16_t overloadIndex)
    : callee(std::move(callee)), arguments(std::move(arguments)),
      builtin(builtin),
      provider(stdlib::builtinCallMetadata(builtin, overloadIndex).provider),
      returnMode(stdlib::builtinCallMetadata(builtin, overloadIndex).returnMode),
      overloadIndex(overloadIndex),
      formatArgumentKinds(std::move(formatArgumentKinds)) {}

MultiReturnCallStore::Target::Target(std::string name, std::string bindingName,
                                     std::size_t byteLength,
                                     MemoryStorage storage,
                                     std::size_t returnIndex)
    : name(std::move(name)), bindingName(std::move(bindingName)),
      byteLength(byteLength), storage(storage), returnIndex(returnIndex) {}

MultiReturnCallStore::MultiReturnCallStore(
    std::string callee, std::vector<std::unique_ptr<Expr>> arguments,
    std::vector<Target> targets)
    : callee(std::move(callee)), arguments(std::move(arguments)),
      targets(std::move(targets)) {}

InputCallStore::Target::Target(std::string name, std::string bindingName,
                               std::size_t byteLength, MemoryStorage storage,
                               std::size_t offset, std::string templateName)
    : name(std::move(name)), bindingName(std::move(bindingName)),
      byteLength(byteLength), storage(storage), offset(offset),
      templateName(std::move(templateName)) {}

InputCallStore::InputCallStore(std::string callee, std::unique_ptr<Expr> file,
                               std::unique_ptr<Expr> format,
                               std::vector<Target> countTargets,
                               std::vector<Target> scanTargets,
                               stdlib::BuiltinId builtin)
    : callee(std::move(callee)), file(std::move(file)),
      format(std::move(format)), countTargets(std::move(countTargets)),
      scanTargets(std::move(scanTargets)), builtin(builtin) {}

Return::Return(std::vector<std::unique_ptr<Expr>> values,
               std::vector<ConversionPlan> conversionPlans)
    : values(std::move(values)), conversionPlans(std::move(conversionPlans)) {}

If::If(std::unique_ptr<Expr> condition, std::unique_ptr<Block> thenBlock,
       std::unique_ptr<Block> elseBlock)
    : condition(std::move(condition)), thenBlock(std::move(thenBlock)),
      elseBlock(std::move(elseBlock)) {}

While::While(std::unique_ptr<Expr> condition, std::unique_ptr<Block> body)
    : condition(std::move(condition)), body(std::move(body)) {}

For::For(std::unique_ptr<Stmt> init, std::unique_ptr<Expr> condition,
         std::vector<std::unique_ptr<Stmt>> post, std::unique_ptr<Block> body)
    : init(std::move(init)), condition(std::move(condition)),
      post(std::move(post)), body(std::move(body)) {}

Goto::Goto(std::string label) : label(std::move(label)) {}

Label::Label(std::string label, std::unique_ptr<Stmt> statement)
    : label(std::move(label)), statement(std::move(statement)) {}

Throw::Throw(std::unique_ptr<Stmt> delivery, std::string sourceTemplateName,
             std::size_t sourceByteLength, std::string targetTemplateName,
             std::size_t targetByteLength)
    : delivery(std::move(delivery)),
      sourceTemplateName(std::move(sourceTemplateName)),
      sourceByteLength(sourceByteLength),
      targetTemplateName(std::move(targetTemplateName)),
      targetByteLength(targetByteLength) {}

TryCatch::TryCatch(std::unique_ptr<Block> tryBlock, std::string errorName,
                   std::string errorBindingName,
                   std::string errorTemplateName,
                   std::size_t errorByteLength,
                   std::unique_ptr<Block> catchBlock)
    : tryBlock(std::move(tryBlock)), errorName(std::move(errorName)),
      errorBindingName(std::move(errorBindingName)),
      errorTemplateName(std::move(errorTemplateName)),
      errorByteLength(errorByteLength), catchBlock(std::move(catchBlock)) {}

Block::Block(std::vector<std::unique_ptr<Stmt>> statements)
    : statements(std::move(statements)), range(activeSourceRange()) {}

Function::Function(std::string name, std::unique_ptr<Block> body)
    : name(std::move(name)), range(activeSourceRange()), body(std::move(body)) {}

Function::Function(std::string name, std::vector<Parameter> parameters,
                   std::vector<std::size_t> returnByteLengths,
                   std::unique_ptr<Block> body)
    : name(std::move(name)), range(activeSourceRange()),
      parameters(std::move(parameters)),
      returnByteLengths(std::move(returnByteLengths)), body(std::move(body)) {}

TranslationUnit::TranslationUnit(
    std::vector<std::unique_ptr<Function>> functions)
    : functions(std::move(functions)) {}

TranslationUnit::TranslationUnit(
    std::vector<GlobalMemory> globals,
    std::vector<std::unique_ptr<Function>> functions)
    : globals(std::move(globals)), functions(std::move(functions)) {}

TranslationUnit::TranslationUnit(
    std::vector<GlobalMemory> globals, std::vector<ExternFunction> externFunctions,
    std::vector<std::unique_ptr<Function>> functions)
    : globals(std::move(globals)), externFunctions(std::move(externFunctions)),
      functions(std::move(functions)) {}

TranslationUnit::TranslationUnit(
    std::vector<GlobalMemory> globals, std::vector<StructLayout> structs,
    std::vector<ExternFunction> externFunctions,
    std::vector<std::unique_ptr<Function>> functions)
    : globals(std::move(globals)), structs(std::move(structs)),
      externFunctions(std::move(externFunctions)), functions(std::move(functions)) {}

TranslationUnit::TranslationUnit(
    std::vector<GlobalMemory> globals, std::vector<StructLayout> structs,
    std::vector<ViewTemplate> viewTemplates, std::vector<ImplOpBinding> implOps,
    std::vector<ExternFunction> externFunctions,
    std::vector<std::unique_ptr<Function>> functions)
    : globals(std::move(globals)), structs(std::move(structs)),
      viewTemplates(std::move(viewTemplates)), implOps(std::move(implOps)),
      externFunctions(std::move(externFunctions)),
      functions(std::move(functions)) {}

TranslationUnit::TranslationUnit(
    std::vector<GlobalMemory> globals, std::vector<StructLayout> structs,
    std::vector<ViewTemplate> viewTemplates, std::vector<ImplOpBinding> implOps,
    std::vector<ExternFunction> externFunctions,
    std::vector<std::unique_ptr<Function>> functions,
    std::unique_ptr<Block> globalInit)
    : globals(std::move(globals)), structs(std::move(structs)),
      viewTemplates(std::move(viewTemplates)), implOps(std::move(implOps)),
      externFunctions(std::move(externFunctions)), functions(std::move(functions)),
      globalInit(std::move(globalInit)) {}

std::vector<diagnostic::Diagnostic>
applyLinkageOverrides(TranslationUnit &unit,
                      const std::vector<LinkageOverride> &overrides) {
  struct PendingOverride {
    Linkage *target = nullptr;
    Linkage linkage = Linkage::External;
  };

  std::vector<diagnostic::Diagnostic> diagnostics;
  std::vector<PendingOverride> pending;
  std::unordered_map<std::string, Linkage> seen;

  const auto targetName = [](LinkageTarget target) -> std::string_view {
    switch (target) {
    case LinkageTarget::Global:
      return "global";
    case LinkageTarget::Function:
      return "function";
    }
    return "symbol";
  };
  const auto keyFor = [](LinkageTarget target,
                         std::string_view symbolName) -> std::string {
    return std::string(target == LinkageTarget::Global ? "global:" :
                                                        "function:") +
           std::string(symbolName);
  };
  const auto addDiagnostic = [&diagnostics](std::string message) {
    diagnostics.push_back(diagnostic::Diagnostic::error(
        diagnostic::Stage::Hir, std::move(message)));
  };

  for (const auto &override : overrides) {
    if (override.symbolName.empty()) {
      addDiagnostic("linkage override has an empty symbol name");
      continue;
    }

    const auto key = keyFor(override.target, override.symbolName);
    if (!seen.emplace(key, override.linkage).second) {
      addDiagnostic("duplicate linkage override for " +
                    std::string(targetName(override.target)) + " '" +
                    override.symbolName + "'");
      continue;
    }

    if (override.target == LinkageTarget::Global) {
      const auto found = std::find_if(
          unit.globals.begin(), unit.globals.end(), [&override](const auto &global) {
            return global.bindingName == override.symbolName;
          });
      if (found == unit.globals.end()) {
        addDiagnostic("linkage override references unknown global '" +
                      override.symbolName + "'");
        continue;
      }
      if (found->isExtern) {
        addDiagnostic("linkage override cannot target extern global declaration '" +
                      override.symbolName + "'");
        continue;
      }
      pending.push_back(PendingOverride{&found->linkage, override.linkage});
      continue;
    }

    const auto found = std::find_if(
        unit.functions.begin(), unit.functions.end(), [&override](const auto &function) {
          return function != nullptr && function->name == override.symbolName;
        });
    if (found != unit.functions.end()) {
      pending.push_back(PendingOverride{&(*found)->linkage, override.linkage});
      continue;
    }

    const auto externFound = std::find_if(
        unit.externFunctions.begin(), unit.externFunctions.end(),
        [&override](const auto &function) {
          return function.name == override.symbolName;
        });
    if (externFound != unit.externFunctions.end()) {
      addDiagnostic("linkage override cannot target extern function declaration '" +
                    override.symbolName + "'");
      continue;
    }
    addDiagnostic("linkage override references unknown function '" +
                  override.symbolName + "'");
  }

  if (!diagnostics.empty()) {
    return diagnostics;
  }

  for (const auto &override : pending) {
    *override.target = override.linkage;
  }
  return {};
}

std::vector<diagnostic::Diagnostic>
applyAbiOverrides(TranslationUnit &unit,
                  const std::vector<AbiOverride> &overrides) {
  struct PendingOverride {
    std::optional<AbiType> *objectTarget = nullptr;
    std::optional<FunctionAbiSignature> *functionTarget = nullptr;
    std::optional<AbiType> objectType;
    std::optional<FunctionAbiSignature> functionSignature;
  };

  const auto addDiagnostic = [](std::vector<diagnostic::Diagnostic> &diagnostics,
                                std::string message) {
    diagnostics.push_back(diagnostic::Diagnostic::error(
        diagnostic::Stage::Hir, std::move(message)));
  };
  const auto targetName = [](LinkageTarget target) -> std::string_view {
    return target == LinkageTarget::Global ? "global" : "function";
  };
  const auto keyFor = [](LinkageTarget target,
                         std::string_view symbolName) -> std::string {
    return std::string(target == LinkageTarget::Global ? "global:" :
                                                        "function:") +
           std::string(symbolName);
  };
  const auto storageByteLength = [](const AbiType &type)
      -> std::optional<std::size_t> {
    if (type.byteLength == 0 || type.elementCount == 0 ||
        type.byteLength > std::numeric_limits<std::size_t>::max() /
                              type.elementCount) {
      return std::nullopt;
    }
    return type.byteLength * type.elementCount;
  };
  std::function<bool(const AbiType &)> isValidType;
  isValidType = [&isValidType, &storageByteLength](const AbiType &type) {
    if (!storageByteLength(type) || type.alignment == 0) {
      return false;
    }
    switch (type.kind) {
    case AbiValueKind::Integer:
      return type.byteLength == 1 || type.byteLength == 2 ||
             type.byteLength == 4 || type.byteLength == 8;
    case AbiValueKind::Floating:
      // The minimal C compatibility surface has float and double only.
      return type.byteLength == 4 || type.byteLength == 8;
    case AbiValueKind::Pointer:
      return type.byteLength == sizeof(void *);
    case AbiValueKind::Aggregate: {
      if (type.aggregateName.empty() || type.aggregateFields.empty() ||
          type.aggregateFields.size() != type.aggregateFieldOffsets.size()) {
        return false;
      }
      std::size_t previousEnd = 0;
      for (std::size_t index = 0; index < type.aggregateFields.size(); ++index) {
        const auto& field = type.aggregateFields[index];
        const auto fieldLength = storageByteLength(field);
        const auto offset = type.aggregateFieldOffsets[index];
        if (!fieldLength || !isValidType(field) || offset < previousEnd ||
            offset > type.byteLength || *fieldLength > type.byteLength - offset) {
          return false;
        }
        previousEnd = offset + *fieldLength;
      }
      return true;
    }
    }
    return false;
  };
  const auto validateSignature = [&](const FunctionAbiSignature &signature,
                                     const std::vector<std::size_t> &parameters,
                                     const std::vector<std::size_t> &returns,
                                     std::string_view name,
                                     std::vector<diagnostic::Diagnostic> &diagnostics) {
    bool valid = true;
    if (signature.parameterTypes.size() != parameters.size()) {
      addDiagnostic(diagnostics, "ABI override parameter count does not match function '" +
                                     std::string(name) + "'");
      valid = false;
    }
    if (signature.returnTypes.size() != returns.size()) {
      addDiagnostic(diagnostics, "ABI override return count does not match function '" +
                                     std::string(name) + "'");
      valid = false;
    }
    const auto validateTypes = [&](const std::vector<AbiType> &types,
                                   const std::vector<std::size_t> &lengths,
                                   std::string_view role) {
      const auto count = std::min(types.size(), lengths.size());
      for (std::size_t index = 0; index < count; ++index) {
        const auto &type = types[index];
        if (!isValidType(type)) {
          addDiagnostic(diagnostics, "ABI override has unsupported " +
                                         std::string(toString(type.kind)) + " " +
                                         std::string(role) + " type for function '" +
                                         std::string(name) + "'");
          valid = false;
        } else if (*storageByteLength(type) != lengths[index]) {
          addDiagnostic(diagnostics, "ABI override " + std::string(role) +
                                         " byte length does not match function '" +
                                         std::string(name) + "'");
          valid = false;
        }
      }
    };
    validateTypes(signature.parameterTypes, parameters, "parameter");
    validateTypes(signature.returnTypes, returns, "return");
    return valid;
  };

  std::vector<diagnostic::Diagnostic> diagnostics;
  std::vector<PendingOverride> pending;
  std::unordered_map<std::string, bool> seen;

  for (const auto &override : overrides) {
    if (override.symbolName.empty()) {
      addDiagnostic(diagnostics, "ABI override has an empty symbol name");
      continue;
    }
    const auto key = keyFor(override.target, override.symbolName);
    if (!seen.emplace(key, true).second) {
      addDiagnostic(diagnostics, "duplicate ABI override for " +
                                     std::string(targetName(override.target)) + " '" +
                                     override.symbolName + "'");
      continue;
    }

    if (override.target == LinkageTarget::Global) {
      if (!override.objectType || override.functionSignature) {
        addDiagnostic(diagnostics, "global ABI override for '" + override.symbolName +
                                       "' must provide exactly one object type");
        continue;
      }
      if (!isValidType(*override.objectType)) {
        addDiagnostic(diagnostics, "ABI override has unsupported " +
                                       std::string(toString(override.objectType->kind)) +
                                       " object type for global '" +
                                       override.symbolName + "'");
        continue;
      }
      const auto found = std::find_if(
          unit.globals.begin(), unit.globals.end(), [&override](const auto &global) {
            return global.bindingName == override.symbolName;
          });
      if (found == unit.globals.end()) {
        addDiagnostic(diagnostics, "ABI override references unknown global '" +
                                       override.symbolName + "'");
        continue;
      }
      const auto objectLength = storageByteLength(*override.objectType);
      if (!objectLength || found->byteLength != *objectLength) {
        addDiagnostic(diagnostics, "ABI override object byte length does not match global '" +
                                       override.symbolName + "'");
        continue;
      }
      pending.push_back(PendingOverride{&found->abiType, nullptr,
                                        override.objectType, std::nullopt});
      continue;
    }

    if (override.objectType || !override.functionSignature) {
      addDiagnostic(diagnostics, "function ABI override for '" + override.symbolName +
                                     "' must provide exactly one function signature");
      continue;
    }

    const auto definition = std::find_if(
        unit.functions.begin(), unit.functions.end(), [&override](const auto &function) {
          return function != nullptr && function->name == override.symbolName;
        });
    if (definition != unit.functions.end()) {
      std::vector<std::size_t> parameters;
      parameters.reserve((*definition)->parameters.size());
      for (const auto &parameter : (*definition)->parameters) {
        parameters.push_back(parameter.byteLength);
      }
      if (validateSignature(*override.functionSignature, parameters,
                            (*definition)->returnByteLengths, override.symbolName,
                            diagnostics)) {
        pending.push_back(PendingOverride{nullptr, &(*definition)->abiSignature,
                                          std::nullopt,
                                          override.functionSignature});
      }
      continue;
    }

    const auto declaration = std::find_if(
        unit.externFunctions.begin(), unit.externFunctions.end(),
        [&override](const auto &function) {
          return function.name == override.symbolName;
        });
    if (declaration == unit.externFunctions.end()) {
      addDiagnostic(diagnostics, "ABI override references unknown function '" +
                                     override.symbolName + "'");
      continue;
    }
    if (validateSignature(*override.functionSignature,
                          declaration->parameterByteLengths,
                          declaration->returnByteLengths, override.symbolName,
                          diagnostics)) {
      pending.push_back(PendingOverride{nullptr, &declaration->abiSignature,
                                        std::nullopt,
                                        override.functionSignature});
    }
  }

  if (!diagnostics.empty()) {
    return diagnostics;
  }
  for (const auto &override : pending) {
    if (override.objectTarget != nullptr) {
      *override.objectTarget = override.objectType;
    }
    if (override.functionTarget != nullptr) {
      *override.functionTarget = override.functionSignature;
    }
  }
  return {};
}

std::vector<diagnostic::Diagnostic>
verifyHIR(const TranslationUnit &unit) {
  std::vector<diagnostic::Diagnostic> diagnostics;

  const auto addDiagnostic = [&diagnostics](const Expr &expression,
                                            std::string message) {
    auto diagnostic = diagnostic::Diagnostic::error(diagnostic::Stage::Hir,
                                                     std::move(message));
    diagnostic.range = expression.range;
    diagnostics.push_back(std::move(diagnostic));
  };
  std::unordered_map<std::string, const Function *> functionContracts;
  for (const auto &function : unit.functions) {
    if (function) {
      functionContracts.emplace(function->name, function.get());
    }
  }
  const auto checkTemplate = [](const ViewSemantics &semantics,
                                std::string_view expected) {
    return semantics.templateName == expected;
  };
  const auto sameSemantics = [](const ViewSemantics &left,
                                const ViewSemantics &right) {
    return left.category == right.category &&
           left.integerInterpretation == right.integerInterpretation &&
           left.lengthKind == right.lengthKind &&
           left.staticByteLength == right.staticByteLength &&
           left.templateName == right.templateName &&
           left.isAddressable == right.isAddressable &&
           left.isMutableLValue == right.isMutableLValue;
  };
  const auto sameValueSemantics = [](const ViewSemantics &left,
                                     const ViewSemantics &right) {
    return left.category == right.category &&
           left.integerInterpretation == right.integerInterpretation &&
           left.lengthKind == right.lengthKind &&
           left.staticByteLength == right.staticByteLength &&
           left.templateName == right.templateName;
  };
  const auto checkSemantics = [&addDiagnostic, &checkTemplate](
                                  const Expr &expression) {
    const auto &semantics = expression.result;
    if (semantics.lengthKind == ViewLengthKind::Static &&
        semantics.staticByteLength == 0) {
      addDiagnostic(expression,
                    "expression result has a static View with zero byte length");
    }
    if (semantics.lengthKind == ViewLengthKind::Dynamic &&
        semantics.staticByteLength != 0) {
      addDiagnostic(expression,
                    "expression result has a dynamic View with a static byte length");
    }
    if (semantics.isMutableLValue && !semantics.isAddressable) {
      addDiagnostic(expression,
                    "expression result is mutable but not addressable");
    }

    const auto requireInterpretation =
        [&addDiagnostic, &expression, &semantics](
            IntegerInterpretation expected) {
          if (semantics.integerInterpretation != expected) {
            addDiagnostic(expression,
                          "expression result has a category/interpretation mismatch");
          }
        };
    const auto requireTemplate = [&addDiagnostic, &expression, &semantics,
                                  &checkTemplate](std::string_view expected) {
      if (!checkTemplate(semantics, expected)) {
        addDiagnostic(expression,
                      "expression result has a category/template mismatch");
      }
    };

    switch (semantics.category) {
    case ViewCategory::SignedInteger:
      requireInterpretation(IntegerInterpretation::Signed);
      if (semantics.templateName != "i8" && semantics.templateName != "i16" &&
          semantics.templateName != "i32" && semantics.templateName != "i64") {
        addDiagnostic(expression,
                      "signed integer result must retain an iN template name");
      }
      break;
    case ViewCategory::UnsignedInteger:
      requireInterpretation(IntegerInterpretation::Unsigned);
      if (semantics.templateName != "u8" && semantics.templateName != "u16" &&
          semantics.templateName != "u32" && semantics.templateName != "u64") {
        addDiagnostic(expression,
                      "unsigned integer result must retain a uN template name");
      }
      break;
    case ViewCategory::UntemplatedInteger:
      if (semantics.integerInterpretation != IntegerInterpretation::Signed &&
          semantics.integerInterpretation != IntegerInterpretation::Unsigned) {
        addDiagnostic(expression,
                      "untemplated integer result must be signed or unsigned");
      }
      if (!semantics.templateName.empty()) {
        addDiagnostic(expression,
                      "untemplated integer result must not retain a template name");
      }
      break;
    case ViewCategory::Floating:
      requireInterpretation(IntegerInterpretation::None);
      if (semantics.templateName != "f16" && semantics.templateName != "f32" &&
          semantics.templateName != "f64" && semantics.templateName != "f128") {
        addDiagnostic(expression,
                      "floating result must retain an fN template name");
      }
      break;
    case ViewCategory::Boolean:
      requireInterpretation(IntegerInterpretation::None);
      requireTemplate("bool");
      if (semantics.lengthKind != ViewLengthKind::Static ||
          semantics.staticByteLength != 1) {
        addDiagnostic(expression, "boolean result must be a one-byte bool View");
      }
      break;
    case ViewCategory::Address:
      requireInterpretation(IntegerInterpretation::None);
      requireTemplate("addr");
      break;
    case ViewCategory::Handle:
      requireInterpretation(IntegerInterpretation::None);
      requireTemplate("handle");
      break;
    case ViewCategory::Bytes:
      requireInterpretation(IntegerInterpretation::None);
      requireTemplate("bytes");
      break;
    case ViewCategory::CString:
      requireInterpretation(IntegerInterpretation::None);
      requireTemplate("cstr");
      break;
    case ViewCategory::UserTemplate:
      requireInterpretation(IntegerInterpretation::None);
      if (semantics.templateName.empty()) {
        addDiagnostic(expression,
                      "user-template result must retain its template name");
      }
      break;
    case ViewCategory::RawBytes:
      requireInterpretation(IntegerInterpretation::RawOnly);
      if (!semantics.templateName.empty()) {
        addDiagnostic(expression,
                      "raw-byte result must not retain a template name");
      }
      break;
    }

    if (const auto expectedLength =
            standardTemplateByteLength(semantics.templateName);
        expectedLength &&
        (semantics.lengthKind != ViewLengthKind::Static ||
         semantics.staticByteLength != *expectedLength)) {
      addDiagnostic(expression,
                    "standard template '" + semantics.templateName +
                        "' does not match its required byte length");
    }
  };
  const auto checkByteLength = [&addDiagnostic](const Expr &expression,
                                                std::size_t byteLength,
                                                std::string_view field) {
    if (expression.result.lengthKind != ViewLengthKind::Static ||
        byteLength != expression.result.staticByteLength) {
      addDiagnostic(expression, "legacy " + std::string(field) +
                                    " does not match result ViewSemantics");
    }
  };
  const auto checkTemplateName = [&addDiagnostic](const Expr &expression,
                                                   std::string_view templateName,
                                                   std::string_view field) {
    if (templateName != expression.result.templateName) {
      addDiagnostic(expression, "legacy " + std::string(field) +
                                    " does not match result ViewSemantics");
    }
  };
  const auto checkAddressFacts = [&addDiagnostic](
                                     const Expr &expression,
                                     const std::optional<AddressFacts> &facts) {
    if (!facts) {
      return;
    }
    if (!isAddressValue(expression.result)) {
      addDiagnostic(expression,
                    "AddressFacts require an addr result View");
    }
    if (facts->origin == AddressOrigin::DynamicAllocation &&
        !facts->isBaseAddress) {
      addDiagnostic(expression,
                    "dynamic-allocation AddressFacts must describe a base address");
    }
  };
  const auto checkConversionPlan = [&addDiagnostic, &sameValueSemantics](
                                       const Expr &expression,
                                       const ConversionPlan &plan,
                                       const ViewSemantics &source,
                                       std::string_view owner) {
    if (!sameValueSemantics(plan.source, source)) {
      addDiagnostic(expression, std::string(owner) +
                                    " conversion plan source does not match its operand");
    }
    if (plan.destination.lengthKind != ViewLengthKind::Static ||
        plan.destination.staticByteLength == 0) {
      addDiagnostic(expression, std::string(owner) +
                                    " conversion plan destination must be a fixed View");
    }
    if (plan.kind == ConversionKind::Identity &&
        !sameValueSemantics(plan.source, plan.destination)) {
      addDiagnostic(expression, std::string(owner) +
                                    " identity conversion plan changes View semantics");
    }
    if (plan.kind == ConversionKind::Floating &&
        (!isFloatingNumeric(plan.destination) ||
         (!isFloatingNumeric(plan.source) && !isIntegerNumeric(plan.source)))) {
      addDiagnostic(expression, std::string(owner) +
                                    " floating conversion has incompatible Views");
    }
    if ((plan.kind == ConversionKind::ByteCopy ||
         plan.kind == ConversionKind::UserTemplateAssignment) &&
        plan.source.lengthKind == ViewLengthKind::Static &&
        plan.source.staticByteLength != plan.destination.staticByteLength) {
      addDiagnostic(expression, std::string(owner) +
                                    " byte-copy conversion requires equal fixed lengths");
    }
  };

  std::function<void(const Expr &)> verifyExpr;
  std::function<void(const Stmt &)> verifyStmt;
  std::function<void(const Block &)> verifyBlock;

  verifyExpr = [&](const Expr &expression) {
    checkSemantics(expression);
    if (const auto *value = dynamic_cast<const IntegerLiteral *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      return;
    }
    if (const auto *value = dynamic_cast<const CharacterLiteral *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      if (value->bytes.size() != value->byteLength) {
        addDiagnostic(*value,
                      "CharacterLiteral byte count does not match result View");
      }
      if (value->byteLength == 1U) {
        if (value->result.integerInterpretation !=
            IntegerInterpretation::Unsigned) {
          addDiagnostic(*value,
                        "single-byte CharacterLiteral must be unsigned");
        }
      } else if (!isRawOnly(value->result)) {
        addDiagnostic(*value,
                      "multi-byte CharacterLiteral must be raw-only");
      }
      return;
    }
    if (const auto *value = dynamic_cast<const StringLiteral *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      return;
    }
    if (const auto *value = dynamic_cast<const FloatLiteral *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      return;
    }
    if (const auto *value = dynamic_cast<const VariableRef *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      checkTemplateName(*value, value->templateName, "templateName");
      checkAddressFacts(*value, value->addressFacts);
      return;
    }
    if (const auto *value = dynamic_cast<const AddressOfExpr *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      checkAddressFacts(*value, value->facts);
      const auto expectedOrigin = value->storage == MemoryStorage::Local
                                      ? AddressOrigin::LocalObject
                                      : (value->storage == MemoryStorage::StaticLocal
                                             ? AddressOrigin::StaticObject
                                             : AddressOrigin::GlobalObject);
      if (value->facts.origin != expectedOrigin ||
          value->facts.knownExtent != value->targetByteLength ||
          value->facts.isBaseAddress != (value->offset == 0)) {
        addDiagnostic(*value,
                      "AddressOfExpr AddressFacts do not match its storage range");
      }
      return;
    }
    if (const auto *value = dynamic_cast<const DerefExpr *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      verifyExpr(*value->address);
      return;
    }
    if (const auto *value = dynamic_cast<const BooleanTestExpr *>(&expression)) {
      if (!sameSemantics(value->result, booleanTestResultSemantics())) {
        addDiagnostic(*value,
                      "BooleanTestExpr result must be the canonical bool View");
      }
      verifyExpr(*value->operand);
      return;
    }
    if (const auto *value = dynamic_cast<const BinaryExpr *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      if (value->operation == BinaryOperator::Unknown) {
        addDiagnostic(*value,
                      "BinaryExpr must carry a resolved binary operation");
      }
      const bool hasCanonicalBooleanResult =
          sameSemantics(value->result, booleanTestResultSemantics());
      if (isBooleanResultOperation(value->operation) &&
          !hasCanonicalBooleanResult) {
        addDiagnostic(*value,
                      "comparison and logical BinaryExpr results must be the canonical bool View");
      }

      const auto isIntegerOrBoolean = [](const ViewSemantics &semantics) {
        return isIntegerNumeric(semantics) ||
               semantics.category == ViewCategory::Boolean;
      };
      const auto isUntemplatedInteger = [](const ViewSemantics &semantics) {
        return isIntegerNumeric(semantics) && semantics.templateName.empty();
      };
      const auto requireBooleanResult = [&]() {
        if (!hasCanonicalBooleanResult &&
            !isBooleanResultOperation(value->operation)) {
          addDiagnostic(*value,
                        "resolved comparison candidate must return the canonical bool View");
        }
      };

      switch (value->operationKind) {
      case StandardOperationKind::StandardInteger: {
        if (!isIntegerOrBoolean(value->left->result) ||
            !isIntegerOrBoolean(value->right->result)) {
          addDiagnostic(*value,
                        "StandardInteger candidate requires integer or bool operands");
          break;
        }
        const auto *selected =
            (value->left->result.category == ViewCategory::SignedInteger ||
             value->left->result.category == ViewCategory::UnsignedInteger)
                ? &value->left->result
                : ((value->right->result.category == ViewCategory::SignedInteger ||
                    value->right->result.category == ViewCategory::UnsignedInteger)
                       ? &value->right->result
                       : nullptr);
        if (selected == nullptr) {
          addDiagnostic(*value,
                        "StandardInteger candidate must retain a standard integer operand");
        } else if (isComparisonOperation(value->operation)) {
          requireBooleanResult();
        } else if (!sameValueSemantics(value->result, *selected)) {
          addDiagnostic(*value,
                        "StandardInteger candidate result does not match its selected operand View");
        }
        break;
      }
      case StandardOperationKind::UntemplatedInteger:
        if ((!isIntegerNumeric(value->left->result) &&
             !isAddressValue(value->left->result)) ||
            (!isIntegerNumeric(value->right->result) &&
             !isAddressValue(value->right->result))) {
          addDiagnostic(*value,
                        "UntemplatedInteger candidate requires integer operands");
        }
        if (isComparisonOperation(value->operation)) {
          requireBooleanResult();
        }
        break;
      case StandardOperationKind::StandardBoolean:
        if (value->left->result.category != ViewCategory::Boolean ||
            value->right->result.category != ViewCategory::Boolean ||
            (value->operation != BinaryOperator::Equal &&
             value->operation != BinaryOperator::NotEqual)) {
          addDiagnostic(*value,
                        "StandardBoolean candidate only permits bool == and != bool");
        }
        requireBooleanResult();
        break;
      case StandardOperationKind::StandardAddress: {
        const auto validAddressPair =
            (isAddressValue(value->left->result) &&
             (isAddressValue(value->right->result) ||
              isUntemplatedInteger(value->right->result))) ||
            (isAddressValue(value->right->result) &&
             isUntemplatedInteger(value->left->result));
        if (!validAddressPair ||
            (value->operation != BinaryOperator::Equal &&
             value->operation != BinaryOperator::NotEqual)) {
          addDiagnostic(*value,
                        "StandardAddress candidate only permits addr == or != a compatible addr/integer View");
        }
        requireBooleanResult();
        break;
      }
      case StandardOperationKind::StandardHandle:
        if (value->left->result.category != ViewCategory::Handle ||
            value->right->result.category != ViewCategory::Handle ||
            (value->operation != BinaryOperator::Equal &&
             value->operation != BinaryOperator::NotEqual)) {
          addDiagnostic(*value,
                        "StandardHandle candidate only permits handle == and != handle");
        }
        requireBooleanResult();
        break;
      case StandardOperationKind::StandardBytesCompare:
        if (value->left->result.category != ViewCategory::Bytes ||
            value->right->result.category != ViewCategory::Bytes ||
            !isComparisonOperation(value->operation)) {
          addDiagnostic(*value,
                        "StandardBytesCompare candidate requires bytes comparison operands");
        }
        requireBooleanResult();
        break;
      case StandardOperationKind::StandardCStringCompare:
        if (value->left->result.category != ViewCategory::CString ||
            value->right->result.category != ViewCategory::CString ||
            !isComparisonOperation(value->operation)) {
          addDiagnostic(*value,
                        "StandardCStringCompare candidate requires cstr comparison operands");
        }
        requireBooleanResult();
        break;
      case StandardOperationKind::Legacy:
      case StandardOperationKind::AddressOffset:
        break;
      }
      if (value->operationKind == StandardOperationKind::AddressOffset) {
        if ((value->operation != BinaryOperator::Add &&
             value->operation != BinaryOperator::Subtract) ||
            (!isAddressValue(value->result) &&
             !isIntegerNumeric(value->result)) ||
            !value->addressFacts ||
            value->addressFacts->origin != AddressOrigin::PointerDerived ||
            value->addressFacts->isBaseAddress) {
          addDiagnostic(*value,
                        "AddressOffset operation must carry derived non-base AddressFacts");
        }
      } else if (value->addressFacts) {
        addDiagnostic(*value,
                      "only AddressOffset operations may carry AddressFacts");
      }
      verifyExpr(*value->left);
      verifyExpr(*value->right);
      return;
    }
    if (const auto *value = dynamic_cast<const UnaryExpr *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      verifyExpr(*value->operand);
      return;
    }
    if (const auto *value = dynamic_cast<const TernaryExpr *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      if (!sameValueSemantics(value->thenExpr->result, value->elseExpr->result) ||
          !sameValueSemantics(value->result, value->thenExpr->result)) {
        addDiagnostic(*value,
                      "TernaryExpr result and branches must have the same View semantics");
      }
      verifyExpr(*value->condition);
      verifyExpr(*value->thenExpr);
      verifyExpr(*value->elseExpr);
      return;
    }
    if (const auto *value = dynamic_cast<const UnsignedExpr *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      verifyExpr(*value->operand);
      return;
    }
    if (const auto *value = dynamic_cast<const IntegerCastExpr *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      const auto expected = value->isSigned ? IntegerInterpretation::Signed
                                            : IntegerInterpretation::Unsigned;
      if (value->result.integerInterpretation != expected) {
        addDiagnostic(*value,
                      "IntegerCastExpr signedness does not match result View");
      }
      verifyExpr(*value->operand);
      return;
    }
    if (const auto *value = dynamic_cast<const TemplateViewExpr *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      checkTemplateName(*value, value->templateName, "templateName");
      if (value->isAddressable != value->result.isAddressable) {
        addDiagnostic(*value,
                      "TemplateViewExpr addressability does not match result View");
      }
      verifyExpr(*value->operand);
      return;
    }
    if (const auto *value =
            dynamic_cast<const UserTemplateOpCallExpr *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      checkTemplateName(*value, value->templateName, "templateName");
      for (const auto &argument : value->arguments) {
        verifyExpr(*argument);
      }
      return;
    }
    if (const auto *value =
            dynamic_cast<const UserTemplateFormatCallExpr *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      verifyExpr(*value->value);
      if (value->file) {
        verifyExpr(*value->file);
      }
      return;
    }
    if (const auto *value = dynamic_cast<const FloatBinaryExpr *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      verifyExpr(*value->left);
      verifyExpr(*value->right);
      return;
    }
    if (const auto *value = dynamic_cast<const FloatCompareExpr *>(&expression)) {
      if (!sameSemantics(value->result, booleanTestResultSemantics())) {
        addDiagnostic(*value,
                      "FloatCompareExpr result must be the canonical bool View");
      }
      verifyExpr(*value->left);
      verifyExpr(*value->right);
      return;
    }
    if (const auto *value = dynamic_cast<const ToFloatExpr *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      verifyExpr(*value->operand);
      return;
    }
    if (const auto *value = dynamic_cast<const ToIntExpr *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      verifyExpr(*value->operand);
      return;
    }
    if (const auto *value = dynamic_cast<const CallExpr *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      checkTemplateName(*value, value->templateName, "templateName");
      if (value->argumentPlans.size() != value->arguments.size()) {
        addDiagnostic(*value,
                      "CallExpr argument conversion plan count does not match arguments");
      } else {
        for (std::size_t index = 0; index < value->arguments.size(); ++index) {
          const auto *cast = dynamic_cast<const IntegerCastExpr *>(
              value->arguments[index].get());
          checkConversionPlan(*value, value->argumentPlans[index],
                              cast ? cast->operand->result
                                   : value->arguments[index]->result,
                              "CallExpr argument");
        }
        if (value->builtin == stdlib::BuiltinId::None) {
          const auto callee = functionContracts.find(value->callee);
          if (callee != functionContracts.end()) {
            const auto &parameters = callee->second->parameters;
            if (parameters.size() != value->arguments.size()) {
              addDiagnostic(*value,
                            "CallExpr argument count does not match callee parameter contract");
            } else {
              for (std::size_t index = 0; index < parameters.size(); ++index) {
                if (!sameValueSemantics(value->argumentPlans[index].destination,
                                        parameters[index].valueSemantics)) {
                  addDiagnostic(*value,
                                "CallExpr argument conversion plan destination does not match callee parameter contract");
                }
              }
            }
          }
        }
      }
      checkAddressFacts(*value, value->addressFacts);
      if (value->addressFacts &&
          (value->builtin != stdlib::BuiltinId::Alloc &&
           value->builtin != stdlib::BuiltinId::Calloc &&
           value->builtin != stdlib::BuiltinId::Realloc)) {
        addDiagnostic(*value,
                      "only allocation calls may carry allocation AddressFacts");
      }
      for (const auto &argument : value->arguments) {
        verifyExpr(*argument);
      }
      return;
    }
    if (const auto *value = dynamic_cast<const DynamicByteViewExpr *>(&expression)) {
      if (value->result.lengthKind != ViewLengthKind::Dynamic) {
        addDiagnostic(*value,
                      "DynamicByteViewExpr result must have a dynamic length");
      }
      verifyExpr(*value->source);
      if (value->runtimeLength) {
        verifyExpr(*value->runtimeLength);
      }
      return;
    }
    if (const auto *value = dynamic_cast<const ByteSwapExpr *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      verifyExpr(*value->source);
      return;
    }
    if (const auto *value = dynamic_cast<const AssignmentExpr *>(&expression)) {
      checkByteLength(*value, value->byteLength, "byteLength");
      for (const auto &store : value->stores) {
        verifyStmt(*store);
      }
      if (value->result) {
        if (!sameSemantics(value->result->result, value->Expr::result)) {
          addDiagnostic(*value,
                        "AssignmentExpr result does not match assigned View");
        }
        verifyExpr(*value->result);
      }
      return;
    }
    addDiagnostic(expression, "unknown HIR expression has no ViewSemantics verifier");
  };

  verifyBlock = [&](const Block &block) {
    for (const auto &statement : block.statements) {
      verifyStmt(*statement);
    }
  };
  verifyStmt = [&](const Stmt &statement) {
    if (const auto *value = dynamic_cast<const StatementList *>(&statement)) {
      for (const auto &item : value->statements) {
        verifyStmt(*item);
      }
    } else if (const auto *value = dynamic_cast<const IntegerStore *>(&statement)) {
      if (value->conversionPlan) {
        checkConversionPlan(*value->value, *value->conversionPlan,
                            value->value->result, "IntegerStore");
      }
      verifyExpr(*value->value);
    } else if (const auto *value = dynamic_cast<const FloatStore *>(&statement)) {
      if (value->conversionPlan) {
        checkConversionPlan(*value->value, *value->conversionPlan,
                            value->value->result, "FloatStore");
      }
      verifyExpr(*value->value);
    } else if (const auto *value = dynamic_cast<const ViewCopyStore *>(&statement)) {
      checkConversionPlan(*value->value, value->conversionPlan,
                          value->value->result, "ViewCopyStore");
      verifyExpr(*value->value);
    } else if (const auto *value = dynamic_cast<const BoolStore *>(&statement)) {
      verifyExpr(*value->value);
    } else if (const auto *value = dynamic_cast<const PointerStore *>(&statement)) {
      verifyExpr(*value->address);
      verifyExpr(*value->value);
    } else if (const auto *value = dynamic_cast<const Call *>(&statement)) {
      for (const auto &argument : value->arguments) {
        verifyExpr(*argument);
      }
    } else if (const auto *value = dynamic_cast<const UserTemplateOpCall *>(&statement)) {
      for (const auto &argument : value->arguments) {
        verifyExpr(*argument);
      }
    } else if (const auto *value =
                   dynamic_cast<const UserTemplateFormatCall *>(&statement)) {
      verifyExpr(*value->value);
      if (value->file) {
        verifyExpr(*value->file);
      }
    } else if (const auto *value =
                   dynamic_cast<const MultiReturnCallStore *>(&statement)) {
      for (const auto &argument : value->arguments) {
        verifyExpr(*argument);
      }
    } else if (const auto *value = dynamic_cast<const InputCallStore *>(&statement)) {
      if (value->file) {
        verifyExpr(*value->file);
      }
      verifyExpr(*value->format);
    } else if (const auto *value = dynamic_cast<const Return *>(&statement)) {
      if (!value->conversionPlans.empty() &&
          value->conversionPlans.size() != value->values.size() &&
          !value->values.empty()) {
        addDiagnostic(*value->values.front(),
                      "Return conversion plan count does not match returned values");
      }
      for (std::size_t index = 0; index < value->values.size(); ++index) {
        if (index < value->conversionPlans.size()) {
          const auto *cast = dynamic_cast<const IntegerCastExpr *>(
              value->values[index].get());
          checkConversionPlan(*value->values[index], value->conversionPlans[index],
                              cast ? cast->operand->result
                                   : value->values[index]->result,
                              "Return");
        }
        verifyExpr(*value->values[index]);
      }
    } else if (const auto *value = dynamic_cast<const If *>(&statement)) {
      verifyExpr(*value->condition);
      verifyBlock(*value->thenBlock);
      if (value->elseBlock) {
        verifyBlock(*value->elseBlock);
      }
    } else if (const auto *value = dynamic_cast<const While *>(&statement)) {
      verifyExpr(*value->condition);
      verifyBlock(*value->body);
    } else if (const auto *value = dynamic_cast<const For *>(&statement)) {
      if (value->init) {
        verifyStmt(*value->init);
      }
      if (value->condition) {
        verifyExpr(*value->condition);
      }
      for (const auto &post : value->post) {
        verifyStmt(*post);
      }
      verifyBlock(*value->body);
    } else if (const auto *value = dynamic_cast<const Label *>(&statement)) {
      verifyStmt(*value->statement);
    } else if (const auto *value = dynamic_cast<const Throw *>(&statement)) {
      if (value->delivery) {
        verifyStmt(*value->delivery);
      }
    } else if (const auto *value = dynamic_cast<const TryCatch *>(&statement)) {
      verifyBlock(*value->tryBlock);
      verifyBlock(*value->catchBlock);
    }
  };

  if (unit.globalInit) {
    verifyBlock(*unit.globalInit);
  }
  for (const auto &function : unit.functions) {
    if (function) {
      verifyBlock(*function->body);
    }
  }
  return diagnostics;
}

} // namespace hitsimple::hir
