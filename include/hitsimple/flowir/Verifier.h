#pragma once

#include "hitsimple/diagnostic/Diagnostic.h"
#include "hitsimple/flowir/FlowIR.h"

#include <vector>

namespace hitsimple::flowir {

std::vector<diagnostic::Diagnostic> verify(const Module &module);

} // namespace hitsimple::flowir
