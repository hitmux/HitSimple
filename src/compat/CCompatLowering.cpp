#include "CCompatLoweringInternal.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace hitsimple::compat::detail {
namespace {

bool checkedMultiply(std::size_t left, std::size_t right, std::size_t& result) {
  if (left == 0 || right == 0) {
    result = 0;
    return true;
  }
  if (left > std::numeric_limits<std::size_t>::max() / right) {
    return false;
  }
  result = left * right;
  return true;
}

TypeInfo scalar(CAbiValueKind kind,
                std::size_t byteLength,
                bool isSigned,
                std::string coreTemplate) {
  CAbiType abi{kind, byteLength, isSigned, ""};
  abi.alignment = byteLength;
  return TypeInfo{std::move(abi), std::move(coreTemplate), nullptr};
}

bool alignUp(std::size_t value, std::size_t alignment, std::size_t& result) {
  if (alignment == 0) {
    return false;
  }
  const auto remainder = value % alignment;
  if (remainder == 0) {
    result = value;
    return true;
  }
  const auto adjustment = alignment - remainder;
  if (value > std::numeric_limits<std::size_t>::max() - adjustment) {
    return false;
  }
  result = value + adjustment;
  return true;
}

} // namespace

std::size_t ObjectInfo::byteLength() const {
  if (!isArray) {
    return type.abi.byteLength;
  }
  std::size_t result = 0;
  return checkedMultiply(type.abi.byteLength, arrayCount, result) ? result : 0;
}

Lowerer::Lowerer(const TranslationUnit& unit, LoweringOptions options)
    : source_(unit), options_(options) {}

void Lowerer::error(const diagnostic::SourceRange& range, std::string message) {
  auto diagnostic = diagnostic::Diagnostic::error(diagnostic::Stage::Parser,
                                                   std::move(message));
  diagnostic.range = range;
  diagnostics_.push_back(std::move(diagnostic));
}

bool Lowerer::hasErrors() const { return !diagnostics_.empty(); }

TypeInfo Lowerer::makePointer(TypeInfo pointee, std::size_t pointerByteLength) {
  TypeInfo result;
  result.abi = CAbiType{CAbiValueKind::Pointer, pointerByteLength, false, ""};
  result.abi.alignment = pointerByteLength;
  result.coreTemplate = "addr";
  result.pointee = std::make_shared<TypeInfo>(std::move(pointee));
  return result;
}

bool Lowerer::sameType(const TypeInfo& left, const TypeInfo& right) {
  if (left.abi.kind != right.abi.kind ||
      left.abi.byteLength != right.abi.byteLength ||
      left.abi.isSigned != right.abi.isSigned ||
      left.abi.aggregateName != right.abi.aggregateName) {
    return false;
  }
  if (left.pointee == nullptr || right.pointee == nullptr) {
    return left.pointee == nullptr && right.pointee == nullptr;
  }
  return sameType(*left.pointee, *right.pointee);
}

bool Lowerer::isInteger(const TypeInfo& type) {
  return type.abi.kind == CAbiValueKind::Integer;
}

bool Lowerer::isFloating(const TypeInfo& type) {
  return type.abi.kind == CAbiValueKind::Floating;
}

bool Lowerer::isPointer(const TypeInfo& type) {
  return type.abi.kind == CAbiValueKind::Pointer;
}

bool Lowerer::isAggregate(const TypeInfo& type) {
  return type.abi.kind == CAbiValueKind::Aggregate;
}

bool Lowerer::isVoid(const TypeInfo& type) {
  return type.abi.kind == CAbiValueKind::Void;
}

std::string Lowerer::byteLengthText(std::size_t byteLength,
                                    std::size_t pointerByteLength) {
  return byteLength == pointerByteLength ? "P" : std::to_string(byteLength);
}

std::string Lowerer::integerOperator(std::size_t byteLength,
                                     std::string_view op) {
  return "%" + std::to_string(byteLength) + "d" + std::string(op);
}

std::string Lowerer::floatOperator(std::size_t byteLength,
                                   std::string_view op) {
  return "%" + std::to_string(byteLength) + "f" + std::string(op);
}

