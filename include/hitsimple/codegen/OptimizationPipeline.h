#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
  std::string llvmIr;
  std::vector<std::string> remarks;
};

// Runs the native-code pipeline over LLVM IR emitted by the frontend. The
// caller retains responsibility for serializing the returned IR and for final
// target code generation.
std::optional<OptimizationPipelineResult>
runOptimizationPipeline(std::string_view llvmIr,
                        const OptimizationPipelineOptions& options,
                        std::string& error);

} // namespace hitsimple::codegen
