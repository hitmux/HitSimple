#include "SemaAnalyzer.h"

#include "hitsimple/literal/Literal.h"

#include <cctype>
#include <cstdint>
#include <limits>
#include <utility>

namespace hitsimple::sema {
namespace {

struct FormatItem {
  char specifier = '\0';
  std::size_t floatByteLength = 0;
};

struct FormatParseResult {
  std::vector<FormatItem> items;
  std::string diagnostic;
};

bool isVarargStatementBuiltin(stdlib::BuiltinId builtin) {
  using enum stdlib::BuiltinId;
  return builtin == Print || builtin == Printf || builtin == Scanf ||
         builtin == Fprintf || builtin == Fscanf;
}

bool isInputBuiltin(stdlib::BuiltinId builtin) {
  return builtin == stdlib::BuiltinId::Scanf ||
         builtin == stdlib::BuiltinId::Fscanf;
}

bool isFloatTemplate(std::string_view name) {
  return name == "f16" || name == "f32" || name == "f64" || name == "f128";
}

bool isPointerTemplate(std::string_view name) {
  return name == "addr" || name == "handle";
}

bool isStandardIntegerTemplate(std::string_view name) {
  return name == "i8" || name == "i16" || name == "i32" ||
         name == "i64" || name == "u8" || name == "u16" ||
         name == "u32" || name == "u64";
}

bool isUnsignedIntegerTemplate(std::string_view name) {
  return name == "u8" || name == "u16" || name == "u32" || name == "u64";
}

bool integerLiteralFits(const hir::IntegerLiteral &literal,
                        std::size_t byteLength, bool isUnsigned) {
  const auto value = literal::parseUnsignedIntegerLiteral(literal.value);
  if (!value || byteLength == 0 || byteLength > 8) {
    return false;
  }
  if (byteLength == 8) {
    return isUnsigned ||
           *value <= static_cast<std::uint64_t>(
                         std::numeric_limits<std::int64_t>::max());
  }
  const auto bits = byteLength * 8U;
  const auto maximum = isUnsigned
                           ? (std::uint64_t{1} << bits) - 1U
                           : (std::uint64_t{1} << (bits - 1U)) - 1U;
  return *value <= maximum;
}

std::optional<std::string> printfFormatForTemplate(std::string_view name) {
  if (name == "bool") {
    return "\"%d\"";
  }
  if (name == "addr" || name == "handle") {
    return "\"%p\"";
  }
  if (name == "cstr") {
    return "\"%s\"";
  }
  if (name == "i8" || name == "i16" || name == "i32" ||
      name == "i64") {
    return "\"%d\"";
  }
  if (name == "u8" || name == "u16" || name == "u32" ||
      name == "u64") {
    return "\"%u\"";
  }
  if (name == "f16" || name == "f32" || name == "f64" ||
      name == "f128") {
    return "\"%f\"";
  }
  return std::nullopt;
}

std::size_t firstFormatArgumentIndex(stdlib::BuiltinId builtin) {
  if (builtin == stdlib::BuiltinId::Fprintf ||
      builtin == stdlib::BuiltinId::Fscanf) {
    return 1;
  }
  return 0;
}

bool isIntegerInputSpecifier(char specifier) {
  return specifier == 'd' || specifier == 'u' || specifier == 'x' ||
         specifier == 'o' || specifier == 'b';
}

FormatParseResult parseLiteralFormat(std::string_view literal) {
  FormatParseResult result;
  const auto decoded = literal::decodeStringLiteral(literal);
  if (!decoded) {
    result.diagnostic = "invalid format string: " + *decoded.error;
    return result;
  }

  const std::string &format = decoded.bytes;
  for (std::size_t index = 0; index < format.size(); ++index) {
    if (format[index] != '%') {
      continue;
    }
    ++index;
    if (index >= format.size()) {
      result.diagnostic = "unterminated format specifier";
      return result;
    }
    if (format[index] == '%') {
      continue;
    }

    std::size_t floatByteLength = 0;
    std::size_t digitsBegin = index;
    while (index < format.size() &&
           std::isdigit(static_cast<unsigned char>(format[index]))) {
      ++index;
    }
    if (digitsBegin != index) {
      const auto text =
          std::string_view(format).substr(digitsBegin, index - digitsBegin);
      floatByteLength = parseByteLength(text);
    }
    if (index >= format.size()) {
      result.diagnostic = "unterminated format specifier";
      return result;
    }

    const char specifier = format[index];
    if (specifier == 'f') {
      if (floatByteLength == 0) {
        floatByteLength = 8;
      }
      if (floatByteLength != 2 && floatByteLength != 4 &&
          floatByteLength != 8 && floatByteLength != 16) {
        result.diagnostic = "float format byte length must be 2, 4, 8, or 16";
        return result;
      }
      result.items.push_back(FormatItem{specifier, floatByteLength});
      continue;
    }

    if (floatByteLength != 0) {
      result.diagnostic = "format byte length is only valid for %f";
      return result;
    }

    if (isIntegerInputSpecifier(specifier) || specifier == 's' ||
        specifier == 'p' || specifier == 'c') {
      result.items.push_back(FormatItem{specifier, 0});
      continue;
    }

    result.diagnostic =
        "unknown format specifier '%" + std::string(1, specifier) + "'";
    return result;
  }
  return result;
}

std::optional<std::string> validateInputTarget(const FormatItem &item,
                                               const hir::Expr &argument) {
  const auto *address = dynamic_cast<const hir::AddressOfExpr *>(&argument);
  if (address == nullptr) {
    return "scanf/fscanf target must be a writable address";
  }

  const auto targetLength = address->targetByteLength;
  if (item.specifier == 'c') {
    if (targetLength < 1) {
      return "scanf/fscanf %c target must have at least 1 byte";
    }
    return std::nullopt;
  }
  if (item.specifier == 's') {
    if (targetLength < 1) {
      return "scanf/fscanf %s target must leave room for a NUL terminator";
    }
    return std::nullopt;
  }
  if (item.specifier == 'p') {
    if (targetLength != pointerByteLength()) {
      return "scanf/fscanf %p target must match pointer length";
    }
    return std::nullopt;
  }
  if (item.specifier == 'f') {
    if (targetLength != item.floatByteLength) {
      return "scanf/fscanf float target byte length does not match format";
    }
    return std::nullopt;
  }
  if (isIntegerInputSpecifier(item.specifier)) {
    if (targetLength != 1 && targetLength != 2 && targetLength != 4 &&
        targetLength != 8) {
      return "scanf/fscanf integer target byte length must be 1, 2, 4, or 8";
    }
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<std::string> validateInputTarget(const FormatItem &item,
                                               const MemoryReference &target) {
  const auto targetLength = target.byteLength;
  if (item.specifier == 'c') {
    if (targetLength < 1) {
      return "scanf/fscanf %c target must have at least 1 byte";
    }
    return std::nullopt;
  }
  if (item.specifier == 's') {
    if (targetLength < 2) {
      return "scanf/fscanf %s target must leave room for a NUL terminator";
    }
    return std::nullopt;
  }
  if (item.specifier == 'p') {
    if (targetLength != pointerByteLength()) {
      return "scanf/fscanf %p target must match pointer length";
    }
    return std::nullopt;
  }
  if (item.specifier == 'f') {
    if (targetLength != item.floatByteLength) {
      return "scanf/fscanf float target byte length does not match format";
    }
    return std::nullopt;
  }
  if (isIntegerInputSpecifier(item.specifier)) {
    if (targetLength != 1 && targetLength != 2 && targetLength != 4 &&
        targetLength != 8) {
      return "scanf/fscanf integer target byte length must be 1, 2, 4, or 8";
    }
    return std::nullopt;
  }
  return std::nullopt;
}

} // namespace

bool Analyzer::isStandardTemplateWithFormat(std::string_view name) const {
  if (name == "bool" || name == "addr" || name == "handle" ||
      name == "cstr" || name == "bytes") {
    return true;
  }
  return name == "i8" || name == "i16" || name == "i32" ||
         name == "i64" || name == "u8" || name == "u16" ||
         name == "u32" || name == "u64" || name == "f16" ||
         name == "f32" || name == "f64" || name == "f128";
}

std::unique_ptr<hir::Stmt> Analyzer::analyzeCall(const ast::CallExpr &call) {
  return analyzeCallStatement(call);
}

std::unique_ptr<hir::Stmt>
Analyzer::analyzeCallStatement(const ast::CallExpr &call) {
  if (stdlib::isRemovedLegacyName(call.callee)) {
    addDiagnostic(
        "legacy standard library name '" + call.callee +
        "' is not accepted; use " +
        std::string(stdlib::replacementForRemovedLegacyName(call.callee)));
    return nullptr;
  }
  if (rejectUnavailableStandardBuiltin(call)) {
    return nullptr;
  }
  const auto builtin = builtinForCall(call).value_or(stdlib::BuiltinId::None);
  const auto formatIndex = firstFormatArgumentIndex(builtin);
  const bool isFormatOutput = builtin == stdlib::BuiltinId::Print ||
                              builtin == stdlib::BuiltinId::Printf ||
                              builtin == stdlib::BuiltinId::Fprintf;
  if (isFormatOutput && call.arguments.size() == formatIndex + 1U) {
    if (builtin == stdlib::BuiltinId::Fprintf &&
        !isHandleExpression(*call.arguments.front())) {
      addDiagnostic("fprintf file argument must be a handle");
      return nullptr;
    }
    if (const auto templateName =
            expressionTemplateName(*call.arguments[formatIndex])) {
      auto lowered = lowerUserTemplateFormatCall(call, builtin, *templateName,
                                                 formatIndex);
      if (!lowered) {
        return nullptr;
      }
      return std::make_unique<hir::UserTemplateFormatCall>(
          std::move(lowered->callee), std::move(lowered->value), lowered->sink,
          std::move(lowered->file), lowered->resultByteLength);
    }
  }
  if (builtin == stdlib::BuiltinId::Print && call.arguments.size() == 1U) {
    const auto &value = *call.arguments[0];
    if (const auto *as = dynamic_cast<const ast::AsExpr *>(&value);
        as != nullptr && as->templateName == "none") {
      auto value = analyze(*as->operand);
      if (!value) {
        return nullptr;
      }
      std::vector<std::unique_ptr<hir::Expr>> arguments;
      arguments.push_back(std::move(value));
      return std::make_unique<hir::Call>("put", std::move(arguments),
                                         stdlib::BuiltinId::Put);
    }
    if (const auto templateName = operatorTemplateName(value);
        templateName && isStandardTemplateWithFormat(*templateName)) {
      auto lowered = lowerStandardTemplatePrintCall(value, *templateName);
      if (!lowered) {
        return nullptr;
      }
      return std::make_unique<hir::Call>(
          std::move(lowered->callee), std::move(lowered->arguments),
          lowered->builtin, std::move(lowered->formatArgumentKinds));
    }
  }

  if (isInputBuiltin(builtin)) {
    addDiagnostic(
        "scanf/fscanf must be used as a left-context multi-assignment");
    return nullptr;
  }

  if (!isVarargStatementBuiltin(builtin)) {
    const auto found = functions_.find(call.callee);
    if (found == functions_.end()) {
      if (isUnsupportedStandardFunction(call.callee)) {
        addDiagnostic("standard library function '" + call.callee +
                      "' is not supported yet");
        return nullptr;
      }
      addDiagnostic("unsupported function call '" + call.callee + "'");
      return nullptr;
    }
    if (!found->second.returnByteLengths.empty() &&
        builtin != stdlib::BuiltinId::Memset &&
        builtin != stdlib::BuiltinId::Put) {
      addDiagnostic("function call '" + call.callee +
                    "' returns a value and cannot be used as a statement");
      return nullptr;
    }
    auto arguments = analyzeCallArguments(call, found->second);
    if (!result_.diagnostics.empty()) {
      return nullptr;
    }
    return std::make_unique<hir::Call>(
        call.callee, std::move(arguments), builtin,
        std::vector<hir::FormatArgKind>{}, builtinOverloadIndex(call, builtin));
  }

  if (builtin == stdlib::BuiltinId::Print) {
    if (call.arguments.empty()) {
      addDiagnostic("print requires a format argument");
      return nullptr;
    }
  }

  if (call.arguments.size() <= formatIndex) {
    addDiagnostic("function call '" + call.callee +
                  "' requires a format string");
    return nullptr;
  }
  if (builtin == stdlib::BuiltinId::Fprintf &&
      !isHandleExpression(*call.arguments.front())) {
    addDiagnostic("fprintf file argument must be a handle");
    return nullptr;
  }
  if ((builtin == stdlib::BuiltinId::Printf ||
       builtin == stdlib::BuiltinId::Fprintf) &&
      !expressionTemplateName(*call.arguments[formatIndex]) &&
      !isCStringExpression(*call.arguments[formatIndex])) {
    addDiagnostic("function call '" + call.callee + "' argument " +
                  std::to_string(formatIndex + 1U) + " must be a cstr View");
    return nullptr;
  }
  if (isFormatOutput && expressionTemplateName(*call.arguments[formatIndex])) {
    addDiagnostic("user template formatting requires a direct print(value), "
                  "printf(value), or fprintf(file, value) call");
    return nullptr;
  }
  if (isFormatOutput) {
    for (std::size_t index = formatIndex + 1U; index < call.arguments.size();
         ++index) {
      if (expressionTemplateName(*call.arguments[index])) {
        addDiagnostic(
            "user template formatting requires a direct print(value), "
            "printf(value), or fprintf(file, value) call");
        return nullptr;
      }
    }
  }

  std::vector<std::unique_ptr<hir::Expr>> arguments;
  std::vector<hir::FormatArgKind> formatArgumentKinds;
  for (const auto &argument : call.arguments) {
    auto lowered = analyze(*argument);
    if (!lowered) {
      return nullptr;
    }
    formatArgumentKinds.push_back(formatArgumentKind(*argument, *lowered));
    arguments.push_back(std::move(lowered));
  }

  const auto *format =
      dynamic_cast<hir::StringLiteral *>(arguments[formatIndex].get());
  if (format == nullptr) {
    return std::make_unique<hir::Call>(call.callee, std::move(arguments),
                                       builtin, std::move(formatArgumentKinds),
                                       builtinOverloadIndex(call, builtin));
  }

  const auto parsedFormat = parseLiteralFormat(format->value);
  if (!parsedFormat.diagnostic.empty()) {
    addDiagnostic("function call '" + call.callee +
                  "' has invalid format string: " + parsedFormat.diagnostic);
    return nullptr;
  }

  const auto varargCount = call.arguments.size() - formatIndex - 1U;
  if (varargCount != parsedFormat.items.size()) {
    addDiagnostic("function call '" + call.callee +
                  "' format item count does not match argument count");
    return nullptr;
  }

  if (isInputBuiltin(builtin)) {
    for (std::size_t index = 0; index < parsedFormat.items.size(); ++index) {
      if (const auto diagnostic =
              validateInputTarget(parsedFormat.items[index],
                                  *arguments[formatIndex + 1U + index])) {
        addDiagnostic(*diagnostic);
        return nullptr;
      }
    }
  }

  for (std::size_t index = 0; index < parsedFormat.items.size(); ++index) {
    const auto &argument = *call.arguments[formatIndex + 1U + index];
    if (isHandleExpression(argument) &&
        parsedFormat.items[index].specifier != 'p') {
      addDiagnostic("handle values can only be formatted with %p");
      return nullptr;
    }
  }

  return std::make_unique<hir::Call>(call.callee, std::move(arguments), builtin,
                                     std::move(formatArgumentKinds),
                                     builtinOverloadIndex(call, builtin));
}

std::optional<AssignmentLowering>
Analyzer::lowerInputLeftContext(const ast::AssignmentExpr &assign,
                                const ast::CallExpr &call) {
  const auto builtin = builtinForCall(call).value_or(stdlib::BuiltinId::None);
  const bool hasFile = builtin == stdlib::BuiltinId::Fscanf;
  const std::size_t formatIndex = hasFile ? 1U : 0U;
  if (call.arguments.size() != (hasFile ? 2U : 1U)) {
    addDiagnostic("scanf/fscanf left-context call only accepts its standard "
                  "format arguments");
    return std::nullopt;
  }
  if (!isCStringExpression(*call.arguments[formatIndex])) {
    addDiagnostic("function call '" + call.callee + "' argument " +
                  std::to_string(formatIndex + 1U) + " must be a cstr View");
    return std::nullopt;
  }
  if (assign.values.size() != 1U) {
    addDiagnostic(
        "scanf/fscanf left-context must be the only right-side value");
    return std::nullopt;
  }
  if (assign.targets.size() < 2U) {
    addDiagnostic("scanf/fscanf left-context requires a count target and at "
                  "least one scan target");
    return std::nullopt;
  }

  const auto *formatLiteral =
      dynamic_cast<ast::StringLiteral *>(call.arguments[formatIndex].get());
  std::optional<FormatParseResult> parsedFormat;
  if (formatLiteral != nullptr) {
    parsedFormat = parseLiteralFormat(formatLiteral->value);
    if (!parsedFormat->diagnostic.empty()) {
      addDiagnostic("function call '" + call.callee +
                    "' has invalid format string: " + parsedFormat->diagnostic);
      return std::nullopt;
    }
    if (parsedFormat->items.empty()) {
      addDiagnostic("scanf/fscanf literal format must contain at least one "
                    "assignment conversion");
      return std::nullopt;
    }
    if (assign.targets.size() != parsedFormat->items.size() + 1U) {
      addDiagnostic("scanf/fscanf left-context target count does not match "
                    "format item count");
      return std::nullopt;
    }
  }

  auto format = analyze(*call.arguments[formatIndex]);
  if (!format) {
    return std::nullopt;
  }

  std::vector<hir::InputCallStore::Target> countTargets;
  const Symbol *resultSymbol = nullptr;
  const auto &countTarget = assign.targets.front();
  if (countTarget.op != "=" && countTarget.op != "%d=") {
    addDiagnostic("scanf/fscanf count target only supports integer assignment");
    return std::nullopt;
  }
  const auto *countIdentifier =
      dynamic_cast<const ast::IdentifierExpr *>(countTarget.target.get());
  if (countIdentifier == nullptr) {
    addDiagnostic("scanf/fscanf count target must be an identifier or '_'");
    return std::nullopt;
  }
  if (countIdentifier->name != "_") {
    const auto *symbol = lookup(countIdentifier->name);
    if (symbol == nullptr) {
      addDiagnostic("use of undeclared variable '" + countIdentifier->name +
                    "'");
      return std::nullopt;
    }
    if (symbol->byteLength != 4) {
      addDiagnostic("scanf/fscanf count target must be 4 bytes");
      return std::nullopt;
    }
    countTargets.emplace_back(symbol->name, symbol->bindingName,
                              symbol->byteLength, symbol->storage, 0);
    resultSymbol = symbol;
  }

  std::vector<hir::InputCallStore::Target> scanTargets;
  for (std::size_t index = 1; index < assign.targets.size(); ++index) {
    const auto &target = assign.targets[index];
    if (target.op != "=") {
      addDiagnostic("scanf/fscanf scan targets do not support assignment "
                    "operators");
      return std::nullopt;
    }
    if (const auto *identifier =
            dynamic_cast<const ast::IdentifierExpr *>(target.target.get())) {
      if (identifier->name == "_") {
        addDiagnostic("scanf/fscanf '_' is only valid for the count target");
        return std::nullopt;
      }
    }
    const auto reference = resolveAddressableReference(*target.target);
    if (!reference) {
      if (result_.diagnostics.empty()) {
        addDiagnostic("scanf/fscanf target must be a writable lvalue");
      }
      return std::nullopt;
    }
    if (parsedFormat) {
      if (const auto diagnostic = validateInputTarget(
              parsedFormat->items[index - 1U], *reference)) {
        addDiagnostic(*diagnostic);
        return std::nullopt;
      }
    }
    scanTargets.emplace_back(reference->name, reference->bindingName,
                             reference->byteLength, reference->storage,
                             reference->offset, reference->templateName);
    if (resultSymbol == nullptr) {
      resultSymbol = lookup(reference->name);
    }
  }

  std::unique_ptr<hir::Expr> file;
  if (hasFile) {
    if (!isHandleExpression(*call.arguments[0])) {
      addDiagnostic("fscanf file argument must be a handle");
      return std::nullopt;
    }
    file = analyze(*call.arguments[0]);
    if (!file) {
      return std::nullopt;
    }
    if (file->result.category != hir::ViewCategory::Handle ||
        file->result.lengthKind != hir::ViewLengthKind::Static ||
        file->result.staticByteLength != pointerByteLength()) {
      addDiagnostic("fscanf file handle must be pointer-sized");
      return std::nullopt;
    }
  }

  AssignmentLowering lowered;
  lowered.stores.push_back(std::make_unique<hir::InputCallStore>(
      call.callee, std::move(file), std::move(format), std::move(countTargets),
      std::move(scanTargets), builtin));
  if (resultSymbol != nullptr) {
    lowered.byteLength = resultSymbol->byteLength;
    lowered.result = std::make_unique<hir::VariableRef>(
        resultSymbol->name, resultSymbol->bindingName, resultSymbol->storage,
        fixedResult(resultSymbol->templateName, resultSymbol->byteLength, true,
                    true));
  } else {
    lowered.byteLength = 4;
    lowered.result = std::make_unique<hir::IntegerLiteral>("0",
                                                            signedIntegerResult(4));
  }
  return lowered;
}

std::optional<StandardTemplatePrintCallLowering>
Analyzer::lowerStandardTemplatePrintCall(const ast::Expr &value,
                                         std::string_view templateName) {
  const ast::Expr *operand = &value;
  if (const auto *asExpr = dynamic_cast<const ast::AsExpr *>(&value);
      asExpr != nullptr && asExpr->templateName == templateName) {
    operand = asExpr->operand.get();
  }

  const bool operandIsHandle = isHandleExpression(*operand);
  if (templateName == "handle" && !operandIsHandle) {
    addDiagnostic("print handle formatting requires a handle value");
    return std::nullopt;
  }
  if (templateName != "handle" && operandIsHandle) {
    addDiagnostic("handle values may only use handle formatting");
    return std::nullopt;
  }

  const auto operandLength = inferByteLength(*operand);
  if (!operandLength) {
    addDiagnostic("print template argument has no known byte length");
    return std::nullopt;
  }
  if (const auto expectedLength = templateByteLength(templateName);
      expectedLength && *expectedLength != *operandLength) {
    addDiagnostic(
        "print template argument byte length does not match template '" +
        std::string(templateName) + "'");
    return std::nullopt;
  }

  auto lowered = isFloatTemplate(templateName)
                     ? analyzeFloatOperand(*operand, *operandLength)
                     : analyze(*operand);
  if (!lowered) {
    return std::nullopt;
  }

  StandardTemplatePrintCallLowering result;
  if (templateName == "bytes") {
    result.callee = "put";
    result.builtin = stdlib::BuiltinId::Put;
    result.arguments.push_back(std::move(lowered));
    return result;
  }

  const auto format = printfFormatForTemplate(templateName);
  if (!format) {
    addDiagnostic("standard template '" + std::string(templateName) +
                  "' formatted print is not supported yet");
    return std::nullopt;
  }
  result.callee = "print";
  result.arguments.push_back(std::make_unique<hir::StringLiteral>(
      *format, fixedResult("cstr", inferStringLiteralByteLength(*format))));
  result.arguments.push_back(std::move(lowered));
  result.formatArgumentKinds = {
      hir::FormatArgKind::String,
      isFloatTemplate(templateName)
          ? hir::FormatArgKind::Float
          : formatArgumentKind(*operand, *result.arguments.back()),
  };
  return result;
}

std::optional<UserTemplateFormatCallLowering>
Analyzer::lowerUserTemplateFormatCall(const ast::CallExpr &call,
                                      stdlib::BuiltinId builtin,
                                      std::string_view templateName,
                                      std::size_t valueIndex) {
  const std::string lookupKey = "format|" + std::string(templateName) + "|addr";
  const auto binding = implOpIndexes_.find(lookupKey);
  if (binding == implOpIndexes_.end() ||
      binding->second >= implOpInfos_.size()) {
    addDiagnostic("user template '" + std::string(templateName) +
                  "' requires matching op format");
    return std::nullopt;
  }
  const auto &format = implOpInfos_[binding->second];
  if (format.implTemplate != templateName ||
      format.returnByteLengths != std::vector<std::size_t>{4}) {
    addDiagnostic(
        "internal error: invalid format impl op binding for template '" +
        std::string(templateName) + "'");
    return std::nullopt;
  }

  auto value = analyze(*call.arguments[valueIndex]);
  if (!value) {
    return std::nullopt;
  }
  hir::FormatOutputSink sink = hir::FormatOutputSink::Stdout;
  std::unique_ptr<hir::Expr> file;
  if (builtin == stdlib::BuiltinId::Fprintf) {
    sink = hir::FormatOutputSink::File;
    file = analyze(*call.arguments.front());
    if (!file) {
      return std::nullopt;
    }
  }
  return UserTemplateFormatCallLowering{format.symbolName, std::move(value),
                                        sink, std::move(file),
                                        format.returnByteLengths.front()};
}

std::vector<std::unique_ptr<hir::Expr>>
Analyzer::analyzeCallArguments(const ast::CallExpr &call,
                               const FunctionSignature &signature) {
  std::vector<std::unique_ptr<hir::Expr>> arguments;
  if (call.arguments.size() != signature.parameterByteLengths.size()) {
    addDiagnostic("function call '" + call.callee +
                  "' argument count does not match declaration");
    return arguments;
  }
  if (!validateHandleCallArguments(call, signature)) {
    return arguments;
  }
  if (!validateBuiltinViewArguments(call, signature)) {
    return arguments;
  }
  const auto *builtin = stdlib::findBuiltin(signature.builtin);
  for (std::size_t index = 0; index < call.arguments.size(); ++index) {
    const auto parameterMode =
        builtin != nullptr && index < builtin->parameters.size()
            ? builtin->parameters[index].mode
            : stdlib::BuiltinParameterMode::None;
    const bool acceptsAnyView =
        parameterMode == stdlib::BuiltinParameterMode::View ||
        parameterMode == stdlib::BuiltinParameterMode::LView ||
        parameterMode == stdlib::BuiltinParameterMode::MemView ||
        parameterMode == stdlib::BuiltinParameterMode::MemLView ||
        parameterMode == stdlib::BuiltinParameterMode::CStrView ||
        parameterMode == stdlib::BuiltinParameterMode::BytesView;
    const auto templateName =
        index < signature.parameterTemplateNames.size()
            ? std::string_view{signature.parameterTemplateNames[index]}
            : std::string_view{};
    const auto expectedLength = signature.parameterByteLengths[index];
    if (isFloatTemplate(templateName)) {
      auto lowered =
          analyzeFloatOperand(*call.arguments[index], expectedLength);
      if (!lowered) {
        return arguments;
      }
      arguments.push_back(std::move(lowered));
      continue;
    }
    auto lowered = analyze(*call.arguments[index]);
    if (!lowered) {
      if (result_.diagnostics.empty()) {
        addDiagnostic("function argument cannot be lowered");
      }
      return arguments;
    }
    if (signature.builtin == stdlib::BuiltinId::Assert && index == 0U) {
      if (!hir::isBooleanTestable(lowered->result)) {
        addDiagnostic("assert condition cannot form a View");
        return arguments;
      }
      arguments.push_back(std::make_unique<hir::BooleanTestExpr>(
          std::move(lowered), booleanResult()));
      continue;
    }
    if (signature.builtin == stdlib::BuiltinId::None &&
        hasRuntimeDynamicView(*lowered)) {
      addDiagnostic(
          "dynamic View cannot be passed to fixed function parameter");
      return arguments;
    }
    const bool isOrdinaryUserFunction =
        signature.builtin == stdlib::BuiltinId::None && !signature.isExtern &&
        !signature.isCAbi && !cCompatibilityMode_;
    if (isOrdinaryUserFunction) {
      const bool integerLiteral =
          dynamic_cast<const hir::IntegerLiteral *>(lowered.get()) != nullptr;
      const bool characterLiteral =
          dynamic_cast<const hir::CharacterLiteral *>(lowered.get()) != nullptr &&
          lowered->result.integerInterpretation !=
              hir::IntegerInterpretation::RawOnly;
      const bool standardIntegerParameter = isStandardIntegerTemplate(templateName);
      const bool literalMatchesInteger =
          standardIntegerParameter && (integerLiteral || characterLiteral);
      const bool untemplatedIntegerParameter = templateName.empty() ||
                                               templateName == "none";
      const bool acceptsUntemplatedInteger =
          untemplatedIntegerParameter &&
          (integerLiteral || characterLiteral ||
           hir::isIntegerNumeric(lowered->result) ||
           lowered->result.category == hir::ViewCategory::Boolean ||
           lowered->result.category == hir::ViewCategory::Address);
      if ((!literalMatchesInteger && !acceptsUntemplatedInteger &&
           lowered->result.templateName != templateName) ||
          lowered->result.lengthKind != hir::ViewLengthKind::Static ||
          (!literalMatchesInteger && !acceptsUntemplatedInteger &&
           lowered->result.staticByteLength != expectedLength)) {
        addDiagnostic("function call '" + call.callee + "' argument " +
                      std::to_string(index + 1U) +
                      " must exactly match parameter template and byte length");
        return arguments;
      }
      if (integerLiteral) {
        const auto *literal = static_cast<const hir::IntegerLiteral *>(lowered.get());
        if (!untemplatedIntegerParameter &&
            !integerLiteralFits(*literal, expectedLength,
                                isUnsignedIntegerTemplate(templateName))) {
          addDiagnostic("function call '" + call.callee + "' argument " +
                        std::to_string(index + 1U) +
                        " integer literal does not fit parameter template");
          return arguments;
        }
      }
      if ((literalMatchesInteger || acceptsUntemplatedInteger) &&
          lowered->result.staticByteLength != expectedLength) {
        const bool destinationSigned =
            !isUnsignedIntegerTemplate(templateName);
        lowered = std::make_unique<hir::IntegerCastExpr>(
            std::move(lowered), destinationSigned,
            fixedResult(std::string(templateName), expectedLength));
      }
      arguments.push_back(std::move(lowered));
      continue;
    }
    const bool exactNonIntegerView =
        lowered->result.lengthKind == hir::ViewLengthKind::Static &&
        lowered->result.staticByteLength == expectedLength &&
        lowered->result.templateName == templateName;
    if (!isIntegerExpression(*lowered)) {
      if (exactNonIntegerView) {
        arguments.push_back(std::move(lowered));
        continue;
      }
      if (acceptsAnyView) {
        arguments.push_back(std::move(lowered));
        continue;
      }
      if (index < signature.stringParameters.size() &&
          signature.stringParameters[index] &&
          dynamic_cast<hir::StringLiteral *>(lowered.get()) != nullptr) {
        arguments.push_back(std::move(lowered));
        continue;
      }
      if (isPointerTemplate(templateName) &&
          dynamic_cast<hir::StringLiteral *>(lowered.get()) != nullptr) {
        arguments.push_back(std::move(lowered));
        continue;
      }
      addDiagnostic("function argument is not an integer expression");
      return arguments;
    }
    const auto actualLength = integerExpressionByteLength(*lowered).value_or(4);
    if (!acceptsAnyView && actualLength > expectedLength && expectedLength < 8) {
      addDiagnostic("function argument byte length does not fit parameter");
      return arguments;
    }
    arguments.push_back(std::move(lowered));
  }
  return arguments;
}

} // namespace hitsimple::sema
