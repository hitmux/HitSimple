#include "support/TestRunner.h"

#include "hitsimple/compat/CCompat.h"
#include "hitsimple/sema/Sema.h"

#include <cstddef>
#include <string>

namespace {

using hitsimple::compat::Linkage;

const hitsimple::compat::LinkageMetadata* findLinkage(
    const hitsimple::compat::LoweringResult& result, std::string_view name) {
  for (const auto& item : result.linkage) {
    if (item.sourceName == name) {
      return &item;
    }
  }
  return nullptr;
}

} // namespace

HS_TEST(CCompat_ParsesAndLowersTypedefStructPointersAndSubscripts) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "typedef unsigned int Count;\n"
      "struct Pair {\n"
      "  int left;\n"
      "  char tag;\n"
      "};\n"
      "extern int puts(char *text);\n"
      "static int helper(Count count) {\n"
      "  int values[4];\n"
      "  int *cursor;\n"
      "  cursor = values;\n"
      "  values[1] = count;\n"
      "  return cursor[1];\n"
      "}\n",
      "compat.c");

  HS_EXPECT_TRUE(parsed.ok());
  HS_EXPECT_EQ(parsed.unit->declarations.size(), 4U);

  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(lowered.ok());
  HS_EXPECT_EQ(lowered.unit->templates.size(), 1U);
  HS_EXPECT_EQ(lowered.unit->externFunctions.size(), 1U);
  HS_EXPECT_EQ(lowered.unit->functions.size(), 1U);

  const auto* helper = findLinkage(lowered, "helper");
  HS_EXPECT_TRUE(helper != nullptr);
  HS_EXPECT_TRUE(helper->isFunction);
  HS_EXPECT_TRUE(helper->isDefinition);
  HS_EXPECT_TRUE(helper->linkage == Linkage::Internal);

  const auto* puts = findLinkage(lowered, "puts");
  HS_EXPECT_TRUE(puts != nullptr);
  HS_EXPECT_TRUE(puts->isFunction);
  HS_EXPECT_TRUE(!puts->isDefinition);
  HS_EXPECT_TRUE(puts->linkage == Linkage::External);
  HS_EXPECT_EQ(puts->parameterTypes.size(), 1U);
}

HS_TEST(CCompat_PreservesFloatTemplatesAndCAbiTypes) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "double add(double lhs, double rhs) {\n"
      "  double value = lhs + rhs;\n"
      "  return value;\n"
      "}\n",
      "float.c");

  HS_EXPECT_TRUE(parsed.ok());
  auto options = hitsimple::compat::LoweringOptions{};
  options.allowHostFloatExternAbi = true;
  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit, options);

  HS_EXPECT_TRUE(lowered.ok());
  HS_EXPECT_EQ(lowered.unit->functions.size(), 1U);
  const auto& function = *lowered.unit->functions.front();
  HS_EXPECT_EQ(function.params.size(), 2U);
  HS_EXPECT_EQ(function.params[0].templateName, "f64");
  HS_EXPECT_EQ(function.params[1].templateName, "f64");
  HS_EXPECT_EQ(function.returns.size(), 1U);
  HS_EXPECT_EQ(function.returns[0].templateName, "f64");

  const auto* add = findLinkage(lowered, "add");
  HS_EXPECT_TRUE(add != nullptr);
  HS_EXPECT_EQ(add->parameterTypes.size(), 2U);
  HS_EXPECT_TRUE(add->parameterTypes[0].kind ==
                 hitsimple::compat::CAbiValueKind::Floating);
  HS_EXPECT_TRUE(add->returnType.has_value());
  HS_EXPECT_TRUE(add->returnType->kind ==
                 hitsimple::compat::CAbiValueKind::Floating);
}

