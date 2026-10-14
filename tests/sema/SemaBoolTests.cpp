#include "SemaTestSupport.h"

#include "hitsimple/hir/HIR.h"

#include <string>

using hitsimple::testing::sema::analyzeSource;

HS_TEST(Sema_RejectsOrdinaryBoolArithmeticBitwiseAndShiftOperations) {
  for (const std::string operation :
       {"+", "-", "*", "/", "%", "**", "&", "|", "^", "<<", ">>"}) {
    const auto result = analyzeSource(
        "func main() -> i32 {\n"
        "    new left as bool = true\n"
        "    new right as bool = false\n"
        "    new value = left " +
        operation + " right\n"
                    "    return 0\n"
                    "}\n");

    HS_EXPECT_TRUE(result.unit == nullptr);
    HS_EXPECT_EQ(result.diagnostics.size(), 1U);
    const std::string expectedDiagnostic =
        operation == "%"
            ? "typed integer operation requires integer operands"
            : "bool only supports == and != ordinary operations";
    HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                       expectedDiagnostic) !=
                   std::string::npos);
  }
}

HS_TEST(Sema_RejectsBoolUnaryMinusAndBitwiseNot) {
  for (const std::string operation : {"-", "~"}) {
    const auto result = analyzeSource(
        "func main() -> i32 {\n"
        "    new flag as bool = true\n"
        "    new value as bool = " +
        operation + "flag\n"
                    "    return 0\n"
                    "}\n");

    HS_EXPECT_TRUE(result.unit == nullptr);
    HS_EXPECT_EQ(result.diagnostics.size(), 1U);
    HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                       "operand of '" + operation +
                       "' is not an integer View") != std::string::npos);
  }
}

