#include "support/TestRunner.h"

#include "hitsimple/hir/HIR.h"

#include <memory>
#include <string>
#include <vector>

namespace {

std::unique_ptr<hitsimple::hir::TranslationUnit>
unitWithReturn(std::unique_ptr<hitsimple::hir::Expr> expression) {
  using namespace hitsimple::hir;
  std::vector<std::unique_ptr<Stmt>> statements;
  std::vector<std::unique_ptr<Expr>> values;
  values.push_back(std::move(expression));
  statements.push_back(std::make_unique<Return>(std::move(values)));

  std::vector<std::unique_ptr<Function>> functions;
  functions.push_back(std::make_unique<Function>(
      "main", std::make_unique<Block>(std::move(statements))));
  return std::make_unique<TranslationUnit>(std::move(functions));
}

} // namespace

HS_TEST(Hir_ViewSemanticsDumpIncludesCompleteResult) {
  using namespace hitsimple::hir;
  auto expression = std::make_unique<BooleanTestExpr>(
      std::make_unique<IntegerLiteral>("1",
                                       viewSemanticsForTemplate("u64", 8)),
      booleanTestResultSemantics());
  const auto unit = unitWithReturn(std::move(expression));

  HS_EXPECT_TRUE(verifyHIR(*unit).empty());
  const auto dump = dumpToString(*unit);
  HS_EXPECT_TRUE(dump.find("BooleanTestExpr category=boolean template=bool "
                           "length=static:1 integer_interpretation=none "
                           "lvalue=false addressable=false") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("category=unsigned-integer template=u64 "
                           "length=static:8 integer_interpretation=unsigned") !=
                 std::string::npos);
}

HS_TEST(Hir_ViewSemanticsVerifierRejectsLegacyByteLengthMismatch) {
  using namespace hitsimple::hir;
  auto expression =
      std::make_unique<IntegerLiteral>("42",
                                       viewSemanticsForTemplate("i32", 4));
  expression->byteLength = 8;
  const auto unit = unitWithReturn(std::move(expression));

  const auto diagnostics = verifyHIR(*unit);
  HS_EXPECT_EQ(diagnostics.size(), 1U);
  HS_EXPECT_TRUE(diagnostics.front().message.find("legacy byteLength") !=
                 std::string::npos);
}

HS_TEST(Hir_ViewSemanticsVerifierRejectsNonCanonicalBooleanTestResult) {
  using namespace hitsimple::hir;
  auto expression = std::make_unique<BooleanTestExpr>(
      std::make_unique<IntegerLiteral>(
          "1", staticViewSemantics(ViewCategory::UntemplatedInteger,
                                    IntegerInterpretation::Signed, 1)),
      viewSemanticsForTemplate("bool", 1, true, true));
  const auto unit = unitWithReturn(std::move(expression));

  const auto diagnostics = verifyHIR(*unit);
  HS_EXPECT_EQ(diagnostics.size(), 1U);
  HS_EXPECT_TRUE(diagnostics.front().message.find("BooleanTestExpr result") !=
                 std::string::npos);
}

HS_TEST(Hir_AddressFactsDescribeObjectAndPointerDerivedAddresses) {
  using namespace hitsimple::hir;
  const auto addressSemantics = viewSemanticsForTemplate("addr", 8);
  auto localAddress = std::make_unique<AddressOfExpr>(
      "buffer", "buffer", 32, MemoryStorage::Local, 0, addressSemantics);
  HS_EXPECT_EQ(localAddress->facts.origin, AddressOrigin::LocalObject);
  HS_EXPECT_TRUE(localAddress->facts.knownExtent.has_value());
  HS_EXPECT_EQ(*localAddress->facts.knownExtent, 32U);
  HS_EXPECT_TRUE(localAddress->facts.isBaseAddress);

  auto offset = std::make_unique<BinaryExpr>(
      std::move(localAddress), "+",
      std::make_unique<IntegerLiteral>("4", viewSemanticsForTemplate("u64", 8)),
      addressSemantics, StandardOperationKind::AddressOffset);
  HS_EXPECT_TRUE(offset->addressFacts.has_value());
  HS_EXPECT_EQ(offset->addressFacts->origin, AddressOrigin::PointerDerived);
  HS_EXPECT_TRUE(!offset->addressFacts->isBaseAddress);
  HS_EXPECT_TRUE(offset->addressFacts->knownExtent.has_value());
  HS_EXPECT_EQ(*offset->addressFacts->knownExtent, 32U);
}

HS_TEST(Hir_AddressFactsRetainConstantAllocationExtent) {
  using namespace hitsimple::hir;
  std::vector<std::unique_ptr<Expr>> arguments;
  arguments.push_back(std::make_unique<IntegerLiteral>(
      "6", viewSemanticsForTemplate("u64", 8)));
  auto allocation = std::make_unique<CallExpr>(
      "alloc", std::move(arguments), false, hitsimple::stdlib::BuiltinId::Alloc,
      std::vector<FormatArgKind>{}, 0, "addr",
      viewSemanticsForTemplate("addr", 8));

  HS_EXPECT_TRUE(allocation->addressFacts.has_value());
  HS_EXPECT_TRUE(allocation->addressFacts->knownExtent.has_value());
  HS_EXPECT_EQ(*allocation->addressFacts->knownExtent, 6U);
  HS_EXPECT_TRUE(allocation->addressFacts->isBaseAddress);
}

