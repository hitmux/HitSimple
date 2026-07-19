#include "hitsimple/stdlib/StandardLibrary.h"
#include "hitsimple/support/ResourcePaths.h"
#include "support/TestRunner.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> publicDeclarations(
    hitsimple::stdlib::StandardHeader header) {
  std::vector<std::string> declarations;
  for (const auto &spec : hitsimple::stdlib::builtinSpecs()) {
    if (spec.visibility == hitsimple::stdlib::BuiltinVisibility::Public &&
        spec.header == header) {
      declarations.emplace_back(spec.headerDeclaration);
    }
  }
  std::sort(declarations.begin(), declarations.end());
  return declarations;
}

std::vector<std::string>
generatedDeclarations(const std::filesystem::path &headerPath) {
  std::ifstream input(headerPath);
  std::vector<std::string> declarations;
  std::string line;
  while (std::getline(input, line)) {
    if (line.rfind("extern ", 0U) == 0U) {
      declarations.push_back(line);
    }
  }
  std::sort(declarations.begin(), declarations.end());
  return declarations;
}

} // namespace

HS_TEST(StandardLibraryManifest_CoversEveryBuiltinWithTypedContract) {
  const auto specs = hitsimple::stdlib::builtinSpecs();
  HS_EXPECT_EQ(specs.size(),
               static_cast<std::size_t>(hitsimple::stdlib::BuiltinId::Count) -
                   1U);

  for (std::size_t index = 1U;
       index < static_cast<std::size_t>(hitsimple::stdlib::BuiltinId::Count);
       ++index) {
    const auto id = static_cast<hitsimple::stdlib::BuiltinId>(index);
    const auto *spec = hitsimple::stdlib::findBuiltin(id);
    HS_EXPECT_TRUE(spec != nullptr);
    HS_EXPECT_EQ(spec->visibility, hitsimple::stdlib::BuiltinVisibility::Public);
    HS_EXPECT_TRUE(!spec->name.empty());
    HS_EXPECT_TRUE(!spec->standardSection.empty());
    HS_EXPECT_TRUE(!spec->headerDeclaration.empty());
    HS_EXPECT_TRUE(!spec->overloads.empty());
    HS_EXPECT_TRUE(spec->provider != hitsimple::stdlib::BuiltinProvider::None);
    HS_EXPECT_TRUE(!spec->testOwners.empty());
    for (const auto owner : spec->testOwners) {
      HS_EXPECT_TRUE(owner.starts_with("tests/"));
    }
    HS_EXPECT_TRUE(!spec->implementationSymbol.empty() ||
                   !spec->sourceModule.empty());
    HS_EXPECT_TRUE(!spec->staticDiagnostics.empty());
  }
}

HS_TEST(StandardLibraryManifest_GeneratedHeadersMatchPublicRegistry) {
  const auto root = hitsimple::support::standardLibraryRoot();
  for (const auto header : hitsimple::stdlib::allStandardHeaders()) {
    const auto path = root / std::string(hitsimple::stdlib::headerName(header));
    HS_EXPECT_TRUE(std::filesystem::exists(path));

    std::ifstream input(path);
    const std::string source((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    HS_EXPECT_TRUE(source.find("Generated from StandardLibraryManifest.json") !=
                   std::string::npos);
    HS_EXPECT_TRUE(source.find(std::string(hitsimple::stdlib::headerGuard(header))) !=
                   std::string::npos);
    HS_EXPECT_EQ(generatedDeclarations(path), publicDeclarations(header));
  }
}

HS_TEST(StandardLibraryManifest_RegistersCoreHsSourceModules) {
  const auto modules = hitsimple::stdlib::sourceModuleSpecs();
  HS_EXPECT_EQ(modules.size(), 1U);
  HS_EXPECT_EQ(modules.front().id, std::string_view("Ctype"));
  HS_EXPECT_EQ(modules.front().file, std::string_view("ctype.hs"));
  HS_EXPECT_TRUE(modules.front().dependencies.empty());
  HS_EXPECT_TRUE(std::filesystem::is_regular_file(
      hitsimple::support::standardLibraryRoot() /
      std::string(modules.front().file)));

  for (const auto id : {hitsimple::stdlib::BuiltinId::IsDigit,
                        hitsimple::stdlib::BuiltinId::IsAlpha,
                        hitsimple::stdlib::BuiltinId::IsAlnum,
                        hitsimple::stdlib::BuiltinId::IsSpace,
                        hitsimple::stdlib::BuiltinId::ToUpper,
                        hitsimple::stdlib::BuiltinId::ToLower}) {
    const auto* spec = hitsimple::stdlib::findBuiltin(id);
    HS_EXPECT_TRUE(spec != nullptr);
    HS_EXPECT_EQ(spec->provider, hitsimple::stdlib::BuiltinProvider::Intrinsic);
    HS_EXPECT_EQ(spec->referenceProvider,
                 hitsimple::stdlib::BuiltinProvider::CoreHs);
    HS_EXPECT_EQ(spec->sourceModule, std::string_view("Ctype"));
    HS_EXPECT_TRUE(hitsimple::stdlib::isStandardLibraryImplementationSymbol(
        spec->implementationSymbol));
  }
  HS_EXPECT_TRUE(!hitsimple::stdlib::isStandardLibraryImplementationSymbol(
      "not_a_standard_library_symbol"));
}
