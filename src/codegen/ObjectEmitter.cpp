#include "hitsimple/codegen/ObjectEmitter.h"

#include "hitsimple/support/Path.h"

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>

#include <string>

namespace hitsimple::codegen {

bool emitObjectFile(llvm::Module &module, llvm::TargetMachine &targetMachine,
                    const std::filesystem::path &outputPath,
                    const ObjectEmissionOptions &options, std::string &error) {
  error.clear();
  if (options.verifyModule) {
    std::string verifierOutput;
    llvm::raw_string_ostream verifierStream(verifierOutput);
    if (llvm::verifyModule(module, &verifierStream)) {
      verifierStream.flush();
      error = "LLVM verifier failed before object emission:\n" + verifierOutput;
      return false;
    }
  }

  std::error_code outputError;
  const auto outputPathText = support::pathToUtf8(outputPath);
  llvm::raw_fd_ostream output(outputPathText, outputError,
                              llvm::sys::fs::OF_None);
  if (outputError) {
    error = "cannot open object output '" + outputPathText +
            "': " + outputError.message();
    return false;
  }

  llvm::legacy::PassManager codegenPasses;
  if (targetMachine.addPassesToEmitFile(codegenPasses, output, nullptr,
                                        llvm::CodeGenFileType::ObjectFile)) {
    error = "target does not support object emission";
    return false;
  }

  codegenPasses.run(module);
  output.flush();
  if (output.has_error()) {
    error = "failed while writing object output '" + outputPathText + "'";
    return false;
  }
  return true;
}

} // namespace hitsimple::codegen
