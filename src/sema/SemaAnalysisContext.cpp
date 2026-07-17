#include "SemaAnalyzer.h"

#include "hitsimple/literal/Literal.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace hitsimple::sema {

bool isMainRequiredForCurrentAnalysis();
namespace {

std::optional<std::size_t> parseUnsignedDecimal(std::string_view text) {
  std::uint64_t value = 0;
  const auto *begin = text.data();
  const auto *end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc() || result.ptr != end) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(value);
}

std::optional<std::size_t> integerLiteralValue(const ast::Expr &expr) {
  const auto *integer = dynamic_cast<const ast::IntegerLiteral *>(&expr);
  if (integer == nullptr) {
    return std::nullopt;
  }
  const auto value = literal::parseUnsignedIntegerLiteral(integer->value);
  if (!value || *value > std::numeric_limits<std::size_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(*value);
}

std::optional<std::size_t> standardTemplateByteLength(std::string_view name) {
  if (name == "bool") {
    return 1;
  }
  if (name == "addr" || name == "handle") {
    return pointerByteLength();
  }
  if (name.size() >= 2 && (name[0] == 'i' || name[0] == 'u' || name[0] == 'f')) {
    const auto bits = parseUnsignedDecimal(name.substr(1));
    if (!bits || *bits == 0 || *bits % 8 != 0) {
      return std::nullopt;
    }
    const auto bytes = *bits / 8;
    if (name[0] == 'f' && bytes != 2 && bytes != 4 && bytes != 8 &&
        bytes != 16) {
      return std::nullopt;
    }
    return bytes;
  }
  return std::nullopt;
}

bool isVariableLengthStandardTemplate(std::string_view name) {
  return name == "bytes" || name == "cstr";
}

bool isReservedIdentifier(std::string_view name) {
  if (name == "_") {
    return true;
  }
  if (name.size() >= 2 && name.front() == 't') {
    return std::all_of(name.begin() + 1, name.end(), [](char ch) {
      return std::isdigit(static_cast<unsigned char>(ch));
    });
  }
  return false;
}

void addBuiltinSignature(
    std::unordered_map<std::string, FunctionSignature> &functions,
    std::string name, std::vector<std::size_t> parameters,
    std::vector<std::size_t> returns, stdlib::BuiltinId builtin,
    std::vector<bool> stringParameters = {}) {
  const auto key = name;
  if (stringParameters.empty()) {
    stringParameters.resize(parameters.size(), false);
  }
  FunctionSignature signature;
  signature.name = std::move(name);
  signature.parameterByteLengths = std::move(parameters);
  signature.parameterTemplateNames.resize(signature.parameterByteLengths.size());
  signature.stringParameters = std::move(stringParameters);
  signature.returnByteLengths = std::move(returns);
  signature.returnTemplateNames.resize(signature.returnByteLengths.size());
  signature.returnsKnown = true;
  signature.returnsExplicit = true;
  signature.isExtern = true;
  signature.builtin = builtin;
  functions.emplace(key, std::move(signature));
}

void setBuiltinTemplateNames(
    std::unordered_map<std::string, FunctionSignature> &functions,
    std::string_view name, std::vector<std::string> parameterTemplateNames,
    std::vector<std::string> returnTemplateNames) {
  const auto found = functions.find(std::string(name));
  if (found == functions.end()) {
    return;
  }
  auto &signature = found->second;
  signature.parameterTemplateNames = std::move(parameterTemplateNames);
  signature.returnTemplateNames = std::move(returnTemplateNames);
}

bool hasSameAbiSignature(const FunctionSignature &signature,
                         const std::vector<std::size_t> &parameters,
                         const std::vector<std::size_t> &returns,
                         bool isCAbi = false) {
  return signature.parameterByteLengths == parameters &&
         signature.returnByteLengths == returns && signature.isCAbi == isCAbi;
}

bool isFloatTemplateName(std::string_view name) {
  return name == "f16" || name == "f32" || name == "f64" ||
         name == "f128";
}

bool isCAbiName(std::string_view abiName) { return abiName == "\"C\""; }

bool containsCAbiForbiddenControlFlow(const ast::Stmt &statement) {
  if (dynamic_cast<const ast::ThrowStmt *>(&statement) != nullptr ||
      dynamic_cast<const ast::TryCatchStmt *>(&statement) != nullptr) {
    return true;
  }
  if (const auto *ifStmt = dynamic_cast<const ast::IfStmt *>(&statement)) {
    for (const auto &nested : ifStmt->thenBlock->statements) {
      if (containsCAbiForbiddenControlFlow(*nested)) {
        return true;
      }
    }
    if (ifStmt->elseBlock) {
      for (const auto &nested : ifStmt->elseBlock->statements) {
        if (containsCAbiForbiddenControlFlow(*nested)) {
          return true;
        }
      }
    }
    return false;
  }
  if (const auto *whileStmt = dynamic_cast<const ast::WhileStmt *>(&statement)) {
    for (const auto &nested : whileStmt->body->statements) {
      if (containsCAbiForbiddenControlFlow(*nested)) {
        return true;
      }
    }
    return false;
  }
  if (const auto *forStmt = dynamic_cast<const ast::ForStmt *>(&statement)) {
    if (forStmt->init && containsCAbiForbiddenControlFlow(*forStmt->init)) {
      return true;
    }
    for (const auto &nested : forStmt->body->statements) {
      if (containsCAbiForbiddenControlFlow(*nested)) {
        return true;
      }
    }
    return false;
  }
  if (const auto *label = dynamic_cast<const ast::LabelStmt *>(&statement)) {
    return label->statement && containsCAbiForbiddenControlFlow(*label->statement);
  }
  return false;
}

bool containsCAbiForbiddenControlFlow(const ast::BlockStmt &block) {
  for (const auto &statement : block.statements) {
    if (containsCAbiForbiddenControlFlow(*statement)) {
      return true;
    }
  }
  return false;
}

std::optional<hir::AbiType> cAbiTypeFor(std::string_view templateName,
                                        std::size_t byteLength) {
  const auto integer = [&](bool isSigned) -> std::optional<hir::AbiType> {
    if (byteLength != 1 && byteLength != 2 && byteLength != 4 &&
        byteLength != 8) {
      return std::nullopt;
    }
    hir::AbiType type{hir::AbiValueKind::Integer, byteLength, isSigned};
    type.alignment = byteLength;
    return type;
  };
  if (templateName == "bool") {
    return byteLength == 1 ? integer(false) : std::nullopt;
  }
  if (templateName.size() == 2 &&
      (templateName.front() == 'i' || templateName.front() == 'u') &&
      (templateName[1] == '8')) {
    return byteLength == 1 ? integer(templateName.front() == 'i')
                           : std::nullopt;
  }
  if (templateName.size() == 3 &&
      (templateName.front() == 'i' || templateName.front() == 'u') &&
      (templateName.substr(1) == "16" || templateName.substr(1) == "32" ||
       templateName.substr(1) == "64")) {
    const auto expected = templateName.substr(1) == "16"
                              ? std::size_t{2}
                              : (templateName.substr(1) == "32"
                                     ? std::size_t{4}
                                     : std::size_t{8});
    return byteLength == expected ? integer(templateName.front() == 'i')
                                  : std::nullopt;
  }
  if (templateName == "f32" || templateName == "f64") {
    const auto expected = templateName == "f32" ? std::size_t{4}
                                                  : std::size_t{8};
    if (byteLength != expected) {
      return std::nullopt;
    }
    hir::AbiType type{hir::AbiValueKind::Floating, byteLength, true};
    type.alignment = byteLength;
    return type;
  }
  if (templateName == "addr" || templateName == "cstr" ||
      templateName == "handle") {
    if (byteLength != pointerByteLength()) {
      return std::nullopt;
    }
    hir::AbiType type{hir::AbiValueKind::Pointer, byteLength, false};
    type.alignment = byteLength;
    return type;
  }
  return std::nullopt;
}

std::optional<hir::FunctionAbiSignature>
makeCAbiSignature(const FunctionSignature &signature) {
  if (signature.returnByteLengths.size() > 1U ||
      signature.parameterTemplateNames.size() !=
          signature.parameterByteLengths.size() ||
      signature.returnTemplateNames.size() != signature.returnByteLengths.size()) {
    return std::nullopt;
  }
  hir::FunctionAbiSignature abi;
  abi.isCCompatibility = true;
  for (std::size_t index = 0; index < signature.parameterByteLengths.size();
       ++index) {
    auto type = cAbiTypeFor(signature.parameterTemplateNames[index],
                            signature.parameterByteLengths[index]);
    if (!type) {
      return std::nullopt;
    }
    abi.parameterTypes.push_back(std::move(*type));
  }
  for (std::size_t index = 0; index < signature.returnByteLengths.size();
       ++index) {
    auto type = cAbiTypeFor(signature.returnTemplateNames[index],
                            signature.returnByteLengths[index]);
    if (!type) {
      return std::nullopt;
    }
    abi.returnTypes.push_back(std::move(*type));
  }
  return abi;
}

bool isCCompatibilityConversion(stdlib::BuiltinId builtin) {
  using enum stdlib::BuiltinId;
  switch (builtin) {
  case ToF16:
  case ToF32:
  case ToF64:
  case ToF128:
  case ToI8:
  case ToI16:
  case ToI32:
  case ToI64:
  case ToU8:
  case ToU16:
  case ToU32:
  case ToU64:
    return true;
  default:
    return false;
  }
}

std::string implMethodKey(std::string_view implTemplate,
                          std::string_view methodName,
                          const std::vector<std::string> &parameterTemplates) {
  std::string key = std::string(implTemplate) + "|" +
                    std::string(methodName) + "|" +
                    std::to_string(parameterTemplates.size());
  for (const auto &templateName : parameterTemplates) {
    key += "|" + templateName;
  }
  return key;
}

std::optional<hir::FunctionAbiSignature>
makeFloatingAbiSignature(const FunctionSignature &signature) {
  bool hasFloatingValue = false;
  hir::FunctionAbiSignature abi;
  abi.isCCompatibility = false;
  abi.parameterTypes.reserve(signature.parameterByteLengths.size());
  for (std::size_t index = 0; index < signature.parameterByteLengths.size();
       ++index) {
    const auto templateName = index < signature.parameterTemplateNames.size()
                                  ? std::string_view{
                                        signature.parameterTemplateNames[index]}
                                  : std::string_view{};
    const auto byteLength = signature.parameterByteLengths[index];
    const bool floating = isFloatTemplateName(templateName);
    hasFloatingValue = hasFloatingValue || floating;
    if (!floating && byteLength != 1 && byteLength != 2 && byteLength != 4 &&
        byteLength != 8) {
      return std::nullopt;
    }
    abi.parameterTypes.push_back(
        hir::AbiType{floating ? hir::AbiValueKind::Floating
                              : hir::AbiValueKind::Integer,
                     byteLength, true});
    abi.parameterTypes.back().alignment = byteLength;
  }
  abi.returnTypes.reserve(signature.returnByteLengths.size());
  for (std::size_t index = 0; index < signature.returnByteLengths.size();
       ++index) {
    const auto templateName = index < signature.returnTemplateNames.size()
                                  ? std::string_view{
                                        signature.returnTemplateNames[index]}
                                  : std::string_view{};
    const auto byteLength = signature.returnByteLengths[index];
    const bool floating = isFloatTemplateName(templateName);
    hasFloatingValue = hasFloatingValue || floating;
    if (!floating && byteLength != 1 && byteLength != 2 && byteLength != 4 &&
        byteLength != 8) {
      return std::nullopt;
    }
    abi.returnTypes.push_back(
        hir::AbiType{floating ? hir::AbiValueKind::Floating
                              : hir::AbiValueKind::Integer,
                     byteLength, true});
    abi.returnTypes.back().alignment = byteLength;
  }
  if (!hasFloatingValue) {
    return std::nullopt;
  }
  return abi;
}

} // namespace

