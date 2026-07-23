#pragma once

#include <string>
#include <vector>

namespace llvm {
class Module;
class TargetMachine;
} // namespace llvm

namespace hitsimple::codegen {

enum class OptimizationLevel {
  O0,
  O1,
  O2,
  O3,
  Os,
};

enum class PgoMode {
  None,
  Instrument,
  Use,
};

enum class SanitizerInstrumentation {
  None,
  Address,
};

struct OptimizationPipelineOptions final {
  OptimizationLevel optimization = OptimizationLevel::O2;
  PgoMode pgoMode = PgoMode::None;
  SanitizerInstrumentation sanitizer = SanitizerInstrumentation::None;
  std::string profilePath;
  bool emitRemarks = false;
};

struct OptimizationPipelineResult final {
  std::vector<std::string> remarks;
};

// Runs the native-code pipeline in place. The caller owns both objects and
// may reuse the TargetMachine for later object emission.
bool runOptimizationPipeline(llvm::Module& module,
                             llvm::TargetMachine& targetMachine,
                             const OptimizationPipelineOptions& options,
                             OptimizationPipelineResult& result,
                             std::string& error);

} // namespace hitsimple::codegen
