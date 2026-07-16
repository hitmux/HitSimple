#include "hitsimple/flowir/Builder.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace hitsimple::flowir {
namespace {

constexpr std::uint32_t maxIdCount = InvalidId;

std::optional<std::uint32_t> fixedTemplateByteLength(std::string_view name) {
  if (name == "bool") {
    return 1;
  }
  if (name == "i8" || name == "u8") {
    return 1;
  }
  if (name == "i16" || name == "u16" || name == "f16") {
    return 2;
  }
  if (name == "i32" || name == "u32" || name == "f32") {
    return 4;
  }
  if (name == "i64" || name == "u64" || name == "f64") {
    return 8;
  }
  if (name == "f128") {
    return 16;
  }
  return std::nullopt;
}

ObjectStorage toObjectStorage(hir::MemoryStorage storage) {
  switch (storage) {
  case hir::MemoryStorage::Global: return ObjectStorage::Global;
  case hir::MemoryStorage::Local: return ObjectStorage::Local;
  case hir::MemoryStorage::StaticLocal: return ObjectStorage::StaticLocal;
  }
  return ObjectStorage::Local;
}

std::uint32_t effectFlags(const hir::EffectContract& contract) {
  std::uint32_t result = FunctionEffectNone;
  for (const auto& range : contract.ranges) {
    result |= range.access == hir::EffectAccess::Read ? FunctionEffectRead
                                                       : FunctionEffectWrite;
  }
  if ((contract.flags & hir::EffectAllocates) != 0U) result |= FunctionEffectAllocates;
  if ((contract.flags & hir::EffectFrees) != 0U) result |= FunctionEffectFrees;
  if ((contract.flags & hir::EffectThrows) != 0U) result |= FunctionEffectThrows;
  if ((contract.flags & hir::EffectNothrow) != 0U ||
      (contract.flags & hir::EffectPure) != 0U) result |= FunctionEffectNothrow;
  if ((contract.flags & hir::EffectIo) != 0U) result |= FunctionEffectIo;
  if ((contract.flags & hir::EffectUnknown) != 0U) result |= FunctionEffectUnknown;
  return result;
}

class FlowBuilder final {
public:
  BuildResult run(const hir::TranslationUnit &unit) {
    collectTemplateLengths(unit);
    for (const auto &global : unit.globals) {
      createObject(InvalidId, global.bindingName, global.byteLength,
                   toObjectStorage(hir::MemoryStorage::Global),
                   global.templateName, global.sourceRange);
    }
    globalObjectsByBinding_ = objectsByBinding_;

    for (const auto &external : unit.externFunctions) {
      addExternalFunction(external);
    }

    if (unit.globalInit && !unit.globalInit->statements.empty()) {
      beginFunction("__flowir_global_init", {}, {}, unit.globalInit->sourceRange);
      lowerBlock(*unit.globalInit);
      finishFunction(unit.globalInit->sourceRange);
    }

    for (const auto &function : unit.functions) {
      if (function) {
        lowerFunction(*function);
      }
    }

    finalizeEdges();
    BuildResult result;
    result.diagnostics = std::move(diagnostics_);
    if (result.diagnostics.empty()) {
      result.module = std::move(module_);
    }
    return result;
  }

private:
  struct Scope final {
    std::vector<ObjectId> automaticObjects;
  };

  struct LoopTarget final {
    BlockId breakBlock = InvalidId;
    BlockId continueBlock = InvalidId;
    std::size_t cleanupScopeCount = 0;
  };

  Module module_;
  std::vector<diagnostic::Diagnostic> diagnostics_;
  std::unordered_map<std::string, StringId> strings_;
  std::unordered_map<std::string, TemplateId> templates_;
  std::unordered_map<std::string, std::uint32_t> templateLengths_;
  std::unordered_map<std::string, std::string> memberTemplates_;
  std::unordered_map<std::string, ObjectId> objectsByBinding_;
  std::unordered_map<std::string, ObjectId> globalObjectsByBinding_;
  std::unordered_map<std::string, BlockId> labelBlocks_;
  std::unordered_map<std::string, std::size_t> labelDepths_;
  std::vector<Scope> scopes_;
  std::vector<LoopTarget> loops_;
  std::vector<BlockId> exceptionTargets_;
  FunctionId currentFunction_ = InvalidId;
  BlockId currentBlock_ = InvalidId;
  BlockId uncaughtBlock_ = InvalidId;
  std::size_t labelScopeBase_ = 0;
  bool fallsThrough_ = true;

  void fail(std::string message) {
    diagnostics_.push_back(
        diagnostic::Diagnostic::error(diagnostic::Stage::Hir, std::move(message)));
  }

  template <typename T>
  Id nextId(const std::vector<T> &table, std::string_view tableName) {
    if (table.size() >= maxIdCount) {
      fail("FlowIR " + std::string(tableName) + " ID overflow");
      return InvalidId;
    }
    return static_cast<Id>(table.size());
  }

  std::uint32_t narrow(std::size_t value, std::string_view subject) {
    if (value >= maxIdCount) {
      fail("FlowIR " + std::string(subject) + " exceeds 32-bit schema limit");
      return 0;
    }
    return static_cast<std::uint32_t>(value);
  }

  StringId internString(std::string_view text) {
    if (text.empty()) {
      return InvalidId;
    }
    const auto found = strings_.find(std::string(text));
    if (found != strings_.end()) {
      return found->second;
    }
    const auto id = nextId(module_.strings, "string");
    if (id == InvalidId) {
      return id;
    }
    module_.strings.emplace_back(text);
    strings_.emplace(module_.strings.back(), id);
    return id;
  }

  SourceMapId sourceMap(
      const std::optional<diagnostic::SourceRange> &range) {
    if (!range || !diagnostic::hasRange(*range)) {
      return InvalidId;
    }
    const auto id = nextId(module_.sourceMaps, "source map");
    if (id == InvalidId) {
      return id;
    }
    module_.sourceMaps.push_back(SourceMapRecord{
        internString(range->begin.file), narrow(range->begin.line, "source line"),
        narrow(range->begin.column, "source column"),
        narrow(range->end.line, "source line"),
        narrow(range->end.column, "source column")});
    return id;
  }

  void collectTemplateLengths(const hir::TranslationUnit &unit) {
    for (const auto &view : unit.viewTemplates) {
      templateLengths_[view.name] = narrow(view.byteLength, "template length");
      for (const auto &member : view.members) {
        memberTemplates_.emplace(view.name + "\x1f" +
                                     std::to_string(member.offset) + "\x1f" +
                                     std::to_string(member.byteLength),
                                 member.templateName);
      }
    }
  }