std::optional<hir::FunctionAbiSignature>
floatingAbiSignature(const FunctionSignature &signature) {
  return makeFloatingAbiSignature(signature);
}

std::optional<hir::FunctionAbiSignature>
cAbiSignature(const FunctionSignature &signature) {
  return makeCAbiSignature(signature);
}

bool Analyzer::collectFunctionSignatures(
    const ast::TranslationUnit &unit, std::vector<hir::ExternFunction> &externs) {
  const auto pointer = pointerByteLength();
  const auto addBuiltin = [this](std::string name,
                                 std::vector<std::size_t> parameters,
                                 std::vector<std::size_t> returns,
                                 std::vector<bool> stringParameters = {}) {
    const auto* spec = stdlib::findBuiltin(name);
    if (spec == nullptr ||
        (!isStandardHeaderIncluded(spec->header) &&
         !(cCompatibilityMode_ && isCCompatibilityConversion(spec->id)))) {
      return;
    }
    addBuiltinSignature(functions_, std::move(name), std::move(parameters),
                        std::move(returns), spec->id,
                        std::move(stringParameters));
  };
  addBuiltin("alloc", {pointer}, {pointer});
  addBuiltin("calloc", {pointer, pointer}, {pointer});
  addBuiltin("realloc", {pointer, pointer}, {pointer});
  addBuiltin("free", {pointer}, {});
  addBuiltin("memset", {pointer, 4, pointer}, {pointer});
  addBuiltin("memcpy", {pointer, pointer, pointer}, {pointer});
  addBuiltin("memmove", {pointer, pointer, pointer}, {pointer});
  addBuiltin("memcmp", {pointer, pointer, pointer}, {4});
  addBuiltin("strlen", {pointer}, {pointer}, {true});
  addBuiltin("strcmp", {pointer, pointer}, {4}, {true, true});
  addBuiltin("strcpy", {pointer, pointer}, {pointer});
  addBuiltin("strncpy", {pointer, pointer, pointer}, {pointer});
  addBuiltin("strcat", {pointer, pointer}, {pointer});
  addBuiltin("strchr", {pointer, 4}, {pointer}, {true, false});
  addBuiltin("put", {4}, {4});
  addBuiltin("get", {}, {4});
  addBuiltin("fopen", {pointer, pointer}, {pointer}, {true, true});
  addBuiltin("fclose", {pointer}, {4});
  addBuiltin("fget", {pointer}, {4});
  addBuiltin("fput", {pointer, 4}, {4});
  addBuiltin("fread", {pointer, pointer, pointer, pointer},
             {pointer});
  addBuiltin("fwrite", {pointer, pointer, pointer, pointer},
             {pointer});
  addBuiltin("fflush", {pointer}, {4});
  addBuiltin("fseek", {pointer, pointer, 4}, {4});
  addBuiltin("ftell", {pointer}, {pointer});
  addBuiltin("feof", {pointer}, {1});
  addBuiltin("ferror", {pointer}, {4});

  setBuiltinTemplateNames(functions_, "fopen", {"cstr", "cstr"},
                          {"handle"});
  setBuiltinTemplateNames(functions_, "fclose", {"handle"}, {"i32"});
  setBuiltinTemplateNames(functions_, "fget", {"handle"}, {"i32"});
  setBuiltinTemplateNames(functions_, "fput", {"handle", "i32"}, {"i32"});
  setBuiltinTemplateNames(functions_, "fread",
                          {"addr", "u64", "u64", "handle"}, {"u64"});
  setBuiltinTemplateNames(functions_, "fwrite",
                          {"addr", "u64", "u64", "handle"}, {"u64"});
  setBuiltinTemplateNames(functions_, "fflush", {"handle"}, {"i32"});
  setBuiltinTemplateNames(functions_, "fseek", {"handle", "i64", "i32"},
                          {"i32"});
  setBuiltinTemplateNames(functions_, "ftell", {"handle"}, {"i64"});
  setBuiltinTemplateNames(functions_, "feof", {"handle"}, {"bool"});
  setBuiltinTemplateNames(functions_, "ferror", {"handle"}, {"i32"});
  addBuiltin("abs", {8}, {8});
  addBuiltin("min", {8, 8}, {8});
  addBuiltin("max", {8, 8}, {8});
  addBuiltin("is_digit", {4}, {1});
  addBuiltin("is_alpha", {4}, {1});
  addBuiltin("is_alnum", {4}, {1});
  addBuiltin("is_space", {4}, {1});
  addBuiltin("to_upper", {4}, {1});
  addBuiltin("to_lower", {4}, {1});
  addBuiltin("srand", {4}, {});
  addBuiltin("rand", {}, {4});
  addBuiltin("time_ms", {}, {8});
  addBuiltin("clock_ms", {}, {8});
  addBuiltin("exit", {4}, {});
  addBuiltin("abort", {}, {});
  addBuiltin("assert", {4, 4}, {});
  addBuiltin("panic", {4}, {});

  addBuiltin("length", {pointer}, {pointer});
  addBuiltin("resize_bytes", {8, pointer}, {8});
  addBuiltin("byte_swap", {8}, {8});
  addBuiltin("to_f16", {8}, {2});
  addBuiltin("to_f32", {8}, {4});
  addBuiltin("to_f64", {8}, {8});
  addBuiltin("to_f128", {8}, {16});
  addBuiltin("to_i8", {8}, {1});
  addBuiltin("to_i16", {8}, {2});
  addBuiltin("to_i32", {8}, {4});
  addBuiltin("to_i64", {8}, {8});
  addBuiltin("to_u8", {8}, {1});
  addBuiltin("to_u16", {8}, {2});
  addBuiltin("to_u32", {8}, {4});
  addBuiltin("to_u64", {8}, {8});
  addBuiltin("f_abs", {8}, {8});
  addBuiltin("f_sqrt", {8}, {8});
  addBuiltin("f_pow", {8, 8}, {8});
  addBuiltin("f_sin", {8}, {8});
  addBuiltin("f_cos", {8}, {8});
  addBuiltin("f_tan", {8}, {8});
  addBuiltin("f_log", {8}, {8});
  addBuiltin("f_exp", {8}, {8});
  addBuiltin("f_floor", {8}, {8});
  addBuiltin("f_ceil", {8}, {8});
  addBuiltin("f_round", {8}, {8});
  addBuiltin("printf", {pointer}, {4}, {true});
  addBuiltin("print", {pointer}, {4});
  addBuiltin("scanf", {pointer}, {4}, {true});
  addBuiltin("fprintf", {pointer, pointer}, {4}, {false, true});
  addBuiltin("fscanf", {pointer, pointer}, {4}, {false, true});
  setBuiltinTemplateNames(functions_, "fprintf", {"handle", "cstr"}, {"i32"});

  std::size_t mainCount = 0;
  for (const auto *externFunction : unit.externFunctions) {
    CurrentRangeGuard rangeGuard(*this, *externFunction);
    FunctionSignature signature;
    signature.name = externFunction->name;
    signature.isExtern = true;
    signature.returnsKnown = true;
    signature.returnsExplicit = true;
    signature.isCAbi = !externFunction->abiName.empty();
    if (signature.isCAbi && !isCAbiName(externFunction->abiName)) {
      addDiagnostic("unsupported extern ABI '" + externFunction->abiName +
                    "'; only \"C\" is supported");
      continue;
    }
    if (isReservedIdentifier(externFunction->name)) {
      addDiagnostic("reserved identifier '" + externFunction->name +
                    "' cannot be used as an extern function name");
      continue;
    }
    if (const auto* standard = stdlib::findBuiltin(externFunction->name)) {
      addDiagnostic("standard library function '" + externFunction->name +
                    "' must not be declared with extern; include <" +
                    std::string(stdlib::headerName(standard->header)) + ">");
      continue;
    }
    if (!registerTopLevelName(externFunction->name)) {
      continue;
    }
    for (const auto &param : externFunction->params) {
      if (isReservedIdentifier(param.name)) {
        addDiagnostic("reserved identifier '" + param.name +
                      "' cannot be used as a parameter name");
        continue;
      }
      if (!signature.isCAbi && isVariableLengthStandardTemplate(param.templateName)) {
        addDiagnostic("core extern parameter '" + param.name +
                      "' cannot pass " + param.templateName +
                      " by value; use [P] as addr");
        continue;
      }

      std::optional<std::size_t> length;
      if (param.length.empty()) {
        length = signature.isCAbi && param.templateName == "cstr"
                     ? std::optional<std::size_t>{pointerByteLength()}
                     : templateByteLength(param.templateName);
        if (!length) {
          addDiagnostic("extern parameter '" + param.name +
                        "' requires an explicit byte length");
          continue;
        }
      } else {
        length = parseDeclaredLength(param.length, param.templateName);
        if (!length) {
          continue;
        }
      }
      if (*length == 0) {
        addDiagnostic("invalid extern parameter byte length for '" +
                      param.name + "'");
        continue;
      }
      signature.parameterByteLengths.push_back(*length);
      signature.parameterTemplateNames.push_back(param.templateName);
      signature.stringParameters.push_back(signature.isCAbi &&
                                           param.templateName == "cstr");
    }
    auto returns = parseReturnSignature(externFunction->returns,
                                        externFunction->name,
                                        signature.isCAbi);
    if (!returns) {
      continue;
    }
    if (const auto found = functions_.find(externFunction->name);
        found != functions_.end()) {
      if (hasSameAbiSignature(found->second, signature.parameterByteLengths,
                              *returns, signature.isCAbi)) {
        continue;
      }
      addDiagnostic("duplicate function declaration '" + externFunction->name +
                    "'");
      continue;
    }
    signature.returnByteLengths = *returns;
    signature.returnTemplateNames.reserve(externFunction->returns.size());
    for (const auto &item : externFunction->returns) {
      signature.returnTemplateNames.push_back(item.templateName);
      signature.returnHasExplicitUserTemplate.push_back(
          !item.templateName.empty() && templates_.contains(item.templateName));
    }
    if (signature.isCAbi && !cAbiSignature(signature)) {
      addDiagnostic("C ABI declaration '" + signature.name +
                    "' uses an unsupported parameter or return type");
      continue;
    }
    externs.emplace_back(signature.name, signature.parameterByteLengths,
                         signature.returnByteLengths);
    if (const auto abi = signature.isCAbi ? cAbiSignature(signature)
                                          : floatingAbiSignature(signature)) {
      externs.back().abiSignature = *abi;
    }
    functions_.emplace(signature.name, std::move(signature));
  }

  for (const auto *function : unit.functions) {
    CurrentRangeGuard rangeGuard(*this, *function);
    ++mainCount;
    if (function->name != "main") {
      --mainCount;
    }
    if (isReservedIdentifier(function->name)) {
      addDiagnostic("reserved identifier '" + function->name +
                    "' cannot be used as a function name");
      continue;
    }
    if (!registerTopLevelName(function->name)) {
      continue;
    }
    if (functions_.find(function->name) != functions_.end()) {
      addDiagnostic("duplicate function declaration '" + function->name + "'");
      continue;
    }
    FunctionSignature signature;
    signature.name = function->name;
    signature.isCAbi = !function->abiName.empty();
    if (signature.isCAbi && !isCAbiName(function->abiName)) {
      addDiagnostic("unsupported extern ABI '" + function->abiName +
                    "'; only \"C\" is supported");
      continue;
    }
    if (signature.isCAbi && function->name == "main") {
      addDiagnostic("C ABI export cannot define main");
      continue;
    }
    if (signature.isCAbi &&
        containsCAbiForbiddenControlFlow(*function->body)) {
      addDiagnostic("C ABI export '" + signature.name +
                    "' cannot contain throw or try/catch");
      continue;
    }
    for (const auto &param : function->params) {
      if (isReservedIdentifier(param.name)) {
        addDiagnostic("reserved identifier '" + param.name +
                      "' cannot be used as a parameter name");
        continue;
      }
      std::optional<std::size_t> length;
      if (param.length.empty()) {
        length = signature.isCAbi && param.templateName == "cstr"
                     ? std::optional<std::size_t>{pointerByteLength()}
                     : (param.templateName.empty()
                     ? std::optional<std::size_t>{4}
                     : templateByteLength(param.templateName));
      } else {
        length = parseDeclaredLength(param.length, param.templateName);
      }
      if (!length || *length == 0) {
        addDiagnostic("invalid parameter byte length for '" + param.name + "'");
        continue;
      }
      signature.parameterByteLengths.push_back(*length);
      signature.parameterTemplateNames.push_back(param.templateName);
      signature.stringParameters.push_back(signature.isCAbi &&
                                           param.templateName == "cstr");
    }
    if (!function->returns.empty()) {
      auto returns = parseReturnSignature(function->returns, function->name,
                                          signature.isCAbi);
      if (!returns) {
        continue;
      }
      signature.returnByteLengths = *returns;
      signature.returnTemplateNames.reserve(function->returns.size());
      for (const auto &item : function->returns) {
        signature.returnTemplateNames.push_back(item.templateName);
        signature.returnHasExplicitUserTemplate.push_back(
            !item.templateName.empty() && templates_.contains(item.templateName));
      }
      signature.returnsKnown = true;
      signature.returnsExplicit = true;
    } else if (signature.isCAbi) {
      signature.returnsKnown = true;
      signature.returnsExplicit = true;
    } else if (function->name == "main") {
      // The host entry point uses the documented i32 ABI even when source
      // omits an explicit return signature.
      signature.returnByteLengths = {4};
      signature.returnTemplateNames = {"i32"};
      signature.returnsKnown = true;
      signature.returnsExplicit = false;
    }
    if (signature.isCAbi && !cAbiSignature(signature)) {
      addDiagnostic("C ABI export '" + signature.name +
                    "' uses an unsupported parameter or return type");
      continue;
    }
    functions_.emplace(signature.name, std::move(signature));
  }

  if (mainCount > 1) {
    addDiagnostic("program must define only one main function");
  } else if (isMainRequiredForCurrentAnalysis() && mainCount == 0 &&
             result_.diagnostics.empty()) {
    addDiagnostic("program must define a main function");
  }
  return result_.diagnostics.empty();
}