void Lowerer::collectDeclarations() {
  const auto sameFunctionSignature = [this](const FunctionInfo& left,
                                            const FunctionInfo& right) {
    if (!sameType(left.returnType, right.returnType) ||
        left.parameters.size() != right.parameters.size()) {
      return false;
    }
    return std::equal(left.parameters.begin(), left.parameters.end(),
                      right.parameters.begin(),
                      [this](const TypeInfo& leftParameter,
                             const TypeInfo& rightParameter) {
                        return sameType(leftParameter, rightParameter);
                      });
  };

  for (const auto& declaration : source_.declarations) {
    if (const auto* structDecl = dynamic_cast<const StructDecl*>(declaration.get())) {
      if (structs_.contains(structDecl->name)) {
        error(structDecl->range, "duplicate C struct declaration '" +
                                  structDecl->name + "'");
      } else {
        structs_.emplace(structDecl->name,
                         StructInfo{structDecl, false, false, 0, 1, {}, {}});
      }
    } else if (const auto* typedefDecl =
                   dynamic_cast<const TypedefDecl*>(declaration.get())) {
      if (aliases_.contains(typedefDecl->declarator.name)) {
        error(typedefDecl->range, "duplicate C typedef '" +
                                  typedefDecl->declarator.name + "'");
      } else {
        aliases_.emplace(typedefDecl->declarator.name,
                         AliasInfo{typedefDecl, false, false, std::nullopt});
      }
    }
  }

  for (const auto& declaration : source_.declarations) {
    const auto* typedefDecl = dynamic_cast<const TypedefDecl*>(declaration.get());
    if (typedefDecl == nullptr) {
      continue;
    }
    const auto found = aliases_.find(typedefDecl->declarator.name);
    if (found != aliases_.end() && found->second.declaration == typedefDecl) {
      resolveAliasObject(typedefDecl->declarator.name, typedefDecl->range);
    }
  }

  for (const auto& declaration : source_.declarations) {
    const auto* function = dynamic_cast<const FunctionDecl*>(declaration.get());
    if (function == nullptr) {
      continue;
    }
    auto returnType = resolveType(function->returnType,
                                  function->declarator.pointerDepth);
    if (!returnType) {
      continue;
    }
    if (isAggregate(*returnType) &&
        !ensureStructComplete(*returnType, function->returnType.range)) {
      continue;
    }
    FunctionInfo info;
    info.returnType = std::move(*returnType);
    info.linkage = function->storage == StorageClass::Static
                       ? Linkage::Internal
                       : Linkage::External;
    info.isDefinition = function->isDefinition();
    bool valid = true;
    for (const auto& parameter : function->parameters) {
      if (parameter.isVoidMarker) {
        continue;
      }
      auto object = resolveObject(parameter.type, parameter.declarator);
      if (!object) {
        valid = false;
        continue;
      }
      TypeInfo type = object->type;
      if (object->isArray) {
        type = makePointer(std::move(type), options_.pointerByteLength);
      }
      info.parameters.push_back(std::move(type));
    }
    if (!valid) {
      continue;
    }
    auto found = functions_.find(function->declarator.name);
    if (found == functions_.end()) {
      functions_.emplace(function->declarator.name, std::move(info));
      continue;
    }
    if (!sameFunctionSignature(found->second, info)) {
      error(function->range,
            "conflicting C function declaration '" +
                function->declarator.name + "'");
      continue;
    }
    if (found->second.linkage == Linkage::External &&
        info.linkage == Linkage::Internal) {
      error(function->range,
            "C static function declaration '" + function->declarator.name +
                "' follows a non-static declaration");
      continue;
    }
    if (found->second.isDefinition && function->isDefinition()) {
      error(function->range,
            "duplicate C function definition '" + function->declarator.name +
                "'");
      continue;
    }
    if (function->isDefinition()) {
      found->second.isDefinition = true;
    }
  }

  for (const auto& declaration : source_.declarations) {
    const auto* prototype = dynamic_cast<const FunctionDecl*>(declaration.get());
    if (prototype == nullptr || prototype->isDefinition() ||
        prototype->storage != StorageClass::Static) {
      continue;
    }

    const auto found = functions_.find(prototype->declarator.name);
    if (found == functions_.end() || !found->second.isDefinition ||
        found->second.linkage != Linkage::Internal) {
      error(prototype->range, "C static function declaration '" +
                                  prototype->declarator.name +
                                  "' requires a definition in the same translation unit");
    }
  }
}

