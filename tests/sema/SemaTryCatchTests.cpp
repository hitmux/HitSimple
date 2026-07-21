#include "SemaTestSupport.h"

#include "hitsimple/hir/HIR.h"

#include <string>

using hitsimple::testing::sema::analyzeSource;

HS_TEST(Sema_LowersStageHForGotoAndTryCatch) {
  auto result = analyzeSource("func main() {\n"
                              "    new sum[4]\n"
                              "    sum = 0\n"
                              "    for (new i[4] = 0; i < 3; i++) {\n"
                              "        if (i == 1) {\n"
                              "            continue\n"
                              "        }\n"
                              "        sum = sum + i\n"
                              "    }\n"
                              "    goto done\n"
                              "    sum = 99\n"
                              "    try {\n"
                              "        throw 7\n"
                              "    } catch (err[4]) {\n"
                              "        sum = err\n"
                              "    }\n"
                              "    done: return sum\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("For") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("Goto label=done") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("Label label=done") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("TryCatch error=err") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("Throw source_template=none source_bytes=4 "
                           "target_template=none target_bytes=4") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersUncaughtThrowForRuntimeTermination) {
  auto result = analyzeSource("func main() {\n"
                              "    throw 7\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("Throw source_template=none source_bytes=1 "
                           "target_template=none target_bytes=0") !=
                 std::string::npos);
}

HS_TEST(Sema_AllowsThrowInsideTry) {
  auto result = analyzeSource("func main() {\n"
                              "    try {\n"
                              "        throw 7\n"
                              "    } catch (err[4]) {\n"
                              "        return err\n"
                              "    }\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
}

HS_TEST(Sema_PreservesViewContractsForTryCatchThrow) {
  auto result = analyzeSource("template ErrorView {\n"
                              "    code[4] as i32\n"
                              "}\n"
                              "func main() {\n"
                              "    new floating as f64 = 42.5\n"
                              "    new custom as ErrorView\n"
                              "    try {\n"
                              "        throw floating\n"
                              "    } catch (floatError as f64) {\n"
                              "        new observed as f64 = floatError\n"
                              "    }\n"
                              "    try {\n"
                              "        throw custom\n"
                              "    } catch (customError as ErrorView) {\n"
                              "        new observed[4] = customError.code\n"
                              "    }\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("Throw source_template=f64 source_bytes=8 "
                           "target_template=f64 target_bytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("Throw source_template=ErrorView source_bytes=4 "
                           "target_template=ErrorView target_bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("TryCatch error=floatError binding=floatError "
                           "template=f64 bytes=8") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("TryCatch error=customError binding=customError "
                           "template=ErrorView bytes=4") !=
                 std::string::npos);
}

HS_TEST(Sema_AllowsThrowWithOrdinaryI32ToAddrAssignment) {
  auto assignment = analyzeSource("func main() {\n"
                                  "    new source as i32 = 7\n"
                                  "    new address as addr = source\n"
                                  "    return 0\n"
                                  "}\n");
  HS_EXPECT_TRUE(assignment.unit != nullptr);
  HS_EXPECT_TRUE(assignment.diagnostics.empty());

  auto thrown = analyzeSource("func main() {\n"
                              "    new source as i32 = 7\n"
                              "    try {\n"
                              "        throw source\n"
                              "    } catch (error as addr) {\n"
                              "        return error? - 7\n"
                              "    }\n"
                              "    return 1\n"
                              "}\n");
  HS_EXPECT_TRUE(thrown.unit != nullptr);
  HS_EXPECT_TRUE(thrown.diagnostics.empty());
}

HS_TEST(Sema_RejectsUntypedIntegerThrowToFloatCatch) {
  auto result = analyzeSource("func main() {\n"
                              "    try {\n"
                              "        throw 7\n"
                              "    } catch (error as f64) {\n"
                              "        return 0\n"
                              "    }\n"
                              "    return 1\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_TRUE(result.diagnostics[0].find(
                     "float operand is not a float expression") !=
                 std::string::npos);
}