  TemplateId templateId(std::string_view name, std::uint32_t byteLength) {
    if (name.empty() || name == "none") {
      return InvalidId;
    }
    const bool dynamic = name == "bytes" || name == "cstr";
    const auto declared = templateLengths_.find(std::string(name));
    const auto fixed = fixedTemplateByteLength(name);
    const auto expected = declared != templateLengths_.end()
                              ? std::optional<std::uint32_t>(declared->second)
                              : fixed;
    if (expected && *expected != byteLength) {
      fail("FlowIR View byte length does not match template '" +
           std::string(name) + "' (expected " + std::to_string(*expected) +
           ", got " + std::to_string(byteLength) + ")");
      return InvalidId;
    }
    const std::string key = std::string(name) + "\x1f" +
                            std::to_string(dynamic ? 0U : byteLength);
    const auto found = templates_.find(key);
    if (found != templates_.end()) {
      return found->second;
    }
    const auto id = nextId(module_.templates, "template");
    if (id == InvalidId) {
      return id;
    }
    module_.templates.push_back(
        TemplateRecord{internString(name), dynamic ? 0U : byteLength, dynamic});
    templates_.emplace(key, id);
    return id;
  }

  std::string compatibleViewTemplate(std::string_view name,
                                     std::uint32_t byteLength) const {
    if (name.empty() || name == "none") {
      return {};
    }
    if (name == "bytes" || name == "cstr") {
      return std::string(name);
    }
    const auto declared = templateLengths_.find(std::string(name));
    const auto fixed = fixedTemplateByteLength(name);
    const auto expected = declared != templateLengths_.end()
                              ? std::optional<std::uint32_t>(declared->second)
                              : fixed;
    return expected && *expected != byteLength ? std::string{} : std::string(name);
  }

  ViewId createView(std::string_view templateName, std::uint32_t byteLength,
                    ValueCategory category, ObjectId object = InvalidId,
                    std::uint32_t offset = 0,
                    std::uint32_t flags = InterpretationNone) {
    const auto id = nextId(module_.views, "view");
    if (id == InvalidId) {
      return id;
    }
    module_.views.push_back(ViewRecord{templateId(templateName, byteLength),
                                       byteLength, category, flags, object,
                                       offset});
    return id;
  }

  std::string templateForInteger(std::uint32_t byteLength, bool isUnsigned = false) {
    return std::string(isUnsigned ? "u" : "i") +
           std::to_string(byteLength * 8U);
  }

  std::string templateForFloat(std::uint32_t byteLength) {
    return "f" + std::to_string(byteLength * 8U);
  }

  ObjectId createObject(
      FunctionId function, std::string_view bindingName, std::size_t byteLength,
      ObjectStorage storage, std::string_view templateName,
      const std::optional<diagnostic::SourceRange> &range) {
    const auto id = nextId(module_.objects, "object");
    if (id == InvalidId) {
      return id;
    }
    const auto length = narrow(byteLength, "object byte length");
    module_.objects.push_back(ObjectRecord{function, internString(bindingName),
                                           length, storage,
                                           templateId(templateName, length),
                                           sourceMap(range)});
    objectsByBinding_[std::string(bindingName)] = id;
    return id;
  }

  std::optional<ObjectId> objectFor(std::string_view bindingName) const {
    const auto found = objectsByBinding_.find(std::string(bindingName));
    if (found == objectsByBinding_.end()) {
      return std::nullopt;
    }
    return found->second;
  }

  std::string objectTemplate(ObjectId object) const {
    if (object == InvalidId || object >= module_.objects.size()) {
      return {};
    }
    const auto templateId = module_.objects[object].templateId;
    if (templateId == InvalidId || templateId >= module_.templates.size()) {
      return {};
    }
    const auto name = module_.templates[templateId].name;
    if (name == InvalidId || name >= module_.strings.size()) {
      return {};
    }
    return module_.strings[name];
  }

  std::string viewTemplate(ViewId view) const {
    if (view == InvalidId || view >= module_.views.size()) {
      return {};
    }
    const auto templateId = module_.views[view].templateId;
    if (templateId == InvalidId || templateId >= module_.templates.size()) {
      return {};
    }
    const auto name = module_.templates[templateId].name;
    return name == InvalidId || name >= module_.strings.size()
               ? std::string{}
               : module_.strings[name];
  }

  std::string objectSliceTemplate(ObjectId object, std::uint32_t offset,
                                  std::uint32_t byteLength) const {
    if (object == InvalidId || object >= module_.objects.size()) {
      return {};
    }
    const auto wholeTemplate = objectTemplate(object);
    if (offset == 0 && module_.objects[object].byteLength == byteLength) {
      return wholeTemplate;
    }
    const auto found = memberTemplates_.find(wholeTemplate + "\x1f" +
                                             std::to_string(offset) + "\x1f" +
                                             std::to_string(byteLength));
    return found == memberTemplates_.end() ? std::string{} : found->second;
  }

  BlockId createBlock() {
    const auto id = nextId(module_.blocks, "block");
    if (id == InvalidId) {
      return id;
    }
    module_.blocks.push_back(BlockRecord{currentFunction_});
    if (currentFunction_ != InvalidId) {
      ++module_.functions[currentFunction_].blockCount;
    }
    return id;
  }

  void activate(BlockId block) {
    currentBlock_ = block;
    fallsThrough_ = true;
    if (block != InvalidId && block < module_.blocks.size() &&
        module_.blocks[block].firstInstruction == InvalidId) {
      module_.blocks[block].firstInstruction =
          narrow(module_.instructions.size(), "instruction ID");
    }
  }

  std::vector<ValueId> emit(Opcode opcode, const std::vector<ValueId> &operands,
                            const std::vector<ViewId> &resultViews,
                            std::optional<ObjectSliceRecord> slice,
                            StringId symbol, SourceMapId source,
                            std::uint32_t auxiliary0 = 0,
                            std::uint32_t auxiliary1 = 0) {
    if (currentBlock_ == InvalidId || currentBlock_ >= module_.blocks.size()) {
      fail("FlowIR instruction has no current block");
      return {};
    }
    const auto instructionId = nextId(module_.instructions, "instruction");
    if (instructionId == InvalidId) {
      return {};
    }
    const auto operandBegin = narrow(module_.operands.size(), "operand ID");
    module_.operands.insert(module_.operands.end(), operands.begin(), operands.end());
    const auto resultBegin = narrow(module_.results.size(), "result ID");
    std::vector<ValueId> results;
    results.reserve(resultViews.size());
    for (const auto view : resultViews) {
      const auto valueId = nextId(module_.values, "value");
      if (valueId == InvalidId) {
        return {};
      }
      module_.values.push_back(ValueRecord{instructionId, view});
      module_.results.push_back(valueId);
      results.push_back(valueId);
    }
    module_.instructions.push_back(InstructionRecord{
        opcode, currentBlock_, operandBegin,
        narrow(operands.size(), "operand count"), resultBegin,
        narrow(resultViews.size(), "result count"),
        slice.value_or(ObjectSliceRecord{}), symbol, source, auxiliary0,
        auxiliary1});
    ++module_.blocks[currentBlock_].instructionCount;
    return results;
  }

  ValueId emitValue(Opcode opcode, const std::vector<ValueId> &operands,
                    ViewId resultView, std::optional<ObjectSliceRecord> slice,
                    StringId symbol, SourceMapId source,
                    std::uint32_t auxiliary0 = 0,
                    std::uint32_t auxiliary1 = 0) {
    const auto values = emit(opcode, operands, {resultView}, slice, symbol,
                             source, auxiliary0, auxiliary1);
    return values.empty() ? InvalidId : values.front();
  }

