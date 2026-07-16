#pragma once

#include "hitsimple/flowir/FlowIR.h"

#include <iosfwd>
#include <string>

namespace hitsimple::flowir {

void dump(const Module &module, std::ostream &out);
std::string dumpToString(const Module &module);

} // namespace hitsimple::flowir