bool isUnsupportedStandardFunction(std::string_view name) {
  static const std::unordered_set<std::string_view> unsupported = {
      "print", "scanf", "fprintf", "fscanf"};
  return unsupported.find(name) != unsupported.end();
}

bool Analyzer::isStandardHeaderIncluded(stdlib::StandardHeader header) const {
  return std::find(standardHeaders_.begin(), standardHeaders_.end(), header) !=
         standardHeaders_.end();
}

std::optional<stdlib::BuiltinId>
Analyzer::builtinForCall(const ast::CallExpr &call) const {
  const auto found = functions_.find(call.callee);
  if (found == functions_.end() ||
      found->second.builtin == stdlib::BuiltinId::None) {
    return std::nullopt;
  }
  return found->second.builtin;
}

bool Analyzer::rejectUnavailableStandardBuiltin(const ast::CallExpr &call) {
  const auto* spec = stdlib::findBuiltin(call.callee);
  if (spec == nullptr || functions_.contains(call.callee)) {
    return false;
  }
  addDiagnostic("standard library function '" + call.callee +
                "' requires $include <" +
                std::string(stdlib::headerName(spec->header)) + ">");
  return true;
}

std::optional<std::vector<std::size_t>>
Analyzer::parseReturnSignature(const std::vector<ast::ReturnItem> &returns,
                               std::string_view owner, bool cAbi) {
  std::vector<std::size_t> byteLengths;
  for (const auto &item : returns) {
    std::size_t length = 0;
    if (!item.length.empty()) {
      length = parseByteLength(item.length);
    } else {
      const auto templateName = item.templateName.empty() ? item.name
                                                           : item.templateName;
      const auto templateLength = cAbi && templateName == "cstr"
                                      ? std::optional<std::size_t>{pointerByteLength()}
                                      : templateByteLength(templateName);
      if (templateLength) {
        length = *templateLength;
      }
    }
    if (length == 0) {
      addDiagnostic("invalid return byte length for '" + std::string(owner) +
                    "'");
      return std::nullopt;
    }
    byteLengths.push_back(length);
  }
  return byteLengths;
}

