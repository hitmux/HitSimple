#include "SemaTestSupport.h"

#include "hitsimple/hir/HIR.h"

#include <string>

using hitsimple::testing::sema::analyzeSource;
using hitsimple::testing::sema::minimalProgram;

HS_TEST(Sema_LowersMinimalProgramToHir) {
  auto result = analyzeSource(minimalProgram);

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("Function name=main") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=x binding=x bytes=1 "
                           "storage=local") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerStore target=x binding=x bytes=1") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("Call callee=printf") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("VariableRef name=x binding=x bytes=1") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("Return") != std::string::npos);
}

HS_TEST(Sema_LowersTypedBinaryExpressionToHir) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[1]\n"
                              "    x %d= 40 %d+ 2\n"
                              "    printf(\"%d\\n\", x %d+ 0)\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=%d+ bytes=4") != std::string::npos);
}

HS_TEST(Sema_LowersCompoundAssignmentToReadModifyWrite) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[1]\n"
                              "    x %d= 40\n"
                              "    x %d+= 2\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("IntegerStore target=x binding=x bytes=1") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=%d+ bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("VariableRef name=x binding=x bytes=1") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersValidatedEffectContractsToHir) {
  auto result = analyzeSource(
      "extern copy(dst[P] as addr, src[P] as addr, len as u64) -> () "
      "effects(read(src, len), write(dst, len), noalias(dst, src), nothrow)\n"
      "func checksum(src[P] as addr, len as u64) -> u64 "
      "effects(read(src, len), nothrow) {\n"
      "    return 0\n"
      "}\n"
      "func main() -> i32 effects(pure) {\n"
      "    return 0\n"
      "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_EQ(result.unit->externFunctions.size(), 1U);
  HS_EXPECT_TRUE(result.unit->externFunctions.front().effectContract.isExplicit);
  HS_EXPECT_TRUE(result.unit->externFunctions.front().effectContract.noAlias.size() == 1U);
  HS_EXPECT_TRUE(result.unit->functions[0]->effectContract.isExplicit);
  HS_EXPECT_TRUE((result.unit->functions[0]->effectContract.flags &
                  hitsimple::hir::EffectNothrow) != 0U);
}

HS_TEST(Sema_LowersControlFlowToHir) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[1]\n"
                              "    while (x) {\n"
                              "        if (x) {\n"
                              "            continue\n"
                              "        } else {\n"
                              "            break\n"
                              "        }\n"
                              "    }\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("While") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("If") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("Continue") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("Break") != std::string::npos);
}

HS_TEST(Sema_RejectsBreakOutsideLoop) {
  auto result = analyzeSource("func main() {\n"
                              "    break\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("break used outside of a loop") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsContinueOutsideLoop) {
  auto result = analyzeSource("func main() {\n"
                              "    continue\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find(
                     "continue used outside of a loop") != std::string::npos);
}

HS_TEST(Sema_LowersFunctionParametersAndCalls) {
  auto result = analyzeSource("func id(value[4]) -> [4] {\n"
                              "    return value\n"
                              "}\n"
                              "func main() {\n"
                              "    return id(3)\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("Function name=id") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("Param name=value binding=value bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=id bytes=4") != std::string::npos);
}

HS_TEST(Sema_LowersFunctionReturnSignatures) {
  auto result = analyzeSource("func main() -> [4] {\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("ReturnSignature bytes=4") != std::string::npos);
}

HS_TEST(Sema_LowersTemplateReturnSignatures) {
  auto result = analyzeSource("extern host_scale(value as f64) -> f64\n"
                              "func main() {\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("ExternFunction name=host_scale") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("Return bytes=8") != std::string::npos);
}

HS_TEST(Sema_LowersTopLevelExternDeclarations) {
  auto result = analyzeSource("extern puts(str[8]) -> ()\n"
                              "extern host_inc(value as i32) -> [4]\n"
                              "extern errno as i32\n"
                              "extern host_buffer[8] as bytes\n"
                              "func main() {\n"
                              "    puts(0)\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("ExternFunction name=puts") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("ExternFunction name=host_inc") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("Param bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("Return bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("GlobalMemory name=errno binding=errno bytes=4 extern=true") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("GlobalMemory name=host_buffer binding=host_buffer "
                           "bytes=8 extern=true") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("Call callee=puts") != std::string::npos);
}

HS_TEST(Sema_RejectsUnsizedExternValuesAndByValueVariableTemplates) {
  auto bytesParameter = analyzeSource("extern host_copy(text[8] as bytes) -> ()\n"
                                      "func main() {\n"
                                      "    return 0\n"
                                      "}\n");
  HS_EXPECT_TRUE(bytesParameter.unit == nullptr);
  HS_EXPECT_EQ(bytesParameter.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(bytesParameter.diagnostics[0].find("cannot pass bytes by value") !=
                 std::string::npos);

  auto cstrParameter = analyzeSource("extern host_copy(text[8] as cstr) -> ()\n"
                                     "func main() {\n"
                                     "    return 0\n"
                                     "}\n");
  HS_EXPECT_TRUE(cstrParameter.unit == nullptr);
  HS_EXPECT_EQ(cstrParameter.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(cstrParameter.diagnostics[0].find("cannot pass cstr by value") !=
                 std::string::npos);

  auto noneParameter = analyzeSource("extern host_copy(value as none) -> ()\n"
                                     "func main() {\n"
                                     "    return 0\n"
                                     "}\n");
  HS_EXPECT_TRUE(noneParameter.unit == nullptr);
  HS_EXPECT_EQ(noneParameter.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(noneParameter.diagnostics[0].find("requires an explicit byte length") !=
                 std::string::npos);

  auto bytesVariable = analyzeSource("extern buffer as bytes\n"
                                     "func main() {\n"
                                     "    return 0\n"
                                     "}\n");
  HS_EXPECT_TRUE(bytesVariable.unit == nullptr);
  HS_EXPECT_EQ(bytesVariable.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(bytesVariable.diagnostics[0].find("requires an explicit byte length") !=
                 std::string::npos);
}

HS_TEST(Sema_PreservesTemplateInferredFunctionParameterLength) {
  auto result = analyzeSource("func show(value as f64) {\n"
                              "    print(value as f64)\n"
                              "    return\n"
                              "}\n"
                              "func main() {\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("Param name=value binding=value bytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("VariableRef name=value binding=value bytes=8") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsStageGFunctionSignatureErrors) {
  auto argumentCount = analyzeSource("func id(value[4]) -> [4] {\n"
                                     "    return value\n"
                                     "}\n"
                                     "func main() {\n"
                                     "    return id()\n"
                                     "}\n");
  HS_EXPECT_TRUE(argumentCount.unit == nullptr);
  HS_EXPECT_TRUE(argumentCount.diagnostics[0].find("argument count") !=
                 std::string::npos);

  auto returnLength = analyzeSource("func id() -> [1] {\n"
                                    "    return 300\n"
                                    "}\n"
                                    "func main() {\n"
                                    "    return 0\n"
                                    "}\n");
  HS_EXPECT_TRUE(returnLength.unit == nullptr);
  HS_EXPECT_TRUE(returnLength.diagnostics[0].find("return value byte length") !=
                 std::string::npos);

  auto multiExpression =
      analyzeSource("func pair() -> ([4], [4]) {\n"
                    "    return 1, 2\n"
                    "}\n"
                    "func main() {\n"
                    "    return pair()\n"
                    "}\n");
  HS_EXPECT_TRUE(multiExpression.unit == nullptr);
  HS_EXPECT_TRUE(multiExpression.diagnostics[0].find("multi-return") !=
                 std::string::npos);
}
