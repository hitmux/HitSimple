#include "SemaTestSupport.h"

#include "hitsimple/hir/HIR.h"
#include "hitsimple/stdlib/StandardLibraryModules.h"

#include <algorithm>
#include <string>

using hitsimple::testing::sema::analyzePreprocessedSource;
using hitsimple::testing::sema::analyzeSource;
using hitsimple::testing::sema::minimalProgram;

HS_TEST(Sema_InfersDeclarationInitializerLength) {
  auto result = analyzeSource("func main() {\n"
                              "    new small = 127\n"
                              "    new medium = 128\n"
                              "    new hex = 0xff\n"
                              "    new binary = 0b1000_0000\n"
                              "    new octal = 0o377\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=small binding=small bytes=1") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=medium binding=medium bytes=2") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=hex binding=hex bytes=2") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=binary binding=binary bytes=2") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=octal binding=octal bytes=2") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersStageEIntegerExpressions) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[4]\n"
                              "    x = (1 + 2 * 3) << 1\n"
                              "    x = x ** 3\n"
                              "    x = x? >= 8 ? ~x : -'a'\n"
                              "    x = true && !false || x == 0\n"
                              "    return x\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=+ bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=* bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=** bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=<< bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("UnsignedExpr bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=>= bytes=1") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("TernaryExpr bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("UnaryExpr op=~ bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("UnaryExpr op=- bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("UnaryExpr op=! bytes=1") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=&& bytes=1") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=|| bytes=1") != std::string::npos);
}

HS_TEST(Sema_FoldsTypedIntegerLiteralExpressions) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[2]\n"
                              "    x = 1 %2d+ 2 %2d* 3\n"
                              "    return x\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("IntegerLiteral value=7 bytes=2") !=
                 std::string::npos);
}

HS_TEST(Sema_SelectsCoreHsReferenceProviderAndModule) {
  auto result = analyzePreprocessedSource("$include <ctype.hsh>\n"
                                          "func main() {\n"
                                          "    return is_digit(48) ? 0 : 1\n"
                                          "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto modules = hitsimple::stdlib::selectStandardLibraryProviders(
      *result.unit, hitsimple::stdlib::BuiltinProviderSelection::Reference);
  HS_EXPECT_EQ(modules.size(), 1U);
  HS_EXPECT_EQ(modules.front(), std::string("Ctype"));

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("ExternFunction name=__hs_stdlib_ctype_is_digit") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=__hs_stdlib_ctype_is_digit") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("provider=CoreHs") != std::string::npos);
}

HS_TEST(Sema_RejectsTypedIntegerConstantOverflow) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[1]\n"
                              "    x = 120 %1d+ 10\n"
                              "    return x\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("constant expression overflows "
                                            "1-byte integer") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersStageEFloatExpressionsAndSizeof) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[4] %f= 1.5\n"
                              "    new y[4] %f= 2.25\n"
                              "    x %f= x %f+ y %f* 2.0\n"
                              "    return sizeof(x)\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("FloatStore target=x binding=x bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("FloatBinaryExpr op=%f+ bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("FloatBinaryExpr op=%f* bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("FloatLiteral value=2.0 bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerLiteral value=4 bytes=8") !=
                 std::string::npos);
}

HS_TEST(Sema_UsesFloatTemplateForOrdinaryComparisons) {
  auto result = analyzeSource("func main() {\n"
                              "    new left as f128 = 1.0\n"
                              "    new right as f128 = 2.0\n"
                              "    new less[1] %b= left < right\n"
                              "    return less\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("FloatCompareExpr op=< floatBytes=16") !=
                 std::string::npos);
}

