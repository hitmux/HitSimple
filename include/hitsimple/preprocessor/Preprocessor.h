#pragma once

#include "hitsimple/diagnostic/Diagnostic.h"
#include "hitsimple/stdlib/StandardLibrary.h"

#include <string>
#include <vector>

namespace hitsimple::preprocessor {

struct PreprocessResult {
  std::string source;
  std::vector<diagnostic::SourceLocation> lineOrigins;
  std::vector<stdlib::StandardHeader> standardHeaders;
  std::vector<diagnostic::Diagnostic> diagnostics;
};

PreprocessResult preprocessFile(const std::string &path);
PreprocessResult preprocessSource(const std::string &source,
                                  const std::string &fileName);
std::vector<diagnostic::Diagnostic> validateSource(const std::string &source,
                                                   const std::string &fileName);

} // namespace hitsimple::preprocessor
