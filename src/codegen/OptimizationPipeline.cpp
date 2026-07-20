#include "hitsimple/codegen/OptimizationPipeline.h"
#include "hitsimple/codegen/LlvmCompatibility.h"

#include <llvm/AsmParser/Parser.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/DiagnosticPrinter.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/PGOOptions.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/VirtualFileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>

#include <memory>
#include <optional>
#include <string>
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
  switch (options.pgoMode) {
  case PgoMode::None:
    return std::nullopt;
  case PgoMode::Instrument:
    return llvm::PGOOptions(options.profilePath, "", "", "", nullptr,
                            llvm::PGOOptions::IRInstr);
  case PgoMode::Use:
    return llvm::PGOOptions(options.profilePath, "", "", "", nullptr,
                            llvm::PGOOptions::IRUse);
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

std::optional<OptimizationPipelineResult>
runOptimizationPipeline(std::string_view llvmIr,
                        const OptimizationPipelineOptions& options,
                        std::string& error) {
  llvm::LLVMContext context;
  std::vector<std::string> remarks;
  if (options.emitRemarks) {
    context.setDiagnosticHandlerCallBack(collectOptimizationRemark, &remarks,
                                         false);
  }

  llvm::SMDiagnostic diagnostic;
  auto buffer = llvm::MemoryBuffer::getMemBuffer(llvmIr, "hitsimple-module");
  auto module = llvm::parseAssembly(*buffer, diagnostic, context);
  if (!module) {
    std::string diagnosticText;
    llvm::raw_string_ostream stream(diagnosticText);
    diagnostic.print("hsc", stream);
    stream.flush();
    error = "cannot parse generated LLVM IR:\n" + diagnosticText;
    return std::nullopt;
  }
  if (!verifyModule(*module, "before optimization", error)) {
    return std::nullopt;
  }

  if (llvm::InitializeNativeTarget() ||
      llvm::InitializeNativeTargetAsmPrinter()) {
    error = "cannot initialize LLVM native target for optimization";
    return std::nullopt;
  }
  const std::string targetTriple = moduleTargetTriple(*module);
  std::string targetError;
  const auto* target = resolveTarget(targetTriple, targetError);
  if (target == nullptr) {
    error = "cannot resolve LLVM target '" + targetTriple +
            "' for optimization: " + targetError;
    return std::nullopt;
  }
  llvm::TargetOptions targetOptions;
  std::unique_ptr<llvm::TargetMachine> targetMachine(
      createGenericTargetMachine(*target, targetTriple, targetOptions));
  if (!targetMachine) {
    error = "cannot create LLVM target machine for optimization";
    return std::nullopt;
  }

  llvm::LoopAnalysisManager loopAnalysisManager;
  llvm::FunctionAnalysisManager functionAnalysisManager;
  llvm::CGSCCAnalysisManager cgsccAnalysisManager;
  llvm::ModuleAnalysisManager moduleAnalysisManager;
  llvm::PassBuilder passBuilder(targetMachine.get(), {}, makePgoOptions(options));
  passBuilder.registerModuleAnalyses(moduleAnalysisManager);
  passBuilder.registerCGSCCAnalyses(cgsccAnalysisManager);
  passBuilder.registerFunctionAnalyses(functionAnalysisManager);
  passBuilder.registerLoopAnalyses(loopAnalysisManager);
  passBuilder.crossRegisterProxies(loopAnalysisManager, functionAnalysisManager,
                                   cgsccAnalysisManager, moduleAnalysisManager);

  const auto level = toLlvmOptimizationLevel(options.optimization);
  llvm::ModulePassManager pipeline;
  pipeline.addPass(llvm::VerifierPass());
  pipeline.addPass(HitSimpleCanonicalizePass(options.emitRemarks));
  pipeline.addPass(llvm::VerifierPass());
  if (level == llvm::OptimizationLevel::O0) {
    pipeline.addPass(passBuilder.buildO0DefaultPipeline(level));
  } else {
    pipeline.addPass(passBuilder.buildPerModuleDefaultPipeline(level));
  }
  pipeline.addPass(llvm::VerifierPass());
  pipeline.run(*module, moduleAnalysisManager);
  if (!verifyModule(*module, "after optimization", error)) {
    return std::nullopt;
  }

  OptimizationPipelineResult result;
  llvm::raw_string_ostream output(result.llvmIr);
  module->print(output, nullptr);
  output.flush();
  result.remarks = std::move(remarks);
  return result;
}

} // namespace hitsimple::codegen
