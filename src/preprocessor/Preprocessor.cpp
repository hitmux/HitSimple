#include "hitsimple/preprocessor/Preprocessor.h"

#include "hitsimple/support/Process.h"
#include "hitsimple/support/Path.h"
#include "hitsimple/support/ResourcePaths.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>

#ifndef HITSIMPLE_VERSION
#define HITSIMPLE_VERSION "0.1.0"
#endif

namespace hitsimple::preprocessor {
namespace {

struct TempDirectory {
  std::filesystem::path path;

  TempDirectory() {
    const auto seed =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() /
           ("hitsimple-mcpp-" + std::to_string(support::currentProcessId()) +
            "-" + std::to_string(seed));
    std::filesystem::create_directories(path);
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

struct PreparedInput {
  std::filesystem::path inputPath;
  std::vector<std::filesystem::path> includePaths;
  std::unordered_map<std::string, std::string> fileMap;
  std::unordered_map<std::string, std::set<std::size_t>> directiveLines;
  std::unordered_map<std::string, std::unordered_map<std::size_t, std::string>>
      directiveKeywords;
  PreprocessResult result;
};

std::string trim(std::string_view text) {
  std::size_t begin = 0;
  while (begin < text.size() &&
         std::isspace(static_cast<unsigned char>(text[begin]))) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

std::optional<std::string> readFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

bool writeFile(const std::filesystem::path &path, const std::string &content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    return false;
  }
  output << content;
  return static_cast<bool>(output);
}

void addDiagnostic(PreprocessResult &result,
                   diagnostic::Severity severity,
                   std::string message,
                   const std::string &fileName = {},
                   std::size_t line = 1,
                   std::size_t column = 1) {
  diagnostic::Diagnostic diag;
  diag.severity = severity;
  diag.stage = diagnostic::Stage::Lexer;
  diag.message = std::move(message);
  if (!fileName.empty()) {
    diag.range = diagnostic::SourceRange{
        diagnostic::SourceLocation{fileName, line, column},
        diagnostic::SourceLocation{fileName, line, column}};
  }
  result.diagnostics.push_back(std::move(diag));
}

std::filesystem::path mirrorPathFor(const std::filesystem::path &root,
                                    const std::filesystem::path &original) {
  if (original.is_absolute()) {
    auto relative = original.lexically_normal().relative_path();
    return root / "files" / relative;
  }
  return root / "files" / original.lexically_normal();
}

std::filesystem::path sourcePathFor(const std::filesystem::path &root,
                                    const std::string &fileName) {
  const auto path = support::pathFromUtf8(fileName);
  if (path.is_absolute()) {
    return mirrorPathFor(root, path);
  }
  return root / "source" / path.lexically_normal();
}

void addFileMapping(PreparedInput &prepared,
                    const std::filesystem::path &generated,
                    const std::string &original,
                    const std::string &spelling = {}) {
  prepared.fileMap[support::pathToUtf8(generated.lexically_normal())] = original;
  prepared.fileMap[support::pathToUtf8(
      std::filesystem::absolute(generated).lexically_normal())] = original;
  if (!spelling.empty()) {
    prepared.fileMap[spelling] = original;
  }
}

std::string mappedFileName(const PreparedInput &prepared, std::string fileName) {
  const auto exact = prepared.fileMap.find(fileName);
  if (exact != prepared.fileMap.end()) {
    return exact->second;
  }
  const auto normalized =
      support::pathToUtf8(
          support::pathFromUtf8(fileName).lexically_normal());
  const auto found = prepared.fileMap.find(normalized);
  if (found != prepared.fileMap.end()) {
    return found->second;
  }
  return fileName;
}

struct Directive {
  std::string keyword;
  std::string rest;
};

std::optional<Directive> parseDollarDirective(std::string_view line) {
  std::size_t index = 0;
  while (index < line.size() &&
         std::isspace(static_cast<unsigned char>(line[index]))) {
    ++index;
  }
  if (index >= line.size() || line[index] != '$') {
    return std::nullopt;
  }
  ++index;
  while (index < line.size() &&
         std::isspace(static_cast<unsigned char>(line[index]))) {
    ++index;
  }

  const std::size_t keywordBegin = index;
  while (index < line.size() &&
         (std::isalpha(static_cast<unsigned char>(line[index])) ||
          line[index] == '_')) {
    ++index;
  }

  Directive directive;
  directive.keyword = std::string(line.substr(keywordBegin, index - keywordBegin));
  directive.rest = trim(line.substr(index));
  return directive;
}

bool isDigit(char ch) { return ch >= '0' && ch <= '9'; }

bool isAlpha(char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

bool isIdentifierStart(char ch) { return isAlpha(ch) || ch == '_'; }

bool isIdentifierContinue(char ch) {
  return isIdentifierStart(ch) || isDigit(ch);
}

bool isKeywordOrReserved(std::string_view text) {
  static constexpr std::array<std::string_view, 36> words{
      "as",       "break",    "case",   "catch",   "const",    "continue",
      "default",  "do",       "else",   "enum",    "extern",   "false",
      "for",      "func",     "goto",   "if",      "impl",     "mut",
      "new",      "none",     "op",     "return",  "self",     "set",
      "sizeof",   "static",   "struct", "switch",  "template", "throw",
      "true",     "try",      "typedef","union",   "volatile", "while"};
  return std::find(words.begin(), words.end(), text) != words.end();
}

bool isReservedTemplateCounter(std::string_view text) {
  if (text.size() < 2 || text.front() != 't') {
    return false;
  }
  for (std::size_t index = 1; index < text.size(); ++index) {
    if (!isDigit(text[index])) {
      return false;
    }
  }
  return true;
}

bool isLegalMacroName(std::string_view text) {
  if (text.empty() || text == "_" || isKeywordOrReserved(text) ||
      isReservedTemplateCounter(text) || !isIdentifierStart(text.front())) {
    return false;
  }
  for (const char ch : text) {
    if (!isIdentifierContinue(ch)) {
      return false;
    }
  }
  return true;
}

std::string firstIdentifierLike(std::string_view text) {
  const std::string trimmed = trim(text);
  if (trimmed.empty() || !isIdentifierStart(trimmed.front())) {
    return {};
  }
  std::size_t end = 1;
  while (end < trimmed.size() && isIdentifierContinue(trimmed[end])) {
    ++end;
  }
  return trimmed.substr(0, end);
}

std::size_t utf8SequenceLength(unsigned char lead) {
  if (lead <= 0x7F) {
    return 1;
  }
  if (lead >= 0xC2 && lead <= 0xDF) {
    return 2;
  }
  if (lead >= 0xE0 && lead <= 0xEF) {
    return 3;
  }
  if (lead >= 0xF0 && lead <= 0xF4) {
    return 4;
  }
  return 0;
}

bool isContinuationByte(unsigned char byte) {
  return byte >= 0x80 && byte <= 0xBF;
}

bool isValidUtf8Sequence(std::string_view source,
                         std::size_t index,
                         std::size_t length) {
  if (index + length > source.size()) {
    return false;
  }
  const auto b0 = static_cast<unsigned char>(source[index]);
  if (length == 1) {
    return b0 <= 0x7F;
  }
  for (std::size_t offset = 1; offset < length; ++offset) {
    if (!isContinuationByte(static_cast<unsigned char>(source[index + offset]))) {
      return false;
    }
  }
  if (length == 3) {
    const auto b1 = static_cast<unsigned char>(source[index + 1]);
    if (b0 == 0xE0 && b1 < 0xA0) {
      return false;
    }
    if (b0 == 0xED && b1 >= 0xA0) {
      return false;
    }
  }
  if (length == 4) {
    const auto b1 = static_cast<unsigned char>(source[index + 1]);
    if (b0 == 0xF0 && b1 < 0x90) {
      return false;
    }
    if (b0 == 0xF4 && b1 > 0x8F) {
      return false;
    }
  }
  return true;
}

void validateUtf8(PreprocessResult &result,
                  std::string_view source,
                  const std::string &fileName) {
  std::size_t line = 1;
  std::size_t column = 1;
  for (std::size_t index = 0; index < source.size();) {
    const auto byte = static_cast<unsigned char>(source[index]);
    if (byte == '\n') {
      ++line;
      column = 1;
      ++index;
      continue;
    }
    const auto length = utf8SequenceLength(byte);
    if (length == 0 || !isValidUtf8Sequence(source, index, length)) {
      addDiagnostic(result, diagnostic::Severity::Error,
                    "source is not valid UTF-8", fileName, line, column);
      return;
    }
    index += length;
    ++column;
  }
}

bool shouldValidateMacroName(std::string_view keyword) {
  return keyword == "define" || keyword == "undef" || keyword == "ifdef" ||
         keyword == "ifndef";
}

std::optional<std::string> macroNameForDirective(const Directive &directive) {
  if (!shouldValidateMacroName(directive.keyword)) {
    return std::nullopt;
  }
  return firstIdentifierLike(directive.rest);
}

void validateDirectiveMacroName(PreprocessResult &result,
                                const Directive &directive,
                                const std::string &fileName,
                                std::size_t lineNumber) {
  const auto macroName = macroNameForDirective(directive);
  if (!macroName) {
    return;
  }
  if (!isLegalMacroName(*macroName)) {
    addDiagnostic(result, diagnostic::Severity::Error,
                  "$" + directive.keyword + " requires a valid macro name",
                  fileName, lineNumber, 1);
  }
}

void validateBareHashInLine(PreprocessResult &result,
                            std::string_view line,
                            const std::string &fileName,
                            std::size_t lineNumber,
                            bool &inBlockComment) {
  const auto directive = parseDollarDirective(line);
  if (directive) {
    return;
  }

  char quote = '\0';
  for (std::size_t index = 0; index < line.size(); ++index) {
    const char ch = line[index];
    const char next = index + 1 < line.size() ? line[index + 1] : '\0';
    if (inBlockComment) {
      if (ch == '*' && next == '/') {
        inBlockComment = false;
        ++index;
      }
      continue;
    }
    if (quote != '\0') {
      if (ch == '\\' && next != '\0') {
        ++index;
        continue;
      }
      if (ch == quote) {
        quote = '\0';
      }
      continue;
    }
    if (ch == '/' && next == '/') {
      return;
    }
    if (ch == '/' && next == '*') {
      inBlockComment = true;
      ++index;
      continue;
    }
    if (ch == '"' || ch == '\'') {
      quote = ch;
      continue;
    }
    if (ch == '#') {
      addDiagnostic(result, diagnostic::Severity::Error,
                    next == '#' ? "bare ## is not valid HitSimple source"
                                : "bare # is not valid HitSimple source",
                    fileName, lineNumber, index + 1);
      return;
    }
  }
}

void validateSourceInto(PreprocessResult &result,
                        const std::string &source,
                        const std::string &fileName) {
  validateUtf8(result, source, fileName);
  std::istringstream input(source);
  std::string line;
  std::size_t lineNumber = 0;
  bool inBlockComment = false;
  while (std::getline(input, line)) {
    ++lineNumber;
    const auto directive = parseDollarDirective(line);
    if (directive) {
      validateDirectiveMacroName(result, *directive, fileName, lineNumber);
    }
    validateBareHashInLine(result, line, fileName, lineNumber, inBlockComment);
  }
}

bool hasError(const PreprocessResult &result) {
  return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                     [](const auto &diagnostic) {
                       return diagnostic.severity ==
                              diagnostic::Severity::Error;
                     });
}

std::string translateLine(std::string_view line) {
  const auto directive = parseDollarDirective(line);
  if (!directive) {
    std::string translated(line);
    std::size_t offset = 0;
    while ((offset = translated.find("\\u", offset)) != std::string::npos) {
      translated.replace(offset, 2, "__HS_BACKSLASH_U__");
      offset += 18;
    }
    return translated;
  }
  if (directive->keyword == "warning") {
    return "#pragma MCPP warning " + directive->rest;
  }
  return "#" + directive->keyword +
         (directive->rest.empty() ? std::string() : " " + directive->rest);
}

std::string escapeLineFileName(std::string_view fileName) {
  std::string escaped;
  for (const char ch : fileName) {
    if (ch == '\\' || ch == '"') {
      escaped.push_back('\\');
    }
    escaped.push_back(ch);
  }
  return escaped;
}

std::optional<std::string> parseIncludeTarget(const Directive &directive,
                                              bool &isSystem) {
  if (directive.keyword != "include") {
    return std::nullopt;
  }
  const auto rest = trim(directive.rest);
  if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"') {
    isSystem = false;
    return rest.substr(1, rest.size() - 2);
  }
  if (rest.size() >= 2 && rest.front() == '<' && rest.back() == '>') {
    isSystem = true;
    return rest.substr(1, rest.size() - 2);
  }
  return std::nullopt;
}

std::array<int, 3> versionParts() {
  std::array<int, 3> parts{0, 0, 0};
  std::istringstream input(HITSIMPLE_VERSION);
  char dot = '\0';
  input >> parts[0] >> dot >> parts[1] >> dot >> parts[2];
  return parts;
}

class Adapter {
public:
  PreprocessResult processFile(const std::string &path) {
    TempDirectory temp;
    PreparedInput prepared;
    const auto originalPath =
        std::filesystem::absolute(support::pathFromUtf8(path)).lexically_normal();
    const auto content = readFile(originalPath);
    if (!content) {
      addDiagnostic(prepared.result, diagnostic::Severity::Error,
                    "cannot read input file '" + path + "'");
      return prepared.result;
    }

    prepared.inputPath = prepareFile(prepared, temp.path, *content, originalPath,
                                     originalPath.parent_path());
    if (hasError(prepared.result)) {
      return std::move(prepared.result);
    }
    return runMcpp(prepared, temp.path);
  }

