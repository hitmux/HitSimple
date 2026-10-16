#include "SemaTestSupport.h"

#include "hitsimple/hir/HIR.h"

#include <algorithm>
#include <string>
#include <vector>

using hitsimple::testing::sema::analyzeSource;

namespace {
void flattenCStringStatements(const hitsimple::hir::Stmt &statement,
                              std::vector<const hitsimple::hir::Stmt *> &out) {
  if (const auto *list = dynamic_cast<const hitsimple::hir::StatementList *>(&statement)) {
    for (const auto &child : list->statements) flattenCStringStatements(*child, out);
  } else {
    out.push_back(&statement);
  }
}
} // namespace

HS_TEST(Sema_CstrDefaultRejectsNonStringSourceMatrix) {
  // Every destination path uses the same source acceptance, independently of
  // equal byte lengths, numeric interpretation, or hidden multi-return storage.
  for (const std::string type : {"as i64", "as u64", "as f64", "as bool",
                                 "as addr", "as handle", "[8] as none",
                                 "[8] as bytes", "as Box"}) {
    const std::string parameter = "source " + type;
    const std::string prefix = "template Box { data[8] as bytes; }\n";
    const std::vector<std::string> programs = {
        "func test(" + parameter + ") { new dst[8] as cstr; dst = source; }\n",
        "func test(" + parameter + ") { new dst[8] as cstr; new code as i32; dst, code = source, 0; }\n",
        "func test(" + parameter + ") { new dst[8] as cstr = source; }\n",
        "func test(" + parameter + ") { new dst as cstr = source; }\n",
        "func test(" + parameter + ") { static dst[8] as cstr = source; }\n",
        "new source " + (type == "[8] as none" ? "[8]" : type) + ";\n"
        "new dst[8] as cstr = source\n",
        "func test(" + parameter + ") -> out[8] as cstr { out = source; return; }\n",
        "func test(" + parameter + ") -> out[8] as cstr { return source; }\n",
        "func test(" + parameter + ") -> [8] as cstr { return source; }\n",
        "func pair() -> (value " + type + ", code as i32) { return; }\n"
        "func test() { new dst[8] as cstr; dst, _ = pair(); }\n",
        "func test(" + parameter + ") -> (out[8] as cstr, code as i32) { return source, 0; }\n"};
    for (const auto &program : programs) {
      auto result = analyzeSource(prefix + program + "func main() -> i32 { return 0; }\n");
      HS_EXPECT_TRUE(result.unit == nullptr);
      HS_EXPECT_TRUE(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
          [](const auto &diagnostic) {
            return diagnostic.find("cstr default assignment requires a cstr source View") != std::string::npos ||
                   diagnostic.find("handle value may only be assigned to a handle target") != std::string::npos;
          }));
    }
  }
  for (const auto *source : {"1", "1.0", "'a'", "'ab'", "true", "\"ok\" as none"}) {
    auto result = analyzeSource(std::string("func main() { new dst[8] as cstr = ") + source + "; }\n");
    HS_EXPECT_TRUE(result.unit == nullptr);
    HS_EXPECT_TRUE(!result.diagnostics.empty());
  }
}

HS_TEST(Sema_CstrDefaultAcceptsViewsAndMaterializesRvaluesOnce) {
  using namespace hitsimple::hir;
  auto result = analyzeSource(
      "func text() -> [8] as cstr { return \"hello\"; }\n"
      "func relay() -> out[4] as cstr { return text(); }\n"
      "func pair() -> (left[8] as cstr, right[4] as cstr) { return text(), \"ok\"; }\n"
      "func main() -> i32 {\n"
      "  new source[8] as cstr = \"source\"\n"
      "  new dst[4] as cstr = text()\n"
      "  dst = source\n"
      "  dst = true ? text() : source\n"
      "  new raw[8] as bytes = \"rawdata\"\n"
      "  dst = raw as cstr\n"
      "  dst = (source = \"next\")\n"
      "  dst, source = pair()\n"
      "  return 0\n}\n");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(verifyHIR(*result.unit).empty());
  std::vector<const Stmt *> statements;
  for (const auto &statement : result.unit->functions.back()->body->statements)
    flattenCStringStatements(*statement, statements);
  unsigned materialized = 0;
  unsigned boundedCopies = 0;
  for (const auto *statement : statements) {
    if (const auto *copy = dynamic_cast<const ViewCopyStore *>(statement);
        copy != nullptr && copy->target.starts_with("$assignment.string")) {
      ++materialized;
      HS_EXPECT_EQ(copy->value->result.templateName, "cstr");
      HS_EXPECT_EQ(copy->conversionPlan.source.templateName, "cstr");
      HS_EXPECT_EQ(copy->targetByteLength, 8U);
    }
    if (const auto *copy = dynamic_cast<const StringCopyStore *>(statement)) {
      ++boundedCopies;
      HS_EXPECT_TRUE(copy->targetByteLength == 4U || copy->targetByteLength == 8U);
      HS_EXPECT_TRUE(copy->sourceByteLength == 4U || copy->sourceByteLength == 8U);
    }
  }
  HS_EXPECT_EQ(materialized, 4U);
  HS_EXPECT_EQ(boundedCopies, 7U);
}

HS_TEST(Sema_CstrExplicitStringAssignmentKeepsRawSources) {
  auto result = analyzeSource(
      "template Box { value[8] as bytes; }\n"
      "func test(integer as i64, floating as f64, raw[8] as none, bytes[8] as bytes, box as Box) {\n"
      "  new dst[8] as cstr %s= integer\n"
      "  dst %s= floating\n"
      "  dst %s= raw\n"
      "  dst %s= bytes\n"
      "  dst %s= box\n"
      "}\nfunc main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  HS_EXPECT_TRUE(result.unit != nullptr);
  HS_EXPECT_TRUE(hitsimple::hir::verifyHIR(*result.unit).empty());
}

HS_TEST(Sema_CstrAnonymousReturnStorageKeepsCAbiBoundary) {
  auto ordinary = analyzeSource(
      "func text() -> [8] as cstr { return \"ok\"; }\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(ordinary.unit != nullptr);
  HS_EXPECT_TRUE(ordinary.diagnostics.empty());
  HS_EXPECT_TRUE(dynamic_cast<const hitsimple::hir::LocalMemory *>(
      ordinary.unit->functions.front()->body->statements.front().get()) != nullptr);
  auto cAbi = analyzeSource(
      "extern \"C\" func echo(value[P] as cstr) -> cstr { return value; }\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(cAbi.unit != nullptr);
  HS_EXPECT_TRUE(cAbi.diagnostics.empty());
  HS_EXPECT_TRUE(dynamic_cast<const hitsimple::hir::Return *>(
      cAbi.unit->functions.front()->body->statements.front().get()) != nullptr);
  auto invalidBare = analyzeSource(
      "func text() -> [8] as cstr { return; }\n"
      "func main() -> i32 { return 0; }\n");
  HS_EXPECT_TRUE(invalidBare.unit == nullptr);
  HS_EXPECT_TRUE(!invalidBare.diagnostics.empty());
}
