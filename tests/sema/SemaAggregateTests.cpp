#include "SemaTestSupport.h"

#include "hitsimple/hir/HIR.h"

#include <string>

using hitsimple::testing::sema::analyzeSource;
using hitsimple::testing::sema::minimalProgram;

HS_TEST(Sema_LowersTopLevelGlobalNewBinding) {
  auto result = analyzeSource("new global_count[4]\n"
                              "func main() {\n"
                              "    global_count %d= 1\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("GlobalMemory name=global_count "
                           "binding=global_count bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerStore target=global_count "
                           "binding=global_count bytes=4 storage=global") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersTypedTopLevelGlobalNewBinding) {
  auto result = analyzeSource("new global_count as i32\n"
                              "func main() {\n"
                              "    global_count = 1\n"
                              "    return global_count\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("GlobalMemory name=global_count "
                           "binding=global_count bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerStore target=global_count "
                           "binding=global_count bytes=4 storage=global") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersStructLayoutsAndMembers) {
  auto result = analyzeSource("struct Pair {\n"
                              "    left[4]\n"
                              "    right[4]\n"
                              "}\n"
                              "func main() {\n"
                              "    new p[s2] ;Pair\n"
                              "    p.left = 1\n"
                              "    p[s1].right = sizeof(Pair)\n"
                              "    return p[s1].right\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("StructLayout name=Pair bytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("Member name=right offset=4 bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=p binding=p bytes=16 "
                           "storage=local template=Pair") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerStore target=p binding=p bytes=4 "
                           "storage=local offset=12") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("VariableRef name=p binding=p bytes=4 "
                           "storage=local offset=12") != std::string::npos);
}

HS_TEST(Sema_LowersTemplateMembers) {
  auto result = analyzeSource("template Pair {\n"
                              "    left[4] as i32\n"
                              "    right[4] as i32\n"
                              "}\n"
                              "func main() {\n"
                              "    new p as Pair\n"
                              "    p.left = 7\n"
                              "    return p.left\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("ViewTemplate name=Pair bytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerStore target=p binding=p bytes=4 "
                           "storage=local") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("VariableRef name=p binding=p bytes=4 "
                           "storage=local") != std::string::npos);
}

HS_TEST(Sema_LowersNestedMemberTemplateOverride) {
  auto result = analyzeSource("template Profile {\n"
                              "    id[4] as u32\n"
                              "}\n"
                              "template User {\n"
                              "    tag[4] as u32\n"
                              "    profile[4] as Profile\n"
                              "}\n"
                              "func main() {\n"
                              "    new user as User\n"
                              "    set user.profile.id as i32\n"
                              "    user.profile.id = -1\n"
                              "    return user.profile.id\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("IntegerStore target=user binding=user bytes=4 "
                           "storage=local offset=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("VariableRef name=user binding=user bytes=4 "
                           "storage=local offset=4") != std::string::npos);
}

