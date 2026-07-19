#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Header final {
  std::string id;
  std::string file;
  std::string guard;
  std::string summary;
};

struct Parameter final {
  std::string mode;
  std::string bootstrapType;
  std::string templateName;
};

struct Overload final {
  std::vector<std::string> parameters;
  std::vector<std::string> results;
};

struct Builtin final {
  std::string id;
  std::string name;
  std::string visibility;
  std::string standardSection;
  std::string header;
  std::vector<Parameter> parameters;
  std::vector<Parameter> results;
  std::vector<Overload> overloads;
  std::string returnMode;
  std::string provider;
  std::string referenceProvider;
  std::string sourceModule;
  std::string implementationSymbol;
  std::vector<std::string> staticDiagnostics;
  std::vector<std::string> checkedObligations;
  std::vector<std::string> testOwners;
  std::string headerDeclaration;
};

struct SourceModule final {
  std::string id;
  std::string file;
  std::vector<std::string> dependencies;
};

struct Manifest final {
  std::vector<Header> headers;
  std::vector<SourceModule> sourceModules;
  std::vector<std::string> defaultTestOwners;
  std::vector<Builtin> builtins;
};

bool isIdentifier(std::string_view value) {
  return !value.empty() &&
         std::isalpha(static_cast<unsigned char>(value.front())) != 0 &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return std::isalnum(character) != 0 || character == '_';
         });
}

bool isSymbol(std::string_view value) {
  return !value.empty() &&
         (std::isalpha(static_cast<unsigned char>(value.front())) != 0 ||
          value.front() == '_') &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return std::isalnum(character) != 0 || character == '_';
         });
}

bool readString(const llvm::json::Object &object, llvm::StringRef field,
                std::string &result, std::string &error) {
  const auto value = object.getString(field);
  if (!value) {
    error = "missing string field '" + field.str() + "'";
    return false;
  }
  result = value->str();
  return true;
}

bool readStringArray(const llvm::json::Object &object, llvm::StringRef field,
                     std::vector<std::string> &result, std::string &error) {
  const auto *array = object.getArray(field);
  if (array == nullptr) {
    error = "missing array field '" + field.str() + "'";
    return false;
  }
  for (const auto &value : *array) {
    const auto string = value.getAsString();
    if (!string) {
      error = "array field '" + field.str() + "' must contain strings";
      return false;
    }
    result.emplace_back(string->str());
  }
  return true;
}

bool readOptionalStringArray(const llvm::json::Object &object,
                             llvm::StringRef field,
                             std::vector<std::string> &result,
                             std::string &error) {
  if (object.get(field) == nullptr) {
    return true;
  }
  return readStringArray(object, field, result, error);
}

bool parseParameter(const llvm::json::Value &value, Parameter &result,
                    std::string &error) {
  const auto *array = value.getAsArray();
  if (array == nullptr || array->size() != 3U) {
    error = "a parameter or result must be [mode, bootstrapType, templateName]";
    return false;
  }
  const auto mode = (*array)[0].getAsString();
  const auto bootstrapType = (*array)[1].getAsString();
  const auto templateName = (*array)[2].getAsString();
  if (!mode || !bootstrapType || !templateName) {
    error = "a parameter or result item must contain only strings";
    return false;
  }
  result = Parameter{mode->str(), bootstrapType->str(), templateName->str()};
  return true;
}

bool parseParameterArray(const llvm::json::Object &object, llvm::StringRef field,
                         std::vector<Parameter> &result, std::string &error) {
  const auto *array = object.getArray(field);
  if (array == nullptr) {
    error = "missing array field '" + field.str() + "'";
    return false;
  }
  for (const auto &value : *array) {
    Parameter parameter;
    if (!parseParameter(value, parameter, error)) {
      return false;
    }
    result.push_back(std::move(parameter));
  }
  return true;
}

bool parseModeArray(const llvm::json::Value &value, std::vector<std::string> &result,
                    std::string &error) {
  const auto *array = value.getAsArray();
  if (array == nullptr) {
    error = "an overload mode list must be an array";
    return false;
  }
  for (const auto &item : *array) {
    const auto mode = item.getAsString();
    if (!mode) {
      error = "an overload mode must be a string";
      return false;
    }
    result.emplace_back(mode->str());
  }
  return true;
}