  void emitVoid(Opcode opcode, const std::vector<ValueId> &operands,
                std::optional<ObjectSliceRecord> slice, StringId symbol,
                SourceMapId source, std::uint32_t auxiliary0 = 0,
                std::uint32_t auxiliary1 = 0) {
    (void)emit(opcode, operands, {}, slice, symbol, source, auxiliary0,
               auxiliary1);
  }

  ObjectSliceRecord knownSlice(ObjectId object, std::uint32_t offset,
                               std::uint32_t byteLength, AccessKind access) {
    return ObjectSliceRecord{object, InvalidId, InvalidId, offset, byteLength,
                             access, AliasClass::KnownObject};
  }

  ObjectSliceRecord unknownSlice(std::uint32_t byteLength, AccessKind access) {
    return ObjectSliceRecord{InvalidId, InvalidId, InvalidId, 0, byteLength,
                             access, AliasClass::UnknownExternal};
  }

  void addEdge(BlockId from, BlockId to, CfgEdgeKind kind) {
    if (from == InvalidId || to == InvalidId || from >= module_.blocks.size() ||
        to >= module_.blocks.size()) {
      fail("FlowIR CFG edge has an invalid block ID");
      return;
    }
    if (nextId(module_.edges, "CFG edge") == InvalidId) {
      return;
    }
    module_.edges.push_back(CfgEdgeRecord{from, to, kind});
  }

  void emitJump(BlockId target, CfgEdgeKind kind,
                const std::optional<diagnostic::SourceRange> &range) {
    emitVoid(Opcode::Jump, {}, std::nullopt, InvalidId, sourceMap(range));
    addEdge(currentBlock_, target, kind);
    fallsThrough_ = false;
  }

  void emitCleanupTo(std::size_t retainedScopeCount,
                     const std::optional<diagnostic::SourceRange> &range) {
    for (std::size_t index = scopes_.size(); index > retainedScopeCount; --index) {
      for (const auto object : scopes_[index - 1U].automaticObjects) {
        emitVoid(Opcode::LifetimeEnd, {}, knownSlice(object, 0,
            module_.objects[object].byteLength, AccessKind::ReadWrite),
            InvalidId, sourceMap(range));
      }
    }
  }

  void beginScope() { scopes_.push_back({}); }

  void endScopeNormally(const std::optional<diagnostic::SourceRange> &range) {
    if (scopes_.empty()) {
      fail("FlowIR scope stack underflow");
      return;
    }
    if (fallsThrough_) {
      for (const auto object : scopes_.back().automaticObjects) {
        emitVoid(Opcode::LifetimeEnd, {}, knownSlice(object, 0,
            module_.objects[object].byteLength, AccessKind::ReadWrite),
            InvalidId, sourceMap(range));
      }
    }
    scopes_.pop_back();
  }

  void lowerBlock(const hir::Block &block) {
    beginScope();
    for (const auto &statement : block.statements) {
      if (statement) {
        lowerStatement(*statement);
      }
    }
    endScopeNormally(block.sourceRange);
  }

  void ensureStatementBlock(const hir::Stmt &statement) {
    if (fallsThrough_ || dynamic_cast<const hir::Label *>(&statement) != nullptr) {
      return;
    }
    activate(createBlock());
  }

  void lowerFunction(const hir::Function &function) {
    std::vector<std::string> parameterTemplates;
    parameterTemplates.reserve(function.parameters.size());
    for (const auto &parameter : function.parameters) {
      parameterTemplates.push_back(parameter.templateName);
    }
    beginFunction(function.name, parameterTemplates, function.returnTemplateNames,
                  function.sourceRange, &function.parameters,
                  &function.returnByteLengths, &function.effectContract);
    collectLabels(*function.body, 0);
    std::vector<std::string> labels;
    labels.reserve(labelDepths_.size());
    for (const auto &[name, unused] : labelDepths_) {
      (void)unused;
      labels.push_back(name);
    }
    std::sort(labels.begin(), labels.end());
    for (const auto &label : labels) {
      labelBlocks_.emplace(label, createBlock());
    }
    labelScopeBase_ = scopes_.size() + 1U;
    lowerBlock(*function.body);
    finishFunction(function.sourceRange);
  }

  void addExternalFunction(const hir::ExternFunction &function) {
    const auto id = nextId(module_.functions, "function");
    if (id == InvalidId) {
      return;
    }
    module_.functions.push_back(FunctionRecord{internString(function.name), InvalidId,
                                                InvalidId, 0, 0, 0, 0, 0,
                                                sourceMap(function.sourceRange)});
    auto &record = module_.functions[id];
    record.declaredEffects = effectFlags(function.effectContract);
    record.hasExplicitEffectContract = function.effectContract.isExplicit;
    record.parameterViewBegin = narrow(module_.views.size(), "view ID");
    for (std::size_t index = 0; index < function.parameterByteLengths.size(); ++index) {
      const auto templateName = index < function.parameterTemplateNames.size()
                                    ? function.parameterTemplateNames[index]
                                    : std::string{};
      (void)createView(templateName,
                       narrow(function.parameterByteLengths[index], "extern parameter length"),
                       ValueCategory::RValue);
      ++record.parameterViewCount;
    }
    record.returnViewBegin = narrow(module_.views.size(), "view ID");
    for (std::size_t index = 0; index < function.returnByteLengths.size(); ++index) {
      const auto templateName = index < function.returnTemplateNames.size()
                                    ? function.returnTemplateNames[index]
                                    : std::string{};
      (void)createView(templateName,
                       narrow(function.returnByteLengths[index], "extern return length"),
                       ValueCategory::RValue);
      ++record.returnViewCount;
    }
  }

  void beginFunction(
      std::string_view name, const std::vector<std::string> &parameterTemplates,
      const std::vector<std::string> &returnTemplates,
      const std::optional<diagnostic::SourceRange> &range,
      const std::vector<hir::Parameter> *parameters = nullptr,
      const std::vector<std::size_t> *returnLengths = nullptr,
      const hir::EffectContract *effectContract = nullptr) {
    objectsByBinding_ = globalObjectsByBinding_;
    currentFunction_ = nextId(module_.functions, "function");
    if (currentFunction_ == InvalidId) {
      return;
    }
    const auto viewBegin = narrow(module_.views.size(), "view ID");
    module_.functions.push_back(FunctionRecord{internString(name), InvalidId,
                                                narrow(module_.blocks.size(), "block ID"),
                                                0, viewBegin, 0, viewBegin, 0,
                                                sourceMap(range)});
    if (effectContract != nullptr) {
      module_.functions[currentFunction_].declaredEffects = effectFlags(*effectContract);
      module_.functions[currentFunction_].hasExplicitEffectContract =
          effectContract->isExplicit;
    }
    if (parameters) {
      module_.functions[currentFunction_].parameterViewBegin =
          narrow(module_.views.size(), "view ID");
      for (const auto &parameter : *parameters) {
        const auto object = createObject(
            currentFunction_, parameter.bindingName, parameter.byteLength,
            ObjectStorage::Parameter, parameter.templateName, range);
        (void)createView(parameter.templateName, narrow(parameter.byteLength,
                                                          "parameter length"),
                         ValueCategory::RValue, object);
        ++module_.functions[currentFunction_].parameterViewCount;
      }
    } else {
      for (const auto &templateName : parameterTemplates) {
        (void)createView(templateName, 0, ValueCategory::RValue);
        ++module_.functions[currentFunction_].parameterViewCount;
      }
    }
    module_.functions[currentFunction_].returnViewBegin =
        narrow(module_.views.size(), "view ID");
    if (returnLengths) {
      for (std::size_t index = 0; index < returnLengths->size(); ++index) {
        const auto templateName = compatibleViewTemplate(
            index < returnTemplates.size() ? returnTemplates[index] : std::string{},
            narrow((*returnLengths)[index], "return length"));
        (void)createView(templateName, narrow((*returnLengths)[index],
                                               "return length"),
                         ValueCategory::RValue);
        ++module_.functions[currentFunction_].returnViewCount;
      }
    }
    const auto entry = createBlock();
    module_.functions[currentFunction_].entryBlock = entry;
    activate(entry);
    scopes_.clear();
    beginScope();
    loops_.clear();
    exceptionTargets_.clear();
    labelBlocks_.clear();
    labelDepths_.clear();
    uncaughtBlock_ = InvalidId;
  }