HS_TEST(Sema_RecordsBooleanValueReadBoundaries) {
  auto result = analyzeSource(
      "func equal(a as bool, b as bool) -> bool { return a == b; }\n"
      "func integral(value as bool) -> i64 {\n"
      "  new converted as i64 = to_i64(value)\n"
      "  return converted\n"
      "}\n"
      "func floating(value as bool) -> f64 { return to_f64(value); }\n"
      "func bytes_of(value as bool) -> u8 { return value as u8; }\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(hitsimple::hir::verifyHIR(*result.unit).empty());

  const auto &functions = result.unit->functions;
  const auto *ret = dynamic_cast<const hitsimple::hir::Return *>(
      functions[0]->body->statements.back().get());
  HS_EXPECT_TRUE(ret != nullptr);
  HS_EXPECT_EQ(ret->conversionPlans[0].kind,
               hitsimple::hir::ConversionKind::BooleanNormalize);
  const auto *returned = dynamic_cast<const hitsimple::hir::BooleanTestExpr *>(
      ret->values[0].get());
  HS_EXPECT_TRUE(returned != nullptr);
  const auto *comparison = dynamic_cast<const hitsimple::hir::BinaryExpr *>(
      returned->operand.get());
  HS_EXPECT_TRUE(comparison != nullptr);
  HS_EXPECT_EQ(comparison->operationKind,
               hitsimple::hir::StandardOperationKind::StandardBoolean);
  HS_EXPECT_TRUE(dynamic_cast<const hitsimple::hir::BooleanTestExpr *>(
                     comparison->left.get()) != nullptr);
  HS_EXPECT_TRUE(dynamic_cast<const hitsimple::hir::BooleanTestExpr *>(
                     comparison->right.get()) != nullptr);

  const auto *store = dynamic_cast<const hitsimple::hir::IntegerStore *>(
      functions[1]->body->statements[1].get());
  HS_EXPECT_TRUE(store != nullptr);
  const auto *integer = dynamic_cast<const hitsimple::hir::IntegerCastExpr *>(
      store->value.get());
  HS_EXPECT_TRUE(integer != nullptr);
  HS_EXPECT_TRUE(dynamic_cast<const hitsimple::hir::BooleanTestExpr *>(
                     integer->operand.get()) != nullptr);
  ret = dynamic_cast<const hitsimple::hir::Return *>(
      functions[2]->body->statements.back().get());
  const auto *floating = dynamic_cast<const hitsimple::hir::ToFloatExpr *>(
      ret->values[0].get());
  HS_EXPECT_TRUE(floating != nullptr);
  HS_EXPECT_TRUE(dynamic_cast<const hitsimple::hir::BooleanTestExpr *>(
                     floating->operand.get()) != nullptr);
  ret = dynamic_cast<const hitsimple::hir::Return *>(
      functions[3]->body->statements.back().get());
  HS_EXPECT_TRUE(dynamic_cast<const hitsimple::hir::TemplateViewExpr *>(
                     ret->values[0].get()) != nullptr);
}

HS_TEST(Sema_BooleanReturnPlansPreserveAnyViewSourceSemantics) {
  for (const std::string parameter :
       {"value as bool", "value as u64", "value as f64", "value as addr",
        "value as handle", "value[3] as bytes"}) {
    auto result = analyzeSource("func probe(" + parameter +
                                ") -> bool { return value; }\n"
                                "func main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.unit != nullptr);
    HS_EXPECT_TRUE(result.diagnostics.empty());
    auto *ret = dynamic_cast<hitsimple::hir::Return *>(
        result.unit->functions[0]->body->statements.back().get());
    HS_EXPECT_TRUE(ret != nullptr);
    HS_EXPECT_EQ(ret->conversionPlans.size(), 1U);
    auto &plan = ret->conversionPlans[0];
    HS_EXPECT_EQ(plan.kind, hitsimple::hir::ConversionKind::BooleanNormalize);
    const auto *test = dynamic_cast<const hitsimple::hir::BooleanTestExpr *>(
        ret->values[0].get());
    HS_EXPECT_TRUE(test != nullptr);
    HS_EXPECT_EQ(plan.source.templateName, test->operand->result.templateName);
    HS_EXPECT_EQ(plan.source.staticByteLength,
                 test->operand->result.staticByteLength);
    HS_EXPECT_EQ(plan.destination.templateName, std::string("bool"));
    HS_EXPECT_TRUE(hitsimple::hir::verifyHIR(*result.unit).empty());
    const auto destination = plan.destination;
    plan.destination = hitsimple::hir::viewSemanticsForTemplate("u8", 1);
    HS_EXPECT_TRUE(!hitsimple::hir::verifyHIR(*result.unit).empty());
    plan.destination = destination;
    plan.source.staticByteLength += 1U;
    HS_EXPECT_TRUE(!hitsimple::hir::verifyHIR(*result.unit).empty());
  }
}

HS_TEST(Sema_PreservesInferredAndExpressionlessReturnScope) {
  const auto inferred = analyzeSource(
      "func inferred(value as bool) {\n"
      "  if (true) { return value; }\n"
      "  return value\n"
      "}\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(inferred.unit != nullptr);
  HS_EXPECT_TRUE(inferred.diagnostics.empty());
  const auto *branch = dynamic_cast<const hitsimple::hir::If *>(
      inferred.unit->functions[0]->body->statements.front().get());
  HS_EXPECT_TRUE(branch != nullptr);
  const auto *first = dynamic_cast<const hitsimple::hir::Return *>(
      branch->thenBlock->statements.back().get());
  const auto *second = dynamic_cast<const hitsimple::hir::Return *>(
      inferred.unit->functions[0]->body->statements.back().get());
  HS_EXPECT_TRUE(first != nullptr && second != nullptr);
  HS_EXPECT_TRUE(dynamic_cast<const hitsimple::hir::VariableRef *>(
                     first->values[0].get()) != nullptr);
  HS_EXPECT_TRUE(dynamic_cast<const hitsimple::hir::VariableRef *>(
                     second->values[0].get()) != nullptr);

  const auto named = analyzeSource(
      "func named() -> output as bool { return; }\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(named.unit == nullptr);
  HS_EXPECT_TRUE(!named.diagnostics.empty());
  HS_EXPECT_TRUE(named.diagnostics[0].message.find(
                     "return value count does not match function signature") !=
                 std::string::npos);
}