bool parseOverloads(const llvm::json::Object &object, std::vector<Overload> &result,
                    std::string &error) {
  const auto *array = object.getArray("overloads");
  if (array == nullptr || array->empty()) {
    error = "every builtin must declare at least one overload";
    return false;
  }
  for (const auto &value : *array) {
    const auto *overload = value.getAsArray();
    if (overload == nullptr || overload->size() != 2U) {
      error = "an overload must be [parameterModes, resultModes]";
      return false;
    }
    Overload parsed;
    if (!parseModeArray((*overload)[0], parsed.parameters, error) ||
        !parseModeArray((*overload)[1], parsed.results, error)) {
      return false;
    }
    result.push_back(std::move(parsed));
  }
  return true;
}

bool parseManifest(const std::filesystem::path &path, Manifest &manifest,
                   std::string &error) {
  const auto buffer = llvm::MemoryBuffer::getFile(path.string());
  if (!buffer) {
    error = "cannot read manifest '" + path.string() + "'";
    return false;
  }
  auto value = llvm::json::parse((*buffer)->getBuffer());
  if (!value) {
    error = "manifest is not valid JSON: " + llvm::toString(value.takeError());
    return false;
  }
  const auto *root = value->getAsObject();
  if (root == nullptr || root->getInteger("version") != 1) {
    error = "manifest must be a version 1 JSON object";
    return false;
  }
  const auto *headers = root->getArray("headers");
  const auto *sourceModules = root->getArray("sourceModules");
  const auto *builtins = root->getArray("builtins");
  if (headers == nullptr || sourceModules == nullptr || builtins == nullptr ||
      !readStringArray(*root, "defaultTestOwners", manifest.defaultTestOwners,
                       error)) {
    if (error.empty()) {
      error = "manifest requires headers, sourceModules, builtins, and defaultTestOwners arrays";
    }
    return false;
  }
  for (const auto &value : *headers) {
    const auto *object = value.getAsObject();
    Header header;
    if (object == nullptr || !readString(*object, "id", header.id, error) ||
        !readString(*object, "file", header.file, error) ||
        !readString(*object, "guard", header.guard, error) ||
        !readString(*object, "summary", header.summary, error)) {
      return false;
    }
    manifest.headers.push_back(std::move(header));
  }
  for (const auto &value : *sourceModules) {
    const auto *object = value.getAsObject();
    SourceModule module;
    if (object == nullptr || !readString(*object, "id", module.id, error) ||
        !readString(*object, "file", module.file, error) ||
        !readStringArray(*object, "dependencies", module.dependencies, error)) {
      return false;
    }
    manifest.sourceModules.push_back(std::move(module));
  }
  for (const auto &value : *builtins) {
    const auto *object = value.getAsObject();
    Builtin builtin;
    if (object == nullptr || !readString(*object, "id", builtin.id, error) ||
        !readString(*object, "name", builtin.name, error) ||
        !readString(*object, "visibility", builtin.visibility, error) ||
        !readString(*object, "standardSection", builtin.standardSection, error) ||
        !readString(*object, "header", builtin.header, error) ||
        !parseParameterArray(*object, "parameters", builtin.parameters, error) ||
        !parseParameterArray(*object, "results", builtin.results, error) ||
        !parseOverloads(*object, builtin.overloads, error) ||
        !readString(*object, "returnMode", builtin.returnMode, error) ||
        !readString(*object, "provider", builtin.provider, error) ||
        !readString(*object, "referenceProvider", builtin.referenceProvider, error) ||
        !readString(*object, "sourceModule", builtin.sourceModule, error) ||
        !readString(*object, "implementationSymbol", builtin.implementationSymbol, error) ||
        !readStringArray(*object, "staticDiagnostics", builtin.staticDiagnostics, error) ||
        !readStringArray(*object, "checkedObligations", builtin.checkedObligations, error) ||
        !readOptionalStringArray(*object, "testOwners", builtin.testOwners, error) ||
        !readString(*object, "headerDeclaration", builtin.headerDeclaration, error)) {
      return false;
    }
    if (builtin.testOwners.empty()) {
      builtin.testOwners = manifest.defaultTestOwners;
    }
    manifest.builtins.push_back(std::move(builtin));
  }
  return true;
}