  void finishFunction(const std::optional<diagnostic::SourceRange> &range) {
    if (fallsThrough_) {
      std::vector<ValueId> implicitValues;
      const auto &function = module_.functions[currentFunction_];
      for (std::uint32_t index = 0; index < function.returnViewCount; ++index) {
        const auto view = function.returnViewBegin + index;
        const auto value = emitValue(Opcode::ConstantInteger, {}, view,
                                     std::nullopt, internString("0"),
                                     sourceMap(range));
        implicitValues.push_back(value);
      }
      emitCleanupTo(0, range);
      emitVoid(Opcode::Return, implicitValues, std::nullopt, InvalidId,
               sourceMap(range));
      fallsThrough_ = false;
    }
    if (uncaughtBlock_ != InvalidId) {
      activate(uncaughtBlock_);
      emitVoid(Opcode::Unreachable, {}, std::nullopt, InvalidId, sourceMap(range));
      fallsThrough_ = false;
    }
    if (!scopes_.empty()) {
      scopes_.clear();
    }
    currentBlock_ = InvalidId;
    currentFunction_ = InvalidId;
  }

  void collectLabels(const hir::Block &block, std::size_t depth) {
    for (const auto &statement : block.statements) {
      if (!statement) {
        continue;
      }
      if (const auto *label = dynamic_cast<const hir::Label *>(statement.get())) {
        labelDepths_.emplace(label->label, depth);
        continue;
      }
      if (const auto *list = dynamic_cast<const hir::StatementList *>(statement.get())) {
        for (const auto &item : list->statements) {
          if (const auto *nestedLabel = dynamic_cast<const hir::Label *>(item.get())) {
            labelDepths_.emplace(nestedLabel->label, depth);
          }
        }
        continue;
      }
      if (const auto *ifStatement = dynamic_cast<const hir::If *>(statement.get())) {
        collectLabels(*ifStatement->thenBlock, depth + 1U);
        if (ifStatement->elseBlock) {
          collectLabels(*ifStatement->elseBlock, depth + 1U);
        }
      } else if (const auto *whileStatement =
                     dynamic_cast<const hir::While *>(statement.get())) {
        collectLabels(*whileStatement->body, depth + 1U);
      } else if (const auto *forStatement =
                     dynamic_cast<const hir::For *>(statement.get())) {
        collectLabels(*forStatement->body, depth + 1U);
      } else if (const auto *tryCatch =
                     dynamic_cast<const hir::TryCatch *>(statement.get())) {
        collectLabels(*tryCatch->tryBlock, depth + 1U);
        collectLabels(*tryCatch->catchBlock, depth + 1U);
      }
    }
  }

  std::uint32_t expressionLength(const hir::Expr &expression) {
    if (const auto *integer = dynamic_cast<const hir::IntegerLiteral *>(&expression)) {
      return narrow(integer->byteLength, "integer length");
    }
    if (const auto *string = dynamic_cast<const hir::StringLiteral *>(&expression)) {
      return narrow(string->byteLength, "string length");
    }
    if (const auto *floating = dynamic_cast<const hir::FloatLiteral *>(&expression)) {
      return narrow(floating->byteLength, "float length");
    }
    if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
      return narrow(variable->byteLength, "variable length");
    }
    if (const auto *address = dynamic_cast<const hir::AddressOfExpr *>(&expression)) {
      return narrow(address->byteLength, "address length");
    }
    if (const auto *deref = dynamic_cast<const hir::DerefExpr *>(&expression)) {
      return narrow(deref->byteLength, "dereference length");
    }
    if (const auto *binary = dynamic_cast<const hir::BinaryExpr *>(&expression)) {
      return narrow(binary->byteLength, "binary length");
    }
    if (const auto *unary = dynamic_cast<const hir::UnaryExpr *>(&expression)) {
      return narrow(unary->byteLength, "unary length");
    }
    if (const auto *ternary = dynamic_cast<const hir::TernaryExpr *>(&expression)) {
      return narrow(ternary->byteLength, "ternary length");
    }
    if (const auto *unsignedExpr = dynamic_cast<const hir::UnsignedExpr *>(&expression)) {
      return narrow(unsignedExpr->byteLength, "unsigned length");
    }
    if (const auto *cast = dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
      return narrow(cast->byteLength, "cast length");
    }
    if (const auto *view = dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
      return narrow(view->byteLength, "View length");
    }
    if (const auto *call = dynamic_cast<const hir::UserTemplateOpCallExpr *>(&expression)) {
      return narrow(call->byteLength, "call result length");
    }
    if (const auto *binary = dynamic_cast<const hir::FloatBinaryExpr *>(&expression)) {
      return narrow(binary->byteLength, "float binary length");
    }
    if (const auto *compare = dynamic_cast<const hir::FloatCompareExpr *>(&expression)) {
      (void)compare;
      return 1;
    }
    if (const auto *toFloat = dynamic_cast<const hir::ToFloatExpr *>(&expression)) {
      return narrow(toFloat->byteLength, "float conversion length");
    }
    if (const auto *toInt = dynamic_cast<const hir::ToIntExpr *>(&expression)) {
      return narrow(toInt->byteLength, "integer conversion length");
    }
    if (const auto *format = dynamic_cast<const hir::UserTemplateFormatCallExpr *>(&expression)) {
      return narrow(format->byteLength, "format result length");
    }
    if (const auto *call = dynamic_cast<const hir::CallExpr *>(&expression)) {
      return narrow(call->byteLength, "call result length");
    }
    if (const auto *swap = dynamic_cast<const hir::ByteSwapExpr *>(&expression)) {
      return narrow(swap->byteLength, "byte-swap length");
    }
    if (const auto *assignment = dynamic_cast<const hir::AssignmentExpr *>(&expression)) {
      return narrow(assignment->byteLength, "assignment result length");
    }
    return 0;
  }

  ViewId viewOfValue(ValueId value) const {
    if (value == InvalidId || value >= module_.values.size()) {
      return InvalidId;
    }
    return module_.values[value].view;
  }