HS_TEST(CCompat_LowersFloatComparisonsToTypedCoreOperators) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "int eq(double left, double right) { return left == right; }\n"
      "int ne(double left, double right) { return left != right; }\n"
      "int lt(double left, double right) { return left < right; }\n"
      "int le(double left, double right) { return left <= right; }\n"
      "int gt(double left, double right) { return left > right; }\n"
      "int ge(double left, double right) { return left >= right; }\n",
      "float-compare.c");

  HS_EXPECT_TRUE(parsed.ok());
  auto options = hitsimple::compat::LoweringOptions{};
  options.allowHostFloatExternAbi = true;
  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit, options);
  HS_EXPECT_TRUE(lowered.ok());

  const auto dump = hitsimple::ast::dumpToString(*lowered.unit);
  for (const std::string op : {"==", "!=", "<", "<=", ">", ">="}) {
    HS_EXPECT_TRUE(dump.find("BinaryExpr op=%8f" + op) != std::string::npos);
  }

  auto analyzeOptions = hitsimple::sema::AnalyzeOptions{};
  analyzeOptions.requireMain = false;
  analyzeOptions.cCompatibilityMode = true;
  const auto analyzed = hitsimple::sema::analyze(*lowered.unit, analyzeOptions);
  HS_EXPECT_TRUE(analyzed.unit != nullptr);
  HS_EXPECT_TRUE(analyzed.diagnostics.empty());

  const auto hirDump = hitsimple::hir::dumpToString(*analyzed.unit);
  for (const std::string op : {"==", "!=", "<", "<=", ">", ">="}) {
    HS_EXPECT_TRUE(hirDump.find("FloatCompareExpr op=%8f" + op) !=
                   std::string::npos);
  }
}

HS_TEST(CCompat_PreservesTopLevelInitializerForCoreSema) {
  const auto parsed = hitsimple::compat::parseCCompatSource("int value = 1;\n",
                                                             "global.c");

  HS_EXPECT_TRUE(parsed.ok());
  HS_EXPECT_EQ(parsed.unit->declarations.size(), 1U);

  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(lowered.ok());
  HS_EXPECT_EQ(lowered.unit->globalNews.size(), 1U);
  const auto& global = *lowered.unit->globalNews.front();
  HS_EXPECT_EQ(global.name, "value");
  HS_EXPECT_EQ(global.length, "4");
  HS_EXPECT_EQ(global.templateName, "i32");
  HS_EXPECT_EQ(global.assignmentOp, "=");
  HS_EXPECT_TRUE(dynamic_cast<const hitsimple::ast::IntegerLiteral*>(
                     global.initializer.get()) != nullptr);

  const auto* value = findLinkage(lowered, "value");
  HS_EXPECT_TRUE(value != nullptr);
  HS_EXPECT_TRUE(!value->isFunction);
  HS_EXPECT_TRUE(value->isDefinition);
  HS_EXPECT_TRUE(value->linkage == Linkage::External);
}

HS_TEST(CCompat_RejectsExternVariableInitializer) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "extern int errno = 1;\n", "extern-initializer.c");

  HS_EXPECT_TRUE(parsed.ok());
  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(!lowered.ok());
  HS_EXPECT_TRUE(!lowered.diagnostics.empty());
  HS_EXPECT_TRUE(lowered.diagnostics.front().find("extern variable") !=
                 std::string::npos);
}

HS_TEST(CCompat_RejectsStringInitializerForNonCharacterArray) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "int values[4] = \"x\";\n", "non-character-array.c");

  HS_EXPECT_TRUE(parsed.ok());
  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(!lowered.ok());
  HS_EXPECT_TRUE(!lowered.diagnostics.empty());
  HS_EXPECT_TRUE(lowered.diagnostics.front().find("char-array string") !=
                 std::string::npos);
}

HS_TEST(CCompat_LowersArrowMemberAccessToAddressedDeref) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "struct Pair { int left; int right; };\n"
      "int read_right(struct Pair *pair) { return pair->right; }\n",
      "arrow.c");

  HS_EXPECT_TRUE(parsed.ok());
  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(lowered.ok());
  const auto dump = hitsimple::ast::dumpToString(*lowered.unit);
  HS_EXPECT_TRUE(dump.find("DerefExpr length=4") != std::string::npos);
}

HS_TEST(CCompat_LowersArrowArrayMembersForIndexingAndDecay) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "struct Buffer { char tag; int items[3]; };\n"
      "int first(int *items) { return items[0]; }\n"
      "int exercise(struct Buffer *buffer) {\n"
      "  buffer->items[1] = 40;\n"
      "  return first(buffer->items) + buffer->items[1] - 80;\n"
      "}\n",
      "arrow-array.c");

  HS_EXPECT_TRUE(parsed.ok());
  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(lowered.ok());

  const auto dump = hitsimple::ast::dumpToString(*lowered.unit);
  HS_EXPECT_TRUE(dump.find("DerefExpr length=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=%8d*") != std::string::npos);

  auto analyzeOptions = hitsimple::sema::AnalyzeOptions{};
  analyzeOptions.requireMain = false;
  analyzeOptions.cCompatibilityMode = true;
  const auto analyzed = hitsimple::sema::analyze(*lowered.unit, analyzeOptions);
  HS_EXPECT_TRUE(analyzed.unit != nullptr);
  HS_EXPECT_TRUE(analyzed.diagnostics.empty());
}