bool knownMode(std::string_view mode) {
  static constexpr std::string_view kModes[] = {
      "View", "LView", "MemView", "MemLView", "CStrView", "BytesView",
      "Addr", "Handle", "Bool", "I8", "I16", "I32", "I64", "U8",
      "U16", "U32", "U64", "F16", "F32", "F64", "F128", "Integer",
      "Floating", "SameType", "None", "LeftContext", "Varargs"};
  return std::find(std::begin(kModes), std::end(kModes), mode) != std::end(kModes);
}

bool knownBootstrapType(std::string_view type) {
  static constexpr std::string_view kTypes[] = {
      "Void", "Pointer", "Bytes1", "Bytes2", "Bytes4", "Bytes8", "Bytes16"};
  return std::find(std::begin(kTypes), std::end(kTypes), type) != std::end(kTypes);
}

bool knownProvider(std::string_view provider) {
  static constexpr std::string_view kProviders[] = {
      "None", "Semantic", "Intrinsic", "CoreHs", "RuntimeBridge",
      "LibcBridge", "FormatProtocol"};
  return std::find(std::begin(kProviders), std::end(kProviders), provider) !=
         std::end(kProviders);
}

bool knownReturnMode(std::string_view mode) {
  static constexpr std::string_view kModes[] = {
      "Void", "Fixed", "ArgumentLength", "DynamicLength", "LeftContext"};
  return std::find(std::begin(kModes), std::end(kModes), mode) != std::end(kModes);
}

bool validateManifest(const Manifest &manifest,
                      const std::filesystem::path &sourceRoot,
                      std::string &error) {
  if (manifest.headers.size() != 7U) {
    error = "manifest must declare exactly seven standard headers";
    return false;
  }
  std::set<std::string> headerIds;
  std::set<std::string> headerFiles;
  for (const auto &header : manifest.headers) {
    if (!isIdentifier(header.id) || header.file.empty() || header.guard.empty() ||
        !headerIds.insert(header.id).second || !headerFiles.insert(header.file).second) {
      error = "standard headers require unique identifier and file names";
      return false;
    }
  }
  std::set<std::string> moduleIds;
  std::set<std::string> moduleFiles;
  for (const auto &module : manifest.sourceModules) {
    const std::filesystem::path file(module.file);
    if (!isIdentifier(module.id) || module.file.empty() ||
        file.extension() != ".hs" || file.has_parent_path() ||
        !moduleIds.insert(module.id).second ||
        !moduleFiles.insert(module.file).second) {
      error = "source modules require unique identifiers and local .hs files";
      return false;
    }
  }
  for (const auto &module : manifest.sourceModules) {
    std::set<std::string> dependencies;
    for (const auto &dependency : module.dependencies) {
      if (!moduleIds.contains(dependency) || dependency == module.id ||
          !dependencies.insert(dependency).second) {
        error = "source module dependencies must be unique known module identifiers";
        return false;
      }
    }
  }
  std::set<std::string> ids;
  std::set<std::string> names;
  for (const auto &builtin : manifest.builtins) {
    if (!isIdentifier(builtin.id) || builtin.name.empty() ||
        !ids.insert(builtin.id).second || !names.insert(builtin.name).second ||
        !headerIds.contains(builtin.header) || !knownProvider(builtin.provider) ||
        !knownProvider(builtin.referenceProvider) ||
        !knownReturnMode(builtin.returnMode)) {
      error = "builtin identifiers, names, headers, providers, and return modes must be valid and unique";
      return false;
    }
    if (builtin.visibility != "Public" && builtin.visibility != "Internal") {
      error = "builtin visibility must be Public or Internal";
      return false;
    }
    if (builtin.visibility == "Public" &&
        (builtin.standardSection.empty() || builtin.headerDeclaration.rfind("extern ", 0U) != 0U ||
         builtin.headerDeclaration.find("->") == std::string::npos)) {
      error = "a public builtin requires a section and official extern declaration";
      return false;
    }
    std::set<std::string> testOwners;
    for (const auto &owner : builtin.testOwners) {
      const std::filesystem::path path(owner);
      if (owner.empty() || path.is_absolute() || path.empty() ||
          path.begin() == path.end() || *path.begin() != "tests" ||
          std::any_of(path.begin(), path.end(), [](const auto &component) {
            return component == "..";
          }) ||
          !testOwners.insert(owner).second ||
          !std::filesystem::is_regular_file(sourceRoot / path)) {
        error = "builtin test owners must be unique existing files below tests/";
        return false;
      }
    }
    if (builtin.visibility == "Public" && builtin.testOwners.empty()) {
      error = "a public builtin requires at least one test owner";
      return false;
    }
    const bool usesCoreHs = builtin.provider == "CoreHs" ||
                            builtin.referenceProvider == "CoreHs";
    if ((usesCoreHs &&
         (!moduleIds.contains(builtin.sourceModule) ||
          !isSymbol(builtin.implementationSymbol))) ||
        (!builtin.sourceModule.empty() && !moduleIds.contains(builtin.sourceModule))) {
      error = "CoreHs builtins require a known source module and implementation symbol";
      return false;
    }
    for (const auto &parameter : builtin.parameters) {
      if (!knownMode(parameter.mode) || !knownBootstrapType(parameter.bootstrapType)) {
        error = "builtin parameter has an unknown mode or bootstrap type";
        return false;
      }
    }
    for (const auto &result : builtin.results) {
      if (!knownMode(result.mode) || !knownBootstrapType(result.bootstrapType)) {
        error = "builtin result has an unknown mode or bootstrap type";
        return false;
      }
    }
    for (const auto &overload : builtin.overloads) {
      for (const auto &mode : overload.parameters) {
        if (!knownMode(mode)) {
          error = "builtin overload has an unknown parameter mode";
          return false;
        }
      }
      for (const auto &mode : overload.results) {
        if (!knownMode(mode)) {
          error = "builtin overload has an unknown result mode";
          return false;
        }
      }
    }
  }
  return true;
}