  ValueId lowerExpression(const hir::Expr &expression) {
    const auto source = sourceMap(expression.sourceRange);
    if (const auto *integer = dynamic_cast<const hir::IntegerLiteral *>(&expression)) {
      const auto length = expressionLength(*integer);
      return emitValue(Opcode::ConstantInteger, {},
                       createView(templateForInteger(length), length,
                                  ValueCategory::RValue),
                       std::nullopt, internString(integer->value), source);
    }
    if (const auto *string = dynamic_cast<const hir::StringLiteral *>(&expression)) {
      const auto length = expressionLength(*string);
      return emitValue(Opcode::ConstantString, {},
                       createView("cstr", length, ValueCategory::RValue),
                       std::nullopt, internString(string->value), source);
    }
    if (const auto *floating = dynamic_cast<const hir::FloatLiteral *>(&expression)) {
      const auto length = expressionLength(*floating);
      return emitValue(Opcode::ConstantFloat, {},
                       createView(templateForFloat(length), length,
                                  ValueCategory::RValue),
                       std::nullopt, internString(floating->value), source);
    }
    if (const auto *variable = dynamic_cast<const hir::VariableRef *>(&expression)) {
      const auto length = expressionLength(*variable);
      const auto object = objectFor(variable->bindingName);
      if (object) {
        return emitValue(Opcode::Load, {},
                         createView(objectSliceTemplate(*object,
                                                        narrow(variable->offset, "object offset"),
                                                        length), length,
                                    ValueCategory::LValue, *object,
                                    narrow(variable->offset, "object offset"),
                                    InterpretationAddressable),
                         knownSlice(*object, narrow(variable->offset, "object offset"),
                                    length, AccessKind::Read),
                         internString(variable->bindingName), source);
      }
      return emitValue(Opcode::Load, {}, createView({}, length, ValueCategory::LValue),
                       unknownSlice(length, AccessKind::Read),
                       internString(variable->bindingName), source);
    }
    if (const auto *address = dynamic_cast<const hir::AddressOfExpr *>(&expression)) {
      const auto length = expressionLength(*address);
      const auto object = objectFor(address->bindingName);
      const auto slice = object
                             ? knownSlice(*object, narrow(address->offset, "object offset"),
                                          narrow(address->targetByteLength, "address target length"),
                                          AccessKind::Read)
                             : unknownSlice(narrow(address->targetByteLength,
                                                   "address target length"), AccessKind::Read);
      return emitValue(Opcode::AddressOf, {},
                       createView("addr", length, ValueCategory::RValue,
                                  InvalidId, 0, InterpretationNone),
                       slice, internString(address->bindingName), source);
    }
    if (const auto *deref = dynamic_cast<const hir::DerefExpr *>(&expression)) {
      const auto address = lowerExpression(*deref->address);
      const auto length = expressionLength(*deref);
      return emitValue(Opcode::Dereference, {address},
                       createView({}, length, ValueCategory::LValue),
                       unknownSlice(length, AccessKind::Read), InvalidId, source);
    }
    if (const auto *binary = dynamic_cast<const hir::BinaryExpr *>(&expression)) {
      const auto left = lowerExpression(*binary->left);
      const auto right = lowerExpression(*binary->right);
      const auto length = expressionLength(*binary);
      return emitValue(Opcode::Binary, {left, right},
                       createView(templateForInteger(length), length,
                                  ValueCategory::RValue),
                       std::nullopt, internString(binary->op), source);
    }
    if (const auto *unary = dynamic_cast<const hir::UnaryExpr *>(&expression)) {
      const auto operand = lowerExpression(*unary->operand);
      const auto length = expressionLength(*unary);
      return emitValue(Opcode::Unary, {operand},
                       createView(templateForInteger(length), length,
                                  ValueCategory::RValue),
                       std::nullopt, internString(unary->op), source);
    }
    if (const auto *ternary = dynamic_cast<const hir::TernaryExpr *>(&expression)) {
      const auto condition = lowerExpression(*ternary->condition);
      const auto thenValue = lowerExpression(*ternary->thenExpr);
      const auto elseValue = lowerExpression(*ternary->elseExpr);
      const auto length = expressionLength(*ternary);
      return emitValue(Opcode::Select, {condition, thenValue, elseValue},
                       createView({}, length, ValueCategory::RValue),
                       std::nullopt, InvalidId, source);
    }
    if (const auto *unsignedExpr = dynamic_cast<const hir::UnsignedExpr *>(&expression)) {
      const auto operand = lowerExpression(*unsignedExpr->operand);
      const auto length = expressionLength(*unsignedExpr);
      return emitValue(Opcode::Convert, {operand},
                       createView(templateForInteger(length, true), length,
                                  ValueCategory::RValue),
                       std::nullopt, internString("unsigned"), source);
    }
    if (const auto *cast = dynamic_cast<const hir::IntegerCastExpr *>(&expression)) {
      const auto operand = lowerExpression(*cast->operand);
      const auto length = expressionLength(*cast);
      return emitValue(Opcode::Convert, {operand},
                       createView(templateForInteger(length, !cast->isSigned), length,
                                  ValueCategory::RValue),
                       std::nullopt, internString("integer_cast"), source);
    }
    if (const auto *view = dynamic_cast<const hir::TemplateViewExpr *>(&expression)) {
      const auto operand = lowerExpression(*view->operand);
      const auto length = expressionLength(*view);
      return emitValue(Opcode::ReinterpretView, {operand},
                       createView(view->templateName, length,
                                  view->isAddressable ? ValueCategory::LValue
                                                      : ValueCategory::RValue,
                                  InvalidId, 0, InterpretationReinterpreted |
                                  (view->isAddressable ? InterpretationAddressable : 0U)),
                       std::nullopt, internString(view->templateName), source);
    }
    if (const auto *call = dynamic_cast<const hir::UserTemplateOpCallExpr *>(&expression)) {
      return lowerCall(call->callee, call->arguments, expressionLength(*call),
                       call->templateName, source);
    }
    if (const auto *binary = dynamic_cast<const hir::FloatBinaryExpr *>(&expression)) {
      const auto left = lowerExpression(*binary->left);
      const auto right = lowerExpression(*binary->right);
      const auto length = expressionLength(*binary);
      return emitValue(Opcode::Binary, {left, right},
                       createView(templateForFloat(length), length,
                                  ValueCategory::RValue),
                       std::nullopt, internString(binary->op), source);
    }
    if (const auto *compare = dynamic_cast<const hir::FloatCompareExpr *>(&expression)) {
      const auto left = lowerExpression(*compare->left);
      const auto right = lowerExpression(*compare->right);
      return emitValue(Opcode::Binary, {left, right},
                       createView("bool", 1, ValueCategory::RValue),
                       std::nullopt, internString(compare->op), source);
    }
    if (const auto *toFloat = dynamic_cast<const hir::ToFloatExpr *>(&expression)) {
      const auto operand = lowerExpression(*toFloat->operand);
      const auto length = expressionLength(*toFloat);
      return emitValue(Opcode::Convert, {operand},
                       createView(templateForFloat(length), length,
                                  ValueCategory::RValue),
                       std::nullopt, internString("to_float"), source);
    }
    if (const auto *toInt = dynamic_cast<const hir::ToIntExpr *>(&expression)) {
      const auto operand = lowerExpression(*toInt->operand);
      const auto length = expressionLength(*toInt);
      return emitValue(Opcode::Convert, {operand},
                       createView(templateForInteger(length, toInt->isUnsigned), length,
                                  ValueCategory::RValue),
                       std::nullopt, internString("to_int"), source);
    }
    if (const auto *format = dynamic_cast<const hir::UserTemplateFormatCallExpr *>(&expression)) {
      std::vector<ValueId> arguments{lowerExpression(*format->value)};
      if (format->file) {
        arguments.push_back(lowerExpression(*format->file));
      }
      const auto length = expressionLength(*format);
      return emitValue(Opcode::Call, arguments,
                       createView("i32", length, ValueCategory::RValue),
                       std::nullopt, internString(format->callee), source,
                       static_cast<std::uint32_t>(format->sink));
    }
    if (const auto *call = dynamic_cast<const hir::CallExpr *>(&expression)) {
      return lowerCall(call->callee, call->arguments, expressionLength(*call),
                       !call->resultTemplateName.empty()
                           ? call->resultTemplateName
                           : (call->isFloating
                                  ? templateForFloat(expressionLength(*call))
                                  : std::string{}),
                       source,
                       static_cast<std::uint32_t>(call->builtin));
    }
    if (const auto *dynamic = dynamic_cast<const hir::DynamicByteViewExpr *>(&expression)) {
      std::vector<ValueId> operands{lowerExpression(*dynamic->source)};
      if (dynamic->runtimeLength) {
        operands.push_back(lowerExpression(*dynamic->runtimeLength));
      }
      return emitValue(Opcode::DynamicView, operands,
                       createView({}, 0, ValueCategory::RValue, InvalidId, 0,
                                  InterpretationDynamicLength),
                       std::nullopt, internString(toString(dynamic->operation)), source);
    }
    if (const auto *swap = dynamic_cast<const hir::ByteSwapExpr *>(&expression)) {
      const auto operand = lowerExpression(*swap->source);
      const auto length = expressionLength(*swap);
      const auto templateName = viewTemplate(viewOfValue(operand));
      return emitValue(Opcode::ByteSwap, {operand},
                       createView(templateName, length, ValueCategory::RValue),
                       std::nullopt, InvalidId, source);
    }
    if (const auto *assignment = dynamic_cast<const hir::AssignmentExpr *>(&expression)) {
      for (const auto &store : assignment->stores) {
        if (store) {
          lowerStatement(*store);
        }
      }
      return lowerExpression(*assignment->result);
    }
    fail("FlowIR builder encountered an unsupported HIR expression");
    return InvalidId;
  }

