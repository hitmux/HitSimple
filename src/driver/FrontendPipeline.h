#pragma once

#include "driver/DriverOptions.h"
#include "hitsimple/compat/CCompat.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hitsimple {
namespace support {
class CompilationMetrics;
struct TranslationUnitMetrics;
struct ClangSelection;
} // namespace support
namespace codegen {
struct CodegenOptions;
}
} // namespace hitsimple

namespace hitsimple::driver {

struct CompiledTranslationUnit final {
  std::string inputPath;
  std::string llvmIr;
  std::size_t mainDefinitionCount = 0;
  std::vector<hitsimple::compat::LinkageMetadata> compatibilityLinkage;
  std::vector<std::string> sourceModules;
};

struct CompiledObjectTranslationUnit final {
  std::size_t mainDefinitionCount = 0;
  std::vector<hitsimple::compat::LinkageMetadata> compatibilityLinkage;
  std::vector<std::string> sourceModules;
};

bool validateCCompatibilityExternalAbi(
    const std::vector<CompiledTranslationUnit>& units);
std::optional<CompiledTranslationUnit> compileTranslationUnit(
    const std::string& inputPath, hitsimple::codegen::CodegenOptions codegenOptions,
    bool cCompatibilityMode,
    hitsimple::stdlib::BuiltinProviderSelection providerSelection,
    bool internalStandardModule, hitsimple::support::CompilationMetrics& metrics);
std::optional<std::vector<CompiledTranslationUnit>> compileSourceModules(
    const std::vector<std::string>& moduleIds,
    hitsimple::codegen::CodegenOptions codegenOptions,
    hitsimple::support::CompilationMetrics& metrics);
std::vector<std::string> collectRequiredSourceModules(
    const std::vector<CompiledTranslationUnit>& units);
int dumpTokens(const std::string& inputPath);
int dumpAst(const std::string& inputPath, bool cCompatibilityMode);
int dumpHir(const std::string& inputPath, bool cCompatibilityMode,
            hitsimple::stdlib::BuiltinProviderSelection providerSelection);
int emitLlvm(const std::string& inputPath,
             const std::optional<std::string>& outputPath,
             hitsimple::codegen::CodegenOptions codegenOptions,
             bool cCompatibilityMode,
             hitsimple::stdlib::BuiltinProviderSelection providerSelection,
             hitsimple::support::CompilationMetrics& metrics);
int preprocessOnly(const std::string& inputPath,
                   const std::optional<std::string>& outputPath);
std::optional<CompiledObjectTranslationUnit> compileObjectTranslationUnit(
    const std::string& inputPath, const std::filesystem::path& outputPath,
    const std::filesystem::path& llvmIrPath,
    hitsimple::codegen::CodegenOptions codegenOptions,
    bool cCompatibilityMode,
    hitsimple::stdlib::BuiltinProviderSelection providerSelection,
    bool includeSourceModules, const hitsimple::support::ClangSelection& clang,
    const NativeBackendOptions& backendOptions,
    hitsimple::support::CompilationMetrics& metrics);

} // namespace hitsimple::driver