  PreprocessResult processSource(const std::string &source,
                                 const std::string &fileName) {
    TempDirectory temp;
    PreparedInput prepared;
    const auto generatedPath = sourcePathFor(temp.path, fileName);
    addFileMapping(prepared, generatedPath, fileName);
    prepared.inputPath =
        prepareSource(prepared, temp.path, source, fileName, generatedPath,
                      support::pathFromUtf8(fileName).parent_path());
    if (hasError(prepared.result)) {
      return std::move(prepared.result);
    }
    return runMcpp(prepared, temp.path);
  }

private:
  std::filesystem::path prepareFile(PreparedInput &prepared,
                                    const std::filesystem::path &tempRoot,
                                    const std::string &source,
                                    const std::filesystem::path &originalPath,
                                    const std::filesystem::path &baseDir) {
    const auto generatedPath = mirrorPathFor(tempRoot, originalPath);
    const auto originalName = support::pathToUtf8(originalPath);
    addFileMapping(prepared, generatedPath, originalName,
                   support::pathToUtf8(originalPath.filename()));
    return prepareSource(prepared, tempRoot, source, originalName,
                         generatedPath, baseDir);
  }

  std::filesystem::path prepareSource(PreparedInput &prepared,
                                      const std::filesystem::path &tempRoot,
                                      const std::string &source,
                                      const std::string &originalName,
                                      const std::filesystem::path &generatedPath,
                                      const std::filesystem::path &baseDir) {
    validateSourceInto(prepared.result, source, originalName);
    std::istringstream input(source);
    std::string line;
    std::string translated = "#line 1 \"" + escapeLineFileName(originalName) +
                             "\"\n";
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
      ++lineNumber;
      const auto directive = parseDollarDirective(line);
      if (directive) {
        prepared.directiveLines[originalName].insert(lineNumber);
        prepared.directiveKeywords[originalName][lineNumber] = directive->keyword;
        bool isSystem = false;
        const auto includeTarget = parseIncludeTarget(*directive, isSystem);
        if (includeTarget) {
          prepareInclude(prepared, tempRoot, *includeTarget, isSystem, baseDir);
        }
      }
      translated += translateLine(line);
      translated += '\n';
    }
    writeFile(generatedPath, translated);
    return generatedPath;
  }

