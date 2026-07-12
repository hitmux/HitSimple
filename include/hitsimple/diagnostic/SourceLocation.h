#pragma once

#include <cstddef>
#include <string>

namespace hitsimple::diagnostic {

struct SourceLocation {
  std::string file;
  std::size_t line = 1;
  std::size_t column = 1;
};

struct SourceRange {
  SourceLocation begin;
  SourceLocation end;
};

bool hasFile(const SourceLocation& location);
bool hasRange(const SourceRange& range);

} // namespace hitsimple::diagnostic
