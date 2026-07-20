#include "support/TestRunner.h"

#include "hitsimple/codegen/OptimizationPipeline.h"

#include <llvm/TargetParser/Host.h>

#include <string>

HS_TEST(OptimizationPipeline_CollectsCanonicalizationRemark) {
  const std::string llvmIr =
      "target triple = \"" + llvm::sys::getDefaultTargetTriple() + "\"\n"
      "define i32 @main() {\n"
      "entry:\n"
      "  ret i32 0\n"
      "}\n";

  hitsimple::codegen::OptimizationPipelineOptions options;
  options.optimization = hitsimple::codegen::OptimizationLevel::O0;
  options.emitRemarks = true;
  std::string error;
  const auto result =
      hitsimple::codegen::runOptimizationPipeline(llvmIr, options, error);

  HS_EXPECT_TRUE(result.has_value());
  HS_EXPECT_TRUE(error.empty());
  HS_EXPECT_TRUE(result->remarks.size() == 1U);
  HS_EXPECT_TRUE(result->remarks.front().find("HitSimple canonicalization completed") !=
                 std::string::npos);
}
