#include "support/TestRunner.h"

#include "hitsimple/codegen/LLVMCodegen.h"
#include "hitsimple/parser/Parser.h"
#include "hitsimple/sema/Sema.h"

#include <memory>
#include <string>
#include <string_view>
#include <optional>
#include <utility>
#include <vector>

namespace {

std::vector<hitsimple::stdlib::StandardHeader> allStandardHeaders() {
  const auto headers = hitsimple::stdlib::allStandardHeaders();
  return {headers.begin(), headers.end()};
}

hitsimple::codegen::EmitResult emitSource(
    std::string_view source,
    hitsimple::codegen::CodegenOptions options = {}) {
  auto parseResult = hitsimple::parser::parseSource(source, "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());

  auto analyzeResult = hitsimple::sema::analyze(
      *parseResult.unit,
      hitsimple::sema::AnalyzeOptions{true, allStandardHeaders()});
  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());

  return hitsimple::codegen::emitLlvmIr(*analyzeResult.unit, "test.hs",
                                        options);
}

hitsimple::sema::AnalyzeResult analyzeSource(std::string_view source) {
  auto parseResult = hitsimple::parser::parseSource(source, "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());
  return hitsimple::sema::analyze(
      *parseResult.unit,
      hitsimple::sema::AnalyzeOptions{true, allStandardHeaders()});
}

hitsimple::codegen::EmitResult emitSourceWithoutStandardHeaders(
    std::string_view source) {
  auto parseResult = hitsimple::parser::parseSource(source, "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());

  auto analyzeResult = hitsimple::sema::analyze(
      *parseResult.unit, hitsimple::sema::AnalyzeOptions{true, {}});
  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());

  return hitsimple::codegen::emitLlvmIr(*analyzeResult.unit, "test.hs");
}

hitsimple::codegen::CodegenOptions
optionsFor(hitsimple::codegen::SafetyMode safetyMode) {
  hitsimple::codegen::CodegenOptions options;
  options.safetyMode = safetyMode;
  return options;
}

hitsimple::hir::FunctionAbiSignature cCompatibilitySignature(
    std::vector<hitsimple::hir::AbiType> parameterTypes,
    std::vector<hitsimple::hir::AbiType> returnTypes) {
  hitsimple::hir::FunctionAbiSignature signature{
      std::move(parameterTypes), std::move(returnTypes)};
  signature.isCCompatibility = true;
  return signature;
}

constexpr std::string_view minimalProgram = "func main() {\n"
                                            "    new x[1]\n"
                                            "    x %d= 42\n"
                                            "    printf(\"%d\\n\", x)\n"
                                            "    return 0\n"
                                            "}\n";

} // namespace

HS_TEST(LLVMCodegen_EmitsMinimalProgramIr) {
  auto result = emitSource(minimalProgram);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(!result.llvmIr.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("define i32 @main()") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("alloca [1 x i8]") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i8 42") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("declare i32 @hs_format_output") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call i32 @hs_format_output") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("ret i32 0") != std::string::npos);
}

HS_TEST(LLVMCodegen_UsesNativeDebugFormatForTargetTriple) {
  const std::vector<std::pair<std::string, hitsimple::codegen::DebugInfoFormat>>
      targets = {
          {"x86_64-unknown-linux-gnu",
           hitsimple::codegen::DebugInfoFormat::Dwarf},
          {"x86_64-apple-darwin", hitsimple::codegen::DebugInfoFormat::Dwarf},
          {"x86_64-w64-windows-gnu",
           hitsimple::codegen::DebugInfoFormat::CodeView},
  };

  for (const auto& [targetTriple, expectedFormat] : targets) {
    HS_EXPECT_EQ(
        hitsimple::codegen::debugInfoFormatForTargetTriple(targetTriple),
        expectedFormat);
  }
}

HS_TEST(LLVMCodegen_LowersUserTemplateOperatorWithInternalViewAbi) {
  auto result = emitSource("template Vec2 {\n"
                           "    x[8] as f64\n"
                           "    y[8] as f64\n"
                           "}\n"
                           "impl Vec2 {\n"
                           "    op + (lhs as Vec2, rhs as Vec2) -> [16] {\n"
                           "        new result as Vec2\n"
                           "        result.x %f= lhs.x %f+ rhs.x\n"
                           "        result.y %f= lhs.y %f+ rhs.y\n"
                           "        return result\n"
                           "    }\n"
                           "}\n"
                           "func main() {\n"
                           "    new lhs as Vec2\n"
                           "    new rhs as Vec2\n"
                           "    new out as Vec2 = lhs + rhs\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("define internal void @__hitsimple.implop") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call void @__hitsimple.implop") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_LowersImplTemplateMethodWithInternalViewAbi) {
  auto result = emitSource("template Counter {\n"
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

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find(
                     "define internal void @__hitsimple.implmethod.Counter.0") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find(
                     "call void @__hitsimple.implmethod.Counter.0") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_ReportsMissingReturnInImplMethodAsViewAbiDiagnostic) {
  auto analyzeResult = analyzeSource("template Counter {\n"
                                     "    value[4] as i32\n"
                                     "}\n"
                                     "impl Counter {\n"
                                     "    func identity(self as Counter) -> as Counter {\n"
                                     "    }\n"
                                     "}\n"
                                     "func main() {\n"
                                     "    return 0\n"
                                     "}\n");

  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());
  auto result =
      hitsimple::codegen::emitLlvmIr(*analyzeResult.unit, "test.hs");
  HS_EXPECT_TRUE(result.llvmIr.empty());
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "missing return in internal View ABI function "
                     "'__hitsimple.implmethod.Counter.0'") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_MaterializesInternalViewAbiReturnToDeclaredLength) {
  auto result = emitSource("template Vec2 {\n"
                           "    x[8] as f64\n"
                           "    y[8] as f64\n"
                           "}\n"
                           "impl Vec2 {\n"
                           "    op format(value as Vec2, out as addr) -> [4] {\n"
                           "        return 0\n"
                           "    }\n"
                           "}\n"
                           "func main() {\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("define internal void @__hitsimple.implop") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_CallsBoundUserTemplateFormatWithStdoutSink) {
  auto result = emitSource("template Vec2 {\n"
                           "    x[8] as f64\n"
                           "    y[8] as f64\n"
                           "}\n"
                           "impl Vec2 {\n"
                           "    op format(value as Vec2, out as addr) -> [4] {\n"
                           "        printf(\"Vec2\\n\")\n"
                           "        return 5\n"
                           "    }\n"
                           "}\n"
                           "func main() {\n"
                           "    new value as Vec2\n"
                           "    print(value as Vec2)\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find(
                     "call void @__hitsimple.implop.Vec2.0") !=
                 std::string::npos);
#if defined(__APPLE__)
  HS_EXPECT_TRUE(result.llvmIr.find("@__stdoutp") != std::string::npos);
#else
  HS_EXPECT_TRUE(result.llvmIr.find("@stdout") != std::string::npos);
#endif
  HS_EXPECT_TRUE(result.llvmIr.find("print.format.") == std::string::npos);
}

