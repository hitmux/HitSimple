#include "support/TestRunner.h"

#include "hitsimple/codegen/LlvmCompatibility.h"
#include "hitsimple/codegen/NativeTarget.h"
#include "hitsimple/codegen/ObjectEmitter.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Process.h>
#include <llvm/TargetParser/Host.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace {

class TemporaryObjectFile final {
public:
  TemporaryObjectFile()
      : path_(std::filesystem::temp_directory_path() /
              ("hitsimple-object-emitter-" +
               std::to_string(llvm::sys::Process::getProcessId()) + ".o")) {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  ~TemporaryObjectFile() {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

struct ObjectEmitterFixture final {
  std::unique_ptr<llvm::LLVMContext> context;
  std::unique_ptr<llvm::Module> module;
  hitsimple::codegen::NativeTarget target;
};

std::optional<ObjectEmitterFixture> makeFixture(bool invalid,
                                                std::string &error) {
  auto context = std::make_unique<llvm::LLVMContext>();
  auto module = std::make_unique<llvm::Module>("object-emitter-test", *context);
  const auto triple = llvm::sys::getDefaultTargetTriple();
  hitsimple::codegen::setModuleTargetTriple(*module, triple);

  hitsimple::codegen::NativeTargetOptions targetOptions;
  targetOptions.triple = triple;
  auto target = hitsimple::codegen::createNativeTarget(targetOptions, error);
  if (!target) {
    return std::nullopt;
  }
  module->setDataLayout(target->machine->createDataLayout());

  llvm::IRBuilder<> builder(*context);
  auto *functionType = llvm::FunctionType::get(builder.getInt32Ty(), false);
  auto *function = llvm::Function::Create(
      functionType, llvm::GlobalValue::ExternalLinkage, "main", *module);
  auto *entry = llvm::BasicBlock::Create(*context, "entry", function);
  auto *exit = llvm::BasicBlock::Create(*context, "exit", function);
  builder.SetInsertPoint(entry);
  builder.CreateBr(exit);
  builder.SetInsertPoint(exit);
  if (invalid) {
    auto *value = builder.CreatePHI(builder.getInt32Ty(), 1, "value");
    value->addIncoming(builder.getInt32(1), exit);
    builder.CreateRet(value);
  } else {
    builder.CreateRet(builder.getInt32(0));
  }

  return ObjectEmitterFixture{std::move(context), std::move(module),
                              std::move(*target)};
}

} // namespace

HS_TEST(ObjectEmitter_EmitsParsableNativeObject) {
  std::string error;
  auto fixture = makeFixture(false, error);
  HS_EXPECT_TRUE(fixture.has_value());
  HS_EXPECT_TRUE(error.empty());

  TemporaryObjectFile output;
  hitsimple::codegen::ObjectEmissionOptions options;
  HS_EXPECT_TRUE(hitsimple::codegen::emitObjectFile(
      *fixture->module, *fixture->target.machine, output.path(), options,
      error));
  HS_EXPECT_TRUE(error.empty());
  HS_EXPECT_TRUE(std::filesystem::is_regular_file(output.path()));
  HS_EXPECT_TRUE(std::filesystem::file_size(output.path()) > 0U);

  auto objectBuffer = llvm::MemoryBuffer::getFile(output.path().string());
  HS_EXPECT_TRUE(static_cast<bool>(objectBuffer));
  if (!objectBuffer) {
    return;
  }
  auto object = llvm::object::ObjectFile::createObjectFile(
      objectBuffer.get()->getMemBufferRef());
  const bool parsed = static_cast<bool>(object);
  if (!parsed) {
    llvm::consumeError(object.takeError());
    HS_EXPECT_TRUE(false);
    return;
  }

  bool containsMain = false;
  for (const auto &symbol : (*object)->symbols()) {
    auto name = symbol.getName();
    if (!name) {
      llvm::consumeError(name.takeError());
      continue;
    }
    containsMain = containsMain || *name == "main" || *name == "_main";
  }
  HS_EXPECT_TRUE(containsMain);
}

HS_TEST(ObjectEmitter_RejectsInvalidModuleBeforeOpeningOutput) {
  std::string error;
  auto fixture = makeFixture(true, error);
  HS_EXPECT_TRUE(fixture.has_value());
  HS_EXPECT_TRUE(error.empty());

  TemporaryObjectFile output;
  hitsimple::codegen::ObjectEmissionOptions options;
  const auto succeeded = hitsimple::codegen::emitObjectFile(
      *fixture->module, *fixture->target.machine, output.path(), options,
      error);

  HS_EXPECT_TRUE(!succeeded);
  HS_EXPECT_TRUE(error.find("LLVM verifier failed before object emission") !=
                 std::string::npos);
  HS_EXPECT_TRUE(!std::filesystem::exists(output.path()));
}

HS_TEST(ObjectEmitter_ReportsOutputOpenFailure) {
  std::string error;
  auto fixture = makeFixture(false, error);
  HS_EXPECT_TRUE(fixture.has_value());
  HS_EXPECT_TRUE(error.empty());

  const auto outputPath = std::filesystem::temp_directory_path() /
                          ("hitsimple-object-emitter-missing-" +
                           std::to_string(llvm::sys::Process::getProcessId())) /
                          "output.o";
  std::error_code filesystemError;
  std::filesystem::remove_all(outputPath.parent_path(), filesystemError);

  hitsimple::codegen::ObjectEmissionOptions options;
  const auto succeeded = hitsimple::codegen::emitObjectFile(
      *fixture->module, *fixture->target.machine, outputPath, options, error);

  HS_EXPECT_TRUE(!succeeded);
  HS_EXPECT_TRUE(error.find("cannot open object output") != std::string::npos);
}
