#include "support/TestRunner.h"

#include "hitsimple/ast/AST.h"
#include "hitsimple/parser/Parser.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view minimalProgram = "func main() {\n"
                                            "    new x[1]\n"
                                            "    x %d= 42\n"
                                            "    printf(\"%d\\n\", x)\n"
                                            "    return 0\n"
                                            "}\n";

} // namespace

HS_TEST(Parser_ParsesMinimalProgram) {
  auto result = hitsimple::parser::parseSource(minimalProgram, "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());
  HS_EXPECT_EQ(result.unit->functions.size(), 1U);
  HS_EXPECT_EQ(result.unit->functions[0]->name, "main");
  HS_EXPECT_EQ(result.unit->functions[0]->body->statements.size(), 4U);
}

HS_TEST(Parser_BindsPostfixIncrementToExplicitDereferenceLvalue) {
  auto result = hitsimple::parser::parseSource("func main() {\n"
                                               "    [1]*ptr++\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());
  const auto *statement = dynamic_cast<const hitsimple::ast::ExprStmt *>(
      result.unit->functions[0]->body->statements[0].get());
  HS_EXPECT_TRUE(statement != nullptr);
  const auto *increment = dynamic_cast<const hitsimple::ast::UnaryExpr *>(
      statement->expression.get());
  HS_EXPECT_TRUE(increment != nullptr);
  HS_EXPECT_EQ(increment->op, "post++");
  HS_EXPECT_TRUE(dynamic_cast<const hitsimple::ast::DerefExpr *>(
                     increment->operand.get()) != nullptr);
}

HS_TEST(Parser_ParsesMultipleFunctions) {
  auto result = hitsimple::parser::parseSource("func helper() {\n"
                                               "    return 1\n"
                                               "}\n"
                                               "\n"
                                               "func main() {\n"
                                               "    return 0\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());
  HS_EXPECT_EQ(result.unit->functions.size(), 2U);
  HS_EXPECT_EQ(result.unit->functions[0]->name, "helper");
  HS_EXPECT_EQ(result.unit->functions[1]->name, "main");

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("FunctionDecl name=helper") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("FunctionDecl name=main") != std::string::npos);
}

HS_TEST(Parser_AcceptsSemicolonStatementSeparators) {
  auto result =
      hitsimple::parser::parseSource("func main() {\n"
                                     "    new x[1]; x %d= 42; return 0\n"
                                     "}\n",
                                     "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());
  HS_EXPECT_EQ(result.unit->functions.size(), 1U);
  HS_EXPECT_EQ(result.unit->functions[0]->body->statements.size(), 3U);
}

HS_TEST(Parser_IgnoresRepeatedStatementTerminators) {
  auto result = hitsimple::parser::parseSource(
      ";\n"
      "template Vec2 {\n"
      "    x[8] as f64;;\n"
      "\n"
      "    y[8] as f64\n"
      "}\n"
      ";\n"
      "struct Pair {\n"
      "    left[4];;\n"
      "    right[4]\n"
      "}\n"
      "impl Vec2 {\n"
      ";\n"
      "    op + (self as Vec2, other as Vec2) -> [16] {\n"
      "        ;\n"
      "        return self;;\n"
      "    }\n"
      "}\n"
      "func main() {\n"
      "    ;;\n"
      "    new x[1];;\n"
      "    return 0;;\n"
      "}\n",
      "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());
  HS_EXPECT_EQ(result.unit->templates.size(), 1U);
  HS_EXPECT_EQ(result.unit->templates[0]->members.size(), 2U);
  HS_EXPECT_EQ(result.unit->structs.size(), 1U);
  HS_EXPECT_EQ(result.unit->structs[0]->members.size(), 2U);
  HS_EXPECT_EQ(result.unit->impls.size(), 1U);
  HS_EXPECT_EQ(result.unit->impls[0]->ops.size(), 1U);
  HS_EXPECT_EQ(result.unit->functions.size(), 1U);
  HS_EXPECT_EQ(result.unit->functions[0]->body->statements.size(), 2U);
}

HS_TEST(Parser_ParsesFunctionParameters) {
  auto result =
      hitsimple::parser::parseSource("func copy(src[4] as i32, dst[s16], "
                                     "count, raw as i32, buf[16] as bytes) {\n"
                                     "    return 0\n"
                                     "}\n",
                                     "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());
  HS_EXPECT_EQ(result.unit->functions.size(), 1U);
  HS_EXPECT_EQ(result.unit->functions[0]->params.size(), 5U);
  HS_EXPECT_EQ(result.unit->functions[0]->params[0].name, "src");
  HS_EXPECT_EQ(result.unit->functions[0]->params[0].length, "4");
  HS_EXPECT_EQ(result.unit->functions[0]->params[0].templateName, "i32");
  HS_EXPECT_EQ(result.unit->functions[0]->params[1].name, "dst");
  HS_EXPECT_EQ(result.unit->functions[0]->params[1].length, "s16");
  HS_EXPECT_EQ(result.unit->functions[0]->params[2].name, "count");
  HS_EXPECT_TRUE(result.unit->functions[0]->params[2].length.empty());
  HS_EXPECT_EQ(result.unit->functions[0]->params[3].name, "raw");
  HS_EXPECT_TRUE(result.unit->functions[0]->params[3].length.empty());
  HS_EXPECT_EQ(result.unit->functions[0]->params[3].templateName, "i32");
  HS_EXPECT_EQ(result.unit->functions[0]->params[4].name, "buf");
  HS_EXPECT_EQ(result.unit->functions[0]->params[4].length, "16");
  HS_EXPECT_EQ(result.unit->functions[0]->params[4].templateName, "bytes");

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("Param name=src length=4 template=i32") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("Param name=dst length=s16") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("Param name=count") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("Param name=raw template=i32") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("Param name=buf length=16 template=bytes") !=
                 std::string::npos);
}

HS_TEST(Parser_ParsesFunctionReturnSignature) {
  auto result = hitsimple::parser::parseSource(
      "func split(value[8]) -> (quot[4], rem[4], result as bool, out[4] as "
      "i32, [16] as none, as i32) {\n"
      "    return 0\n"
      "}\n",
      "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());
  HS_EXPECT_EQ(result.unit->functions.size(), 1U);
  HS_EXPECT_EQ(result.unit->functions[0]->returns.size(), 6U);
  HS_EXPECT_EQ(result.unit->functions[0]->returns[0].name, "quot");
  HS_EXPECT_EQ(result.unit->functions[0]->returns[0].length, "4");
  HS_EXPECT_EQ(result.unit->functions[0]->returns[1].name, "rem");
  HS_EXPECT_EQ(result.unit->functions[0]->returns[1].length, "4");
  HS_EXPECT_EQ(result.unit->functions[0]->returns[2].name, "result");
  HS_EXPECT_EQ(result.unit->functions[0]->returns[2].templateName, "bool");
  HS_EXPECT_EQ(result.unit->functions[0]->returns[3].name, "out");
  HS_EXPECT_EQ(result.unit->functions[0]->returns[3].length, "4");
  HS_EXPECT_EQ(result.unit->functions[0]->returns[3].templateName, "i32");
  HS_EXPECT_EQ(result.unit->functions[0]->returns[4].length, "16");
  HS_EXPECT_EQ(result.unit->functions[0]->returns[4].templateName, "none");
  HS_EXPECT_TRUE(result.unit->functions[0]->returns[5].name.empty());
  HS_EXPECT_EQ(result.unit->functions[0]->returns[5].templateName, "i32");

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("ReturnItem name=quot length=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("ReturnItem name=rem length=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("ReturnItem name=result template=bool") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("ReturnItem name=out length=4 template=i32") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("ReturnItem length=16 template=none") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("ReturnItem template=i32") != std::string::npos);
}

HS_TEST(Parser_ParsesEmptyFunctionReturnSignature) {
  auto result = hitsimple::parser::parseSource("func main() -> () {\n"
                                               "    return 0\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());
  HS_EXPECT_EQ(result.unit->functions.size(), 1U);
  HS_EXPECT_TRUE(result.unit->functions[0]->returns.empty());
}

HS_TEST(Parser_ParsesStandardTemplateReturnSignature) {
  auto result = hitsimple::parser::parseSource("func main() -> i32 {\n"
                                               "    return 0\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());
  HS_EXPECT_EQ(result.unit->functions[0]->returns.size(), 1U);
  HS_EXPECT_TRUE(result.unit->functions[0]->returns[0].name.empty());
  HS_EXPECT_TRUE(result.unit->functions[0]->returns[0].length.empty());
  HS_EXPECT_EQ(result.unit->functions[0]->returns[0].templateName, "i32");
}

HS_TEST(Parser_AllowsBlankLinesBeforeElse) {
  auto result = hitsimple::parser::parseSource("func main() {\n"
                                               "    if (0) { return 1; }\n"
                                               "\n"
                                               "    else { return 0; }\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());
  HS_EXPECT_EQ(result.unit->functions[0]->body->statements.size(), 1U);
}

HS_TEST(Parser_ParsesTopLevelExternDeclarations) {
  auto result = hitsimple::parser::parseSource(
      "extern errno as i32\n"
      "extern host_buffer[8] as bytes\n"
      "extern puts(str[8]) -> ()\n"
      "extern split(value[8]) -> (quot[4], rem[4])\n"
      "func main() {\n"
      "    return 0\n"
      "}\n",
      "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());
  HS_EXPECT_EQ(result.unit->externVariables.size(), 2U);
  HS_EXPECT_EQ(result.unit->externVariables[0]->name, "errno");
  HS_EXPECT_TRUE(result.unit->externVariables[0]->length.empty());
  HS_EXPECT_EQ(result.unit->externVariables[0]->templateName, "i32");
  HS_EXPECT_EQ(result.unit->externVariables[1]->name, "host_buffer");
  HS_EXPECT_EQ(result.unit->externVariables[1]->length, "8");
  HS_EXPECT_EQ(result.unit->externVariables[1]->templateName, "bytes");
  HS_EXPECT_EQ(result.unit->externFunctions.size(), 2U);
  HS_EXPECT_EQ(result.unit->externFunctions[0]->name, "puts");
  HS_EXPECT_EQ(result.unit->externFunctions[0]->params.size(), 1U);
  HS_EXPECT_EQ(result.unit->externFunctions[0]->params[0].length, "8");
  HS_EXPECT_TRUE(result.unit->externFunctions[0]->returns.empty());
  HS_EXPECT_EQ(result.unit->externFunctions[1]->returns.size(), 2U);

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("ExternVarDecl name=errno template=i32") !=
                 std::string::npos);
  HS_EXPECT_TRUE(
      dump.find("ExternVarDecl name=host_buffer length=8 template=bytes") !=
      std::string::npos);
  HS_EXPECT_TRUE(dump.find("ExternFunctionDecl name=puts") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("ExternFunctionDecl name=split") !=
                 std::string::npos);
}

HS_TEST(Parser_ParsesExplicitCAbiImportsAndExports) {
  auto result = hitsimple::parser::parseSource(
      "extern \"C\" native_add(value as i32) -> i32\n"
      "extern \"C\" func hsc_increment(value as i32) -> i32 {\n"
      "    return value %d+ 1\n"
      "}\n"
      "func main() {\n"
      "    return hsc_increment(41)\n"
      "}\n",
      "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());
  HS_EXPECT_EQ(result.unit->externFunctions.size(), 1U);
  HS_EXPECT_EQ(result.unit->externFunctions.front()->abiName, "\"C\"");
  HS_EXPECT_EQ(result.unit->functions.size(), 2U);
  HS_EXPECT_EQ(result.unit->functions.front()->name, "hsc_increment");
  HS_EXPECT_EQ(result.unit->functions.front()->abiName, "\"C\"");

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("ExternFunctionDecl name=native_add abi=\"C\"") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("FunctionDecl name=hsc_increment abi=\"C\"") !=
                 std::string::npos);
}

HS_TEST(Parser_ParsesTopLevelGlobalNewDeclaration) {
  auto result = hitsimple::parser::parseSource("new global_count[4]\n"
                                               "func main() {\n"
                                               "    return 0\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());
  HS_EXPECT_EQ(result.unit->globalNews.size(), 1U);
  HS_EXPECT_EQ(result.unit->globalNews[0]->name, "global_count");
  HS_EXPECT_EQ(result.unit->globalNews[0]->length, "4");

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("GlobalNewDecl name=global_count length=4") !=
                 std::string::npos);
}

HS_TEST(Parser_ParsesTopLevelGlobalNewInitializerAndTemplate) {
  auto result = hitsimple::parser::parseSource("new value as f64 = 1.0\n"
                                               "func main() {\n"
                                               "    return 0\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());
  HS_EXPECT_EQ(result.unit->globalNews.size(), 1U);
  const auto &global = *result.unit->globalNews.front();
  HS_EXPECT_EQ(global.name, "value");
  HS_EXPECT_TRUE(global.length.empty());
  HS_EXPECT_EQ(global.templateName, "f64");
  HS_EXPECT_EQ(global.assignmentOp, "=");
  HS_EXPECT_TRUE(dynamic_cast<const hitsimple::ast::FloatLiteral *>(
                     global.initializer.get()) != nullptr);

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("GlobalNewDecl name=value op== template=f64") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("FloatLiteral value=1.0") != std::string::npos);
}

HS_TEST(Parser_ParsesTopLevelStructDeclaration) {
  auto result = hitsimple::parser::parseSource("struct Point {\n"
                                               "    x[4]\n"
                                               "    y[4]\n"
                                               "}\n"
                                               "func main() {\n"
                                               "    return 0\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());
  HS_EXPECT_EQ(result.unit->structs.size(), 1U);
  HS_EXPECT_EQ(result.unit->structs[0]->name, "Point");
  HS_EXPECT_EQ(result.unit->structs[0]->members.size(), 2U);
  HS_EXPECT_EQ(result.unit->structs[0]->members[0].name, "x");
  HS_EXPECT_EQ(result.unit->structs[0]->members[0].length, "4");

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("StructDecl name=Point") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("StructMember name=y length=4") !=
                 std::string::npos);
}

HS_TEST(Parser_ParsesStructTemplateDeclarationMark) {
  auto result = hitsimple::parser::parseSource("struct Pair {\n"
                                               "    left[4]\n"
                                               "    right[4]\n"
                                               "}\n"
                                               "func main() {\n"
                                               "    new p[s2] ;Pair\n"
                                               "    return p[s1].right\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("DeclItem name=p length=s2 template=Pair") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("IndexExpr") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("MemberExpr member=right") != std::string::npos);
}

HS_TEST(Parser_ParsesBeta21TemplateImplAndAsEntrypoints) {
  auto result = hitsimple::parser::parseSource(
      "template Vec2 {\n"
      "    x[8] as f64\n"
      "    y[8] as f64\n"
      "}\n"
      "impl Vec2 {\n"
      "    op + (self as Vec2, other as Vec2) {\n"
      "        return self\n"
      "    }\n"
      "    func length(self as Vec2) -> f64 {\n"
      "        return 0.0\n"
      "    }\n"
      "}\n"
      "func main() {\n"
      "    new raw[16] as bytes\n"
      "    set raw as none\n"
      "    raw as bytes\n"
      "    return 0\n"
      "}\n",
      "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());
  HS_EXPECT_EQ(result.unit->templates.size(), 1U);
  HS_EXPECT_EQ(result.unit->impls.size(), 1U);
  HS_EXPECT_TRUE(result.unit->impls[0]->containsOp());
  HS_EXPECT_EQ(result.unit->impls[0]->methods.size(), 1U);

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("TemplateDecl name=Vec2") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("TemplateMember name=x length=8 template=f64") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("ImplDecl name=Vec2 containsOp=true") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("ImplOpDecl op=+") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("ImplOpParam name=self template=Vec2") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("ImplMethodDecl") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("FunctionDecl name=length") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("Param name=self template=Vec2") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("ReturnItem template=f64") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("DeclItem name=raw length=16 template=bytes") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("SetStmt template=none") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("AsExpr template=bytes") != std::string::npos);
}

HS_TEST(Parser_ParsesSetMemberChain) {
  auto result = hitsimple::parser::parseSource("func main() {\n"
                                               "    set user.id as u32\n"
                                               "    return 0\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("SetStmt template=u32") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("MemberExpr member=id") != std::string::npos);
}

HS_TEST(Parser_ParsesDeclarationListsStaticAndInitializers) {
  auto result =
      hitsimple::parser::parseSource("func main() {\n"
                                     "    static counter[4], cache[8]\n"
                                     "    new {\n"
                                     "        id[4] = 100,\n"
                                     "        name[32] %s= \"HitSimple\"\n"
                                     "    }\n"
                                     "    return 0\n"
                                     "}\n",
                                     "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());
  HS_EXPECT_EQ(result.unit->functions[0]->body->statements.size(), 3U);

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("VarDeclStmt storage=static") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("DeclItem name=counter length=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("DeclItem name=id length=4 op==") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("DeclItem name=name length=32 op=%s=") !=
                 std::string::npos);
}

HS_TEST(Parser_ParsesOrdinaryArithmeticPrecedence) {
  auto result = hitsimple::parser::parseSource("func main() {\n"
                                               "    new x[4]\n"
                                               "    x = 1 + 2 * 3 - 4 / 2 % 2\n"
                                               "    return 0\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  const auto minus = dump.find("BinaryExpr op=-");
  const auto plus = dump.find("BinaryExpr op=+");
  const auto multiply = dump.find("BinaryExpr op=*");
  const auto remainder = dump.find("BinaryExpr op=%");
  HS_EXPECT_TRUE(minus != std::string::npos);
  HS_EXPECT_TRUE(plus != std::string::npos);
  HS_EXPECT_TRUE(multiply != std::string::npos);
  HS_EXPECT_TRUE(remainder != std::string::npos);
  HS_EXPECT_TRUE(minus < plus);
  HS_EXPECT_TRUE(plus < multiply);
}

HS_TEST(Parser_ParsesExpandedExpressionAndLvalueGrammar) {
  auto result = hitsimple::parser::parseSource(
      "func main() {\n"
      "    new x[8]\n"
      "    x[0] = sizeof(Point) + [4]*ptr * (true ? 2 : 3)\n"
      "    x = !flag || value == 1 && other != 2\n"
      "    x = record.member + bytes[1:4] + bytes[1:+2]\n"
      "    x = addr? < limit\n"
      "    x = (addr?) ? 1 : 2\n"
      "    return 0\n"
      "}\n",
      "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("IndexExpr") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("SizeofExpr name=Point") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("DerefExpr length=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("TernaryExpr") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("UnaryExpr op=!") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("UnsignedExpr") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=||") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("MemberExpr member=member") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("SliceExpr mode=end") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("SliceExpr mode=length") != std::string::npos);
}

HS_TEST(Parser_DumpAstForMinimalProgram) {
  auto result = hitsimple::parser::parseSource(minimalProgram, "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("FunctionDecl name=main") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("NewDecl name=x length=1") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("AssignStmt target=x op=%d=") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=printf") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("ReturnStmt") != std::string::npos);
}

HS_TEST(Parser_ParsesExpandedControlFlowStatements) {
  auto result =
      hitsimple::parser::parseSource("func main() {\n"
                                     "    if (x) {\n"
                                     "        goto done\n"
                                     "    } else if (y) {\n"
                                     "        throw 1\n"
                                     "    } else {\n"
                                     "        set y ;none\n"
                                     "    }\n"
                                     "    for (new i[4]; i < 10; i++) {\n"
                                     "        continue\n"
                                     "    }\n"
                                     "    try {\n"
                                     "        done:\n"
                                     "        return 0\n"
                                     "    } catch (err[4] as i32) {\n"
                                     "        return err\n"
                                     "    }\n"
                                     "}\n",
                                     "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("GotoStmt label=done") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("ThrowStmt") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("SetStmt template=none") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("ForStmt") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("UnaryExpr op=post++") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("TryCatchStmt error=err length=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("template=i32") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("LabelStmt label=done") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("EmptyStmt") != std::string::npos);
}

HS_TEST(Parser_ParsesCatchParameterEntrypoints) {
  auto result = hitsimple::parser::parseSource(
      "func main() {\n"
      "    try { throw 1; } catch (err[4]) { return err; }\n"
      "    try { throw 1; } catch (err as i32) { return err; }\n"
      "    try { throw 1; } catch (err[4] as i32) { return err; }\n"
      "    try { throw 1; } catch (err) { return err; }\n"
      "    return 0\n"
      "}\n",
      "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("TryCatchStmt error=err length=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("TryCatchStmt error=err template=i32") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("TryCatchStmt error=err length=4 template=i32") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("TryCatchStmt error=err\n") != std::string::npos);
}

HS_TEST(Parser_RejectsTemplateMemberWithoutLength) {
  auto result = hitsimple::parser::parseSource("template Vec2 {\n"
                                               "    x as f64\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_TRUE(result.error.find("syntax error") != std::string::npos);
}

HS_TEST(Parser_ParsesTypedBinaryExpressionPrecedence) {
  auto result = hitsimple::parser::parseSource("func main() {\n"
                                               "    new x[1]\n"
                                               "    x %d= 1 %d+ 2 %d* 3\n"
                                               "    return 0\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  const auto add = dump.find("BinaryExpr op=%d+");
  const auto multiply = dump.find("BinaryExpr op=%d*");
  HS_EXPECT_TRUE(add != std::string::npos);
  HS_EXPECT_TRUE(multiply != std::string::npos);
  HS_EXPECT_TRUE(add < multiply);
}

HS_TEST(Parser_PreservesCompoundAssignmentOperator) {
  auto result = hitsimple::parser::parseSource("func main() {\n"
                                               "    new x[1]\n"
                                               "    x %d+= 2\n"
                                               "    return 0\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("AssignStmt target=x op=%d+=") != std::string::npos);
}

HS_TEST(Parser_ParsesStageFMultiAssignmentTargets) {
  auto result =
      hitsimple::parser::parseSource("func main() {\n"
                                     "    new a[4], b[4], text[8]\n"
                                     "    a, _, (text %s=) = 1, b, \"ok\"\n"
                                     "    return 0\n"
                                     "}\n",
                                     "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("AssignmentTarget target=a op==") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("AssignmentTarget target=_ op==") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("AssignmentTarget target=text op=%s=") !=
                 std::string::npos);
}

HS_TEST(Parser_ParsesParenthesizedTypedBinaryExpression) {
  auto result = hitsimple::parser::parseSource("func main() {\n"
                                               "    new x[1]\n"
                                               "    x %d= (1 %d+ 2) %d* 3\n"
                                               "    return 0\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  const auto multiply = dump.find("BinaryExpr op=%d*");
  const auto add = dump.find("BinaryExpr op=%d+");
  HS_EXPECT_TRUE(multiply != std::string::npos);
  HS_EXPECT_TRUE(add != std::string::npos);
  HS_EXPECT_TRUE(multiply < add);
}

HS_TEST(Parser_ParsesControlFlowStatements) {
  auto result = hitsimple::parser::parseSource("func main() {\n"
                                               "    new x[1]\n"
                                               "    while (x) {\n"
                                               "        if (x) {\n"
                                               "            continue\n"
                                               "        } else {\n"
                                               "            break\n"
                                               "        }\n"
                                               "    }\n"
                                               "    return 0\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("WhileStmt") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("IfStmt") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("ContinueStmt") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("BreakStmt") != std::string::npos);
}

HS_TEST(Parser_ReportsSyntaxErrorLocation) {
  auto result = hitsimple::parser::parseSource("func main() {\n"
                                               "    new x[]\n"
                                               "}\n",
                                               "broken.hs");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_TRUE(result.error.find("broken.hs:2:11") != std::string::npos);
  HS_EXPECT_TRUE(result.error.find("parser: error") == std::string::npos);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);

  const auto& diagnostic = result.diagnostics[0];
  HS_EXPECT_EQ(diagnostic.stage, hitsimple::diagnostic::Stage::Parser);
  HS_EXPECT_EQ(diagnostic.severity, hitsimple::diagnostic::Severity::Error);
  HS_EXPECT_TRUE(diagnostic.range.has_value());
  HS_EXPECT_EQ(diagnostic.range->begin.file, std::string("broken.hs"));
  HS_EXPECT_EQ(diagnostic.range->begin.line, 2U);
  HS_EXPECT_EQ(diagnostic.range->begin.column, 11U);
  HS_EXPECT_EQ(diagnostic.format(),
               std::string("broken.hs:2:11: parser: error: ") +
                   diagnostic.message);
}

HS_TEST(Parser_ParsesMultiReturnStatementValues) {
  auto result = hitsimple::parser::parseSource("func pair() -> ([4], [4]) {\n"
                                               "    return 1, 2\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("ReturnStmt") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerLiteral value=1") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerLiteral value=2") != std::string::npos);
}

HS_TEST(Parser_ParsesNestedCallArgumentsAndAssignmentExpression) {
  auto result =
      hitsimple::parser::parseSource("func main() {\n"
                                     "    new a[4], b[4]\n"
                                     "    printf(\"%d\", add(a = b = 1, 2))\n"
                                     "    return a\n"
                                     "}\n",
                                     "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=printf") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("CallExpr callee=add") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("AssignmentExpr") != std::string::npos);
}

HS_TEST(Parser_ParsesForStatementWithEmptyClauses) {
  auto result = hitsimple::parser::parseSource("func main() {\n"
                                               "    for (; ; ) {\n"
                                               "        break\n"
                                               "    }\n"
                                               "    return 0\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("ForStmt") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("BreakStmt") != std::string::npos);
}

HS_TEST(Parser_ParsesNestedStructMemberAndSliceTargets) {
  auto result = hitsimple::parser::parseSource("func main() {\n"
                                               "    new packet[16]\n"
                                               "    packet.header[0:+4] = 7\n"
                                               "    return packet.header[0]\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());

  const std::string dump = hitsimple::ast::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("MemberExpr member=header") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("SliceExpr mode=length") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("IndexExpr") != std::string::npos);
}

HS_TEST(Parser_RejectsMissingFunctionBody) {
  auto result = hitsimple::parser::parseSource("func main()\n", "broken.hs");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_TRUE(result.error.find("broken.hs:") == 0);
}

HS_TEST(Parser_AttachesSourceRangesToNativeAstNodes) {
  auto result = hitsimple::parser::parseSource("func main() {\n"
                                               "    return missing\n"
                                               "}\n",
                                               "test.hs");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());
  HS_EXPECT_TRUE(result.unit->range.has_value());

  const auto *function = result.unit->functions[0];
  HS_EXPECT_TRUE(function->range.has_value());
  HS_EXPECT_EQ(function->range->begin.file, std::string("test.hs"));
  HS_EXPECT_EQ(function->range->begin.line, 1U);
  HS_EXPECT_EQ(function->range->begin.column, 1U);
  HS_EXPECT_TRUE(function->body->range.has_value());
  HS_EXPECT_EQ(function->body->range->begin.line, 1U);
  HS_EXPECT_EQ(function->body->range->begin.column, 13U);

  const auto *returned = dynamic_cast<const hitsimple::ast::ReturnStmt *>(
      function->body->statements[0].get());
  HS_EXPECT_TRUE(returned != nullptr);
  HS_EXPECT_TRUE(returned->range.has_value());
  HS_EXPECT_EQ(returned->range->begin.line, 2U);
  HS_EXPECT_EQ(returned->range->begin.column, 5U);
  HS_EXPECT_TRUE(returned->values[0]->range.has_value());
  HS_EXPECT_EQ(returned->values[0]->range->begin.line, 2U);
  HS_EXPECT_EQ(returned->values[0]->range->begin.column, 12U);
}

HS_TEST(Parser_PreservesPreprocessorLineOriginInAstRange) {
  std::vector<hitsimple::diagnostic::SourceLocation> lineOrigins = {
      {"main.hs", 1, 1}, {"included.hsi", 42, 1}, {"main.hs", 2, 1}};
  auto result = hitsimple::parser::parseSource("func main() {\n"
                                               "    return missing\n"
                                               "}\n",
                                               "main.hs",
                                               std::move(lineOrigins));

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.error.empty());
  const auto *returned = dynamic_cast<const hitsimple::ast::ReturnStmt *>(
      result.unit->functions[0]->body->statements[0].get());
  HS_EXPECT_TRUE(returned != nullptr);
  HS_EXPECT_TRUE(returned->values[0]->range.has_value());
  HS_EXPECT_EQ(returned->values[0]->range->begin.file,
               std::string("included.hsi"));
  HS_EXPECT_EQ(returned->values[0]->range->begin.line, 42U);
  HS_EXPECT_EQ(returned->values[0]->range->begin.column, 12U);
}
