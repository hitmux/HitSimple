#include "SemaTestSupport.h"

#include "hitsimple/hir/HIR.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using hitsimple::testing::sema::analyzeSource;

HS_TEST(Sema_InfersDeclarationsFromValidatedViewLengths) {
  auto result = analyzeSource(
      "func selected(choose as bool, left as f64, right as f64) -> u64 {\n"
      "    new flag = true\n"
      "    new predicate = left == right\n"
      "    new logic = flag && predicate\n"
      "    new bits = (choose ? left : right) as u64\n"
      "    new nested = ((choose ? left : right) as bytes) as u64\n"
      "    new indexed = bits[0] as u8\n"
      "    new indirect = [8]*&bits\n"
      "    new slice = bits[0:3]\n"
      "    new buffer as bytes = resize_bytes(bits, 3)\n"
      "    new wide as bytes = resize_bytes(bits, 16)\n"
      "    new folded as bytes = resize_bytes(bits, 1 + 2)\n"
      "    new swapped as bytes = byte_swap(buffer)\n"
      "    new known as bytes = resize_bytes(bits, length(buffer))\n"
      "    static once = true\n"
      "    return bits\n}\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(hitsimple::hir::verifyHIR(*result.unit).empty());
  const auto dump = hitsimple::hir::dumpToString(*result.unit);
  for (const auto &[name, width] :
       std::vector<std::pair<std::string, std::size_t>>{
           {"flag", 1}, {"predicate", 1}, {"logic", 1}, {"bits", 8},
           {"nested", 8}, {"indexed", 1}, {"indirect", 8}, {"slice", 3},
           {"buffer", 3}, {"wide", 16}, {"folded", 3}, {"swapped", 3},
           {"known", 3}, {"once", 1}}) {
    HS_EXPECT_TRUE(dump.find("LocalMemory name=" + name + " binding=" + name +
                             " bytes=" + std::to_string(width)) !=
                   std::string::npos);
  }
}

HS_TEST(Sema_InfersGlobalLengthsAcrossForwardReferences) {
  const auto result = analyzeSource(
      "new flag = true\n"
      "new measured = length(later)\n"
      "new called = make()\n"
      "new external = foreign()\n"
      "new derived = following\n"
      "new later as bytes = 'abc'\n"
      "new following = 128\n"
      "extern foreign() -> u16\n"
      "func make() -> u64 { return 42; }\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_EQ(result.unit->globals.size(), 7U);
  const std::vector<std::size_t> widths{1, 8, 8, 2, 2, 3, 2};
  const std::vector<std::string> names{
      "flag", "measured", "called", "external", "derived", "later", "following"};
  for (std::size_t index = 0; index < widths.size(); ++index) {
    HS_EXPECT_EQ(result.unit->globals[index].name, names[index]);
    HS_EXPECT_EQ(result.unit->globals[index].byteLength, widths[index]);
  }
}

HS_TEST(Sema_LengthRetainsValidatedStaticAndDynamicOperands) {
  using namespace hitsimple::hir;
  for (const auto &expression : {
           "true", "value", "value[0]", "value[1:3]", "file",
           "(true ? value : value) as u64", "make()",
           "resize_bytes(value, count)",
           "byte_swap(resize_bytes(value, count))",
           "resize_bytes(value, count) as bytes",
           "resize_bytes(value, count) as u64"}) {
    const auto result = analyzeSource(
        "func make() -> u64 { return 42; }\n"
        "func probe(value as u64, count as u64, file as handle) -> u64 { return length(" +
        std::string(expression) + "); }\n"
        "func main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.unit != nullptr);
    HS_EXPECT_TRUE(result.diagnostics.empty());
    const auto *ret = dynamic_cast<const Return *>(
        result.unit->functions[1]->body->statements.front().get());
    HS_EXPECT_TRUE(ret != nullptr);
    const auto *call = dynamic_cast<const CallExpr *>(ret->values.front().get());
    HS_EXPECT_TRUE(call != nullptr);
    HS_EXPECT_EQ(call->builtin, hitsimple::stdlib::BuiltinId::Length);
    HS_EXPECT_EQ(call->result.templateName, std::string("u64"));
    HS_EXPECT_EQ(call->result.staticByteLength, 8U);
    HS_EXPECT_EQ(call->arguments.size(), 1U);
    HS_EXPECT_EQ(call->argumentPlans.front().kind, ConversionKind::Identity);
    HS_EXPECT_EQ(call->argumentPlans.front().source.lengthKind,
                 call->arguments.front()->result.lengthKind);
    HS_EXPECT_TRUE(verifyHIR(*result.unit).empty());
  }
}

HS_TEST(Sema_LengthRejectsInvalidOperandsAndUnknownExtents) {
  const auto prefix =
      "func identity(value as f64) -> f64 { return value; }\n"
      "func empty() -> () {}\n"
      "func probe(left as f64, right as u64, count as u64) -> u64 { return ";
  for (const auto &[expression, diagnostic] :
       std::vector<std::pair<std::string, std::string>>{
           {"length(1 as NotATemplate)", "unknown template 'NotATemplate'"},
           {"length(1 as u64)", "does not match template 'u64'"},
           {"length(missing)", "undeclared variable"},
           {"length(identity())", "argument count"},
           {"length(identity(right))", "float operand"},
           {"length(empty())", "requires a known return signature"},
           {"length(left + right)", "requires the same template"},
           {"length(true ? left : right)", "ternary branches"},
           {"length(true ? right : missing)", "undeclared variable"},
           {"length()", "length expects 1 argument"},
           {"length(left, right)", "length expects 1 argument"}}) {
    const auto result = analyzeSource(std::string(prefix) + expression +
                                     "; }\nfunc main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.unit == nullptr);
    HS_EXPECT_TRUE(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                              [&](const auto &item) {
      return item.message.find(diagnostic) != std::string::npos;
    }));
    HS_EXPECT_TRUE(std::none_of(result.diagnostics.begin(), result.diagnostics.end(),
                               [](const auto &item) {
      return item.message.find("internal error") != std::string::npos;
    }));
  }
  for (const auto &body : {
           "new dynamic as bytes = resize_bytes(value, count)",
           "new dynamic as bytes = byte_swap(resize_bytes(value, count))",
           "new dynamic as bytes = resize_bytes(value, count) as bytes"}) {
    const auto result = analyzeSource(
        "func probe(value as u64, count as u64) -> () { " + std::string(body) +
        "; }\nfunc main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.unit == nullptr);
    HS_EXPECT_TRUE(result.diagnostics.front().message.find("invalid byte length") !=
                   std::string::npos);
  }
  const auto cycle = analyzeSource(
      "new first = second\nnew second = first\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(cycle.unit == nullptr);
  HS_EXPECT_TRUE(cycle.diagnostics.front().message.find("cyclic global byte-length") !=
                 std::string::npos);
}

HS_TEST(Sema_ChainedAsPreservesOperandValueCategory) {
  using namespace hitsimple::hir;
  for (const auto &expression : {"((value as bytes) as u64)",
                                 "((make() as bytes) as u64)"}) {
    const auto result = analyzeSource(
        "func make() -> u64 { return 1; }\n"
        "func probe(value as u64) -> u64 { return " + std::string(expression) +
        "; }\nfunc main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.unit != nullptr);
    const auto *ret = dynamic_cast<const Return *>(
        result.unit->functions[1]->body->statements.front().get());
    HS_EXPECT_TRUE(ret != nullptr);
    const auto *outer = dynamic_cast<const TemplateViewExpr *>(ret->values.front().get());
    HS_EXPECT_TRUE(outer != nullptr);
    const bool lvalue = std::string(expression).find("value") != std::string::npos;
    HS_EXPECT_EQ(outer->result.isAddressable, lvalue);
    HS_EXPECT_EQ(outer->result.isMutableLValue, lvalue);
    HS_EXPECT_EQ(outer->operand->result.isAddressable, lvalue);
  }
}

HS_TEST(Sema_ConversionsUseValidatedOperandLengthsAndCategories) {
  const auto result = analyzeSource(
      "func probe(value as i32, number as f64, other as f64) -> () {\n"
      "    new integer = to_i64(value = 1)\n"
      "    new floating as f64 = to_f64(value = 2)\n"
      "    new converted = to_i32(number = other)\n"
      "    new wider as f64 = to_f64(number = other)\n"
      "    new selected = to_i32(true ? number : other)\n"
      "    new indexed = to_u64(value[0] as u8)\n"
      "}\nfunc main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("ToIntExpr floatBytes=8") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=integer binding=integer bytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=converted binding=converted bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(hitsimple::hir::verifyHIR(*result.unit).empty());
}

HS_TEST(Sema_InferredDeclarationsReserveTheirOwnBindings) {
  const auto result = analyzeSource(
      "new first = length(second)\n"
      "new second = length(first)\n"
      "func probe() -> u64 {\n"
      "    new count = length(count)\n"
      "    new value[1] = 0\n"
      "    if (true) { new value = length(value); return value; }\n"
      "    return count\n}\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("VariableRef name=value binding=value.1 bytes=8") !=
                 std::string::npos);
  HS_EXPECT_EQ(result.unit->globals[0].byteLength, 8U);
  HS_EXPECT_EQ(result.unit->globals[1].byteLength, 8U);
  const auto underdetermined = analyzeSource(
      "func main() -> i32 {\n"
      "    new value[1] = 0\n"
      "    if (true) { new value = value; }\n"
      "    return 0\n}\n");
  HS_EXPECT_TRUE(underdetermined.unit == nullptr);
  HS_EXPECT_TRUE(underdetermined.diagnostics.front().message.find("own initializer") !=
                 std::string::npos);
}

HS_TEST(Sema_InfersResolvedOperationAndMethodResultLengths) {
  const auto result = analyzeSource(
      "template Record { value[8] as u64; }\n"
      "impl Record {\n"
      "    op +(left as Record, right as Record) -> u8 { return 1; }\n"
      "    func measure(self as Record) -> u16 { return 2; }\n"
      "}\n"
      "func main() -> i32 {\n"
      "    new record as Record\n"
      "    new operation = record + record\n"
      "    new method = record.measure()\n"
      "    new comparison = 1 == 2\n"
      "    return 0\n}\n");
  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=operation binding=operation bytes=1") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=method binding=method bytes=2") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=comparison binding=comparison bytes=1") !=
                 std::string::npos);
}

HS_TEST(Sema_LengthRejectsKnownEmptyInterpretedViews) {
  for (const auto &type : {"bytes", "cstr", "u64"}) {
    for (const auto &empty : {"resize_bytes(value, 0)",
                              "byte_swap(resize_bytes(value, 0))",
                              "(resize_bytes(value, 0) as none)"}) {
      const auto result = analyzeSource(
          "func probe(value as u64) -> u64 { return length(" + std::string(empty) + " as " +
          std::string(type) + "); }\nfunc main() -> i32 { return 0; }\n");
      HS_EXPECT_TRUE(result.unit == nullptr);
      HS_EXPECT_TRUE(result.diagnostics.front().message.find("positive byte length") !=
                     std::string::npos);
    }
  }
}

HS_TEST(Sema_HIRRejectsInconsistentLengthAndInterpretationMetadata) {
  using namespace hitsimple::hir;
  auto result = analyzeSource(
      "func probe() -> u64 { return length((1 as u8) as bytes); }\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(result.unit != nullptr);
  auto *ret = dynamic_cast<Return *>(result.unit->functions[0]->body->statements[0].get());
  HS_EXPECT_TRUE(ret != nullptr);
  auto *length = dynamic_cast<CallExpr *>(ret->values[0].get());
  HS_EXPECT_TRUE(length != nullptr);
  length->result = viewSemanticsForTemplate("u32", 4);
  length->byteLength = 4;
  length->templateName = "u32";
  auto diagnostics = verifyHIR(*result.unit);
  HS_EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto &d) {
    return d.message.find("eight-byte u64 result") != std::string::npos;
  }));
  length->result = viewSemanticsForTemplate("u64", 8);
  length->byteLength = 8;
  length->templateName = "u64";
  auto *view = dynamic_cast<TemplateViewExpr *>(length->arguments[0].get());
  HS_EXPECT_TRUE(view != nullptr);
  view->isAddressable = true;
  view->result.isAddressable = true;
  view->result.isMutableLValue = true;
  length->argumentPlans[0].source = view->result;
  length->argumentPlans[0].destination = view->result;
  diagnostics = verifyHIR(*result.unit);
  HS_EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto &d) {
    return d.message.find("preserve operand value category") != std::string::npos;
  }));
}

