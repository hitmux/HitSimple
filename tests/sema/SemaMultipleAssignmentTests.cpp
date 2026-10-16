#include "SemaTestSupport.h"

#include "hitsimple/hir/HIR.h"

#include <algorithm>
#include <string>
#include <vector>

using hitsimple::testing::sema::analyzeSource;

namespace {
std::vector<const hitsimple::hir::Stmt *> assignmentStatements(
    const hitsimple::hir::TranslationUnit &unit) {
  const auto main = std::find_if(unit.functions.begin(), unit.functions.end(),
      [](const auto &function) { return function->name == "main"; });
  HS_EXPECT_TRUE(main != unit.functions.end());
  const auto &body = (*main)->body->statements;
  const auto *list = dynamic_cast<const hitsimple::hir::StatementList *>(
      body[body.size() - 2U].get());
  HS_EXPECT_TRUE(list != nullptr);
  std::vector<const hitsimple::hir::Stmt *> result;
  for (const auto &statement : list->statements) {
    result.push_back(statement.get());
  }
  return result;
}
} // namespace

HS_TEST(Sema_MultipleAssignmentCapturesViewsBeforeStores) {
  using namespace hitsimple::hir;
  auto result = analyzeSource(
      "func observe() -> i32 { return 3; }\n"
      "func main() -> i32 { new a as i32; new b as i32; new c as i32;\n"
      " a, b, c = 9, observe(), a\n return 0\n}\n");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(verifyHIR(*result.unit).empty());
  const auto statements = assignmentStatements(*result.unit);
  HS_EXPECT_EQ(statements.size(), 6U);
  for (std::size_t index = 0; index < 3; ++index) {
    const auto *capture = dynamic_cast<const CaptureView *>(statements[index]);
    const auto *store = dynamic_cast<const IntegerStore *>(statements[index + 3]);
    HS_EXPECT_TRUE(capture != nullptr && store != nullptr);
    const Expr *storedSource = store->value.get();
    if (const auto *cast = dynamic_cast<const IntegerCastExpr *>(storedSource)) {
      storedSource = cast->operand.get();
    }
    const auto *reference = dynamic_cast<const CapturedViewRef *>(storedSource);
    HS_EXPECT_TRUE(reference != nullptr);
    HS_EXPECT_EQ(reference->captureName, capture->captureName);
    HS_EXPECT_EQ(reference->result.staticByteLength,
                 capture->source->result.staticByteLength);
    HS_EXPECT_EQ(reference->result.isAddressable, index == 2U);
    HS_EXPECT_EQ(reference->result.isMutableLValue, index == 2U);
    if (index > 0) {
      HS_EXPECT_EQ(reference->result.templateName, "i32");
    }
  }
  const auto *call = dynamic_cast<const CaptureView *>(statements[1]);
  HS_EXPECT_TRUE(dynamic_cast<const CallExpr *>(call->source.get()) != nullptr);
}

HS_TEST(Sema_MultipleAssignmentRetainsDiscardEffectsAndDynamicViews) {
  using namespace hitsimple::hir;
  for (const auto *assignment : {"_ = effect()", "_, _ = effect(), effect()"}) {
    auto result = analyzeSource(
        "func effect() -> i32 { return 2; }\n"
        "func main() -> i32 { " + std::string(assignment) + "; return 0; }\n");
    HS_EXPECT_TRUE(result.unit != nullptr && result.diagnostics.empty());
    HS_EXPECT_TRUE(verifyHIR(*result.unit).empty());
    const auto &statement = result.unit->functions.back()->body->statements[0];
    if (std::string(assignment).starts_with("_,")) {
      const auto *list = dynamic_cast<const StatementList *>(statement.get());
      HS_EXPECT_TRUE(list != nullptr);
      HS_EXPECT_EQ(list->statements.size(), 2U);
      for (const auto &item : list->statements) {
        const auto *capture = dynamic_cast<const CaptureView *>(item.get());
        HS_EXPECT_TRUE(capture != nullptr);
        HS_EXPECT_TRUE(dynamic_cast<const CallExpr *>(capture->source.get()) != nullptr);
      }
    } else {
      const auto *capture = dynamic_cast<const CaptureView *>(statement.get());
      HS_EXPECT_TRUE(capture != nullptr);
      HS_EXPECT_TRUE(dynamic_cast<const CallExpr *>(capture->source.get()) != nullptr);
    }
  }
  auto dynamic = analyzeSource(
      "func main() -> i32 { new flag as bool; new n as i32 = 4; new bytes[4];\n"
      " n, flag = 1, resize_bytes(bytes, n)\n return 0\n}\n");
  HS_EXPECT_TRUE(dynamic.unit != nullptr && dynamic.diagnostics.empty());
  HS_EXPECT_TRUE(verifyHIR(*dynamic.unit).empty());
  const auto statements = assignmentStatements(*dynamic.unit);
  const auto *source = dynamic_cast<const CaptureView *>(statements[1]);
  const auto *store = dynamic_cast<const BoolStore *>(statements[3]);
  HS_EXPECT_TRUE(source != nullptr && store != nullptr);
  const auto *test = dynamic_cast<const BooleanTestExpr *>(store->value.get());
  HS_EXPECT_TRUE(test != nullptr);
  HS_EXPECT_EQ(test->operand->result.lengthKind, ViewLengthKind::Dynamic);
  HS_EXPECT_TRUE(!test->operand->result.isAddressable);
}

