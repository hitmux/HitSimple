#pragma once

#include "safety/StaticSafetyAnalyzer.h"
#include "safety/StaticSafetyState.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hitsimple::safety::detail {

class StaticSafetyAnalyzer {
public:
  explicit StaticSafetyAnalyzer(StaticSafetyOptions options)
      : options_(options) {}

  StaticSafetyResult analyze(const hir::TranslationUnit& unit) {
    if (!options_.enabled) {
      return {};
    }
    validateSafety(unit);
    return StaticSafetyResult{std::move(diagnostics_)};
  }

private:
  class SourceRangeScope final {
  public:
    SourceRangeScope(
        StaticSafetyAnalyzer& analyzer,
        const std::optional<diagnostic::SourceRange>& range)
        : analyzer_(analyzer), previous_(analyzer.currentDiagnosticRange_) {
      if (range) {
        analyzer_.currentDiagnosticRange_ = range;
      }
    }

    ~SourceRangeScope() {
      analyzer_.currentDiagnosticRange_ = std::move(previous_);
    }

  private:
    StaticSafetyAnalyzer& analyzer_;
    std::optional<diagnostic::SourceRange> previous_;
  };

  void addDiagnostic(std::string message) {
    auto emitted =
        diagnostic::Diagnostic::error(diagnostic::Stage::Codegen,
                                      std::move(message));
    emitted.range = currentDiagnosticRange_;
    diagnostics_.push_back(std::move(emitted));
  }

  void validateSafety(const hir::TranslationUnit& unit);
  bool validateSafety(const hir::Block& block);
  bool validateSafety(const hir::Stmt& statement);
  void validateSafety(const hir::Expr& expression);
  void resetStaticSafetyState();
  StaticSafetyState staticSafetyState() const;
  void restoreStaticSafetyState(const StaticSafetyState& state);
  StaticSafetyState mergedStaticSafetyStates(
      const StaticSafetyState& left, const StaticSafetyState& right) const;
  void mergeStaticSafetyStates(const StaticSafetyState& left,
                               const StaticSafetyState& right);
  void expireStaticLocalBindings(const std::vector<std::string>& bindingNames);
  bool enqueueStaticGoto(std::string_view label,
                         const StaticSafetyState& incoming);
  StaticSafetyState exitStaticScopes(StaticGotoContext* target,
                                     const StaticSafetyState& incoming);
  void invalidateStaticBinding(std::string_view bindingName);
  void invalidateStaticFactsOverlapping(
      const std::optional<StaticAddressRange>& range,
      std::optional<std::uint64_t> byteLength);
  void invalidateStaticGlobalFacts();
  std::optional<std::int64_t> staticSignedInteger(
      const hir::Expr& expression) const;
  std::optional<std::uint64_t> staticUnsignedInteger(
      const hir::Expr& expression) const;
  std::optional<bool> staticBooleanValue(const hir::Expr& expression) const;
  std::optional<StaticAddressFact> staticAddressFact(
      const hir::Expr& expression) const;
  void validateStaticDynamicBase(const hir::Expr& expression,
                                 std::string_view operation);
  bool releaseStaticDynamicObject(const hir::Expr& expression);
  void recordStaticAddressAssignment(std::string_view bindingName,
                                     const hir::Expr& value);
  void validateStaticAddressAccess(const hir::Expr& expression,
                                   std::string_view operation);
  std::optional<bool> staticCStringTerminated(
      const hir::Expr& expression) const;
  std::optional<StaticAddressRange> staticAddressRange(
      const hir::Expr& expression) const;
  std::optional<StaticAddressRange> staticViewRange(
      const hir::Expr& expression) const;
  std::optional<StaticAddressRange> staticMemoryOperandRange(
      const hir::Expr& expression) const;

  StaticSafetyOptions options_;
  std::unordered_map<std::string, std::optional<std::int64_t>>
      staticIntegerValues_;
  std::unordered_map<std::string, std::optional<std::uint64_t>>
      staticUnsignedIntegerValues_;
  std::unordered_map<std::string, std::optional<StaticAddressFact>>
      staticAddressFacts_;
  std::unordered_map<std::string, std::optional<bool>>
      staticCStringTerminations_;
  std::unordered_map<std::size_t, StaticDynamicObjectState>
      staticDynamicObjectStates_;
  std::unordered_set<std::string> staticGlobalBindings_;
  std::size_t nextStaticDynamicObjectId_ = 0;
  StaticGotoContext* staticGotoContext_ = nullptr;
  StaticLoopContext* staticLoopContext_ = nullptr;
  std::optional<diagnostic::SourceRange> currentDiagnosticRange_;
  std::vector<diagnostic::Diagnostic> diagnostics_;
};

std::optional<std::int64_t> constantSignedInteger(const hir::Expr& expression);
std::optional<std::uint64_t> constantUnsignedInteger(
    const hir::Expr& expression);
std::optional<std::int64_t> addSignedIntegers(std::int64_t left,
                                              std::int64_t right);
std::optional<std::uint64_t> multiplyUnsignedIntegers(std::uint64_t left,
                                                       std::uint64_t right);
std::optional<std::int64_t> signedMinimumForByteLength(
    std::size_t byteLength);

} // namespace hitsimple::safety::detail
