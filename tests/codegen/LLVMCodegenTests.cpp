#include "support/TestRunner.h"

#include "hitsimple/codegen/LLVMCodegen.h"
#include "hitsimple/parser/Parser.h"
#include "hitsimple/sema/Sema.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::vector<hitsimple::stdlib::StandardHeader> allStandardHeaders() {
  const auto headers = hitsimple::stdlib::allStandardHeaders();
  return {headers.begin(), headers.end()};
}

std::string llvmIr(const hitsimple::codegen::ModuleEmitResult& result) {
  if (!result.module) {
    return {};
  }
  std::string output;
  llvm::raw_string_ostream stream(output);
  result.module->print(stream, nullptr);
  stream.flush();
  return output;
}

hitsimple::codegen::ModuleEmitResult
emitSource(std::string_view source,
           hitsimple::codegen::CodegenOptions options = {}) {
  auto parseResult = hitsimple::parser::parseSource(source, "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());

  auto analyzeResult = hitsimple::sema::analyze(
      *parseResult.unit,
      hitsimple::sema::AnalyzeOptions{true, allStandardHeaders()});
  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());

  return hitsimple::codegen::emitLlvmModule(*analyzeResult.unit, "test.hs",
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

hitsimple::codegen::ModuleEmitResult
emitSourceWithoutStandardHeaders(std::string_view source) {
  auto parseResult = hitsimple::parser::parseSource(source, "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());

  auto analyzeResult = hitsimple::sema::analyze(
      *parseResult.unit, hitsimple::sema::AnalyzeOptions{true, {}});
  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());

  return hitsimple::codegen::emitLlvmModule(*analyzeResult.unit, "test.hs");
}

hitsimple::codegen::CodegenOptions
optionsFor(hitsimple::codegen::SafetyMode safetyMode) {
  hitsimple::codegen::CodegenOptions options;
  options.safetyMode = safetyMode;
  return options;
}

hitsimple::hir::FunctionAbiSignature
cCompatibilitySignature(std::vector<hitsimple::hir::AbiType> parameterTypes,
                        std::vector<hitsimple::hir::AbiType> returnTypes) {
  hitsimple::hir::FunctionAbiSignature signature{std::move(parameterTypes),
                                                 std::move(returnTypes)};
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

HS_TEST(LLVMCodegen_ReturnsOwnedVerifiedModule) {
  hitsimple::codegen::CodegenOptions options;
  options.emitDebugInfo = true;
  auto result = emitSource(minimalProgram, options);

  HS_EXPECT_TRUE(result.ok());
  HS_EXPECT_TRUE(result.context != nullptr);
  HS_EXPECT_TRUE(result.module != nullptr);
  HS_EXPECT_TRUE(&result.module->getContext() == result.context.get());
  HS_EXPECT_TRUE(!llvm::verifyModule(*result.module, &llvm::errs()));
  HS_EXPECT_TRUE(result.module->getNamedMetadata("llvm.dbg.cu") != nullptr);

  auto moved = std::move(result);
  HS_EXPECT_TRUE(moved.ok());
  HS_EXPECT_TRUE(&moved.module->getContext() == moved.context.get());
  HS_EXPECT_TRUE(!llvm::verifyModule(*moved.module, &llvm::errs()));
}

HS_TEST(LLVMCodegen_EmitsMinimalProgramIr) {
  auto result = emitSource(minimalProgram);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(!llvmIr(result).empty());
  HS_EXPECT_TRUE(llvmIr(result).find("define i32 @main()") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("alloca [1 x i8]") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i8 42") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("declare i32 @hs_format_output") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call i32 @hs_format_output") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("ret i32 0") != std::string::npos);
}

HS_TEST(LLVMCodegen_PreservesPointerDerivedAddressesAndScalarLocals) {
  auto result = emitSource("func main() {\n"
                           "    new values[64]\n"
                           "    new index as u64 = 4\n"
                           "    new pointer as addr = &values[index]\n"
                           "    [1]*pointer = 42\n"
                           "    return [1]*pointer - 42\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("alloca i64") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("alloca ptr") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("getelementptr i8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("ptrtoint") == std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("inttoptr") == std::string::npos);
}

HS_TEST(LLVMCodegen_PreservesImmediateAddressRebindingAsPointerArithmetic) {
  auto result = emitSource("func main() {\n"
                           "    new values[16]\n"
                           "    new base as addr = &values[0]\n"
                           "    new offset as u64 = 8\n"
                           "    new derived as addr = base? + offset?\n"
                           "    new restored as addr = derived? - offset?\n"
                           "    [8]*restored = 42\n"
                           "    return [8]*restored - 42\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("getelementptr i8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("ptrtoint") == std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("inttoptr") == std::string::npos);
}

HS_TEST(LLVMCodegen_KeepsAllocationAndReallocationAsPointersUntilObserved) {
  auto result = emitSource("func main() {\n"
                           "    new original as addr = alloc(8)\n"
                           "    new resized as addr = realloc(original, 16)\n"
                           "    free(resized)\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("call ptr @malloc") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call ptr @realloc") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call void @free") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("ptrtoint") == std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("inttoptr") == std::string::npos);
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

  for (const auto &[targetTriple, expectedFormat] : targets) {
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
  HS_EXPECT_TRUE(
      llvmIr(result).find("define internal void @__hitsimple.implop") !=
      std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call void @__hitsimple.implop") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_LowersImplTemplateMethodWithInternalViewAbi) {
  auto result =
      emitSource("template Counter {\n"
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
  HS_EXPECT_TRUE(
      llvmIr(result).find(
          "define internal void @__hitsimple.implmethod.Counter.0") !=
      std::string::npos);
  HS_EXPECT_TRUE(
      llvmIr(result).find("call void @__hitsimple.implmethod.Counter.0") !=
      std::string::npos);
}

HS_TEST(LLVMCodegen_ReportsMissingReturnInImplMethodAsViewAbiDiagnostic) {
  auto analyzeResult =
      analyzeSource("template Counter {\n"
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
  auto result = hitsimple::codegen::emitLlvmModule(*analyzeResult.unit, "test.hs");
  HS_EXPECT_TRUE(llvmIr(result).empty());
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "missing return in internal View ABI function "
                     "'__hitsimple.implmethod.Counter.0'") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_MaterializesInternalViewAbiReturnToDeclaredLength) {
  auto result =
      emitSource("template Vec2 {\n"
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
  HS_EXPECT_TRUE(
      llvmIr(result).find("define internal void @__hitsimple.implop") !=
      std::string::npos);
}

HS_TEST(LLVMCodegen_CallsBoundUserTemplateFormatWithStdoutSink) {
  auto result =
      emitSource("template Vec2 {\n"
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
  HS_EXPECT_TRUE(llvmIr(result).find("call void @__hitsimple.implop.Vec2.0") !=
                 std::string::npos);
#if defined(__APPLE__)
  HS_EXPECT_TRUE(llvmIr(result).find("@__stdoutp") != std::string::npos);
#else
  HS_EXPECT_TRUE(llvmIr(result).find("@stdout") != std::string::npos);
#endif
  HS_EXPECT_TRUE(llvmIr(result).find("print.format.") == std::string::npos);
}

HS_TEST(LLVMCodegen_MaterializesNegativeUserTemplateFormatResult) {
  auto result =
      emitSource("template FailFmt {\n"
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
  HS_EXPECT_TRUE(llvmIr(result).find("-7") != std::string::npos);
  HS_EXPECT_TRUE(
      llvmIr(result).find("call void @__hitsimple.implop.FailFmt.0") !=
      std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("format.result.value") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_MaterializesUserTemplateFprintfFormatResult) {
  auto result =
      emitSource("template Marker {\n"
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
  HS_EXPECT_TRUE(llvmIr(result).find("call void @__hitsimple.implop.Marker.0") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("format.file") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("format.result.value") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("print.format.") == std::string::npos);
}

HS_TEST(LLVMCodegen_CallsBoundUserTemplateFormatWithFprintfSink) {
  auto result =
      emitSource("template Marker {\n"
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
  HS_EXPECT_TRUE(llvmIr(result).find("call void @__hitsimple.implop.Marker.0") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("fput.file") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("print.format.") == std::string::npos);
}

HS_TEST(LLVMCodegen_DefaultsUnannotatedMainToI32Zero) {
  auto result = emitSource("func main() {\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("define i32 @main()") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("ret i32 0") != std::string::npos);
}

HS_TEST(LLVMCodegen_PreservesUnannotatedOrdinaryReturnInference) {
  auto result = emitSource("func helper() {\n"
                           "    return 1\n"
                           "}\n"
                           "func main() {\n"
                           "    return helper()\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("define i8 @helper()") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_RejectsIntegerViewsBeyondLlvmBitWidth) {
  const auto byteLength =
      static_cast<std::size_t>(llvm::IntegerType::MAX_INT_BITS / 8U) + 1U;
  auto result = emitSource("func main() {\n"
                           "    new value[" + std::to_string(byteLength) +
                           "]\n"
                           "    return value\n"
                           "}\n");

  HS_EXPECT_TRUE(llvmIr(result).empty());
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_EQ(result.diagnostics.front().stage,
               hitsimple::diagnostic::Stage::Codegen);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "unsupported integer byte length") != std::string::npos);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     std::to_string(byteLength)) != std::string::npos);
}

HS_TEST(LLVMCodegen_ReportsMissingReturnForExplicitNonI32Main) {
  auto result = emitSource("func main() -> [1] {\n"
                           "}\n");

  HS_EXPECT_TRUE(llvmIr(result).empty());
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "missing return in function 'main'") != std::string::npos);
}

HS_TEST(LLVMCodegen_KeepsExplicitNonI32MainReturnType) {
  auto result = emitSource("func main() -> [1] {\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("define i8 @main()") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("ret i8 0") != std::string::npos);
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

  const auto result = hitsimple::codegen::emitLlvmModule(unit, "test.hs");

  HS_EXPECT_TRUE(llvmIr(result).empty());
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "function return type is not void") != std::string::npos);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find("invalid LLVM IR") ==
                 std::string::npos);
}

HS_TEST(LLVMCodegen_RejectsInvalidDirectHirBeforeEmission) {
  auto value = std::make_unique<hitsimple::hir::IntegerLiteral>(
      "42", hitsimple::hir::viewSemanticsForTemplate("i32", 4));
  value->byteLength = 8;

  std::vector<std::unique_ptr<hitsimple::hir::Expr>> values;
  values.push_back(std::move(value));
  std::vector<std::unique_ptr<hitsimple::hir::Stmt>> statements;
  statements.push_back(
      std::make_unique<hitsimple::hir::Return>(std::move(values)));
  std::vector<std::unique_ptr<hitsimple::hir::Function>> functions;
  functions.push_back(std::make_unique<hitsimple::hir::Function>(
      "main", std::vector<hitsimple::hir::Parameter>{},
      std::vector<std::size_t>{4},
      std::make_unique<hitsimple::hir::Block>(std::move(statements))));
  hitsimple::hir::TranslationUnit unit(std::move(functions));

  const auto result = hitsimple::codegen::emitLlvmModule(unit, "test.hs");

  HS_EXPECT_TRUE(llvmIr(result).empty());
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_EQ(result.diagnostics.front().stage, hitsimple::diagnostic::Stage::Hir);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find("legacy byteLength") !=
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
  HS_EXPECT_TRUE(llvmIr(result).find("add i8") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find(", 2") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i8") != std::string::npos);
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
  HS_EXPECT_TRUE(llvmIr(result).find(
                     "declare i32 @hs_scan_input(ptr, ptr, ptr, i64, i32)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call i32 @hs_scan_input") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("scan.targets") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i32") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call i32 @hs_format_output") !=
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
  HS_EXPECT_TRUE(llvmIr(result).find("call i32 @hs_format_output") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("inttoptr i64") == std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsFileFormattedIoBuiltins) {
  auto result =
      emitSource("func main() {\n"
                 "    new file = fopen(\"/tmp/hitsimple-missing\", \"r\")\n"
                 "    new x[4]\n"
                 "    new count[4]\n"
                 "    count, x = fscanf(file, \"%d\")\n"
                 "    fprintf(file, \"%d\", x)\n"
                 "    new c = fget(file)\n"
                 "    return c\n"
                 "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find(
                     "declare i32 @hs_scan_input(ptr, ptr, ptr, i64, i32)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("declare i32 @hs_format_output") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("declare i32 @fgetc(ptr)") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_PromotesFormattedFloatVariablesForVarargs) {
  auto result =
      emitSource("func main() {\n"
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
  HS_EXPECT_TRUE(llvmIr(result).find("load double") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("format.float") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call i32 @hs_format_output") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_ReinterpretsTemporaryTemplateViewWithoutConversion) {
  auto result = emitSource("func main() {\n"
                           "    new bits as u32 = 0x3f800000\n"
                           "    new value as f32 = bits as f32\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("load float") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("sitofp") == std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("uitofp") == std::string::npos);
}

HS_TEST(LLVMCodegen_RejectsMismatchedFormatArgumentKindsForFprintfCall) {
  std::vector<std::unique_ptr<hitsimple::hir::Expr>> arguments;
  arguments.push_back(std::make_unique<hitsimple::hir::IntegerLiteral>(
      "0", hitsimple::hir::viewSemanticsForTemplate("addr", sizeof(void *))));
  arguments.push_back(std::make_unique<hitsimple::hir::StringLiteral>(
      "\"%f\"", hitsimple::hir::viewSemanticsForTemplate("cstr", 3)));
  arguments.push_back(std::make_unique<hitsimple::hir::FloatLiteral>(
      "1.5", hitsimple::hir::viewSemanticsForTemplate("f64", 8)));

  std::vector<std::unique_ptr<hitsimple::hir::Stmt>> statements;
  statements.push_back(std::make_unique<hitsimple::hir::Call>(
      "fprintf", std::move(arguments), hitsimple::stdlib::BuiltinId::Fprintf,
      std::vector<hitsimple::hir::FormatArgKind>{
          hitsimple::hir::FormatArgKind::Bytes,
          hitsimple::hir::FormatArgKind::String}));
  std::vector<std::unique_ptr<hitsimple::hir::Expr>> returnValues;
  returnValues.push_back(
      std::make_unique<hitsimple::hir::IntegerLiteral>(
          "0", hitsimple::hir::viewSemanticsForTemplate("i32", 4)));
  statements.push_back(
      std::make_unique<hitsimple::hir::Return>(std::move(returnValues)));

  std::vector<std::unique_ptr<hitsimple::hir::Function>> functions;
  functions.push_back(std::make_unique<hitsimple::hir::Function>(
      "main", std::vector<hitsimple::hir::Parameter>{},
      std::vector<std::size_t>{4},
      std::make_unique<hitsimple::hir::Block>(std::move(statements))));
  hitsimple::hir::TranslationUnit unit(std::move(functions));

  const auto result = hitsimple::codegen::emitLlvmModule(unit, "test.hs");

  HS_EXPECT_TRUE(llvmIr(result).empty());
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(
      result.diagnostics.front().message.find(
          "format output argument kinds do not match call arguments") !=
      std::string::npos);
}

HS_TEST(LLVMCodegen_RejectsMismatchedFormatArgumentKindsForPrintfCallExpr) {
  std::vector<std::unique_ptr<hitsimple::hir::Expr>> arguments;
  arguments.push_back(std::make_unique<hitsimple::hir::StringLiteral>(
      "\"%f\"", hitsimple::hir::viewSemanticsForTemplate("cstr", 3)));
  arguments.push_back(std::make_unique<hitsimple::hir::FloatLiteral>(
      "1.5", hitsimple::hir::viewSemanticsForTemplate("f64", 8)));

  std::vector<std::unique_ptr<hitsimple::hir::Expr>> returnValues;
  returnValues.push_back(std::make_unique<hitsimple::hir::CallExpr>(
      "printf", std::move(arguments), false,
      hitsimple::stdlib::BuiltinId::Printf,
      std::vector<hitsimple::hir::FormatArgKind>{
          hitsimple::hir::FormatArgKind::String},
      0, "i32", hitsimple::hir::viewSemanticsForTemplate("i32", 4)));
  std::vector<std::unique_ptr<hitsimple::hir::Stmt>> statements;
  statements.push_back(
      std::make_unique<hitsimple::hir::Return>(std::move(returnValues)));

  std::vector<std::unique_ptr<hitsimple::hir::Function>> functions;
  functions.push_back(std::make_unique<hitsimple::hir::Function>(
      "main", std::vector<hitsimple::hir::Parameter>{},
      std::vector<std::size_t>{4},
      std::make_unique<hitsimple::hir::Block>(std::move(statements))));
  hitsimple::hir::TranslationUnit unit(std::move(functions));

  const auto result = hitsimple::codegen::emitLlvmModule(unit, "test.hs");

  HS_EXPECT_TRUE(llvmIr(result).empty());
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(
      result.diagnostics.front().message.find(
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
  HS_EXPECT_TRUE(llvmIr(result).find("sitofp i32") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("to half") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store half") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsCompoundAssignmentAsLoadOpStore) {
  auto result = emitSource("func main() {\n"
                           "    new x[1]\n"
                           "    x %d= 40\n"
                           "    x %d+= 2\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("load i8") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("add i8") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i8") != std::string::npos);
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
  HS_EXPECT_TRUE(llvmIr(result).find("while.cond") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("while.body") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("while.end") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("if.then") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("if.else") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("br i1") != std::string::npos);
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
  HS_EXPECT_TRUE(llvmIr(result).find("for.cond") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("for.post") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("label.done") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("try.body") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("try.catch") != std::string::npos);
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
  HS_EXPECT_TRUE(llvmIr(result).find("try.catch") != std::string::npos);
  HS_EXPECT_TRUE(
      llvmIr(result).find("store double %value.float, ptr %error.addr") !=
      std::string::npos);
}

HS_TEST(LLVMCodegen_UsesExitStatusOneForUncaughtViewThrow) {
  auto result = emitSource("func main() {\n"
                           "    new value as f64 = 42.5\n"
                           "    throw value\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("call void @exit(i32 1)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("ret i32 1") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("unreachable") == std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsGlobalMemoryAccess) {
  auto result = emitSource("new global_count[4]\n"
                           "func main() {\n"
                           "    global_count %d= 7\n"
                           "    return global_count\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@global_count = global "
                                    "[4 x i8] zeroinitializer") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i32 7") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("load i32") != std::string::npos);
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
  HS_EXPECT_TRUE(
      llvmIr(result).find("define internal void @__hitsimple.global.init()") !=
      std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@llvm.global_ctors = appending global") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call i32 @seed()") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store double 1.500000e+00") !=
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
  HS_EXPECT_TRUE(llvmIr(result).find("%x = alloca [1 x i8]") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("%x.1 = alloca [2 x i8]") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i8 2") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i16 3") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsStageEIntegerExpressions) {
  auto result = emitSource("func main() {\n"
                           "    new x[4]\n"
                           "    x = 2\n"
                           "    x = (x + 2) * (x - 1) << 1\n"
                           "    x = x ** 3\n"
                           "    x = x? >= 8 ? ~x : -('a' %4d+ 0)\n"
                           "    x = true && !false || x == 0\n"
                           "    return x\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("mul i32") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("add i32") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("shl i32") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("pow.cond") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("pow.body") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("icmp uge i32") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("ternary.then") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("xor i32") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("-97") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("logic.rhs") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("icmp eq i32") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsStageEFloatExpressions) {
  auto result = emitSource("func main() {\n"
                           "    new x[4] %f= 1.5\n"
                           "    new y[4] %f= 2.25\n"
                           "    x %f= (x %f+ y) %f* 2.0\n"
                           "    return sizeof(x)\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("store float") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("load float") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("fadd float") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("fmul float") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("ret i32 4") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsStageEConversionFunctions) {
  auto result = emitSource("func main() {\n"
                           "    new x[4] = 42\n"
                           "    new f[4] %f= to_f32(x)\n"
                           "    new y[2] = to_i16(f)\n"
                           "    return y\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("sitofp i32") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("fptosi float") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i16") != std::string::npos);
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
  HS_EXPECT_TRUE(llvmIr(result).find("call float @llvm.fabs.f32") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call double @llvm.sqrt.f64") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call float @llvm.pow.f32") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call float @llvm.sin.f32") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call float @tanf(float") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call double @llvm.ceil.f64") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_LowersNestedFloatingStandardCalls) {
  auto result =
      emitSource("func main() {\n"
                 "    new value as f32 = 4.0\n"
                 "    new minimum as f32 = min(f_sqrt(value), f_sqrt(value))\n"
                 "    new integral as i32 = to_i32(f_sqrt(value))\n"
                 "    return integral\n"
                 "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("call float @llvm.sqrt.f32") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("fptosi float") != std::string::npos);
}

HS_TEST(LLVMCodegen_UsesF16AndF128MathFallbacks) {
  hitsimple::codegen::CodegenOptions options;
#if defined(__aarch64__) || defined(_M_ARM64)
  options.targetTriple = "aarch64-unknown-linux-gnu";
#else
  options.targetTriple = "x86_64-unknown-linux-gnu";
#endif
  auto result =
      emitSource("func main() {\n"
                 "    new half as f16 = 1.5\n"
                 "    new halfResult as f16 = f_sqrt(half)\n"
                 "    new quad as f128 = 1.234567890123456789012345678901234\n"
                 "    new quadResult as f128 = f_sin(quad)\n"
                 "    return 0\n"
                 "}\n",
                 options);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("fpext half") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("fptrunc float") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("declare fp128 @hs_f128_literal(ptr)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("declare fp128 @hs_f128_sin(fp128)") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_LowersWindowsF128ThroughBitPatternRuntime) {
  hitsimple::codegen::CodegenOptions options;
#if defined(__aarch64__) || defined(_M_ARM64)
  options.targetTriple = "aarch64-w64-windows-gnu";
#else
  options.targetTriple = "x86_64-w64-windows-gnu";
#endif
  auto result = emitSource("func main() {\n"
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
  HS_EXPECT_TRUE(
      llvmIr(result).find("target triple = \"" + options.targetTriple + "\"") !=
      std::string::npos);
  HS_EXPECT_TRUE(
      llvmIr(result).find("declare void @hs_f128_literal(ptr, ptr)") !=
      std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call void @hs_f128_from_i64(ptr") !=
                 std::string::npos);
  HS_EXPECT_TRUE(
      llvmIr(result).find("declare void @hs_f128_add(ptr, ptr, ptr)") !=
      std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call void @hs_f128_sqrt(ptr") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("declare i8 @hs_f128_lt(ptr, ptr)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("declare double @hs_f128_to_f64(ptr)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("declare i128 @hs_f128_") ==
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("fadd fp128") == std::string::npos);
}

HS_TEST(LLVMCodegen_LowersDarwinF128ThroughBitPatternRuntime) {
  hitsimple::codegen::CodegenOptions options;
#if defined(__aarch64__) || defined(_M_ARM64)
  options.targetTriple = "arm64-apple-darwin";
#else
  options.targetTriple = "x86_64-apple-darwin";
#endif
  auto result = emitSource("func main() {\n"
                           "    new value as f128 = 1.5\n"
                           "    new root as f128 = f_sqrt(value)\n"
                           "    new narrowed as f64 = to_f64(root)\n"
                           "    return 0\n"
                           "}\n",
                           options);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(
      llvmIr(result).find("target triple = \"" + options.targetTriple + "\"") !=
      std::string::npos);
  HS_EXPECT_TRUE(
      llvmIr(result).find("declare void @hs_f128_literal(ptr, ptr)") !=
      std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call void @hs_f128_sqrt(ptr") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("declare double @hs_f128_to_f64(ptr)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("declare fp128 @hs_f128_") ==
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
  HS_EXPECT_TRUE(llvmIr(result).find("store i8 72") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i8 101") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i8 108") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i8 0") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("icmp ne") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("zext i1") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsPrefixedIntegerLiterals) {
  auto result = emitSource("func main() {\n"
                           "    new x[4] = 0xff\n"
                           "    new y[4] = 0b1010_0001\n"
                           "    new z[4] = 0o377\n"
                           "    return x\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("store i32 255") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i32 161") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i32 255") != std::string::npos);
}

HS_TEST(LLVMCodegen_TruncatesIntegerStoresToNarrowTargets) {
  auto result = emitSource("func main() {\n"
                           "    new wide[4] = 255\n"
                           "    new narrow[1]\n"
                           "    narrow = wide\n"
                           "    return narrow\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("trunc i32") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i8") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsEscapedStringBytes) {
  auto result = emitSource("func main() {\n"
                           "    new text[5] %s= \"\\x41\\101\\u00E9\"\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("store i8 65") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i8 -61") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i8 -87") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i8 0") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsPointerValueAndDereference) {
  auto result = emitSource("func main() {\n"
                           "    new x[4]\n"
                           "    new ptr[8]\n"
                           "    ptr = &x\n"
                           "    [4]*ptr = 42\n"
                           "    return [4]*ptr\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("ptrtoint") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("inttoptr") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i32 42") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("load i32") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsIndexReadAndWrite) {
  auto result = emitSource("func main() {\n"
                           "    new x[4]\n"
                           "    x[0] = 65\n"
                           "    x[1] = 66\n"
                           "    return x[1]\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("store i8 65") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i8 66") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("load i8") != std::string::npos);
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
  HS_EXPECT_TRUE(llvmIr(result).find("store i32 42") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("load i32") != std::string::npos);
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
  HS_EXPECT_TRUE(llvmIr(result).find("@malloc") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@realloc") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@free") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i32 42") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsStandardLibraryCoreCalls) {
  auto result = emitSource("func main() {\n"
                           "    new ptr = calloc(1, 8)\n"
                           "    memset(ptr, 0, 8)\n"
                           "    new len = strlen(\"AB\")\n"
                           "    new cmp[4] = memcmp(ptr, ptr, 2)\n"
                           "    new off = strchr(\"AB\", 'B')\n"
                           "    new swapped[2] as bytes = byte_swap(0x1234 as bytes)\n"
                           "    new lower[1] as u8 = 'a'\n"
                           "    new up[1] as u8 = to_upper(lower)\n"
                           "    free(ptr)\n"
                           "    return len\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@calloc") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@memset") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@strlen") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@memcmp") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("declare ptr @strchr(ptr, i32)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call ptr @strchr") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_strchr_offset") == std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@llvm.bswap.i16") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@toupper") == std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("ctype.to_upper") != std::string::npos);
}

HS_TEST(LLVMCodegen_MaterializesCtypePredicatesAsCanonicalBoolBytes) {
  for (const std::string predicate :
       {"is_digit", "is_alpha", "is_alnum", "is_space", "is_lower", "is_upper",
        "is_xdigit", "is_blank", "is_control", "is_print", "is_graph", "is_punct"}) {
    // Nonconstant parameters keep the predicate and its widening visible.
    const auto call = predicate + "(ch)";
    const auto source =
        "func bytes_of(value as bool) -> u8 { return value as u8; }\n"
        "func wrapped(ch as i32) -> bool { return " + call + "; }\n"
        "func probe(ch as i32, expected as bool) -> i32 {\n"
        "    new equal as bool = " + call + " == expected\n"
        "    new different as bool = expected != " + call + "\n"
        "    new argument as u8 = bytes_of(" + call + ")\n"
        "    new choice as bool = expected ? " + call + " : false\n"
        "    new integer as i32 = " + call + "\n"
        "    new signed_cast as i32 = to_i32(" + call + ")\n"
        "    new unsigned_cast as u32 = to_u32(" + call + ")\n"
        "    return integer\n"
        "}\n"
        "func main() -> i32 { return probe(9, true); }\n";
    auto result = emitSource(source);
    HS_EXPECT_TRUE(result.diagnostics.empty());
    HS_EXPECT_TRUE(result.module != nullptr);
    HS_EXPECT_TRUE(!llvm::verifyModule(*result.module));

    unsigned canonicalResults = 0;
    for (const auto &function : *result.module) {
      for (const auto &block : function) {
        for (const auto &instruction : block) {
          if (instruction.getName().str().rfind("ctype.bool", 0) == 0) {
            HS_EXPECT_EQ(instruction.getOpcode(), llvm::Instruction::ZExt);
            HS_EXPECT_TRUE(instruction.getOperand(0)->getType()->isIntegerTy(1));
            HS_EXPECT_TRUE(instruction.getType()->isIntegerTy(8));
            ++canonicalResults;
          }
          if (instruction.getOpcode() == llvm::Instruction::SExt) {
            HS_EXPECT_TRUE(
                !instruction.getOperand(0)->getType()->isIntegerTy(1));
          }
        }
      }
    }
    HS_EXPECT_EQ(canonicalResults, 8U);
  }
}

HS_TEST(LLVMCodegen_LowersFputWithFileBeforeValue) {
  auto result = emitSource("func main() {\n"
                           "    new file = fopen(\"/dev/null\", \"w\")\n"
                           "    new written = fput(file, 'x')\n"
                           "    new closed = fclose(file)\n"
                           "    return written\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(
      llvmIr(result).find("declare i64 @fwrite(ptr, i64, i64, ptr)") !=
      std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call i64 @fwrite(ptr") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_DoesNotLowerUserFunctionWithStandardLibraryName) {
  auto result =
      emitSourceWithoutStandardHeaders("func byte_swap(value[4]) -> [4] {\n"
                                       "    return value\n"
                                       "}\n"
                                       "func main() {\n"
                                       "    new input[4] = 0x12345678\n"
                                       "    new output[4] = byte_swap(input)\n"
                                       "    return output\n"
                                       "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("llvm.bswap") == std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("define i32 @byte_swap") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsStandardStringCallsAndRejectsLegacyCapacity) {
  auto result = emitSource("func main() {\n"
                           "    new dst[16]\n"
                           "    new src[16] as cstr\n"
                           "    new copied = strcpy(dst, src)\n"
                           "    new limited = strncpy(dst, src, 4)\n"
                           "    new appended = strcat(dst, src)\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@strcpy") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@strncpy") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@strcat") != std::string::npos);

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

HS_TEST(LLVMCodegen_BorrowsPointerSizedViewsForLibraryCalls) {
  const auto result = emitSource("func main() {\n"
                                 "    new destination[8] as cstr\n"
                                 "    new source[8] as cstr = \"ok\"\n"
                                 "    new copied as addr = strcpy(destination, source)\n"
                                 "    new length as u64 = strlen(destination)\n"
                                 "    return length\n"
                                 "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@strcpy") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@strlen") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("inttoptr") == std::string::npos);
}

HS_TEST(LLVMCodegen_UsesRawAddressArithmeticForMemoryOperands) {
  const auto result = emitSource("func main() {\n"
                                 "    new buffer[4] = 0x03020100\n"
                                 "    new offset as u64 = get() - 48\n"
                                 "    new copied as addr = "
                                 "memcpy(&buffer[offset], &buffer[0], 3)\n"
                                 "    return 0\n"
                                 "}\n",
                                 optionsFor(hitsimple::codegen::SafetyMode::Checked));

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("call ptr @hs.runtime.location.") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("ptr %mem.dst, ptr %mem.src, i64 3)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call ptr @hs_memcpy(ptr %0, ptr %1") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_StaticCheckedRejectsKnownFileBufferBounds) {
  const auto freadResult =
      emitSource("func main() {\n"
                 "    new file as handle = fopen(\"/dev/null\", \"r\")\n"
                 "    new destination[1] as bytes\n"
                 "    new count as u64 = fread(destination, 1, 2, file)\n"
                 "    return 0\n"
                 "}\n",
                 optionsFor(hitsimple::codegen::SafetyMode::StaticChecked));
  HS_EXPECT_TRUE(!freadResult.diagnostics.empty());
  HS_EXPECT_TRUE(freadResult.diagnostics.front().message.find(
                     "fread destination range out of bounds") !=
                 std::string::npos);

  const auto fwriteResult =
      emitSource("func main() {\n"
                 "    new file as handle = fopen(\"/dev/null\", \"w\")\n"
                 "    new source[1] as bytes\n"
                 "    new count as u64 = fwrite(source, 1, 2, file)\n"
                 "    return 0\n"
                 "}\n",
                 optionsFor(hitsimple::codegen::SafetyMode::StaticChecked));
  HS_EXPECT_TRUE(!fwriteResult.diagnostics.empty());
  HS_EXPECT_TRUE(fwriteResult.diagnostics.front().message.find(
                     "fwrite source range out of bounds") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsStandardCallocForm) {
  auto standard = emitSource("func main() {\n"
                             "    new ptr = calloc(1, 8)\n"
                             "    free(ptr)\n"
                             "    return 0\n"
                             "}\n");
  HS_EXPECT_TRUE(standard.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(standard).find("@calloc") != std::string::npos);

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
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_alloc") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_check_store") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_free") != std::string::npos);
}

HS_TEST(LLVMCodegen_CheckedRegistersInternalObjectsAndFrames) {
  auto options = optionsFor(hitsimple::codegen::SafetyMode::Checked);
  auto result = emitSource("new global_value[4]\n"
                           "func main() {\n"
                           "    new local_value[4]\n"
                           "    new ptr[8]\n"
                           "    ptr = &global_value\n"
                           "    [4]*ptr = 42\n"
                           "    return local_value\n"
                           "}\n",
                           options);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_register_static") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_register_local") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_frame_enter") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_frame_exit") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@llvm.global_ctors") != std::string::npos);
}

HS_TEST(LLVMCodegen_CheckedDoesNotTrustExternGlobalAddresses) {
  auto options = optionsFor(hitsimple::codegen::SafetyMode::Checked);
  auto result = emitSource("extern host_buffer[4]\n"
                           "func main() {\n"
                           "    return [4]*&host_buffer\n"
                           "}\n",
                           options);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_check_load") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_register_static") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_CheckedGuardsViewDereferencesBeforeTheirReads) {
  const auto options = optionsFor(hitsimple::codegen::SafetyMode::Checked);
  const auto condition = emitSource("func main() {\n"
                                    "    new pointer as addr = alloc(1)\n"
                                    "    if ([1]*pointer) {\n"
                                    "        return 1\n"
                                    "    }\n"
                                    "    return 0\n"
                                    "}\n",
                                    options);
  const auto copy = emitSource("func main() {\n"
                               "    new pointer as addr = alloc(1)\n"
                               "    new target[1] as bytes\n"
                               "    target = [1]*pointer\n"
                               "    return 0\n"
                               "}\n",
                               options);

  HS_EXPECT_TRUE(condition.diagnostics.empty());
  const auto conditionCheck =
      llvmIr(condition).find("ptr %deref.view.value, i64 1)");
  const auto conditionRead = llvmIr(condition).find("cond.value");
  HS_EXPECT_TRUE(conditionCheck != std::string::npos);
  HS_EXPECT_TRUE(conditionRead != std::string::npos);
  HS_EXPECT_TRUE(conditionCheck < conditionRead);

  HS_EXPECT_TRUE(copy.diagnostics.empty());
  const auto copyCheck = llvmIr(copy).find("ptr %deref.view.value, i64 1)");
  const auto copyRead = llvmIr(copy).find("llvm.memcpy");
  HS_EXPECT_TRUE(copyCheck != std::string::npos);
  HS_EXPECT_TRUE(copyRead != std::string::npos);
  HS_EXPECT_TRUE(copyCheck < copyRead);
}

HS_TEST(LLVMCodegen_StaticCheckedRejectsNullViewDereferences) {
  const auto options =
      optionsFor(hitsimple::codegen::SafetyMode::StaticChecked);
  const auto condition = emitSource("func main() {\n"
                                    "    new pointer as addr = 0\n"
                                    "    if ([1]*pointer) {\n"
                                    "        return 1\n"
                                    "    }\n"
                                    "    return 0\n"
                                    "}\n",
                                    options);
  const auto copy = emitSource("func main() {\n"
                               "    new pointer as addr = 0\n"
                               "    new target[1] as bytes\n"
                               "    target = [1]*pointer\n"
                               "    return 0\n"
                               "}\n",
                               options);

  HS_EXPECT_TRUE(!condition.diagnostics.empty());
  HS_EXPECT_TRUE(condition.diagnostics.front().message.find(
                     "null address dereference") != std::string::npos);
  HS_EXPECT_TRUE(!copy.diagnostics.empty());
  HS_EXPECT_TRUE(copy.diagnostics.front().message.find(
                     "null address dereference") != std::string::npos);
}

HS_TEST(LLVMCodegen_StaticCheckedTracksDynamicAddressLifetimes) {
  const auto options =
      optionsFor(hitsimple::codegen::SafetyMode::StaticChecked);
  const auto interiorFree = emitSource("func main() {\n"
                                       "    new pointer = alloc(2)\n"
                                       "    new interior as addr = pointer? + 1\n"
                                       "    free(interior)\n"
                                       "    return 0\n"
                                       "}\n",
                                       options);
  const auto doubleFree = emitSource("func main() {\n"
                                     "    new pointer = alloc(1)\n"
                                     "    free(pointer)\n"
                                     "    free(pointer)\n"
                                     "    return 0\n"
                                     "}\n",
                                     options);
  const auto useAfterFree = emitSource("func main() {\n"
                                       "    new pointer = alloc(1)\n"
                                       "    free(pointer)\n"
                                       "    return [1]*pointer\n"
                                       "}\n",
                                       options);
  const auto useAfterRealloc =
      emitSource("func main() {\n"
                 "    new pointer = alloc(1)\n"
                 "    new alias as addr = pointer\n"
                 "    pointer = realloc(pointer, 2)\n"
                 "    return [1]*alias\n"
                 "}\n",
                 options);
  const auto reallocStaticStorage =
      emitSource("func main() {\n"
                 "    new local[1] as bytes\n"
                 "    new pointer = realloc(&local, 2)\n"
                 "    return 0\n"
                 "}\n",
                 options);
  const auto freeStaticStorage = emitSource("func main() {\n"
                                            "    new local[1] as bytes\n"
                                            "    free(&local)\n"
                                            "    return 0\n"
                                            "}\n",
                                            options);

  HS_EXPECT_TRUE(!interiorFree.diagnostics.empty());
  HS_EXPECT_TRUE(interiorFree.diagnostics.front().message.find(
                     "free requires a dynamic-object base address") !=
                 std::string::npos);
  HS_EXPECT_TRUE(!doubleFree.diagnostics.empty());
  HS_EXPECT_TRUE(doubleFree.diagnostics.front().message.find("double free") !=
                 std::string::npos);
  HS_EXPECT_TRUE(!useAfterFree.diagnostics.empty());
  HS_EXPECT_TRUE(useAfterFree.diagnostics.front().message.find("use after free") !=
                 std::string::npos);
  HS_EXPECT_TRUE(useAfterRealloc.diagnostics.empty());
  HS_EXPECT_TRUE(!reallocStaticStorage.diagnostics.empty());
  HS_EXPECT_TRUE(reallocStaticStorage.diagnostics.front().message.find(
                     "realloc requires a dynamic-object base address") !=
                 std::string::npos);
  HS_EXPECT_TRUE(!freeStaticStorage.diagnostics.empty());
  HS_EXPECT_TRUE(freeStaticStorage.diagnostics.front().message.find(
                     "free requires a dynamic-object base address") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_StaticCheckedTracksSignedPointerOffsetRanges) {
  const auto options =
      optionsFor(hitsimple::codegen::SafetyMode::StaticChecked);
  const auto staticBeforeBase = emitSource("func main() {\n"
                                          "    new bytes[1] as bytes\n"
                                          "    return [1]*((&bytes)? - 1)\n"
                                          "}\n",
                                          options);
  const auto staticAtEnd = emitSource("func main() {\n"
                                      "    new bytes[1] as bytes\n"
                                      "    return [1]*((&bytes)? + 1)\n"
                                      "}\n",
                                      options);
  const auto dynamicBeforeBase = emitSource("func main() {\n"
                                           "    new pointer = alloc(1)\n"
                                           "    return [1]*(pointer? - 1)\n"
                                           "}\n",
                                           options);
  const auto dynamicAtEnd = emitSource("func main() {\n"
                                       "    new pointer = alloc(1)\n"
                                       "    return [1]*(pointer? + 1)\n"
                                       "}\n",
                                       options);
  const auto returnsToBase = emitSource("func main() {\n"
                                        "    new bytes[1] as bytes = 0\n"
                                        "    new pointer as addr = (&bytes)? + 1\n"
                                        "    return [1]*(pointer? - 1)\n"
                                        "}\n",
                                        options);

  for (const auto *result : {&staticBeforeBase, &staticAtEnd,
                             &dynamicBeforeBase, &dynamicAtEnd}) {
    HS_EXPECT_TRUE(!result->diagnostics.empty());
    HS_EXPECT_TRUE(result->diagnostics.front().message.find(
                       "memory load out of bounds") != std::string::npos);
  }
  HS_EXPECT_TRUE(returnsToBase.diagnostics.empty());
}

HS_TEST(LLVMCodegen_CheckedInvalidatesAddressFactsAfterStorageRewrite) {
  const auto options = optionsFor(hitsimple::codegen::SafetyMode::Checked);
  const auto result = emitSource("func main() -> i32 {\n"
                                 "    new large[8] as bytes\n"
                                 "    new pointer as addr = &large\n"
                                 "    set pointer as f64\n"
                                 "    pointer = 0.0\n"
                                 "    set pointer as addr\n"
                                 "    return [1]*pointer\n"
                                 "}\n",
                                 options);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_check_load") != std::string::npos);
}

HS_TEST(LLVMCodegen_StaticCheckedRestoresGlobalSafetyBaselineForFunctions) {
  const auto staticChecked =
      optionsFor(hitsimple::codegen::SafetyMode::StaticChecked);
  const auto checked = optionsFor(hitsimple::codegen::SafetyMode::Checked);
  const auto globalOob = emitSource("new small[1] as bytes\n"
                                    "new pointer as addr = &small\n"
                                    "func inspect() -> i32 {\n"
                                    "    return [2]*pointer\n"
                                    "}\n"
                                    "func main() -> i32 {\n"
                                    "    return inspect()\n"
                                    "}\n",
                                    staticChecked);
  const auto globalDoubleFree = emitSource("new pointer as addr = alloc(1)\n"
                                           "func main() -> i32 {\n"
                                           "    free(pointer)\n"
                                           "    free(pointer)\n"
                                           "    return 0\n"
                                           "}\n",
                                           staticChecked);
  const auto distinctGlobalAndFunctionAllocations =
      emitSource("new globalPointer as addr = alloc(1)\n"
                 "func main() -> i32 {\n"
                 "    new localPointer as addr = alloc(1)\n"
                 "    free(globalPointer)\n"
                 "    free(localPointer)\n"
                 "    return 0\n"
                 "}\n",
                 staticChecked);
  const auto knownGlobalLoad = emitSource("new small[1] as bytes\n"
                                          "new pointer as addr = &small\n"
                                          "func inspect() -> i32 {\n"
                                          "    return [1]*pointer\n"
                                          "}\n"
                                          "func main() -> i32 {\n"
                                          "    return inspect()\n"
                                          "}\n",
                                          checked);

  HS_EXPECT_TRUE(!globalOob.diagnostics.empty());
  HS_EXPECT_TRUE(globalOob.diagnostics.front().message.find(
                     "memory load out of bounds") != std::string::npos);
  HS_EXPECT_TRUE(!globalDoubleFree.diagnostics.empty());
  HS_EXPECT_TRUE(globalDoubleFree.diagnostics.front().message.find("double free") !=
                 std::string::npos);
  HS_EXPECT_TRUE(distinctGlobalAndFunctionAllocations.diagnostics.empty());
  HS_EXPECT_TRUE(knownGlobalLoad.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(knownGlobalLoad).find("@hs_check_load") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_StaticCheckedUsesIntegerDataflowForDivision) {
  const auto options =
      optionsFor(hitsimple::codegen::SafetyMode::StaticChecked);
  const auto knownZero = emitSource("func main() {\n"
                                   "    new divisor as i32 = 0\n"
                                   "    return 1 / divisor\n"
                                   "}\n",
                                   options);
  const auto replacedWithNonZero = emitSource("func main() {\n"
                                              "    new divisor as i32 = 0\n"
                                              "    divisor = 1\n"
                                              "    return 1 / divisor\n"
                                              "}\n",
                                              options);

  HS_EXPECT_TRUE(!knownZero.diagnostics.empty());
  HS_EXPECT_TRUE(knownZero.diagnostics.front().message.find(
                     "division by zero") != std::string::npos);
  HS_EXPECT_TRUE(replacedWithNonZero.diagnostics.empty());
}

HS_TEST(LLVMCodegen_StaticCheckedMergesControlFlowSafetyFacts) {
  const auto options =
      optionsFor(hitsimple::codegen::SafetyMode::StaticChecked);
  const auto deadBranch = emitSource("func main() {\n"
                                    "    new divisor as i32 = 1\n"
                                    "    if (false) {\n"
                                    "        divisor = 0\n"
                                    "    }\n"
                                    "    return 1 / divisor\n"
                                    "}\n",
                                    options);
  const auto divergentBranches = emitSource(
      "func divide(flag as i32) -> i32 {\n"
      "    new divisor as i32 = 1\n"
      "    if (flag) {\n"
      "        divisor = 0\n"
      "    } else {\n"
      "        divisor = 1\n"
      "    }\n"
      "    return 1 / divisor\n"
      "}\n"
      "func main() {\n"
      "    return divide(0)\n"
      "}\n",
      options);
  const auto loopWrite = emitSource("func divide(flag as i32) -> i32 {\n"
                                    "    new divisor as i32 = 1\n"
                                    "    while (flag) {\n"
                                    "        divisor = 0\n"
                                    "    }\n"
                                    "    return 1 / divisor\n"
                                    "}\n"
                                    "func main() {\n"
                                    "    return divide(0)\n"
                                    "}\n",
                                    options);

  HS_EXPECT_TRUE(deadBranch.diagnostics.empty());
  HS_EXPECT_TRUE(divergentBranches.diagnostics.empty());
  HS_EXPECT_TRUE(loopWrite.diagnostics.empty());
}

HS_TEST(LLVMCodegen_StaticSafetyTracksGotoEdges) {
  const auto staticChecked =
      optionsFor(hitsimple::codegen::SafetyMode::StaticChecked);
  const auto skippedAssignment = emitSource(
      "func main() -> i32 {\n"
      "    new small[1] as bytes\n"
      "    new large[8] as bytes\n"
      "    new p as addr = &small\n"
      "    goto done\n"
      "    p = &large\n"
      "    done: return [2]*p\n"
      "}\n",
      staticChecked);

  HS_EXPECT_TRUE(!skippedAssignment.diagnostics.empty());
  HS_EXPECT_TRUE(skippedAssignment.diagnostics.front().message.find(
                     "memory load out of bounds") != std::string::npos);

  const auto nestedSkippedAssignment = emitSource(
      "func main() -> i32 {\n"
      "    new small[1] as bytes\n"
      "    new large[8] as bytes\n"
      "    new p as addr = &small\n"
      "    if (true) {\n"
      "        goto done\n"
      "    }\n"
      "    p = &large\n"
      "    done: return [2]*p\n"
      "}\n",
      staticChecked);

  HS_EXPECT_TRUE(!nestedSkippedAssignment.diagnostics.empty());
  HS_EXPECT_TRUE(nestedSkippedAssignment.diagnostics.front().message.find(
                     "memory load out of bounds") != std::string::npos);

  const auto checked = emitSource(
      "func main(flag as bool) -> i32 {\n"
      "    new small[1] as bytes\n"
      "    new large[8] as bytes\n"
      "    new p as addr = &small\n"
      "    if (flag) {\n"
      "        goto done\n"
      "    }\n"
      "    p = &large\n"
      "    done: return [2]*p\n"
      "}\n",
      optionsFor(hitsimple::codegen::SafetyMode::Checked));

  HS_EXPECT_TRUE(checked.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(checked).find("@hs_check_load") != std::string::npos);
}

HS_TEST(LLVMCodegen_StaticSafetyHonorsLogicalShortCircuitEffects) {
  const auto staticChecked =
      optionsFor(hitsimple::codegen::SafetyMode::StaticChecked);
  const auto shortCircuitAnd = emitSource(
      "func main() -> i32 {\n"
      "    new small[1] as bytes\n"
      "    new large[8] as bytes\n"
      "    new p as addr = &small\n"
      "    if (false && (p = &large)) {}\n"
      "    return [2]*p\n"
      "}\n",
      staticChecked);
  const auto shortCircuitOr = emitSource(
      "func main() -> i32 {\n"
      "    new small[1] as bytes\n"
      "    new large[8] as bytes\n"
      "    new p as addr = &small\n"
      "    if (true || (p = &large)) {}\n"
      "    return [2]*p\n"
      "}\n",
      staticChecked);
  const auto unknownLeft = emitSource(
      "func main(flag as bool) -> i32 {\n"
      "    new small[1] as bytes\n"
      "    new large[8] as bytes\n"
      "    new p as addr = &small\n"
      "    if (flag && (p = &large)) {}\n"
      "    return [2]*p\n"
      "}\n",
      optionsFor(hitsimple::codegen::SafetyMode::Checked));

  HS_EXPECT_TRUE(!shortCircuitAnd.diagnostics.empty());
  HS_EXPECT_TRUE(shortCircuitAnd.diagnostics.front().message.find(
                     "memory load out of bounds") != std::string::npos);
  HS_EXPECT_TRUE(!shortCircuitOr.diagnostics.empty());
  HS_EXPECT_TRUE(shortCircuitOr.diagnostics.front().message.find(
                    "memory load out of bounds") != std::string::npos);
  HS_EXPECT_TRUE(unknownLeft.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(unknownLeft).find("@hs_check_load") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_CheckedInvalidatesUserTemplateAssignmentTargets) {
  const auto result = emitSource(
      "template Box {\n"
      "    pointer[P] as addr\n"
      "}\n"
      "impl Box {\n"
      "    op = (dst as Box, src as Box) -> [P] {\n"
      "        dst.pointer = src.pointer\n"
      "        return dst\n"
      "    }\n"
      "}\n"
      "func main() -> i32 {\n"
      "    new small[1] as bytes\n"
      "    new large[8] as bytes\n"
      "    new source as Box\n"
      "    source.pointer = &small\n"
      "    new target as Box\n"
      "    target.pointer = &large\n"
      "    target = source\n"
      "    return [2]*target.pointer\n"
      "}\n",
      optionsFor(hitsimple::codegen::SafetyMode::Checked));

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_check_load") != std::string::npos);
}

HS_TEST(LLVMCodegen_CheckedInvalidatesGlobalsAfterUserTemplateFormatExpression) {
  const auto result = emitSource(
      "new small[1] as bytes\n"
      "new large[8] as bytes\n"
      "new p as addr = &large\n"
      "template Writer {\n"
      "    value[1] as bytes\n"
      "}\n"
      "impl Writer {\n"
      "    op format(value as Writer, out as addr) -> [4] {\n"
      "        p = &small\n"
      "        return 0\n"
      "    }\n"
      "}\n"
      "func main() -> i32 {\n"
      "    new value as Writer\n"
      "    new n = print(value as Writer)\n"
      "    return [2]*p\n"
      "}\n",
      optionsFor(hitsimple::codegen::SafetyMode::Checked));

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_check_load") != std::string::npos);
}

HS_TEST(LLVMCodegen_MaterializesDynamicViewsAndGenericByteSwap) {
  auto result =
      emitSource("func main() {\n"
                 "    new source[3] as bytes = 'ABC'\n"
                 "    new requested[8] = 3\n"
                 "    new resized[3] as bytes = resize_bytes(source, requested)\n"
                 "    new swapped[3] as bytes = byte_swap(source)\n"
                 "    return 0\n"
                 "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("alloca i8, i64") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_reverse_bytes") != std::string::npos);
}

HS_TEST(LLVMCodegen_CheckedGuardsDynamicViewsReadAsFixedIntegers) {
  const auto result = emitSource(
      "func dynamic_length() -> u64 {\n"
      "    return 4\n"
      "}\n"
      "func main() -> u64 {\n"
      "    new source as u64 = 1\n"
      "    new requested as u64 = dynamic_length()\n"
      "    new value as u64 = (resize_bytes(source, requested) as u64) + 1\n"
      "    return value\n"
      "}\n",
      optionsFor(hitsimple::codegen::SafetyMode::Checked));

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_check_view_length") !=
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
                           "    return compared\n"
                           "}\n",
                           optionsFor(hitsimple::codegen::SafetyMode::Checked));

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_calloc") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_memcpy") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_memcmp") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_strlen") != std::string::npos);
}

HS_TEST(LLVMCodegen_StaticCheckedRejectsOverlappingMemcpy) {
  auto result =
      emitSource("func main() {\n"
                 "    new buffer[4] = 0x03020100\n"
                 "    new copied as addr = memcpy(&buffer[1], &buffer[0], 3)\n"
                 "    return 0\n"
                 "}\n",
                 optionsFor(hitsimple::codegen::SafetyMode::StaticChecked));

  HS_EXPECT_TRUE(!result.diagnostics.empty());
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "overlapping memcpy ranges") != std::string::npos);
}

HS_TEST(LLVMCodegen_CheckedUsesFileRuntimeBridges) {
  const auto result =
      emitSource("func main() {\n"
                 "    new file as handle = fopen(\"/dev/null\", \"w+\")\n"
                 "    new buffer[4]\n"
                 "    new input = fget(file)\n"
                 "    new written = fput(file, buffer)\n"
                 "    new read = fread(buffer, 1, 4, file)\n"
                 "    new saved = fwrite(buffer, 1, 4, file)\n"
                 "    new seeked = fseek(file, 0, 0)\n"
                 "    new position = ftell(file)\n"
                 "    new flushed = fflush(file)\n"
                 "    new ended = feof(file)\n"
                 "    new failed = ferror(file)\n"
                 "    new closed = fclose(file)\n"
                 "    return input + written + read + saved + seeked + "
                 "position + flushed + "
                 "ended + failed + closed\n"
                 "}\n",
                 optionsFor(hitsimple::codegen::SafetyMode::Checked));

  HS_EXPECT_TRUE(result.diagnostics.empty());
  for (const std::string_view symbol :
       {"@hs_fopen", "@hs_fget", "@hs_fput", "@hs_fread", "@hs_fwrite",
        "@hs_fseek", "@hs_ftell", "@hs_fflush", "@hs_feof", "@hs_ferror",
        "@hs_fclose"}) {
    HS_EXPECT_TRUE(llvmIr(result).find(symbol) != std::string::npos);
  }
}

HS_TEST(LLVMCodegen_FileStateChecksAreCheckedOnly) {
  constexpr std::string_view source =
      "func main() {\n"
      "    new input = get()\n"
      "    new file as handle = fopen(\"file.tmp\", \"w+\")\n"
      "    fprintf(file, \"ok\")\n"
      "    new closed = fclose(file)\n"
      "    return closed\n"
      "}\n";
  for (const auto mode : {hitsimple::codegen::SafetyMode::Unchecked,
                          hitsimple::codegen::SafetyMode::StaticChecked,
                          hitsimple::codegen::SafetyMode::Checked}) {
    const auto result = emitSource(source, optionsFor(mode));
    HS_EXPECT_TRUE(result.diagnostics.empty());
    const bool checked = mode == hitsimple::codegen::SafetyMode::Checked;
    for (const std::string_view symbol :
         {"@hs_fopen", "@hs_fclose", "@hs_check_file_handle", "@hs_get"}) {
      HS_EXPECT_EQ(llvmIr(result).find(symbol) != std::string::npos, checked);
    }
  }
}

HS_TEST(LLVMCodegen_RuntimeSourceLocationsFollowCheckedModePolicy) {
  constexpr std::string_view source =
      "func main() {\n"
      "    new source[4] = 0x03020100\n"
      "    new destination[1]\n"
      "    new count as u64 = get()\n"
      "    new copied as addr = memcpy(destination, source, count)\n"
      "    return 0\n"
      "}\n";

  const auto checked =
      emitSource(source, optionsFor(hitsimple::codegen::SafetyMode::Checked));
  HS_EXPECT_TRUE(checked.diagnostics.empty());
  const auto locationCall =
      llvmIr(checked).find("call void @hs_set_source_location");
  const auto runtimeCall = llvmIr(checked).find("call ptr @hs_memcpy");
  HS_EXPECT_TRUE(locationCall != std::string::npos);
  HS_EXPECT_TRUE(runtimeCall != std::string::npos);
  HS_EXPECT_TRUE(locationCall < runtimeCall);
  HS_EXPECT_TRUE(llvmIr(checked).find("test.hs") != std::string::npos);

  const auto unchecked = emitSource(
      source, optionsFor(hitsimple::codegen::SafetyMode::Unchecked));
  HS_EXPECT_TRUE(unchecked.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(unchecked).find("hs_set_source_location") ==
                 std::string::npos);

  const auto staticChecked = emitSource(
      source, optionsFor(hitsimple::codegen::SafetyMode::StaticChecked));
  HS_EXPECT_TRUE(staticChecked.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(staticChecked).find("hs_set_source_location") ==
                 std::string::npos);
}

HS_TEST(LLVMCodegen_WindowsSoftwareF128SkipsRuntimeSourceLocations) {
  hitsimple::codegen::CodegenOptions options;
#if defined(__aarch64__) || defined(_M_ARM64)
  options.targetTriple = "aarch64-w64-windows-gnu";
#else
  options.targetTriple = "x86_64-w64-windows-gnu";
#endif
  options.safetyMode = hitsimple::codegen::SafetyMode::Checked;
  const auto ordinary = emitSource(
      "func main() {\n"
      "    new source[4] = 0x03020100\n"
      "    new destination[1]\n"
      "    new count as u64 = get()\n"
      "    new copied as addr = memcpy(destination, source, count)\n"
      "    return 0\n"
      "}\n",
      options);
  HS_EXPECT_TRUE(ordinary.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(ordinary).find("hs_set_source_location") !=
                 std::string::npos);

  const auto result = emitSource(
      "func main() -> i32 {\n"
      "    new converted as i32\n"
      "    new value as f128\n"
      "    converted, value = scanf(\"%16f\")\n"
      "    new written as i32 = printf(\"%16f\\n\", value)\n"
      "    return 0\n"
      "}\n",
      options);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_scan_input") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_format_output") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("hs_set_source_location") ==
                 std::string::npos);
}

HS_TEST(LLVMCodegen_CheckedAbsReportsMinimumSignedValue) {
  const auto checked =
      emitSource("func magnitude(value as i8) -> i8 {\n"
                 "    return abs(value)\n"
                 "}\n"
                 "func main() {\n"
                 "    return magnitude(1)\n"
                 "}\n",
                 optionsFor(hitsimple::codegen::SafetyMode::Checked));

  HS_EXPECT_TRUE(checked.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(checked).find("@hs_abs_overflow") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(checked).find("abs.ismin") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(checked).find("abs.overflow") != std::string::npos);

  const auto staticChecked =
      emitSource("func magnitude(value as i8) -> i8 {\n"
                 "    return abs(value)\n"
                 "}\n"
                 "func main() {\n"
                 "    return magnitude(1)\n"
                 "}\n",
                 optionsFor(hitsimple::codegen::SafetyMode::StaticChecked));

  HS_EXPECT_TRUE(staticChecked.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(staticChecked).find("@hs_abs_overflow") ==
                 std::string::npos);
}

HS_TEST(LLVMCodegen_CheckedRejectsStaticallyKnownAbsMinimum) {
  const auto checked =
      emitSource("func main() {\n"
                 "    new value as i8 = -128\n"
                 "    new magnitude as i8 = abs(value)\n"
                 "    return magnitude\n"
                 "}\n",
                 optionsFor(hitsimple::codegen::SafetyMode::Checked));

  HS_EXPECT_TRUE(!checked.diagnostics.empty());
  HS_EXPECT_TRUE(checked.diagnostics[0].message.find(
                     "abs of minimum signed value") != std::string::npos);

  const auto staticChecked =
      emitSource("func main() {\n"
                 "    new value as i8 = -128\n"
                 "    new magnitude as i8 = abs(value)\n"
                 "    return magnitude\n"
                 "}\n",
                 optionsFor(hitsimple::codegen::SafetyMode::StaticChecked));

  HS_EXPECT_TRUE(staticChecked.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(staticChecked).find("@hs_abs_overflow") ==
                 std::string::npos);
}

HS_TEST(LLVMCodegen_StaticCheckedRejectsStaticallyKnownLibraryProducts) {
  const auto result =
      emitSource("func main() {\n"
                 "    new count as u64 = 18446744073709551615\n"
                 "    new size as u64 = 2\n"
                 "    new ptr as addr = calloc(count, size)\n"
                 "    return 0\n"
                 "}\n",
                 optionsFor(hitsimple::codegen::SafetyMode::StaticChecked));

  HS_EXPECT_TRUE(!result.diagnostics.empty());
  HS_EXPECT_TRUE(result.diagnostics[0].message.find("calloc size overflow") !=
                 std::string::npos);
  HS_EXPECT_TRUE(result.diagnostics[0].range.has_value());
  HS_EXPECT_EQ(result.diagnostics[0].range->begin.file, "test.hs");
  HS_EXPECT_EQ(result.diagnostics[0].range->begin.line, 4U);
  HS_EXPECT_EQ(result.diagnostics[0].range->begin.column, 23U);
}

HS_TEST(LLVMCodegen_UsesTypedRuntimeDescriptorsForNativeFormatting) {
  auto result = emitSource("func main() {\n"
                           "    new value[4] = 42\n"
                           "    printf(\"%d %s\\n\", value, \"ok\")\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_format_output") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("declare i32 @printf(ptr, ...)") ==
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
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_check_store") == std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_check_load") == std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i32 42") != std::string::npos);
}

HS_TEST(LLVMCodegen_CheckedInvalidatesFactsAfterIndirectWrites) {
  const auto checked = optionsFor(hitsimple::codegen::SafetyMode::Checked);
  const std::vector<std::pair<std::string_view, std::string_view>> cases = {
      {"func main() -> i32 {\n"
       "    new large[8] as bytes\n"
       "    new small[1] as bytes\n"
       "    new p as addr = &large\n"
       "    [8]*&p = &small\n"
       "    return [2]*p\n"
       "}\n",
       "@hs_check_load"},
      {"func main() -> i32 {\n"
       "    new large[8] as bytes\n"
       "    new p as addr = &large\n"
       "    memset(&p, 0, 8)\n"
       "    return [2]*p\n"
       "}\n",
       "@hs_check_load"},
      {"func main() -> i32 {\n"
       "    new large[8] as bytes\n"
       "    new small[1] as bytes\n"
       "    new p as addr = &large\n"
       "    new replacement as addr = &small\n"
       "    new copied as addr = memcpy(&p, &replacement, 8)\n"
       "    return [2]*p\n"
       "}\n",
       "@hs_check_load"},
      {"func main() -> i32 {\n"
       "    new large[8] as bytes\n"
       "    new small[1] as bytes\n"
       "    new p as addr = &large\n"
       "    new replacement as addr = &small\n"
       "    new moved as addr = memmove(&p, &replacement, 8)\n"
       "    return [2]*p\n"
       "}\n",
       "@hs_check_load"},
      {"func main() -> i32 {\n"
       "    new divisor as i64 = 0\n"
       "    new file as handle\n"
       "    new read as u64 = fread(divisor, 8, 1, file)\n"
       "    new quotient as i64 = 1 / divisor\n"
       "    return 0\n"
       "}\n",
       "@hs_checked_division_by_zero"}};

  for (const auto &[source, requiredIr] : cases) {
    const auto result = emitSource(source, checked);
    HS_EXPECT_TRUE(result.diagnostics.empty());
    HS_EXPECT_TRUE(llvmIr(result).find(requiredIr) != std::string::npos);
  }
}

HS_TEST(LLVMCodegen_CheckedInvalidatesGlobalFactsAfterOrdinaryCall) {
  const auto result = emitSource(
      "new large[8] as bytes\n"
      "new small[1] as bytes\n"
      "new p as addr = &large\n"
      "func poison() {\n"
      "    p = &small\n"
      "}\n"
      "func main() -> i32 {\n"
      "    poison()\n"
      "    return [2]*p\n"
      "}\n",
      optionsFor(hitsimple::codegen::SafetyMode::Checked));

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_check_load") != std::string::npos);
}

HS_TEST(LLVMCodegen_CheckedInvalidatesEscapedLocalFactsAfterOrdinaryCall) {
  const auto result = emitSource(
      "new small[1] as bytes\n"
      "func poison(slot as addr) {\n"
      "    [8]*slot = &small\n"
      "}\n"
      "func main() -> i32 {\n"
      "    new large[8] as bytes\n"
      "    new p as addr = &large\n"
      "    poison(&p)\n"
      "    return [2]*p\n"
      "}\n",
      optionsFor(hitsimple::codegen::SafetyMode::Checked));

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_check_load") != std::string::npos);
}

HS_TEST(LLVMCodegen_UsesRhsSemanticsForAddressOffsetWidening) {
  const auto result = emitSource(
      "func main() -> i32 {\n"
      "    new buffer[4] as bytes\n"
      "    new base as addr = &buffer[1]\n"
      "    new negative8 as i8 = -1\n"
      "    new negative16 as i16 = -1\n"
      "    new positive8 as u8 = 1\n"
      "    new before8 as addr = base? + negative8\n"
      "    new before16 as addr = base? + negative16\n"
      "    new after as addr = base? + positive8\n"
      "    [1]*before8 = 1\n"
      "    [1]*before16 = 2\n"
      "    [1]*after = 3\n"
      "    return 0\n"
      "}\n",
      optionsFor(hitsimple::codegen::SafetyMode::Checked));

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("sext i8") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("sext i16") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("zext i8") != std::string::npos);
}

HS_TEST(LLVMCodegen_CheckedRejectsDistortedIntegerCastAddressFacts) {
  const auto result = emitSource(
      "func main() -> i32 {\n"
      "    new large[8] as bytes\n"
      "    new p as addr = &large\n"
      "    new narrowed as i32 = to_i32(p)\n"
      "    new widened as u64 = to_u64(narrowed)\n"
      "    new restored as addr = widened\n"
      "    return [2]*restored\n"
      "}\n",
      optionsFor(hitsimple::codegen::SafetyMode::Checked));

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_check_load") != std::string::npos);
}

HS_TEST(LLVMCodegen_StaticCheckedMergesReallocFailureWithSuccess) {
  const auto result = emitSource(
      "func main() -> i32 {\n"
      "    new p as addr = alloc(1)\n"
      "    new alias as addr = p\n"
      "    p = realloc(p, 2)\n"
      "    free(alias)\n"
      "    return 0\n"
      "}\n",
      optionsFor(hitsimple::codegen::SafetyMode::StaticChecked));

  HS_EXPECT_TRUE(result.diagnostics.empty());
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
  HS_EXPECT_TRUE(llvmIr(result).find("@malloc") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@realloc") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@free") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_alloc") == std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_realloc") == std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_check_store") == std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_free") == std::string::npos);
}

HS_TEST(LLVMCodegen_StaticCheckedRejectsStaticMemoryErrors) {
  auto index =
      emitSource("func main() {\n"
                 "    new a[4]\n"
                 "    a[4] = 1\n"
                 "    return 0\n"
                 "}\n",
                 optionsFor(hitsimple::codegen::SafetyMode::StaticChecked));
  HS_EXPECT_TRUE(!index.diagnostics.empty());
  HS_EXPECT_TRUE(index.diagnostics[0].find("out of bounds") !=
                 std::string::npos);

  auto nullStore =
      emitSource("func main() {\n"
                 "    new p as addr = 0\n"
                 "    [1]*p = 1\n"
                 "    return 0\n"
                 "}\n",
                 optionsFor(hitsimple::codegen::SafetyMode::StaticChecked));
  HS_EXPECT_TRUE(!nullStore.diagnostics.empty());
  HS_EXPECT_TRUE(nullStore.diagnostics[0].find("null address store") !=
                 std::string::npos);

  auto staticPointerStore =
      emitSource("func main() {\n"
                 "    new a[4]\n"
                 "    [1]*((&a)? + 4) = 1\n"
                 "    return 0\n"
                 "}\n",
                 optionsFor(hitsimple::codegen::SafetyMode::StaticChecked));
  HS_EXPECT_TRUE(!staticPointerStore.diagnostics.empty());
  HS_EXPECT_TRUE(staticPointerStore.diagnostics[0].find("memory store out of "
                                                        "bounds") !=
                 std::string::npos);

  auto staticPointerLoad =
      emitSource("func main() {\n"
                 "    new a[4]\n"
                 "    return [1]*((&a)? + 4)\n"
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
  HS_EXPECT_TRUE(llvmIr(result).find("store i32 7") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("str.copy.active") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("str.copy.byte") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("ret i32") != std::string::npos);
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
  HS_EXPECT_TRUE(llvmIr(result).find("define i32 @add_one(i32") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call i32 @add_one") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("define i32 @main()") != std::string::npos);
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
  HS_EXPECT_TRUE(llvmIr(result).find("define { i32, i32 } @pair(i32") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call { i32, i32 } @pair") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("extractvalue { i32, i32 }") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_PreservesTypedFloatingMultiReturnAbi) {
  auto result =
      emitSource("func pair(value as f64) -> (first as f64, second as f64) {\n"
                 "    return value, value %f+ 1.0\n"
                 "}\n"
                 "func main() {\n"
                 "    new first as f64\n"
                 "    new second as f64\n"
                 "    first, second = pair(40.0)\n"
                 "    return 0\n"
                 "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("define { double, double } @pair(double") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call { double, double } @pair(double") !=
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
  HS_EXPECT_TRUE(llvmIr(result).find("alloca [16 x i8]") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("getelementptr inbounds [16 x i8], ptr %p, "
                                    "i32 0, i32 12") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i32 8") != std::string::npos);
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
  HS_EXPECT_TRUE(llvmIr(result).find("str.copy.byte") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("getelementptr inbounds [16 x i8], ptr %p, "
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
  HS_EXPECT_TRUE(llvmIr(result).find("icmp slt i32") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("zext i1") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i8") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsExternalFunctionDeclarationAndCall) {
  auto result = emitSource("extern host_counter[4]\n"
                           "extern host_inc(value[4]) -> [4]\n"
                           "func main() {\n"
                           "    return host_inc(host_counter)\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@host_counter = external global "
                                    "[4 x i8]") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("declare i32 @host_inc(i32)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call i32 @host_inc") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsExplicitCAbiImportsAndExports) {
  auto result =
      emitSource("extern \"C\" native_scale(value as f64) -> f64\n"
                 "extern \"C\" sink(text as cstr) -> ()\n"
                 "extern \"C\" func hsc_increment(value as i32) -> i32 {\n"
                 "    return value %d+ 1\n"
                 "}\n"
                 "func main() {\n"
                 "    sink(\"ok\")\n"
                 "    return hsc_increment(41)\n"
                 "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("declare double @native_scale(double)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("declare void @sink(ptr)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("define i32 @hsc_increment(i32") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call i32 @hsc_increment(i32 41)") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_AppliesInternalLinkageOverridesToDefinitions) {
  auto parseResult =
      hitsimple::parser::parseSource("new file_local[4]\n"
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

  auto result = hitsimple::codegen::emitLlvmModule(*analyzeResult.unit, "test.hs");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@file_local = internal global "
                                    "[4 x i8] zeroinitializer") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("define internal i32 @file_helper()") !=
                 std::string::npos);
}

HS_TEST(Hir_LinkageOverrideRejectsExternDeclarationsAtomically) {
  auto parseResult =
      hitsimple::parser::parseSource("new visible[4]\n"
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

  auto result = hitsimple::codegen::emitLlvmModule(*analyzeResult.unit, "test.hs");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@errno = external global i32") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("define double @host_identity(double") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("ret double") != std::string::npos);
}

HS_TEST(LLVMCodegen_RejectsAggregateCAbiForNonSysVElfTarget) {
  auto parseResult =
      hitsimple::parser::parseSource("extern host_pass(value[8]) -> [8]\n"
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
      hitsimple::codegen::emitLlvmModule(*analyzeResult.unit, "test.hs", options);

  HS_EXPECT_TRUE(llvmIr(result).empty());
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "C aggregate by-value ABI is only supported for x86_64 "
                     "SysV ELF targets") != std::string::npos);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "x86_64-apple-darwin") != std::string::npos);
}

HS_TEST(LLVMCodegen_RejectsAggregateCAbiForX32Target) {
  auto parseResult =
      hitsimple::parser::parseSource("extern host_pass(value[8]) -> [8]\n"
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
      hitsimple::codegen::emitLlvmModule(*analyzeResult.unit, "test.hs", options);

  HS_EXPECT_TRUE(llvmIr(result).empty());
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "C aggregate by-value ABI is only supported for x86_64 "
                     "SysV ELF targets") != std::string::npos);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "x86_64-unknown-linux-gnux32") != std::string::npos);
}

HS_TEST(LLVMCodegen_RejectsIndirectAggregateCAbiForX32Target) {
  auto parseResult =
      hitsimple::parser::parseSource("extern host_weighted(value[24]) -> [24]\n"
                                     "func main() {\n"
                                     "    return 0\n"
                                     "}\n",
                                     "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());

  auto analyzeResult = hitsimple::sema::analyze(*parseResult.unit);
  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());

  hitsimple::hir::AbiType weighted{hitsimple::hir::AbiValueKind::Aggregate, 24,
                                   false, "Weighted"};
  weighted.alignment = 8;
  weighted.aggregateFields = {
      {hitsimple::hir::AbiValueKind::Integer, 8, true},
      {hitsimple::hir::AbiValueKind::Integer, 8, true},
      {hitsimple::hir::AbiValueKind::Integer, 8, true},
  };
  weighted.aggregateFieldOffsets = {0, 8, 16};
  const std::vector<hitsimple::hir::AbiOverride> overrides = {
      {"host_weighted", hitsimple::hir::LinkageTarget::Function, std::nullopt,
       cCompatibilitySignature({weighted}, {weighted})},
  };
  const auto overrideDiagnostics =
      hitsimple::hir::applyAbiOverrides(*analyzeResult.unit, overrides);
  HS_EXPECT_TRUE(overrideDiagnostics.empty());

  hitsimple::codegen::CodegenOptions options;
  options.targetTriple = "x86_64-unknown-linux-gnux32";
  const auto result =
      hitsimple::codegen::emitLlvmModule(*analyzeResult.unit, "test.hs", options);

  HS_EXPECT_TRUE(llvmIr(result).empty());
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "C aggregate by-value ABI is only supported for x86_64 "
                     "SysV ELF targets") != std::string::npos);
  HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                     "x86_64-unknown-linux-gnux32") != std::string::npos);
}

HS_TEST(LLVMCodegen_RejectsIndirectAggregateCAbiForDarwinTarget) {
  auto parseResult =
      hitsimple::parser::parseSource("extern host_weighted(value[24]) -> [24]\n"
                                     "func main() {\n"
                                     "    return 0\n"
                                     "}\n",
                                     "test.hs");
  HS_EXPECT_TRUE(parseResult.unit != nullptr);
  HS_EXPECT_TRUE(parseResult.error.empty());

  auto analyzeResult = hitsimple::sema::analyze(*parseResult.unit);
  HS_EXPECT_TRUE(analyzeResult.unit != nullptr);
  HS_EXPECT_TRUE(analyzeResult.diagnostics.empty());

  hitsimple::hir::AbiType weighted{hitsimple::hir::AbiValueKind::Aggregate, 24,
                                   false, "Weighted"};
  weighted.alignment = 8;
  weighted.aggregateFields = {
      {hitsimple::hir::AbiValueKind::Integer, 8, true},
      {hitsimple::hir::AbiValueKind::Integer, 8, true},
      {hitsimple::hir::AbiValueKind::Integer, 8, true},
  };
  weighted.aggregateFieldOffsets = {0, 8, 16};
  const std::vector<hitsimple::hir::AbiOverride> overrides = {
      {"host_weighted", hitsimple::hir::LinkageTarget::Function, std::nullopt,
       cCompatibilitySignature({weighted}, {weighted})},
  };
  const auto overrideDiagnostics =
      hitsimple::hir::applyAbiOverrides(*analyzeResult.unit, overrides);
  HS_EXPECT_TRUE(overrideDiagnostics.empty());

  hitsimple::codegen::CodegenOptions options;
  options.targetTriple = "x86_64-apple-darwin";
  const auto result =
      hitsimple::codegen::emitLlvmModule(*analyzeResult.unit, "test.hs", options);

  HS_EXPECT_TRUE(llvmIr(result).empty());
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
  auto parseResult =
      hitsimple::parser::parseSource("extern host_pass(value[8]) -> [8]\n"
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
      hitsimple::codegen::emitLlvmModule(*analyzeResult.unit, "test.hs", options);

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(
      llvmIr(result).find("target triple = \"x86_64-pc-linux-gnu\"") !=
      std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("declare i64 @host_pass(i64)") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_RejectsMultipleReturnsForCCompatibilitySignature) {
  auto parseResult =
      hitsimple::parser::parseSource("func pair(value[4]) -> ([4], [4]) {\n"
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
      hitsimple::codegen::emitLlvmModule(*analyzeResult.unit, "test.hs");

  HS_EXPECT_TRUE(llvmIr(result).empty());
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

  auto result = hitsimple::codegen::emitLlvmModule(*analyzeResult.unit, "test.hs");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("declare void @host_sink(double, ptr)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call void @host_sink(double 1.250000e+00, "
                                    "ptr null)") != std::string::npos);
}

HS_TEST(LLVMCodegen_UsesFloatingExternReturnInFloatAssignment) {
  auto parseResult =
      hitsimple::parser::parseSource("extern host_value() -> f64\n"
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

  auto result = hitsimple::codegen::emitLlvmModule(*analyzeResult.unit, "test.hs");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("declare double @host_value()") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call double @host_value()") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store double") != std::string::npos);
}

HS_TEST(Hir_AbiOverridesRejectMismatchedSignaturesAtomically) {
  auto parseResult =
      hitsimple::parser::parseSource("extern host_id(value[4]) -> [4]\n"
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
  HS_EXPECT_TRUE(llvmIr(result).find("@counter = internal global "
                                    "[4 x i8] zeroinitializer") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("add i32") != std::string::npos);
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
  HS_EXPECT_TRUE(llvmIr(result).find("alloca [24 x i8]") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("i32 20") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("store i32 9") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsAddressOfStaticAndGlobalMemory) {
  auto result = emitSource("new global[4]\n"
                           "func main() {\n"
                           "    static local[4]\n"
                           "    new gptr[8]\n"
                           "    new lptr[8]\n"
                           "    gptr = &global\n"
                           "    lptr = &local\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@global = global") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@local = internal global") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("ptrtoint (ptr @global to i64)") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("ptrtoint (ptr @local to i64)") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_UsesSourceSemanticsForIntegerWidening) {
  auto result = emitSource("func widen(value as u32) -> u64 {\n"
                           "    return value\n"
                           "}\n"
                           "func main() {\n"
                           "    new source as u32 = 16\n"
                           "    new stored as u64 = source\n"
                           "    new returned as u64 = widen(source)\n"
                           "    new address as addr = alloc(source)\n"
                           "    free(address)\n"
                           "    return stored + returned\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("zext i32") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("sext i32") == std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call ptr @malloc(i64") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_PreservesViewSignednessDuringWidening) {
  const auto result = emitSource(
      "func main() -> i32 {\n"
      "    new raw as u8 = 255\n"
      "    new unsigned as u64 = raw as u8\n"
      "    new signed as i64 = raw as i8\n"
      "    if (unsigned != 255) {\n"
      "        return 1\n"
      "    }\n"
      "    if (signed != -1) {\n"
      "        return 2\n"
      "    }\n"
      "    return 0\n"
      "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto ir = llvmIr(result);
  HS_EXPECT_TRUE(ir.find("intcast.zext") != std::string::npos);
  HS_EXPECT_TRUE(ir.find("intcast.sext") != std::string::npos);
}

HS_TEST(LLVMCodegen_LowersBooleanTestsForCompleteStaticAndDynamicViews) {
  auto result = emitSource("func main() {\n"
                           "    new wide as u64 = 4294967296\n"
                           "    new fixed[3] as bytes\n"
                           "    fixed[2] = 1\n"
                           "    new length as u64 = 3\n"
                           "    if (wide) { }\n"
                           "    if (fixed) { }\n"
                           "    if (resize_bytes(fixed, length)) { }\n"
                           "    return 0\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("load i64, ptr %wide") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("cond.byte.nonzero") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("call i32 @hs_view_any_nonzero") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("trunc i64 to i32") ==
                 std::string::npos);
}

HS_TEST(LLVMCodegen_UsesRuntimeBooleanTestForLargeStaticViews) {
  const auto result = emitSource("func main() {\n"
                                 "    new value[1048577]\n"
                                 "    return !value\n"
                                 "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto ir = llvmIr(result);
  HS_EXPECT_TRUE(ir.find("call i32 @hs_view_any_nonzero") !=
                 std::string::npos);
  HS_EXPECT_TRUE(ir.find("cond.byte.nonzero") == std::string::npos);
}

HS_TEST(LLVMCodegen_UsesOnlyProvenAlignmentForPackedViews) {
  auto result = emitSource("template Packed {\n"
                           "    marker[1] as u8\n"
                           "    value[8] as u64\n"
                           "}\n"
                           "func main() {\n"
                           "    new packed as Packed\n"
                           "    packed.value = 1\n"
                           "    new copied as u64 = packed.value\n"
                           "    return copied\n"
                           "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("store i64 1, ptr %packed.addr, align 1") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("load i64, ptr %packed.addr") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("load i64, ptr %packed.addr1, align 1") !=
                 std::string::npos);
}

HS_TEST(LLVMCodegen_CheckedArithmeticGuardsAvoidPoison) {
  auto result = emitSource("func arithmetic(divisor as i32, count as i32, "
                           "exponent as i32) -> i32 {\n"
                           "    return 1 / divisor + (1 << count) + (2 ** exponent)\n"
                           "}\n"
                           "func main() {\n"
                           "    return arithmetic(1, 0, 0)\n"
                           "}\n",
                           optionsFor(hitsimple::codegen::SafetyMode::Checked));

  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_checked_division_by_zero") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_checked_negative_shift") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_checked_negative_exponent") !=
                 std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("div.special") != std::string::npos);
  HS_EXPECT_TRUE(llvmIr(result).find("phi i32") != std::string::npos);
}

HS_TEST(LLVMCodegen_ReadsBooleanValuesOnlyAtSemanticBoundaries) {
  auto result = emitSource(
      "func equal(a as bool, b as bool) -> u8 { return (a == b) as u8; }\n"
      "func integral(value as bool) -> i64 { return to_i64(value); }\n"
      "func floating(value as bool) -> f64 { return to_f64(value); }\n"
      "func returned(value as bool) -> bool { return value; }\n"
      "func bytes_of(value as bool) -> u8 { return value as u8; }\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.module != nullptr);
  HS_EXPECT_TRUE(!llvm::verifyModule(*result.module));
  for (const auto &[name, expectedReads] :
       std::vector<std::pair<std::string, unsigned>>{
           {"equal", 2}, {"integral", 1}, {"floating", 1},
           {"returned", 1}, {"bytes_of", 0}}) {
    const auto *function = result.module->getFunction(name);
    HS_EXPECT_TRUE(function != nullptr);
    unsigned reads = 0;
    for (const auto &block : *function) {
      for (const auto &instruction : block) {
        if (instruction.getName().str().rfind("cond.nonzero", 0) == 0) {
          const auto *compare = llvm::dyn_cast<llvm::ICmpInst>(&instruction);
          HS_EXPECT_TRUE(compare != nullptr);
          HS_EXPECT_EQ(compare->getPredicate(), llvm::CmpInst::ICMP_NE);
          HS_EXPECT_TRUE(compare->getOperand(0)->getType()->isIntegerTy(8));
          ++reads;
        }
      }
    }
    HS_EXPECT_EQ(reads, expectedReads);
  }
}

HS_TEST(LLVMCodegen_NormalizesFeofBeforeNarrowing) {
  for (const auto mode : {hitsimple::codegen::SafetyMode::Unchecked,
                          hitsimple::codegen::SafetyMode::StaticChecked,
                          hitsimple::codegen::SafetyMode::Checked}) {
    auto result = emitSource(
        "func ended(file as handle) -> u8 { return feof(file) as u8; }\n"
        "func failed(file as handle) -> i32 { return ferror(file); }\n"
        "func flushed(file as handle) -> i32 { return fflush(file); }\n"
        "func closed(file as handle) -> i32 { return fclose(file); }\n"
        "func main() -> i32 { return 0; }\n", optionsFor(mode));
    HS_EXPECT_TRUE(result.diagnostics.empty());
    HS_EXPECT_TRUE(result.module != nullptr);
    HS_EXPECT_TRUE(!llvm::verifyModule(*result.module));
    unsigned normalized = 0;
    for (const auto &function : *result.module) {
      for (const auto &block : function) {
        for (const auto &instruction : block) {
          if (instruction.getName() != "feof.bool") {
            continue;
          }
          HS_EXPECT_EQ(instruction.getOpcode(), llvm::Instruction::ZExt);
          HS_EXPECT_TRUE(instruction.getType()->isIntegerTy(8));
          const auto *compare = llvm::dyn_cast<llvm::ICmpInst>(
              instruction.getOperand(0));
          HS_EXPECT_TRUE(compare != nullptr);
          HS_EXPECT_EQ(compare->getPredicate(), llvm::CmpInst::ICMP_NE);
          HS_EXPECT_TRUE(compare->getOperand(0)->getType()->isIntegerTy(32));
          const auto *call = llvm::dyn_cast<llvm::CallInst>(compare->getOperand(0));
          HS_EXPECT_TRUE(call != nullptr);
          const auto *callee = call->getCalledFunction();
          HS_EXPECT_TRUE(callee != nullptr);
          if (mode == hitsimple::codegen::SafetyMode::Checked) {
            // Checked calls go through a source-location wrapper.
            unsigned runtimeCalls = 0;
            for (const auto &wrapperBlock : *callee) {
              for (const auto &wrapperInstruction : wrapperBlock) {
                const auto *runtimeCall =
                    llvm::dyn_cast<llvm::CallInst>(&wrapperInstruction);
                if (runtimeCall != nullptr && runtimeCall->getCalledFunction() != nullptr &&
                    runtimeCall->getCalledFunction()->getName() == "hs_feof") {
                  ++runtimeCalls;
                }
              }
            }
            HS_EXPECT_EQ(runtimeCalls, 1U);
          } else {
            HS_EXPECT_EQ(callee->getName().str(), std::string("feof"));
          }
          ++normalized;
        }
      }
    }
    HS_EXPECT_EQ(normalized, 1U);
  }
}

HS_TEST(LLVMCodegen_BooleanReturnsReadComputedFloatAndDynamicViewBytes) {
  auto result = emitSource(
      "func sum(a as f64, b as f64) -> bool { return a + b; }\n"
      "func converted(value as i32) -> bool { return to_f64(value); }\n"
      "func resized(value as u32, count as u64) -> bool {\n"
      "  return resize_bytes(value, count)\n"
      "}\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.module != nullptr);
  HS_EXPECT_TRUE(!llvm::verifyModule(*result.module));
  const auto ir = llvmIr(result);
  HS_EXPECT_TRUE(ir.find("fadd double") != std::string::npos);
  HS_EXPECT_TRUE(ir.find("sitofp i32") != std::string::npos);
  HS_EXPECT_TRUE(ir.find("fcmp") == std::string::npos);
  HS_EXPECT_TRUE(ir.find("call i32 @hs_view_any_nonzero") != std::string::npos);
}

HS_TEST(LLVMCodegen_EmitsExplicitNumericReturnConversions) {
  for (const auto mode : {hitsimple::codegen::SafetyMode::Unchecked,
                          hitsimple::codegen::SafetyMode::StaticChecked,
                          hitsimple::codegen::SafetyMode::Checked}) {
    auto result = emitSource(
        "func direct(value as i8) -> i32 { return to_i32(value); }\n"
        "func nested(value as i8) -> i64 { return to_i64(to_i32(value)); }\n"
        "func widen(value as i8) -> i64 { return to_i32(value); }\n"
        "func unsigned_value(value as u8) -> u64 { return to_u64(value); }\n"
        "func floating(value as f64) -> i32 { return to_i32(value); }\n"
        "func main() -> i32 { return 0; }\n", optionsFor(mode));
    HS_EXPECT_TRUE(result.diagnostics.empty());
    HS_EXPECT_TRUE(result.module != nullptr);
    HS_EXPECT_TRUE(!llvm::verifyModule(*result.module));
    for (const auto &[name, width] :
         std::vector<std::pair<std::string, unsigned>>{
             {"direct", 32}, {"nested", 64}, {"widen", 64},
             {"unsigned_value", 64}, {"floating", 32}}) {
      const auto *function = result.module->getFunction(name);
      HS_EXPECT_TRUE(function != nullptr);
      HS_EXPECT_TRUE(function->getReturnType()->isIntegerTy(width));
    }
    const auto ir = llvmIr(result);
    HS_EXPECT_TRUE(ir.find("sext i8") != std::string::npos);
    HS_EXPECT_TRUE(ir.find("sext i32") != std::string::npos);
    HS_EXPECT_TRUE(ir.find("zext i8") != std::string::npos);
    HS_EXPECT_TRUE(ir.find("fptosi double") != std::string::npos);
  }
}

HS_TEST(LLVMCodegen_FloatingTernariesCopySelectedViewBytes) {
  for (const auto mode : {hitsimple::codegen::SafetyMode::Unchecked,
                          hitsimple::codegen::SafetyMode::StaticChecked,
                          hitsimple::codegen::SafetyMode::Checked}) {
    for (const unsigned bits : {16U, 32U, 64U, 128U}) {
      const auto type = "f" + std::to_string(bits);
      auto result = emitSource(
          "func truth(choose as bool, left as " + type + ", right as " + type +
          ") -> bool { return choose ? left : right; }\n"
          "func selected(choose as bool, left as " + type + ", right as " + type +
          ") -> " + type + " { return choose ? left : right; }\n"
          "func main() -> i32 { return 0; }\n", optionsFor(mode));
      HS_EXPECT_TRUE(result.diagnostics.empty());
      HS_EXPECT_TRUE(result.module != nullptr);
      HS_EXPECT_TRUE(!llvm::verifyModule(*result.module));
      for (const auto name : {"truth", "selected"}) {
        const auto *function = result.module->getFunction(name);
        HS_EXPECT_TRUE(function != nullptr);
        unsigned copies = 0;
        unsigned rawTests = 0;
        for (const auto &block : *function) {
          for (const auto &instruction : block) {
            if (const auto *copy = llvm::dyn_cast<llvm::MemCpyInst>(&instruction)) {
              const auto *length =
                  llvm::dyn_cast<llvm::ConstantInt>(copy->getLength());
              HS_EXPECT_TRUE(length != nullptr);
              HS_EXPECT_EQ(length->getZExtValue(), bits / 8U);
              HS_EXPECT_TRUE(block.getName().str().rfind("view.ternary.", 0) == 0);
              ++copies;
            }
            if (const auto *test = llvm::dyn_cast<llvm::ICmpInst>(&instruction);
                test != nullptr &&
                test->getOperand(0)->getType()->isIntegerTy(bits) &&
                instruction.getName().str().rfind("cond.nonzero", 0) == 0) {
              HS_EXPECT_EQ(test->getPredicate(), llvm::CmpInst::ICMP_NE);
              ++rawTests;
            }
            HS_EXPECT_TRUE(!llvm::isa<llvm::FCmpInst>(instruction));
            HS_EXPECT_TRUE(!llvm::isa<llvm::FPToSIInst>(instruction));
            HS_EXPECT_TRUE(!llvm::isa<llvm::FPToUIInst>(instruction));
            HS_EXPECT_TRUE(!llvm::isa<llvm::FPExtInst>(instruction));
            HS_EXPECT_TRUE(!llvm::isa<llvm::FPTruncInst>(instruction));
          }
        }
        HS_EXPECT_EQ(copies, 2U);
        HS_EXPECT_EQ(rawTests, std::string_view(name) == "truth" ? 1U : 0U);
      }
      HS_EXPECT_EQ(result.module->getFunction("selected")
                       ->getReturnType()->getScalarSizeInBits(), bits);
    }
  }
}

HS_TEST(LLVMCodegen_FloatingTernaryEvaluatesOnlySelectedBranch) {
  auto result = emitSource(
      "func predicate() -> bool { return true; }\n"
      "func left() -> f64 { return 1.0; }\n"
      "func right() -> f64 { return 0.0; }\n"
      "func probe() -> bool { return predicate() ? left() : right(); }\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.module != nullptr);
  HS_EXPECT_TRUE(!llvm::verifyModule(*result.module));
  const auto *function = result.module->getFunction("probe");
  HS_EXPECT_TRUE(function != nullptr);
  unsigned predicates = 0;
  unsigned leftCalls = 0;
  unsigned rightCalls = 0;
  for (const auto &block : *function) {
    for (const auto &instruction : block) {
      const auto *call = llvm::dyn_cast<llvm::CallInst>(&instruction);
      if (call == nullptr || call->getCalledFunction() == nullptr) {
        continue;
      }
      const auto name = call->getCalledFunction()->getName();
      if (name == "left") {
        HS_EXPECT_EQ(block.getName().str(), std::string("view.ternary.then"));
        ++leftCalls;
      } else if (name == "right") {
        HS_EXPECT_EQ(block.getName().str(), std::string("view.ternary.else"));
        ++rightCalls;
      } else if (name == "predicate") {
        HS_EXPECT_TRUE(block.getName().str().rfind("view.ternary.", 0) != 0);
        ++predicates;
      }
    }
  }
  HS_EXPECT_EQ(predicates, 1U);
  HS_EXPECT_EQ(leftCalls, 1U);
  HS_EXPECT_EQ(rightCalls, 1U);
}

HS_TEST(LLVMCodegen_TernaryAsRetainsBranchLocalViewCopies) {
  for (const unsigned bits : {16U, 32U, 64U, 128U}) {
    const auto type = "f" + std::to_string(bits);
    const auto size = std::to_string(bits / 8U);
    auto result = emitSource(
        "func predicate() -> bool { return true; }\n"
        "func left(value as " + type + ") -> " + type + " { return value; }\n"
        "func right(value as " + type + ") -> " + type + " { return value; }\n"
        "func probe(a as " + type + ", b as " + type + ") -> [" + size +
        "] as bytes { return (predicate() ? left(a) : right(b)) as bytes; }\n"
        "func main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.diagnostics.empty());
    HS_EXPECT_TRUE(result.module != nullptr);
    HS_EXPECT_TRUE(!llvm::verifyModule(*result.module));
    const auto *function = result.module->getFunction("probe");
    HS_EXPECT_TRUE(function != nullptr);
    unsigned predicates = 0;
    unsigned leftCalls = 0;
    unsigned rightCalls = 0;
    for (const auto &block : *function) {
      for (const auto &instruction : block) {
        HS_EXPECT_TRUE(!llvm::isa<llvm::FPToSIInst>(instruction));
        HS_EXPECT_TRUE(!llvm::isa<llvm::FPToUIInst>(instruction));
        HS_EXPECT_TRUE(!llvm::isa<llvm::FPExtInst>(instruction));
        HS_EXPECT_TRUE(!llvm::isa<llvm::FPTruncInst>(instruction));
        const auto *call = llvm::dyn_cast<llvm::CallInst>(&instruction);
        if (call == nullptr || call->getCalledFunction() == nullptr) {
          continue;
        }
        const auto name = call->getCalledFunction()->getName();
        if (name == "predicate") {
          HS_EXPECT_TRUE(block.getName().str().rfind("view.ternary.", 0) != 0);
          ++predicates;
        } else if (name == "left" || name == "right") {
          HS_EXPECT_EQ(block.getName().str(), name == "left"
              ? std::string("view.ternary.then")
              : std::string("view.ternary.else"));
          if (name == "left") {
            ++leftCalls;
          } else {
            ++rightCalls;
          }
        }
      }
    }
    HS_EXPECT_EQ(predicates, 1U);
    HS_EXPECT_EQ(leftCalls, 1U);
    HS_EXPECT_EQ(rightCalls, 1U);
  }
}

HS_TEST(LLVMCodegen_PreservesFloatingRawMemoryBuiltinViews) {
  for (const auto bytes : {2U, 4U, 8U, 16U}) {
    const auto type = "f" + std::to_string(bytes * 8U);
    auto result = emitSource(
        "func probe(value as " + type + ") -> i32 {\n"
        "    new target as " + type + "\n"
        "    new copied as addr = memcpy(target, value, " + std::to_string(bytes) + ")\n"
        "    memset(target, value, " + std::to_string(bytes) + ")\n"
        "    new filled as addr = memset(target, value, " + std::to_string(bytes) + ")\n"
        "    return memcmp(target, value, " + std::to_string(bytes) + ")\n}\n"
        "func main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.module != nullptr);
    HS_EXPECT_TRUE(result.diagnostics.empty());
    HS_EXPECT_TRUE(!llvm::verifyModule(*result.module, &llvm::errs()));
    const auto ir = llvmIr(result);
    HS_EXPECT_TRUE(ir.find("call ptr @memcpy") != std::string::npos);
    HS_EXPECT_TRUE(ir.find("call ptr @memset") != std::string::npos);
    HS_EXPECT_TRUE(ir.find("call i32 @memcmp") != std::string::npos);
    HS_EXPECT_TRUE(ir.find("view.low.byte = load i8") != std::string::npos);
    HS_EXPECT_TRUE(ir.find("fptosi") == std::string::npos);
    HS_EXPECT_TRUE(ir.find("fptoui") == std::string::npos);
  }
}

HS_TEST(LLVMCodegen_CopiesFloatingAssignmentResultViews) {
  for (const auto bytes : {2U, 4U, 8U, 16U}) {
    const auto type = "f" + std::to_string(bytes * 8U);
    auto result = emitSource(
        "func probe(left as " + type + ", right as " + type + ") -> i32 {\n"
        "    return memcmp((left = right), (left = right), " +
        std::to_string(bytes) + ")\n}\n"
        "func assigned(left as " + type + ", right as " + type + ") -> " +
        type + " { return left = right; }\n"
        "func main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.module != nullptr);
    HS_EXPECT_TRUE(result.diagnostics.empty());
    HS_EXPECT_TRUE(!llvm::verifyModule(*result.module, &llvm::errs()));
    const auto ir = llvmIr(result);
    HS_EXPECT_TRUE(ir.find("view.assignment = alloca") != std::string::npos);
    HS_EXPECT_TRUE(ir.find("llvm.memcpy") != std::string::npos);
    HS_EXPECT_TRUE(ir.find("fptosi") == std::string::npos);
    HS_EXPECT_TRUE(ir.find("fptoui") == std::string::npos);
  }
}

HS_TEST(LLVMCodegen_LengthEvaluatesViewWithoutReadingStorage) {
  const auto result = emitSource(
      "func tick() -> u64 { return 42; }\n"
      "func call_length() -> u64 { return length(tick()); }\n"
      "func local_length() -> u64 { new untouched[17]; return length(untouched); }\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto ir = llvmIr(result);
  HS_EXPECT_TRUE(ir.find("call i64 @tick()") != std::string::npos);
  const auto *function = result.module->getFunction("local_length");
  HS_EXPECT_TRUE(function != nullptr);
  for (const auto &block : *function) {
    for (const auto &instruction : block) {
      HS_EXPECT_TRUE(!llvm::isa<llvm::LoadInst>(instruction));
    }
  }
}

HS_TEST(LLVMCodegen_LengthChecksDynamicAsExtent) {
  const auto result = emitSource(
      "func probe(count as u64) -> u64 {\n"
      "    new value as u64 = 42\n"
      "    return length(resize_bytes(value, count) as u64)\n}\n"
      "func main() -> i32 { return 0; }\n",
      optionsFor(hitsimple::codegen::SafetyMode::Checked));
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_check_view_length") != std::string::npos);
}

HS_TEST(LLVMCodegen_LengthChecksDynamicVariableTemplateExtent) {
  const auto result = emitSource(
      "func probe(count as u64) -> u64 {\n"
      "    new value as u64 = 42\n"
      "    return length(resize_bytes(value, count) as bytes)\n}\n"
      "func main() -> i32 { return 0; }\n",
      optionsFor(hitsimple::codegen::SafetyMode::Checked));
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("@hs_check_view_nonempty") != std::string::npos);
}

HS_TEST(LLVMCodegen_MultiReturnDestinationContracts) {
  for (const auto *type : {"f16", "f32", "f64", "f128"}) {
    const std::string floating(type);
    auto result = emitSource(
        "func pair(value as " + floating + ") -> (first as " + floating +
        ", second as " + floating + ") { return value, value; }\n"
        "func main() -> i32 {\n  new flag[8]\n  new number as " + floating +
        "\n  (flag %b=), (number %f=) = pair(2.5)\n  return 0\n}\n");
    HS_EXPECT_TRUE(result.ok());
    HS_EXPECT_TRUE(!llvm::verifyModule(*result.module));
    std::size_t calls = 0;
    std::size_t components = 0;
    for (const auto &block : *result.module->getFunction("main")) {
      for (const auto &instruction : block) {
        if (const auto *call = llvm::dyn_cast<llvm::CallInst>(&instruction);
            call != nullptr && call->getCalledFunction() != nullptr &&
            call->getCalledFunction()->getName() == "pair") {
          ++calls;
        }
        if (llvm::isa<llvm::ExtractValueInst>(&instruction)) {
          ++components;
        }
      }
    }
    HS_EXPECT_EQ(calls, 1U);
    HS_EXPECT_EQ(components, 2U);
    HS_EXPECT_TRUE(llvmIr(result).find("fptosi") == std::string::npos);
    HS_EXPECT_TRUE(llvmIr(result).find("fptoui") == std::string::npos);
  }
  auto widths = emitSource(
      "func pair(value as f64) -> (first as f64, second as f64) {\n"
      "  return value, value\n}\n"
      "func main() -> i32 { new a as f32; new b as f64;\n"
      "  a, b = pair(2.5)\n  return 0\n}\n");
  HS_EXPECT_TRUE(widths.ok());
  HS_EXPECT_TRUE(!llvm::verifyModule(*widths.module));
  HS_EXPECT_TRUE(llvmIr(widths).find("fptrunc double") != std::string::npos);
}

HS_TEST(LLVMCodegen_CharacterClassifiersObserveOneRawByte) {
  for (const auto width : {16, 32, 64, 128}) {
    const auto type = "f" + std::to_string(width);
    auto result = emitSource(
        "func probe(value as " + type + ") -> u8 {\n"
        "    new result as u8 = is_digit(value) as u8\n"
        "    result = is_alpha(value) as u8\n"
        "    result = is_alnum(value) as u8\n"
        "    result = is_space(value) as u8\n"
        "    result = is_lower(value) as u8\n"
        "    result = is_upper(value) as u8\n"
        "    result = is_xdigit(value) as u8\n"
        "    result = is_blank(value) as u8\n"
        "    result = is_control(value) as u8\n"
        "    result = is_print(value) as u8\n"
        "    result = is_graph(value) as u8\n"
        "    result = is_punct(value) as u8\n"
        "    result = to_upper(value)\n"
        "    return to_lower(value)\n"
        "}\n"
        "func main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.diagnostics.empty());
    HS_EXPECT_TRUE(result.module != nullptr);
    HS_EXPECT_TRUE(!llvm::verifyModule(*result.module));
    unsigned byteLoads = 0;
    for (const auto &function : *result.module) {
      for (const auto &block : function) {
        for (const auto &instruction : block) {
          const auto *load = llvm::dyn_cast<llvm::LoadInst>(&instruction);
          if (load && load->getName().str().rfind("view.low.byte", 0) == 0) {
            HS_EXPECT_TRUE(load->getType()->isIntegerTy(8));
            HS_EXPECT_EQ(load->getAlign().value(), 1U);
            ++byteLoads;
          }
          HS_EXPECT_TRUE(instruction.getOpcode() != llvm::Instruction::FPToSI);
          HS_EXPECT_TRUE(instruction.getOpcode() != llvm::Instruction::FPToUI);
        }
      }
    }
    HS_EXPECT_EQ(byteLoads, 14U);
  }
}

HS_TEST(LLVMCodegen_MultipleAssignmentFormsSourcesBeforeWrites) {
  auto result = emitSource(
      "new g as i32 = 3\n"
      "func observe() -> i32 { return g; }\n"
      "func main() -> i32 { new out as i32;\n"
      " g, out = 9, observe()\n _ = observe()\n return out\n}\n");
  HS_EXPECT_TRUE(result.ok());
  HS_EXPECT_TRUE(!llvm::verifyModule(*result.module));
  std::size_t calls = 0;
  bool wroteGlobal = false;
  for (const auto &block : *result.module->getFunction("main")) {
    for (const auto &instruction : block) {
      if (const auto *call = llvm::dyn_cast<llvm::CallInst>(&instruction);
          call != nullptr && call->getCalledFunction() != nullptr &&
          call->getCalledFunction()->getName() == "observe") {
        if (calls == 0) {
          HS_EXPECT_TRUE(!wroteGlobal);
        } else {
          HS_EXPECT_TRUE(wroteGlobal);
        }
        ++calls;
      }
      if (const auto *store = llvm::dyn_cast<llvm::StoreInst>(&instruction);
          store != nullptr && store->getPointerOperand()->stripPointerCasts() ==
                                  result.module->getNamedGlobal("g")) {
        HS_EXPECT_EQ(calls, 1U);
        wroteGlobal = true;
      }
    }
  }
  HS_EXPECT_EQ(calls, 2U);
  HS_EXPECT_TRUE(wroteGlobal);
}

HS_TEST(LLVMCodegen_MultipleAssignmentCapturedFloatingViews) {
  for (const auto *type : {"f16", "f32", "f64", "f128"}) {
    const std::string floating(type);
    auto result = emitSource(
        "func identity(value as " + floating + ") -> " + floating +
        " { return value; }\n"
        "func main() -> i32 { new a as " + floating +
        " = 2.5; new b as " + floating + ";\n"
        " a, b = identity(a), a\n return 0\n}\n");
    HS_EXPECT_TRUE(result.ok());
    HS_EXPECT_TRUE(!llvm::verifyModule(*result.module));
    const auto ir = llvmIr(result);
    HS_EXPECT_TRUE(ir.find("fptosi") == std::string::npos);
    HS_EXPECT_TRUE(ir.find("fptoui") == std::string::npos);
  }
}

HS_TEST(LLVMCodegen_MultipleAssignmentKeepsStaticSafetyEffects) {
  const auto options = optionsFor(hitsimple::codegen::SafetyMode::StaticChecked);
  for (const auto *statements : {
           "new zero as i32 = 0; _ = 8 / zero;",
           "new zero as i32 = 0; new a as i32; a, _ = 1, 8 / zero;",
           "new zero as i32 = 1; new a as i32; zero, a = 0, 1; a = 8 / zero;",
           "new zero as i32 = 1; new alias as i32; zero, alias = 0, zero; alias = 8 / alias;"}) {
    auto result = emitSource("func main() -> i32 { " + std::string(statements) +
                                 " return 0; }", options);
    HS_EXPECT_TRUE(!result.ok());
    HS_EXPECT_TRUE(!result.diagnostics.empty());
    HS_EXPECT_TRUE(result.diagnostics[0].message.find("division by zero") !=
                   std::string::npos);
  }
  // Assignment sees zero's new bytes through a captured lvalue. Freezing a
  // constant at capture time would incorrectly diagnose the following divide.
  auto lvalue = emitSource(
      "func main() -> i32 { new zero as i32 = 0; new alias as i32;\n"
      " zero, alias = 2, zero\n return 8 / alias\n}", options);
  HS_EXPECT_TRUE(lvalue.ok());
  HS_EXPECT_TRUE(!llvm::verifyModule(*lvalue.module));
  auto rvalue = emitSource(
      "func main() -> i32 { new value as i32 = 2; new old as i32;\n"
      " value, old = 0, value + 0\n return 8 / old\n}", options);
  HS_EXPECT_TRUE(rvalue.ok());
  HS_EXPECT_TRUE(!llvm::verifyModule(*rvalue.module));
}

HS_TEST(LLVMCodegen_MultipleAssignmentKeepsCapturedAllocationFacts) {
  const auto options = optionsFor(hitsimple::codegen::SafetyMode::StaticChecked);
  for (const auto &[statements, diagnostic] :
       std::vector<std::pair<std::string, std::string>>{
           {"new p as addr; p, _ = alloc(4), 0; free(p); free(p);", "double free"},
           {"new p as addr = alloc(4); new out as i32; "
            "out, _ = [4]*p, (p = realloc(p, 0));", "use after free"},
           {"new p as addr = alloc(4); new out as i32; "
            "out, _ = [4]*p, realloc(p, 0);", "use after free"},
           {"new p as addr; p, _ = calloc(2, 2), 0; free(p); return [1]*p;", "use after free"},
           {"new p as addr; p, _ = alloc(4), 0; new value[8]; value = [8]*p;", "out of bounds"},
           {"new p as addr; new q as addr; p = alloc(4); q, _ = p? + 1, 0; free(q);",
            "dynamic-object base address"}}) {
    auto result = emitSource("func main() -> i32 { " + statements +
                                 " return 0; }", options);
    HS_EXPECT_TRUE(!result.ok());
    HS_EXPECT_TRUE(!result.diagnostics.empty());
    HS_EXPECT_TRUE(result.diagnostics[0].message.find(diagnostic) != std::string::npos);
  }
  auto valid = emitSource(
      "func main() -> i32 { new p as addr; p, _ = alloc(4), 0;\n"
      " p, _ = realloc(p, 8), 1\n free(p)\n return 0\n}", options);
  HS_EXPECT_TRUE(valid.ok());
  HS_EXPECT_TRUE(!llvm::verifyModule(*valid.module));
}

HS_TEST(LLVMCodegen_MultipleAssignmentKeepsContextualFloatingTrees) {
  for (const auto *assignment : {
           "a, b = a %f+ 2.5, 1",
           "a, b = 1.5 %f+ 2.5, 1",
           "(a %f=), b = a %f+ a %f* 2.0, 1"}) {
    auto result = emitSource(
        "func main() -> i32 { new a as f32 = 1.0; new b as i32; " +
        std::string(assignment) + "; return 0; }");
    HS_EXPECT_TRUE(result.ok());
    HS_EXPECT_TRUE(!llvm::verifyModule(*result.module));
    const auto ir = llvmIr(result);
    if (std::string(assignment).find("1.5 %f+") == std::string::npos) {
      HS_EXPECT_TRUE(ir.find("fadd float") != std::string::npos);
    }
    HS_EXPECT_TRUE(ir.find("store float") != std::string::npos);
    HS_EXPECT_TRUE(ir.find("fadd double") == std::string::npos);
  }
}

HS_TEST(LLVMCodegen_SeparatesNameBindingFromDefinitionStorage) {
  auto result = emitSource(
      "func main() { new x as i32 = 1; new y as i32 = 9; "
      "new saved as addr = &x; x &= &y; x = 17; return x; }");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto ir = llvmIr(result);
  HS_EXPECT_TRUE(ir.find("x.binding = alloca ptr") != std::string::npos);
  HS_EXPECT_TRUE(ir.find("store ptr %x, ptr %x.binding") != std::string::npos);
  HS_EXPECT_TRUE(ir.find("store ptr %x.rebound, ptr %x.binding") != std::string::npos);
  HS_EXPECT_TRUE(ir.find("load ptr, ptr %x.binding") != std::string::npos);
  HS_EXPECT_TRUE(ir.find("store i32 17, ptr %x.addr.binding") != std::string::npos);
}

HS_TEST(LLVMCodegen_RebindingAdjustsSignedAndUnsignedWidths) {
  auto result = emitSource(
      "func use(s as i8, u as u8, wide[16]) -> addr { new x[3]; "
      "x &= s; x &= u; x &= wide; return &x; } "
      "func main() { return 0; }");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto ir = llvmIr(result);
  HS_EXPECT_TRUE(ir.find("sext i8") != std::string::npos);
  HS_EXPECT_TRUE(ir.find("zext i8") != std::string::npos);
  HS_EXPECT_TRUE(ir.find("trunc i128") != std::string::npos);
}

HS_TEST(LLVMCodegen_RebindingGlobalsIsTranslationUnitWide) {
  auto result = emitSource(
      "new value as i32 = 1; new target as i32 = 2; "
      "func read() -> i32 { return value; } "
      "func change() { value &= &target; } "
      "func main() { change(); return read(); }");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto ir = llvmIr(result);
  HS_EXPECT_TRUE(ir.find("@value.binding = internal global ptr @value") != std::string::npos);
  HS_EXPECT_TRUE(ir.find("load ptr, ptr @value.binding") != std::string::npos);
  HS_EXPECT_TRUE(ir.find("store ptr @target, ptr @value.binding") != std::string::npos);
}

HS_TEST(LLVMCodegen_RebindingOpaqueHandleRetainsTemplate) {
  auto result = emitSource(
      "func choose(first as handle, second as handle) -> handle { "
      "first &= &second; return first; } func main() { return 0; }");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(llvmIr(result).find("first.binding = alloca ptr") != std::string::npos);
}

HS_TEST(LLVMCodegen_RebindingStrchrReadsOnlyLowByte) {
  auto result = emitSource(
      "func search(ch[16] as bytes) -> addr { return strchr(\"abc\", ch); } "
      "func main() { return 0; }");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto ir = llvmIr(result);
  HS_EXPECT_TRUE(ir.find("load i8") != std::string::npos);
  HS_EXPECT_TRUE(ir.find("load i128") == std::string::npos);
  HS_EXPECT_TRUE(ir.find("call ptr @strchr") != std::string::npos);
}

HS_TEST(LLVMCodegen_RebindingPreservesVisibleMemoryAndStringExtents) {
  auto result = emitSource(
      "func amount() -> u64 { return 2; } "
      "func main() { new small[2] as cstr; new large[8] as cstr = \"abc\"; "
      "small &= &large; memset(small, 0, amount()); return to_i32(strlen(small)); }",
      optionsFor(hitsimple::codegen::SafetyMode::Checked));
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto ir = llvmIr(result);
  HS_EXPECT_TRUE(ir.find("@hs_check_view_capacity(i64") != std::string::npos);
  HS_EXPECT_TRUE(ir.find("@hs_check_cstr_view(ptr") != std::string::npos);
}

HS_TEST(LLVMCodegen_RebindingIndexChecksBeforePointerWidthTruncation) {
  auto result = emitSource(
      "func locate(index[16]) -> addr { new small[1] as bytes; new large[8] as bytes; "
      "small &= &large; return &small[index]; } func main() { return 0; }",
      optionsFor(hitsimple::codegen::SafetyMode::Checked));
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto ir = llvmIr(result);
  HS_EXPECT_TRUE(ir.find("icmp sge i128") != std::string::npos);
  HS_EXPECT_TRUE(ir.find("icmp ule i128") != std::string::npos);
  HS_EXPECT_TRUE(ir.find("@hs_check_view_offset") != std::string::npos);
  HS_EXPECT_TRUE(ir.find("trunc i128") != std::string::npos);
}

HS_TEST(LLVMCodegen_RebindingTernaryRetainsSourceWidth) {
  for (const auto *type : {"i8", "u8", "none"}) {
    const auto length = std::string(type) == "none" ? "[16]" : "";
    auto result = emitSource(
        "func choose(flag as bool, first" + std::string(length) + " as " + type +
        ", second" + length + " as " + type + ") -> addr { "
        "new target as i32; target &= flag ? first : second; return &target; } "
        "func main() { return 0; }");
    HS_EXPECT_TRUE(result.diagnostics.empty());
    HS_EXPECT_TRUE(!llvm::verifyModule(*result.module));
    const auto ir = llvmIr(result);
    HS_EXPECT_TRUE(ir.find(std::string("phi i") + (std::string(type) == "none" ? "128" : "8")) != std::string::npos);
  }
}

HS_TEST(LLVMCodegen_BorrowedRebindingTargetsAvoidInboundsBeforeChecks) {
  auto result = emitSource(
      "template Pair { first[4] as i32; last[4] as i32; }\n"
      "impl Pair { op = (dst as Pair, src as Pair) -> Pair { "
      "dst.last = src.last; return dst; } }\n"
      "func main() { new dst as Pair; new src as Pair; dst = src; return 0; }",
      optionsFor(hitsimple::codegen::SafetyMode::Checked));
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(!llvm::verifyModule(*result.module));
  const auto *operation = result.module->getFunction("__hitsimple.implop.Pair.0");
  HS_EXPECT_TRUE(operation != nullptr);
  bool foundBorrowedMember = false;
  for (const auto &block : *operation) {
    for (const auto &instruction : block) {
      const auto *gep = llvm::dyn_cast<llvm::GetElementPtrInst>(&instruction);
      if (gep && gep->getPointerOperand() == operation->getArg(1)) {
        foundBorrowedMember = true;
        HS_EXPECT_TRUE(!gep->isInBounds());
      }
    }
  }
  HS_EXPECT_TRUE(foundBorrowedMember);
  HS_EXPECT_TRUE(llvmIr(result).find("call void @hs_check_store") != std::string::npos);
}

HS_TEST(LLVMCodegen_RebindingFileBuffersUseAddressViewBytes) {
  auto result = emitSource(
      "func transfer(file as handle) { new backing[16] as bytes; "
      "new pointer as addr = &backing; new read_count = fread(pointer, 1, 8, file); "
      "new write_count = fwrite(pointer, 1, 8, file); } func main() { return 0; }",
      optionsFor(hitsimple::codegen::SafetyMode::Checked));
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(!llvm::verifyModule(*result.module));
  const auto *function = result.module->getFunction("transfer");
  HS_EXPECT_TRUE(function != nullptr);
  unsigned checkedBuffers = 0;
  for (const auto &block : *function) {
    for (const auto &instruction : block) {
      const auto *call = llvm::dyn_cast<llvm::CallInst>(&instruction);
      if (call && (call->getName() == "fread.ret" || call->getName() == "fwrite.ret")) {
        const auto *storage = llvm::dyn_cast<llvm::AllocaInst>(call->getArgOperand(0));
        HS_EXPECT_TRUE(storage != nullptr);
        HS_EXPECT_EQ(storage->getName().str(), "pointer");
        ++checkedBuffers;
      }
    }
  }
  HS_EXPECT_EQ(checkedBuffers, 2U);
}

HS_TEST(LLVMCodegen_LiteralTernariesCopyCompleteSelectedViews) {
  for (const auto mode : {hitsimple::codegen::SafetyMode::Unchecked,
                          hitsimple::codegen::SafetyMode::StaticChecked,
                          hitsimple::codegen::SafetyMode::Checked}) {
    for (const auto &[left, right, width] :
         std::vector<std::tuple<std::string, std::string, unsigned>>{
             {"\"a\\0b\"", "\"z\\0w\"", 4},
             {"'a\\0b'", "'z\\0w'", 3},
             {"\"ab\"", "\"xy\"", 3},
             {"\"\"", "\"\"", 1},
             {"\"abcdefghijklmnopq\"", "\"qrstuvwxyzabcdefg\"", 18}}) {
      auto result = emitSource(
          "func selected(choose as bool) -> [" + std::to_string(width) +
          "] as bytes { return (choose ? " + left + " : " + right + ") as bytes; }\n"
          "func main() -> i32 { return 0; }\n", optionsFor(mode));
      HS_EXPECT_TRUE(result.diagnostics.empty());
      HS_EXPECT_TRUE(result.module != nullptr);
      HS_EXPECT_TRUE(!llvm::verifyModule(*result.module, &llvm::errs()));
      const auto *function = result.module->getFunction("selected");
      HS_EXPECT_TRUE(function != nullptr);
      unsigned copies = 0;
      for (const auto &block : *function) {
        for (const auto &instruction : block) {
          if (const auto *copy = llvm::dyn_cast<llvm::MemCpyInst>(&instruction)) {
            const auto *length = llvm::dyn_cast<llvm::ConstantInt>(copy->getLength());
            HS_EXPECT_TRUE(length != nullptr);
            HS_EXPECT_EQ(length->getZExtValue(), width);
            HS_EXPECT_TRUE(block.getName().str().rfind("view.ternary.", 0) == 0);
            ++copies;
          }
          // Only the later ABI return may load an integer representation;
          // branch selection itself copies bytes without numeric reads.
          if (block.getName().str() == "view.ternary.then" ||
              block.getName().str() == "view.ternary.else") {
            HS_EXPECT_TRUE(!llvm::isa<llvm::LoadInst>(instruction));
            HS_EXPECT_TRUE(!llvm::isa<llvm::CastInst>(instruction));
          }
        }
      }
      HS_EXPECT_EQ(copies, 2U);
      if (width == 4U) {
        const auto ir = llvmIr(result);
        HS_EXPECT_TRUE(ir.find("c\"a\\00b\\00\"") != std::string::npos);
        HS_EXPECT_TRUE(ir.find("c\"z\\00w\\00\"") != std::string::npos);
      }
    }
  }
}

HS_TEST(LLVMCodegen_LiteralTernariesPreserveBranchLocalEffectsAndConsumers) {
  for (const auto mode : {hitsimple::codegen::SafetyMode::Unchecked,
                          hitsimple::codegen::SafetyMode::StaticChecked,
                          hitsimple::codegen::SafetyMode::Checked}) {
    auto result = emitSource(
        "func predicate() -> bool { return true; }\n"
        "func left() -> [4] as bytes { return \"a\\0b\" as bytes; }\n"
        "func right() -> [4] as bytes { return \"z\\0w\" as bytes; }\n"
        "func identity(value[4] as cstr) -> [4] as bytes { return value as bytes; }\n"
        "func probe() -> [4] as bytes { return predicate() ? left() : right(); }\n"
        "func direct() -> [4] as bytes { return identity(predicate() ? \"a\\0b\" : \"z\\0w\"); }\n"
        "func cstring() -> [4] as cstr { return predicate() ? \"a\\0b\" : \"z\\0w\"; }\n"
        "func size() -> u64 { return length(predicate() ? \"a\\0b\" : \"z\\0w\"); }\n"
        "func main() -> i32 { return 0; }\n", optionsFor(mode));
    HS_EXPECT_TRUE(result.diagnostics.empty());
    HS_EXPECT_TRUE(result.module != nullptr);
    HS_EXPECT_TRUE(!llvm::verifyModule(*result.module, &llvm::errs()));
    for (const auto name : {"probe", "direct", "cstring", "size"}) {
      const auto *function = result.module->getFunction(name);
      HS_EXPECT_TRUE(function != nullptr);
      unsigned predicates = 0;
      unsigned branchCalls = 0;
      for (const auto &block : *function) {
        for (const auto &instruction : block) {
          const auto *call = llvm::dyn_cast<llvm::CallInst>(&instruction);
          if (call == nullptr || call->getCalledFunction() == nullptr) { continue; }
          const auto called = call->getCalledFunction()->getName();
          if (called == "predicate") {
            HS_EXPECT_TRUE(block.getName().str().rfind("view.ternary.", 0) != 0);
            ++predicates;
          } else if (called == "left" || called == "right") {
            HS_EXPECT_EQ(block.getName().str(), called == "left"
                ? std::string("view.ternary.then") : std::string("view.ternary.else"));
            ++branchCalls;
          }
        }
      }
      HS_EXPECT_EQ(predicates, 1U);
      HS_EXPECT_EQ(branchCalls, std::string_view(name) == "probe" ? 2U : 0U);
    }
  }
}
