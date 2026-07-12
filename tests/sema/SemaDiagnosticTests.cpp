#include "SemaTestSupport.h"

#include "hitsimple/hir/HIR.h"

#include <string>

using hitsimple::testing::sema::analyzeSource;
using hitsimple::testing::sema::minimalProgram;

HS_TEST(Sema_RejectsUnsupportedCompoundAssignmentWidth) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[1]\n"
                              "    x %100d+= 2\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find(
                     "unsupported assignment operator") != std::string::npos);
}

HS_TEST(Sema_RejectsCompoundAssignmentDivisionByZero) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[1]\n"
                              "    x %d/= 0\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("division by zero") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsUnsupportedBinaryIntegerWidth) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[1]\n"
                              "    x %d= 1 %100d+ 2\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("unsupported binary operator") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsDuplicateDeclaration) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[1]\n"
                              "    new x[1]\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("duplicate declaration 'x'") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsUndeclaredAssignmentTarget) {
  auto result = analyzeSource("func main() {\n"
                              "    x %d= 42\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("undeclared variable 'x'") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsUndeclaredCallArgument) {
  auto result = analyzeSource("func main() {\n"
                              "    printf(\"%d\\n\", x)\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("undeclared variable 'x'") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsIntegerThatDoesNotFitTarget) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[1]\n"
                              "    x %d= 128\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("does not fit") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsIntegerLiteralAboveSignedEightByteRange) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[8]\n"
                              "    x = 9223372036854775808\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("out of range") !=
                 std::string::npos);
}

HS_TEST(Sema_AllowsU64MaxAndI64MinTargetedLiterals) {
  auto result = analyzeSource("func main() {\n"
                              "    new max as u64 = 18446744073709551615\n"
                              "    new min as i64 = -9223372036854775808\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
}

HS_TEST(Sema_RejectsReservedIdentifierDeclarations) {
  auto underscore = analyzeSource("func main() {\n"
                                  "    new _ as i32 = 1\n"
                                  "    return 0\n"
                                  "}\n");
  HS_EXPECT_TRUE(underscore.unit == nullptr);

  auto numberedTemplate = analyzeSource("func main() {\n"
                                        "    new t10 as i32 = 1\n"
                                        "    return 0\n"
                                        "}\n");
  HS_EXPECT_TRUE(numberedTemplate.unit == nullptr);
}

HS_TEST(Sema_RejectsMultiByteCharLiteralIntoNarrowInteger) {
  auto result = analyzeSource("func main() {\n"
                              "    new x as u8 = 'AB'\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("character literal byte length") !=
                 std::string::npos);
}

HS_TEST(Sema_AllowsBlockShadowingWithDistinctBindings) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[1]\n"
                              "    if (x) {\n"
                              "        new x[2]\n"
                              "        x %d= 1\n"
                              "    }\n"
                              "    x %d= 2\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=x binding=x bytes=1") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=x binding=x.1 bytes=2") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerStore target=x binding=x.1 bytes=2") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerStore target=x binding=x bytes=1") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsUseAfterBlockScope) {
  auto result = analyzeSource("func main() {\n"
                              "    if (1) {\n"
                              "        new x[1]\n"
                              "    }\n"
                              "    x %d= 1\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("undeclared variable 'x'") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersStaticLocalMemory) {
  auto result = analyzeSource("func main() {\n"
                              "    static counter[4]\n"
                              "    counter %d= 1\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=counter binding=counter bytes=4 "
                           "storage=static") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerStore target=counter binding=counter "
                           "bytes=4 storage=static") != std::string::npos);
}

HS_TEST(Sema_RejectsDuplicateTopLevelFunction) {
  auto result = analyzeSource("func helper() {\n"
                              "    return 0\n"
                              "}\n"
                              "func helper() {\n"
                              "    return 1\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(
      result.diagnostics[0].find("duplicate top-level declaration 'helper'") !=
      std::string::npos);
}