  ValueId lowerCall(std::string_view callee,
                    const std::vector<std::unique_ptr<hir::Expr>> &arguments,
                    std::uint32_t resultLength, std::string_view templateName,
                    SourceMapId source, std::uint32_t auxiliary = 0) {
    std::vector<ValueId> values;
    values.reserve(arguments.size());
    for (const auto &argument : arguments) {
      values.push_back(lowerExpression(*argument));
    }
    return emitValue(Opcode::Call, values,
                     createView(templateName, resultLength,
                                ValueCategory::RValue),
                     std::nullopt, internString(callee), source, auxiliary);
  }

  void emitStore(std::string_view bindingName, std::size_t byteLength,
                 std::size_t offset, ValueId value,
                 const std::optional<diagnostic::SourceRange> &range) {
    const auto length = narrow(byteLength, "store length");
    const auto object = objectFor(bindingName);
    emitVoid(Opcode::Store, {value},
             object ? knownSlice(*object, narrow(offset, "store offset"), length,
                                 AccessKind::Write)
                    : unknownSlice(length, AccessKind::Write),
             internString(bindingName), sourceMap(range));
  }

  void lowerStatement(const hir::Stmt &statement) {
    ensureStatementBlock(statement);
    const auto source = statement.sourceRange;
    if (const auto *list = dynamic_cast<const hir::StatementList *>(&statement)) {
      for (const auto &item : list->statements) {
        if (item) {
          lowerStatement(*item);
        }
      }
      return;
    }
    if (const auto *local = dynamic_cast<const hir::LocalMemory *>(&statement)) {
      const auto object = createObject(currentFunction_, local->bindingName,
                                       local->byteLength, toObjectStorage(local->storage),
                                       local->templateName, source);
      emitVoid(Opcode::LifetimeStart, {},
               knownSlice(object, 0, narrow(local->byteLength, "local length"),
                          AccessKind::ReadWrite),
               internString(local->bindingName), sourceMap(source));
      if (local->storage != hir::MemoryStorage::StaticLocal && !scopes_.empty()) {
        scopes_.back().automaticObjects.push_back(object);
      }
      return;
    }
    if (const auto *store = dynamic_cast<const hir::IntegerStore *>(&statement)) {
      emitStore(store->bindingName, store->targetByteLength, store->offset,
                lowerExpression(*store->value), source);
      return;
    }
    if (const auto *store = dynamic_cast<const hir::FloatStore *>(&statement)) {
      emitStore(store->bindingName, store->targetByteLength, store->offset,
                lowerExpression(*store->value), source);
      return;
    }
    if (const auto *store = dynamic_cast<const hir::StringStore *>(&statement)) {
      const auto length = narrow(store->targetByteLength, "string store length");
      const auto value = emitValue(Opcode::ConstantString, {},
                                   createView("cstr", length, ValueCategory::RValue),
                                   std::nullopt, internString(store->value), sourceMap(source));
      emitStore(store->bindingName, store->targetByteLength, store->offset, value, source);
      return;
    }
    if (const auto *store = dynamic_cast<const hir::StringCopyStore *>(&statement)) {
      const auto sourceObject = objectFor(store->sourceBindingName);
      const auto length = narrow(store->sourceByteLength, "string copy length");
      const auto value = emitValue(Opcode::Load, {},
          createView(sourceObject ? objectSliceTemplate(*sourceObject,
                                                         narrow(store->sourceOffset, "string copy offset"),
                                                         length)
                                  : "cstr", length,
                     ValueCategory::LValue, sourceObject.value_or(InvalidId),
                     narrow(store->sourceOffset, "string copy offset"),
                     InterpretationAddressable),
          sourceObject ? knownSlice(*sourceObject,
              narrow(store->sourceOffset, "string copy offset"), length, AccessKind::Read)
                       : unknownSlice(length, AccessKind::Read),
          internString(store->sourceBindingName), sourceMap(source));
      emitStore(store->bindingName, store->targetByteLength, store->targetOffset,
                value, source);
      return;
    }
    if (const auto *store = dynamic_cast<const hir::BoolStore *>(&statement)) {
      emitStore(store->bindingName, store->targetByteLength, store->offset,
                lowerExpression(*store->value), source);
      return;
    }
    if (const auto *store = dynamic_cast<const hir::PointerStore *>(&statement)) {
      const auto address = lowerExpression(*store->address);
      const auto value = lowerExpression(*store->value);
      emitVoid(Opcode::Store, {address, value},
               unknownSlice(narrow(store->targetByteLength, "pointer store length"),
                            AccessKind::Write),
               InvalidId, sourceMap(source));
      return;
    }
    if (const auto *call = dynamic_cast<const hir::Call *>(&statement)) {
      std::vector<ValueId> arguments;
      for (const auto &argument : call->arguments) {
        arguments.push_back(lowerExpression(*argument));
      }
      emitVoid(Opcode::Call, arguments, std::nullopt, internString(call->callee),
               sourceMap(source), static_cast<std::uint32_t>(call->builtin));
      return;
    }
    if (const auto *call = dynamic_cast<const hir::UserTemplateOpCall *>(&statement)) {
      std::vector<ValueId> arguments;
      for (const auto &argument : call->arguments) {
        arguments.push_back(lowerExpression(*argument));
      }
      emitVoid(Opcode::Call, arguments, std::nullopt, internString(call->callee),
               sourceMap(source), narrow(call->resultByteLength, "call result length"));
      return;
    }
    if (const auto *call = dynamic_cast<const hir::UserTemplateFormatCall *>(&statement)) {
      std::vector<ValueId> arguments{lowerExpression(*call->value)};
      if (call->file) {
        arguments.push_back(lowerExpression(*call->file));
      }
      emitVoid(Opcode::Call, arguments, std::nullopt, internString(call->callee),
               sourceMap(source), static_cast<std::uint32_t>(call->sink));
      return;
    }
    if (const auto *call = dynamic_cast<const hir::MultiReturnCallStore *>(&statement)) {
      std::vector<ValueId> arguments;
      for (const auto &argument : call->arguments) {
        arguments.push_back(lowerExpression(*argument));
      }
      std::vector<ViewId> resultViews;
      for (const auto &target : call->targets) {
        resultViews.push_back(createView({}, narrow(target.byteLength, "return length"),
                                         ValueCategory::RValue));
      }
      const auto results = emit(Opcode::Call, arguments, resultViews, std::nullopt,
                                internString(call->callee), sourceMap(source));
      for (std::size_t index = 0; index < call->targets.size() && index < results.size(); ++index) {
        const auto &target = call->targets[index];
        emitStore(target.bindingName, target.byteLength, 0, results[index], source);
      }
      return;
    }
    if (const auto *input = dynamic_cast<const hir::InputCallStore *>(&statement)) {
      std::vector<ValueId> arguments;
      if (input->file) {
        arguments.push_back(lowerExpression(*input->file));
      }
      arguments.push_back(lowerExpression(*input->format));
      emitVoid(Opcode::Input, arguments, std::nullopt, internString(input->callee),
               sourceMap(source), static_cast<std::uint32_t>(input->builtin));
      for (const auto &target : input->countTargets) {
        const auto value = emitValue(Opcode::Load, {},
            createView(target.templateName, narrow(target.byteLength, "input target length"),
                       ValueCategory::LValue),
            unknownSlice(narrow(target.byteLength, "input target length"), AccessKind::Read),
            internString(target.bindingName), sourceMap(source));
        emitStore(target.bindingName, target.byteLength, target.offset, value, source);
      }
      for (const auto &target : input->scanTargets) {
        const auto value = emitValue(Opcode::Load, {},
            createView(target.templateName, narrow(target.byteLength, "input target length"),
                       ValueCategory::LValue),
            unknownSlice(narrow(target.byteLength, "input target length"), AccessKind::Read),
            internString(target.bindingName), sourceMap(source));
        emitStore(target.bindingName, target.byteLength, target.offset, value, source);
      }
      return;
    }
    if (const auto *ret = dynamic_cast<const hir::Return *>(&statement)) {
      std::vector<ValueId> values;
      for (const auto &value : ret->values) {
        values.push_back(lowerExpression(*value));
      }
      emitCleanupTo(0, source);
      emitVoid(Opcode::Return, values, std::nullopt, InvalidId, sourceMap(source));
      fallsThrough_ = false;
      return;
    }
    if (const auto *ifStatement = dynamic_cast<const hir::If *>(&statement)) {
      lowerIf(*ifStatement);
      return;
    }
    if (const auto *whileStatement = dynamic_cast<const hir::While *>(&statement)) {
      lowerWhile(*whileStatement);
      return;
    }
    if (const auto *forStatement = dynamic_cast<const hir::For *>(&statement)) {
      lowerFor(*forStatement);
      return;
    }
    if (dynamic_cast<const hir::Break *>(&statement) != nullptr) {
      if (loops_.empty()) {
        fail("FlowIR builder found break without loop target");
        return;
      }
      const auto target = loops_.back();
      emitCleanupTo(target.cleanupScopeCount, source);
      emitJump(target.breakBlock, CfgEdgeKind::Cleanup, source);
      return;
    }
    if (dynamic_cast<const hir::Continue *>(&statement) != nullptr) {
      if (loops_.empty()) {
        fail("FlowIR builder found continue without loop target");
        return;
      }
      const auto target = loops_.back();
      emitCleanupTo(target.cleanupScopeCount, source);
      emitJump(target.continueBlock, CfgEdgeKind::Cleanup, source);
      return;
    }
    if (const auto *gotoStatement = dynamic_cast<const hir::Goto *>(&statement)) {
      const auto found = labelBlocks_.find(gotoStatement->label);
      const auto depth = labelDepths_.find(gotoStatement->label);
      if (found == labelBlocks_.end() || depth == labelDepths_.end()) {
        fail("FlowIR builder found unresolved label '" + gotoStatement->label + "'");
        return;
      }
      emitCleanupTo(labelScopeBase_ + depth->second, source);
      emitJump(found->second, CfgEdgeKind::Cleanup, source);
      return;
    }
    if (const auto *label = dynamic_cast<const hir::Label *>(&statement)) {
      const auto target = labelBlocks_.find(label->label);
      if (target == labelBlocks_.end()) {
        fail("FlowIR builder found an unregistered label '" + label->label + "'");
        return;
      }
      if (fallsThrough_) {
        emitJump(target->second, CfgEdgeKind::Normal, source);
      }
      activate(target->second);
      lowerStatement(*label->statement);
      return;
    }
    if (const auto *throwStatement = dynamic_cast<const hir::Throw *>(&statement)) {
      if (throwStatement->delivery) {
        lowerStatement(*throwStatement->delivery);
      }
      emitCleanupTo(0, source);
      const auto target = exceptionTargets_.empty() ? ensureUncaughtBlock() :
                                                      exceptionTargets_.back();
      emitVoid(Opcode::Throw, {}, std::nullopt,
               internString(throwStatement->sourceTemplateName), sourceMap(source),
               narrow(throwStatement->sourceByteLength, "throw source length"),
               narrow(throwStatement->targetByteLength, "throw target length"));
      addEdge(currentBlock_, target, CfgEdgeKind::Exceptional);
      fallsThrough_ = false;
      return;
    }
    if (const auto *tryCatch = dynamic_cast<const hir::TryCatch *>(&statement)) {
      lowerTryCatch(*tryCatch);
      return;
    }
    fail("FlowIR builder encountered an unsupported HIR statement");
  }