HS_TEST(Sema_PreservesUserTemplateFunctionReturnForThrow) {
  auto result = analyzeSource("template ErrorView {\n"
                              "    code[4] as i32\n"
                              "}\n"
                              "func make_error() -> as ErrorView {\n"
                              "    new error as ErrorView\n"
                              "    error.code = 42\n"
                              "    return error\n"
                              "}\n"
                              "func main() {\n"
                              "    try {\n"
                              "        throw make_error()\n"
                              "    } catch (error as ErrorView) {\n"
                              "        return error.code - 42\n"
                              "    }\n"
                              "    return 1\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("Throw source_template=ErrorView source_bytes=4 "
                           "target_template=ErrorView target_bytes=4") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsIncompatibleCatchViewContracts) {
  auto unsized = analyzeSource("func main() {\n"
                               "    try {\n"
                               "        throw 1\n"
                               "    } catch (error as bytes) {\n"
                               "        return 0\n"
                               "    }\n"
                               "    return 1\n"
                               "}\n");
  HS_EXPECT_TRUE(unsized.unit == nullptr);
  HS_EXPECT_TRUE(unsized.diagnostics[0].find(
                     "catch error 'error' requires an explicit byte length") !=
                 std::string::npos);

  auto lengthMismatch = analyzeSource("func main() {\n"
                                      "    new value as f64 = 42.5\n"
                                      "    try {\n"
                                      "        throw value\n"
                                      "    } catch (error[4] as bytes) {\n"
                                      "        return 0\n"
                                      "    }\n"
                                      "    return 1\n"
                                      "}\n");
  HS_EXPECT_TRUE(lengthMismatch.unit == nullptr);
  HS_EXPECT_TRUE(lengthMismatch.diagnostics[0].find(
                     "bytes assignment requires an equal-length source View") !=
                 std::string::npos);

  auto templateMismatch = analyzeSource("template Left {\n"
                                        "    code[4] as i32\n"
                                        "}\n"
                                        "template Right {\n"
                                        "    code[4] as i32\n"
                                        "}\n"
                                        "func main() {\n"
                                        "    new value as Left\n"
                                        "    try {\n"
                                        "        throw value\n"
                                        "    } catch (error as Right) {\n"
                                        "        return 0\n"
                                        "    }\n"
                                        "    return 1\n"
                                        "}\n");
  HS_EXPECT_TRUE(templateMismatch.unit == nullptr);
  HS_EXPECT_TRUE(templateMismatch.diagnostics[0].find(
                     "default user template assignment requires matching templates") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsThrowLiteralThatDoesNotFitCatchView) {
  auto assignment = analyzeSource("func main() {\n"
                                  "    new value as i8 = 256\n"
                                  "    return 0\n"
                                  "}\n");
  HS_EXPECT_TRUE(assignment.unit == nullptr);
  HS_EXPECT_EQ(assignment.diagnostics.size(), 1U);
  if (!assignment.diagnostics.empty()) {
    HS_EXPECT_TRUE(assignment.diagnostics[0].find(
                       "integer literal '256' does not fit in target 'value'") !=
                   std::string::npos);
  }

  auto thrown = analyzeSource("func main() {\n"
                              "    try {\n"
                              "        throw 256\n"
                              "    } catch (error as i8) {\n"
                              "        return error\n"
                              "    }\n"
                              "    return 1\n"
                              "}\n");
  HS_EXPECT_TRUE(thrown.unit == nullptr);
  HS_EXPECT_EQ(thrown.diagnostics.size(), 1U);
  if (!thrown.diagnostics.empty()) {
    HS_EXPECT_TRUE(thrown.diagnostics[0].find(
                       "integer literal '256' does not fit in target 'error'") !=
                   std::string::npos);
  }
}

HS_TEST(Sema_RejectsFloatFunctionResultForIntegerCatchView) {
  auto assignment = analyzeSource("func make_float() -> as f64 {\n"
                                  "    return 1.5\n"
                                  "}\n"
                                  "func main() {\n"
                                  "    new value as i64\n"
                                  "    value = make_float()\n"
                                  "    return 0\n"
                                  "}\n");
  HS_EXPECT_TRUE(assignment.unit == nullptr);
  HS_EXPECT_EQ(assignment.diagnostics.size(), 1U);
  if (!assignment.diagnostics.empty()) {
    HS_EXPECT_TRUE(assignment.diagnostics[0].find(
                       "right operand of '=' is not an integer expression") !=
                   std::string::npos);
  }

  auto thrown = analyzeSource("func make_float() -> as f64 {\n"
                              "    return 1.5\n"
                              "}\n"
                              "func main() {\n"
                              "    try {\n"
                              "        throw make_float()\n"
                              "    } catch (error as i64) {\n"
                              "        return error\n"
                              "    }\n"
                              "    return 1\n"
                              "}\n");
  HS_EXPECT_TRUE(thrown.unit == nullptr);
  HS_EXPECT_EQ(thrown.diagnostics.size(), 1U);
  if (!thrown.diagnostics.empty()) {
    HS_EXPECT_TRUE(thrown.diagnostics[0].find(
                       "right operand of '=' is not an integer expression") !=
                   std::string::npos);
  }
}

HS_TEST(Sema_AllowsAddrFunctionResultForAddrCatchView) {
  auto result = analyzeSource("func make_addr() -> as addr {\n"
                              "    return 7\n"
                              "}\n"
                              "func main() {\n"
                              "    try {\n"
                              "        throw make_addr()\n"
                              "    } catch (error as addr) {\n"
                              "        return error? - 7\n"
                              "    }\n"
                              "    return 1\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("Throw source_template=addr source_bytes=8 "
                           "target_template=addr target_bytes=8") !=
                 std::string::npos);
}