HS_TEST(Sema_OwnBindingPrecedesAddressAndHandleTemplateInference) {
  for (const auto &source : {
      "func probe() -> value as addr { if (true) { new value = value; }; return; }\n",
      "func probe(value as handle) -> () { if (true) { new value = value; }; }\n",
      "func probe() -> () { new value as addr = 0; if (true) { new value = value; }; }\n",
      "func probe(value as handle) -> () { if (true) { static value = value; }; }\n"}) {
    const auto result = analyzeSource(std::string(source) +
                                      "func main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.unit == nullptr);
    HS_EXPECT_TRUE(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [](const auto &diagnostic) {
          return diagnostic.message.find("own initializer") != std::string::npos;
        }));
  }
  const auto positive = analyzeSource(
      "func probe(source as addr, file as handle) -> () {\n"
      "    new address = source\n"
      "    new opaque = file\n"
      "    new sized[P] = source\n"
      "    new self_address = &self_address\n"
      "    if (true) { new address = &address; }\n"
      "}\nfunc main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(positive.unit != nullptr);
  HS_EXPECT_TRUE(positive.diagnostics.empty());
  HS_EXPECT_TRUE(hitsimple::hir::verifyHIR(*positive.unit).empty());
  const auto dump = hitsimple::hir::dumpToString(*positive.unit);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=address binding=address bytes=8 storage=local template=addr") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=opaque binding=opaque bytes=8 storage=local template=handle") !=
                 std::string::npos);
}

