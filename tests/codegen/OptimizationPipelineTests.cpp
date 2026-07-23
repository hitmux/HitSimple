#include "support/TestRunner.h"

#include "hitsimple/codegen/LlvmCompatibility.h"
#include "hitsimple/codegen/NativeTarget.h"
#include "hitsimple/codegen/OptimizationPipeline.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>

#include <array>
#include <memory>
#include <optional>
#include <string>

namespace {

struct OptimizationFixture final {
  std::unique_ptr<llvm::LLVMContext> context;
  std::unique_ptr<llvm::Module> module;
  hitsimple::codegen::NativeTarget target;
};

std::optional<OptimizationFixture>
makeFixture(bool invalid, hitsimple::codegen::OptimizationLevel optimization,
            std::string& error) {
  auto context = std::make_unique<llvm::LLVMContext>();
  auto module = std::make_unique<llvm::Module>("optimization-test", *context);
  const auto triple = llvm::sys::getDefaultTargetTriple();
  hitsimple::codegen::setModuleTargetTriple(*module, triple);

  hitsimple::codegen::NativeTargetOptions targetOptions;
  targetOptions.triple = triple;
  targetOptions.optimization = optimization;
  auto target = hitsimple::codegen::createNativeTarget(targetOptions, error);
  if (!target) {
    return std::nullopt;
  }
  module->setDataLayout(target->machine->createDataLayout());

  llvm::IRBuilder<> builder(*context);
  auto* functionType = llvm::FunctionType::get(builder.getInt32Ty(), false);
  auto* function = llvm::Function::Create(
      functionType, llvm::GlobalValue::ExternalLinkage, "main", *module);
  auto* entry = llvm::BasicBlock::Create(*context, "entry", function);
  auto* exit = llvm::BasicBlock::Create(*context, "exit", function);
  builder.SetInsertPoint(entry);
  builder.CreateBr(exit);
  builder.SetInsertPoint(exit);
  if (invalid) {
    auto* value = builder.CreatePHI(builder.getInt32Ty(), 1, "value");
    value->addIncoming(builder.getInt32(1), exit);
    builder.CreateRet(value);
  } else {
    builder.CreateRet(builder.getInt32(0));
  }
  return OptimizationFixture{std::move(context), std::move(module),
                             std::move(*target)};
}

void addOverflowingMemoryAccess(OptimizationFixture &fixture) {
  llvm::IRBuilder<> builder(*fixture.context);
  auto *function = fixture.module->getFunction("main");
  auto *entry = &function->getEntryBlock();
  auto *returnInstruction = entry->getTerminator();
  builder.SetInsertPoint(returnInstruction);
  auto *buffer = builder.CreateAlloca(builder.getInt8Ty(), builder.getInt32(4),
                                      "buffer");
  auto *outOfBounds = builder.CreateInBoundsGEP(
      builder.getInt8Ty(), buffer, builder.getInt32(4), "out.of.bounds");
  builder.CreateStore(builder.getInt8(1), outOfBounds);
  new llvm::GlobalVariable(*fixture.module,
                           llvm::ArrayType::get(builder.getInt8Ty(), 4),
                           false, llvm::GlobalValue::ExternalLinkage,
                           llvm::ConstantAggregateZero::get(
                               llvm::ArrayType::get(builder.getInt8Ty(), 4)),
                           "global.buffer");
}

std::string moduleText(const llvm::Module &module) {
  std::string text;
  llvm::raw_string_ostream stream(text);
  module.print(stream, nullptr);
  stream.flush();
  return text;
}

} // namespace

HS_TEST(OptimizationPipeline_CollectsCanonicalizationRemark) {
  std::string error;
  auto fixture = makeFixture(false, hitsimple::codegen::OptimizationLevel::O0,
                             error);
  HS_EXPECT_TRUE(fixture.has_value());
  HS_EXPECT_TRUE(error.empty());

  hitsimple::codegen::OptimizationPipelineOptions options;
  options.optimization = hitsimple::codegen::OptimizationLevel::O0;
  options.emitRemarks = true;
  hitsimple::codegen::OptimizationPipelineResult result;
  const auto succeeded = hitsimple::codegen::runOptimizationPipeline(
      *fixture->module, *fixture->target.machine, options, result, error);

  HS_EXPECT_TRUE(succeeded);
  HS_EXPECT_TRUE(error.empty());
  HS_EXPECT_TRUE(result.remarks.size() == 1U);
  HS_EXPECT_TRUE(result.remarks.front().find(
                     "HitSimple canonicalization completed") !=
                 std::string::npos);
}