HS_TEST(Hir_ViewSemanticsVerifierRejectsIncompleteCallAndAddressPlans) {
  using namespace hitsimple::hir;
  std::vector<std::unique_ptr<Expr>> arguments;
  arguments.push_back(std::make_unique<IntegerLiteral>(
      "1", viewSemanticsForTemplate("i32", 4)));
  auto incompleteCall = std::make_unique<CallExpr>(
      "identity", std::move(arguments), false, hitsimple::stdlib::BuiltinId::None,
      std::vector<FormatArgKind>{}, 0, "i32",
      viewSemanticsForTemplate("i32", 4));
  incompleteCall->argumentPlans.clear();
  const auto callUnit = unitWithReturn(std::move(incompleteCall));
  const auto callDiagnostics = verifyHIR(*callUnit);
  HS_EXPECT_EQ(callDiagnostics.size(), 1U);
  HS_EXPECT_TRUE(callDiagnostics.front().message.find("argument conversion plan") !=
                 std::string::npos);

  const auto addressSemantics = viewSemanticsForTemplate("addr", 8);
  auto offset = std::make_unique<BinaryExpr>(
      std::make_unique<AddressOfExpr>("buffer", "buffer", 4,
                                      MemoryStorage::Local, 0, addressSemantics),
      "+", std::make_unique<IntegerLiteral>(
               "1", viewSemanticsForTemplate("u64", 8)),
      addressSemantics, StandardOperationKind::AddressOffset);
  offset->addressFacts->isBaseAddress = true;
  const auto addressUnit = unitWithReturn(std::move(offset));
  const auto addressDiagnostics = verifyHIR(*addressUnit);
  HS_EXPECT_EQ(addressDiagnostics.size(), 1U);
  HS_EXPECT_TRUE(addressDiagnostics.front().message.find("AddressOffset") !=
                 std::string::npos);
}

HS_TEST(Hir_ViewSemanticsVerifierRejectsCallPlanDestinationThatDiffersFromCallee) {
  using namespace hitsimple::hir;
  const auto i32 = viewSemanticsForTemplate("i32", 4);
  const auto i64 = viewSemanticsForTemplate("i64", 8);

  std::vector<std::unique_ptr<Expr>> arguments;
  arguments.push_back(std::make_unique<IntegerLiteral>("1", i32));
  auto call = std::make_unique<CallExpr>(
      "identity", std::move(arguments), false, hitsimple::stdlib::BuiltinId::None,
      std::vector<FormatArgKind>{}, 0, "i32", i32);
  call->argumentPlans[0] =
      ConversionPlan{ConversionKind::IntegerWidth, call->arguments[0]->result,
                     i64};

  std::vector<std::unique_ptr<Stmt>> mainStatements;
  std::vector<std::unique_ptr<Expr>> values;
  values.push_back(std::move(call));
  mainStatements.push_back(std::make_unique<Return>(std::move(values)));

  std::vector<std::unique_ptr<Function>> functions;
  std::vector<Parameter> parameters;
  parameters.emplace_back("value", "value.1", i32);
  functions.push_back(std::make_unique<Function>(
      "identity", std::move(parameters), std::vector<std::size_t>{},
      std::make_unique<Block>(std::vector<std::unique_ptr<Stmt>>{})));
  functions.push_back(std::make_unique<Function>(
      "main", std::make_unique<Block>(std::move(mainStatements))));
  const TranslationUnit unit(std::move(functions));

  const auto diagnostics = verifyHIR(unit);
  HS_EXPECT_EQ(diagnostics.size(), 1U);
  HS_EXPECT_TRUE(diagnostics.front().message.find(
                     "destination does not match callee parameter contract") !=
                 std::string::npos);
}

HS_TEST(Hir_VerifierRejectsStandardTemplateWithWrongByteLength) {
  using namespace hitsimple::hir;
  auto expression = std::make_unique<IntegerLiteral>(
      "42", staticViewSemantics(ViewCategory::SignedInteger,
                                  IntegerInterpretation::Signed, 8, "i32"));
  const auto unit = unitWithReturn(std::move(expression));

  const auto diagnostics = verifyHIR(*unit);
  HS_EXPECT_EQ(diagnostics.size(), 1U);
  HS_EXPECT_TRUE(diagnostics.front().message.find("standard template 'i32'") !=
                 std::string::npos);
}

HS_TEST(Hir_VerifierRejectsComparisonWithNonBooleanResult) {
  using namespace hitsimple::hir;
  auto comparison = std::make_unique<BinaryExpr>(
      std::make_unique<IntegerLiteral>("1", viewSemanticsForTemplate("i32", 4)),
      "==",
      std::make_unique<IntegerLiteral>("1", viewSemanticsForTemplate("i32", 4)),
      viewSemanticsForTemplate("i32", 4),
      StandardOperationKind::StandardInteger);
  const auto unit = unitWithReturn(std::move(comparison));

  const auto diagnostics = verifyHIR(*unit);
  HS_EXPECT_EQ(diagnostics.size(), 1U);
  HS_EXPECT_TRUE(diagnostics.front().message.find("comparison and logical") !=
                 std::string::npos);
}

HS_TEST(Hir_VerifierRejectsIntegerCandidateWithBytesOperand) {
  using namespace hitsimple::hir;
  auto operation = std::make_unique<BinaryExpr>(
      std::make_unique<VariableRef>(
          "bytes", staticViewSemantics(ViewCategory::Bytes,
                                        IntegerInterpretation::None, 4, "bytes")),
      "%d+",
      std::make_unique<IntegerLiteral>("1", viewSemanticsForTemplate("i32", 4)),
      staticViewSemantics(ViewCategory::UntemplatedInteger,
                          IntegerInterpretation::Signed, 4),
      StandardOperationKind::UntemplatedInteger);
  const auto unit = unitWithReturn(std::move(operation));

  const auto diagnostics = verifyHIR(*unit);
  HS_EXPECT_EQ(diagnostics.size(), 1U);
  HS_EXPECT_TRUE(diagnostics.front().message.find("UntemplatedInteger") !=
                 std::string::npos);
}