std::string quote(std::string_view value) {
  std::string result{"\""};
  for (const char character : value) {
    switch (character) {
    case '\\': result += "\\\\"; break;
    case '\"': result += "\\\""; break;
    case '\n': result += "\\n"; break;
    case '\r': result += "\\r"; break;
    case '\t': result += "\\t"; break;
    default: result += character; break;
    }
  }
  result += '\"';
  return result;
}

template <typename Value>
void emitModeArray(std::ostream &out, std::string_view name,
                   const std::vector<Value> &values) {
  if (values.empty()) {
    return;
  }
  out << "constexpr std::array<BuiltinParameterMode, " << values.size() << "> "
      << name << " = {{";
  for (const auto &value : values) {
    out << "BuiltinParameterMode::" << value << ", ";
  }
  out << "}};\n";
}

template <typename Value>
void emitStringArray(std::ostream &out, std::string_view name,
                     const std::vector<Value> &values) {
  if (values.empty()) {
    return;
  }
  out << "constexpr std::array<std::string_view, " << values.size() << "> "
      << name << " = {{";
  for (const auto &value : values) {
    out << quote(value) << ", ";
  }
  out << "}};\n";
}

std::string spanExpression(std::string_view type, std::string_view name,
                           std::size_t size) {
  if (size == 0U) {
    return "{}";
  }
  return "std::span<const " + std::string(type) + ">(" + std::string(name) + ")";
}

void emitParameters(std::ostream &out, std::string_view name,
                    const std::vector<Parameter> &parameters,
                    std::string_view type) {
  if (parameters.empty()) {
    return;
  }
  out << "constexpr std::array<" << type << ", " << parameters.size() << "> "
      << name << " = {{\n";
  for (const auto &parameter : parameters) {
    out << "    {BuiltinParameterMode::" << parameter.mode
        << ", BuiltinBootstrapType::" << parameter.bootstrapType << ", "
        << quote(parameter.templateName);
    if (type == "BuiltinParameter") {
      out << ", " << (parameter.mode == "CStrView" ? "true" : "false");
    }
    out << "},\n";
  }
  out << "}};\n";
}

