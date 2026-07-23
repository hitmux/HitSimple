#pragma once

#include <filesystem>
#include <string>

namespace llvm {
class Module;
class TargetMachine;
} // namespace llvm

namespace hitsimple::codegen {

struct ObjectEmissionOptions final {
  bool verifyModule = true;
};

bool emitObjectFile(llvm::Module &module, llvm::TargetMachine &targetMachine,
                    const std::filesystem::path &outputPath,
                    const ObjectEmissionOptions &options, std::string &error);

} // namespace hitsimple::codegen
