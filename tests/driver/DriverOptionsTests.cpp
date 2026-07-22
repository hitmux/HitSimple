#include "support/TestRunner.h"

#include "driver/DriverOptions.h"

#include <string>
#include <vector>

namespace {

hitsimple::driver::DriverOptionsParseResult parse(
    std::vector<std::string> arguments) {
  return hitsimple::driver::parseDriverOptions(arguments);
}

} // namespace

HS_TEST(DriverOptions_UsesDefaultsAndCollectsInputPaths) {
  const auto defaults = parse({"hsc"});
  HS_EXPECT_TRUE(defaults.ok());
  HS_EXPECT_TRUE(defaults.options->inputPaths.empty());
  HS_EXPECT_TRUE(defaults.options->crateType ==
                 hitsimple::driver::CrateType::Bin);
  HS_EXPECT_TRUE(defaults.options->backendOptions.optimization ==
                 hitsimple::driver::OptimizationLevel::O2);
  HS_EXPECT_TRUE(defaults.options->backendOptions.pgoMode ==
                 hitsimple::driver::PgoMode::None);
  HS_EXPECT_TRUE(defaults.options->backendOptions.sanitizer ==
                 hitsimple::driver::Sanitizer::None);

  const auto parsed = parse({"hsc", "first.hs", "second.hs"});
  HS_EXPECT_TRUE(parsed.ok());
  HS_EXPECT_EQ(parsed.options->inputPaths.size(), 2U);
  HS_EXPECT_EQ(parsed.options->inputPaths[0], "first.hs");
  HS_EXPECT_EQ(parsed.options->inputPaths[1], "second.hs");
}

HS_TEST(DriverOptions_ParsesRuntimeSanitizer) {
  const auto parsed = parse(
      {"hsc", "--sanitize=address", "--sanitize=undefined", "unit.hs"});
  HS_EXPECT_TRUE(!parsed.ok());
  HS_EXPECT_EQ(parsed.error, "--sanitize may only be specified once");

  const auto address = parse({"hsc", "--sanitize=address", "unit.hs"});
  HS_EXPECT_TRUE(address.ok());
  HS_EXPECT_TRUE(address.options->backendOptions.sanitizer ==
                 hitsimple::driver::Sanitizer::Address);

  const auto undefined = parse({"hsc", "--sanitize=undefined", "unit.hs"});
  HS_EXPECT_TRUE(undefined.ok());
  HS_EXPECT_TRUE(undefined.options->backendOptions.sanitizer ==
                 hitsimple::driver::Sanitizer::Undefined);
}

HS_TEST(DriverOptions_ParsesActionsAndCodegenOptions) {
  const auto parsed = parse(
      {"hsc", "--dump-hir", "--c-compat", "--stdlib-provider=reference",
       "--static-checked", "-O1", "-O3", "--timing",
       "--diagnostic-format=json", "unit.c"});
  HS_EXPECT_TRUE(parsed.ok());
  HS_EXPECT_TRUE(parsed.options->shouldDumpHir);
  HS_EXPECT_TRUE(parsed.options->cCompatibilityMode);
  HS_EXPECT_TRUE(parsed.options->shouldPrintTiming);
  HS_EXPECT_TRUE(parsed.options->diagnosticOutputFormat ==
                 hitsimple::driver::DiagnosticOutputFormat::Json);
  HS_EXPECT_TRUE(parsed.options->providerSelection ==
                 hitsimple::stdlib::BuiltinProviderSelection::Reference);
  HS_EXPECT_TRUE(parsed.options->codegenOptions.safetyMode ==
                 hitsimple::codegen::SafetyMode::StaticChecked);
  HS_EXPECT_TRUE(parsed.options->backendOptions.optimization ==
                 hitsimple::driver::OptimizationLevel::O3);
}

HS_TEST(DriverOptions_ParsesNativeCargoAndPgoCombinations) {
  const auto parsed = parse(
      {"hsc", "--entry=native", "--c-source", "bridge.c", "--cxx-source",
       "bridge.cpp", "--clang", "clang-18", "--clangxx", "clang++-18",
       "--linker-language=cxx", "--cargo-manifest", "Cargo.toml",
       "--cargo-package", "engine", "--cargo-profile", "release",
       "--cargo-features", "fast,simd", "--cargo-no-default-features",
       "--pgo-instrument=run.profraw", "--timing-json=timing.json",
       "-o", "program", "main.hs"});
  HS_EXPECT_TRUE(parsed.ok());
  HS_EXPECT_TRUE(parsed.options->externalInputs.entryMode ==
                 hitsimple::driver::EntryMode::Native);
  HS_EXPECT_EQ(parsed.options->externalInputs.cSources.size(), 1U);
  HS_EXPECT_EQ(parsed.options->externalInputs.cxxSources.size(), 1U);
  HS_EXPECT_TRUE(parsed.options->externalInputs.linkerLanguage ==
                 hitsimple::driver::LinkerLanguage::Cxx);
  HS_EXPECT_TRUE(parsed.options->cargoManifest.has_value());
  HS_EXPECT_EQ(*parsed.options->cargoPackage, "engine");
  HS_EXPECT_TRUE(parsed.options->cargoNoDefaultFeatures);
  HS_EXPECT_TRUE(parsed.options->backendOptions.pgoMode ==
                 hitsimple::driver::PgoMode::Instrument);
  HS_EXPECT_EQ(parsed.options->outputPath.value(), "program");
}

HS_TEST(DriverOptions_ReportsFirstInvalidCombination) {
  const auto multipleActions = parse({"hsc", "--dump-ast", "--emit-llvm",
                                      "unit.hs"});
  HS_EXPECT_TRUE(!multipleActions.ok());
  HS_EXPECT_EQ(multipleActions.error,
               "multiple action options are not allowed: --dump-ast --emit-llvm");

  const auto missingValue = parse({"hsc", "--clang"});
  HS_EXPECT_TRUE(!missingValue.ok());
  HS_EXPECT_EQ(missingValue.error, "--clang requires an executable path");

  const auto invalidCrate = parse({"hsc", "--crate-type=shared", "unit.hs"});
  HS_EXPECT_TRUE(!invalidCrate.ok());
  HS_EXPECT_EQ(invalidCrate.error,
               "unsupported --crate-type 'shared'; expected bin, object, or staticlib");

  const auto pgoObject = parse({"hsc", "--pgo-instrument=run.profraw",
                                "--crate-type=object", "unit.hs"});
  HS_EXPECT_TRUE(!pgoObject.ok());
  HS_EXPECT_EQ(pgoObject.error,
               "PGO options are only supported for --crate-type=bin");

  const auto invalidSanitizer = parse({"hsc", "--sanitize=memory", "unit.hs"});
  HS_EXPECT_TRUE(!invalidSanitizer.ok());
  HS_EXPECT_EQ(invalidSanitizer.error,
               "unsupported --sanitize 'memory'; expected address or undefined");

  const auto sanitizerObject = parse(
      {"hsc", "--sanitize=address", "--crate-type=object", "unit.hs"});
  HS_EXPECT_TRUE(!sanitizerObject.ok());
  HS_EXPECT_EQ(sanitizerObject.error,
               "--sanitize is only supported for --crate-type=bin");

  const auto sanitizerAction = parse(
      {"hsc", "--sanitize=address", "--emit-llvm", "unit.hs"});
  HS_EXPECT_TRUE(!sanitizerAction.ok());
  HS_EXPECT_EQ(sanitizerAction.error,
               "--sanitize is only supported for executable builds");
}