HS_TEST(Sema_RejectsDuplicateTopLevelGlobal) {
  auto result = analyzeSource("new shared[4]\n"
                              "new shared[8]\n"
                              "func main() {\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(
      result.diagnostics[0].find("duplicate top-level declaration 'shared'") !=
      std::string::npos);
}

HS_TEST(Sema_LowersTopLevelInitializersIntoOrderedGlobalInitBlock) {
  auto result = analyzeSource("new first[4] = second\n"
                              "new second[4] = 42\n"
                              "new value as f64 = 1.0\n"
                              "new label[8] as cstr = \"Kai\"\n"
                              "new from_function[4] = make_value()\n"
                              "func make_value() -> [4] {\n"
                              "    return 7\n"
                              "}\n"
                              "func main() {\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.unit->globalInit != nullptr);
  HS_EXPECT_EQ(result.unit->globalInit->statements.size(), 5U);

  const auto *first = dynamic_cast<const hitsimple::hir::IntegerStore *>(
      result.unit->globalInit->statements[0].get());
  HS_EXPECT_TRUE(first != nullptr);
  HS_EXPECT_EQ(first->storage, hitsimple::hir::MemoryStorage::Global);
  const auto *firstValue =
      dynamic_cast<const hitsimple::hir::VariableRef *>(first->value.get());
  HS_EXPECT_TRUE(firstValue != nullptr);
  HS_EXPECT_EQ(firstValue->bindingName, "second");

  const auto *second = dynamic_cast<const hitsimple::hir::IntegerStore *>(
      result.unit->globalInit->statements[1].get());
  HS_EXPECT_TRUE(second != nullptr);
  HS_EXPECT_EQ(second->storage, hitsimple::hir::MemoryStorage::Global);

  const auto *floatStore = dynamic_cast<const hitsimple::hir::FloatStore *>(
      result.unit->globalInit->statements[2].get());
  HS_EXPECT_TRUE(floatStore != nullptr);
  HS_EXPECT_EQ(floatStore->storage, hitsimple::hir::MemoryStorage::Global);

  const auto *stringStore = dynamic_cast<const hitsimple::hir::StringStore *>(
      result.unit->globalInit->statements[3].get());
  HS_EXPECT_TRUE(stringStore != nullptr);
  HS_EXPECT_EQ(stringStore->storage, hitsimple::hir::MemoryStorage::Global);

  const auto *functionStore = dynamic_cast<const hitsimple::hir::IntegerStore *>(
      result.unit->globalInit->statements[4].get());
  HS_EXPECT_TRUE(functionStore != nullptr);
  const auto *functionValue =
      dynamic_cast<const hitsimple::hir::CallExpr *>(functionStore->value.get());
  HS_EXPECT_TRUE(functionValue != nullptr);
  HS_EXPECT_EQ(functionValue->callee, "make_value");

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("GlobalInit") != std::string::npos);
}

HS_TEST(Sema_RejectsTopLevelTemplateGlobalNameCollision) {
  auto result = analyzeSource("template Shared {\n"
                              "    value[4] as i32\n"
                              "}\n"
                              "new Shared[4]\n"
                              "func main() {\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(
      result.diagnostics[0].find("duplicate top-level declaration 'Shared'") !=
      std::string::npos);
}

HS_TEST(Sema_RejectsTopLevelTemplateFunctionNameCollision) {
  auto result = analyzeSource("template Helper {\n"
                              "    value[4] as i32\n"
                              "}\n"
                              "func Helper() {\n"
                              "    return 1\n"
                              "}\n"
                              "func main() {\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(
      result.diagnostics[0].find("duplicate top-level declaration 'Helper'") !=
      std::string::npos);
}

HS_TEST(Sema_RejectsUnknownFunctionCall) {
  auto result = analyzeSource("func main() {\n"
                              "    missing(1)\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("unsupported function call "
                                            "'missing'") != std::string::npos);
}

