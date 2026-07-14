#pragma once

#include "hitsimple/codegen/LlvmCompatibility.h"

#include <string_view>

namespace hitsimple::codegen {

inline bool usesSoftwareF128Backend(std::string_view targetTriple) {
  const auto target = parseTargetTriple(targetTriple);
  return target.isOSWindows() || target.isOSDarwin();
}

} // namespace hitsimple::codegen