bool Analyzer::registerReturnLengths(
    const std::vector<std::size_t> &byteLengths) {
  if (currentFunction_ == nullptr) {
    return true;
  }
  if (currentFunction_->returnsKnown) {
    if (currentFunction_->returnByteLengths == byteLengths) {
      return true;
    }
    // The unannotated entry point has a fixed i32 ABI; its return expressions
    // are converted to that ABI instead of participating in inference.
    if (currentFunction_->name == "main" &&
        !currentFunction_->returnsExplicit &&
        currentFunction_->returnByteLengths.size() == byteLengths.size()) {
      return true;
    }
    if (!currentFunction_->returnsExplicit &&
        currentFunction_->returnByteLengths.size() == byteLengths.size()) {
      for (std::size_t index = 0; index < byteLengths.size(); ++index) {
        currentFunction_->returnByteLengths[index] =
            std::max(currentFunction_->returnByteLengths[index],
                     byteLengths[index]);
      }
      return true;
    }
    if (currentFunction_->returnByteLengths != byteLengths) {
      addDiagnostic("return value lengths do not match function signature");
      return false;
    }
    return true;
  }
  currentFunction_->returnByteLengths = byteLengths;
  currentFunction_->returnsKnown = true;
  return true;
}

bool Analyzer::registerTopLevelName(std::string_view name) {
  if (!topLevelNames_.insert(std::string(name)).second) {
    addDiagnostic("duplicate top-level declaration '" + std::string(name) + "'");
    return false;
  }
  return true;
}

void Analyzer::addDiagnostic(std::string diagnostic) {
  auto entry = diagnostic::Diagnostic::error(
      diagnostic::Stage::Sema, std::move(diagnostic));
  if (currentRange_ && diagnostic::hasRange(*currentRange_)) {
    entry.range = *currentRange_;
  }
  result_.diagnostics.push_back(std::move(entry));
}

void Analyzer::beginScope() { scopes_.emplace_back(); }

void Analyzer::endScope() {
  if (!scopes_.empty()) {
    scopes_.pop_back();
  }
}

Symbol *Analyzer::lookup(std::string_view name) {
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    const auto found = scope->find(std::string(name));
    if (found != scope->end()) {
      return &found->second;
    }
  }
  return nullptr;
}

const Symbol *Analyzer::lookup(std::string_view name) const {
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    const auto found = scope->find(std::string(name));
    if (found != scope->end()) {
      return &found->second;
    }
  }
  return nullptr;
}

bool Analyzer::declare(std::string_view name, std::size_t byteLength,
                       hir::MemoryStorage storage, Symbol &out) {
  return declare(name, byteLength, storage, "", out);
}

bool Analyzer::declare(std::string_view name, std::size_t byteLength,
                       hir::MemoryStorage storage, std::string templateName,
                       Symbol &out, std::string bindingName) {
  if (scopes_.empty()) {
    beginScope();
  }

  auto &current = scopes_.back();
  const std::string key(name);
  if (isReservedIdentifier(key)) {
    return false;
  }
  if (current.find(key) != current.end()) {
    return false;
  }

  if (bindingName.empty()) {
    bindingName = makeBindingName(name);
  }
  out = Symbol{key, std::move(bindingName), byteLength, storage,
               std::move(templateName)};
  if (templates_.find(out.templateName) != templates_.end()) {
    userTemplateBindings_[out.bindingName] = out.templateName;
  }
  current.emplace(key, out);
  return true;
}

std::string Analyzer::makeBindingName(std::string_view name) {
  auto &count = bindingCounts_[std::string(name)];
  if (count == 0) {
    ++count;
    return std::string(name);
  }
  const std::string binding = std::string(name) + "." + std::to_string(count);
  ++count;
  return binding;
}

bool Analyzer::collectStructLayouts(const ast::TranslationUnit &unit,
                                    std::vector<hir::StructLayout> &layouts) {
  for (const auto *decl : unit.structs) {
    CurrentRangeGuard rangeGuard(*this, *decl);
    if (isReservedIdentifier(decl->name)) {
      addDiagnostic("reserved identifier '" + decl->name +
                    "' cannot be used as a struct name");
      continue;
    }
    if (!registerTopLevelName(decl->name)) {
      continue;
    }
    if (structs_.find(decl->name) != structs_.end()) {
      addDiagnostic("duplicate top-level declaration '" + decl->name + "'");
      continue;
    }
    StructInfo info;
    info.name = decl->name;
    std::unordered_set<std::string> memberNames;
    std::vector<hir::StructMemberLayout> hirMembers;
    std::size_t offset = 0;
    for (const auto &member : decl->members) {
      if (isReservedIdentifier(member.name)) {
        addDiagnostic("reserved identifier '" + member.name +
                      "' cannot be used as a member name");
        continue;
      }
      if (!memberNames.insert(member.name).second) {
        addDiagnostic("duplicate member '" + member.name + "' in struct '" +
                      decl->name + "'");
        continue;
      }
      const auto length = parseByteLength(member.length);
      if (length == 0) {
        addDiagnostic("invalid member byte length for '" + member.name +
                      "' in struct '" + decl->name + "'");
        continue;
      }
      info.members.push_back(StructMemberInfo{member.name, length, offset, ""});
      hirMembers.emplace_back(member.name, length, offset);
      offset += length;
    }
    info.byteLength = offset;
    layouts.emplace_back(info.name, std::move(hirMembers), info.byteLength);
    structs_.emplace(info.name, std::move(info));
  }
  return result_.diagnostics.empty();
}

std::optional<std::size_t>
Analyzer::templateByteLength(std::string_view templateName) const {
  if (templateName.empty() || templateName == "none" ||
      isVariableLengthStandardTemplate(templateName)) {
    return std::nullopt;
  }
  if (const auto standardLength = standardTemplateByteLength(templateName)) {
    return standardLength;
  }
  const auto structIt = structs_.find(std::string(templateName));
  if (structIt != structs_.end()) {
    return structIt->second.byteLength;
  }
  const auto templateIt = templates_.find(std::string(templateName));
  if (templateIt != templates_.end()) {
    return templateIt->second.byteLength;
  }
  return std::nullopt;
}

