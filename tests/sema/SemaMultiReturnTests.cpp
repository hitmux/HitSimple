#include "SemaTestSupport.h"

#include "hitsimple/hir/HIR.h"

#include <algorithm>
#include <string>
#include <vector>

using hitsimple::testing::sema::analyzeSource;

namespace {
void flatten(const hitsimple::hir::Stmt &statement,
             std::vector<const hitsimple::hir::Stmt *> &statements) {
  if (const auto *list =
          dynamic_cast<const hitsimple::hir::StatementList *>(&statement)) {
    for (const auto &child : list->statements) {
      flatten(*child, statements);
    }
  } else {
    statements.push_back(&statement);
  }
}
} // namespace

HS_TEST(Sema_MultiReturnUsesOrdinaryDestinationContracts) {
  using namespace hitsimple::hir;
  auto result = analyzeSource(
      "func pair() -> (first as f64, second as f64) { return 0.0, 2.0; }\n"
      "func main() -> i32 {\n"
      "  new flag[8]\n  new number as f64\n"
      "  (flag %b=), (number %f=) = pair()\n  return 0\n}\n");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(verifyHIR(*result.unit).empty());
  std::vector<const Stmt *> statements;
  for (const auto &statement : result.unit->functions.back()->body->statements) {
    flatten(*statement, statements);
  }
  const MultiReturnCallStore *capture = nullptr;
  const BoolStore *boolean = nullptr;
  const FloatStore *floating = nullptr;
  std::size_t captures = 0;
  for (const auto *statement : statements) {
    if (const auto *call = dynamic_cast<const MultiReturnCallStore *>(statement)) {
      capture = call;
      ++captures;
      HS_EXPECT_TRUE(boolean == nullptr && floating == nullptr);
    } else if (const auto *store = dynamic_cast<const BoolStore *>(statement)) {
      boolean = store;
      HS_EXPECT_TRUE(capture != nullptr);
    } else if (const auto *store = dynamic_cast<const FloatStore *>(statement)) {
      floating = store;
      HS_EXPECT_TRUE(capture != nullptr);
    }
  }
  HS_EXPECT_EQ(captures, 1U);
  HS_EXPECT_TRUE(capture != nullptr && boolean != nullptr && floating != nullptr);
  HS_EXPECT_EQ(capture->targets.size(), 2U);
  HS_EXPECT_EQ(capture->targets[0].returnIndex, 0U);
  HS_EXPECT_EQ(capture->targets[1].returnIndex, 1U);
  HS_EXPECT_TRUE(capture->targets[0].bindingName != boolean->bindingName);
  HS_EXPECT_TRUE(capture->targets[1].bindingName != floating->bindingName);
  const auto *test = dynamic_cast<const BooleanTestExpr *>(boolean->value.get());
  HS_EXPECT_TRUE(test != nullptr);
  for (const auto *value : {test->operand.get(), floating->value.get()}) {
    HS_EXPECT_EQ(value->result.templateName, "f64");
    HS_EXPECT_EQ(value->result.staticByteLength, 8U);
    HS_EXPECT_TRUE(!value->result.isAddressable);
    HS_EXPECT_TRUE(!value->result.isMutableLValue);
  }
}

HS_TEST(Sema_MultiReturnUsesUserAssignmentAndWidths) {
  using namespace hitsimple::hir;
  auto result = analyzeSource(
      "template Box { value[4] as i32; }\n"
      "impl Box { op = (dst as Box, src as Box) -> Box {\n"
      "  dst.value = src.value + 1\n  return dst\n} }\n"
      "func pair() -> (first as Box, second as u8) {\n"
      "  new box as Box\n  box.value = 4\n  new byte as u8 = 255\n  return box, byte\n}\n"
      "func main() -> i32 {\n  new box as Box\n  new wide as u64\n"
      "  box, wide = pair()\n  return 0\n}\n");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(verifyHIR(*result.unit).empty());
  std::vector<const Stmt *> statements;
  for (const auto &function : result.unit->functions) {
    if (function->name == "main") {
      for (const auto &statement : function->body->statements) {
        flatten(*statement, statements);
      }
    }
  }
  bool sawAssignment = false;
  bool sawWidth = false;
  for (const auto *statement : statements) {
    if (const auto *call = dynamic_cast<const UserTemplateOpCall *>(statement)) {
      sawAssignment = true;
      HS_EXPECT_EQ(call->arguments.size(), 2U);
      HS_EXPECT_TRUE(call->arguments[0]->result.isMutableLValue);
      HS_EXPECT_TRUE(!call->arguments[1]->result.isMutableLValue);
      HS_EXPECT_TRUE(!call->arguments[1]->result.isAddressable);
      HS_EXPECT_EQ(call->arguments[1]->result.templateName, "Box");
    } else if (const auto *store = dynamic_cast<const IntegerStore *>(statement);
               store != nullptr && store->target == "wide") {
      const auto *cast = dynamic_cast<const IntegerCastExpr *>(store->value.get());
      HS_EXPECT_TRUE(cast != nullptr);
      HS_EXPECT_EQ(cast->operand->result.templateName, "u8");
      HS_EXPECT_EQ(store->targetByteLength, 8U);
      sawWidth = true;
    }
  }
  HS_EXPECT_TRUE(sawAssignment && sawWidth);
}

