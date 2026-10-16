#include "SemaTestSupport.h"

#include "hitsimple/codegen/LLVMCodegen.h"
#include "hitsimple/hir/HIR.h"

#include <llvm/IR/Instructions.h>

#include <array>
#include <string>

using hitsimple::testing::sema::analyzeSource;
namespace hir = hitsimple::hir;

HS_TEST(Sema_NamedReturnsCreateEntryStorageWithoutInitialization) {
  auto result = analyzeSource(
      "func named() -> (out as i32, raw[16] as bytes) {\n"
      "  out = 42\n"
      "  new source[16] as bytes = \"123456789012345\"\n"
      "  raw = source\n"
      "  return\n"
      "}\nfunc main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto &body = result.unit->functions[0]->body->statements;
  HS_EXPECT_EQ(body.size(), 6U);
  const auto *out = dynamic_cast<const hir::LocalMemory *>(body[0].get());
  const auto *raw = dynamic_cast<const hir::LocalMemory *>(body[1].get());
  HS_EXPECT_TRUE(out != nullptr && raw != nullptr);
  HS_EXPECT_EQ(out->byteLength, 4U);
  HS_EXPECT_EQ(out->templateName, "i32");
  HS_EXPECT_EQ(raw->byteLength, 16U);
  HS_EXPECT_EQ(raw->templateName, "bytes");
  HS_EXPECT_TRUE(dynamic_cast<const hir::IntegerStore *>(body[2].get()) != nullptr);
  const auto *returned = dynamic_cast<const hir::Return *>(body.back().get());
  HS_EXPECT_TRUE(returned != nullptr);
  HS_EXPECT_TRUE(returned->conversionPlans.empty());
  HS_EXPECT_EQ(returned->values.size(), 2U);
  const auto *value = dynamic_cast<const hir::VariableRef *>(returned->values[0].get());
  HS_EXPECT_TRUE(value != nullptr);
  HS_EXPECT_EQ(value->bindingName, out->bindingName);
}

HS_TEST(Sema_NamedReturnsAssignExplicitValuesInItemOrder) {
  auto result = analyzeSource(
      "func ordered() -> (first as i32, second as i32) { return 41, first + 1; }\n"
      "func mixed() -> (i32, second as i32) { return 20, 22; }\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  for (std::size_t index = 0; index < 2U; ++index) {
    const auto &body = result.unit->functions[index]->body->statements;
    HS_EXPECT_EQ(body.size(), 3U);
    const auto *first = dynamic_cast<const hir::LocalMemory *>(body[0].get());
    const auto *second = dynamic_cast<const hir::LocalMemory *>(body[1].get());
    const auto *sequence = dynamic_cast<const hir::StatementList *>(body[2].get());
    HS_EXPECT_TRUE(first && second && sequence);
    HS_EXPECT_EQ(sequence->statements.size(), 3U);
    const auto *writeFirst = dynamic_cast<const hir::IntegerStore *>(sequence->statements[0].get());
    const auto *writeSecond = dynamic_cast<const hir::IntegerStore *>(sequence->statements[1].get());
    HS_EXPECT_TRUE(writeFirst && writeSecond);
    HS_EXPECT_EQ(writeFirst->bindingName, first->bindingName);
    HS_EXPECT_EQ(writeSecond->bindingName, second->bindingName);
    HS_EXPECT_TRUE(dynamic_cast<const hir::Return *>(sequence->statements[2].get()));
  }
}

HS_TEST(Sema_NamedReturnBareReadKeepsBindingAndSignature) {
  auto result = analyzeSource(
      "func shadow() -> out as bool {\n"
      "  set out as u8\n"
      "  out = 255\n"
      "  if (true) { new out as i32 = 42; return; }\n"
      "  return\n"
      "}\nfunc main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto &body = result.unit->functions[0]->body->statements;
  const auto *slot = dynamic_cast<const hir::LocalMemory *>(body.front().get());
  const auto *branch = dynamic_cast<const hir::If *>(body[3].get());
  HS_EXPECT_TRUE(slot && branch);
  const auto *returned = dynamic_cast<const hir::Return *>(branch->thenBlock->statements.back().get());
  HS_EXPECT_TRUE(returned && returned->conversionPlans.empty());
  const auto *value = dynamic_cast<const hir::VariableRef *>(returned->values[0].get());
  HS_EXPECT_TRUE(value);
  HS_EXPECT_EQ(value->bindingName, slot->bindingName);
  HS_EXPECT_EQ(value->templateName, "bool");
  HS_EXPECT_EQ(value->byteLength, 1U);
}

HS_TEST(Sema_NamedReturnBoolExplicitAssignmentAcceptsAnyView) {
  for (const auto &source : {
           "func f(value as handle) -> out as bool { return value; }\n",
           "func f(value[16] as bytes) -> out as bool { return value; }\n",
           "func f(value as f128) -> out as bool { return value; }\n"}) {
    auto result = analyzeSource(std::string(source) + "func main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.unit != nullptr);
    HS_EXPECT_TRUE(result.diagnostics.empty());
    const auto *sequence = dynamic_cast<const hir::StatementList *>(
        result.unit->functions[0]->body->statements.back().get());
    HS_EXPECT_TRUE(sequence);
    const auto *store = dynamic_cast<const hir::BoolStore *>(sequence->statements[0].get());
    HS_EXPECT_TRUE(store);
    HS_EXPECT_TRUE(dynamic_cast<const hir::BooleanTestExpr *>(store->value.get()));
  }
}

HS_TEST(Sema_NamedReturnsRejectSameScopeCollisions) {
  for (const auto &source : {
           "func f() -> (out as i32, out as i32) { return 1, 2; }\n",
           "func f(out as i32) -> out as i32 { return out; }\n",
           "func f() -> out as i32 { new out as i32 = 1; return; }\n",
           "func f() -> out as i32 { static out as i32 = 1; return; }\n",
           "template T { x[4] as i32; }\nimpl T { func f(self as T, out as i32) -> out as i32 { return out; } }\n",
           "template T { x[4] as i32; }\nimpl T { op + (left as T, right as T) -> left as T { return left; } }\n"}) {
    auto result = analyzeSource(std::string(source) + "func main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.unit == nullptr);
    HS_EXPECT_TRUE(!result.diagnostics.empty());
    HS_EXPECT_TRUE(result.diagnostics[0].message.find("duplicate") != std::string::npos);
  }
}

HS_TEST(Sema_NamedReturnsRejectAnonymousBareAndWrongCounts) {
  for (const auto &source : {
           "func f() -> i32 { return; }\n",
           "func f() -> (out as i32, i32) { out = 1; return; }\n",
           "func f() -> (out as i32, second as i32) { return 1; }\n",
           "func f() -> out as i32 { return 1, 2; }\n",
           "func f() -> out as i32 { return 1; }\nfunc g() -> i32 { return out; }\n"}) {
    auto result = analyzeSource(std::string(source) + "func main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.unit == nullptr);
    HS_EXPECT_TRUE(!result.diagnostics.empty());
  }
}

HS_TEST(Sema_NamedReturnsUseSharedMethodAndOperatorStorage) {
  auto result = analyzeSource(
      "template T { x[4] as i32; }\n"
      "impl T {\n"
      "  func get(self as T) -> out as i32 { out = self.x; return; }\n"
      "  op + (left as T, right as T) -> out as T { out.x = left.x + right.x; return; }\n"
      "}\nfunc main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  for (std::size_t index = 0; index < 2U; ++index) {
    const auto &function = result.unit->functions[index];
    HS_EXPECT_TRUE(function->usesViewAbi);
    HS_EXPECT_TRUE(dynamic_cast<const hir::LocalMemory *>(function->body->statements.front().get()));
    const auto *returned = dynamic_cast<const hir::Return *>(function->body->statements.back().get());
    HS_EXPECT_TRUE(returned);
    HS_EXPECT_TRUE(dynamic_cast<const hir::VariableRef *>(returned->values[0].get()));
  }
}

HS_TEST(LLVMCodegen_NamedReturnsDoNotInventZeroInitialization) {
  // An uninitialized read has undefined behavior. Inspect allocation only;
  // never execute this function or assert a value for its unwritten bytes.
  auto result = analyzeSource(
      "func unwritten() -> out as i32 { return; }\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(result.unit != nullptr);
  auto emitted = hitsimple::codegen::emitLlvmModule(*result.unit, "test.hs");
  HS_EXPECT_TRUE(emitted.module != nullptr);
  HS_EXPECT_TRUE(emitted.diagnostics.empty());
  const auto *function = emitted.module->getFunction("unwritten");
  HS_EXPECT_TRUE(function);
  std::size_t allocations = 0;
  for (const auto &block : *function) {
    for (const auto &instruction : block) {
      if (llvm::isa<llvm::AllocaInst>(instruction)) { ++allocations; }
      HS_EXPECT_TRUE(!llvm::isa<llvm::StoreInst>(instruction));
    }
  }
  HS_EXPECT_EQ(allocations, 1U);
}

HS_TEST(LLVMCodegen_NamedReturnsStillRequireExplicitReturn) {
  for (const auto mode : {hitsimple::codegen::SafetyMode::Unchecked,
                          hitsimple::codegen::SafetyMode::StaticChecked,
                          hitsimple::codegen::SafetyMode::Checked}) {
    auto result = analyzeSource(
        "func missing() -> out as i32 { out = 42; }\n"
        "func main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.unit != nullptr);
    hitsimple::codegen::CodegenOptions options;
    options.safetyMode = mode;
    auto emitted = hitsimple::codegen::emitLlvmModule(*result.unit, "test.hs", options);
    HS_EXPECT_TRUE(!emitted.diagnostics.empty());
    HS_EXPECT_TRUE(emitted.diagnostics[0].message.find("missing return") != std::string::npos);
  }
}

HS_TEST(Sema_NamedReturnCopiesCStringRvalueThroughBoundedStorage) {
  auto result = analyzeSource(
      "func text() -> [8] as cstr { return \"1234567\"; }\n"
      "func copy() -> out[4] as cstr { return text(); }\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto *sequence = dynamic_cast<const hir::StatementList *>(
      result.unit->functions[1]->body->statements.back().get());
  HS_EXPECT_TRUE(sequence);
  HS_EXPECT_EQ(sequence->statements.size(), 4U);
  const auto *source = dynamic_cast<const hir::LocalMemory *>(sequence->statements[0].get());
  const auto *materialized = dynamic_cast<const hir::ViewCopyStore *>(sequence->statements[1].get());
  const auto *copy = dynamic_cast<const hir::StringCopyStore *>(sequence->statements[2].get());
  HS_EXPECT_TRUE(source && materialized && copy);
  HS_EXPECT_EQ(source->byteLength, 8U);
  HS_EXPECT_TRUE(dynamic_cast<const hir::CallExpr *>(materialized->value.get()));
  HS_EXPECT_EQ(copy->sourceBindingName, source->bindingName);
  HS_EXPECT_EQ(copy->targetByteLength, 4U);
}

HS_TEST(Sema_NamedReturnRejectsInvalidDestinationSources) {
  for (const auto &source : {
           "func f(value as i64) -> out[8] as cstr { return value; }\n",
           "func f(value as i32) -> out[8] as bytes { return value; }\n",
           "func f(value as f64) -> out[8] as none { return value; }\n"}) {
    auto result = analyzeSource(std::string(source) + "func main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.unit == nullptr);
    HS_EXPECT_TRUE(!result.diagnostics.empty());
  }
}
