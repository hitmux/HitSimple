#include "hitsimple/diagnostic/Diagnostic.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <utility>

namespace hitsimple::diagnostic {

bool hasFile(const SourceLocation& location) { return !location.file.empty(); }

bool hasRange(const SourceRange& range) {
  return hasFile(range.begin) || hasFile(range.end);
}

void appendJsonString(std::ostream& out, std::string_view value) {
  static constexpr char hex[] = "0123456789abcdef";
  out << '"';
  for (const unsigned char character : value) {
    switch (character) {
    case '\"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\b':
      out << "\\b";
      break;
    case '\f':
      out << "\\f";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      if (character < 0x20U) {
        out << "\\u00" << hex[(character >> 4U) & 0x0FU]
            << hex[character & 0x0FU];
      } else {
        out << static_cast<char>(character);
      }
      break;
    }
  }
  out << '"';
}

void appendJsonLocation(std::ostream& out, const SourceLocation& location) {
  out << "{\"file\":";
  appendJsonString(out, location.file);
  out << ",\"line\":" << location.line << ",\"column\":"
      << location.column << '}';
}

void appendJsonRange(std::ostream& out, const SourceRange& range) {
  out << "{\"begin\":";
  appendJsonLocation(out, range.begin);
  out << ",\"end\":";
  appendJsonLocation(out, range.end);
  out << '}';
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
  case Stage::Preprocessor:
    return "preprocessor";
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

std::string Diagnostic::formatJson() const {
  std::ostringstream out;
  out << "{\"severity\":";
  appendJsonString(out, severityName(severity));
  out << ",\"stage\":";
  appendJsonString(out, stageName(stage));
  out << ",\"message\":";
  appendJsonString(out, message);
  out << ",\"primary\":";
  if (range) {
    appendJsonRange(out, *range);
  } else {
    out << "null";
  }
  out << ",\"related\":[";
  for (std::size_t index = 0; index < labels.size(); ++index) {
    if (index != 0) {
      out << ',';
    }
    out << "{\"message\":";
    appendJsonString(out, labels[index].message);
    out << ",\"range\":";
    appendJsonRange(out, labels[index].range);
    out << '}';
  }
  out << "]}";
  return out.str();
}

const std::string& Diagnostic::str() const { return message; }

std::size_t Diagnostic::find(std::string_view needle) const {
  return message.find(needle);
}

bool Diagnostic::empty() const { return message.empty(); }

std::string renderSourceExcerpt(const Diagnostic &diagnostic) {
  if (!diagnostic.range || !hasFile(diagnostic.range->begin)) {
    return {};
  }

  const auto &range = *diagnostic.range;
  if (range.begin.line == 0 || range.begin.column == 0) {
    return {};
  }

  std::ifstream input(range.begin.file, std::ios::binary);
  if (!input) {
    return {};
  }

  std::string sourceLine;
  for (std::size_t line = 1; line <= range.begin.line; ++line) {
    if (!std::getline(input, sourceLine)) {
      return {};
    }
  }
  if (!sourceLine.empty() && sourceLine.back() == '\r') {
    sourceLine.pop_back();
  }

  const auto sourceColumn = range.begin.column - 1U;
  if (sourceColumn > sourceLine.size()) {
    return {};
  }

  std::string displayedLine;
  std::size_t markerColumn = 0;
  for (std::size_t index = 0; index < sourceLine.size(); ++index) {
    const auto character = sourceLine[index];
    if (character == '\t') {
      const auto width = 4U - displayedLine.size() % 4U;
      displayedLine.append(width, ' ');
      if (index < sourceColumn) {
        markerColumn += width;
      }
      continue;
    }
    displayedLine.push_back(character);
    if (index < sourceColumn) {
      ++markerColumn;
    }
  }

  std::size_t underlineLength = 1;
  if (range.begin.file == range.end.file &&
      range.begin.line == range.end.line &&
      range.end.column > range.begin.column) {
    underlineLength = range.end.column - range.begin.column;
  }
  underlineLength = std::min(
      underlineLength, std::max<std::size_t>(1, sourceLine.size() - sourceColumn));

  return "  " + displayedLine + "\n  " +
         std::string(markerColumn, ' ') + '^' +
         std::string(underlineLength - 1U, '~');
}

std::ostream& operator<<(std::ostream& out, const Diagnostic& diagnostic) {
  out << diagnostic.format();
  return out;
}

} // namespace hitsimple::diagnostic