HS_TEST(LLVMCodegen_MaterializesNegativeUserTemplateFormatResult) {
  auto result = emitSource("template FailFmt {\n"
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

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("-7") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find(
                     "call void @__hitsimple.implop.FailFmt.0") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("format.result.value") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_MaterializesUserTemplateFprintfFormatResult) {
  auto result = emitSource("template Marker {\n"
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

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find(
                     "call void @__hitsimple.implop.Marker.0") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("format.file") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("format.result.value") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("print.format.") == std::string::npos);
}

HS_TEST(LLVMCodegen_CallsBoundUserTemplateFormatWithFprintfSink) {
  auto result = emitSource("template Marker {\n"
                           "    value[1]\n"
                           "}\n"
                           "impl Marker {\n"
                           "    op format(value as Marker, out as addr) -> [4] {\n"
                           "        new written = fput(out as handle, 'F')\n"
                           "        return written\n"
                           "    }\n"
                           "}\n"
                           "func main() {\n"
                           "    new file = fopen(\"/dev/null\", \"w\")\n"
                           "    new value as Marker\n"
                           "    fprintf(file, value as Marker)\n"
                           "    new closed = fclose(file)\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find(
                     "call void @__hitsimple.implop.Marker.0") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("fput.file") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("print.format.") == std::string::npos);
}