  void prepareInclude(PreparedInput &prepared,
                      const std::filesystem::path &tempRoot,
                      const std::string &target,
                      bool isSystem,
                      const std::filesystem::path &baseDir) {
    std::filesystem::path originalPath;
    std::filesystem::path generatedPath;
    if (isSystem) {
      if (target == "core.hsh") {
        addDiagnostic(prepared.result, diagnostic::Severity::Error,
                      "standard header <core.hsh> was removed; include the "
                      "required grouped standard header instead");
        return;
      }
      const auto standardLibraryRoot = support::standardLibraryRoot();
      originalPath =
          (standardLibraryRoot / support::pathFromUtf8(target)).lexically_normal();
      generatedPath =
          (tempRoot / "include" / support::pathFromUtf8(target)).lexically_normal();
      if (std::filesystem::exists(standardLibraryRoot) &&
          std::find(prepared.includePaths.begin(), prepared.includePaths.end(),
                    tempRoot / "include") == prepared.includePaths.end()) {
        prepared.includePaths.push_back(tempRoot / "include");
      }

      if (const auto* header = stdlib::findStandardHeader(target)) {
        if (std::find(prepared.result.standardHeaders.begin(),
                      prepared.result.standardHeaders.end(), *header) ==
            prepared.result.standardHeaders.end()) {
          prepared.result.standardHeaders.push_back(*header);
        }
        addFileMapping(prepared, generatedPath,
                       support::pathToUtf8(originalPath), target);
        // Standard headers are semantic import declarations. Their source
        // remains the public interface, while this marker prevents ordinary
        // extern parsing from bypassing the registry's view contracts.
        writeFile(generatedPath,
                  "#define " + std::string(stdlib::headerGuard(*header)) +
                      " 1\n");
        return;
      }
    } else {
      originalPath =
          (baseDir / support::pathFromUtf8(target)).lexically_normal();
      generatedPath = mirrorPathFor(tempRoot, originalPath);
    }

    if (prepared.fileMap.find(support::pathToUtf8(generatedPath)) !=
        prepared.fileMap.end()) {
      return;
    }
    const auto originalName = support::pathToUtf8(originalPath);
    addFileMapping(prepared, generatedPath, originalName, target);

    const auto content = readFile(originalPath);
    if (!content) {
      return;
    }
    prepareSource(prepared, tempRoot, *content, originalName,
                  generatedPath, originalPath.parent_path());
  }