bool writeGeneratedCpp(const Manifest &manifest, const std::filesystem::path &path,
                       std::string &error) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  if (!out) {
    error = "cannot write generated registry '" + path.string() + "'";
    return false;
  }
  out << "// Generated by StandardLibraryManifestTool. Do not edit.\n"
      << "#include \"hitsimple/stdlib/StandardLibrary.h\"\n\n"
      << "#include <array>\n#include <cstddef>\n\n"
      << "namespace hitsimple::stdlib {\nnamespace {\n";
  out << "constexpr std::array<StandardHeader, " << manifest.headers.size()
      << "> kHeaders = {{";
  for (const auto &header : manifest.headers) {
    out << "StandardHeader::" << header.id << ", ";
  }
  out << "}};\n\n";
  for (const auto &module : manifest.sourceModules) {
    emitStringArray(out, "k" + module.id + "Dependencies", module.dependencies);
  }
  out << "constexpr std::array<SourceModuleSpec, "
      << manifest.sourceModules.size() << "> kSourceModules = {{\n";
  for (const auto &module : manifest.sourceModules) {
    out << "    {" << quote(module.id) << ", " << quote(module.file) << ", "
        << spanExpression("std::string_view", "k" + module.id + "Dependencies",
                          module.dependencies.size())
        << "},\n";
  }
  out << "}};\n\n";
  for (const auto &builtin : manifest.builtins) {
    const std::string prefix = "k" + builtin.id;
    emitParameters(out, prefix + "Parameters", builtin.parameters,
                   "BuiltinParameter");
    emitParameters(out, prefix + "Results", builtin.results, "BuiltinResult");
    emitStringArray(out, prefix + "StaticDiagnostics", builtin.staticDiagnostics);
    emitStringArray(out, prefix + "CheckedObligations", builtin.checkedObligations);
    emitStringArray(out, prefix + "TestOwners", builtin.testOwners);
    for (std::size_t index = 0; index < builtin.overloads.size(); ++index) {
      const auto &overload = builtin.overloads[index];
      emitModeArray(out, prefix + "Overload" + std::to_string(index) + "Parameters",
                    overload.parameters);
      emitModeArray(out, prefix + "Overload" + std::to_string(index) + "Results",
                    overload.results);
    }
    out << "constexpr std::array<BuiltinOverload, " << builtin.overloads.size()
        << "> " << prefix << "Overloads = {{\n";
    for (std::size_t index = 0; index < builtin.overloads.size(); ++index) {
      const auto &overload = builtin.overloads[index];
      const std::string overloadPrefix = prefix + "Overload" + std::to_string(index);
      out << "    {"
          << spanExpression("BuiltinParameterMode", overloadPrefix + "Parameters",
                            overload.parameters.size())
          << ", "
          << spanExpression("BuiltinParameterMode", overloadPrefix + "Results",
                            overload.results.size())
          << "},\n";
    }
    out << "}};\n\n";
  }
  out << "constexpr std::array<BuiltinSpec, " << manifest.builtins.size()
      << "> kBuiltins = {{\n";
  for (const auto &builtin : manifest.builtins) {
    const std::string prefix = "k" + builtin.id;
    out << "    {BuiltinId::" << builtin.id << ", " << quote(builtin.name)
        << ", BuiltinVisibility::" << builtin.visibility << ", "
        << quote(builtin.standardSection) << ", StandardHeader::" << builtin.header
        << ", " << spanExpression("BuiltinParameter", prefix + "Parameters",
                                  builtin.parameters.size())
        << ", " << spanExpression("BuiltinResult", prefix + "Results", builtin.results.size())
        << ", std::span<const BuiltinOverload>(" << prefix << "Overloads), "
        << "BuiltinReturnMode::" << builtin.returnMode << ", BuiltinProvider::"
        << builtin.provider << ", BuiltinProvider::" << builtin.referenceProvider
        << ", " << quote(builtin.sourceModule) << ", "
        << quote(builtin.implementationSymbol) << ", "
        << spanExpression("std::string_view", prefix + "StaticDiagnostics",
                          builtin.staticDiagnostics.size())
        << ", " << spanExpression("std::string_view", prefix + "CheckedObligations",
                                  builtin.checkedObligations.size())
        << ", " << spanExpression("std::string_view", prefix + "TestOwners",
                                  builtin.testOwners.size())
        << ", " << quote(builtin.headerDeclaration) << "},\n";
  }
  out << "}};\n\n"
      << "constexpr bool providerImplemented(BuiltinProvider provider) {\n"
      << "  switch (provider) {\n"
      << "  case BuiltinProvider::Semantic:\n  case BuiltinProvider::Intrinsic:\n"
      << "  case BuiltinProvider::RuntimeBridge:\n  case BuiltinProvider::LibcBridge:\n"
      << "  case BuiltinProvider::FormatProtocol: return true;\n"
      << "  case BuiltinProvider::None:\n  case BuiltinProvider::CoreHs: return false;\n"
      << "  }\n  return false;\n}\n\n"
      << "constexpr bool hasCompleteBuiltinIdCoverage() {\n"
      << "  std::array<bool, static_cast<std::size_t>(BuiltinId::Count)> seen{};\n"
      << "  for (const auto& spec : kBuiltins) {\n"
      << "    const auto index = static_cast<std::size_t>(spec.id);\n"
      << "    if (index == 0U || index >= seen.size() || seen[index] ||\n"
      << "        spec.visibility != BuiltinVisibility::Public ||\n"
      << "        spec.headerDeclaration.empty() || !providerImplemented(spec.provider)) return false;\n"
      << "    seen[index] = true;\n  }\n"
      << "  for (std::size_t index = 1; index < seen.size(); ++index) if (!seen[index]) return false;\n"
      << "  return true;\n}\n\n"
      << "static_assert(hasCompleteBuiltinIdCoverage(),\n"
      << "              \"manifest must cover every public BuiltinId exactly once\");\n"
      << "constexpr bool overloadModeMatches(BuiltinParameterMode mode, std::string_view templateName) {\n"
      << "  switch (mode) {\n"
      << "  case BuiltinParameterMode::I8: return templateName == \"i8\";\n"
      << "  case BuiltinParameterMode::I16: return templateName == \"i16\";\n"
      << "  case BuiltinParameterMode::I32: return templateName == \"i32\";\n"
      << "  case BuiltinParameterMode::I64: return templateName == \"i64\";\n"
      << "  case BuiltinParameterMode::U8: return templateName == \"u8\";\n"
      << "  case BuiltinParameterMode::U16: return templateName == \"u16\";\n"
      << "  case BuiltinParameterMode::U32: return templateName == \"u32\";\n"
      << "  case BuiltinParameterMode::U64: return templateName == \"u64\";\n"
      << "  case BuiltinParameterMode::F16: return templateName == \"f16\";\n"
      << "  case BuiltinParameterMode::F32: return templateName == \"f32\";\n"
      << "  case BuiltinParameterMode::F64: return templateName == \"f64\";\n"
      << "  case BuiltinParameterMode::F128: return templateName == \"f128\";\n"
      << "  case BuiltinParameterMode::Bool: return templateName == \"bool\";\n"
      << "  case BuiltinParameterMode::Addr: return templateName == \"addr\";\n"
      << "  case BuiltinParameterMode::Handle: return templateName == \"handle\";\n"
      << "  case BuiltinParameterMode::CStrView: return templateName.empty() || templateName == \"cstr\";\n"
      << "  default: return true;\n  }\n}\n\n"
      << "} // namespace\n\n"
      << "std::span<const BuiltinSpec> builtinSpecs() { return kBuiltins; }\n\n"
      << "const BuiltinSpec* findBuiltin(std::string_view name) {\n"
      << "  for (const auto& spec : kBuiltins) if (spec.name == name) return &spec;\n"
      << "  return nullptr;\n}\n\n"
      << "const BuiltinSpec* findBuiltin(BuiltinId id) {\n"
      << "  for (const auto& spec : kBuiltins) if (spec.id == id) return &spec;\n"
      << "  return nullptr;\n}\n\n"
      << "bool isStandardLibraryImplementationSymbol(std::string_view name) {\n"
      << "  for (const auto& spec : kBuiltins) if (!spec.sourceModule.empty() && spec.implementationSymbol == name) return true;\n"
      << "  return false;\n}\n\n"
      << "BuiltinCallMetadata builtinCallMetadata(BuiltinId id, std::uint16_t overloadIndex) {\n"
      << "  if (const auto* spec = findBuiltin(id)) return {id, spec->provider, spec->returnMode, overloadIndex};\n"
      << "  return {};\n}\n\n"
      << "std::uint16_t findBuiltinOverload(BuiltinId id, std::span<const std::string_view> argumentTemplates) {\n"
      << "  const auto* spec = findBuiltin(id);\n  if (spec == nullptr) return 0;\n"
      << "  for (std::size_t index = 0; index < spec->overloads.size(); ++index) {\n"
      << "    const auto& overload = spec->overloads[index];\n    bool matches = true;\n"
      << "    std::size_t argumentIndex = 0;\n"
      << "    for (; argumentIndex < overload.parameterModes.size(); ++argumentIndex) {\n"
      << "      const auto mode = overload.parameterModes[argumentIndex];\n"
      << "      if (mode == BuiltinParameterMode::Varargs) { matches = argumentTemplates.size() >= argumentIndex; break; }\n"
      << "      if (argumentIndex >= argumentTemplates.size() || !overloadModeMatches(mode, argumentTemplates[argumentIndex])) { matches = false; break; }\n"
      << "    }\n"
      << "    const bool hasVarargs = !overload.parameterModes.empty() && overload.parameterModes.back() == BuiltinParameterMode::Varargs;\n"
      << "    if (matches && (hasVarargs || argumentTemplates.size() == overload.parameterModes.size())) return static_cast<std::uint16_t>(index);\n"
      << "  }\n  return 0;\n}\n\n"
      << "std::string_view headerName(StandardHeader header) {\n  switch (header) {\n";
  for (const auto &header : manifest.headers) {
    out << "  case StandardHeader::" << header.id << ": return " << quote(header.file)
        << ";\n";
  }
  out << "  case StandardHeader::Count: return {};\n  }\n  return {};\n}\n\n"
      << "std::string_view headerGuard(StandardHeader header) {\n  switch (header) {\n";
  for (const auto &header : manifest.headers) {
    out << "  case StandardHeader::" << header.id << ": return " << quote(header.guard)
        << ";\n";
  }
  out << "  case StandardHeader::Count: return {};\n  }\n  return {};\n}\n\n"
      << "const StandardHeader* findStandardHeader(std::string_view name) {\n"
      << "  for (const auto& header : kHeaders) if (headerName(header) == name) return &header;\n"
      << "  return nullptr;\n}\n\n"
      << "std::span<const StandardHeader> allStandardHeaders() { return kHeaders; }\n\n"
      << "std::span<const SourceModuleSpec> sourceModuleSpecs() { return kSourceModules; }\n\n"
      << "const SourceModuleSpec* findSourceModule(std::string_view id) {\n"
      << "  for (const auto& module : kSourceModules) if (module.id == id) return &module;\n"
      << "  return nullptr;\n}\n\n"
      << "std::string_view toString(BuiltinProvider provider) {\n  switch (provider) {\n"
      << "  case BuiltinProvider::None: return \"None\";\n  case BuiltinProvider::Semantic: return \"Semantic\";\n"
      << "  case BuiltinProvider::Intrinsic: return \"Intrinsic\";\n  case BuiltinProvider::CoreHs: return \"CoreHs\";\n"
      << "  case BuiltinProvider::RuntimeBridge: return \"RuntimeBridge\";\n  case BuiltinProvider::LibcBridge: return \"LibcBridge\";\n"
      << "  case BuiltinProvider::FormatProtocol: return \"FormatProtocol\";\n  }\n  return {};\n}\n\n"
      << "std::string_view toString(BuiltinProviderSelection selection) {\n  switch (selection) {\n"
      << "  case BuiltinProviderSelection::Optimized: return \"optimized\";\n"
      << "  case BuiltinProviderSelection::Reference: return \"reference\";\n"
      << "  }\n  return {};\n}\n\n"
      << "std::string_view toString(BuiltinReturnMode mode) {\n  switch (mode) {\n"
      << "  case BuiltinReturnMode::Void: return \"Void\";\n  case BuiltinReturnMode::Fixed: return \"Fixed\";\n"
      << "  case BuiltinReturnMode::ArgumentLength: return \"ArgumentLength\";\n"
      << "  case BuiltinReturnMode::DynamicLength: return \"DynamicLength\";\n"
      << "  case BuiltinReturnMode::LeftContext: return \"LeftContext\";\n  }\n  return {};\n}\n\n"
      << "std::string_view toString(BuiltinParameterMode mode) {\n  switch (mode) {\n";
  static constexpr std::string_view modes[] = {
      "View", "LView", "MemView", "MemLView", "CStrView", "BytesView", "Addr",
      "Handle", "Bool", "I8", "I16", "I32", "I64", "U8", "U16", "U32",
      "U64", "F16", "F32", "F64", "F128", "Integer", "Floating", "SameType",
      "None", "LeftContext", "Varargs"};
  for (const auto mode : modes) {
    out << "  case BuiltinParameterMode::" << mode << ": return \"" << mode
        << "\";\n";
  }
  out << "  }\n  return {};\n}\n\n"
      << "bool isRemovedLegacyName(std::string_view name) {\n"
      << "  return name == \"core.hsh\" || name == \"to_float\" || name == \"to_int\" || name == \"reinterpret\";\n}\n\n"
      << "std::string_view replacementForRemovedLegacyName(std::string_view name) {\n"
      << "  if (name == \"core.hsh\") return \"the grouped standard headers\";\n"
      << "  if (name == \"to_float\") return \"to_f16(), to_f32(), to_f64(), or to_f128()\";\n"
      << "  if (name == \"to_int\") return \"to_i8(), to_i16(), to_i32(), to_i64(), to_u8(), to_u16(), to_u32(), or to_u64()\";\n"
      << "  if (name == \"reinterpret\") return \"as, ?, resize_bytes(), or byte_swap()\";\n"
      << "  return {};\n}\n\n} // namespace hitsimple::stdlib\n";
  return true;
}