HS_TEST(CCompat_LowersAggregateByValueFunctionAbi) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "struct Pair { int left; char tag; };\n"
      "struct Pair pass(struct Pair value) { return value; }\n",
      "aggregate-by-value.c");

  HS_EXPECT_TRUE(parsed.ok());
  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(lowered.ok());
  HS_EXPECT_EQ(lowered.unit->functions.size(), 1U);

  const auto* pass = findLinkage(lowered, "pass");
  HS_EXPECT_TRUE(pass != nullptr);
  HS_EXPECT_EQ(pass->parameterTypes.size(), 1U);
  HS_EXPECT_TRUE(pass->parameterTypes.front().kind ==
                 hitsimple::compat::CAbiValueKind::Aggregate);
  HS_EXPECT_EQ(pass->parameterTypes.front().byteLength, 8U);
  HS_EXPECT_EQ(pass->parameterTypes.front().alignment, 4U);
  HS_EXPECT_TRUE(pass->returnType.has_value());
  HS_EXPECT_TRUE(pass->returnType->kind ==
                 hitsimple::compat::CAbiValueKind::Aggregate);
  HS_EXPECT_EQ(pass->returnType->byteLength, 8U);
}

HS_TEST(CCompat_MergesStaticPrototypeWithItsDefinition) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "static int helper(void);\n"
      "static int helper(void) { return 7; }\n",
      "static-prototype.c");

  HS_EXPECT_TRUE(parsed.ok());
  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(lowered.ok());
  HS_EXPECT_EQ(lowered.unit->functions.size(), 1U);
  HS_EXPECT_TRUE(lowered.unit->externFunctions.empty());
  HS_EXPECT_EQ(lowered.linkage.size(), 1U);
  HS_EXPECT_EQ(lowered.linkage[0].sourceName, "helper");
  HS_EXPECT_TRUE(lowered.linkage[0].isDefinition);
  HS_EXPECT_TRUE(lowered.linkage[0].linkage == Linkage::Internal);
}

HS_TEST(CCompat_RejectsStandaloneStaticPrototype) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "static int helper(void);\n", "static-prototype-only.c");

  HS_EXPECT_TRUE(parsed.ok());
  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(!lowered.ok());
  HS_EXPECT_TRUE(!lowered.diagnostics.empty());
  HS_EXPECT_TRUE(lowered.diagnostics.front().message.find(
                     "requires a definition in the same translation unit") !=
                 std::string::npos);
}

HS_TEST(CCompat_RejectsIncompatibleFunctionDeclarations) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "int helper(int value);\n"
      "unsigned int helper(unsigned int value) { return value; }\n",
      "incompatible-prototype.c");

  HS_EXPECT_TRUE(parsed.ok());
  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(!lowered.ok());
  HS_EXPECT_TRUE(!lowered.diagnostics.empty());
  HS_EXPECT_TRUE(lowered.diagnostics.front().message.find(
                     "conflicting C function declaration") !=
                 std::string::npos);
}

HS_TEST(CCompat_AcceptsStandardArrayIntegerLiteralForms) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "int decimal[1_0];\n"
      "int hex_lower[0x1_0];\n"
      "int hex_upper[0X1_1];\n"
      "int octal_lower[0o1_0];\n"
      "int octal_upper[0O1_1];\n"
      "int binary_lower[0b1_0];\n"
      "int binary_upper[0B1_1];\n",
      "array-counts.c");

  HS_EXPECT_TRUE(parsed.ok());
  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(lowered.ok());
  HS_EXPECT_EQ(lowered.unit->globalNews.size(), 7U);
  HS_EXPECT_EQ(lowered.unit->globalNews[0]->length, "40");
  HS_EXPECT_EQ(lowered.unit->globalNews[1]->length, "64");
  HS_EXPECT_EQ(lowered.unit->globalNews[2]->length, "68");
  HS_EXPECT_EQ(lowered.unit->globalNews[3]->length, "32");
  HS_EXPECT_EQ(lowered.unit->globalNews[4]->length, "36");
  HS_EXPECT_EQ(lowered.unit->globalNews[5]->length, "8");
  HS_EXPECT_EQ(lowered.unit->globalNews[6]->length, "12");
}

