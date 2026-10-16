#include "support/TestRunner.h"

#include "hitsimple/codegen/LLVMCodegen.h"
#include "hitsimple/parser/Parser.h"
#include "hitsimple/sema/Sema.h"

#include <llvm/IR/Instructions.h>
#include <llvm/IR/Verifier.h>

#include <string>

HS_TEST(LLVMCodegen_CstrAssignmentMaterializesOneCallPerSource) {
  auto parsed = hitsimple::parser::parseSource(
      "func text() -> [8] as cstr { return \"hello\"; }\n"
      "func named() -> out[4] as cstr { return text(); }\n"
      "func anonymous() -> [4] as cstr { return text(); }\n"
      "func pair() -> ([8] as cstr, i32) { return text(), 42; }\n"
      "func main() -> i32 {\n"
      "  new dst[4] as cstr = text()\n"
      "  new raw[8] as bytes = \"rawdata\"\n"
      "  dst = raw as cstr\n"
      "  new code as i32\n"
      "  dst, code = pair()\n"
      "  return 0\n}\n", "cstr-test.hs");
  HS_EXPECT_TRUE(parsed.unit != nullptr);
  HS_EXPECT_TRUE(parsed.error.empty());
  auto analyzed = hitsimple::sema::analyze(*parsed.unit);
  HS_EXPECT_TRUE(analyzed.diagnostics.empty());
  HS_EXPECT_TRUE(analyzed.unit != nullptr);
  for (const auto mode : {hitsimple::codegen::SafetyMode::Unchecked,
                          hitsimple::codegen::SafetyMode::StaticChecked,
                          hitsimple::codegen::SafetyMode::Checked}) {
    hitsimple::codegen::CodegenOptions options;
    options.safetyMode = mode;
    auto emitted = hitsimple::codegen::emitLlvmModule(*analyzed.unit, "cstr-test.hs", options);
    HS_EXPECT_TRUE(emitted.ok());
    HS_EXPECT_TRUE(!llvm::verifyModule(*emitted.module));
    for (const auto *name : {"main", "named", "anonymous", "pair"}) {
      unsigned textCalls = 0;
      unsigned pairCalls = 0;
      unsigned extracts = 0;
      unsigned byteStores = 0;
      const auto *function = emitted.module->getFunction(name);
      HS_EXPECT_TRUE(function != nullptr);
      for (const auto &block : *function) {
        for (const auto &instruction : block) {
          if (const auto *call = llvm::dyn_cast<llvm::CallInst>(&instruction);
              call != nullptr && call->getCalledFunction() != nullptr) {
            if (call->getCalledFunction()->getName() == "text") ++textCalls;
            if (call->getCalledFunction()->getName() == "pair") ++pairCalls;
          }
          if (llvm::isa<llvm::ExtractValueInst>(instruction)) ++extracts;
          if (const auto *store = llvm::dyn_cast<llvm::StoreInst>(&instruction);
              store != nullptr && store->getValueOperand()->getType()->isIntegerTy(8)) {
            ++byteStores;
            HS_EXPECT_EQ(store->getAlign().value(), 1U);
          }
          HS_EXPECT_TRUE(!llvm::isa<llvm::FPToSIInst>(instruction));
          HS_EXPECT_TRUE(!llvm::isa<llvm::FPToUIInst>(instruction));
        }
      }
      HS_EXPECT_EQ(textCalls, 1U);
      HS_EXPECT_TRUE(byteStores >= 4U);
      if (std::string(name) == "main") {
        HS_EXPECT_EQ(pairCalls, 1U);
        HS_EXPECT_EQ(extracts, 2U);
      }
    }
  }
}