HS_TEST(LLVMCodegen_DefaultsUnannotatedMainToI32Zero) {
  auto result = emitSource("func main() {\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("define i32 @main()") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("ret i32 0") != std::string::npos);
}

HS_TEST(LLVMCodegen_PreservesUnannotatedOrdinaryReturnInference) {
  auto result = emitSource("func helper() {\n"
                           "    return 1\n"
                           "}\n"
                           "func main() {\n"
                           "    return helper()\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("define i8 @helper()") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_ReportsMissingReturnForExplicitNonI32Main) {
  auto result = emitSource("func main() -> [1] {\n"
                           "}\n");

  HS_EXPECT_TRUE(result.llvmIr.empty());
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "missing return in function 'main'") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_KeepsExplicitNonI32MainReturnType) {
  auto result = emitSource("func main() -> [1] {\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("define i8 @main()") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("ret i8 0") != std::string::npos);
}

HS_TEST(LLVMCodegen_RejectsEmptyReturnForI8MainHir) {
  std::vector<std::unique_ptr<hitsimple::hir::Stmt>> statements;
  statements.push_back(std::make_unique<hitsimple::hir::Return>(
      std::vector<std::unique_ptr<hitsimple::hir::Expr>>{}));

  std::vector<std::unique_ptr<hitsimple::hir::Function>> functions;
  functions.push_back(std::make_unique<hitsimple::hir::Function>(
      "main", std::vector<hitsimple::hir::Parameter>{},
      std::vector<std::size_t>{1},
      std::make_unique<hitsimple::hir::Block>(std::move(statements))));
  hitsimple::hir::TranslationUnit unit(std::move(functions));

  const auto result = hitsimple::codegen::emitLlvmIr(unit, "test.hs");

  HS_EXPECT_TRUE(result.llvmIr.empty());
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "function return type is not void") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find("invalid LLVM IR") ==
                 std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsTypedBinaryIntegerOps) {
  auto result = emitSource("func main() {\n"
                           "    new x[1]\n"
                           "    x %d= 40 %d+ 2\n"
                           "    printf(\"%d\\n\", x %d+ 2)\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("add i32") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find(", 2") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i8") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsFormattedIoBuiltins) {
  auto result = emitSource("func main() {\n"
                           "    new x[4]\n"
                           "    new count[4]\n"
                           "    count, x = scanf(\"%d\")\n"
                           "    print(\"%d\\n\", x)\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find(
                     "declare i32 @hs_scan_input(ptr, ptr, ptr, i64, i32)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call i32 @hs_scan_input") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("scan.targets") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i32") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call i32 @hs_format_output") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsPrintfStringPointersAsVarargPointers) {
  auto result = emitSource("func main() {\n"
                           "    new ptr = calloc(1, 8)\n"
                           "    printf(\"%s\", ptr)\n"
                           "    free(ptr)\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("call i32 @hs_format_output") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("inttoptr i64") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsFileFormattedIoBuiltins) {
  auto result = emitSource("func main() {\n"
                           "    new file = fopen(\"/tmp/hitsimple-missing\", \"r\")\n"
                           "    new x[4]\n"
                           "    new count[4]\n"
                           "    count, x = fscanf(file, \"%d\")\n"
                           "    fprintf(file, \"%d\", x)\n"
                           "    new c = fget(file)\n"
                           "    return c\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find(
                     "declare i32 @hs_scan_input(ptr, ptr, ptr, i64, i32)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("declare i32 @hs_format_output") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("declare i32 @fgetc(ptr)") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_PromotesFormattedFloatVariablesForVarargs) {
  auto result = emitSource("func main() {\n"
                           "    new half as f16 = 0.5\n"
                           "    new narrow as f32 = 1.5\n"
                           "    new wide as f64 = 2.5\n"
                           "    new file = fopen(\"/tmp/hitsimple-float\", \"w\")\n"
                           "    printf(\"%f\", wide)\n"
                           "    print(wide as f64)\n"
                           "    fprintf(file, \"%f\", half)\n"
                           "    fprintf(file, \"%f\", narrow)\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("load double") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("format.float") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call i32 @hs_format_output") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_ReinterpretsTemporaryTemplateViewWithoutConversion) {
  auto result = emitSource("func main() {\n"
                           "    new bits as u32 = 0x3f800000\n"
                           "    new value as f32 = bits as f32\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("load float") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("sitofp") == std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("uitofp") == std::string::npos);
}

HS_TEST(LLVMCodegen_RejectsMismatchedFormatArgumentKindsForFprintfCall) {
  std::vector<std::unique_ptr<hitsimple::hir::Expr>> arguments;
  arguments.push_back(
      std::make_unique<hitsimple::hir::IntegerLiteral>("0", sizeof(void *)));
  arguments.push_back(
      std::make_unique<hitsimple::hir::StringLiteral>("\"%f\""));
  arguments.push_back(
      std::make_unique<hitsimple::hir::FloatLiteral>("1.5", 8));

  std::vector<std::unique_ptr<hitsimple::hir::Stmt>> statements;
  statements.push_back(std::make_unique<hitsimple::hir::Call>(
      "fprintf", std::move(arguments), hitsimple::stdlib::BuiltinId::Fprintf,
      std::vector<hitsimple::hir::FormatArgKind>{
          hitsimple::hir::FormatArgKind::Bytes,
          hitsimple::hir::FormatArgKind::String}));
  std::vector<std::unique_ptr<hitsimple::hir::Expr>> returnValues;
  returnValues.push_back(
      std::make_unique<hitsimple::hir::IntegerLiteral>("0", 4));
  statements.push_back(
      std::make_unique<hitsimple::hir::Return>(std::move(returnValues)));

  std::vector<std::unique_ptr<hitsimple::hir::Function>> functions;
  functions.push_back(std::make_unique<hitsimple::hir::Function>(
      "main", std::vector<hitsimple::hir::Parameter>{},
      std::vector<std::size_t>{4},
      std::make_unique<hitsimple::hir::Block>(std::move(statements))));
  hitsimple::hir::TranslationUnit unit(std::move(functions));

  const auto result = hitsimple::codegen::emitLlvmIr(unit, "test.hs");

  HS_EXPECT_TRUE(result.llvmIr.empty());
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "format output argument kinds do not match call arguments") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_RejectsMismatchedFormatArgumentKindsForPrintfCallExpr) {
  std::vector<std::unique_ptr<hitsimple::hir::Expr>> arguments;
  arguments.push_back(
      std::make_unique<hitsimple::hir::StringLiteral>("\"%f\""));
  arguments.push_back(
      std::make_unique<hitsimple::hir::FloatLiteral>("1.5", 8));

  std::vector<std::unique_ptr<hitsimple::hir::Expr>> returnValues;
  returnValues.push_back(std::make_unique<hitsimple::hir::CallExpr>(
      "printf", std::move(arguments), 4, false,
      hitsimple::stdlib::BuiltinId::Printf,
      std::vector<hitsimple::hir::FormatArgKind>{
          hitsimple::hir::FormatArgKind::String}));
  std::vector<std::unique_ptr<hitsimple::hir::Stmt>> statements;
  statements.push_back(
      std::make_unique<hitsimple::hir::Return>(std::move(returnValues)));

  std::vector<std::unique_ptr<hitsimple::hir::Function>> functions;
  functions.push_back(std::make_unique<hitsimple::hir::Function>(
      "main", std::vector<hitsimple::hir::Parameter>{},
      std::vector<std::size_t>{4},
      std::make_unique<hitsimple::hir::Block>(std::move(statements))));
  hitsimple::hir::TranslationUnit unit(std::move(functions));

  const auto result = hitsimple::codegen::emitLlvmIr(unit, "test.hs");

  HS_EXPECT_TRUE(result.llvmIr.empty());
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "format output argument kinds do not match call arguments") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsTwoByteFloatIr) {
  auto result = emitSource("func main() {\n"
                           "    new x[4] = 42\n"
                           "    new f[2] %f= to_f16(x)\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("sitofp i32") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("to half") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store half") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsCompoundAssignmentAsLoadOpStore) {
  auto result = emitSource("func main() {\n"
                           "    new x[1]\n"
                           "    x %d= 40\n"
                           "    x %d+= 2\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("load i8") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("sext i8") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("add i32") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i8") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsControlFlowBranches) {
  auto result = emitSource("func main() {\n"
                           "    new x[1]\n"
                           "    x %d= 1\n"
                           "    while (x) {\n"
                           "        if (x) {\n"
                           "            x %d= 0\n"
                           "            continue\n"
                           "        } else {\n"
                           "            break\n"
                           "        }\n"
                           "    }\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("while.cond") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("while.body") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("while.end") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("if.then") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("if.else") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("br i1") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsStageHForGotoAndTryCatch) {
  auto result = emitSource("func main() {\n"
                           "    new sum[4]\n"
                           "    sum = 0\n"
                           "    for (new i[4] = 0; i < 3; i++) {\n"
                           "        if (i == 1) {\n"
                           "            continue\n"
                           "        }\n"
                           "        sum = sum + i\n"
                           "    }\n"
                           "    try {\n"
                           "        throw 7\n"
                           "    } catch (err[4]) {\n"
                           "        sum = err\n"
                           "    }\n"
                           "    goto done\n"
                           "    sum = 99\n"
                           "    done: return sum\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("for.cond") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("for.post") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("label.done") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("try.body") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("try.catch") != std::string::npos);
}

HS_TEST(LLVMCodegen_LowersFloatingThrowThroughCatchDelivery) {
  auto result = emitSource("func main() {\n"
                           "    new value as f64 = 42.5\n"
                           "    try {\n"
                           "        throw value\n"
                           "    } catch (error as f64) {\n"
                           "        new observed as f64 = error\n"
                           "    }\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("try.catch") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find(
                     "store double %value.float, ptr %error.addr") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_UsesTrapForUncaughtViewThrow) {
  auto result = emitSource("func main() {\n"
                           "    new value as f64 = 42.5\n"
                           "    throw value\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("llvm.trap") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsGlobalMemoryAccess) {
  auto result = emitSource("new global_count[4]\n"
                           "func main() {\n"
                           "    global_count %d= 7\n"
                           "    return global_count\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("@global_count = global "
                                    "[4 x i8] zeroinitializer") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i32 7") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("load i32") != std::string::npos);
}

HS_TEST(LLVMCodegen_RegistersOrderedGlobalInitializers) {
  auto result = emitSource("new global_value[4] = seed()\n"
                           "new global_ratio as f64 = 1.5\n"
                           "new global_name[8] as cstr = \"Kai\"\n"
                           "func seed() -> [4] {\n"
                           "    return 42\n"
                           "}\n"
                           "func main() {\n"
                           "    return global_value - 42\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("define internal void @__hitsimple.global.init()") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@llvm.global_ctors = appending global") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call i32 @seed()") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store double 1.500000e+00") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsShadowedLocalsWithDistinctStorage) {
  auto result = emitSource("func main() {\n"
                           "    new x[1]\n"
                           "    x %d= 2\n"
                           "    if (x) {\n"
                           "        new x[2]\n"
                           "        x %d= 3\n"
                           "    }\n"
                           "    return x\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("%x = alloca [1 x i8]") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("%x.1 = alloca [2 x i8]") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i8 2") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i16 3") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsStageEIntegerExpressions) {
  auto result = emitSource("func main() {\n"
                           "    new x[4]\n"
                           "    x = 2\n"
                           "    x = (x + 2) * (x - 1) << 1\n"
                           "    x = x ** 3\n"
                           "    x = x? >= 8 ? ~x : -'a'\n"
                           "    x = true && !false || x == 0\n"
                           "    return x\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("mul i32") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("add i32") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("shl i32") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("pow.cond") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("pow.body") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("icmp uge i32") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("ternary.then") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("xor i32") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("-97") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("logic.rhs") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("icmp eq i32") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsStageEFloatExpressions) {
  auto result = emitSource("func main() {\n"
                           "    new x[4] %f= 1.5\n"
                           "    new y[4] %f= 2.25\n"
                           "    x %f= (x %f+ y) %f* 2.0\n"
                           "    return sizeof(x)\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("store float") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("load float") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("fadd float") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("fmul float") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("ret i32 4") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsStageEConversionFunctions) {
  auto result = emitSource("func main() {\n"
                           "    new x[4] = 42\n"
                           "    new f[4] %f= to_f32(x)\n"
                           "    new y[2] = to_i16(f)\n"
                           "    return y\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("sitofp i32") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("fptosi float") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i16") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsFloatMathHelpers) {
  auto result = emitSource("func main() {\n"
                           "    new x[4] %f= 1.5\n"
                           "    new y[4] %f= f_abs(x)\n"
                           "    new z[8] %f= f_sqrt(4.0)\n"
                           "    new p[4] %f= f_pow(x, 2.0)\n"
                           "    new s[4] %f= f_sin(x)\n"
                           "    new t[4] %f= f_tan(x)\n"
                           "    new c[8] %f= f_ceil(z)\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("call float @llvm.fabs.f32") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call double @llvm.sqrt.f64") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call float @llvm.pow.f32") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call float @llvm.sin.f32") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call float @tanf(float") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call double @llvm.ceil.f64") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_UsesF16AndF128MathFallbacks) {
  hitsimple::codegen::CodegenOptions options;
#if defined(__aarch64__) || defined(_M_ARM64)
  options.targetTriple = "aarch64-unknown-linux-gnu";
#else
  options.targetTriple = "x86_64-unknown-linux-gnu";
#endif
  auto result = emitSource(
      "func main() {\n"
      "    new half as f16 = 1.5\n"
      "    new halfResult as f16 = f_sqrt(half)\n"
      "    new quad as f128 = 1.234567890123456789012345678901234\n"
      "    new quadResult as f128 = f_sin(quad)\n"
      "    return 0\n"
      "}\n",
      options);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("fpext half") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("fptrunc float") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("declare fp128 @hs_f128_literal(ptr)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("declare fp128 @hs_f128_sin(fp128)") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_LowersWindowsF128ThroughBitPatternRuntime) {
  hitsimple::codegen::CodegenOptions options;
#if defined(__aarch64__) || defined(_M_ARM64)
  options.targetTriple = "aarch64-w64-windows-gnu";
#else
  options.targetTriple = "x86_64-w64-windows-gnu";
#endif
  auto result = emitSource(
      "func main() {\n"
      "    new left as f128 = 1.5\n"
      "    new integer[8] = 2\n"
      "    new right as f128 = to_f128(integer)\n"
      "    new sum as f128 = left %f+ right\n"
      "    new root as f128 = f_sqrt(sum)\n"
      "    new smaller[1] %b= left < right\n"
      "    new narrowed as f64 = to_f64(root)\n"
      "    return 0\n"
      "}\n",
      options);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("target triple = \"" + options.targetTriple +
                                    "\"") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("declare void @hs_f128_literal(ptr, ptr)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call void @hs_f128_from_i64(ptr") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("declare void @hs_f128_add(ptr, ptr, ptr)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call void @hs_f128_sqrt(ptr") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("declare i8 @hs_f128_lt(ptr, ptr)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("declare double @hs_f128_to_f64(ptr)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("declare i128 @hs_f128_") ==
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("fadd fp128") == std::string::npos);
}

HS_TEST(LLVMCodegen_LowersDarwinF128ThroughBitPatternRuntime) {
  hitsimple::codegen::CodegenOptions options;
#if defined(__aarch64__) || defined(_M_ARM64)
  options.targetTriple = "arm64-apple-darwin";
#else
  options.targetTriple = "x86_64-apple-darwin";
#endif
  auto result = emitSource(
      "func main() {\n"
      "    new value as f128 = 1.5\n"
      "    new root as f128 = f_sqrt(value)\n"
      "    new narrowed as f64 = to_f64(root)\n"
      "    return 0\n"
      "}\n",
      options);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("target triple = \"" + options.targetTriple +
                                    "\"") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find(
                     "declare void @hs_f128_literal(ptr, ptr)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call void @hs_f128_sqrt(ptr") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("declare double @hs_f128_to_f64(ptr)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("declare fp128 @hs_f128_") ==
                 std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsStageFStringAndBoolStores) {
  auto result = emitSource("func main() {\n"
                           "    new text[5] %s= \"HelloWorld\"\n"
                           "    new x[4] = 42\n"
                           "    new flag[4] %b= 42\n"
                           "    flag %b= x\n"
                           "    return flag\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("store i8 72") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i8 101") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i8 108") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i8 0") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("icmp ne i32") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("zext i1") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsPrefixedIntegerLiterals) {
  auto result = emitSource("func main() {\n"
                           "    new x[4] = 0xff\n"
                           "    new y[4] = 0b1010_0001\n"
                           "    new z[4] = 0o377\n"
                           "    return x\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("store i32 255") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i32 161") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i32 255") != std::string::npos);
}

HS_TEST(LLVMCodegen_TruncatesIntegerStoresToNarrowTargets) {
  auto result = emitSource("func main() {\n"
                           "    new wide[4] = 255\n"
                           "    new narrow[1]\n"
                           "    narrow = wide\n"
                           "    return narrow\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("trunc i32") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i8") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsEscapedStringBytes) {
  auto result = emitSource("func main() {\n"
                           "    new text[5] %s= \"\\x41\\101\\u00E9\"\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("store i8 65") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i8 -61") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i8 -87") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i8 0") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsAddressRebindAndDereference) {
  auto result = emitSource("func main() {\n"
                           "    new x[4]\n"
                           "    new ptr[8]\n"
                           "    ptr &= &x\n"
                           "    [4]*ptr = 42\n"
                           "    return [4]*ptr\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("ptrtoint") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("inttoptr") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i32 42") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("load i32") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsIndexReadAndWrite) {
  auto result = emitSource("func main() {\n"
                           "    new x[4]\n"
                           "    x[0] = 65\n"
                           "    x[1] = 66\n"
                           "    return x[1]\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("store i8 65") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i8 66") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("load i8") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsSliceReadAndWrite) {
  auto result = emitSource("func main() {\n"
                           "    new x[8]\n"
                           "    new y[4]\n"
                           "    x[0:4] = 42\n"
                           "    y = x[0:+4]\n"
                           "    return y\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("store i32 42") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("load i32") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsDynamicMemoryBridgeCalls) {
  auto result = emitSource("func main() {\n"
                           "    new ptr = alloc(4)\n"
                           "    [4]*ptr = 42\n"
                           "    ptr = realloc(ptr, 8)\n"
                           "    free(ptr)\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("@malloc") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@realloc") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@free") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i32 42") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsStandardLibraryCoreCalls) {
  auto result = emitSource("func main() {\n"
                           "    new ptr = calloc(1, 8)\n"
                           "    memset(ptr, 0, 8)\n"
                           "    new len = strlen(\"AB\")\n"
                           "    new cmp[4] = memcmp(ptr, ptr, 2)\n"
                           "    new off = strchr(\"AB\", 'B')\n"
                           "    new swapped[2] = byte_swap(0x1234)\n"
                           "    new lower[1] = 'a'\n"
                           "    new up[1] = to_upper(lower)\n"
                           "    free(ptr)\n"
                           "    return len\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("@calloc") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@memset") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@strlen") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@memcmp") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("declare ptr @strchr(ptr, i32)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call ptr @strchr") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_strchr_offset") ==
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("llvm.bswap") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@toupper") == std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("ctype.to_upper") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_LowersFputWithFileBeforeValue) {
  auto result = emitSource("func main() {\n"
                           "    new file = fopen(\"/dev/null\", \"w\")\n"
                           "    new written = fput(file, 'x')\n"
                           "    new closed = fclose(file)\n"
                           "    return written\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("declare i64 @fwrite(ptr, i64, i64, ptr)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call i64 @fwrite(ptr") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_DoesNotLowerUserFunctionWithStandardLibraryName) {
  auto result = emitSourceWithoutStandardHeaders(
      "func byte_swap(value[4]) -> [4] {\n"
      "    return value\n"
      "}\n"
      "func main() {\n"
      "    new input[4] = 0x12345678\n"
      "    new output[4] = byte_swap(input)\n"
      "    return output\n"
      "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("llvm.bswap") == std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("define i32 @byte_swap") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsStandardStringCallsAndRejectsLegacyCapacity) {
  auto result = emitSource("func main() {\n"
                           "    new dst[16]\n"
                           "    new src[16]\n"
                           "    new copied = strcpy(&dst, &src)\n"
                           "    new limited = strncpy(&dst, &src, 4)\n"
                           "    new appended = strcat(&dst, &src)\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("@strcpy") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@strncpy") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@strcat") != std::string::npos);

  auto legacy = analyzeSource("func main() {\n"
                              "    new dst[16]\n"
                              "    new src[16]\n"
                              "    new copied = strcpy(&dst, &src, 16)\n"
                              "    return 0\n"
                              "}\n");
  HS_EXPECT_TRUE(legacy.unit == nullptr);
  HS_EXPECT_TRUE(!legacy.diagnostics.empty());
  HS_EXPECT_TRUE(legacy.diagnostics[0].find("argument count does not match "
                                            "declaration") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsStandardCallocForm) {
  auto standard = emitSource("func main() {\n"
                             "    new ptr = calloc(1, 8)\n"
                             "    free(ptr)\n"
                             "    return 0\n"
                             "}\n");
  HS_EXPECT_TRUE(standard.diagnostics.empty());
  HS_EXPECT_TRUE(standard.llvmIr.find("@calloc") != std::string::npos);

  auto invalid = analyzeSource("func main() {\n"
                               "    new ptr = calloc(8)\n"
                               "    return 0\n"
                               "}\n");
  HS_EXPECT_TRUE(invalid.unit == nullptr);
  HS_EXPECT_TRUE(!invalid.diagnostics.empty());
}

HS_TEST(LLVMCodegen_EmitsCheckedRuntimeCalls) {
  auto options = optionsFor(hitsimple::codegen::SafetyMode::Checked);
  auto result = emitSource("func main() {\n"
                           "    new ptr = alloc(4)\n"
                           "    [4]*ptr = 42\n"
                           "    free(ptr)\n"
                           "    return 0\n"
                           "}\n",
                           options);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_alloc") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_check_store") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_free") != std::string::npos);
}

HS_TEST(LLVMCodegen_CheckedRegistersInternalObjectsAndFrames) {
  auto options = optionsFor(hitsimple::codegen::SafetyMode::Checked);
  auto result = emitSource("new global_value[4]\n"
                           "func main() {\n"
                           "    new local_value[4]\n"
                           "    new ptr[8]\n"
                           "    ptr &= &global_value\n"
                           "    [4]*ptr = 42\n"
                           "    return local_value\n"
                           "}\n",
                           options);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_register_static") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_register_local") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_frame_enter") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_frame_exit") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@llvm.global_ctors") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_CheckedDoesNotTrustExternGlobalAddresses) {
  auto options = optionsFor(hitsimple::codegen::SafetyMode::Checked);
  auto result = emitSource("extern host_buffer[4]\n"
                           "func main() {\n"
                           "    return [4]*&host_buffer\n"
                           "}\n",
                           options);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_check_load") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_register_static") ==
                 std::string::npos);
}

HS_TEST(LLVMCodegen_MaterializesDynamicViewsAndGenericByteSwap) {
  auto result = emitSource("func main() {\n"
                           "    new source[3] = 0x030201\n"
                           "    new requested[8] = 3\n"
                           "    new resized[3] = resize_bytes(source, requested)\n"
                           "    new swapped[3] = byte_swap(source)\n"
                           "    return resized + swapped\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("alloca i8, i64") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_reverse_bytes") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_CheckedUsesMemoryAndStringRuntimeBridges) {
  auto result = emitSource("func main() {\n"
                           "    new source[4] = 0x03020100\n"
                           "    new destination[4]\n"
                           "    new heap = calloc(1, 4)\n"
                           "    new copied = memcpy(destination, source, 4)\n"
                           "    new compared = memcmp(destination, source, 4)\n"
                           "    new textLength = strlen(\"ok\")\n"
                           "    free(heap)\n"
                           "    return copied + compared + textLength\n"
                           "}\n",
                           optionsFor(hitsimple::codegen::SafetyMode::Checked));

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_calloc") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_memcpy") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_memcmp") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_strlen") != std::string::npos);
}