HS_TEST(Sema_KeepsMemberTemplateOverridesBoundToOneName) {
  auto result = analyzeSource("template Profile {\n"
                              "    name[4] as cstr\n"
                              "}\n"
                              "template User {\n"
                              "    profile[4] as Profile\n"
                              "}\n"
                              "func main() {\n"
                              "    new first as User\n"
                              "    new second as User\n"
                              "    set first.profile.name as none\n"
                              "    second.profile.name = \"ok\"\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
}

HS_TEST(Sema_PreservesMemberTemplateOverrideAfterAddressRebinding) {
  auto result = analyzeSource("template User {\n"
                              "    name[8] as u64\n"
                              "}\n"
                              "func main() {\n"
                              "    new first as User\n"
                              "    new second as User\n"
                              "    set first.name as cstr\n"
                              "    first &= &second\n"
                              "    first.name = \"ok\"\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
}

HS_TEST(Sema_LeavesOtherMemberPathsAtTheirDeclaredTemplate) {
  auto result = analyzeSource("template User {\n"
                              "    name[4] as cstr\n"
                              "    tag[4] as cstr\n"
                              "}\n"
                              "func main() {\n"
                              "    new user as User\n"
                              "    set user.name as none\n"
                              "    user.tag = \"ok\"\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());
}

HS_TEST(Sema_LowersAddressableTemplateViewsForMemberReadWriteAndAddressOf) {
  auto result = analyzeSource("template Pair {\n"
                              "    left[4] as i32\n"
                              "}\n"
                              "func main() {\n"
                              "    new raw[4] as bytes\n"
                              "    (raw as Pair).left = 7\n"
                              "    new ptr[P] as addr = &(raw as Pair).left\n"
                              "    new value[4] = (raw as Pair).left\n"
                              "    return value - 7\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("IntegerStore target=raw binding=raw bytes=4 "
                           "storage=local") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("AddressOf name=raw binding=raw targetBytes=4 "
                           "storage=local bytes=8") != std::string::npos);
}

HS_TEST(Sema_DoesNotPersistAnExpressionTemplateViewOnItsSourceBinding) {
  auto result = analyzeSource("func main() {\n"
                              "    new raw[4] as cstr\n"
                              "    (raw as f32) = 1.0\n"
                              "    raw = \"x\"\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("FloatStore target=raw binding=raw bytes=4 "
                           "storage=local") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("StringStore target=raw binding=raw bytes=4 "
                           "storage=local value=\"x\"") != std::string::npos);
}

HS_TEST(Sema_RejectsInvalidStructMemberAccess) {
  auto result = analyzeSource("struct Pair {\n"
                              "    left[4]\n"
                              "}\n"
                              "func main() {\n"
                              "    new p[4] ;Pair\n"
                              "    return p.right\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_TRUE(!result.diagnostics.empty());
  HS_EXPECT_TRUE(result.diagnostics[0].find("unknown member 'right'") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersDeclarationLists) {
  auto result = analyzeSource("func main() {\n"
                              "    new a[4], b[4]\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=a binding=a bytes=4") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=b binding=b bytes=4") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersIndexAssignmentTargets) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[4]\n"
                              "    x[0] = 1\n"
                              "    x = x[0]\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("PointerStore bytes=1") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("DerefExpr bytes=1") != std::string::npos);
}

HS_TEST(Sema_LowersSliceReadAndWrite) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[8]\n"
                              "    new y[4]\n"
                              "    x[0:4] = 42\n"
                              "    y = x[0:+4]\n"
                              "    return y\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("PointerStore bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("DerefExpr bytes=4") != std::string::npos);
}

HS_TEST(Sema_LowersCompoundAssignmentForIndexAndSliceTargets) {
  auto result = analyzeSource("func main() {\n"
                              "    new x[8]\n"
                              "    x[0] %1d+= 2\n"
                              "    x[0:4] %4d+= 3\n"
                              "    return x[0]\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("PointerStore bytes=1") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("PointerStore bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=%1d+ bytes=1") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("BinaryExpr op=%4d+ bytes=4") != std::string::npos);
}

HS_TEST(Sema_RejectsStageHInvalidJumps) {
  auto unknown = analyzeSource("func main() {\n"
                               "    goto missing\n"
                               "    return 0\n"
                               "}\n");
  HS_EXPECT_TRUE(unknown.unit == nullptr);
  HS_EXPECT_TRUE(unknown.diagnostics[0].find("unknown label 'missing'") !=
                 std::string::npos);

  auto duplicate = analyzeSource("func main() {\n"
                                 "    here: return 0\n"
                                 "    here: return 1\n"
                                 "}\n");
  HS_EXPECT_TRUE(duplicate.unit == nullptr);
  HS_EXPECT_TRUE(duplicate.diagnostics[0].find("duplicate label 'here'") !=
                 std::string::npos);

  auto inner = analyzeSource("func main() {\n"
                             "    goto inside\n"
                             "    if (1) {\n"
                             "        inside: return 0\n"
                             "    }\n"
                             "    return 1\n"
                             "}\n");
  HS_EXPECT_TRUE(inner.unit == nullptr);
  HS_EXPECT_TRUE(inner.diagnostics[0].find("goto into an inner block") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersStructArrayElementOffsets) {
  auto result = analyzeSource("struct Pair {\n"
                              "    left[4]\n"
                              "    right[4]\n"
                              "}\n"
                              "func main() {\n"
                              "    new p[s3] ;Pair\n"
                              "    p[s2].right = 9\n"
                              "    return p[s2].right\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("LocalMemory name=p binding=p bytes=24 "
                           "storage=local template=Pair") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("IntegerStore target=p binding=p bytes=4 "
                           "storage=local offset=20") != std::string::npos);
}

HS_TEST(Sema_LowersMemberStringCopyOffsets) {
  auto result = analyzeSource("struct Pair {\n"
                              "    left[8]\n"
                              "    right[8]\n"
                              "}\n"
                              "func main() {\n"
                              "    new p[s1] ;Pair\n"
                              "    p.left %s= \"left\"\n"
                              "    p.right %s= p.left\n"
                              "    p.left %s= p.right\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("StringCopyStore target=p binding=p bytes=8 "
                           "storage=local offset=8 source=p "
                           "sourceBinding=p sourceBytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("StringCopyStore target=p binding=p bytes=8 "
                           "storage=local source=p sourceBinding=p "
                           "sourceBytes=8 sourceOffset=8") !=
                 std::string::npos);
}

HS_TEST(Sema_RejectsStructArrayMemberAccessOutOfBounds) {
  auto result = analyzeSource("struct Pair {\n"
                              "    left[4]\n"
                              "    right[4]\n"
                              "}\n"
                              "func main() {\n"
                              "    new p[4] ;Pair\n"
                              "    p[s3].right = 5\n"
                              "    return 0\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit == nullptr);
  HS_EXPECT_EQ(result.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(result.diagnostics[0].find("out of bounds") !=
                 std::string::npos);
}

HS_TEST(Sema_LowersGlobalSliceReadAndWrite) {
  auto result = analyzeSource("new global_bytes[8]\n"
                              "func main() {\n"
                              "    global_bytes[4:+4] = 5\n"
                              "    return global_bytes[4:+4]\n"
                              "}\n");

  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(result.diagnostics.empty());

  const std::string dump = hitsimple::hir::dumpToString(*result.unit);
  HS_EXPECT_TRUE(dump.find("GlobalMemory name=global_bytes "
                           "binding=global_bytes bytes=8") !=
                 std::string::npos);
  HS_EXPECT_TRUE(dump.find("PointerStore bytes=4") != std::string::npos);
  HS_EXPECT_TRUE(dump.find("DerefExpr bytes=4") != std::string::npos);
}
