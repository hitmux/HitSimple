#pragma once

#include "driver/DriverOptions.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hitsimple {
namespace support {
class CompilationMetrics;
struct ClangSelection;
struct LlvmArSelection;
} // namespace support
namespace codegen {
struct CodegenOptions;
}
} // namespace hitsimple

namespace hitsimple::driver {

std::string targetTriple();
bool emitObjectWithClang(std::string_view llvmIr,
                         const std::filesystem::path& llvmIrPath,
                         const std::filesystem::path& objectPath,
                         const hitsimple::support::ClangSelection& clang,
                         const NativeBackendOptions& backendOptions);
std::string defaultObjectOutputPath(const std::string& inputPath);
int compileObject(const std::vector<std::string>& inputPaths,
                  const std::optional<std::string>& outputPath,
                  hitsimple::codegen::CodegenOptions codegenOptions,
                  bool cCompatibilityMode,
                  hitsimple::stdlib::BuiltinProviderSelection providerSelection,
                  const hitsimple::support::ClangSelection& clang,
                  const NativeBackendOptions& backendOptions,
                  hitsimple::support::CompilationMetrics& metrics);
int compileStaticLibrary(
    const std::vector<std::string>& inputPaths,
    const std::optional<std::string>& outputPath,
    hitsimple::codegen::CodegenOptions codegenOptions, bool cCompatibilityMode,
    hitsimple::stdlib::BuiltinProviderSelection providerSelection,
    const hitsimple::support::LlvmArSelection& llvmAr,
    const hitsimple::support::ClangSelection& clang,
    const NativeBackendOptions& backendOptions,
    hitsimple::support::CompilationMetrics& metrics);
int compileExecutable(const std::vector<std::string>& inputPaths,
                      const std::optional<std::string>& outputPath,
                      hitsimple::codegen::CodegenOptions codegenOptions,
                      bool cCompatibilityMode,
                      hitsimple::stdlib::BuiltinProviderSelection providerSelection,
                      const hitsimple::support::ClangSelection& clang,
                      const NativeBackendOptions& backendOptions,
                      hitsimple::support::CompilationMetrics& metrics);
int compileMixedExecutable(
    const std::vector<std::string>& inputPaths,
    const std::optional<std::string>& outputPath,
    hitsimple::codegen::CodegenOptions codegenOptions, bool cCompatibilityMode,
    hitsimple::stdlib::BuiltinProviderSelection providerSelection,
    const std::optional<std::filesystem::path>& clangOverride,
    const std::optional<std::filesystem::path>& clangxxOverride,
    const ExternalBuildInputs& externalInputs,
    const NativeBackendOptions& backendOptions,
    hitsimple::support::CompilationMetrics& metrics);

} // namespace hitsimple::driver