bool Analyzer::collectViewTemplates(
    const ast::TranslationUnit &unit,
    std::vector<hir::ViewTemplate> &viewTemplates) {
  for (const auto *decl : unit.templates) {
    CurrentRangeGuard rangeGuard(*this, *decl);
    if (isReservedIdentifier(decl->name)) {
      addDiagnostic("reserved identifier '" + decl->name +
                    "' cannot be used as a template name");
      continue;
    }
    if (!registerTopLevelName(decl->name)) {
      continue;
    }
    if (templates_.find(decl->name) != templates_.end() ||
        structs_.find(decl->name) != structs_.end()) {
      addDiagnostic("duplicate top-level declaration '" + decl->name + "'");
      continue;
    }

    TemplateInfo info;
    info.name = decl->name;
    std::unordered_set<std::string> memberNames;
    std::vector<hir::ViewMember> hirMembers;
    std::size_t offset = 0;
    for (const auto &member : decl->members) {
      if (isReservedIdentifier(member.name)) {
        addDiagnostic("reserved identifier '" + member.name +
                      "' cannot be used as a member name");
        continue;
      }
      if (!memberNames.insert(member.name).second) {
        addDiagnostic("duplicate member '" + member.name + "' in template '" +
                      decl->name + "'");
        continue;
      }

      std::size_t length = 0;
      if (!member.length.empty()) {
        length = parseByteLength(member.length);
      } else if (!member.templateName.empty()) {
        const auto inferred = templateByteLength(member.templateName);
        if (inferred) {
          length = *inferred;
        }
      }

      if (length == 0) {
        addDiagnostic("invalid member byte length for '" + member.name +
                      "' in template '" + decl->name + "'");
        continue;
      }

      if (!member.templateName.empty() && member.templateName != "none" &&
          !isVariableLengthStandardTemplate(member.templateName) &&
          !templateByteLength(member.templateName)) {
        addDiagnostic("unknown template '" + member.templateName +
                      "' for member '" + member.name + "'");
        continue;
      }

      info.members.push_back(
          StructMemberInfo{member.name, length, offset, member.templateName});
      hirMembers.emplace_back(member.name, length, offset, member.templateName);
      offset += length;
    }
    info.byteLength = offset;
    viewTemplates.emplace_back(info.name, std::move(hirMembers),
                               info.byteLength);
    templates_.emplace(info.name, std::move(info));
  }
  return result_.diagnostics.empty();
}

bool Analyzer::collectImplOps(
    const ast::TranslationUnit &unit, std::vector<hir::ImplOpBinding> &implOps) {
  std::unordered_set<std::string> opKeys;
  for (const auto *decl : unit.impls) {
    CurrentRangeGuard rangeGuard(*this, *decl);
    if (templates_.find(decl->name) == templates_.end()) {
      addDiagnostic("impl target is not a user template: '" + decl->name + "'");
      continue;
    }

    for (const auto &op : decl->ops) {
      if (op.op != "=" && op.op != "==" && op.op != "!=" && op.op != "<" &&
          op.op != "<=" && op.op != ">" && op.op != ">=" && op.op != "+" &&
          op.op != "-" && op.op != "*" && op.op != "/" && op.op != "%" &&
          op.op != "**" && op.op != "<<" && op.op != ">>" && op.op != "&" &&
          op.op != "|" && op.op != "^" && op.op != "format") {
        addDiagnostic("unsupported impl op '" + op.op + "'");
        continue;
      }

      if (op.op == "format") {
        if (op.params.size() != 2U) {
          addDiagnostic("impl op format must have exactly two parameters");
          continue;
        }
      } else if (op.params.size() != 2U) {
        addDiagnostic("impl op '" + op.op +
                      "' must have exactly two parameters");
        continue;
      }

      if (op.returns.empty()) {
        addDiagnostic("impl op '" + op.op + "' must declare a return view");
        continue;
      }

      bool valid = true;
      std::vector<hir::ImplOpParam> params;
      std::string key = decl->name + "|" + op.op + "|" +
                        std::to_string(op.params.size());
      for (std::size_t index = 0; index < op.params.size(); ++index) {
        const auto &param = op.params[index];
        if (param.name == "self" && index != 0) {
          addDiagnostic("'self' is only valid as the first impl parameter");
          valid = false;
        }
        if (param.isMutable) {
          if (param.name == "self") {
            addDiagnostic("mut self is reserved in Beta.21; use an assignment "
                          "target, explicit addr parameter, or returned view");
          } else {
            addDiagnostic("mut impl parameters are reserved in Beta.21");
          }
          valid = false;
        }
        if (param.templateName == "none" ||
            isVariableLengthStandardTemplate(param.templateName) ||
            !templateByteLength(param.templateName)) {
          addDiagnostic("unknown or unsized impl parameter template '" +
                        param.templateName + "'");
          valid = false;
        }
        key += "|" + param.templateName;
        params.emplace_back(param.name, param.templateName, param.isMutable);
      }
      if (!valid) {
        continue;
      }
      if (!opKeys.insert(key).second) {
        addDiagnostic("duplicate impl op binding '" + op.op + "' for template '" +
                      decl->name + "'");
        continue;
      }

      auto returns = parseReturnSignature(op.returns,
                                          decl->name + "::op " + op.op);
      if (!returns) {
        continue;
      }
      if (returns->size() != 1U) {
        addDiagnostic("impl op '" + op.op +
                      "' must declare exactly one return view");
        continue;
      }
      if (op.op == "format") {
        if (op.params[0].templateName != decl->name ||
            op.params[1].templateName != "addr" ||
            *returns != std::vector<std::size_t>{4}) {
          addDiagnostic("impl op format must use signature (value as " +
                        decl->name + ", out as addr) -> [4]");
          continue;
        }
      } else {
        implOpKeys_.insert(op.op + "|" + op.params[0].templateName + "|" +
                           op.params[1].templateName);
      }
      const std::string symbolName =
          "__hitsimple.implop." + decl->name + "." +
          std::to_string(implOps.size());
      topLevelNames_.insert(symbolName);
      std::vector<std::string> returnTemplateNames;
      std::vector<bool> returnHasExplicitUserTemplate;
      returnTemplateNames.reserve(op.returns.size());
      returnHasExplicitUserTemplate.reserve(op.returns.size());
      for (const auto &item : op.returns) {
        returnHasExplicitUserTemplate.push_back(
            !item.templateName.empty() && templates_.contains(item.templateName));
        if (!item.templateName.empty()) {
          returnTemplateNames.push_back(item.templateName);
        } else if (op.op == "==" || op.op == "!=" || op.op == "<" ||
                   op.op == "<=" || op.op == ">" || op.op == ">=") {
          returnTemplateNames.emplace_back("bool");
        } else {
          returnTemplateNames.push_back(decl->name);
        }
      }
      const std::string lookupKey =
          op.op + "|" + op.params[0].templateName + "|" +
          op.params[1].templateName;
      const auto bindingIndex = implOps.size();
      implOps.emplace_back(decl->name, op.op, symbolName, std::move(params),
                           *returns);
      implOpIndexes_.emplace(lookupKey, bindingIndex);
      implOpInfos_.push_back(ImplOpInfo{&op, symbolName, decl->name, *returns,
                                        std::move(returnTemplateNames),
                                        std::move(returnHasExplicitUserTemplate)});
    }
  }
  return result_.diagnostics.empty();
}

bool Analyzer::collectImplMethods(const ast::TranslationUnit &unit) {
  for (const auto *decl : unit.impls) {
    CurrentRangeGuard implRangeGuard(*this, *decl);
    if (templates_.find(decl->name) == templates_.end()) {
      continue;
    }

    for (const auto &method : decl->methods) {
      if (!method) {
        addDiagnostic("internal error: null impl method declaration");
        continue;
      }
      CurrentRangeGuard methodRangeGuard(*this, *method);
      if (method->params.empty()) {
        addDiagnostic("impl method '" + method->name + "' for template '" +
                      decl->name + "' must have at least one parameter");
        continue;
      }

      bool valid = true;
      std::vector<std::size_t> parameterByteLengths;
      std::vector<std::string> parameterTemplateNames;
      parameterByteLengths.reserve(method->params.size());
      parameterTemplateNames.reserve(method->params.size());
      for (std::size_t index = 0; index < method->params.size(); ++index) {
        const auto &param = method->params[index];
        if (param.name == "self" && index != 0U) {
          addDiagnostic("'self' is only valid as the first impl method parameter");
          valid = false;
        }
        if (param.isMutable) {
          if (param.name == "self") {
            addDiagnostic("mut self is reserved in Beta.21; use an assignment "
                          "target, explicit addr parameter, or returned view");
          } else {
            addDiagnostic("mut impl parameters are reserved in Beta.21");
          }
          valid = false;
        }
        if (param.templateName == "none" ||
            isVariableLengthStandardTemplate(param.templateName)) {
          addDiagnostic("unknown or unsized impl method parameter template '" +
                        param.templateName + "'");
          valid = false;
          continue;
        }
        const auto byteLength = templateByteLength(param.templateName);
        if (!byteLength || *byteLength == 0U) {
          addDiagnostic("unknown or unsized impl method parameter template '" +
                        param.templateName + "'");
          valid = false;
          continue;
        }
        if (!param.length.empty()) {
          const auto declaredLength =
              parseDeclaredLength(param.length, param.templateName);
          if (!declaredLength) {
            valid = false;
            continue;
          }
          if (*declaredLength != *byteLength) {
            addDiagnostic("impl method parameter byte length does not match "
                          "template '" +
                          param.templateName + "'");
            valid = false;
            continue;
          }
        }
        parameterByteLengths.push_back(*byteLength);
        parameterTemplateNames.push_back(param.templateName);
      }
      if (!valid) {
        continue;
      }
      if (parameterTemplateNames.front() != decl->name) {
        addDiagnostic("first parameter of impl method '" + method->name +
                      "' must use template '" + decl->name + "'");
        continue;
      }

      const auto overloadKey =
          implMethodKey(decl->name, method->name, parameterTemplateNames);
      if (implMethodIndexes_.contains(overloadKey)) {
        addDiagnostic("duplicate impl method binding '" + method->name +
                      "' for template '" + decl->name + "'");
        continue;
      }

      const auto returns = parseReturnSignature(
          method->returns, decl->name + "::" + method->name);
      if (!returns) {
        continue;
      }
      if (returns->size() > 1U) {
        addDiagnostic("impl method '" + method->name +
                      "' supports at most one return view");
        continue;
      }
      std::vector<std::string> returnTemplateNames;
      returnTemplateNames.reserve(method->returns.size());
      for (const auto &item : method->returns) {
        returnTemplateNames.push_back(item.templateName.empty()
                                          ? "none"
                                          : item.templateName);
      }

      const std::string symbolName =
          "__hitsimple.implmethod." + decl->name + "." +
          std::to_string(implMethodInfos_.size());
      topLevelNames_.insert(symbolName);
      const auto methodIndex = implMethodInfos_.size();
      implMethodIndexes_.emplace(overloadKey, methodIndex);
      implMethodInfos_.push_back(ImplMethodInfo{
          method.get(), decl->name, symbolName, std::move(parameterByteLengths),
          std::move(parameterTemplateNames), *returns,
          std::move(returnTemplateNames), overloadKey});
    }
  }
  return result_.diagnostics.empty();
}

