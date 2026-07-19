#include "hitsimple/hir/HIR.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <unordered_map>
#include <utility>

namespace hitsimple::hir {

namespace {
thread_local std::optional<diagnostic::SourceRange> activeRange;
} // namespace

void setActiveSourceRange(std::optional<diagnostic::SourceRange> range) {
  activeRange = std::move(range);
}

std::optional<diagnostic::SourceRange> activeSourceRange() {
  return activeRange;
}

Expr::Expr() : range(activeSourceRange()) {}

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

IntegerLiteral::IntegerLiteral(std::string value) : value(std::move(value)) {}

IntegerLiteral::IntegerLiteral(std::string value, std::size_t byteLength)
    : value(std::move(value)), byteLength(byteLength) {}

StringLiteral::StringLiteral(std::string value) : value(std::move(value)) {}

StringLiteral::StringLiteral(std::string value, std::size_t byteLength)
    : value(std::move(value)), byteLength(byteLength) {}

FloatLiteral::FloatLiteral(std::string value, std::size_t byteLength)
    : value(std::move(value)), byteLength(byteLength) {}

VariableRef::VariableRef(std::string name, std::size_t byteLength,
                         std::string templateName)
    : name(std::move(name)), bindingName(this->name), byteLength(byteLength),
      templateName(std::move(templateName)) {}

VariableRef::VariableRef(std::string name, std::string bindingName,
                         std::size_t byteLength, MemoryStorage storage,
                         std::string templateName)
    : name(std::move(name)), bindingName(std::move(bindingName)),
      byteLength(byteLength), storage(storage),
      templateName(std::move(templateName)) {}

VariableRef::VariableRef(std::string name, std::string bindingName,
                         std::size_t byteLength, MemoryStorage storage,
                         std::size_t offset, std::string templateName)
    : name(std::move(name)), bindingName(std::move(bindingName)),
      byteLength(byteLength), storage(storage), offset(offset),
      templateName(std::move(templateName)) {}

AddressOfExpr::AddressOfExpr(std::string name, std::string bindingName,
                             std::size_t targetByteLength,
                             MemoryStorage storage, std::size_t offset,
                             std::size_t byteLength)
    : name(std::move(name)), bindingName(std::move(bindingName)),
      targetByteLength(targetByteLength), storage(storage), offset(offset),
      byteLength(byteLength) {}

DerefExpr::DerefExpr(std::unique_ptr<Expr> address, std::size_t byteLength)
    : address(std::move(address)), byteLength(byteLength) {}

BinaryExpr::BinaryExpr(std::unique_ptr<Expr> left, std::string op,
                       std::unique_ptr<Expr> right, std::size_t byteLength)
    : left(std::move(left)), op(std::move(op)), right(std::move(right)),
      byteLength(byteLength) {}

UnaryExpr::UnaryExpr(std::string op, std::unique_ptr<Expr> operand,
                     std::size_t byteLength)
    : op(std::move(op)), operand(std::move(operand)), byteLength(byteLength) {}

TernaryExpr::TernaryExpr(std::unique_ptr<Expr> condition,
                         std::unique_ptr<Expr> thenExpr,
                         std::unique_ptr<Expr> elseExpr, std::size_t byteLength)
    : condition(std::move(condition)), thenExpr(std::move(thenExpr)),
      elseExpr(std::move(elseExpr)), byteLength(byteLength) {}

UnsignedExpr::UnsignedExpr(std::unique_ptr<Expr> operand,
                           std::size_t byteLength)
    : operand(std::move(operand)), byteLength(byteLength) {}

IntegerCastExpr::IntegerCastExpr(std::unique_ptr<Expr> operand,
                                 std::size_t byteLength, bool isSigned)
    : operand(std::move(operand)), byteLength(byteLength), isSigned(isSigned) {}

TemplateViewExpr::TemplateViewExpr(std::unique_ptr<Expr> operand,
                                   std::size_t byteLength,
                                   std::string templateName,
                                   bool isAddressable)
    : operand(std::move(operand)), byteLength(byteLength),
      templateName(std::move(templateName)), isAddressable(isAddressable) {}

UserTemplateOpCallExpr::UserTemplateOpCallExpr(
    std::string callee, std::vector<std::unique_ptr<Expr>> arguments,
    std::size_t byteLength, std::string templateName)
    : callee(std::move(callee)), arguments(std::move(arguments)),
      byteLength(byteLength), templateName(std::move(templateName)) {}

FloatBinaryExpr::FloatBinaryExpr(std::unique_ptr<Expr> left, std::string op,
                                 std::unique_ptr<Expr> right,
                                 std::size_t byteLength)
    : left(std::move(left)), op(std::move(op)), right(std::move(right)),
      byteLength(byteLength) {}

FloatCompareExpr::FloatCompareExpr(std::unique_ptr<Expr> left, std::string op,
                                   std::unique_ptr<Expr> right,
                                   std::size_t operandByteLength)
    : left(std::move(left)), op(std::move(op)), right(std::move(right)),
      operandByteLength(operandByteLength) {}

ToFloatExpr::ToFloatExpr(std::unique_ptr<Expr> operand, std::size_t byteLength,
                         bool sourceUnsigned, bool sourceIsFloating)
    : operand(std::move(operand)), byteLength(byteLength),
      sourceUnsigned(sourceUnsigned), sourceIsFloating(sourceIsFloating) {}

ToIntExpr::ToIntExpr(std::unique_ptr<Expr> operand, std::size_t floatByteLength,
                     std::size_t byteLength, bool isUnsigned)
    : operand(std::move(operand)), floatByteLength(floatByteLength),
      byteLength(byteLength), isUnsigned(isUnsigned) {}

CallExpr::CallExpr(std::string callee,
                   std::vector<std::unique_ptr<Expr>> arguments,
                   std::size_t byteLength, bool isFloating,
                   stdlib::BuiltinId builtin,
                   std::vector<FormatArgKind> formatArgumentKinds,
                   std::uint16_t overloadIndex, std::string templateName)
    : callee(std::move(callee)), arguments(std::move(arguments)),
      byteLength(byteLength), isFloating(isFloating), builtin(builtin),
      provider(stdlib::builtinCallMetadata(builtin, overloadIndex).provider),
      returnMode(stdlib::builtinCallMetadata(builtin, overloadIndex).returnMode),
      overloadIndex(overloadIndex),
      formatArgumentKinds(std::move(formatArgumentKinds)),
      templateName(std::move(templateName)) {}

UserTemplateFormatCallExpr::UserTemplateFormatCallExpr(
    std::string callee, std::unique_ptr<Expr> value, FormatOutputSink sink,
    std::unique_ptr<Expr> file, std::size_t byteLength)
    : callee(std::move(callee)), value(std::move(value)), sink(sink),
      file(std::move(file)), byteLength(byteLength) {}

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
    std::unique_ptr<Expr> runtimeLength)
    : operation(operation), source(std::move(source)),
      runtimeLength(std::move(runtimeLength)) {}

ByteSwapExpr::ByteSwapExpr(std::unique_ptr<Expr> source,
                           std::size_t byteLength)
    : source(std::move(source)), byteLength(byteLength) {}

AssignmentExpr::AssignmentExpr(std::vector<std::unique_ptr<Stmt>> stores,
                               std::unique_ptr<Expr> result,
                               std::size_t byteLength)
    : stores(std::move(stores)), result(std::move(result)),
      byteLength(byteLength) {}

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
      range(activeSourceRange()), byteLength(byteLength) {}

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

Return::Return(std::vector<std::unique_ptr<Expr>> values)
    : values(std::move(values)) {}

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

} // namespace hitsimple::hir