HS_TEST(Sema_UsesTemporaryFloatViewInFloatOperators) {
  auto result = analyzeSource("func main() {\n"
                              "    new bits as u32 = 0x3f800000\n"
                              "    new value as f32 = (bits as f32) %f+ 1.0\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("FloatBinaryExpr op=%f+ bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("TemplateViewExpr template=f32 bytes=4") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersDefaultFloatAssignmentForFloatTemplate) {
  auto result = analyzeSource("func main() {\n"
                              "    new x as f64 = 1.5\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("FloatStore target=x binding=x bytes=8") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersTwoByteFloatAssignment) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[2] %f= 1.5\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("FloatStore target=x binding=x bytes=2") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("FloatLiteral value=1.5 bytes=2") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersStageEConversionFunctions) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[4] = 42\n"
                              "    new f[4] %f= to_f32(x)\n"
                              "    new y[2] = to_i16(f)\n"
                              "    return y\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("ToFloatExpr bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("ToIntExpr floatBytes=4 bytes=2") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=y binding=y bytes=2") !=
                 std::string::npos);
}

HS_TEST(Sema_ConvertsIntegerInputsToIntegerStandardTypes) {
  auto result = analyzeSource("func main() {\n"
                              "    new value[4] = 42\n"
                              "    new signed = to_i16(value)\n"
                              "    new unsigned = to_u8(value)\n"
                              "    return signed + unsigned\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("IntegerCastExpr bytes=2 signed=true") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerCastExpr bytes=1 signed=false") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersFloatMathHelpers) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[4] %f= 1.5\n"
                              "    new y[4] %f= f_abs(x)\n"
                              "    new z[8] %f= f_sqrt(4.0)\n"
                              "    new p[4] %f= f_pow(x, 2.0)\n"
                              "    new s[4] %f= f_sin(x)\n"
                              "    new c[8] %f= f_ceil(z)\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=f_abs bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=f_sqrt bytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(
      dump.find("CallExpr callee=f_sqrt bytes=8 floating=true builtin=52 "
                "provider=Intrinsic returnRule=ArgumentLength overload=2") !=
      std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=f_pow bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=f_sin bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=f_ceil bytes=8") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersFloatingMinAndMax) {
  auto result = analyzeSource("func main() {\n"
                              "    new left as f32 = 1.5\n"
                              "    new right as f32 = 2.0\n"
                              "    new lower as f32 = min(left, right)\n"
                              "    new upper as f32 = max(left, right)\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=min bytes=4 floating=true") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=max bytes=4 floating=true") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersStandardLibraryCoreCalls) {
  auto result = analyzeSource("func main() {\n"
                              "    new ptr = calloc(1, 8)\n"
                              "    memset(ptr, 0, 8)\n"
                              "    new len = length(ptr)\n"
                              "    new text_len = strlen(\"abc\")\n"
                              "    new swapped[2] = byte_swap(0x1234)\n"
                              "    new raw[1] = resize_bytes(swapped, 1)\n"
                              "    new upper[1] = to_upper('a')\n"
                              "    free(ptr)\n"
                              "    return len\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=calloc bytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("Call callee=memset") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerLiteral value=8 bytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=strlen bytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=byte_swap bytes=2") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=resize_bytes bytes=1") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=to_upper bytes=1") !=
                 std::string::npos);
}