HS_TEST(CCompat_LowersArrayTypedefsWithObjectShapeAndCAbiMetadata) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "typedef int Row[3];\n"
      "typedef int *Slots[2];\n"
      "Row row;\n"
      "Slots slots;\n"
      "int read(int *values) { return values[1]; }\n"
      "int main(void) {\n"
      "  Row local_row;\n"
      "  Slots local_slots;\n"
      "  int value = 40;\n"
      "  local_row[1] = 2;\n"
      "  local_slots[0] = &value;\n"
      "  return read(local_row) + *local_slots[0] - 42;\n"
      "}\n",
      "array-typedef.c");

  HS_EXPECT_TRUE(parsed.ok());
  HS_EXPECT_EQ(parsed.unit->declarations.size(), 6U);

  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(lowered.ok());
  HS_EXPECT_TRUE(lowered.unit->templates.empty());
  HS_EXPECT_EQ(lowered.unit->globalNews.size(), 2U);
  HS_EXPECT_EQ(lowered.unit->globalNews[0]->name, "row");
  HS_EXPECT_EQ(lowered.unit->globalNews[0]->templateName, "bytes");
  HS_EXPECT_EQ(lowered.unit->globalNews[0]->length, "12");
  HS_EXPECT_EQ(lowered.unit->globalNews[1]->name, "slots");
  HS_EXPECT_EQ(lowered.unit->globalNews[1]->templateName, "bytes");
  HS_EXPECT_EQ(lowered.unit->globalNews[1]->length,
               std::to_string(2U * sizeof(void*)));

  const auto* row = findLinkage(lowered, "row");
  HS_EXPECT_TRUE(row != nullptr);
  HS_EXPECT_TRUE(row->objectType.has_value());
  HS_EXPECT_TRUE(row->objectType->kind ==
                 hitsimple::compat::CAbiValueKind::Integer);
  HS_EXPECT_EQ(row->objectType->byteLength, 4U);
  HS_EXPECT_EQ(row->objectType->alignment, 4U);
  HS_EXPECT_EQ(row->objectType->elementCount, 3U);

  const auto* slots = findLinkage(lowered, "slots");
  HS_EXPECT_TRUE(slots != nullptr);
  HS_EXPECT_TRUE(slots->objectType.has_value());
  HS_EXPECT_TRUE(slots->objectType->kind ==
                 hitsimple::compat::CAbiValueKind::Pointer);
  HS_EXPECT_EQ(slots->objectType->byteLength, sizeof(void*));
  HS_EXPECT_EQ(slots->objectType->alignment, sizeof(void*));
  HS_EXPECT_EQ(slots->objectType->elementCount, 2U);

  const auto coreDump = hitsimple::ast::dumpToString(*lowered.unit);
  HS_EXPECT_TRUE(coreDump.find("Row") == std::string::npos);
  HS_EXPECT_TRUE(coreDump.find("Slots") == std::string::npos);

  auto analyzeOptions = hitsimple::sema::AnalyzeOptions{};
  analyzeOptions.requireMain = false;
  analyzeOptions.cCompatibilityMode = true;
  const auto analyzed = hitsimple::sema::analyze(*lowered.unit, analyzeOptions);
  HS_EXPECT_TRUE(analyzed.unit != nullptr);
  HS_EXPECT_TRUE(analyzed.diagnostics.empty());
  const auto hirDump = hitsimple::hir::dumpToString(*analyzed.unit);
  HS_EXPECT_TRUE(hirDump.find("Row") == std::string::npos);
  HS_EXPECT_TRUE(hirDump.find("Slots") == std::string::npos);
}