HS_TEST(Sema_ResizeBytesProducesRawRvalueViewsAtEveryLength) {
  using namespace hitsimple::hir;
  for (const auto &[spelling, width] :
       std::vector<std::pair<std::string, std::size_t>>{
           {"1", 1}, {"2", 2}, {"4", 4}, {"8", 8}, {"3", 3},
           {"16", 16}, {"1 + 3", 4}, {"length(value)", 8},
           {"count", 0}, {"0", 0}}) {
    const auto result = analyzeSource(
        "func probe(value as f64, count as u64) -> u64 {\n"
        "    return length(resize_bytes(value, " + spelling + "));\n}\n"
        "func main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.unit != nullptr);
    HS_EXPECT_TRUE(result.diagnostics.empty());
    HS_EXPECT_TRUE(verifyHIR(*result.unit).empty());
    const auto *ret = dynamic_cast<const Return *>(
        result.unit->functions.front()->body->statements.front().get());
    HS_EXPECT_TRUE(ret != nullptr);
    const auto *length = dynamic_cast<const CallExpr *>(ret->values.front().get());
    HS_EXPECT_TRUE(length != nullptr);
    const auto *resize = dynamic_cast<const DynamicByteViewExpr *>(
        length->arguments.front().get());
    HS_EXPECT_TRUE(resize != nullptr);
    HS_EXPECT_EQ(resize->operation, DynamicByteViewOperation::ResizeBytes);
    HS_EXPECT_EQ(resize->result.category, ViewCategory::RawBytes);
    HS_EXPECT_EQ(resize->result.integerInterpretation, IntegerInterpretation::RawOnly);
    HS_EXPECT_TRUE(resize->result.templateName.empty());
    HS_EXPECT_TRUE(!resize->result.isAddressable);
    HS_EXPECT_TRUE(!resize->result.isMutableLValue);
    HS_EXPECT_EQ(resize->result.lengthKind,
                 width == 0 ? ViewLengthKind::Dynamic : ViewLengthKind::Static);
    HS_EXPECT_EQ(resize->result.staticByteLength, width);
    HS_EXPECT_EQ(resize->source->result.category, ViewCategory::Floating);
    HS_EXPECT_TRUE(resize->runtimeLength != nullptr);
  }
}

HS_TEST(Sema_ResizeBytesRequiresExplicitNumericInterpretation) {
  for (const auto width : {1U, 2U, 4U, 8U}) {
    const auto rejected = analyzeSource(
        "func probe(value as f64) -> u8 { return to_u8(resize_bytes(value, " +
        std::to_string(width) + ")); }\n"
        "func main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(!rejected.diagnostics.empty());
    HS_EXPECT_TRUE(rejected.diagnostics.front().message.find(
                       "requires an integer, floating, or address input") !=
                   std::string::npos);
    const auto accepted = analyzeSource(
        "func probe(value as f64) -> u8 { return to_u8(resize_bytes(value, " +
        std::to_string(width) + ") as u" + std::to_string(width * 8) + "); }\n"
        "func main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(accepted.unit != nullptr);
    HS_EXPECT_TRUE(accepted.diagnostics.empty());
    HS_EXPECT_TRUE(hitsimple::hir::verifyHIR(*accepted.unit).empty());
  }
}

HS_TEST(Sema_ByteSwapProducesUntemplatedRawRvalues) {
  using namespace hitsimple::hir;
  for (const auto &[source, width] :
       std::vector<std::pair<std::string, std::size_t>>{
           {"flag", 1}, {"integer", 2}, {"half", 2}, {"single", 4},
           {"real", 8}, {"wide", 16}, {"raw", 3}, {"text", 5},
           {"record", 3}, {"file", 8}, {"address", 8},
           {"\"A\\0Z\"", 4}, {"'é中'", 5}, {"0x1234", 2},
           {"raw[0:2]", 2}, {"[3]*address", 3}, {"byte_swap(raw)", 3},
           {"resize_bytes(raw, count)", 0}, {"resize_bytes(raw, 0)", 0}}) {
    const auto result = analyzeSource(
        "template Record { data[3] as bytes; }\n"
        "func probe(flag as bool, integer as u16, half as f16, single as f32, "
        "real as f64, wide as f128, raw[3] as bytes, text[5] as cstr, "
        "record as Record, file as handle, address as addr, count as u64) -> u64 {\n"
        "    return length(byte_swap(" + source + "));\n}\n"
        "func main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.unit != nullptr);
    HS_EXPECT_TRUE(result.diagnostics.empty());
    HS_EXPECT_TRUE(verifyHIR(*result.unit).empty());
    const auto *ret = dynamic_cast<const Return *>(
        result.unit->functions.front()->body->statements.front().get());
    HS_EXPECT_TRUE(ret != nullptr);
    const auto *length = dynamic_cast<const CallExpr *>(ret->values.front().get());
    HS_EXPECT_TRUE(length != nullptr);
    const auto &swap = *length->arguments.front();
    HS_EXPECT_EQ(swap.result.category, ViewCategory::RawBytes);
    HS_EXPECT_EQ(swap.result.integerInterpretation, IntegerInterpretation::RawOnly);
    HS_EXPECT_TRUE(swap.result.templateName.empty());
    HS_EXPECT_TRUE(!swap.result.isAddressable);
    HS_EXPECT_TRUE(!swap.result.isMutableLValue);
    HS_EXPECT_EQ(swap.result.lengthKind,
                 width == 0 ? ViewLengthKind::Dynamic : ViewLengthKind::Static);
    HS_EXPECT_EQ(swap.result.staticByteLength, width);
    if (const auto *call = dynamic_cast<const CallExpr *>(&swap)) {
      HS_EXPECT_EQ(call->builtin, hitsimple::stdlib::BuiltinId::ByteSwap);
      HS_EXPECT_TRUE(call->templateName.empty());
    } else if (const auto *dynamic = dynamic_cast<const DynamicByteViewExpr *>(&swap)) {
      HS_EXPECT_EQ(dynamic->operation, DynamicByteViewOperation::ByteSwap);
      HS_EXPECT_TRUE(dynamic->runtimeLength == nullptr);
    } else {
      HS_EXPECT_TRUE(dynamic_cast<const ByteSwapExpr *>(&swap) != nullptr);
    }
  }
}

HS_TEST(Sema_ByteSwapRequiresExplicitNumericInterpretation) {
  const auto rejected = analyzeSource(
      "func probe(value as u16) -> u64 { return to_u64(byte_swap(value)); }\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(rejected.unit == nullptr);
  HS_EXPECT_TRUE(std::any_of(rejected.diagnostics.begin(), rejected.diagnostics.end(),
      [](const auto &diagnostic) {
        return diagnostic.message.find("requires an integer, floating, or address input") !=
               std::string::npos;
      }));
  const auto accepted = analyzeSource(
      "func probe(value as u16) -> u64 { return to_u64(byte_swap(value) as u16); }\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(accepted.unit != nullptr);
  HS_EXPECT_TRUE(accepted.diagnostics.empty());
  HS_EXPECT_TRUE(hitsimple::hir::verifyHIR(*accepted.unit).empty());
}

HS_TEST(Sema_ByteSwapHandleAcceptancePreservesOtherHandleRestrictions) {
  for (const auto &statement : {"_ = to_u64(file);", "_ = resize_bytes(file, 8);",
                                "free(file);", "_ = file + file;"}) {
    const auto result = analyzeSource(
        "func probe(file as handle) -> i32 { " + std::string(statement) +
        " return 0; }\nfunc main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.unit == nullptr);
    // A standalone free statement prevents a void-return mismatch from being
    // mistaken for the intended handle restriction.
    HS_EXPECT_TRUE(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [](const auto &diagnostic) {
          return diagnostic.message.find("handle") != std::string::npos;
        }));
  }
}

HS_TEST(Sema_ByteSwapDoesNotIntroduceBytesComparisonCandidates) {
  const auto rejected = analyzeSource(
      "func probe(raw[3] as bytes) -> bool { return byte_swap(raw) == byte_swap(raw); }\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(rejected.unit == nullptr);
  HS_EXPECT_TRUE(std::any_of(rejected.diagnostics.begin(), rejected.diagnostics.end(),
      [](const auto &diagnostic) {
        return diagnostic.message.find("ordinary operation") != std::string::npos;
      }));
  const auto accepted = analyzeSource(
      "func probe(raw[3] as bytes) -> bool {\n"
      "    return (byte_swap(raw) as bytes) == (byte_swap(raw) as bytes);\n}\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(accepted.unit != nullptr);
  HS_EXPECT_TRUE(accepted.diagnostics.empty());
  HS_EXPECT_TRUE(hitsimple::hir::verifyHIR(*accepted.unit).empty());
}