HS_TEST(LLVMCodegen_UsesTypedRuntimeDescriptorsForNativeFormatting) {
  auto result = emitSource("func main() {\n"
                           "    new value[4] = 42\n"
                           "    printf(\"%d %s\\n\", value, \"ok\")\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_format_output") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("declare i32 @printf(ptr, ...)") ==
                 std::string::npos);
}

HS_TEST(LLVMCodegen_CheckedSkipsRuntimeChecksForStaticAddressRanges) {
  auto options = optionsFor(hitsimple::codegen::SafetyMode::Checked);
  auto result = emitSource("struct Pair {\n"
                           "    left[4]\n"
                           "    right[4]\n"
                           "}\n"
                           "func main() {\n"
                           "    new a[4]\n"
                           "    new pair[s1] ;Pair\n"
                           "    [4]*&a = 42\n"
                           "    [4]*&pair.right = [4]*&a\n"
                           "    return [4]*&pair.right\n"
                           "}\n",
                           options);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_check_store") == std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_check_load") == std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i32 42") != std::string::npos);
}

HS_TEST(LLVMCodegen_StaticCheckedDoesNotEmitRuntimeChecks) {
  auto options = optionsFor(hitsimple::codegen::SafetyMode::StaticChecked);
  auto result = emitSource("func main() {\n"
                           "    new ptr = alloc(4)\n"
                           "    [4]*ptr = 42\n"
                           "    ptr = realloc(ptr, 8)\n"
                           "    free(ptr)\n"
                           "    return 0\n"
                           "}\n",
                           options);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("@malloc") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@realloc") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@free") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_alloc") == std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_realloc") == std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_check_store") == std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@hs_free") == std::string::npos);
}

