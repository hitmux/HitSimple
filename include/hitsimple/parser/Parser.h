#pragma once

#include "hitsimple/ast/AST.h"
#include "hitsimple/diagnostic/SourceLocation.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hitsimple::parser {

struct ParseResult {
  std::unique_ptr<ast::TranslationUnit> unit;
  std::string error;
};

ParseResult parseSource(std::string_view source, std::string fileName);
ParseResult parseSource(std::string_view source,
                        std::string fileName,
                        std::vector<diagnostic::SourceLocation> lineOrigins);

} // namespace hitsimple::parser
