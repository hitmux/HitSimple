#include "hitsimple/codegen/OptimizationPipeline.h"

#include "hitsimple/codegen/SanitizerPipeline.h"

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/DiagnosticPrinter.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/PGOOptions.h>
#include <llvm/Support/VirtualFileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hitsimple::codegen {
namespace {

llvm::OptimizationLevel toLlvmOptimizationLevel(OptimizationLevel level) {
  switch (level) {
  case OptimizationLevel::O0:
    return llvm::OptimizationLevel::O0;
  case OptimizationLevel::O1:
    return llvm::OptimizationLevel::O1;
  case OptimizationLevel::O2:
    return llvm::OptimizationLevel::O2;
  case OptimizationLevel::O3:
    return llvm::OptimizationLevel::O3;
  case OptimizationLevel::Os:
    return llvm::OptimizationLevel::Os;
  }
  return llvm::OptimizationLevel::O2;
}

std::optional<llvm::PGOOptions>
makePgoOptions(const OptimizationPipelineOptions& options) {
  const auto makeOptions = [&](llvm::PGOOptions::PGOAction action) {
#if LLVM_VERSION_MAJOR >= 22
    return llvm::PGOOptions(options.profilePath, "", "", "", action);
#else
    return llvm::PGOOptions(options.profilePath, "", "", "", nullptr,
                            action);
#endif
  };

  switch (options.pgoMode) {
  case PgoMode::None:
    return std::nullopt;
  case PgoMode::Instrument:
    return makeOptions(llvm::PGOOptions::IRInstr);
  case PgoMode::Use:
    return makeOptions(llvm::PGOOptions::IRUse);
  }
  return std::nullopt;
}

bool verifyModule(llvm::Module& module, std::string_view boundary,
                  std::string& error) {
  std::string verifierOutput;
  llvm::raw_string_ostream stream(verifierOutput);
  if (!llvm::verifyModule(module, &stream)) {
    return true;
  }
  stream.flush();
  error = "LLVM verifier failed " + std::string(boundary) + ":\n" +
          verifierOutput;
  return false;
}

class HitSimpleCanonicalizePass final
    : public llvm::PassInfoMixin<HitSimpleCanonicalizePass> {
public:
  explicit HitSimpleCanonicalizePass(bool emitRemark)
      : emitRemark_(emitRemark) {}

  llvm::PreservedAnalyses run(llvm::Module& module,
                              llvm::ModuleAnalysisManager&) {
    // This is intentionally a no-op registration point. Language-specific
    // canonicalization may be added here only after it has an independent
    // legality proof and regression coverage.
    if (emitRemark_) {
      for (auto& function : module) {
        if (function.isDeclaration()) {
          continue;
        }
        auto remark = llvm::OptimizationRemark(
            "HitSimpleCanonicalize", "PipelineBoundary", &function);
        remark << "HitSimple canonicalization completed; invoking the LLVM "
                  "default optimization pipeline";
        module.getContext().diagnose(remark);
        break;
      }
    }
    return llvm::PreservedAnalyses::all();
  }

private:
  bool emitRemark_ = false;
};

void appendOptimizationRemark(const llvm::DiagnosticInfo& diagnostic,
                              void* context) {
  if (diagnostic.getKind() != llvm::DK_OptimizationRemark) {
    return;
  }
  auto& remarks = *static_cast<std::vector<std::string>*>(context);
  std::string message;
  llvm::raw_string_ostream stream(message);
  llvm::DiagnosticPrinterRawOStream printer(stream);
  diagnostic.print(printer);
  stream.flush();
  remarks.push_back(std::move(message));
}

#if LLVM_VERSION_MAJOR == 18
void collectOptimizationRemark(const llvm::DiagnosticInfo& diagnostic,
                               void* context) {
  appendOptimizationRemark(diagnostic, context);
}
#else
void collectOptimizationRemark(const llvm::DiagnosticInfo* diagnostic,
                               void* context) {
  if (diagnostic != nullptr) {
    appendOptimizationRemark(*diagnostic, context);
  }
}
#endif

} // namespace

bool runOptimizationPipeline(llvm::Module& module,
                             llvm::TargetMachine& targetMachine,
                             const OptimizationPipelineOptions& options,
                             OptimizationPipelineResult& result,
                             std::string& error) {
  result.remarks.clear();
  if (!verifyModule(module, "before optimization", error)) {
    return false;
  }

  llvm::LoopAnalysisManager loopAnalysisManager;
  llvm::FunctionAnalysisManager functionAnalysisManager;
  llvm::CGSCCAnalysisManager cgsccAnalysisManager;
  llvm::ModuleAnalysisManager moduleAnalysisManager;
  llvm::PassBuilder passBuilder(&targetMachine, {}, makePgoOptions(options));
  passBuilder.registerModuleAnalyses(moduleAnalysisManager);
  passBuilder.registerCGSCCAnalyses(cgsccAnalysisManager);
  passBuilder.registerFunctionAnalyses(functionAnalysisManager);
  passBuilder.registerLoopAnalyses(loopAnalysisManager);
  passBuilder.crossRegisterProxies(loopAnalysisManager, functionAnalysisManager,
                                   cgsccAnalysisManager, moduleAnalysisManager);

  const auto level = toLlvmOptimizationLevel(options.optimization);
  addSanitizerAttributes(module, options.sanitizer);
  llvm::ModulePassManager pipeline;
  pipeline.addPass(llvm::VerifierPass());
  pipeline.addPass(HitSimpleCanonicalizePass(options.emitRemarks));
  pipeline.addPass(llvm::VerifierPass());
  registerSanitizerPasses(pipeline, options.sanitizer);
  pipeline.addPass(llvm::VerifierPass());
  if (level == llvm::OptimizationLevel::O0) {
    pipeline.addPass(passBuilder.buildO0DefaultPipeline(level));
  } else {
    pipeline.addPass(passBuilder.buildPerModuleDefaultPipeline(level));
  }
  pipeline.addPass(llvm::VerifierPass());
  if (options.emitRemarks) {
    module.getContext().setDiagnosticHandlerCallBack(
        collectOptimizationRemark, &result.remarks, false);
  }
  pipeline.run(module, moduleAnalysisManager);
  if (options.emitRemarks) {
    module.getContext().setDiagnosticHandlerCallBack(nullptr, nullptr, false);
  }
  return verifyModule(module, "after optimization", error);
}

} // namespace hitsimple::codegen
