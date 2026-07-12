#include "hitsimple/diagnostic/Diagnostic.h"

#include <sstream>
#include <utility>

namespace hitsimple::diagnostic {

bool hasFile(const SourceLocation& location) { return !location.file.empty(); }

bool hasRange(const SourceRange& range) {
  return hasFile(range.begin) || hasFile(range.end);
}

std::string_view severityName(Severity severity) {
  switch (severity) {
  case Severity::Note:
    return "note";
  case Severity::Warning:
    return "warning";
  case Severity::Error:
    return "error";
  }
  return "error";
}

std::string_view stageName(Stage stage) {
  switch (stage) {
  case Stage::Cli:
    return "cli";
  case Stage::Lexer:
    return "lexer";
  case Stage::Parser:
    return "parser";
  case Stage::Sema:
    return "sema";
  case Stage::Hir:
    return "hir";
  case Stage::Codegen:
    return "codegen";
  case Stage::Runtime:
    return "runtime";
  }
  return "sema";
}

Diagnostic Diagnostic::error(Stage stage, std::string message) {
  Diagnostic diagnostic;
  diagnostic.severity = Severity::Error;
  diagnostic.stage = stage;
  diagnostic.message = std::move(message);
  return diagnostic;
}

std::string Diagnostic::format() const {
  std::ostringstream out;
  if (range && hasFile(range->begin)) {
    out << range->begin.file << ':' << range->begin.line << ':'
        << range->begin.column << ": ";
  }
  out << stageName(stage) << ": " << severityName(severity) << ": "
      << message;
  return out.str();
}

const std::string& Diagnostic::str() const { return message; }

std::size_t Diagnostic::find(std::string_view needle) const {
  return message.find(needle);
}

bool Diagnostic::empty() const { return message.empty(); }

std::ostream& operator<<(std::ostream& out, const Diagnostic& diagnostic) {
  out << diagnostic.format();
  return out;
}

} // namespace hitsimple::diagnostic
