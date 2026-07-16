#pragma once

#include "hitsimple/flowir/FlowIR.h"

#include <cstdint>
#include <vector>

namespace hitsimple::flowir {

std::vector<std::uint8_t> serialize(const Module &module);

} // namespace hitsimple::flowir