HS_TEST(CCompat_RejectsInvalidArrayTypedefShapesAndCycles) {
  const auto zeroLength = hitsimple::compat::parseCCompatSource(
      "typedef int Empty[0];\n", "zero-array-typedef.c");
  HS_EXPECT_TRUE(zeroLength.ok());
  const auto zeroLengthLowered =
      hitsimple::compat::lowerCCompatToCore(*zeroLength.unit);
  HS_EXPECT_TRUE(!zeroLengthLowered.ok());
  HS_EXPECT_TRUE(zeroLengthLowered.diagnostics.front().message.find(
                     "element count must be greater than zero") !=
                 std::string::npos);

  const auto multidimensional = hitsimple::compat::parseCCompatSource(
      "typedef int Row[3];\n"
      "Row matrix[2];\n", "multidimensional-array-typedef.c");
  HS_EXPECT_TRUE(multidimensional.ok());
  const auto multidimensionalLowered =
      hitsimple::compat::lowerCCompatToCore(*multidimensional.unit);
  HS_EXPECT_TRUE(!multidimensionalLowered.ok());
  HS_EXPECT_TRUE(multidimensionalLowered.diagnostics.front().message.find(
                     "multidimensional C arrays") != std::string::npos);

  const auto pointerToArray = hitsimple::compat::parseCCompatSource(
      "typedef int Row[3];\n"
      "Row *pointer;\n", "pointer-to-array-typedef.c");
  HS_EXPECT_TRUE(pointerToArray.ok());
  const auto pointerToArrayLowered =
      hitsimple::compat::lowerCCompatToCore(*pointerToArray.unit);
  HS_EXPECT_TRUE(!pointerToArrayLowered.ok());
  HS_EXPECT_TRUE(pointerToArrayLowered.diagnostics.front().message.find(
                     "pointer to C array typedef") != std::string::npos);

  const auto cycle = hitsimple::compat::parseCCompatSource(
      "typedef Second First;\n"
      "typedef First Second;\n", "cyclic-array-typedef.c");
  HS_EXPECT_TRUE(cycle.ok());
  const auto cycleLowered = hitsimple::compat::lowerCCompatToCore(*cycle.unit);
  HS_EXPECT_TRUE(!cycleLowered.ok());
  HS_EXPECT_TRUE(cycleLowered.diagnostics.front().message.find(
                     "cyclic C typedef") != std::string::npos);
}

HS_TEST(CCompat_LowersIfWhileAndCompoundAssignment) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "int find(int *values, int count) {\n"
      "  int index = 0;\n"
      "  while (index < count) {\n"
      "    if (values[index] == count) { return values[index]; }\n"
      "    index += 1;\n"
      "  }\n"
      "  return 0;\n"
      "}\n",
      "control.c");

  HS_EXPECT_TRUE(parsed.ok());
  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(lowered.ok());
  const auto dump = hitsimple::ast::dumpToString(*lowered.unit);
  HS_EXPECT_TRUE(dump.find("WhileStmt") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("IfStmt") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("AssignmentExpr") != std::string::npos);
}

HS_TEST(CCompat_RejectsUnknownTypedefAndVoidObjectDuringLowering) {
  const auto unknown = hitsimple::compat::parseCCompatSource("missing_t value;\n",
                                                              "unknown.c");
  HS_EXPECT_TRUE(unknown.ok());
  const auto unknownLowered =
      hitsimple::compat::lowerCCompatToCore(*unknown.unit);
  HS_EXPECT_TRUE(!unknownLowered.ok());
  HS_EXPECT_TRUE(unknownLowered.diagnostics.front().find("unknown C typedef") !=
                 std::string::npos);

  const auto invalidVoid =
      hitsimple::compat::parseCCompatSource("void value;\n", "void.c");
  HS_EXPECT_TRUE(invalidVoid.ok());
  const auto voidLowered =
      hitsimple::compat::lowerCCompatToCore(*invalidVoid.unit);
  HS_EXPECT_TRUE(!voidLowered.ok());
  HS_EXPECT_TRUE(voidLowered.diagnostics.front().find("void cannot") !=
                 std::string::npos);
}

HS_TEST(CCompat_RejectsUnsupportedDeclaratorsAndUnknownElementSize) {
  const auto multidimensional = hitsimple::compat::parseCCompatSource(
      "int matrix[2][3];\n", "matrix.c");
  HS_EXPECT_TRUE(!multidimensional.ok());
  HS_EXPECT_TRUE(!multidimensional.diagnostics.empty());

  const auto parenthesized = hitsimple::compat::parseCCompatSource(
      "int (*values)[4];\n", "parenthesized.c");
  HS_EXPECT_TRUE(!parenthesized.ok());
  HS_EXPECT_TRUE(!parenthesized.diagnostics.empty());

  const auto parsed = hitsimple::compat::parseCCompatSource(
      "int read_byte(void *cursor) { return cursor[0]; }\n", "void-pointer.c");
  HS_EXPECT_TRUE(parsed.ok());
  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(!lowered.ok());
  HS_EXPECT_TRUE(!lowered.diagnostics.empty());
}