std::optional<TypeInfo> Lowerer::resolveBaseType(const CType& type) {
  if (options_.rejectQualifiers && (type.isConst || type.isVolatile)) {
    error(type.range, "C const/volatile qualifiers require core metadata and are not enabled");
    return std::nullopt;
  }
  switch (type.base) {
  case BaseType::Char:
    return scalar(CAbiValueKind::Integer, 1, false, "u8");
  case BaseType::SignedChar:
    return scalar(CAbiValueKind::Integer, 1, true, "i8");
  case BaseType::UnsignedChar:
    return scalar(CAbiValueKind::Integer, 1, false, "u8");
  case BaseType::Short:
    return scalar(CAbiValueKind::Integer, 2, true, "i16");
  case BaseType::UnsignedShort:
    return scalar(CAbiValueKind::Integer, 2, false, "u16");
  case BaseType::Int:
    return scalar(CAbiValueKind::Integer, 4, true, "i32");
  case BaseType::UnsignedInt:
    return scalar(CAbiValueKind::Integer, 4, false, "u32");
  case BaseType::Long:
    return scalar(CAbiValueKind::Integer, 8, true, "i64");
  case BaseType::UnsignedLong:
    return scalar(CAbiValueKind::Integer, 8, false, "u64");
  case BaseType::LongLong:
    return scalar(CAbiValueKind::Integer, 8, true, "i64");
  case BaseType::UnsignedLongLong:
    return scalar(CAbiValueKind::Integer, 8, false, "u64");
  case BaseType::Float:
    return scalar(CAbiValueKind::Floating, 4, true, "f32");
  case BaseType::Double:
    return scalar(CAbiValueKind::Floating, 8, true, "f64");
  case BaseType::Void:
    return scalar(CAbiValueKind::Void, 0, false, "");
  case BaseType::Struct:
    return TypeInfo{CAbiType{CAbiValueKind::Aggregate, 0, false, type.name},
                    type.name, nullptr};
  case BaseType::TypedefName:
    return resolveAlias(type.name, type.range);
  }
  error(type.range, "unsupported C compatibility type");
  return std::nullopt;
}

std::optional<TypeInfo> Lowerer::resolveType(const CType& type,
                                             std::size_t pointerDepth) {
  auto result = resolveBaseType(type);
  if (!result) {
    return std::nullopt;
  }
  for (std::size_t level = 0; level < pointerDepth; ++level) {
    *result = makePointer(std::move(*result), options_.pointerByteLength);
  }
  return result;
}

std::optional<TypeInfo> Lowerer::resolveAlias(
    std::string_view name, const diagnostic::SourceRange& range) {
  auto object = resolveAliasObject(name, range);
  if (!object) {
    return std::nullopt;
  }
  if (object->isArray) {
    error(range, "C array typedef '" + std::string(name) +
                     "' cannot be used where a scalar C type is required");
    return std::nullopt;
  }
  return object->type;
}

std::optional<ObjectInfo> Lowerer::resolveAliasObject(
    std::string_view name, const diagnostic::SourceRange& range) {
  const auto found = aliases_.find(std::string(name));
  if (found == aliases_.end()) {
    error(range, "unknown C typedef name '" + std::string(name) + "'");
    return std::nullopt;
  }
  auto& alias = found->second;
  if (alias.resolved) {
    return alias.object;
  }
  if (alias.resolving) {
    error(range, "cyclic C typedef involving '" + std::string(name) + "'");
    return std::nullopt;
  }
  alias.resolving = true;
  auto object = resolveObject(alias.declaration->type,
                              alias.declaration->declarator);
  alias.resolving = false;
  if (!object) {
    return std::nullopt;
  }
  alias.object = *object;
  alias.resolved = true;
  return alias.object;
}

bool Lowerer::ensureStructComplete(TypeInfo& type,
                                   const diagnostic::SourceRange& range) {
  if (!isAggregate(type)) {
    return true;
  }
  if (!resolveStruct(type.abi.aggregateName, range)) {
    return false;
  }
  type.abi = structs_.at(type.abi.aggregateName).abi;
  return true;
}