HS_TEST(OptimizationPipeline_RunsEverySupportedOptimizationLevelInPlace) {
  constexpr std::array levels{
      hitsimple::codegen::OptimizationLevel::O0,
      hitsimple::codegen::OptimizationLevel::O1,
      hitsimple::codegen::OptimizationLevel::O2,
      hitsimple::codegen::OptimizationLevel::O3,
      hitsimple::codegen::OptimizationLevel::Os,
  };
  for (const auto level : levels) {
    std::string error;
    auto fixture = makeFixture(false, level, error);
    HS_EXPECT_TRUE(fixture.has_value());
    HS_EXPECT_TRUE(error.empty());

    hitsimple::codegen::OptimizationPipelineOptions options;
    options.optimization = level;
    hitsimple::codegen::OptimizationPipelineResult result;
    HS_EXPECT_TRUE(hitsimple::codegen::runOptimizationPipeline(
        *fixture->module, *fixture->target.machine, options, result, error));
    HS_EXPECT_TRUE(error.empty());
    HS_EXPECT_TRUE(fixture->module->getFunction("main") != nullptr);
  }
}

HS_TEST(OptimizationPipeline_RejectsInvalidModuleBeforeOptimization) {
  std::string error;
  auto fixture = makeFixture(true, hitsimple::codegen::OptimizationLevel::O2,
                             error);
  HS_EXPECT_TRUE(fixture.has_value());
  HS_EXPECT_TRUE(error.empty());

  hitsimple::codegen::OptimizationPipelineOptions options;
  hitsimple::codegen::OptimizationPipelineResult result;
  const auto succeeded = hitsimple::codegen::runOptimizationPipeline(
      *fixture->module, *fixture->target.machine, options, result, error);

  HS_EXPECT_TRUE(!succeeded);
  HS_EXPECT_TRUE(error.find("LLVM verifier failed before optimization") !=
                 std::string::npos);
}

HS_TEST(OptimizationPipeline_AddressSanitizerInstrumentsFunctionsAndGlobals) {
  std::string error;
  auto fixture = makeFixture(false, hitsimple::codegen::OptimizationLevel::O2,
                             error);
  HS_EXPECT_TRUE(fixture.has_value());
  HS_EXPECT_TRUE(error.empty());
  addOverflowingMemoryAccess(*fixture);

  hitsimple::codegen::OptimizationPipelineOptions options;
  options.sanitizer = hitsimple::codegen::SanitizerInstrumentation::Address;
  hitsimple::codegen::OptimizationPipelineResult result;
  HS_EXPECT_TRUE(hitsimple::codegen::runOptimizationPipeline(
      *fixture->module, *fixture->target.machine, options, result, error));
  HS_EXPECT_TRUE(error.empty());

  const auto text = moduleText(*fixture->module);
  HS_EXPECT_TRUE(text.find("__asan_report") != std::string::npos);
  HS_EXPECT_TRUE(text.find("asan.module_ctor") != std::string::npos);
  HS_EXPECT_TRUE(text.find("__asan_global") != std::string::npos);
}

HS_TEST(OptimizationPipeline_DisabledAddressSanitizerAddsNoInstrumentation) {
  std::string error;
  auto fixture = makeFixture(false, hitsimple::codegen::OptimizationLevel::O2,
                             error);
  HS_EXPECT_TRUE(fixture.has_value());
  HS_EXPECT_TRUE(error.empty());
  addOverflowingMemoryAccess(*fixture);

  hitsimple::codegen::OptimizationPipelineOptions options;
  hitsimple::codegen::OptimizationPipelineResult result;
  HS_EXPECT_TRUE(hitsimple::codegen::runOptimizationPipeline(
      *fixture->module, *fixture->target.machine, options, result, error));
  HS_EXPECT_TRUE(error.empty());

  const auto text = moduleText(*fixture->module);
  HS_EXPECT_TRUE(text.find("__asan_") == std::string::npos);
  HS_EXPECT_TRUE(text.find("asan.module_ctor") == std::string::npos);
}