  void lowerIf(const hir::If &statement) {
    const auto condition = lowerExpression(*statement.condition);
    const auto thenBlock = createBlock();
    const auto elseBlock = createBlock();
    const auto joinBlock = createBlock();
    emitVoid(Opcode::Branch, {condition}, std::nullopt, InvalidId,
             sourceMap(statement.sourceRange));
    addEdge(currentBlock_, thenBlock, CfgEdgeKind::True);
    addEdge(currentBlock_, elseBlock, CfgEdgeKind::False);

    activate(thenBlock);
    lowerBlock(*statement.thenBlock);
    const bool thenFalls = fallsThrough_;
    if (thenFalls) {
      emitJump(joinBlock, CfgEdgeKind::Normal, statement.sourceRange);
    }

    activate(elseBlock);
    if (statement.elseBlock) {
      lowerBlock(*statement.elseBlock);
    }
    const bool elseFalls = fallsThrough_;
    if (elseFalls) {
      emitJump(joinBlock, CfgEdgeKind::Normal, statement.sourceRange);
    }

    activate(joinBlock);
    fallsThrough_ = thenFalls || elseFalls;
  }

  void lowerWhile(const hir::While &statement) {
    const auto header = createBlock();
    const auto body = createBlock();
    const auto after = createBlock();
    emitJump(header, CfgEdgeKind::Normal, statement.sourceRange);
    activate(header);
    const auto condition = lowerExpression(*statement.condition);
    emitVoid(Opcode::Branch, {condition}, std::nullopt, InvalidId,
             sourceMap(statement.sourceRange));
    addEdge(header, body, CfgEdgeKind::True);
    addEdge(header, after, CfgEdgeKind::False);
    loops_.push_back(LoopTarget{after, header, scopes_.size()});
    activate(body);
    lowerBlock(*statement.body);
    if (fallsThrough_) {
      emitJump(header, CfgEdgeKind::LoopBack, statement.sourceRange);
    }
    loops_.pop_back();
    activate(after);
  }

