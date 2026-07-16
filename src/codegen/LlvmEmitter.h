#pragma once

#include "hitsimple/codegen/LLVMCodegen.h"
#include "hitsimple/hir/HIR.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

namespace hitsimple::codegen {

struct Local {
  llvm::Value *storage = nullptr;
  llvm::Type *storageType = nullptr;
  std::size_t byteLength = 0;
  std::optional<hir::AbiType> abiType;
};

struct LoopTargets {
  llvm::BasicBlock *breakBlock = nullptr;
  llvm::BasicBlock *continueBlock = nullptr;
};

struct CatchTarget {
  llvm::BasicBlock *catchBlock = nullptr;
  llvm::Type *errorStorageType = nullptr;
  llvm::Value *errorStorage = nullptr;
  std::size_t errorByteLength = 0;
};

struct RuntimeObject {
  llvm::Value *storage = nullptr;
  llvm::Type *storageType = nullptr;
  std::size_t byteLength = 0;
};

struct ViewValue {
  llvm::Value *data = nullptr;
  llvm::Value *length = nullptr;
  std::optional<std::size_t> staticLength;
};

class LlvmEmitter {
public:
  LlvmEmitter(std::string moduleName, CodegenOptions options);

  EmitResult emit(const hir::TranslationUnit &unit);

private:
  struct StaticAddressRange {
    std::int64_t lowerBound = 0;
    std::int64_t offset = 0;
    std::uint64_t upperBound = 0;
  };

  struct CAbiMemoryPlan {
    std::optional<hir::AbiType> indirectReturn;
    std::vector<std::optional<hir::AbiType>> indirectParameters;
  };

  struct CAbiDirectPiece {
    llvm::Type *type = nullptr;
    std::size_t offset = 0;
  };

  struct CAbiDirectPlan {
    llvm::Type *physicalType = nullptr;
    std::vector<CAbiDirectPiece> pieces;
  };

  void emit(const hir::GlobalMemory &global);
  void declare(const hir::ExternFunction &function);
  llvm::Function *declare(const hir::Function &function);
  void emitGlobalInit(const hir::Block *block);
  void emit(const hir::Function &function);
  void emit(const hir::Block &block);
  void emit(const hir::Stmt &statement);
  void emit(const hir::LocalMemory &local);
  void emit(const hir::IntegerStore &store);
  void emit(const hir::FloatStore &store);
  void emit(const hir::StringStore &store);
  void emit(const hir::StringCopyStore &store);
  void emit(const hir::BoolStore &store);
  void emit(const hir::PointerStore &store);
  void emit(const hir::Call &call);
  void emit(const hir::UserTemplateOpCall &call);
  void emit(const hir::UserTemplateFormatCall &call);
  void emit(const hir::MultiReturnCallStore &call);
  void emit(const hir::InputCallStore &call);
  void emit(const hir::Return &ret);
  void emit(const hir::If &ifStmt);
  void emit(const hir::While &whileStmt);
  void emit(const hir::For &forStmt);
  void emit(const hir::Goto &gotoStmt);
  void emit(const hir::Label &label);
  void emit(const hir::Throw &throwStmt);
  void emit(const hir::TryCatch &tryCatch);
  void emitBreak();
  void emitContinue();
  void collectLabels(const hir::Block &block, llvm::Function &function);
  void collectLabels(const hir::Stmt &statement, llvm::Function &function);
  void validateSafety(const hir::TranslationUnit &unit);
  void validateSafety(const hir::Block &block);
  void validateSafety(const hir::Stmt &statement);
  void validateSafety(const hir::Expr &expression);
  std::optional<StaticAddressRange>
  staticAddressRange(const hir::Expr &expression) const;
  bool targetsRegisteredInternalObject(const hir::Expr &expression) const;
  bool hasKnownStaticAddressRange(const hir::Expr &expression,
                                  std::size_t byteLength) const;

