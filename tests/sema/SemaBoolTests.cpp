#include "SemaTestSupport.h"

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
    HS_EXPECT_TRUE(result.diagnostics.front().message.find(
                       "bool only supports == and != ordinary operations") !=
                   std::string::npos);
  }
}

HS_TEST(Sema_RejectsBoolUnaryMinusAndBitwiseNot) {
  for (const std::string operation : {"-", "~"}) {
    const auto result = analyzeSource(
        "func main() -> i32 {\n"
        "    new flag as bool = true\n"
        "    new value = " +
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
