#pragma once

#include "hitsimple/flowir/Analysis.h"

#include <vector>

namespace hitsimple::flowir::detail {

std::vector<EffectSummary> summarizeEffects(const Module& module);

} // namespace hitsimple::flowir::detail