  llvm::Value *emitConditionValue(const hir::Expr &expression);
  llvm::Value *emitIntegerValue(const hir::Expr &expression,
                                std::size_t byteLength);
  llvm::Value *emitIntegerValue(const hir::Expr &expression,
                                std::size_t byteLength,
                                bool unsignedInterpretation);
  llvm::Value *emitPointerValue(const hir::Expr &expression,
                                std::string_view name);
  llvm::Value *emitFloatValue(const hir::Expr &expression,
                              std::size_t byteLength);
  llvm::Value *convertFloatValue(llvm::Value *value,
                                 std::size_t sourceByteLength,
                                 std::size_t targetByteLength);
  llvm::Value *emitF128ResultCall(std::string_view symbol,
                                 std::vector<llvm::Value *> arguments,
                                 const std::vector<bool> &f128Arguments,
                                 std::string_view name);
  llvm::Value *emitF128ScalarCall(std::string_view symbol,
                                 llvm::Type *returnType,
                                 std::vector<llvm::Value *> arguments,
                                 const std::vector<bool> &f128Arguments,
                                 std::string_view name);
  bool usesSoftwareF128() const;
  bool isF128ValueType(llvm::Type *type) const;
  ViewValue emitViewValue(const hir::Expr &expression);
  ViewValue emitUserTemplateOpCall(
      std::string_view callee,
      const std::vector<std::unique_ptr<hir::Expr>> &arguments,
      std::size_t resultByteLength);
  ViewValue emitUserTemplateFormatCall(std::string_view callee,
                                       const hir::Expr &value,
                                       hir::FormatOutputSink sink,
                                       const hir::Expr *file,
                                       std::size_t resultByteLength);
  llvm::Value *emitFormatOutput(
      const std::vector<std::unique_ptr<hir::Expr>> &arguments,
      const std::vector<hir::FormatArgKind> &formatArgumentKinds,
      stdlib::BuiltinId builtin, std::string_view calleeName);
  llvm::Value *emitCallValue(const hir::CallExpr &call);
  llvm::Value *emitValueForType(const hir::Expr &expression,
                                llvm::Type *type,
                                std::string_view name);
  llvm::IntegerType *integerTypeForByteLength(std::size_t byteLength);
  llvm::Type *floatTypeForByteLength(std::size_t byteLength);
  llvm::Type *abiTypeFor(const hir::AbiType &type);
  llvm::Type *cAbiPhysicalTypeFor(const hir::AbiType &type);
  std::optional<CAbiDirectPlan>
  cAbiDirectPlanFor(const hir::AbiType &type);
  void rememberCAbiDirectAggregatePlans(
      std::string_view name,
      const std::optional<hir::FunctionAbiSignature> &signature);
  llvm::Value *packCAbiDirectValue(llvm::Value *storage,
                                   llvm::Type *storageType,
                                   const hir::AbiType &logicalType,
                                   const CAbiDirectPlan &plan,
                                   std::string_view name);
  bool unpackCAbiDirectValue(llvm::Value *value, llvm::Value *storage,
                             llvm::Type *storageType,
                             const hir::AbiType &logicalType,
                             const CAbiDirectPlan &plan,
                             std::string_view name);
  std::optional<CAbiMemoryPlan>
  cAbiMemoryPlanFor(const hir::FunctionAbiSignature &signature);
  llvm::FunctionType *cAbiFunctionType(
      const std::optional<hir::FunctionAbiSignature> &signature,
      const std::vector<std::size_t> &parameterByteLengths,
      const std::vector<std::size_t> &returnByteLengths,
      const CAbiMemoryPlan *memoryPlan);
  void applyCAbiMemoryAttributes(llvm::Function &function,
                                 const CAbiMemoryPlan &plan);
  void applyCAbiMemoryAttributes(llvm::CallBase &call,
                                 const CAbiMemoryPlan &plan);
  const CAbiMemoryPlan *cAbiMemoryPlan(std::string_view name) const;
  llvm::Value *emitCAbiMemoryCall(
                                  std::string_view name,
                                  const std::vector<std::unique_ptr<hir::Expr>> &arguments,
                                  llvm::Function &callee,
                                  const CAbiMemoryPlan &plan);
  llvm::Value *emitCAbiDirectCall(
      std::string_view name,
      const std::vector<std::unique_ptr<hir::Expr>> &arguments,
      llvm::Function &callee);
  llvm::Type *functionReturnType(
      const std::vector<std::size_t> &byteLengths,
      const std::optional<hir::FunctionAbiSignature> &abiSignature =
          std::nullopt);
  llvm::Value *firstBytePointer(llvm::Type *storageType, llvm::Value *storage);
  llvm::Value *bytePointer(llvm::Type *storageType, llvm::Value *storage,
                           std::size_t offset, std::string_view name);
  llvm::AllocaInst *createFunctionEntryAlloca(llvm::Type *storageType,
                                               std::string_view name);
  void registerLocalObject(llvm::Value *storage, std::size_t byteLength);
  void registerStaticObject(llvm::Value *storage, std::size_t byteLength);
  void emitRuntimeFrameEnter();
  void emitRuntimeFrameExit();
  void emitEffectContractEnter(const hir::Function &function);
  void emitEffectContractExit();
  void emitEffectRead(llvm::Value *pointer, llvm::Value *byteLength);
  void emitEffectWrite(llvm::Value *pointer, llvm::Value *byteLength);
  void emitEffectEvent(std::uint32_t event);
  llvm::Value *effectObjectPointer(const hir::Function &function,
                                   std::string_view object);
  llvm::Value *effectRangeLength(const hir::Function &function,
                                 const hir::EffectRange &range);
  llvm::FunctionCallee declarePrintf();
  llvm::FunctionCallee declareMalloc();
  llvm::FunctionCallee declareCalloc();
  llvm::FunctionCallee declareRealloc();
  llvm::FunctionCallee declareFree();
  llvm::FunctionCallee declareCheckedAlloc();
  llvm::FunctionCallee declareCheckedCalloc();
  llvm::FunctionCallee declareCheckedRealloc();
  llvm::FunctionCallee declareCheckedFree();
  llvm::FunctionCallee declareCheckLoad();
  llvm::FunctionCallee declareCheckStore();
  llvm::FunctionCallee declareCheckViewLength();
  llvm::FunctionCallee declareReverseBytes();
  llvm::FunctionCallee declareRegisterLocalObject();
  llvm::FunctionCallee declareRegisterStaticObject();
  llvm::FunctionCallee declareRuntimeFrameEnter();
  llvm::FunctionCallee declareRuntimeFrameExit();
  llvm::FunctionCallee declareEffectContractEnter();
  llvm::FunctionCallee declareEffectContractExit();
  llvm::FunctionCallee declareEffectContractAddRange();
  llvm::FunctionCallee declareEffectNoAliasCheck();
  llvm::FunctionCallee declareEffectReadCheck();
  llvm::FunctionCallee declareEffectWriteCheck();
  llvm::FunctionCallee declareEffectEventCheck();
  llvm::FunctionCallee declareCFunction(std::string_view name,
                                        llvm::Type *returnType,
                                        std::vector<llvm::Type *> parameters,
                                        bool variadic = false);
  llvm::Value *emitStdoutFile();
  static std::string decodeStringLiteral(std::string_view literal);

