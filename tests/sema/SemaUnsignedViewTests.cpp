#include "SemaTestSupport.h"

#include "hitsimple/hir/HIR.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using hitsimple::testing::sema::analyzeSource;

HS_TEST(Sema_UnsignedInterpretationAcceptsAllViewCategories) {
  using namespace hitsimple::hir;
  for (const auto &[operand, width] :
       std::vector<std::pair<std::string, std::size_t>>{
           {"half", 2}, {"single", 4}, {"real", 8}, {"quad", 16},
           {"text", 5}, {"raw", 3}, {"user", 3}, {"file", sizeof(void*)},
           {"pointer", sizeof(void*)}, {"integer", 2}, {"flag", 1},
           {"\"abc\"", 4}, {"'é中'", 5}, {"'A'", 1}, {"1.25", 8},
           {"65535", 4}, {"raw[0:2]", 2}, {"[3]*pointer", 3}}) {
    const auto result = analyzeSource(
        "template Blob { data[3] as bytes; }\n"
        "func probe(half as f16, single as f32, real as f64, quad as f128, "
        "text[5] as cstr, raw[3] as bytes, user as Blob, file as handle, "
        "pointer as addr, integer as i16, flag as bool) -> u64 { "
        "return length((" + operand + ")?); }\nfunc main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.unit != nullptr);
    HS_EXPECT_TRUE(result.diagnostics.empty());
    HS_EXPECT_TRUE(verifyHIR(*result.unit).empty());
    const auto *ret = dynamic_cast<const Return *>(
        result.unit->functions.front()->body->statements.front().get());
    HS_EXPECT_TRUE(ret != nullptr);
    const auto *length = dynamic_cast<const CallExpr *>(ret->values.front().get());
    HS_EXPECT_TRUE(length != nullptr);
    const auto *value = dynamic_cast<const UnsignedExpr *>(length->arguments.front().get());
    HS_EXPECT_TRUE(value != nullptr);
    HS_EXPECT_EQ(value->result.category, ViewCategory::UntemplatedInteger);
    HS_EXPECT_EQ(value->result.integerInterpretation, IntegerInterpretation::Unsigned);
    HS_EXPECT_EQ(value->result.staticByteLength, width);
    HS_EXPECT_EQ(value->result.templateName, std::string{});
    HS_EXPECT_TRUE(!value->result.isAddressable);
    HS_EXPECT_TRUE(!value->result.isMutableLValue);
  }
}

HS_TEST(Sema_UnsignedInterpretationRetainsDynamicAndEmptyViewExtents) {
  for (const auto *operand : {"resize_bytes(raw, count)",
                              "byte_swap(resize_bytes(raw, count))",
                              "resize_bytes(raw, 0)",
                              "(resize_bytes(raw, count) as none)"}) {
    const auto result = analyzeSource(
        "func probe(raw[3] as bytes, count as u64) -> u64 { return length((" +
        std::string(operand) + ")?); }\nfunc main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.unit != nullptr);
    HS_EXPECT_TRUE(result.diagnostics.empty());
    HS_EXPECT_TRUE(hitsimple::hir::verifyHIR(*result.unit).empty());
    const auto dump = hitsimple::hir::dumpToString(*result.unit);
    HS_EXPECT_TRUE(dump.find("UnsignedExpr bytes=0 category=untemplated-integer "
        "template=none length=dynamic integer_interpretation=unsigned "
        "lvalue=false addressable=false") != std::string::npos);
  }
}

HS_TEST(Sema_UnsignedDynamicArithmeticReportsUnsupportedWidth) {
  for (const auto *expression : {"resize_bytes(raw, count)? + 1",
                                 "resize_bytes(raw, count)? %8d+ 1",
                                 "~resize_bytes(raw, count)?",
                                 "-resize_bytes(raw, count)?"}) {
    const auto result = analyzeSource(
        "func probe(raw[3] as bytes, count as u64) -> u64 { return " +
        std::string(expression) + "; }\nfunc main() -> i32 { return 0; }\n");
    HS_EXPECT_TRUE(result.unit == nullptr);
    HS_EXPECT_TRUE(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [](const auto &diagnostic) {
          return diagnostic.message.find("runtime-sized integer") != std::string::npos;
        }));
  }
}
