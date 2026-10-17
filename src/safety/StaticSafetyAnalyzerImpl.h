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
  bool validateStatementBody(const hir::Stmt& statement);
  void validateSafety(const hir::Expr& expression);
  void resetStaticSafetyState();
  StaticSafetyState staticSafetyState() const;
  void restoreStaticSafetyState(const StaticSafetyState& state);
  StaticSafetyState mergedStaticSafetyStates(
      const StaticSafetyState& left, const StaticSafetyState& right) const;
  void mergeStaticSafetyStates(const StaticSafetyState& left,
                               const StaticSafetyState& right);
  void expireStaticTemporaries();
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
  std::optional<std::int64_t> staticIndexOffset(
      const hir::Expr& expression) const;
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
  void validateStaticAddressAccess(
      const std::optional<StaticAddressFact>& fact, std::string_view operation);
  std::optional<bool> staticCStringTerminated(
      const hir::Expr& expression) const;
  std::optional<StaticAddressRange> staticAddressRange(
      const hir::Expr& expression) const;
  std::optional<StaticAddressRange> staticViewRange(
      const hir::Expr& expression) const;
  bool isRawMemoryAddress(const hir::Expr& expression) const;
  std::optional<StaticAddressRange> staticMemoryOperandRange(
      const hir::Expr& expression) const;

  std::optional<StaticAddressFact> staticBindingAddressFact(
      std::string_view bindingName, std::size_t offset,
      std::size_t byteLength) const;
  std::optional<std::string> staticContentKey(
      const std::optional<StaticAddressRange>& range,
      std::size_t byteLength) const;
  std::optional<std::string> staticContentKey(
      std::string_view bindingName, std::size_t offset,
      std::size_t byteLength) const;
  void validateStaticMemoryAccess(const std::optional<StaticAddressFact>& fact,
                                 std::uint64_t byteLength,
                                 std::string_view operation);
  std::optional<std::string> prepareStaticNamedStore(
      std::string_view bindingName, std::size_t offset, std::size_t byteLength);
  void validateStaticViewFormation(const hir::Expr& expression);
  void validateStaticResizeCopy(const hir::DynamicByteViewExpr& view);
  std::optional<StaticAddressFact> staticViewAddressFact(
      const hir::Expr& expression) const;
  void validateStaticCStringRead(const hir::Expr& expression);
  void recordStaticViewCapture(const std::string& name,
      const hir::Expr& source, bool retainLocation);
  std::unique_ptr<hir::Expr> captureStaticEvaluatedArgument(
      const hir::Expr& source, bool retainLocation, bool rawMemory = false);
  void releaseStaticCallArguments(
      const std::vector<std::unique_ptr<hir::Expr>>& arguments);
  bool isStaticCStringArgument(stdlib::BuiltinId builtin,
      std::uint16_t overloadIndex, const std::vector<hir::FormatArgKind>& kinds,
      std::size_t index) const;
  std::vector<std::unique_ptr<hir::Expr>> prepareStaticCallArguments(
      stdlib::BuiltinId builtin, std::uint16_t overloadIndex,
      const std::vector<hir::FormatArgKind>& kinds,
      const std::vector<std::unique_ptr<hir::Expr>>& arguments);

  void validateStaticCallArgument(stdlib::BuiltinId builtin,
      std::uint16_t overloadIndex, const std::vector<hir::FormatArgKind>& kinds,
      std::size_t index, const hir::Expr& argument);
  void validateStaticUserCallArguments(std::string_view callee,
      const std::vector<std::unique_ptr<hir::Expr>>& arguments);
  std::size_t validateStaticMemoryOperands(
      stdlib::BuiltinId builtin,
      const std::vector<std::unique_ptr<hir::Expr>>& arguments);

  // Lvalue captures freeze their location, while reads of those bytes remain
  // live. They must never resolve a subsequently rebound source name again.
  std::unordered_map<std::string, std::optional<std::string>> capturedAliases_;
  std::unordered_map<std::string, std::optional<StaticAddressRange>> capturedRanges_;

  StaticSafetyOptions options_;
  std::unordered_map<std::string, std::optional<std::int64_t>>
      staticIntegerValues_;
  std::unordered_map<std::string, std::optional<std::uint64_t>>
      staticUnsignedIntegerValues_;
  std::unordered_map<std::string, std::optional<StaticAddressFact>>
      staticAddressFacts_;
  std::unordered_map<std::string, std::optional<StaticAddressFact>>
      staticBindingAddressFacts_;
  // Results are snapshots at the call's evaluation point, not a re-read of
  // argument bindings after subsequent argument side effects or stores.
  std::unordered_map<const hir::Expr*, std::optional<StaticAddressFact>>
      staticCallAddressFacts_;
  std::unordered_map<std::string, std::size_t> staticDefinitionByteLengths_;
  std::unordered_set<std::string> staticPotentiallyReboundBindings_;
  std::unordered_map<std::string, std::vector<bool>> staticViewParameterCopies_;
  std::unordered_map<std::string, std::optional<bool>>
      staticCStringTerminations_;
  std::unordered_map<std::size_t, StaticDynamicObjectState>
      staticDynamicObjectStates_;
  std::unordered_set<std::string> staticGlobalBindings_;
  std::size_t nextStaticDynamicObjectId_ = 0;
  std::size_t nextStaticCallCaptureId_ = 0;
  StaticGotoContext* staticGotoContext_ = nullptr;
  StaticLoopContext* staticLoopContext_ = nullptr;
  StaticCatchContext* staticCatchContext_ = nullptr;
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