  void addDiagnostic(std::string diagnostic);
  bool hasStaticSafetyChecks() const;
  bool hasRuntimeSafetyChecks() const;

  std::string moduleName_;
  CodegenOptions options_;
  llvm::LLVMContext context_;
  std::unique_ptr<llvm::Module> module_;
  llvm::IRBuilder<> builder_;
  std::unordered_map<std::string, Local> locals_;
  std::unordered_map<std::string, Local> globals_;
  std::unordered_map<std::string, std::optional<std::int64_t>>
      staticIntegerValues_;
  std::vector<LoopTargets> loopTargets_;
  std::vector<CatchTarget> catchTargets_;
  std::unordered_map<std::string, llvm::BasicBlock *> labelBlocks_;
  std::unordered_map<std::string, CAbiMemoryPlan> cAbiMemoryPlans_;
  std::unordered_map<std::string,
                     std::vector<std::optional<hir::AbiType>>>
      cAbiDirectAggregateParameters_;
  std::unordered_map<std::string, hir::AbiType> cAbiDirectAggregateReturns_;
  std::vector<RuntimeObject> internalGlobals_;
  llvm::BasicBlock *functionEntryBlock_ = nullptr;
  llvm::Value *cAbiSRetStorage_ = nullptr;
  llvm::Type *cAbiSRetStorageType_ = nullptr;
  llvm::Value *viewAbiResultStorage_ = nullptr;
  std::size_t viewAbiResultByteLength_ = 0;
  bool runtimeFrameActive_ = false;
  bool effectContractActive_ = false;
  std::vector<diagnostic::Diagnostic> diagnostics_;
};

} // namespace hitsimple::codegen