HS_TEST(CCompat_PreservesUnsignedIntegerOperatorsAndCompoundAssignments) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "unsigned int identity(unsigned int value) { return value; }\n"
      "int check(void) {\n"
      "  unsigned int zero = 0U;\n"
      "  long zero_long = 0L;\n"
      "  unsigned long zero_wide = 0UL;\n"
      "  if (zero != 0U || zero_long != 0L || zero_wide != 0UL) { return 3; }\n"
      "  unsigned int value = 0x80000000U;\n"
      "  int negative = -1;\n"
      "  if (negative < value) { return 1; }\n"
      "  value >>= 31;\n"
      "  value <<= 4;\n"
      "  value &= 0x1fU;\n"
      "  value ^= 1U;\n"
      "  value |= 2U;\n"
      "  return identity(0xffffffffU) == value ? 0 : 2;\n"
      "}\n",
      "unsigned-operators.c");

  HS_EXPECT_TRUE(parsed.ok());
  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(lowered.ok());

  const auto astDump = hitsimple::ast::dumpToString(*lowered.unit);
  HS_EXPECT_TRUE(astDump.find("BinaryExpr op=%4u<") != std::string::npos);
  HS_EXPECT_TRUE(astDump.find("AssignmentTarget target=value op=%4u>>=") !=
                 std::string::npos);
  HS_EXPECT_TRUE(astDump.find("AssignmentTarget target=value op=%4u<<=") !=
                 std::string::npos);
  HS_EXPECT_TRUE(astDump.find("AssignmentTarget target=value op=%4u&=") !=
                 std::string::npos);
  HS_EXPECT_TRUE(astDump.find("AssignmentTarget target=value op=%4u^=") !=
                 std::string::npos);
  HS_EXPECT_TRUE(astDump.find("AssignmentTarget target=value op=%4u|=") !=
                 std::string::npos);
  HS_EXPECT_TRUE(astDump.find("AssignmentTarget target=value op=&=") ==
                 std::string::npos);

  auto analyzeOptions = hitsimple::sema::AnalyzeOptions{};
  analyzeOptions.requireMain = false;
  analyzeOptions.cCompatibilityMode = true;
  const auto analyzed = hitsimple::sema::analyze(*lowered.unit, analyzeOptions);
  HS_EXPECT_TRUE(analyzed.unit != nullptr);
  HS_EXPECT_TRUE(analyzed.diagnostics.empty());

  const auto hirDump = hitsimple::hir::dumpToString(*analyzed.unit);
  HS_EXPECT_TRUE(hirDump.find("UnsignedExpr bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(hirDump.find("BinaryExpr op=%4u>> bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(hirDump.find("BinaryExpr op=%4u& bytes=4") !=
                 std::string::npos);
}

HS_TEST(CCompat_AcceptsLongLongTypeFormsAndLiteralSuffixes) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "long long first = -1LL;\n"
      "long long int second = 2lL;\n"
      "unsigned long long third = 3ULL;\n"
      "unsigned long long int fourth = 4uLl;\n",
      "long-long.c");

  HS_EXPECT_TRUE(parsed.ok());
  const auto* first = dynamic_cast<const hitsimple::compat::VarDecl*>(
      parsed.unit->declarations[0].get());
  const auto* second = dynamic_cast<const hitsimple::compat::VarDecl*>(
      parsed.unit->declarations[1].get());
  const auto* third = dynamic_cast<const hitsimple::compat::VarDecl*>(
      parsed.unit->declarations[2].get());
  const auto* fourth = dynamic_cast<const hitsimple::compat::VarDecl*>(
      parsed.unit->declarations[3].get());
  HS_EXPECT_TRUE(first != nullptr);
  HS_EXPECT_TRUE(second != nullptr);
  HS_EXPECT_TRUE(third != nullptr);
  HS_EXPECT_TRUE(fourth != nullptr);
  HS_EXPECT_TRUE(first->type.base == hitsimple::compat::BaseType::LongLong);
  HS_EXPECT_TRUE(second->type.base == hitsimple::compat::BaseType::LongLong);
  HS_EXPECT_TRUE(third->type.base ==
                 hitsimple::compat::BaseType::UnsignedLongLong);
  HS_EXPECT_TRUE(fourth->type.base ==
                 hitsimple::compat::BaseType::UnsignedLongLong);
  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(lowered.ok());
  HS_EXPECT_EQ(lowered.unit->globalNews.size(), 4U);
  HS_EXPECT_EQ(lowered.unit->globalNews[0]->templateName, "i64");
  HS_EXPECT_EQ(lowered.unit->globalNews[1]->templateName, "i64");
  HS_EXPECT_EQ(lowered.unit->globalNews[2]->templateName, "u64");
  HS_EXPECT_EQ(lowered.unit->globalNews[3]->templateName, "u64");
}

