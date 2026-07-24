#pragma once

#include "driver/DriverOptions.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
class Module;
}

namespace hitsimple {
namespace support {
class CompilationMetrics;
struct LlvmArSelection;
} // namespace support
namespace codegen {
struct CodegenOptions;
struct ModuleEmitResult;
}
} // namespace hitsimple

namespace hitsimple::driver {

std::string targetTriple();
bool emitOptimizedObject(hitsimple::codegen::ModuleEmitResult& emission,
                         const std::filesystem::path& objectPath,
                         const NativeBackendOptions& backendOptions,
                         hitsimple::support::CompilationMetrics& metrics);
std::string defaultObjectOutputPath(const std::string& inputPath);
int compileObject(const std::vector<std::string>& inputPaths,
                  const std::optional<std::string>& outputPath,
                  hitsimple::codegen::CodegenOptions codegenOptions,
                  bool cCompatibilityMode,
                  hitsimple::stdlib::BuiltinProviderSelection providerSelection,
                  const NativeBackendOptions& backendOptions,
                  hitsimple::support::CompilationMetrics& metrics);
int compileStaticLibrary(
    const std::vector<std::string>& inputPaths,
    const std::optional<std::string>& outputPath,
    hitsimple::codegen::CodegenOptions codegenOptions, bool cCompatibilityMode,
    hitsimple::stdlib::BuiltinProviderSelection providerSelection,
    const hitsimple::support::LlvmArSelection& llvmAr,
    const NativeBackendOptions& backendOptions,
    hitsimple::support::CompilationMetrics& metrics);
int compileExecutable(const std::vector<std::string>& inputPaths,
                      const std::optional<std::string>& outputPath,
                      hitsimple::codegen::CodegenOptions codegenOptions,
                      bool cCompatibilityMode,
                      hitsimple::stdlib::BuiltinProviderSelection providerSelection,
                      const std::optional<std::filesystem::path>& clangOverride,
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