HS_TEST(Sema_MultipleAssignmentPreservesUserSourceAndDestinationContracts) {
  using namespace hitsimple::hir;
  auto result = analyzeSource(
      "template Box { value[4] as i32; }\n"
      "impl Box { op = (dst as Box, src as Box) -> Box {\n"
      " dst.value = src.value; return dst; } }\n"
      "func main() -> i32 { new a as Box; new b as Box;\n"
      " a, b = b, a\n return 0\n}\n");
  HS_EXPECT_TRUE(result.unit != nullptr && result.diagnostics.empty());
  HS_EXPECT_TRUE(verifyHIR(*result.unit).empty());
  const auto statements = assignmentStatements(*result.unit);
  HS_EXPECT_EQ(statements.size(), 4U);
  for (std::size_t index = 2; index < 4; ++index) {
    const auto *store = dynamic_cast<const UserTemplateOpCall *>(statements[index]);
    HS_EXPECT_TRUE(store != nullptr);
    HS_EXPECT_TRUE(store->arguments[0]->result.isMutableLValue);
    const auto *source = dynamic_cast<const CapturedViewRef *>(store->arguments[1].get());
    HS_EXPECT_TRUE(source != nullptr);
    HS_EXPECT_TRUE(source->result.isMutableLValue);
    HS_EXPECT_EQ(source->result.templateName, "Box");
  }
}

HS_TEST(Sema_MultipleAssignmentRejectsInvalidSourcesAndTargets) {
  for (const auto &[source, expected] :
       std::vector<std::pair<std::string, std::string>>{
           {"new a as i32; a, _ = 1, missing()", "unsupported function call"},
           {"new a as i32; new f as f32; a, _ = f, 1", "not an integer expression"},
           {"new a[2] as bytes; new b[4] as bytes; a, _ = b, 0", "equal-length source"},
           {"new a as i32; new b as i32; a, b = 1", "target count"},
           {"new a as i32; a, _ = 1, _", "undeclared variable"},
           {"new a as i32; 2, a = 1, 2", "complex assignment targets"}}) {
    auto result = analyzeSource("func main() -> i32 { " + source + "; return 0; }");
    HS_EXPECT_TRUE(result.unit == nullptr);
    HS_EXPECT_TRUE(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [&](const auto &diagnostic) {
          return diagnostic.message.find(expected) != std::string::npos;
        }));
  }
}

HS_TEST(Sema_MultipleAssignmentLiteralContextsRetainViewAcceptance) {
  for (const auto *source : {
           "new flag as bool; flag, _ = 0.0, 1",
           "new bytes[8] as bytes; bytes, _ = 2.5, 1",
           "new low as f16; new high as f128; low, high = 2.5, 5.5",
           "new a as u64; new b as i64; a, b = 18446744073709551615, -9223372036854775808"}) {
    auto result = analyzeSource("func main() -> i32 { " + std::string(source) +
                                   "; return 0; }");
    HS_EXPECT_TRUE(result.unit != nullptr && result.diagnostics.empty());
    HS_EXPECT_TRUE(hitsimple::hir::verifyHIR(*result.unit).empty());
  }
  auto rejected = analyzeSource(
      "func main() -> i32 { new bytes[4] as bytes; bytes, _ = 2.5, 0; return 0; }");
  HS_EXPECT_TRUE(rejected.unit == nullptr);
  HS_EXPECT_TRUE(rejected.diagnostics[0].message.find("equal-length source View") !=
                 std::string::npos);
}