HS_TEST(Sema_AllowsDiscardingPutResult) {
  auto result = analyzeSource("func main() {\n"
                              "    put('!')\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("Call callee=put") != std::string::npos);
}

HS_TEST(Sema_LowersDynamicResizeBytesForAnExplicitlySizedTarget) {
  auto result =
      analyzeSource("func main() {\n"
                    "    new source[3] = 0x030201\n"
                    "    new requested[8] = 3\n"
                    "    new result[3] = resize_bytes(source, requested)\n"
                    "    return result\n"
                    "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("DynamicByteViewExpr op=resize_bytes") !=
                 std::string::npos);
}

HS_TEST(Sema_AllowsLiteralResizeBytesInFixedReturn) {
  auto result = analyzeSource("func trim(source[4]) -> [3] {\n"
                              "    return resize_bytes(source, 3)\n"
                              "}\n"
                              "func main() {\n"
                              "    new source[4] = 0x04030201\n"
                              "    new result[3] = trim(source)\n"
                              "    return result\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
}

HS_TEST(Sema_AllowsByteSwapForAnArbitraryFixedLengthView) {
  auto result = analyzeSource("func main() {\n"
                              "    new source[3] = 0x030201\n"
                              "    new result[3] = byte_swap(source)\n"
                              "    return result\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("ByteSwapExpr bytes=3") != std::string::npos);
}

HS_TEST(Sema_LowersFormattedAndRawOutputAsValueExpressions) {
  auto result = analyzeSource("func main() {\n"
                              "    new bytes[3] = 0x030201\n"
                              "    new formatted = printf(\"%d\", 42)\n"
                              "    new filled = memset(bytes, 0, 3)\n"
                              "    new raw = print(bytes as none)\n"
                              "    new written = put(bytes)\n"
                              "    return formatted + filled + raw + written\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=printf bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=memset bytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=put bytes=4") != std::string::npos);
}

HS_TEST(Sema_AllowsDynamicFormatInScanfLeftContext) {
  auto result = analyzeSource("func main() {\n"
                              "    new format[3] as cstr = \"%d\"\n"
                              "    new count[4]\n"
                              "    new value[4]\n"
                              "    count, value = scanf(format)\n"
                              "    return count\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("InputCallStore callee=scanf") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("VariableRef name=format") != std::string::npos);
}

HS_TEST(Sema_RequiresIncludedStandardLibraryHeader) {
  auto result = analyzePreprocessedSource("func main() {\n"
                                          "    new ptr = alloc(8)\n"
                                          "    return 0\n"
                                          "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_TRUE(!result.diagnostics.empty());
  HS_EXPECT_TRUE(
      std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                  [](const auto &diagnostic) {
                    return diagnostic.find("stdlib.hsh") != std::string::npos;
                  }));
}

HS_TEST(Sema_ImportsStandardLibraryFunctionFromMatchingHeader) {
  auto result = analyzePreprocessedSource("$include <stdlib.hsh>\n"
                                          "func main() {\n"
                                          "    new ptr = alloc(8)\n"
                                          "    free(ptr)\n"
                                          "    return 0\n"
                                          "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
}

HS_TEST(Sema_ImportsMemoryFunctionsFromStringHeader) {
  auto result = analyzePreprocessedSource(
      "$include <string.hsh>\n"
      "func main() {\n"
      "    new source[1] = 42\n"
      "    new destination[1]\n"
      "    new copied = memcpy(destination, source, 1)\n"
      "    return 0\n"
      "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
}

HS_TEST(Sema_RejectsMemoryFunctionFromWrongHeader) {
  auto result = analyzePreprocessedSource(
      "$include <stdlib.hsh>\n"
      "func main() {\n"
      "    new source[1] = 42\n"
      "    new destination[1]\n"
      "    new copied = memcpy(destination, source, 1)\n"
      "    return 0\n"
      "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_TRUE(!result.diagnostics.empty());
  HS_EXPECT_TRUE(result.diagnostics[0].find("string.hsh") != std::string::npos);
}

HS_TEST(Sema_RejectsRemovedStandardLibraryNames) {
  for (const auto name : {"to_float", "to_int", "reinterpret"}) {
    auto result = analyzePreprocessedSource("func main() {\n"
                                            "    new value = " +
                                            std::string(name) +
                                            "(1)\n"
                                            "    return 0\n"
                                            "}\n");

    HS_EXPECT_TRUE(result.unit == nullptr);
    HS_EXPECT_TRUE(!result.diagnostics.empty());
    HS_EXPECT_TRUE(result.diagnostics[0].find("not accepted") !=
                   std::string::npos);
  }
}

HS_TEST(Sema_ImportsIntegerMathFromStdlibHeader) {
  auto result = analyzePreprocessedSource("$include <stdlib.hsh>\n"
                                          "func main() {\n"
                                          "    new value as i32 = -1\n"
                                          "    new magnitude = abs(value)\n"
                                          "    return magnitude\n"
                                          "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
}

HS_TEST(Sema_RejectsUnsignedAbsOperand) {
  auto result = analyzePreprocessedSource("$include <stdlib.hsh>\n"
                                          "func main() {\n"
                                          "    new value as u8 = 255\n"
                                          "    new magnitude = abs(value)\n"
                                          "    return magnitude\n"
                                          "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(
      result.diagnostics[0].find("abs requires a signed integer expression") !=
      std::string::npos);
}

HS_TEST(Sema_RequiresCstrViewsForStandardLibraryStringArguments) {
  auto result = analyzeSource("func main() {\n"
                              "    new text[4] as bytes\n"
                              "    new length as u64 = strlen(text)\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics.front().find(
                     "function call 'strlen' argument 1 must be a cstr View") !=
                 std::string::npos);
}

HS_TEST(Sema_RequiresCstrViewsInsteadOfAddresses) {
  auto result = analyzeSource("func main() {\n"
                              "    new text[4] as cstr = \"ok\"\n"
                              "    new length as u64 = strlen(&text)\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics.front().find(
                     "function call 'strlen' argument 1 must be a cstr View") !=
                 std::string::npos);
}

HS_TEST(Sema_RequiresWritableLviewsForStandardLibraryWrites) {
  const auto stringResult =
      analyzeSource("func main() {\n"
                    "    new copied as addr = strcpy(\"immutable\", \"x\")\n"
                    "    return 0\n"
                    "}\n");
  HS_EXPECT_TRUE(stringResult.unit == nullptr);
  HS_EXPECT_EQ(stringResult.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(stringResult.diagnostics.front().find(
                     "function call 'strcpy' argument 1 must be a writable "
                     "lvalue View") != std::string::npos);

  const auto fileResult =
      analyzeSource("func main() {\n"
                    "    new file as handle = fopen(\"/dev/null\", \"r\")\n"
                    "    new destination[1] as bytes\n"
                    "    new count as u64 = fread(&destination, 1, 1, file)\n"
                    "    return 0\n"
                    "}\n");
  HS_EXPECT_TRUE(fileResult.unit == nullptr);
  HS_EXPECT_EQ(fileResult.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(fileResult.diagnostics.front().find(
                     "function call 'fread' argument 1 must be a writable "
                     "lvalue View") != std::string::npos);
}

HS_TEST(Sema_RequiresWritableMemoryViewsForMemoryWrites) {
  const auto rejected =
      analyzeSource("func main() {\n"
                    "    new address as addr = memset(\"immutable\", 0, 1)\n"
                    "    return 0\n"
                    "}\n");
  HS_EXPECT_TRUE(rejected.unit == nullptr);
  HS_EXPECT_EQ(rejected.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(rejected.diagnostics.front().find(
                     "function call 'memset' argument 1 must be a writable "
                     "memory View or addr") != std::string::npos);

  const auto accepted =
      analyzeSource("func main() {\n"
                    "    new destination[1] as bytes\n"
                    "    new address as addr = memset(destination, 0, 1)\n"
                    "    return 0\n"
                    "}\n");
  HS_EXPECT_TRUE(accepted.unit != nullptr);
  HS_EXPECT_TRUE(accepted.diagnostics.empty());
}

HS_TEST(Sema_EnforcesAddrParametersAndKeepsViewParametersPolymorphic) {
  const auto rejected = analyzeSource("func main() {\n"
                                      "    free(1)\n"
                                      "    return 0\n"
                                      "}\n");
  HS_EXPECT_TRUE(rejected.unit == nullptr);
  HS_EXPECT_EQ(rejected.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(rejected.diagnostics.front().find(
                     "function call 'free' argument 1 must be an addr View") !=
                 std::string::npos);

  const auto accepted =
      analyzeSource("func main() {\n"
                    "    new file as handle = fopen(\"/dev/null\", \"w\")\n"
                    "    new value[8] as bytes\n"
                    "    new written as i32 = fput(file, value)\n"
                    "    return written\n"
                    "}\n");
  HS_EXPECT_TRUE(accepted.unit != nullptr);
  HS_EXPECT_TRUE(accepted.diagnostics.empty());
}

HS_TEST(Sema_RequiresCstrViewsForFormatProtocols) {
  auto result = analyzeSource("func main() {\n"
                              "    new format[3] as bytes\n"
                              "    new value as i32 = 1\n"
                              "    new written as i32 = printf(format, value)\n"
                              "    return written\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics.front().find(
                     "function call 'printf' argument 1 must be a cstr View") !=
                 std::string::npos);
}

HS_TEST(Sema_PreservesFormatArgumentKindsForDynamicFormat) {
  const auto result = analyzeSource("func main() {\n"
                                    "    new format[3] as cstr = \"%f\"\n"
                                    "    new value as f64 = 1.5\n"
                                    "    new written as i32 = printf(format, value)\n"
                                    "    return written\n"
                                    "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=printf") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("formatArgs=string,float") != std::string::npos);
}

HS_TEST(Sema_AcceptsStdlibDeclarationsFromSystemInclude) {
  auto result = analyzePreprocessedSource("$include <stdlib.hsh>\n"
                                          "$include <string.hsh>\n"
                                          "$include <stdio.hsh>\n"
                                          "func main() {\n"
                                          "    new ptr = calloc(1, 8)\n"
                                          "    new len = strlen(\"abc\")\n"
                                          "    free(ptr)\n"
                                          "    print(\"%d\", len)\n"
                                          "    return 0\n"
                                          "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=calloc bytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=strlen bytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("Call callee=print") != std::string::npos);
}

HS_TEST(Sema_LowersStandardTemplatePrintToPrintfStyleCall) {
  auto result = analyzeSource("func main() {\n"
                              "    new n[4] = 42\n"
                              "    print(n as i32)\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("Call callee=print") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("StringLiteral value=\"%d\"") != std::string::npos);
}

HS_TEST(Sema_LowersStandardTemplatePrintF64) {
  auto result = analyzeSource("func main() {\n"
                              "    new n as f64 = 1.5\n"
                              "    print(n as f64)\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("Call callee=print") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("StringLiteral value=\"%f\"") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("VariableRef name=n binding=n bytes=8") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersUserTemplateOperatorThroughInternalViewAbi) {
  auto result = analyzeSource("template Vec2 {\n"
                              "    x[8] as f64\n"
                              "    y[8] as f64\n"
                              "}\n"
                              "impl Vec2 {\n"
                              "    op + (lhs as Vec2, rhs as Vec2) -> [16] {\n"
                              "        return lhs\n"
                              "    }\n"
                              "}\n"
                              "func main() {\n"
                              "    new lhs as Vec2\n"
                              "    new rhs as Vec2\n"
                              "    new out[16] = lhs + rhs\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("UserTemplateOpCallExpr template=Vec2 bytes=16") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersImplTemplateMethodCallThroughInternalViewAbi) {
  auto result =
      analyzeSource("template Counter {\n"
                    "    value[4] as i32\n"
                    "}\n"
                    "impl Counter {\n"
                    "    func identity(self as Counter) -> as Counter {\n"
                    "        return self\n"
                    "    }\n"
                    "}\n"
                    "func main() {\n"
                    "    new value as Counter\n"
                    "    new copy as Counter = value.identity()\n"
                    "    return 0\n"
                    "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("UserTemplateOpCallExpr template=Counter bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("callee=__hitsimple.implmethod.Counter.0") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersUserTemplateOperatorWithStandardTemplateOperand) {
  auto result =
      analyzeSource("template Vec2 {\n"
                    "    x[8] as f64\n"
                    "    y[8] as f64\n"
                    "}\n"
                    "impl Vec2 {\n"
                    "    op * (lhs as Vec2, scalar as f64) -> [16] {\n"
                    "        return lhs\n"
                    "    }\n"
                    "}\n"
                    "func main() {\n"
                    "    new lhs as Vec2\n"
                    "    new scalar as f64 = 2.0\n"
                    "    new out as Vec2 = lhs * scalar\n"
                    "    return 0\n"
                    "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("UserTemplateOpCallExpr template=Vec2 bytes=16") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersUserTemplateAssignmentOperatorAsWriteThroughCall) {
  auto result = analyzeSource("template Vec2 {\n"
                              "    x[8] as f64\n"
                              "    y[8] as f64\n"
                              "}\n"
                              "impl Vec2 {\n"
                              "    op = (dst as Vec2, src as Vec2) -> [16] {\n"
                              "        dst.x %f= src.x\n"
                              "        dst.y %f= src.y\n"
                              "        return dst\n"
                              "    }\n"
                              "}\n"
                              "func main() {\n"
                              "    new dst as Vec2\n"
                              "    new src as Vec2\n"
                              "    dst = src\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("UserTemplateOpCall callee=__hitsimple.implop") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersPostfixIncrementForAllAddressableIntegerLvalues) {
  auto result = analyzeSource("template Cell {\n"
                              "    value[1]\n"
                              "}\n"
                              "func main() {\n"
                              "    new cell as Cell\n"
                              "    new bytes[4]\n"
                              "    new target[1]\n"
                              "    new ptr[8]\n"
                              "    ptr &= &target\n"
                              "    cell.value++\n"
                              "    bytes[0]++\n"
                              "    bytes[1:2]--\n"
                              "    [1]*ptr++\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("IntegerStore target=cell binding=cell bytes=1") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("PointerStore bytes=1") != std::string::npos);
}

HS_TEST(Sema_LowersStandardTemplateMemberAsInteger) {
  auto result = analyzeSource("template Cell {\n"
                              "    value[1] as i8\n"
                              "}\n"
                              "func main() {\n"
                              "    new cell as Cell\n"
                              "    cell.value = 1\n"
                              "    return cell.value - 1\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
}

HS_TEST(Sema_RejectsPostfixIncrementForNonIntegerViews) {
  const auto expectRejected = [](const std::string &source) {
    auto result = analyzeSource(source);
    HS_EXPECT_TRUE(result.unit == nullptr);
    HS_EXPECT_EQ(result.diagnostics.size(), 1U);
    HS_EXPECT_TRUE(result.diagnostics[0].find(
                       "increment target must use an integer View") !=
                   std::string::npos);
  };

  expectRejected("func main() {\n"
                 "    new value[4] as f32\n"
                 "    value++\n"
                 "    return 0\n"
                 "}\n");
  expectRejected("func main() {\n"
                 "    new value[1] as bool\n"
                 "    value++\n"
                 "    return 0\n"
                 "}\n");
  expectRejected("func main() {\n"
                 "    new value[8] as addr\n"
                 "    value++\n"
                 "    return 0\n"
                 "}\n");
  expectRejected("template Cell {\n"
                 "    value[1]\n"
                 "}\n"
                 "func main() {\n"
                 "    new value as Cell\n"
                 "    value++\n"
                 "    return 0\n"
                 "}\n");
  expectRejected("func main() {\n"
                 "    new value[4] as cstr\n"
                 "    value++\n"
                 "    return 0\n"
                 "}\n");
  expectRejected("func main() {\n"
                 "    new value[4] as bytes\n"
                 "    value++\n"
                 "    return 0\n"
                 "}\n");
}

HS_TEST(Sema_ResolvesUserTemplatePrintFormatCandidate) {
  auto result =
      analyzeSource("template Vec2 {\n"
                    "    x[8] as f64\n"
                    "    y[8] as f64\n"
                    "}\n"
                    "impl Vec2 {\n"
                    "    op format(value as Vec2, out as addr) -> [4] {\n"
                    "        return 0\n"
                    "    }\n"
                    "}\n"
                    "func main() {\n"
                    "    new v as Vec2\n"
                    "    print(v as Vec2)\n"
                    "    return 0\n"
                    "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("ImplOp template=Vec2 op=format") !=
                 std::string::npos);
  HS_EXPECT_TRUE(
      dump.find("UserTemplateFormatCall callee=__hitsimple.implop.Vec2.0 "
                "sink=stdout resultBytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("print.format.") == std::string::npos);
}

HS_TEST(Sema_LowersUserTemplateFormatAsI32Expression) {
  auto result =
      analyzeSource("template FailFmt {\n"
                    "    value[4] as i32\n"
                    "}\n"
                    "impl FailFmt {\n"
                    "    op format(value as FailFmt, out as addr) -> [4] {\n"
                    "        return -7\n"
                    "    }\n"
                    "}\n"
                    "func main() {\n"
                    "    new value as FailFmt\n"
                    "    return print(value as FailFmt)\n"
                    "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(
      dump.find(
          "UserTemplateFormatCallExpr callee=__hitsimple.implop.FailFmt.0 "
          "sink=stdout bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("print.format.") == std::string::npos);
}

HS_TEST(Sema_LowersUserTemplateFprintfFormatAsI32Expression) {
  auto result =
      analyzeSource("template Marker {\n"
                    "    value[1]\n"
                    "}\n"
                    "impl Marker {\n"
                    "    op format(value as Marker, out as addr) -> [4] {\n"
                    "        new written = fput(out as handle, 'E')\n"
                    "        return written\n"
                    "    }\n"
                    "}\n"
                    "func main() {\n"
                    "    new file = fopen(\"/dev/null\", \"w\")\n"
                    "    new value as Marker\n"
                    "    return fprintf(file, value as Marker)\n"
                    "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(
      dump.find("UserTemplateFormatCallExpr callee=__hitsimple.implop.Marker.0 "
                "sink=file bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("print.format.") == std::string::npos);
}

HS_TEST(Sema_LowersInputAndFormattedOutputBuiltins) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[4]\n"
                              "    new count[4]\n"
                              "    count, x = scanf(\"%d\")\n"
                              "    print(\"%d\", x)\n"
                              "    new c = get()\n"
                              "    return c\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("InputCallStore callee=scanf") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("CountTarget name=count") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("ScanTarget name=x") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("Call callee=print") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=get bytes=4") != std::string::npos);
}

HS_TEST(Sema_LowersScanfStringWithRuntimeCapacityDescriptor) {
  auto result = analyzeSource("func main() {\n"
                              "    new text[8]\n"
                              "    _, text = scanf(\"%s\")\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("InputCallStore callee=scanf") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("StringLiteral value=\"%s\"") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("ScanTarget name=text binding=text bytes=8") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersDynamicStartLengthModeSlice) {
  auto result = analyzeSource("func main() {\n"
                              "    new data[8]\n"
                              "    new i[4] = 2\n"
                              "    new x[2] = data[i:+2]\n"
                              "    return x\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("DerefExpr bytes=2") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("VariableRef name=i binding=i bytes=4") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersTwoByteFloatConversion) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[4] = 42\n"
                              "    new f[2] %f= to_f16(x)\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("FloatStore target=f binding=f bytes=2") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("ToFloatExpr bytes=2") != std::string::npos);
}

HS_TEST(Sema_LowersStageFStringAndBoolStores) {
  auto result = analyzeSource("func main() {\n"
                              "    new text[5] %s= \"HelloWorld\"\n"
                              "    new flag[4] %b= true\n"
                              "    flag %b= text == 0\n"
                              "    return flag\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("StringStore target=text binding=text bytes=5 "
                           "storage=local value=\"HelloWorld\"") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("BoolStore target=flag binding=flag bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerLiteral value=1 bytes=1") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=== bytes=1") != std::string::npos);
}

HS_TEST(Sema_LowersCstrDefaultAssignmentMatrixEntry) {
  auto result = analyzeSource("func main() {\n"
                              "    new text[8] as cstr = \"Kai\"\n"
                              "    text = \"HitSimple\"\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=text binding=text bytes=8 "
                           "storage=local template=cstr") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("StringStore target=text binding=text bytes=8 "
                           "storage=local value=\"Kai\"") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("StringStore target=text binding=text bytes=8 "
                           "storage=local value=\"HitSimple\"") !=
                 std::string::npos);
}

HS_TEST(Sema_DecodesEscapedCharAndStringLiteralBytes) {
  auto result = analyzeSource("func main() {\n"
                              "    new text %s= \"\\x41\\101\\u00E9\"\n"
                              "    new ch = '\\x41'\n"
                              "    new wide = '\\u00E9'\n"
                              "    return ch\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=text binding=text bytes=5") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=ch binding=ch bytes=1") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerLiteral value=65 bytes=1") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=wide binding=wide bytes=2") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerLiteral value=43459 bytes=2") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsInvalidOctalEscapes) {
  auto octal = analyzeSource("func main() {\n"
                             "    new text[4] %s= \"\\400\"\n"
                             "    return 0\n"
                             "}\n");
  HS_EXPECT_TRUE(octal.unit == nullptr);
  HS_EXPECT_EQ(octal.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(octal.diagnostics[0].find("octal escape is out of range") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersStringAssignmentFromMemorySource) {
  auto result = analyzeSource("func main() {\n"
                              "    new source[8] %s= \"Hello\"\n"
                              "    new target[8]\n"
                              "    target %s= source\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("StringCopyStore target=target binding=target "
                           "bytes=8 storage=local source=source") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersStageFChainMultiIgnoreAndAnnotatedTargets) {
  auto result = analyzeSource("func main() {\n"
                              "    new a[4], b[4], c[4], text[8]\n"
                              "    a = b = 3\n"
                              "    a, _, (text %s=) = b, c, \"ok\"\n"
                              "    return a\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("AssignmentExpr bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerStore target=b binding=b bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerStore target=a binding=a bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("StringStore target=text binding=text bytes=8") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsMultiAssignmentCountMismatch) {
  auto result = analyzeSource("func main() {\n"
                              "    new a[4], b[4]\n"
                              "    a, b = 1, 2, 3\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("assignment target count does not "
                                            "match value count") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsNonIntegerBoolAssignmentSource) {
  auto result = analyzeSource("func main() {\n"
                              "    new flag[1]\n"
                              "    flag %b= \"true\"\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("right operand of '%b=' is not an "
                                            "integer expression") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersAddressRebindingAndDereference) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[4]\n"
                              "    new ptr[8]\n"
                              "    ptr &= &x\n"
                              "    [4]*ptr = 42\n"
                              "    x = [4]*ptr\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("AddressOf name=x binding=x targetBytes=4 "
                           "storage=local bytes=8") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("PointerStore bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("DerefExpr bytes=4") != std::string::npos);
}

HS_TEST(Sema_LowersAddressOfIndexedMemoryForLibraryCalls) {
  auto result = analyzeSource(
      "func main() {\n"
      "    new buffer[4] = 0x03020100\n"
      "    new copied as addr = memcpy(&buffer[1], &buffer[0], 3)\n"
      "    return 0\n"
      "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=memcpy bytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=+ bytes=8") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("AddressOf name=buffer binding=buffer targetBytes=4 "
                           "storage=local bytes=8") != std::string::npos);
}

HS_TEST(Sema_LowersConcreteMinOverloadsAndResultTemplates) {
  auto result =
      analyzeSource("func main() {\n"
                    "    new signed as i16 = 1\n"
                    "    new unsigned as u8 = 1\n"
                    "    new floating as f32 = 1.0\n"
                    "    new signed_result as i16 = min(signed, signed)\n"
                    "    new unsigned_result as u8 = max(unsigned, unsigned)\n"
                    "    new floating_result as f32 = min(floating, floating)\n"
                    "    return 0\n"
                    "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=min bytes=2 builtin=49 "
                           "provider=Intrinsic returnRule=ArgumentLength "
                           "overload=1 template=i16") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=max bytes=1 builtin=50 "
                           "provider=Intrinsic returnRule=ArgumentLength "
                           "overload=4 template=u8") != std::string::npos);
  HS_EXPECT_TRUE(
      dump.find("CallExpr callee=min bytes=4 floating=true builtin=49 "
                "provider=Intrinsic returnRule=ArgumentLength "
                "overload=9 template=f32") != std::string::npos);
}

HS_TEST(Sema_PropagatesNestedFloatingBuiltinTemplates) {
  auto result = analyzeSource(
      "func main() {\n"
      "    new value as f32 = 4.0\n"
      "    new minimum as f32 = min(f_sqrt(value), f_sqrt(value))\n"
      "    new integral as i32 = to_i32(f_sqrt(value))\n"
      "    return integral\n"
      "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(
      dump.find("CallExpr callee=min bytes=4 floating=true builtin=49 "
                "provider=Intrinsic returnRule=ArgumentLength "
                "overload=9 template=f32") != std::string::npos);
  HS_EXPECT_TRUE(
      dump.find("CallExpr callee=f_sqrt bytes=4 floating=true builtin=52 "
                "provider=Intrinsic returnRule=ArgumentLength "
                "overload=1 template=f32") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("ToIntExpr floatBytes=4 bytes=4") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersAddressRebindingDeclarationInitializer) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[4]\n"
                              "    new ptr[8] &= &x\n"
                              "    [4]*ptr = 42\n"
                              "    return x\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=ptr binding=ptr bytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerStore target=ptr binding=ptr bytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("AddressOf name=x binding=x targetBytes=4 "
                           "storage=local bytes=8") != std::string::npos);
}

HS_TEST(Sema_LowersCompoundAssignmentForDereferenceTarget) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[4]\n"
                              "    new ptr[8] &= &x\n"
                              "    [4]*ptr = 40\n"
                              "    [4]*ptr %4d+= 2\n"
                              "    return x\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("PointerStore bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=%4d+ bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("DerefExpr bytes=4") != std::string::npos);
}

HS_TEST(Sema_RejectsNonPointerSizedAddressRebindingTarget) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[4]\n"
                              "    x &= &x\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("must be pointer-sized") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersDynamicMemoryBridgeCalls) {
  auto result = analyzeSource("func main() {\n"
                              "    new ptr = alloc(4)\n"
                              "    [4]*ptr = 42\n"
                              "    ptr = realloc(ptr, 8)\n"
                              "    free(ptr)\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=alloc bytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=realloc bytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("Call callee=free") != std::string::npos);
}

HS_TEST(Sema_RejectsStaticNullDereference) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[4]\n"
                              "    x = [4]*0\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("null address dereference") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersSignedAndBitwiseExpressions) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[4] = (1 << 3) | 2\n"
                              "    new y[4] = ~0\n"
                              "    return x + y\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=<< bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=| bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("UnaryExpr op=~ bytes=4") != std::string::npos);
}

HS_TEST(Sema_LowersComparisonExpressionsToOneByteResults) {
  auto result = analyzeSource("func main() {\n"
                              "    new a[4] = 1\n"
                              "    new b[4] = 2\n"
                              "    new flag[1]\n"
                              "    flag = a < b\n"
                              "    flag = a != b\n"
                              "    return flag\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=< bytes=1") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=!= bytes=1") != std::string::npos);
}

HS_TEST(Sema_RejectsModuloByZeroConstantExpression) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[4] = 1 % 0\n"
                              "    return x\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("division by zero") !=
                 std::string::npos);
}

HS_TEST(Sema_AllowsStringAssignmentTruncationToTargetBytes) {
  auto result = analyzeSource("func main() {\n"
                              "    new text[2] %s= \"abc\"\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("StringStore target=text binding=text bytes=2") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersAddressOfGlobalAndStaticStorage) {
  auto result = analyzeSource("new global[4]\n"
                              "func main() {\n"
                              "    static local[4]\n"
                              "    new gptr[8]\n"
                              "    new lptr[8]\n"
                              "    gptr &= &global\n"
                              "    lptr &= &local\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("AddressOf name=global binding=global "
                           "targetBytes=4 storage=global bytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("AddressOf name=local binding=local "
                           "targetBytes=4 storage=static bytes=8") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersHandleFileIoAssignmentComparisonAndFormatting) {
  auto result =
      analyzeSource("func main() {\n"
                    "    new file = fopen(\"/tmp/hitsimple-handle\", \"w\")\n"
                    "    new copy as handle = file\n"
                    "    new equal as bool = file == copy\n"
                    "    print(file as handle)\n"
                    "    new status[4] = fclose(copy)\n"
                    "    return equal\n"
                    "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=file binding=file bytes=8 "
                           "storage=local template=handle") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=copy binding=copy bytes=8 "
                           "storage=local template=handle") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=== bytes=1") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("Call callee=print") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=fclose bytes=4") !=
                 std::string::npos);
}
