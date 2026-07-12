#pragma once

#include "hitsimple/diagnostic/SourceLocation.h"

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace hitsimple::diagnostic {

enum class Severity {
  Note,
  Warning,
  Error,
};

enum class Stage {
  Cli,
  Lexer,
  Parser,
  Sema,
  Hir,
  Codegen,
  Runtime,
};

std::string_view severityName(Severity severity);
std::string_view stageName(Stage stage);

struct Diagnostic {
  Severity severity = Severity::Error;
  Stage stage = Stage::Sema;
  std::optional<SourceRange> range;
  std::string message;

  static Diagnostic error(Stage stage, std::string message);

  std::string format() const;
  const std::string &str() const;
  std::size_t find(std::string_view needle) const;
  bool empty() const;
};

std::ostream &operator<<(std::ostream &out, const Diagnostic &diagnostic);

} // namespace hitsimple::diagnostic