HS_TEST(Sema_ReportsCallValueErrorsForThrow) {
  auto noValue = analyzeSource("extern no_value() -> ()\n"
                               "func main() {\n"
                               "    try {\n"
                               "        throw no_value()\n"
                               "    } catch (error[1]) {\n"
                               "        return error\n"
                               "    }\n"
                               "    return 1\n"
                               "}\n");
  HS_EXPECT_TRUE(noValue.unit == nullptr);
  HS_EXPECT_EQ(noValue.diagnostics.size(), 1U);
  if (!noValue.diagnostics.empty()) {
    HS_EXPECT_TRUE(noValue.diagnostics[0].find(
                       "function call 'no_value' does not return a value") !=
                   std::string::npos);
  }

  auto multipleValues = analyzeSource("extern pair() -> ([1], [1])\n"
                                      "func main() {\n"
                                      "    try {\n"
                                      "        throw pair()\n"
                                      "    } catch (error[1]) {\n"
                                      "        return error\n"
                                      "    }\n"
                                      "    return 1\n"
                                      "}\n");
  HS_EXPECT_TRUE(multipleValues.unit == nullptr);
  HS_EXPECT_EQ(multipleValues.diagnostics.size(), 1U);
  if (!multipleValues.diagnostics.empty()) {
    HS_EXPECT_TRUE(multipleValues.diagnostics[0].find(
                       "multi-return function call 'pair' cannot be used as a "
                       "single expression") != std::string::npos);
  }
}

HS_TEST(Sema_LowersCstrLiteralThrowThroughStringMaterialization) {
  auto result = analyzeSource("func main() {\n"
                              "    try {\n"
                              "        throw \"hi\"\n"
                              "    } catch (error[4] as cstr) {\n"
                              "        return 0\n"
                              "    }\n"
                              "    return 1\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("Throw source_template=cstr source_bytes=4 "
                           "target_template=cstr target_bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("StringStore target=error binding=error bytes=4 "
                           "storage=local value=\"hi\"") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersCstrVariableThrowThroughStringMaterialization) {
  auto result = analyzeSource("func main() {\n"
                              "    new source[3] as cstr = \"hi\"\n"
                              "    try {\n"
                              "        throw source\n"
                              "    } catch (error[4] as cstr) {\n"
                              "        return 0\n"
                              "    }\n"
                              "    return 1\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("Throw source_template=cstr source_bytes=4 "
                           "target_template=cstr target_bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("StringCopyStore target=error binding=error bytes=4 "
                           "storage=local source=source sourceBinding=source "
                           "sourceBytes=3") != std::string::npos);
}

HS_TEST(Sema_LowersUserTemplateAssignmentOperatorForCatchThrow) {
  auto result = analyzeSource("template Counter {\n"
                              "    value[4] as i32\n"
                              "}\n"
                              "impl Counter {\n"
                              "    op = (dst as Counter, src as Counter) -> [4] {\n"
                              "        dst.value = src.value + 1\n"
                              "        return dst\n"
                              "    }\n"
                              "}\n"
                              "func main() {\n"
                              "    new source as Counter\n"
                              "    source.value = 41\n"
                              "    try {\n"
                              "        throw source\n"
                              "    } catch (error as Counter) {\n"
                              "        return error.value - 42\n"
                              "    }\n"
                              "    return 1\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("Throw source_template=Counter source_bytes=4 "
                           "target_template=Counter target_bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("UserTemplateOpCall callee=__hitsimple.implop") !=
                 std::string::npos);
}

HS_TEST(Sema_ReportsTargetByteLengthForDefaultFloatAssignment) {
  auto result = analyzeSource("func main() {\n"
                              "    new source as f64 = 42.5\n"
                              "    new target[4]\n"
                              "    target = source\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  if (!result.diagnostics.empty()) {
    HS_EXPECT_TRUE(result.diagnostics[0].find(
                       "right operand of '=' byte length 8 does not match "
                       "target byte length 4") != std::string::npos);
  }
}