  PreprocessResult runMcpp(PreparedInput &prepared,
                           const std::filesystem::path &tempRoot) {
    const auto outPath = tempRoot / "mcpp.out";
    const auto errPath = tempRoot / "mcpp.err";
    const auto parts = versionParts();

    std::vector<std::string> arguments{
        "-V199901L",
        "-W0",
        "-D__HITSIMPLE__=1",
        "-D__HS_VERSION__=\\\"" HITSIMPLE_VERSION "\\\"",
        "-D__HS_VERSION_MAJOR__=" + std::to_string(parts[0]),
        "-D__HS_VERSION_MINOR__=" + std::to_string(parts[1]),
        "-D__HS_VERSION_PATCH__=" + std::to_string(parts[2]),
    };
    for (const auto &includePath : prepared.includePaths) {
      arguments.push_back("-I");
      arguments.push_back(support::pathToUtf8(includePath));
    }
    arguments.push_back(support::pathToUtf8(prepared.inputPath));
    arguments.push_back("-o");
    arguments.push_back(support::pathToUtf8(outPath));

    const auto process = support::runProcess(
        support::preprocessorExecutablePath(), arguments, std::nullopt,
        errPath);
    const auto output = readFile(outPath).value_or("");
    const auto errors = readFile(errPath).value_or("");
    parseMcppOutput(prepared, output);
    parseMcppDiagnostics(prepared, errors);
    if (!process.launched && prepared.result.diagnostics.empty()) {
      addDiagnostic(prepared.result, diagnostic::Severity::Error,
                    "cannot start mcpp preprocessor: " + process.error);
    } else if (process.exitCode != 0 && prepared.result.diagnostics.empty()) {
      addDiagnostic(prepared.result, diagnostic::Severity::Error,
                    "mcpp preprocessing failed");
    }
    return std::move(prepared.result);
  }

