#include "SemaAnalyzer.h"

namespace hitsimple::sema {

namespace {

thread_local bool requireMainForCurrentAnalysis = true;

class MainRequirementScope {
public:
  explicit MainRequirementScope(bool requireMain)
      : previous_(requireMainForCurrentAnalysis) {
    requireMainForCurrentAnalysis = requireMain;
  }

  ~MainRequirementScope() { requireMainForCurrentAnalysis = previous_; }

private:
  bool previous_;
};

} // namespace

bool isMainRequiredForCurrentAnalysis() {
  return requireMainForCurrentAnalysis;
}

AnalyzeResult analyze(const ast::TranslationUnit& unit, AnalyzeOptions options) {
  MainRequirementScope scope(options.requireMain);
  return Analyzer().analyze(unit, options);
}

} // namespace hitsimple::sema