bool writeHeaders(const Manifest &manifest, const std::filesystem::path &directory,
                  std::string &error) {
  std::filesystem::create_directories(directory);
  for (const auto &header : manifest.headers) {
    std::ofstream out(directory / header.file);
    if (!out) {
      error = "cannot write generated header '" + (directory / header.file).string() + "'";
      return false;
    }
    out << "// Generated from StandardLibraryManifest.json. Do not edit.\n"
        << "$ifndef " << header.guard << "\n$define " << header.guard << " 1\n\n"
        << "// " << header.summary << "\n";
    for (const auto &builtin : manifest.builtins) {
      if (builtin.visibility == "Public" && builtin.header == header.id) {
        out << builtin.headerDeclaration << "\n";
      }
    }
    out << "\n$endif\n";
  }
  return true;
}

void printUsage() {
  llvm::errs() << "usage: hsc_stdlib_manifest_tool --manifest <path> --cpp-out <path> --headers-out-dir <path>\n";
}

} // namespace

int main(int argc, char **argv) {
  std::filesystem::path manifestPath;
  std::filesystem::path cppOutput;
  std::filesystem::path headersOutput;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if ((argument == "--manifest" || argument == "--cpp-out" ||
         argument == "--headers-out-dir") &&
        index + 1 < argc) {
      const std::filesystem::path value(argv[++index]);
      if (argument == "--manifest") manifestPath = value;
      if (argument == "--cpp-out") cppOutput = value;
      if (argument == "--headers-out-dir") headersOutput = value;
      continue;
    }
    printUsage();
    return 1;
  }
  if (manifestPath.empty() || cppOutput.empty() || headersOutput.empty()) {
    printUsage();
    return 1;
  }
  Manifest manifest;
  std::string error;
  if (!parseManifest(manifestPath, manifest, error) ||
      !validateManifest(manifest, manifestPath.parent_path().parent_path(), error) ||
      !writeGeneratedCpp(manifest, cppOutput, error) ||
      !writeHeaders(manifest, headersOutput, error)) {
    llvm::errs() << "standard library manifest error: " << error << '\n';
    return 1;
  }
  return 0;
}