  void parseMcppOutput(PreparedInput &prepared, const std::string &output) {
    std::istringstream input(output);
    std::string line;
    std::string currentFile;
    std::size_t currentLine = 1;
    while (std::getline(input, line)) {
      std::size_t markerLine = 0;
      std::string markerFile;
      if (parseLineMarker(line, markerLine, markerFile)) {
        currentLine = markerLine;
        currentFile = mappedFileName(prepared, markerFile);
        continue;
      }
      if (line.empty() && isDirectiveLine(prepared, currentFile, currentLine)) {
        ++currentLine;
        continue;
      }
      std::size_t offset = 0;
      while ((offset = line.find("__HS_BACKSLASH_U__", offset)) !=
             std::string::npos) {
        line.replace(offset, 18, "\\u");
        offset += 2;
      }
      prepared.result.source += line;
      prepared.result.source += '\n';
      prepared.result.lineOrigins.push_back(
          diagnostic::SourceLocation{currentFile, currentLine, 1});
      ++currentLine;
    }
  }

  bool isDirectiveLine(const PreparedInput &prepared,
                       const std::string &fileName,
                       std::size_t line) const {
    const auto found = prepared.directiveLines.find(fileName);
    return found != prepared.directiveLines.end() &&
           found->second.find(line) != found->second.end();
  }