const ImplMethodInfo *Analyzer::findImplMethod(
    std::string_view implTemplate, std::string_view methodName,
    const std::vector<std::string> &parameterTemplateNames) const {
  const auto key =
      implMethodKey(implTemplate, methodName, parameterTemplateNames);
  const auto found = implMethodIndexes_.find(key);
  if (found == implMethodIndexes_.end()) {
    return nullptr;
  }
  return &implMethodInfos_[found->second];
}

std::optional<std::size_t>
Analyzer::parseDeclaredLength(std::string_view length,
                              std::string_view templateName) {
  if (length.starts_with('s')) {
    if (templateName.empty()) {
      addDiagnostic("struct count length requires a template mark");
      return std::nullopt;
    }
    const auto templateSize = templateByteLength(templateName);
    if (!templateSize) {
      addDiagnostic("unknown sized template '" + std::string(templateName) +
                    "'");
      return std::nullopt;
    }
    const auto count = parseUnsignedDecimal(length.substr(1));
    if (!count || *count == 0) {
      addDiagnostic("invalid struct count length '[s" +
                    std::string(length.substr(1)) + "]'");
      return std::nullopt;
    }
    return *templateSize * *count;
  }

  if (!templateName.empty() && templateName != "none" &&
      !isVariableLengthStandardTemplate(templateName)) {
    if (!templateByteLength(templateName)) {
      addDiagnostic("unknown template '" + std::string(templateName) + "'");
      return std::nullopt;
    }
  }
  return parseByteLength(length);
}

std::optional<std::size_t>
Analyzer::structIndex(const ast::Expr &expression) const {
  if (const auto *identifier =
          dynamic_cast<const ast::IdentifierExpr *>(&expression)) {
    if (identifier->name.starts_with('s')) {
      return parseUnsignedDecimal(std::string_view(identifier->name).substr(1));
    }
  }
  return std::nullopt;
}

std::optional<MemoryReference>
Analyzer::resolveMemoryReference(const ast::Expr &expr) {
  const auto *member = dynamic_cast<const ast::MemberExpr *>(&expr);
  if (member == nullptr) {
    return std::nullopt;
  }

  std::vector<std::string_view> memberPath;
  const ast::Expr *base = &expr;
  while (const auto *pathMember = dynamic_cast<const ast::MemberExpr *>(base)) {
    memberPath.push_back(pathMember->member);
    base = pathMember->base.get();
  }
  std::reverse(memberPath.begin(), memberPath.end());

  std::optional<std::string_view> baseViewTemplate;
  while (const auto *asExpr = dynamic_cast<const ast::AsExpr *>(base)) {
    baseViewTemplate = asExpr->templateName;
    base = asExpr->operand.get();
  }

  std::size_t instanceIndex = 0;
  if (const auto *index = dynamic_cast<const ast::IndexExpr *>(base)) {
    const auto parsedIndex = structIndex(*index->index);
    if (!parsedIndex) {
      addDiagnostic("struct index must use [sN]");
      return std::nullopt;
    }
    instanceIndex = *parsedIndex;
    base = index->base.get();
  }

  const auto *identifier = dynamic_cast<const ast::IdentifierExpr *>(base);
  if (identifier == nullptr) {
    addDiagnostic("member access base must be an identifier");
    return std::nullopt;
  }

  const auto *symbol = lookup(identifier->name);
  if (symbol == nullptr) {
    addDiagnostic("use of undeclared variable '" + identifier->name + "'");
    return std::nullopt;
  }
  if (symbol->templateName.empty()) {
    addDiagnostic("member access requires a struct template for '" +
                  identifier->name + "'");
    return std::nullopt;
  }

  struct AggregateLayout final {
    const std::vector<StructMemberInfo> *members = nullptr;
    std::string_view name;
    std::size_t byteLength = 0;
  };
  const auto layoutFor = [&](std::string_view templateName)
      -> std::optional<AggregateLayout> {
    if (const auto structIt = structs_.find(std::string(templateName));
        structIt != structs_.end()) {
      return AggregateLayout{&structIt->second.members, structIt->second.name,
                             structIt->second.byteLength};
    }
    if (const auto templateIt = templates_.find(std::string(templateName));
        templateIt != templates_.end()) {
      return AggregateLayout{&templateIt->second.members,
                             templateIt->second.name,
                             templateIt->second.byteLength};
    }
    return std::nullopt;
  };

  std::string_view layoutTemplateName = symbol->templateName;
  if (baseViewTemplate) {
    if (*baseViewTemplate == "none" || *baseViewTemplate == "bytes" ||
        *baseViewTemplate == "cstr" ||
        templates_.find(std::string(*baseViewTemplate)) == templates_.end()) {
      addDiagnostic("member access requires a user template View");
      return std::nullopt;
    }
    const auto viewLength = templateByteLength(*baseViewTemplate);
    if (!viewLength || *viewLength != symbol->byteLength) {
      addDiagnostic("member access View byte length does not match its source");
      return std::nullopt;
    }
    layoutTemplateName = *baseViewTemplate;
  }

  auto layout = layoutFor(layoutTemplateName);
  if (!layout) {
    addDiagnostic("unknown struct template '" + std::string(layoutTemplateName) +
                  "'");
    return std::nullopt;
  }

  std::size_t offset = instanceIndex * layout->byteLength;
  std::string memberTemplateName;
  for (std::size_t index = 0; index < memberPath.size(); ++index) {
    const auto memberIt = std::find_if(
        layout->members->begin(), layout->members->end(),
        [&](const StructMemberInfo &candidate) {
          return candidate.name == memberPath[index];
        });
    if (memberIt == layout->members->end()) {
      addDiagnostic("unknown member '" + std::string(memberPath[index]) +
                    "' in struct '" + std::string(layout->name) + "'");
      return std::nullopt;
    }

    offset += memberIt->offset;
    memberTemplateName = memberIt->templateName;
    if (index + 1U == memberPath.size()) {
      if (offset + memberIt->byteLength > symbol->byteLength) {
        addDiagnostic("member access '" + identifier->name + "." +
                      std::string(memberPath.back()) + "' is out of bounds");
        return std::nullopt;
      }

      std::string key = symbol->bindingName;
      for (const auto pathSegment : memberPath) {
        key += '\x1f';
        key += pathSegment;
      }
      if (const auto override = memberTemplateOverrides_.find(key);
          override != memberTemplateOverrides_.end()) {
        memberTemplateName = override->second.value_or("");
      }
      return MemoryReference{symbol->name, symbol->bindingName,
                             memberIt->byteLength, symbol->storage, offset,
                             std::move(memberTemplateName)};
    }

    if (templates_.find(memberTemplateName) == templates_.end()) {
      addDiagnostic("member '" + std::string(memberPath[index]) +
                    "' must use a user template for nested member access");
      return std::nullopt;
    }
    layout = layoutFor(memberTemplateName);
    if (!layout) {
      addDiagnostic("unknown struct template '" + memberTemplateName + "'");
      return std::nullopt;
    }
  }

  return std::nullopt;
}

