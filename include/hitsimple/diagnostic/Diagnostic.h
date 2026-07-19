#pragma once

#include "hitsimple/diagnostic/SourceLocation.h"

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace hitsimple::diagnostic {

enum class Severity {
  Note,
  Warning,
  Error,
};

enum class Stage {
  Cli,
  Preprocessor,
  Lexer,
  Parser,
  Sema,
  Hir,
  Codegen,
  Runtime,
};

std::string_view severityName(Severity severity);
std::string_view stageName(Stage stage);

struct DiagnosticLabel {
  SourceRange range;
  std::string message;
};

struct Diagnostic {
  Severity severity = Severity::Error;
  Stage stage = Stage::Sema;
  std::optional<SourceRange> range;
  std::string message;
  std::vector<DiagnosticLabel> labels;

  static Diagnostic error(Stage stage, std::string message);

  std::string format() const;
  std::string formatJson() const;
  const std::string &str() const;
  std::size_t find(std::string_view needle) const;
  bool empty() const;
};

// Returns the optional source line and marker that follow a human-readable
// diagnostic. The stable Diagnostic::format() primary line is never changed.
std::string renderSourceExcerpt(const Diagnostic &diagnostic);

std::ostream &operator<<(std::ostream &out, const Diagnostic &diagnostic);

} // namespace hitsimple::diagnostic