  void lowerFor(const hir::For &statement) {
    beginScope();
    if (statement.init) {
      lowerStatement(*statement.init);
    }
    const auto header = createBlock();
    const auto body = createBlock();
    const auto post = createBlock();
    const auto after = createBlock();
    emitJump(header, CfgEdgeKind::Normal, statement.sourceRange);
    activate(header);
    if (statement.condition) {
      const auto condition = lowerExpression(*statement.condition);
      emitVoid(Opcode::Branch, {condition}, std::nullopt, InvalidId,
               sourceMap(statement.sourceRange));
      addEdge(header, body, CfgEdgeKind::True);
      addEdge(header, after, CfgEdgeKind::False);
    } else {
      emitJump(body, CfgEdgeKind::Normal, statement.sourceRange);
    }
    loops_.push_back(LoopTarget{after, post, scopes_.size()});
    activate(body);
    lowerBlock(*statement.body);
    if (fallsThrough_) {
      emitJump(post, CfgEdgeKind::Normal, statement.sourceRange);
    }
    activate(post);
    for (const auto &item : statement.post) {
      if (item) {
        lowerStatement(*item);
      }
    }
    if (fallsThrough_) {
      emitJump(header, CfgEdgeKind::LoopBack, statement.sourceRange);
    }
    loops_.pop_back();
    activate(after);
    endScopeNormally(statement.sourceRange);
  }

  BlockId ensureUncaughtBlock() {
    if (uncaughtBlock_ == InvalidId) {
      uncaughtBlock_ = createBlock();
    }
    return uncaughtBlock_;
  }

  void lowerTryCatch(const hir::TryCatch &statement) {
    const auto tryBlock = createBlock();
    const auto catchBlock = createBlock();
    const auto joinBlock = createBlock();
    const auto errorObject = createObject(currentFunction_, statement.errorBindingName,
        statement.errorByteLength, ObjectStorage::Catch, statement.errorTemplateName,
        statement.sourceRange);
    emitJump(tryBlock, CfgEdgeKind::Normal, statement.sourceRange);

    activate(tryBlock);
    emitVoid(Opcode::LifetimeStart, {},
             knownSlice(errorObject, 0, narrow(statement.errorByteLength,
                                                "catch length"), AccessKind::ReadWrite),
             internString(statement.errorBindingName), sourceMap(statement.sourceRange));
    exceptionTargets_.push_back(catchBlock);
    lowerBlock(*statement.tryBlock);
    exceptionTargets_.pop_back();
    const bool tryFalls = fallsThrough_;
    if (tryFalls) {
      emitVoid(Opcode::LifetimeEnd, {},
               knownSlice(errorObject, 0, narrow(statement.errorByteLength,
                                                  "catch length"), AccessKind::ReadWrite),
               internString(statement.errorBindingName), sourceMap(statement.sourceRange));
      emitJump(joinBlock, CfgEdgeKind::Normal, statement.sourceRange);
    }

    activate(catchBlock);
    emitVoid(Opcode::Catch, {},
             knownSlice(errorObject, 0, narrow(statement.errorByteLength,
                                                "catch length"), AccessKind::Read),
             internString(statement.errorBindingName), sourceMap(statement.sourceRange));
    lowerBlock(*statement.catchBlock);
    const bool catchFalls = fallsThrough_;
    if (catchFalls) {
      emitVoid(Opcode::LifetimeEnd, {},
               knownSlice(errorObject, 0, narrow(statement.errorByteLength,
                                                  "catch length"), AccessKind::ReadWrite),
               internString(statement.errorBindingName), sourceMap(statement.sourceRange));
      emitJump(joinBlock, CfgEdgeKind::Normal, statement.sourceRange);
    }

    activate(joinBlock);
    fallsThrough_ = tryFalls || catchFalls;
  }

  void finalizeEdges() {
    std::vector<std::vector<CfgEdgeId>> successors(module_.blocks.size());
    std::vector<std::vector<CfgEdgeId>> predecessors(module_.blocks.size());
    for (CfgEdgeId id = 0; id < module_.edges.size(); ++id) {
      const auto &edge = module_.edges[id];
      if (edge.from < successors.size() && edge.to < predecessors.size()) {
        successors[edge.from].push_back(id);
        predecessors[edge.to].push_back(id);
      }
    }
    for (BlockId block = 0; block < module_.blocks.size(); ++block) {
      auto &record = module_.blocks[block];
      record.successorBegin = narrow(module_.successorEdges.size(), "successor ID");
      record.successorCount = narrow(successors[block].size(), "successor count");
      module_.successorEdges.insert(module_.successorEdges.end(),
                                    successors[block].begin(), successors[block].end());
      record.predecessorBegin = narrow(module_.predecessorEdges.size(), "predecessor ID");
      record.predecessorCount = narrow(predecessors[block].size(), "predecessor count");
      module_.predecessorEdges.insert(module_.predecessorEdges.end(),
                                      predecessors[block].begin(), predecessors[block].end());
      if (record.firstInstruction == InvalidId) {
        record.firstInstruction = narrow(module_.instructions.size(), "instruction ID");
      }
    }
  }
};

} // namespace

BuildResult build(const hir::TranslationUnit &unit) {
  return FlowBuilder{}.run(unit);
}

} // namespace hitsimple::flowir