HS_TEST(CCompat_RejectsInvalidLongLongLiteralSuffixes) {
  const auto thirdLong = hitsimple::compat::parseCCompatSource(
      "int value = 1LLL;\n", "third-long.c");
  HS_EXPECT_TRUE(thirdLong.ok());
  const auto thirdLongLowered =
      hitsimple::compat::lowerCCompatToCore(*thirdLong.unit);
  HS_EXPECT_TRUE(!thirdLongLowered.ok());
  HS_EXPECT_TRUE(thirdLongLowered.diagnostics.front().message.find(
                     "invalid or unsupported C integer literal") !=
                 std::string::npos);

  const auto duplicateUnsigned = hitsimple::compat::parseCCompatSource(
      "int value = 1UUL;\n", "duplicate-unsigned.c");
  HS_EXPECT_TRUE(duplicateUnsigned.ok());
  const auto duplicateUnsignedLowered =
      hitsimple::compat::lowerCCompatToCore(*duplicateUnsigned.unit);
  HS_EXPECT_TRUE(!duplicateUnsignedLowered.ok());
  HS_EXPECT_TRUE(duplicateUnsignedLowered.diagnostics.front().message.find(
                     "invalid or unsupported C integer literal") !=
                 std::string::npos);
}

HS_TEST(CCompat_LowersExplicitIntegerCastsAtTheirTargetWidths) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "int cast_values(long long wide, signed char small) {\n"
      "  signed char i8 = (signed char)wide;\n"
      "  unsigned char u8 = (unsigned char)wide;\n"
      "  short i16 = (short int)wide;\n"
      "  unsigned short u16 = (unsigned short int)wide;\n"
      "  int i32 = (int)wide;\n"
      "  unsigned int u32 = (unsigned int)wide;\n"
      "  long long i64 = (long long)small;\n"
      "  unsigned long long u64 = (unsigned long long)small;\n"
      "  return 0;\n"
      "}\n",
      "integer-casts.c");

  HS_EXPECT_TRUE(parsed.ok());
  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(lowered.ok());

  auto analyzeOptions = hitsimple::sema::AnalyzeOptions{};
  analyzeOptions.requireMain = false;
  analyzeOptions.cCompatibilityMode = true;
  const auto analyzed = hitsimple::sema::analyze(*lowered.unit, analyzeOptions);
  HS_EXPECT_TRUE(analyzed.unit != nullptr);
  HS_EXPECT_TRUE(analyzed.diagnostics.empty());

  const auto hirDump = hitsimple::hir::dumpToString(*analyzed.unit);
  HS_EXPECT_TRUE(hirDump.find("IntegerCastExpr bytes=1 signed=true") !=
                 std::string::npos);
  HS_EXPECT_TRUE(hirDump.find("IntegerCastExpr bytes=1 signed=false") !=
                 std::string::npos);
  HS_EXPECT_TRUE(hirDump.find("IntegerCastExpr bytes=2 signed=true") !=
                 std::string::npos);
  HS_EXPECT_TRUE(hirDump.find("IntegerCastExpr bytes=2 signed=false") !=
                 std::string::npos);
  HS_EXPECT_TRUE(hirDump.find("IntegerCastExpr bytes=4 signed=true") !=
                 std::string::npos);
  HS_EXPECT_TRUE(hirDump.find("IntegerCastExpr bytes=4 signed=false") !=
                 std::string::npos);
  HS_EXPECT_TRUE(hirDump.find("IntegerCastExpr bytes=8 signed=true") !=
                 std::string::npos);
  HS_EXPECT_TRUE(hirDump.find("IntegerCastExpr bytes=8 signed=false") !=
                 std::string::npos);
}

