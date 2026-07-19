#pragma once

#include "hitsimple/hir/HIR.h"
#include "hitsimple/stdlib/StandardLibrary.h"

#include <string>
#include <vector>

namespace hitsimple::stdlib {

// Resolves the active provider recorded in HIR and returns the required source
// modules in dependency order. CoreHs calls are rewritten to their reserved
// implementation symbols and receive matching extern declarations.
std::vector<std::string> selectStandardLibraryProviders(
    hir::TranslationUnit& unit, BuiltinProviderSelection selection);

} // namespace hitsimple::stdlib