HS_TEST(LLVMCodegen_StaticCheckedRejectsStaticMemoryErrors) {
  auto index = emitSource("func main() {\n"
                          "    new a[4]\n"
                          "    a[4] = 1\n"
                          "    return 0\n"
                          "}\n",
                          optionsFor(hitsimple::codegen::SafetyMode::
                                         StaticChecked));
  HS_EXPECT_TRUE(!index.diagnostics.empty());
  HS_EXPECT_TRUE(index.diagnostics[0].find("out of bounds") !=
                 std::string::npos);

  auto nullStore = emitSource("func main() {\n"
                              "    new p as addr = 0\n"
                              "    [1]*p = 1\n"
                              "    return 0\n"
                              "}\n",
                              optionsFor(hitsimple::codegen::SafetyMode::
                                             StaticChecked));
  HS_EXPECT_TRUE(!nullStore.diagnostics.empty());
  HS_EXPECT_TRUE(nullStore.diagnostics[0].find("null address store") !=
                 std::string::npos);

  auto staticPointerStore = emitSource(
      "func main() {\n"
      "    new a[4]\n"
      "    [1]*(&a + 4) = 1\n"
      "    return 0\n"
      "}\n",
      optionsFor(hitsimple::codegen::SafetyMode::StaticChecked));
  HS_EXPECT_TRUE(!staticPointerStore.diagnostics.empty());
  HS_EXPECT_TRUE(staticPointerStore.diagnostics[0].find("memory store out of "
                                                        "bounds") !=
                 std::string::npos);

  auto staticPointerLoad = emitSource(
      "func main() {\n"
      "    new a[4]\n"
      "    return [1]*(&a + 4)\n"
      "}\n",
      optionsFor(hitsimple::codegen::SafetyMode::Checked));
  HS_EXPECT_TRUE(!staticPointerLoad.diagnostics.empty());
  HS_EXPECT_TRUE(staticPointerLoad.diagnostics[0].find("memory load out of "
                                                       "bounds") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsStageFAssignmentExpressionAndStringCopy) {
  auto result = emitSource("func main() {\n"
                           "    new a[4], b[4], source[8], target[8]\n"
                           "    source %s= \"Hello\"\n"
                           "    a = b = 7\n"
                           "    a, _, (target %s=) = b, 0, source\n"
                           "    return a\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("store i32 7") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("str.copy.active") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("str.copy.byte") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("ret i32") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsStageGFunctionsAndCalls) {
  auto result = emitSource("func add_one(value[4]) -> [4] {\n"
                           "    return value + 1\n"
                           "}\n"
                           "func main() {\n"
                           "    new x[4]\n"
                           "    x = add_one(41)\n"
                           "    return x\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("define i32 @add_one(i32") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call i32 @add_one") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("define i32 @main()") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsStageGMultiReturnCallStore) {
  auto result = emitSource("func pair(value[4]) -> ([4], [4]) {\n"
                           "    return value, value + 1\n"
                           "}\n"
                           "func main() {\n"
                           "    new a[4], b[4]\n"
                           "    a, b = pair(7)\n"
                           "    return b\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("define { i32, i32 } @pair(i32") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call { i32, i32 } @pair") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("extractvalue { i32, i32 }") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_PreservesTypedFloatingMultiReturnAbi) {
  auto result = emitSource("func pair(value as f64) -> (first as f64, second as f64) {\n"
                           "    return value, value %f+ 1.0\n"
                           "}\n"
                           "func main() {\n"
                           "    new first as f64\n"
                           "    new second as f64\n"
                           "    first, second = pair(40.0)\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("define { double, double } @pair(double") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call { double, double } @pair(double") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsStageIStructMemberOffsets) {
  auto result = emitSource("struct Pair {\n"
                           "    left[4]\n"
                           "    right[4]\n"
                           "}\n"
                           "func main() {\n"
                           "    new p[s2] ;Pair\n"
                           "    p.left = 3\n"
                           "    p[s1].right = sizeof(Pair)\n"
                           "    return p[s1].right\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("alloca [16 x i8]") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("getelementptr inbounds [16 x i8], ptr %p, "
                                    "i32 0, i32 12") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i32 8") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsMemberStringCopyOffsets) {
  auto result = emitSource("struct Pair {\n"
                           "    left[8]\n"
                           "    right[8]\n"
                           "}\n"
                           "func main() {\n"
                           "    new p[s1] ;Pair\n"
                           "    p.left %s= \"left\"\n"
                           "    p.right %s= p.left\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("str.copy.byte") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("getelementptr inbounds [16 x i8], ptr %p, "
                                    "i32 0, i32 8") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsIntegerComparisonAndBooleanStores) {
  auto result = emitSource("func main() {\n"
                           "    new a[4] = 1\n"
                           "    new b[4] = 2\n"
                           "    new flag[1]\n"
                           "    flag %b= a < b\n"
                           "    return flag\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("icmp slt i32") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("zext i1") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i8") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsExternalFunctionDeclarationAndCall) {
  auto result = emitSource("extern host_counter[4]\n"
                           "extern host_inc(value[4]) -> [4]\n"
                           "func main() {\n"
                           "    return host_inc(host_counter)\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("@host_counter = external global "
                                    "[4 x i8]") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("declare i32 @host_inc(i32)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call i32 @host_inc") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_AppliesInternalLinkageOverridesToDefinitions) {
  auto parseResult = hitsimple::parser::parseSource(
      "new file_local[4]\n"
      "func file_helper() -> [4] {\n"
      "    file_local = 7\n"
      "    return file_local\n"
      "}\n"
      "func main() {\n"
      "    return file_helper()\n"
      "}\n",
      "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());

  auto analyzeResult = hitsimple::sema::analyze(*parseResult.unit);
  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());

  const std::vector<hitsimple::hir::LinkageOverride> overrides = {
      {"file_local", hitsimple::hir::LinkageTarget::Global,
       hitsimple::hir::Linkage::Internal},
      {"file_helper", hitsimple::hir::LinkageTarget::Function,
       hitsimple::hir::Linkage::Internal},
  };
  const auto diagnostics =
      hitsimple::hir::applyLinkageOverrides(*analyzeResult.unit, overrides);
  HS_EXPECT_TRUE(diagnostics.empty());

  auto result = hitsimple::codegen::emitLlvmIr(*analyzeResult.unit, "test.hs");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("@file_local = internal global "
                                    "[4 x i8] zeroinitializer") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("define internal i32 @file_helper()") !=
                 std::string::npos);
}

HS_TEST(Hir_LinkageOverrideRejectsExternDeclarationsAtomically) {
  auto parseResult = hitsimple::parser::parseSource(
      "new visible[4]\n"
      "extern host_inc(value[4]) -> [4]\n"
      "func main() {\n"
      "    return host_inc(visible)\n"
      "}\n",
      "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());

  auto analyzeResult = hitsimple::sema::analyze(*parseResult.unit);
  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());

  const std::vector<hitsimple::hir::LinkageOverride> overrides = {
      {"visible", hitsimple::hir::LinkageTarget::Global,
       hitsimple::hir::Linkage::Internal},
      {"host_inc", hitsimple::hir::LinkageTarget::Function,
       hitsimple::hir::Linkage::Internal},
  };
  const auto diagnostics =
      hitsimple::hir::applyLinkageOverrides(*analyzeResult.unit, overrides);
  HS_EXPECT_EQ(diagnostics.size(), std::size_t{1});
  HS_EXPECT_TRUE(diagnostics.front().find("extern function declaration") !=
                 std::string::npos);
  HS_EXPECT_EQ(analyzeResult.unit->globals.front().linkage,
               hitsimple::hir::Linkage::External);
}

HS_TEST(LLVMCodegen_AppliesAbiOverridesToCCompatibleSymbols) {
  auto parseResult = hitsimple::parser::parseSource(
      "extern errno as i32\n"
      "func host_identity(value as f64) -> f64 {\n"
      "    return value\n"
      "}\n"
      "func main() {\n"
      "    return 0\n"
      "}\n",
      "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());

  auto analyzeResult = hitsimple::sema::analyze(*parseResult.unit);
  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());

  const std::vector<hitsimple::hir::AbiOverride> overrides = {
      {"errno", hitsimple::hir::LinkageTarget::Global,
       hitsimple::hir::AbiType{hitsimple::hir::AbiValueKind::Integer, 4, true},
       std::nullopt},
      {"host_identity", hitsimple::hir::LinkageTarget::Function, std::nullopt,
       hitsimple::hir::FunctionAbiSignature{
           {{hitsimple::hir::AbiValueKind::Floating, 8, true}},
           {{hitsimple::hir::AbiValueKind::Floating, 8, true}}}},
  };
  const auto diagnostics =
      hitsimple::hir::applyAbiOverrides(*analyzeResult.unit, overrides);
  HS_EXPECT_TRUE(diagnostics.empty());

  auto result = hitsimple::codegen::emitLlvmIr(*analyzeResult.unit, "test.hs");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("@errno = external global i32") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("define double @host_identity(double") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("ret double") != std::string::npos);
}

HS_TEST(LLVMCodegen_RejectsAggregateCAbiForNonSysVElfTarget) {
  auto parseResult = hitsimple::parser::parseSource(
      "extern host_pass(value[8]) -> [8]\n"
      "func main() {\n"
      "    return 0\n"
      "}\n",
      "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());

  auto analyzeResult = hitsimple::sema::analyze(*parseResult.unit);
  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());

  hitsimple::hir::AbiType pair{hitsimple::hir::AbiValueKind::Aggregate, 8,
                                false, "Pair"};
  pair.alignment = 4;
  pair.aggregateFields = {
      {hitsimple::hir::AbiValueKind::Integer, 4, true},
      {hitsimple::hir::AbiValueKind::Integer, 1, true},
  };
  pair.aggregateFieldOffsets = {0, 4};
  const std::vector<hitsimple::hir::AbiOverride> overrides = {
      {"host_pass", hitsimple::hir::LinkageTarget::Function, std::nullopt,
       cCompatibilitySignature({pair}, {pair})},
  };
  const auto overrideDiagnostics =
      hitsimple::hir::applyAbiOverrides(*analyzeResult.unit, overrides);
  HS_EXPECT_TRUE(overrideDiagnostics.empty());

  hitsimple::codegen::CodegenOptions options;
  options.targetTriple = "x86_64-apple-darwin";
  const auto result =
      hitsimple::codegen::emitLlvmIr(*analyzeResult.unit, "test.hs", options);

  HS_EXPECT_TRUE(result.llvmIr.empty());
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "C aggregate by-value ABI is only supported for x86_64 "
                     "SysV ELF targets") != std::string::npos);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "x86_64-apple-darwin") != std::string::npos);
}