HS_TEST(Sema_MultiReturnRejectsInvalidDestinationContracts) {
  const std::string floating =
      "func pair() -> (first as f64, second as f64) { return 1.0, 2.0; }\n";
  for (const auto &[source, expected] :
       std::vector<std::pair<std::string, std::string>>{
           {floating + "func main() { new a as u64; new b as u64; "
                       "a, b = pair(); return 0; }", "not an integer expression"},
           {floating + "func main() { new a[8]; new b[8]; "
                       "a, b = pair(); return 0; }", "not an integer expression"},
           {floating + "func main() { new a[4] as bytes; new b as f64; "
                       "a, b = pair(); return 0; }", "equal-length source View"},
           {floating + "func main() { new a as f64; "
                       "a, _, _ = pair(); return 0; }", "result count"},
           {floating + "func main() { new a as addr; new b as f64; "
                       "(a &=), b = pair(); return 0; }", "requires an integer or address View"},
           {"template A { value[4] as i32; }\n"
            "template B { value[4] as i32; }\n"
            "func pair() -> (first as A, second as A) {\n"
            "  new a as A; return a, a; }\n"
            "func main() { new b as B; b, _ = pair(); return 0; }",
            "matching templates"}}) {
    auto result = analyzeSource(source);
    HS_EXPECT_TRUE(result.unit == nullptr);
    HS_EXPECT_TRUE(std::any_of(
        result.diagnostics.begin(), result.diagnostics.end(),
        [&](const auto &diagnostic) {
          return diagnostic.message.find(expected) != std::string::npos;
        }));
  }
}

HS_TEST(Sema_MultiReturnAndOrdinaryAssignmentsAcceptOpaqueViews) {
  for (const auto *assignment : {
           "a, (b as bytes) = pair(value)",
           "a = value; (b as bytes) = value",
           "(a %b=), (b as bytes) = pair(value)",
           "a %b= value; (b as bytes) = value"}) {
    auto result = analyzeSource(
        "func pair(value as handle) -> (first as handle, second as handle) {\n"
        "  return value, value\n}\n"
        "func probe(value as handle) -> i32 {\n"
        "  new a as bool\n  new b as addr\n  " +
        std::string(assignment) + "\n  return 0\n}\n"
        "func main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.diagnostics.empty());
    HS_EXPECT_TRUE(result.unit != nullptr);
    HS_EXPECT_TRUE(hitsimple::hir::verifyHIR(*result.unit).empty());
  }
}

HS_TEST(Sema_MultiReturnRebindingUsesCapturedAddresses) {
  auto result = analyzeSource(
      "func pair(p as addr, q as addr) -> (addr, addr) { return p, q; } "
      "func main() { new a[3]; new b[4]; new x[3]; new y[4]; "
      "(a &=), (b &=) = pair(&x, &y); return 0; }");
  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto dump = hitsimple::hir::dumpToString(*result.unit);
  const auto call = dump.find("MultiReturnCallStore callee=pair");
  const auto first = dump.find("BindingRebind target=a");
  const auto second = dump.find("BindingRebind target=b");
  HS_EXPECT_TRUE(call != std::string::npos);
  HS_EXPECT_TRUE(call < first && first < second);
}
