#include "SemaAnalyzer.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace hitsimple::sema {
namespace {

struct EffectParameter final {
  std::string name;
  std::string templateName;
};

bool integerLike(std::string_view templateName) {
  return templateName.empty() || templateName == "bool" ||
         (!templateName.empty() &&
          (templateName.front() == 'i' || templateName.front() == 'u'));
}

bool isSimpleEffect(std::string_view name) {
  return name == "pure" || name == "readonly" || name == "allocates" ||
         name == "frees" || name == "throws" || name == "nothrow" ||
         name == "io" || name == "unknown";
}

std::uint32_t simpleFlag(std::string_view name) {
  if (name == "pure") return hir::EffectPure;
  if (name == "readonly") return hir::EffectReadonly;
  if (name == "allocates") return hir::EffectAllocates;
  if (name == "frees") return hir::EffectFrees;
  if (name == "throws") return hir::EffectThrows;
  if (name == "nothrow") return hir::EffectNothrow;
  if (name == "io") return hir::EffectIo;
  if (name == "unknown") return hir::EffectUnknown;
  return hir::EffectNone;
}

} // namespace

std::optional<hir::EffectContract> Analyzer::validateEffectContract(
    const std::optional<ast::EffectClause>& clause,
    const std::vector<ast::Param>& params, bool isExtern) {
  std::vector<EffectParameter> converted;
  converted.reserve(params.size());
  for (const auto& param : params) {
    converted.push_back(EffectParameter{param.name, param.templateName});
  }
  const auto validate = [this, isExtern, &clause, &converted]()
      -> std::optional<hir::EffectContract> {
    hir::EffectContract contract;
    if (!clause) {
      contract.flags = isExtern ? hir::EffectUnknown : hir::EffectNone;
      return contract;
    }
    contract.isExplicit = true;
    std::unordered_map<std::string, EffectParameter> parameters;
    for (const auto& param : converted) parameters.emplace(param.name, param);
    std::unordered_set<std::string> seen;
    std::unordered_set<std::string> ranges;
    std::unordered_set<std::string> aliases;
    bool invalid = false;
    const auto fail = [&](std::string message) {
      addDiagnostic(std::move(message));
      invalid = true;
    };
    for (const auto& item : clause->items) {
      if (item.name == "read" || item.name == "write") {
        if (item.object.empty() || item.range.empty()) {
          fail("effect '" + item.name + "' requires an object and range");
          continue;
        }
        const auto parameter = parameters.find(item.object);
        const auto* object = lookup(item.object);
        const bool isAddressParameter =
            parameter != parameters.end() && parameter->second.templateName == "addr";
        const bool isStaticObject = object != nullptr &&
                                    object->storage == hir::MemoryStorage::Global;
        if (!isAddressParameter && !isStaticObject) {
          fail("effect object '" + item.object +
               "' must name an addr parameter or static storage object");
          continue;
        }
        if (item.range == "all") {
          if (!isStaticObject) {
            fail("effect range 'all' requires a static storage object");
            continue;
          }
        } else if (const auto rangeParameter = parameters.find(item.range);
                   rangeParameter != parameters.end()) {
          if (!integerLike(rangeParameter->second.templateName)) {
            fail("effect range parameter '" + item.range +
                 "' must have an integer-like View");
            continue;
          }
        } else if (parseByteLength(item.range) == 0 && item.range != "0") {
          fail("effect range '" + item.range +
               "' must be a nonnegative integer literal or parameter");
          continue;
        }
        const auto key = item.name + "\x1f" + item.object;
        if (!ranges.insert(key).second) {
          fail("duplicate " + item.name + " effect range for '" + item.object + "'");
          continue;
        }
        contract.ranges.push_back(hir::EffectRange{
            item.object, item.range,
            item.name == "read" ? hir::EffectAccess::Read : hir::EffectAccess::Write});
        continue;
      }
      if (item.name == "noalias") {
        if (item.object.empty() || item.range.empty()) {
          fail("effect 'noalias' requires two parameters");
          continue;
        }
        const auto left = parameters.find(item.object);
        const auto right = parameters.find(item.range);
        if (left == parameters.end() || right == parameters.end() ||
            left->second.templateName != "addr" ||
            right->second.templateName != "addr" || item.object == item.range) {
          fail("noalias requires two distinct addr parameters");
          continue;
        }
        const auto ordered = std::minmax(item.object, item.range);
        const auto key = ordered.first + "\x1f" + ordered.second;
        if (!aliases.insert(key).second) {
          fail("duplicate noalias effect for '" + item.object + "' and '" + item.range + "'");
          continue;
        }
        contract.noAlias.emplace_back(item.object, item.range);
        continue;
      }
      if (!isSimpleEffect(item.name) || !item.object.empty() || !item.range.empty()) {
        fail("invalid effect item '" + item.name + "'");
        continue;
      }
      if (!seen.insert(item.name).second) {
        fail("duplicate effect item '" + item.name + "'");
        continue;
      }
      contract.flags |= simpleFlag(item.name);
    }
    const bool unknown = (contract.flags & hir::EffectUnknown) != 0U;
    if (unknown && (clause->items.size() != 1U || !contract.ranges.empty() ||
                    !contract.noAlias.empty())) {
      fail("effect 'unknown' must not appear with another effect item");
    }
    const bool throws = (contract.flags & hir::EffectThrows) != 0U;
    const bool nothrow = (contract.flags & hir::EffectNothrow) != 0U;
    const bool pure = (contract.flags & hir::EffectPure) != 0U;
    const bool readonly = (contract.flags & hir::EffectReadonly) != 0U;
    if (throws && nothrow) fail("effects 'throws' and 'nothrow' are mutually exclusive");
    if (!unknown && !pure && throws == nothrow) {
      fail("a non-pure effect contract must state exactly one of throws or nothrow");
    }
    if (pure && (throws || (contract.flags & (hir::EffectAllocates | hir::EffectFrees |
                                               hir::EffectIo)) != 0U ||
                 !contract.ranges.empty())) {
      fail("pure effect contract cannot permit external reads, writes, throws, allocation, or I/O");
    }
    const bool readonlyWrite = std::any_of(
        contract.ranges.begin(), contract.ranges.end(),
        [](const hir::EffectRange& range) {
          return range.access == hir::EffectAccess::Write;
        });
    if (readonly && ((contract.flags & (hir::EffectAllocates | hir::EffectFrees |
                                        hir::EffectIo)) != 0U || readonlyWrite)) {
      fail("readonly effect contract cannot permit writes, allocation, deallocation, or I/O");
    }
    for (const auto& [left, right] : contract.noAlias) {
      const auto hasRange = [&contract](std::string_view name) {
        return std::any_of(contract.ranges.begin(), contract.ranges.end(),
                           [name](const hir::EffectRange& range) {
                             return range.object == name;
                           });
      };
      if (!hasRange(left) || !hasRange(right)) {
        fail("noalias parameters must each have a declared read or write range");
      }
    }
    return invalid ? std::nullopt : std::optional<hir::EffectContract>(std::move(contract));
  };
  return validate();
}

std::optional<hir::EffectContract> Analyzer::validateEffectContract(
    const std::optional<ast::EffectClause>& clause,
    const std::vector<ast::ImplOpParam>& params, bool isExtern) {
  std::vector<ast::Param> converted;
  converted.reserve(params.size());
  for (const auto& param : params) {
    converted.emplace_back(param.name, "", param.templateName);
  }
  return validateEffectContract(clause, converted, isExtern);
}

} // namespace hitsimple::sema