HS_TEST(LLVMCodegen_RejectsAggregateCAbiForX32Target) {
  auto parseResult = hitsimple::parser::parseSource(
      "extern host_pass(value[8]) -> [8]\n"
      "func main() {\n"
      "    return 0\n"
      "}\n",
      "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());

  auto analyzeResult = hitsimple::sema::analyze(*parseResult.unit);
  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());

  hitsimple::hir::AbiType pair{hitsimple::hir::AbiValueKind::Aggregate, 8,
                                false, "Pair"};
  pair.alignment = 4;
  pair.aggregateFields = {
      {hitsimple::hir::AbiValueKind::Integer, 4, true},
      {hitsimple::hir::AbiValueKind::Integer, 1, true},
  };
  pair.aggregateFieldOffsets = {0, 4};
  const std::vector<hitsimple::hir::AbiOverride> overrides = {
      {"host_pass", hitsimple::hir::LinkageTarget::Function, std::nullopt,
       cCompatibilitySignature({pair}, {pair})},
  };
  const auto overrideDiagnostics =
      hitsimple::hir::applyAbiOverrides(*analyzeResult.unit, overrides);
  HS_EXPECT_TRUE(overrideDiagnostics.empty());

  hitsimple::codegen::CodegenOptions options;
  options.targetTriple = "x86_64-unknown-linux-gnux32";
  const auto result =
      hitsimple::codegen::emitLlvmIr(*analyzeResult.unit, "test.hs", options);

  HS_EXPECT_TRUE(result.llvmIr.empty());
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "C aggregate by-value ABI is only supported for x86_64 "
                     "SysV ELF targets") != std::string::npos);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "x86_64-unknown-linux-gnux32") != std::string::npos);
}