bool Lowerer::resolveStruct(std::string_view name,
                            const diagnostic::SourceRange& range) {
  const auto found = structs_.find(std::string(name));
  if (found == structs_.end()) {
    error(range, "unknown C struct '" + std::string(name) + "'");
    return false;
  }
  auto& structure = found->second;
  if (structure.resolved) {
    return true;
  }
  if (structure.resolving) {
    error(range, "C struct '" + std::string(name) +
                     "' contains itself by value");
    return false;
  }
  structure.resolving = true;
  std::size_t offset = 0;
  std::size_t aggregateAlignment = 1;
  std::vector<CAbiType> abiFields;
  std::vector<std::size_t> abiOffsets;
  for (const auto& field : structure.declaration->fields) {
    auto object = resolveObject(field.type, field.declarator);
    if (!object) {
      structure.resolving = false;
      return false;
    }
    const auto byteLength = object->byteLength();
    const auto fieldAlignment = object->type.abi.alignment;
    if (byteLength == 0 || fieldAlignment == 0 ||
        !alignUp(offset, fieldAlignment, offset) ||
        offset > std::numeric_limits<std::size_t>::max() - byteLength) {
      error(field.range, "invalid or overflowing C struct field size for '" +
                             field.declarator.name + "'");
      structure.resolving = false;
      return false;
    }
    structure.fields.push_back(FieldInfo{field.declarator.name, std::move(*object),
                                         offset});
    auto fieldAbi = structure.fields.back().object.type.abi;
    fieldAbi.elementCount = structure.fields.back().object.isArray
                                ? structure.fields.back().object.arrayCount
                                : 1;
    abiFields.push_back(std::move(fieldAbi));
    abiOffsets.push_back(offset);
    aggregateAlignment = std::max(aggregateAlignment, fieldAlignment);
    offset += byteLength;
  }
  if (!alignUp(offset, aggregateAlignment, structure.byteLength)) {
    error(structure.declaration->range,
          "overflowing C struct size for '" + structure.declaration->name + "'");
    structure.resolving = false;
    return false;
  }
  structure.alignment = aggregateAlignment;
  structure.abi = CAbiType{CAbiValueKind::Aggregate, structure.byteLength,
                            false, structure.declaration->name};
  structure.abi.alignment = structure.alignment;
  structure.abi.aggregateFields = std::move(abiFields);
  structure.abi.aggregateFieldOffsets = std::move(abiOffsets);
  structure.resolving = false;
  structure.resolved = true;
  return true;
}

std::optional<ObjectInfo> Lowerer::resolveObject(const CType& type,
                                                  const Declarator& declarator) {
  if (type.base == BaseType::TypedefName) {
    if (options_.rejectQualifiers && (type.isConst || type.isVolatile)) {
      error(type.range,
            "C const/volatile qualifiers require core metadata and are not enabled");
      return std::nullopt;
    }
    auto aliased = resolveAliasObject(type.name, type.range);
    if (!aliased) {
      return std::nullopt;
    }
    if (aliased->isArray) {
      if (declarator.pointerDepth != 0) {
        error(declarator.range,
              "pointer to C array typedef '" + type.name +
                  "' is outside the Standard 16.1 compatibility subset");
        return std::nullopt;
      }
      if (declarator.arrayCount) {
        error(declarator.range,
              "multidimensional C arrays through a typedef are not supported");
        return std::nullopt;
      }
      return aliased;
    }
  }

  auto resolved = resolveType(type, declarator.pointerDepth);
  if (!resolved) {
    return std::nullopt;
  }
  if (isVoid(*resolved)) {
    error(declarator.range, "void cannot be used for a C object, field, array, or by-value parameter");
    return std::nullopt;
  }
  if (isAggregate(*resolved) && !ensureStructComplete(*resolved, declarator.range)) {
    return std::nullopt;
  }
  ObjectInfo object{std::move(*resolved), declarator.arrayCount.has_value(),
                    declarator.arrayCount.value_or(0)};
  if (object.isArray && object.arrayCount == 0) {
    error(declarator.range, "C array element count must be greater than zero");
    return std::nullopt;
  }
  if (object.byteLength() == 0) {
    error(declarator.range, "C object byte length is not statically known");
    return std::nullopt;
  }
  return object;
}

void Lowerer::pushScope() { scopes_.emplace_back(); }

void Lowerer::popScope() {
  if (!scopes_.empty()) {
    scopes_.pop_back();
  }
}

void Lowerer::bindObject(std::string name, ObjectInfo object) {
  if (scopes_.empty()) {
    pushScope();
  }
  scopes_.back()[std::move(name)] = std::move(object);
}

std::optional<ObjectInfo> Lowerer::lookupObject(std::string_view name) const {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    const auto found = it->find(std::string(name));
    if (found != it->end()) {
      return found->second;
    }
  }
  return std::nullopt;
}

LoweringResult Lowerer::lower() {
  collectDeclarations();
  pushScope();
  for (const auto& declaration : source_.declarations) {
    lowerTopLevel(*declaration);
  }
  popScope();

  LoweringResult result;
  result.unit = std::make_unique<ast::TranslationUnit>(std::move(declarations_));
  result.linkage = std::move(linkage_);
  result.diagnostics = std::move(diagnostics_);
  return result;
}

} // namespace hitsimple::compat::detail

namespace hitsimple::compat {

LoweringResult lowerCCompatToCore(const TranslationUnit& unit,
                                  LoweringOptions options) {
  return detail::Lowerer(unit, options).lower();
}

} // namespace hitsimple::compat