HS_TEST(CCompat_UsesIndependentPromotionsForShiftOperands) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "int shifts(unsigned int value, long long signed_count,\n"
      "           unsigned long long unsigned_count) {\n"
      "  unsigned int left = value << signed_count;\n"
      "  unsigned int right = value >> unsigned_count;\n"
      "  return 0;\n"
      "}\n",
      "integer-shifts.c");

  HS_EXPECT_TRUE(parsed.ok());
  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(lowered.ok());
  const auto astDump = hitsimple::ast::dumpToString(*lowered.unit);
  HS_EXPECT_TRUE(astDump.find("BinaryExpr op=%4u<<") != std::string::npos);
  HS_EXPECT_TRUE(astDump.find("BinaryExpr op=%4u>>") != std::string::npos);
  HS_EXPECT_TRUE(astDump.find("BinaryExpr op=%8u<<") == std::string::npos);
  HS_EXPECT_TRUE(astDump.find("BinaryExpr op=%8u>>") == std::string::npos);

  auto analyzeOptions = hitsimple::sema::AnalyzeOptions{};
  analyzeOptions.requireMain = false;
  analyzeOptions.cCompatibilityMode = true;
  const auto analyzed = hitsimple::sema::analyze(*lowered.unit, analyzeOptions);
  HS_EXPECT_TRUE(analyzed.unit != nullptr);
  HS_EXPECT_TRUE(analyzed.diagnostics.empty());
  const auto hirDump = hitsimple::hir::dumpToString(*analyzed.unit);
  HS_EXPECT_TRUE(hirDump.find("BinaryExpr op=%4u<< bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(hirDump.find("BinaryExpr op=%4u>> bytes=4") !=
                 std::string::npos);

  const auto negativeCount = hitsimple::compat::parseCCompatSource(
      "int bad(void) { return 1U << -1; }\n", "negative-shift.c");
  HS_EXPECT_TRUE(negativeCount.ok());
  const auto negativeCountLowered =
      hitsimple::compat::lowerCCompatToCore(*negativeCount.unit);
  HS_EXPECT_TRUE(!negativeCountLowered.ok());
  HS_EXPECT_TRUE(negativeCountLowered.diagnostics.front().message.find(
                     "shift count") != std::string::npos);

  const auto wideCount = hitsimple::compat::parseCCompatSource(
      "int bad(void) { return 1U << 32LL; }\n", "wide-shift.c");
  HS_EXPECT_TRUE(wideCount.ok());
  const auto wideCountLowered =
      hitsimple::compat::lowerCCompatToCore(*wideCount.unit);
  HS_EXPECT_TRUE(!wideCountLowered.ok());
  HS_EXPECT_TRUE(wideCountLowered.diagnostics.front().message.find(
                     "shift count") != std::string::npos);
}

HS_TEST(CCompat_RejectsNegativeConstantShiftCountThroughCast) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "int bad(void) { return 1U << (int)-1; }\n",
      "negative-cast-shift.c");
  HS_EXPECT_TRUE(parsed.ok());

  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(!lowered.ok());
  HS_EXPECT_TRUE(lowered.diagnostics.front().message.find("shift count") !=
                 std::string::npos);
}

HS_TEST(CCompat_RejectsPromotedNegativeNarrowShiftCount) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "int bad(void) { return 1U << (signed char)-1; }\n",
      "negative-narrow-shift.c");
  HS_EXPECT_TRUE(parsed.ok());

  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(!lowered.ok());
  HS_EXPECT_TRUE(lowered.diagnostics.front().message.find("non-negative") !=
                 std::string::npos);
}

HS_TEST(CCompat_RejectsWideConstantShiftCountThroughUnsignedCast) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "int bad(void) { return 1U << (unsigned long long)32; }\n",
      "wide-cast-shift.c");
  HS_EXPECT_TRUE(parsed.ok());

  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(!lowered.ok());
  HS_EXPECT_TRUE(lowered.diagnostics.front().message.find("shift count") !=
                 std::string::npos);
}

HS_TEST(CCompat_RejectsWideConstantShiftCountThroughBinaryExpression) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "int bad(void) { return 1U << (16 + 16); }\n",
      "wide-binary-shift.c");
  HS_EXPECT_TRUE(parsed.ok());

  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(!lowered.ok());
  HS_EXPECT_TRUE(lowered.diagnostics.front().message.find("shift count") !=
                 std::string::npos);
}

HS_TEST(CCompat_AllowsUnaryNegativeZeroShiftCount) {
  const auto parsed = hitsimple::compat::parseCCompatSource(
      "int good(void) { return 1U << -0; }\n", "negative-zero-shift.c");
  HS_EXPECT_TRUE(parsed.ok());
  const auto lowered = hitsimple::compat::lowerCCompatToCore(*parsed.unit);
  HS_EXPECT_TRUE(lowered.ok());
}