std::optional<MemoryReference>
Analyzer::resolveAddressableReference(const ast::Expr &expr) {
  if (const auto *identifier = dynamic_cast<const ast::IdentifierExpr *>(&expr)) {
    const auto *symbol = lookup(identifier->name);
    if (symbol == nullptr) {
      addDiagnostic("use of undeclared variable '" + identifier->name + "'");
      return std::nullopt;
    }
    return MemoryReference{symbol->name, symbol->bindingName,
                           symbol->byteLength, symbol->storage, 0,
                           symbol->templateName};
  }
  if (const auto *asExpr = dynamic_cast<const ast::AsExpr *>(&expr)) {
    auto reference = resolveAddressableReference(*asExpr->operand);
    if (!reference) {
      if (result_.diagnostics.empty()) {
        addDiagnostic("temporary template view is not addressable");
      }
      return std::nullopt;
    }
    if (asExpr->templateName == "none") {
      reference->templateName.clear();
      return reference;
    }
    if (asExpr->templateName == "bytes" || asExpr->templateName == "cstr") {
      reference->templateName = asExpr->templateName;
      return reference;
    }
    const auto viewLength = templateByteLength(asExpr->templateName);
    if (!viewLength) {
      addDiagnostic("unknown template '" + asExpr->templateName + "'");
      return std::nullopt;
    }
    if (*viewLength != reference->byteLength) {
      addDiagnostic("expression template view byte length does not match template '" +
                    asExpr->templateName + "'");
      return std::nullopt;
    }
    reference->templateName = asExpr->templateName;
    return reference;
  }
  return resolveMemoryReference(expr);
}

std::unique_ptr<hir::Expr> Analyzer::lowerIndexAddress(
    const ast::IndexExpr &expr) {
  const auto reference = resolveAddressableReference(*expr.base);
  if (!reference) {
    return nullptr;
  }
  if (reference->templateName == "handle") {
    addDiagnostic("handle values cannot be indexed");
    return nullptr;
  }
  auto index = analyze(*expr.index);
  if (!index) {
    return nullptr;
  }
  if (!isIntegerExpression(*index)) {
    addDiagnostic("index expression is not an integer expression");
    return nullptr;
  }
  auto base = std::make_unique<hir::AddressOfExpr>(
      reference->name, reference->bindingName, reference->byteLength,
      reference->storage, reference->offset, pointerByteLength());
  return std::make_unique<hir::BinaryExpr>(std::move(base), "+",
                                           std::move(index),
                                           pointerByteLength());
}

std::optional<SliceLowering> Analyzer::lowerSlice(const ast::SliceExpr &expr) {
  const auto reference = resolveAddressableReference(*expr.base);
  if (!reference) {
    return std::nullopt;
  }
  if (reference->templateName == "handle") {
    addDiagnostic("handle values cannot be sliced");
    return std::nullopt;
  }
  const auto startLiteral = integerLiteralValue(*expr.start);
  const auto endLiteral = integerLiteralValue(*expr.end);
  if (!endLiteral) {
    addDiagnostic(expr.lengthMode
                      ? "slice length must be an integer literal for now"
                      : "slice end must be an integer literal for now");
    return std::nullopt;
  }

  std::size_t byteLength = 0;
  if (expr.lengthMode) {
    byteLength = *endLiteral;
  } else {
    if (!startLiteral) {
      addDiagnostic("slice start must be an integer literal for start:end "
                    "slices for now");
      return std::nullopt;
    }
    if (*endLiteral < *startLiteral) {
      addDiagnostic("slice end must be greater than or equal to start");
      return std::nullopt;
    }
    byteLength = *endLiteral - *startLiteral;
  }
  if (byteLength == 0) {
    addDiagnostic("slice length must be greater than zero");
    return std::nullopt;
  }

  auto start = analyze(*expr.start);
  if (!start) {
    return std::nullopt;
  }
  if (!isIntegerExpression(*start)) {
    addDiagnostic("slice start is not an integer expression");
    return std::nullopt;
  }

  auto base = std::make_unique<hir::AddressOfExpr>(
      reference->name, reference->bindingName, reference->byteLength,
      reference->storage, reference->offset, pointerByteLength());
  SliceLowering lowered;
  lowered.address = std::make_unique<hir::BinaryExpr>(
      std::move(base), "+", std::move(start), pointerByteLength());
  lowered.byteLength = byteLength;
  return lowered;
}

std::optional<std::size_t>
Analyzer::inferByteLength(const ast::Expr &expression) {
  if (const auto *asExpr = dynamic_cast<const ast::AsExpr *>(&expression)) {
    return inferByteLength(*asExpr->operand);
  }
  if (const auto *binary = dynamic_cast<const ast::BinaryExpr *>(&expression)) {
    const auto leftTemplate = operatorTemplateName(*binary->left);
    const auto rightTemplate = operatorTemplateName(*binary->right);
    if (leftTemplate && rightTemplate) {
      const auto key = binary->op + "|" + *leftTemplate + "|" +
                       *rightTemplate;
      if (const auto found = implOpIndexes_.find(key);
          found != implOpIndexes_.end()) {
        const auto &info = implOpInfos_[found->second];
        if (info.returnByteLengths.size() == 1U) {
          return info.returnByteLengths.front();
        }
      }
    }
  }
  if (dynamic_cast<const ast::MemberExpr *>(&expression) != nullptr) {
    if (const auto reference = resolveMemoryReference(expression)) {
      return reference->byteLength;
    }
    return std::nullopt;
  }
  if (const auto *integer =
          dynamic_cast<const ast::IntegerLiteral *>(&expression)) {
    return inferIntegerLiteralByteLength(*integer);
  }
  if (const auto *string =
          dynamic_cast<const ast::StringLiteral *>(&expression)) {
    const auto decoded = literal::decodeStringLiteral(string->value);
    if (!decoded) {
      addDiagnostic("invalid string literal '" + string->value +
                    "': " + *decoded.error);
      return std::nullopt;
    }
    return decoded.bytes.size() + 1;
  }
  if (const auto *character =
          dynamic_cast<const ast::CharLiteral *>(&expression)) {
    const auto decoded = literal::decodeCharLiteral(character->value);
    if (!decoded) {
      addDiagnostic("invalid character literal '" + character->value +
                    "': " + *decoded.error);
      return std::nullopt;
    }
    return decoded.bytes.size();
  }
  if (dynamic_cast<const ast::FloatLiteral *>(&expression) != nullptr) {
    return 8;
  }
  if (const auto *cast =
          dynamic_cast<const ast::IntegerCastExpr *>(&expression)) {
    return cast->byteLength;
  }
  if (const auto *call = dynamic_cast<const ast::CallExpr *>(&expression)) {
    const auto *standard = stdlib::findBuiltin(call->callee);
    if (standard != nullptr && !functions_.contains(call->callee)) {
      addDiagnostic("standard library function '" + call->callee +
                    "' requires $include <" +
                    std::string(stdlib::headerName(standard->header)) + ">");
      return std::nullopt;
    }
    const auto found = functions_.find(call->callee);
    const auto builtin = found != functions_.end() ? found->second.builtin
                                                     : stdlib::BuiltinId::None;
    if (builtin == stdlib::BuiltinId::Length) {
      return pointerByteLength();
    }
    if (builtin == stdlib::BuiltinId::ResizeBytes &&
        call->arguments.size() == 2U) {
      if (const auto *length = dynamic_cast<const ast::IntegerLiteral *>(
              call->arguments[1].get())) {
        return parseByteLength(length->value);
      }
    }
    if (builtin == stdlib::BuiltinId::ByteSwap && call->arguments.size() == 1U) {
      return inferByteLength(*call->arguments[0]);
    }
    if (builtin == stdlib::BuiltinId::Abs && call->arguments.size() == 1U) {
      return inferByteLength(*call->arguments[0]);
    }
    if ((builtin == stdlib::BuiltinId::Min || builtin == stdlib::BuiltinId::Max) &&
        call->arguments.size() == 2U) {
      const auto left = inferByteLength(*call->arguments[0]);
      const auto right = inferByteLength(*call->arguments[1]);
      if (left && right) {
        return std::max(*left, *right);
      }
    }
    if (builtin == stdlib::BuiltinId::FAbs ||
        builtin == stdlib::BuiltinId::FSqrt ||
        builtin == stdlib::BuiltinId::FPow ||
        builtin == stdlib::BuiltinId::FSin ||
        builtin == stdlib::BuiltinId::FCos ||
        builtin == stdlib::BuiltinId::FTan ||
        builtin == stdlib::BuiltinId::FLog ||
        builtin == stdlib::BuiltinId::FExp ||
        builtin == stdlib::BuiltinId::FFloor ||
        builtin == stdlib::BuiltinId::FCeil ||
        builtin == stdlib::BuiltinId::FRound) {
      if (!call->arguments.empty()) {
        return inferByteLength(*call->arguments.front());
      }
    }
    if (found != functions_.end() &&
        found->second.returnByteLengths.size() == 1U) {
      return found->second.returnByteLengths.front();
    }
  }
  if (const auto *call = dynamic_cast<const ast::MethodCallExpr *>(&expression)) {
    const auto receiverTemplate = expressionTemplateName(*call->receiver);
    if (!receiverTemplate) {
      return std::nullopt;
    }
    std::vector<std::string> parameterTemplateNames;
    parameterTemplateNames.reserve(call->arguments.size() + 1U);
    parameterTemplateNames.push_back(*receiverTemplate);
    for (const auto &argument : call->arguments) {
      const auto argumentTemplate = operatorTemplateName(*argument);
      if (!argumentTemplate) {
        return std::nullopt;
      }
      parameterTemplateNames.push_back(*argumentTemplate);
    }
    const auto *method = findImplMethod(*receiverTemplate, call->method,
                                        parameterTemplateNames);
    if (method != nullptr && method->returnByteLengths.size() == 1U) {
      return method->returnByteLengths.front();
    }
  }
  if (const auto *identifier =
          dynamic_cast<const ast::IdentifierExpr *>(&expression)) {
    if (const auto *symbol = lookup(identifier->name)) {
      return symbol->byteLength;
    }
  }
  return std::nullopt;
}