HS_TEST(LLVMCodegen_RejectsIndirectAggregateCAbiForX32Target) {
  auto parseResult = hitsimple::parser::parseSource(
      "extern host_weighted(value[24]) -> [24]\n"
      "func main() {\n"
      "    return 0\n"
      "}\n",
      "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());

  auto analyzeResult = hitsimple::sema::analyze(*parseResult.unit);
  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());

  hitsimple::hir::AbiType weighted{
      hitsimple::hir::AbiValueKind::Aggregate, 24, false, "Weighted"};
  weighted.alignment = 8;
  weighted.aggregateFields = {
      {hitsimple::hir::AbiValueKind::Integer, 8, true},
      {hitsimple::hir::AbiValueKind::Integer, 8, true},
      {hitsimple::hir::AbiValueKind::Integer, 8, true},
  };
  weighted.aggregateFieldOffsets = {0, 8, 16};
  const std::vector<hitsimple::hir::AbiOverride> overrides = {
      {"host_weighted", hitsimple::hir::LinkageTarget::Function,
       std::nullopt,
       cCompatibilitySignature({weighted}, {weighted})},
  };
  const auto overrideDiagnostics =
      hitsimple::hir::applyAbiOverrides(*analyzeResult.unit, overrides);
  HS_EXPECT_TRUE(overrideDiagnostics.empty());

  hitsimple::codegen::CodegenOptions options;
  options.targetTriple = "x86_64-unknown-linux-gnux32";
  const auto result =
      hitsimple::codegen::emitLlvmIr(*analyzeResult.unit, "test.hs", options);

  HS_EXPECT_TRUE(result.llvmIr.empty());
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "C aggregate by-value ABI is only supported for x86_64 "
                     "SysV ELF targets") != std::string::npos);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "x86_64-unknown-linux-gnux32") != std::string::npos);
}