HS_TEST(Sema_LowersBeta21TemplateAndImplOpSkeleton) {
  auto result =
      analyzeSource("template Vec2 {\n"
                    "    x[8] as f64\n"
                    "    y[8] as f64\n"
                    "}\n"
                    "impl Vec2 {\n"
                    "    op + (self as Vec2, other as Vec2) -> [16] {\n"
                    "        return self\n"
                    "    }\n"
                    "}\n"
                    "func main() {\n"
                    "    return 0\n"
                    "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("ViewTemplate name=Vec2 bytes=16") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("ViewMember name=x offset=0 bytes=8 template=f64") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("ImplOp template=Vec2 op=+") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("Param name=self template=Vec2") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("Return bytes=16") != std::string::npos);
}

HS_TEST(Sema_RejectsInvalidImplOpBindings) {
  auto result =
      analyzeSource("template Vec2 {\n"
                    "    x[8] as f64\n"
                    "    y[8] as f64\n"
                    "}\n"
                    "impl Vec2 {\n"
                    "    op + (lhs as Vec2) -> [16] {\n"
                    "        return lhs\n"
                    "    }\n"
                    "    op * (lhs as Vec2, mut self as Vec2) -> [16] {\n"
                    "        return lhs\n"
                    "    }\n"
                    "}\n"
                    "func main() {\n"
                    "    return 0\n"
                    "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_TRUE(result.diagnostics.size() >= 3U);
  HS_EXPECT_TRUE(result.diagnostics[0].find(
                     "must have exactly two parameters") != std::string::npos);
  HS_EXPECT_TRUE(result.diagnostics[1].find("self") != std::string::npos);
  HS_EXPECT_TRUE(result.diagnostics[2].find("mut self is reserved") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsMutImplParameterMatrix) {
  auto result =
      analyzeSource("template Cell {\n"
                    "    value[4] as i32\n"
                    "}\n"
                    "impl Cell {\n"
                    "    op = (mut self as Cell, value as Cell) -> [4] {\n"
                    "        return value\n"
                    "    }\n"
                    "    op + (self as Cell, mut other as Cell) -> [4] {\n"
                    "        return self\n"
                    "    }\n"
                    "}\n"
                    "func main() {\n"
                    "    return 0\n"
                    "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 2U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("mut self is reserved") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.diagnostics[1].find(
                     "mut impl parameters are reserved") != std::string::npos);
}

HS_TEST(Sema_RejectsInvalidImplMethodBindings) {
  const auto zeroParameter = analyzeSource(
      "template Counter {\n"
      "    value[4] as i32\n"
      "}\n"
      "impl Counter {\n"
      "    func zero() -> as Counter {\n"
      "        return 0\n"
      "    }\n"
      "}\n"
      "func main() {\n"
      "    return 0\n"
      "}\n");
  HS_EXPECT_TRUE(zeroParameter.unit == nullptr);
  HS_EXPECT_EQ(zeroParameter.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(zeroParameter.diagnostics[0].find("at least one parameter") !=
                 std::string::npos);

  const auto wrongReceiver = analyzeSource(
      "template Counter {\n"
      "    value[4] as i32\n"
      "}\n"
      "impl Counter {\n"
      "    func wrong(value as i32) -> as Counter {\n"
      "        return value\n"
      "    }\n"
      "}\n"
      "func main() {\n"
      "    return 0\n"
      "}\n");
  HS_EXPECT_TRUE(wrongReceiver.unit == nullptr);
  HS_EXPECT_EQ(wrongReceiver.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(wrongReceiver.diagnostics[0].find("first parameter") !=
                 std::string::npos);

  const auto multipleReturns = analyzeSource(
      "template Counter {\n"
      "    value[4] as i32\n"
      "}\n"
      "impl Counter {\n"
      "    func split(self as Counter) -> (as Counter, as Counter) {\n"
      "        return self, self\n"
      "    }\n"
      "}\n"
      "func main() {\n"
      "    return 0\n"
      "}\n");
  HS_EXPECT_TRUE(multipleReturns.unit == nullptr);
  HS_EXPECT_EQ(multipleReturns.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(multipleReturns.diagnostics[0].find(
                     "supports at most one return view") != std::string::npos);

  const auto duplicate = analyzeSource(
      "template Counter {\n"
      "    value[4] as i32\n"
      "}\n"
      "impl Counter {\n"
      "    func copy(first as Counter) -> as Counter {\n"
      "        return first\n"
      "    }\n"
      "    func copy(second as Counter) -> as i32 {\n"
      "        return second\n"
      "    }\n"
      "}\n"
      "func main() {\n"
      "    return 0\n"
      "}\n");
  HS_EXPECT_TRUE(duplicate.unit == nullptr);
  HS_EXPECT_EQ(duplicate.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(duplicate.diagnostics[0].find("duplicate impl method binding") !=
                 std::string::npos);

  const auto mutableParameter = analyzeSource(
      "template Counter {\n"
      "    value[4] as i32\n"
      "}\n"
      "impl Counter {\n"
      "    func update(mut self as Counter) -> as Counter {\n"
      "        return self\n"
      "    }\n"
      "    func replace(self as Counter, mut value as i32) -> as Counter {\n"
      "        return self\n"
      "    }\n"
      "}\n"
      "func main() {\n"
      "    return 0\n"
      "}\n");
  HS_EXPECT_TRUE(mutableParameter.unit == nullptr);
  HS_EXPECT_EQ(mutableParameter.diagnostics.size(), 2U);
  HS_EXPECT_TRUE(mutableParameter.diagnostics[0].find("mut self is reserved") !=
                 std::string::npos);
  HS_EXPECT_TRUE(mutableParameter.diagnostics[1].find(
                     "mut impl parameters are reserved") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsImplMethodTemplateParameterLengthMismatch) {
  const auto wrongSelfLength = analyzeSource(
      "template Counter {\n"
      "    value[4] as i32\n"
      "}\n"
      "impl Counter {\n"
      "    func copy(self[8] as Counter) -> as Counter {\n"
      "        return self\n"
      "    }\n"
      "}\n"
      "func main() {\n"
      "    return 0\n"
      "}\n");
  HS_EXPECT_TRUE(wrongSelfLength.unit == nullptr);
  HS_EXPECT_EQ(wrongSelfLength.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(wrongSelfLength.diagnostics[0].find(
                     "byte length does not match template 'Counter'") !=
                 std::string::npos);

  const auto wrongArgumentLength = analyzeSource(
      "template Counter {\n"
      "    value[4] as i32\n"
      "}\n"
      "impl Counter {\n"
      "    func add(self as Counter, other[8] as Counter) -> as Counter {\n"
      "        return self\n"
      "    }\n"
      "}\n"
      "func main() {\n"
      "    return 0\n"
      "}\n");
  HS_EXPECT_TRUE(wrongArgumentLength.unit == nullptr);
  HS_EXPECT_EQ(wrongArgumentLength.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(wrongArgumentLength.diagnostics[0].find(
                     "byte length does not match template 'Counter'") !=
                 std::string::npos);

  const auto wrongStandardTemplateLength = analyzeSource(
      "template Counter {\n"
      "    value[4] as i32\n"
      "}\n"
      "impl Counter {\n"
      "    func add(self as Counter, amount[8] as i32) -> as Counter {\n"
      "        return self\n"
      "    }\n"
      "}\n"
      "func main() {\n"
      "    return 0\n"
      "}\n");
  HS_EXPECT_TRUE(wrongStandardTemplateLength.unit == nullptr);
  HS_EXPECT_EQ(wrongStandardTemplateLength.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(wrongStandardTemplateLength.diagnostics[0].find(
                     "byte length does not match template 'i32'") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsMismatchedUserTemplateReturnViews) {
  const auto ordinaryFunction = analyzeSource(
      "template A {\n"
      "    value[4] as i32\n"
      "}\n"
      "template B {\n"
      "    value[4] as i32\n"
      "}\n"
      "func relabel(value as A) -> as B {\n"
      "    return value\n"
      "}\n"
      "func main() {\n"
      "    return 0\n"
      "}\n");
  HS_EXPECT_TRUE(ordinaryFunction.unit == nullptr);
  HS_EXPECT_EQ(ordinaryFunction.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(ordinaryFunction.diagnostics[0].find(
                     "user template return requires matching templates") !=
                 std::string::npos);

  const auto implMethod = analyzeSource(
      "template A {\n"
      "    value[4] as i32\n"
      "}\n"
      "template B {\n"
      "    value[4] as i32\n"
      "}\n"
      "impl A {\n"
      "    func relabel(self as A) -> as B {\n"
      "        return self\n"
      "    }\n"
      "}\n"
      "func main() {\n"
      "    return 0\n"
      "}\n");
  HS_EXPECT_TRUE(implMethod.unit == nullptr);
  HS_EXPECT_EQ(implMethod.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(implMethod.diagnostics[0].find(
                     "user template return requires matching templates") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsUnknownImplMethodCall) {
  auto result = analyzeSource("template Counter {\n"
                              "    value[4] as i32\n"
                              "}\n"
                              "impl Counter {\n"
                              "    func identity(self as Counter) -> as Counter {\n"
                              "        return self\n"
                              "    }\n"
                              "}\n"
                              "func main() {\n"
                              "    new value as Counter\n"
                              "    value.missing()\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("no matching impl method 'missing'") !=
                 std::string::npos);
}

HS_TEST(Sema_KeepsImplMethodsOutOfTopLevelFunctionLookup) {
  auto result = analyzeSource("template Counter {\n"
                              "    value[4] as i32\n"
                              "}\n"
                              "impl Counter {\n"
                              "    func identity(self as Counter) -> as Counter {\n"
                              "        return self\n"
                              "    }\n"
                              "}\n"
                              "func main() {\n"
                              "    identity()\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("unsupported function call 'identity'") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsSetMemberTemplateLengthMismatch) {
  auto result = analyzeSource("template User {\n"
                              "    id[4] as i32\n"
                              "}\n"
                              "func main() {\n"
                              "    new user as User\n"
                              "    set user.id as i64\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find(
                     "set template byte length does not match target") !=
                 std::string::npos);
}

HS_TEST(Sema_TreatsSetMemberNoneAsAnExplicitOverride) {
  auto result = analyzeSource("template Profile {\n"
                              "    name[4] as cstr\n"
                              "}\n"
                              "template User {\n"
                              "    profile[4] as Profile\n"
                              "}\n"
                              "func main() {\n"
                              "    new user as User\n"
                              "    set user.profile.name as none\n"
                              "    user.profile.name = \"x\"\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find(
                     "right operand of '=' is not an integer expression") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersMemberTemplateOverrideForOperatorLookup) {
  auto result = analyzeSource("template Vec2 {\n"
                              "    x[4] as i32\n"
                              "    y[4] as i32\n"
                              "}\n"
                              "template Pair {\n"
                              "    left[8] as bytes\n"
                              "    right[8] as bytes\n"
                              "}\n"
                              "impl Vec2 {\n"
                              "    op + (lhs as Vec2, rhs as Vec2) -> [8] as Vec2 {\n"
                              "        return lhs\n"
                              "    }\n"
                              "}\n"
                              "func main() {\n"
                              "    new pair as Pair\n"
                              "    set pair.left as Vec2\n"
                              "    set pair.right as Vec2\n"
                              "    return pair.left + pair.right\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("UserTemplateOpCallExpr template=Vec2 bytes=8") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsSetThroughNonTemplateIntermediateMember) {
  auto result = analyzeSource("template User {\n"
                              "    raw[4] as i32\n"
                              "}\n"
                              "func main() {\n"
                              "    new user as User\n"
                              "    set user.raw.value as i32\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find(
                     "must use a user template for nested member access") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersExpressionAsTemporaryTemplateView) {
  auto result = analyzeSource("func main() {\n"
                              "    new bits as u32 = 0x3f800000\n"
                              "    new value as f32 = bits as f32\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("TemplateViewExpr template=f32 bytes=4 "
                           "addressable=true") != std::string::npos);
}

HS_TEST(Sema_RejectsAddressOfRightValueTemplateView) {
  auto result = analyzeSource("func main() {\n"
                              "    new ptr[P] as addr = &(to_i32(1) as i32)\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find(
                     "temporary template view is not addressable") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersNoneExpressionTemplateView) {
  auto result = analyzeSource("func main() {\n"
                              "    new text[4] as cstr\n"
                              "    new raw[4] = text as none\n"
                              "    return raw\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("TemplateViewExpr template=none bytes=4") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsScanfFormatArgumentCountMismatch) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[4]\n"
                              "    new count[4]\n"
                              "    count, x = scanf(\"%d %d\")\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("target count") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsScanfStatementForm) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[4]\n"
                              "    scanf(\"%d\", &x)\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("left-context") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsScanfFloatTargetLengthMismatch) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[8]\n"
                              "    new count[4]\n"
                              "    count, x = scanf(\"%4f\")\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("float target byte length") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsScanfUnderscoreScanTarget) {
  auto result = analyzeSource("func main() {\n"
                              "    _, _ = scanf(\"%d\")\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find(
                     "only valid for the count target") != std::string::npos);
}

HS_TEST(Sema_RejectsScanfStringTargetWithoutCapacity) {
  auto result = analyzeSource("func main() {\n"
                              "    new count[4]\n"
                              "    new text[1]\n"
                              "    count, text = scanf(\"%s\")\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("NUL terminator") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsRuntimeResizeBytesInFixedParameter) {
  auto result = analyzeSource("func identity(value[4]) -> [4] {\n"
                              "    return value\n"
                              "}\n"
                              "func main() {\n"
                              "    new source[3] = 0x030201\n"
                              "    new requested[8] = 3\n"
                              "    new result[4] = identity(resize_bytes(source, requested))\n"
                              "    return result\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("dynamic View") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsRuntimeResizeBytesInFixedReturn) {
  auto result = analyzeSource("func resize(source[3], requested[8]) -> [4] {\n"
                              "    return resize_bytes(source, requested)\n"
                              "}\n"
                              "func main() {\n"
                              "    new source[3] = 0x030201\n"
                              "    new requested[8] = 3\n"
                              "    new result[4] = resize(source, requested)\n"
                              "    return result\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("dynamic View") !=
                 std::string::npos);
}

HS_TEST(Sema_AllowsDynamicFormatPrint) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[4]\n"
                              "    print(x)\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
}

HS_TEST(Sema_LowersRawPrintNoneViewToPut) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[4]\n"
                              "    print(x as none)\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("Call callee=put") != std::string::npos);
}

HS_TEST(Sema_RejectsTemplatePrintWithoutFormatOp) {
  auto result = analyzeSource("template NoFmt {\n"
                              "    value[4] as i32\n"
                              "}\n"
                              "func main() {\n"
                              "    new x as NoFmt\n"
                              "    print(x as NoFmt)\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("requires matching op format") !=
                 std::string::npos);
}

HS_TEST(Sema_ReportsNeutralMissingUserTemplateFormatDiagnostic) {
  const auto expectMissingFormat = [](std::string_view call) {
    auto result = analyzeSource("template NoFmt {\n"
                                "    value[4] as i32\n"
                                "}\n"
                                "func main() {\n"
                                "    new file = fopen(\"/dev/null\", \"w\")\n"
                                "    new value as NoFmt\n"
                                "    " +
                                std::string(call) +
                                "\n"
                                "    return 0\n"
                                "}\n");

    HS_EXPECT_TRUE(result.unit == nullptr);
    HS_EXPECT_EQ(result.diagnostics.size(), 1U);
    HS_EXPECT_TRUE(
        result.diagnostics[0].find(
            "user template 'NoFmt' requires matching op format") !=
        std::string::npos);
  };

  expectMissingFormat("printf(value as NoFmt)");
  expectMissingFormat("fprintf(file, value as NoFmt)");
}

HS_TEST(Sema_RejectsNonStandardFormatOpSignature) {
  auto result =
      analyzeSource("template BadFmt {\n"
                    "    value[4] as i32\n"
                    "}\n"
                    "impl BadFmt {\n"
                    "    op format(value as BadFmt, out as addr) -> [8] {\n"
                    "        return 0\n"
                    "    }\n"
                    "}\n"
                    "func main() {\n"
                    "    return 0\n"
                    "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find(
                     "impl op format must use signature") != std::string::npos);
}

HS_TEST(Sema_RejectsPrintInvalidFormatSpecifier) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[4]\n"
                              "    print(\"%q\", x)\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("invalid format string") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.diagnostics[0].find("unknown format specifier") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsReturnCountMismatch) {
  auto result = analyzeSource("func pair() -> ([4], [4]) {\n"
                              "    return 1\n"
                              "}\n"
                              "func main() {\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("return value count") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsUnsupportedTemplateOnMemoryWithoutStruct) {
  auto result = analyzeSource("func main() {\n"
                              "    new p[4] ;Missing\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("unknown template") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsHandleAssignmentAndOrdinaryOperators) {
  auto assignment = analyzeSource("func main() {\n"
                                  "    new file as handle\n"
                                  "    file = 0\n"
                                  "    return 0\n"
                                  "}\n");
  HS_EXPECT_TRUE(assignment.unit == nullptr);
  HS_EXPECT_EQ(assignment.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(assignment.diagnostics[0].find("assigned from a handle") !=
                 std::string::npos);

  auto arithmetic = analyzeSource("func main() {\n"
                                  "    new file as handle\n"
                                  "    new value[8] = file + 1\n"
                                  "    return 0\n"
                                  "}\n");
  HS_EXPECT_TRUE(arithmetic.unit == nullptr);
  HS_EXPECT_EQ(arithmetic.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(arithmetic.diagnostics[0].find("only support == and !=") !=
                 std::string::npos);

  auto dereference = analyzeSource("func main() {\n"
                                   "    new file as handle\n"
                                   "    new value[1] = [1]*file\n"
                                   "    return 0\n"
                                   "}\n");
  HS_EXPECT_TRUE(dereference.unit == nullptr);
  HS_EXPECT_EQ(dereference.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(dereference.diagnostics[0].find("cannot be dereferenced") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsHandleFreeAndTemplateRebinding) {
  auto freeCall = analyzeSource("func main() {\n"
                                "    new file as handle\n"
                                "    free(file)\n"
                                "    return 0\n"
                                "}\n");
  HS_EXPECT_TRUE(freeCall.unit == nullptr);
  HS_EXPECT_EQ(freeCall.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(freeCall.diagnostics[0].find("handle parameter") !=
                 std::string::npos);

  auto rebind = analyzeSource("func main() {\n"
                              "    new file as handle\n"
                              "    set file as addr\n"
                              "    return 0\n"
                              "}\n");
  HS_EXPECT_TRUE(rebind.unit == nullptr);
  HS_EXPECT_EQ(rebind.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(rebind.diagnostics[0].find("cannot be rebound") !=
                 std::string::npos);

  auto formatting = analyzeSource("func main() {\n"
                                  "    new file as handle\n"
                                  "    print(file as i64)\n"
                                  "    return 0\n"
                                  "}\n");
  HS_EXPECT_TRUE(formatting.unit == nullptr);
  HS_EXPECT_EQ(formatting.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(formatting.diagnostics[0].find("handle formatting") !=
                 std::string::npos);
}