  static bool parseLineMarker(std::string_view line,
                              std::size_t &lineNumber,
                              std::string &fileName) {
    lineNumber = 0;
    std::string_view rest;
    if (line.rfind("#line ", 0) == 0) {
      rest = line.substr(6);
    } else if (line.rfind("# ", 0) == 0) {
      rest = line.substr(2);
    } else {
      return false;
    }

    std::size_t index = 0;
    while (index < rest.size() &&
           std::isdigit(static_cast<unsigned char>(rest[index]))) {
      lineNumber = lineNumber * 10 + static_cast<unsigned char>(rest[index] - '0');
      ++index;
    }
    while (index < rest.size() &&
           std::isspace(static_cast<unsigned char>(rest[index]))) {
      ++index;
    }
    if (index >= rest.size() || rest[index] != '"') {
      return true;
    }
    ++index;
    std::string parsed;
    while (index < rest.size() && rest[index] != '"') {
      parsed.push_back(rest[index++]);
    }
    fileName = std::move(parsed);
    return true;
  }

  void parseMcppDiagnostics(PreparedInput &prepared, const std::string &text) {
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
      if (line.empty() || line.front() == ' ' ||
          line.find("error in preprocessor") != std::string::npos ||
          line.find("errors in preprocessor") != std::string::npos) {
        continue;
      }
      std::size_t firstColon = std::string::npos;
      std::size_t secondColon = std::string::npos;
      std::size_t candidate = line.find(':');
      while (candidate != std::string::npos) {
        const auto next = line.find(':', candidate + 1);
        if (next == std::string::npos) {
          break;
        }
        const auto lineNumber = std::string_view(line).substr(
            candidate + 1, next - candidate - 1);
        if (!lineNumber.empty() &&
            std::all_of(lineNumber.begin(), lineNumber.end(), isDigit)) {
          firstColon = candidate;
          secondColon = next;
          break;
        }
        candidate = next;
      }
      if (firstColon == std::string::npos) {
        continue;
      }
      const auto thirdColon = line.find(':', secondColon + 1);
      if (thirdColon == std::string::npos) {
        continue;
      }

      const auto fileName = mappedFileName(prepared, line.substr(0, firstColon));
      const auto lineText = line.substr(firstColon + 1,
                                        secondColon - firstColon - 1);
      std::size_t sourceLine = 1;
      try {
        sourceLine = static_cast<std::size_t>(std::stoul(lineText));
      } catch (...) {
        sourceLine = 1;
      }
      auto severityText = trim(
          std::string_view(line).substr(secondColon + 1,
                                        thirdColon - secondColon - 1));
      auto message = trim(std::string_view(line).substr(thirdColon + 1));
      if (message.find("End of input within #if") != std::string::npos) {
        message = "unterminated preprocessor conditional";
      }
      const auto severity = severityText.find("warning") != std::string::npos
                                ? diagnostic::Severity::Warning
                                : diagnostic::Severity::Error;
      const auto fileDirectives = prepared.directiveKeywords.find(fileName);
      if (fileDirectives != prepared.directiveKeywords.end()) {
        const auto directive = fileDirectives->second.find(sourceLine);
        if (directive != fileDirectives->second.end() &&
            (directive->second == "else" || directive->second == "elif" ||
             directive->second == "endif") &&
            (message.find("No corresponding #if") != std::string::npos ||
             message.find("Not in a #if") != std::string::npos)) {
          message = "$" + directive->second + " without $if";
        }
      }
      addDiagnostic(prepared.result, severity, std::move(message), fileName,
                    sourceLine, 1);
    }
  }
};

} // namespace

PreprocessResult preprocessFile(const std::string &path) {
  return Adapter().processFile(path);
}

PreprocessResult preprocessSource(const std::string &source,
                                  const std::string &fileName) {
  return Adapter().processSource(source, fileName);
}

std::vector<diagnostic::Diagnostic> validateSource(const std::string &source,
                                                   const std::string &fileName) {
  PreprocessResult result;
  validateSourceInto(result, source, fileName);
  return std::move(result.diagnostics);
}

} // namespace hitsimple::preprocessor