HS_TEST(LLVMCodegen_RejectsIndirectAggregateCAbiForDarwinTarget) {
  auto parseResult = hitsimple::parser::parseSource(
      "extern host_weighted(value[24]) -> [24]\n"
      "func main() {\n"
      "    return 0\n"
      "}\n",
      "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());

  auto analyzeResult = hitsimple::sema::analyze(*parseResult.unit);
  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());

  hitsimple::hir::AbiType weighted{
      hitsimple::hir::AbiValueKind::Aggregate, 24, false, "Weighted"};
  weighted.alignment = 8;
  weighted.aggregateFields = {
      {hitsimple::hir::AbiValueKind::Integer, 8, true},
      {hitsimple::hir::AbiValueKind::Integer, 8, true},
      {hitsimple::hir::AbiValueKind::Integer, 8, true},
  };
  weighted.aggregateFieldOffsets = {0, 8, 16};
  const std::vector<hitsimple::hir::AbiOverride> overrides = {
      {"host_weighted", hitsimple::hir::LinkageTarget::Function,
       std::nullopt,
       cCompatibilitySignature({weighted}, {weighted})},
  };
  const auto overrideDiagnostics =
      hitsimple::hir::applyAbiOverrides(*analyzeResult.unit, overrides);
  HS_EXPECT_TRUE(overrideDiagnostics.empty());

  hitsimple::codegen::CodegenOptions options;
  options.targetTriple = "x86_64-apple-darwin";
  const auto result =
      hitsimple::codegen::emitLlvmIr(*analyzeResult.unit, "test.hs", options);

  HS_EXPECT_TRUE(result.llvmIr.empty());
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "C aggregate by-value ABI is only supported for x86_64 "
                     "SysV ELF targets") != std::string::npos);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "x86_64-apple-darwin") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsAggregateCAbiForExplicitX8664SysVElfTarget) {
#if defined(__aarch64__) || defined(_M_ARM64)
  return;
#endif
  auto parseResult = hitsimple::parser::parseSource(
      "extern host_pass(value[8]) -> [8]\n"
      "func main() {\n"
      "    return 0\n"
      "}\n",
      "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());

  auto analyzeResult = hitsimple::sema::analyze(*parseResult.unit);
  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());

  hitsimple::hir::AbiType pair{hitsimple::hir::AbiValueKind::Aggregate, 8,
                                false, "Pair"};
  pair.alignment = 4;
  pair.aggregateFields = {
      {hitsimple::hir::AbiValueKind::Integer, 4, true},
      {hitsimple::hir::AbiValueKind::Integer, 1, true},
  };
  pair.aggregateFieldOffsets = {0, 4};
  const std::vector<hitsimple::hir::AbiOverride> overrides = {
      {"host_pass", hitsimple::hir::LinkageTarget::Function, std::nullopt,
       cCompatibilitySignature({pair}, {pair})},
  };
  const auto overrideDiagnostics =
      hitsimple::hir::applyAbiOverrides(*analyzeResult.unit, overrides);
  HS_EXPECT_TRUE(overrideDiagnostics.empty());

  hitsimple::codegen::CodegenOptions options;
  options.targetTriple = "x86_64-pc-linux-gnu";
  const auto result =
      hitsimple::codegen::emitLlvmIr(*analyzeResult.unit, "test.hs", options);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find(
                     "target triple = \"x86_64-pc-linux-gnu\"") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("declare i64 @host_pass(i64)") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_RejectsMultipleReturnsForCCompatibilitySignature) {
  auto parseResult = hitsimple::parser::parseSource(
      "func pair(value[4]) -> ([4], [4]) {\n"
      "    return value, value\n"
      "}\n"
      "func main() {\n"
      "    new first[4]\n"
      "    new second[4]\n"
      "    first, second = pair(1)\n"
      "    return 0\n"
      "}\n",
      "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());

  auto analyzeResult = hitsimple::sema::analyze(*parseResult.unit);
  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());

  const std::vector<hitsimple::hir::AbiOverride> overrides = {
      {"pair", hitsimple::hir::LinkageTarget::Function, std::nullopt,
       hitsimple::hir::FunctionAbiSignature{
           {{hitsimple::hir::AbiValueKind::Integer, 4, true}},
           {{hitsimple::hir::AbiValueKind::Integer, 4, true},
            {hitsimple::hir::AbiValueKind::Integer, 4, true}}}},
  };
  const auto overrideDiagnostics =
      hitsimple::hir::applyAbiOverrides(*analyzeResult.unit, overrides);
  HS_EXPECT_TRUE(overrideDiagnostics.empty());

  const auto result =
      hitsimple::codegen::emitLlvmIr(*analyzeResult.unit, "test.hs");

  HS_EXPECT_TRUE(result.llvmIr.empty());
  HS_EXPECT_TRUE(!result.diagnostics.empty());
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "C compatibility functions cannot use multiple return "
                     "values") != std::string::npos);
}

HS_TEST(LLVMCodegen_UsesFloatAndPointerAbiAtExternCallSites) {
  auto parseResult = hitsimple::parser::parseSource(
      "extern host_sink(value as f64, data[P] as addr) -> ()\n"
      "func main() {\n"
      "    host_sink(1.25, 0)\n"
      "    return 0\n"
      "}\n",
      "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());

  auto analyzeResult = hitsimple::sema::analyze(*parseResult.unit);
  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());

  const std::vector<hitsimple::hir::AbiOverride> overrides = {
      {"host_sink", hitsimple::hir::LinkageTarget::Function, std::nullopt,
       hitsimple::hir::FunctionAbiSignature{
           {{hitsimple::hir::AbiValueKind::Floating, 8, true},
            {hitsimple::hir::AbiValueKind::Pointer, sizeof(void *), false}},
           {}}},
  };
  const auto diagnostics =
      hitsimple::hir::applyAbiOverrides(*analyzeResult.unit, overrides);
  HS_EXPECT_TRUE(diagnostics.empty());

  auto result = hitsimple::codegen::emitLlvmIr(*analyzeResult.unit, "test.hs");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("declare void @host_sink(double, ptr)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call void @host_sink(double 1.250000e+00, "
                                    "ptr null)") != std::string::npos);
}

HS_TEST(LLVMCodegen_UsesFloatingExternReturnInFloatAssignment) {
  auto parseResult = hitsimple::parser::parseSource(
      "extern host_value() -> f64\n"
      "func main() {\n"
      "    new value[8] as f64\n"
      "    value = host_value()\n"
      "    return 0\n"
      "}\n",
      "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());

  auto analyzeResult = hitsimple::sema::analyze(*parseResult.unit);
  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());

  const std::vector<hitsimple::hir::AbiOverride> overrides = {
      {"host_value", hitsimple::hir::LinkageTarget::Function, std::nullopt,
       hitsimple::hir::FunctionAbiSignature{
           {}, {{hitsimple::hir::AbiValueKind::Floating, 8, true}}}},
  };
  const auto diagnostics =
      hitsimple::hir::applyAbiOverrides(*analyzeResult.unit, overrides);
  HS_EXPECT_TRUE(diagnostics.empty());

  auto result = hitsimple::codegen::emitLlvmIr(*analyzeResult.unit, "test.hs");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("declare double @host_value()") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("call double @host_value()") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store double") != std::string::npos);
}

HS_TEST(Hir_AbiOverridesRejectMismatchedSignaturesAtomically) {
  auto parseResult = hitsimple::parser::parseSource(
      "extern host_id(value[4]) -> [4]\n"
      "func main() {\n"
      "    return 0\n"
      "}\n",
      "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());

  auto analyzeResult = hitsimple::sema::analyze(*parseResult.unit);
  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());

  const std::vector<hitsimple::hir::AbiOverride> overrides = {
      {"host_id", hitsimple::hir::LinkageTarget::Function, std::nullopt,
       hitsimple::hir::FunctionAbiSignature{
           {{hitsimple::hir::AbiValueKind::Floating, 8, true}},
           {{hitsimple::hir::AbiValueKind::Integer, 4, true}}}},
  };
  const auto diagnostics =
      hitsimple::hir::applyAbiOverrides(*analyzeResult.unit, overrides);
  HS_EXPECT_EQ(diagnostics.size(), std::size_t{1});
  HS_EXPECT_TRUE(diagnostics.front().find("parameter byte length") !=
                 std::string::npos);
  HS_EXPECT_TRUE(!analyzeResult.unit->externFunctions.front().abiSignature);
}

HS_TEST(LLVMCodegen_EmitsStaticLocalBackingStorage) {
  auto result = emitSource("func main() {\n"
                           "    static counter[4]\n"
                           "    counter = counter + 1\n"
                           "    return counter\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("@counter = internal global "
                                    "[4 x i8] zeroinitializer") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("add i32") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsStructArrayMemberAddressing) {
  auto result = emitSource("struct Pair {\n"
                           "    left[4]\n"
                           "    right[4]\n"
                           "}\n"
                           "func main() {\n"
                           "    new p[s3] ;Pair\n"
                           "    p[s2].right = 9\n"
                           "    return p[s2].right\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("alloca [24 x i8]") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("i32 20") != std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("store i32 9") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsAddressOfStaticAndGlobalMemory) {
  auto result = emitSource("new global[4]\n"
                           "func main() {\n"
                           "    static local[4]\n"
                           "    new gptr[8]\n"
                           "    new lptr[8]\n"
                           "    gptr &= &global\n"
                           "    lptr &= &local\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.llvmIr.find("@global = global") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("@local = internal global") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("ptrtoint (ptr @global to i64)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.llvmIr.find("ptrtoint (ptr @local to i64)") !=
                 std::string::npos);
}
