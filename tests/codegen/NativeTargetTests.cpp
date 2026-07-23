#include "support/TestRunner.h"

#include "hitsimple/codegen/LlvmCompatibility.h"
#include "hitsimple/codegen/NativeTarget.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/TargetParser/Host.h>

#include <string>

namespace {

hitsimple::codegen::NativeTargetOptions nativeHostOptions() {
  hitsimple::codegen::NativeTargetOptions options;
  options.triple = llvm::sys::getDefaultTargetTriple();
  return options;
}

} // namespace

HS_TEST(NativeTarget_MapsOptimizationLevels) {
  using hitsimple::codegen::OptimizationLevel;
  using hitsimple::codegen::toCodeGenOptLevel;

  HS_EXPECT_EQ(toCodeGenOptLevel(OptimizationLevel::O0),
               llvm::CodeGenOptLevel::None);
  HS_EXPECT_EQ(toCodeGenOptLevel(OptimizationLevel::O1),
               llvm::CodeGenOptLevel::Less);
  HS_EXPECT_EQ(toCodeGenOptLevel(OptimizationLevel::O2),
               llvm::CodeGenOptLevel::Default);
  HS_EXPECT_EQ(toCodeGenOptLevel(OptimizationLevel::O3),
               llvm::CodeGenOptLevel::Aggressive);
  HS_EXPECT_EQ(toCodeGenOptLevel(OptimizationLevel::Os),
               llvm::CodeGenOptLevel::Default);
}

HS_TEST(NativeTarget_RejectsInvalidTripleWithError) {
  auto options = nativeHostOptions();
  options.triple = "invalid-unknown-hitsimple";
  std::string error;

  const auto target = hitsimple::codegen::createNativeTarget(options, error);

  HS_EXPECT_TRUE(!target.has_value());
  HS_EXPECT_TRUE(error.find("cannot resolve LLVM target") != std::string::npos);
  HS_EXPECT_TRUE(error.find(options.triple) != std::string::npos);
}

HS_TEST(NativeTarget_ModuleDataLayoutMatchesMachine) {
  const auto options = nativeHostOptions();
  std::string error;
  const auto target = hitsimple::codegen::createNativeTarget(options, error);

  HS_EXPECT_TRUE(target.has_value());
  HS_EXPECT_TRUE(error.empty());

  llvm::LLVMContext context;
  llvm::Module module("native-target-test", context);
  hitsimple::codegen::setModuleTargetTriple(module, options.triple);
  module.setDataLayout(target->machine->createDataLayout());

  HS_EXPECT_EQ(module.getDataLayout().getStringRepresentation(),
               target->machine->createDataLayout().getStringRepresentation());
}