std::optional<std::string>
Analyzer::expressionTemplateName(const ast::Expr &expression) {
  if (const auto *identifier =
          dynamic_cast<const ast::IdentifierExpr *>(&expression)) {
    if (const auto *symbol = lookup(identifier->name);
        symbol != nullptr && templates_.find(symbol->templateName) != templates_.end()) {
      return symbol->templateName;
    }
  }
  if (const auto *asExpr = dynamic_cast<const ast::AsExpr *>(&expression)) {
    if (templates_.find(asExpr->templateName) != templates_.end()) {
      return asExpr->templateName;
    }
  }
  if (dynamic_cast<const ast::MemberExpr *>(&expression) != nullptr) {
    const auto reference = resolveMemoryReference(expression);
    if (reference && templates_.find(reference->templateName) != templates_.end()) {
      return reference->templateName;
    }
  }
  if (const auto *call = dynamic_cast<const ast::MethodCallExpr *>(&expression)) {
    const auto returnTemplate = operatorTemplateName(*call);
    if (returnTemplate && templates_.find(*returnTemplate) != templates_.end()) {
      return returnTemplate;
    }
  }
  return std::nullopt;
}

std::optional<std::string>
Analyzer::operatorTemplateName(const ast::Expr &expression) {
  if (const auto *identifier =
          dynamic_cast<const ast::IdentifierExpr *>(&expression)) {
    if (const auto *symbol = lookup(identifier->name);
        symbol != nullptr && !symbol->templateName.empty() &&
        symbol->templateName != "none") {
      return symbol->templateName;
    }
  }
  if (const auto *asExpr = dynamic_cast<const ast::AsExpr *>(&expression)) {
    if (asExpr->templateName != "none") {
      return asExpr->templateName;
    }
  }
  if (const auto *call = dynamic_cast<const ast::CallExpr *>(&expression)) {
    if (const auto found = functions_.find(call->callee);
        found != functions_.end() && found->second.returnsKnown &&
        found->second.returnTemplateNames.size() == 1U) {
      const auto &returnTemplate = found->second.returnTemplateNames.front();
      if (!returnTemplate.empty() && returnTemplate != "none") {
        return returnTemplate;
      }
    }
  }
  if (const auto *binary = dynamic_cast<const ast::BinaryExpr *>(&expression)) {
    const auto leftTemplate = operatorTemplateName(*binary->left);
    const auto rightTemplate = operatorTemplateName(*binary->right);
    if (leftTemplate && rightTemplate) {
      const auto key = binary->op + "|" + *leftTemplate + "|" +
                       *rightTemplate;
      if (const auto found = implOpIndexes_.find(key);
          found != implOpIndexes_.end()) {
        const auto &info = implOpInfos_[found->second];
        if (info.returnTemplateNames.size() == 1U) {
          return info.returnTemplateNames.front();
        }
      }
    }
  }
  if (dynamic_cast<const ast::MemberExpr *>(&expression) != nullptr) {
    const auto reference = resolveMemoryReference(expression);
    if (reference && !reference->templateName.empty() &&
        reference->templateName != "none") {
      return reference->templateName;
    }
  }
  if (const auto *call = dynamic_cast<const ast::MethodCallExpr *>(&expression)) {
    const auto receiverTemplate = expressionTemplateName(*call->receiver);
    if (!receiverTemplate) {
      return std::nullopt;
    }
    std::vector<std::string> parameterTemplateNames;
    parameterTemplateNames.reserve(call->arguments.size() + 1U);
    parameterTemplateNames.push_back(*receiverTemplate);
    for (const auto &argument : call->arguments) {
      const auto argumentTemplate = operatorTemplateName(*argument);
      if (!argumentTemplate) {
        return std::nullopt;
      }
      parameterTemplateNames.push_back(*argumentTemplate);
    }
    const auto *method = findImplMethod(*receiverTemplate, call->method,
                                        parameterTemplateNames);
    if (method != nullptr && method->returnTemplateNames.size() == 1U) {
      return method->returnTemplateNames.front();
    }
  }
  return std::nullopt;
}

UserTemplateViewAssignmentCompatibility
Analyzer::userTemplateViewAssignmentCompatibility(
    std::string_view destinationTemplate, const ast::Expr &source) {
  if (!templates_.contains(std::string(destinationTemplate))) {
    return UserTemplateViewAssignmentCompatibility::Compatible;
  }
  const auto sourceTemplate = operatorTemplateName(source);
  if (!sourceTemplate || !templates_.contains(*sourceTemplate)) {
    return UserTemplateViewAssignmentCompatibility::SourceIsNotUserTemplate;
  }
  if (*sourceTemplate != destinationTemplate) {
    return UserTemplateViewAssignmentCompatibility::TemplateMismatch;
  }
  return UserTemplateViewAssignmentCompatibility::Compatible;
}

hir::FormatArgKind
Analyzer::formatArgumentKind(const ast::Expr &expression,
                             const hir::Expr &lowered) {
  const auto *call = dynamic_cast<const hir::CallExpr *>(&lowered);
  if (dynamic_cast<const hir::FloatLiteral *>(&lowered) != nullptr ||
      dynamic_cast<const hir::FloatBinaryExpr *>(&lowered) != nullptr ||
      dynamic_cast<const hir::ToFloatExpr *>(&lowered) != nullptr ||
      (call != nullptr && call->isFloating)) {
    return hir::FormatArgKind::Float;
  }
  if (dynamic_cast<const hir::StringLiteral *>(&lowered) != nullptr) {
    return hir::FormatArgKind::String;
  }

  std::string_view templateName;
  if (const auto *identifier =
          dynamic_cast<const ast::IdentifierExpr *>(&expression)) {
    if (const auto *symbol = lookup(identifier->name)) {
      templateName = symbol->templateName;
    }
  } else if (const auto *asExpr = dynamic_cast<const ast::AsExpr *>(&expression)) {
    templateName = asExpr->templateName;
  } else if (const auto reference = resolveMemoryReference(expression)) {
    templateName = reference->templateName;
  }

  if (templateName == "f16" || templateName == "f32" ||
      templateName == "f64" || templateName == "f128") {
    return hir::FormatArgKind::Float;
  }
  if (templateName == "cstr") {
    return hir::FormatArgKind::String;
  }
  return hir::FormatArgKind::Bytes;
}

bool Analyzer::isHandleExpression(const ast::Expr &expression) {
  if (const auto *asExpr = dynamic_cast<const ast::AsExpr *>(&expression)) {
    return asExpr->templateName == "handle";
  }

  if (const auto *identifier =
          dynamic_cast<const ast::IdentifierExpr *>(&expression)) {
    const auto *symbol = lookup(identifier->name);
    return symbol != nullptr && symbol->templateName == "handle";
  }

  if (const auto *member = dynamic_cast<const ast::MemberExpr *>(&expression)) {
    const auto reference = resolveMemoryReference(*member);
    return reference && reference->templateName == "handle";
  }

  const auto *call = dynamic_cast<const ast::CallExpr *>(&expression);
  if (call == nullptr) {
    return false;
  }
  if (builtinForCall(*call) == stdlib::BuiltinId::Fopen) {
    return true;
  }
  const auto found = functions_.find(call->callee);
  if (found == functions_.end()) {
    return false;
  }
  const auto &signature = found->second;
  return signature.returnsKnown &&
         signature.returnTemplateNames.size() == 1U &&
         signature.returnTemplateNames.front() == "handle";
}

bool Analyzer::validateHandleCallArguments(const ast::CallExpr &call,
                                           const FunctionSignature &signature) {
  const auto count = std::min(call.arguments.size(),
                              signature.parameterTemplateNames.size());
  for (std::size_t index = 0; index < count; ++index) {
    const bool expectsHandle =
        signature.parameterTemplateNames[index] == "handle";
    const bool isHandle = isHandleExpression(*call.arguments[index]);
    if (expectsHandle && !isHandle) {
      addDiagnostic("function call '" + call.callee + "' argument " +
                    std::to_string(index + 1U) + " must be a handle");
      return false;
    }
    if (!expectsHandle && isHandle) {
      addDiagnostic("handle argument may only be passed to a handle parameter");
      return false;
    }
  }
  return true;
}

} // namespace hitsimple::sema
