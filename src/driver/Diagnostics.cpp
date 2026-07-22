#include "driver/Diagnostics.h"

#include "hitsimple/support/Path.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <utility>

namespace hitsimple::driver {

namespace {

DiagnosticOutputFormat diagnosticOutputFormat = DiagnosticOutputFormat::Human;

} // namespace

DiagnosticOutputFormatScope::DiagnosticOutputFormatScope(
    DiagnosticOutputFormat format)
    : previous_(diagnosticOutputFormat) {
  diagnosticOutputFormat = format;
}

DiagnosticOutputFormatScope::~DiagnosticOutputFormatScope() {
  diagnosticOutputFormat = previous_;
}

void printHumanDiagnostic(const hitsimple::diagnostic::Diagnostic& diagnostic) {
  std::cerr << "hsc: " << diagnostic.format() << '\n';
  const auto excerpt = hitsimple::diagnostic::renderSourceExcerpt(diagnostic);
  if (!excerpt.empty()) {
    std::cerr << excerpt << '\n';
  }
}

void printDiagnostic(const hitsimple::diagnostic::Diagnostic& diagnostic) {
  if (diagnosticOutputFormat == DiagnosticOutputFormat::Json) {
    std::cerr << diagnostic.formatJson() << '\n';
    return;
  }

  printHumanDiagnostic(diagnostic);
  for (const auto& label : diagnostic.labels) {
    auto note = hitsimple::diagnostic::Diagnostic::error(
        diagnostic.stage, label.message);
    note.severity = hitsimple::diagnostic::Severity::Note;
    note.range = label.range;
    printHumanDiagnostic(note);
  }
}

bool printDiagnostics(const std::vector<hitsimple::diagnostic::Diagnostic>& diagnostics) {
  bool hasError = false;
  for (const auto& diagnostic : diagnostics) {
    printDiagnostic(diagnostic);
    hasError = hasError ||
               diagnostic.severity == hitsimple::diagnostic::Severity::Error;
  }
  return hasError;
}

hitsimple::diagnostic::SourceRange
fileStartRange(std::string_view inputPath) {
  const hitsimple::diagnostic::SourceLocation location{std::string(inputPath),
                                                        1, 1};
  return {location, location};
}

hitsimple::diagnostic::Diagnostic
fileLevelDiagnostic(hitsimple::diagnostic::Stage stage, std::string message,
                    std::string_view inputPath) {
  auto diagnostic = hitsimple::diagnostic::Diagnostic::error(
      stage, std::move(message));
  if (!inputPath.empty()) {
    diagnostic.range = fileStartRange(inputPath);
  }
  return diagnostic;
}

std::string escapeLexeme(std::string_view lexeme) {
  std::string escaped;
  for (const char ch : lexeme) {
    switch (ch) {
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    case '\\':
      escaped += "\\\\";
      break;
    case '`':
      escaped += "\\`";
      break;
    default:
      escaped += ch;
      break;
    }
  }
  return escaped;
}

bool writeFile(const std::string& path, const std::string& content) {
  std::ofstream output(hitsimple::support::pathFromUtf8(path),
                       std::ios::binary);
  if (!output) {
    return false;
  }

  output << content;
  return static_cast<bool>(output);
}

bool validateOutputParent(const std::string& path) {
  const auto parent = hitsimple::support::pathFromUtf8(path).parent_path();
  if (parent.empty()) {
    return true;
  }
  if (!std::filesystem::exists(parent)) {
    std::cerr << "hsc: output directory does not exist '"
              << hitsimple::support::pathToUtf8(parent) << "'\n";
    return false;
  }
  if (!std::filesystem::is_directory(parent)) {
    std::cerr << "hsc: output parent is not a directory '"
              << hitsimple::support::pathToUtf8(parent) << "'\n";
    return false;
  }
  return true;
}

bool outputPathsConflict(const std::string& left, const std::string& right) {
  const auto normalized = [](const std::string& path) {
    return std::filesystem::absolute(hitsimple::support::pathFromUtf8(path))
        .lexically_normal();
  };
  return normalized(left) == normalized(right);
}

std::optional<std::string> readFile(const std::string& path) {
  std::ifstream input(hitsimple::support::pathFromUtf8(path),
                      std::ios::binary);
  if (!input) {
    return std::nullopt;
  }

  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

bool prepareOptimizationRemarksOutput(const std::filesystem::path& path) {
  const auto pathText = hitsimple::support::pathToUtf8(path);
  if (!validateOutputParent(pathText)) {
    return false;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    std::cerr << "hsc: cannot write optimization remarks '" << pathText
              << "'\n";
    return false;
  }
  return true;
}

bool appendOptimizationRemarks(const std::filesystem::path& path,
                               const std::vector<std::string>& remarks) {
  if (remarks.empty()) {
    return true;
  }
  std::ofstream output(path, std::ios::binary | std::ios::app);
  if (!output) {
    std::cerr << "hsc: cannot append optimization remarks '"
              << hitsimple::support::pathToUtf8(path) << "'\n";
    return false;
  }
  for (const auto& remark : remarks) {
    output << remark << '\n';
  }
  return static_cast<bool>(output);
}

} // namespace hitsimple::driver
