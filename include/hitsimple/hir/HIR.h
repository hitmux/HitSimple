#pragma once

#include "hitsimple/diagnostic/Diagnostic.h"
#include "hitsimple/stdlib/StandardLibrary.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hitsimple::hir {

struct Block;

enum class MemoryStorage {
  Global,
  Local,
  StaticLocal,
};

std::string_view toString(MemoryStorage storage);

// Linkage applies to top-level definitions.  Declarations are represented by
// GlobalMemory::isExtern and TranslationUnit::externFunctions and always use
// external linkage.
enum class Linkage {
  External,
  Internal,
};

std::string_view toString(Linkage linkage);

enum class LinkageTarget {
  Global,
  Function,
};

// Core HIR normally carries only byte lengths.  ABI overrides are deliberately
// opt-in metadata for compatibility lowering, where C distinguishes pointer
// and floating-point values that share an integer-sized byte length.
enum class AbiValueKind {
  Integer,
  Floating,
  Pointer,
  Aggregate,
};

std::string_view toString(AbiValueKind kind);

struct AbiType final {
  AbiType() = default;
  AbiType(AbiValueKind kind, std::size_t byteLength, bool isSigned,
          std::string aggregateName = {})
      : kind(kind), byteLength(byteLength), isSigned(isSigned),
        aggregateName(std::move(aggregateName)) {}

  AbiValueKind kind = AbiValueKind::Integer;
  // byteLength describes one value.  aggregate field arrays use elementCount
  // to retain both their element ABI and their full storage extent.
  std::size_t byteLength = 0;
  bool isSigned = true;
  std::string aggregateName;
  std::size_t alignment = 1;
  std::size_t elementCount = 1;
  std::vector<AbiType> aggregateFields;
  std::vector<std::size_t> aggregateFieldOffsets;
};

struct FunctionAbiSignature final {
  std::vector<AbiType> parameterTypes;
  std::vector<AbiType> returnTypes;
  // ABI overrides retain C compatibility semantics. Core typed signatures
  // explicitly opt out to preserve HitSimple's multi-return ABI.
  bool isCCompatibility = true;
};

// symbolName is the name used by LLVM code generation: a global binding name
// or a function name.  This keeps front-end compatibility metadata outside
// the core AST and semantic analyzer.
struct LinkageOverride final {
  std::string symbolName;
  LinkageTarget target = LinkageTarget::Global;
  Linkage linkage = Linkage::External;
};

// An ABI override applies after semantic analysis.  It never changes core
// source semantics; it only instructs LLVM codegen how to declare a C-facing
// object or function.  A function may be an extern declaration or a
// definition.  A global override is valid only for a definition/declaration
// represented by GlobalMemory.
struct AbiOverride final {
  std::string symbolName;
  LinkageTarget target = LinkageTarget::Global;
  std::optional<AbiType> objectType;
  std::optional<FunctionAbiSignature> functionSignature;
};

struct Expr {
  virtual ~Expr() = default;
};

struct IntegerLiteral final : Expr {
  explicit IntegerLiteral(std::string value);
  IntegerLiteral(std::string value, std::size_t byteLength);

  std::string value;
  std::size_t byteLength = 4;
};

struct StringLiteral final : Expr {
  explicit StringLiteral(std::string value);
  StringLiteral(std::string value, std::size_t byteLength);

  std::string value;
  std::size_t byteLength = 0;
};

struct FloatLiteral final : Expr {
  FloatLiteral(std::string value, std::size_t byteLength);

  std::string value;
  std::size_t byteLength = 8;
};

struct VariableRef final : Expr {
  VariableRef(std::string name, std::size_t byteLength);
  VariableRef(std::string name, std::string bindingName, std::size_t byteLength,
              MemoryStorage storage);
  VariableRef(std::string name, std::string bindingName, std::size_t byteLength,
              MemoryStorage storage, std::size_t offset);

  std::string name;
  std::string bindingName;
  std::size_t byteLength = 0;
  MemoryStorage storage = MemoryStorage::Local;
  std::size_t offset = 0;
};

struct AddressOfExpr final : Expr {
  AddressOfExpr(std::string name, std::string bindingName,
                std::size_t targetByteLength, MemoryStorage storage,
                std::size_t offset, std::size_t byteLength);

  std::string name;
  std::string bindingName;
  std::size_t targetByteLength = 0;
  MemoryStorage storage = MemoryStorage::Local;
  std::size_t offset = 0;
  std::size_t byteLength = 0;
};

struct DerefExpr final : Expr {
  DerefExpr(std::unique_ptr<Expr> address, std::size_t byteLength);

  std::unique_ptr<Expr> address;
  std::size_t byteLength = 0;
};

struct BinaryExpr final : Expr {
  BinaryExpr(std::unique_ptr<Expr> left, std::string op,
             std::unique_ptr<Expr> right, std::size_t byteLength);

  std::unique_ptr<Expr> left;
  std::string op;
  std::unique_ptr<Expr> right;
  std::size_t byteLength = 0;
};

struct UnaryExpr final : Expr {
  UnaryExpr(std::string op, std::unique_ptr<Expr> operand,
            std::size_t byteLength);

  std::string op;
  std::unique_ptr<Expr> operand;
  std::size_t byteLength = 0;
};

struct TernaryExpr final : Expr {
  TernaryExpr(std::unique_ptr<Expr> condition, std::unique_ptr<Expr> thenExpr,
              std::unique_ptr<Expr> elseExpr, std::size_t byteLength);

  std::unique_ptr<Expr> condition;
  std::unique_ptr<Expr> thenExpr;
  std::unique_ptr<Expr> elseExpr;
  std::size_t byteLength = 0;
};

struct UnsignedExpr final : Expr {
  UnsignedExpr(std::unique_ptr<Expr> operand, std::size_t byteLength);

  std::unique_ptr<Expr> operand;
  std::size_t byteLength = 0;
};

struct IntegerCastExpr final : Expr {
  IntegerCastExpr(std::unique_ptr<Expr> operand, std::size_t byteLength,
                  bool isSigned);

  std::unique_ptr<Expr> operand;
  std::size_t byteLength = 0;
  bool isSigned = true;
};

// A temporary interpretation of an existing View. It owns no storage and
// therefore must preserve the operand's bytes and addressability.
struct TemplateViewExpr final : Expr {
  TemplateViewExpr(std::unique_ptr<Expr> operand, std::size_t byteLength,
                   std::string templateName, bool isAddressable);

  std::unique_ptr<Expr> operand;
  std::size_t byteLength = 0;
  std::string templateName;
  bool isAddressable = false;
};

// A call to a user-defined `impl op`. The hidden callee uses the internal
// View ABI: the result and each operand are byte addresses, never C ABI
// aggregate values.
struct UserTemplateOpCallExpr final : Expr {
  UserTemplateOpCallExpr(std::string callee,
                         std::vector<std::unique_ptr<Expr>> arguments,
                         std::size_t byteLength, std::string templateName);

  std::string callee;
  std::vector<std::unique_ptr<Expr>> arguments;
  std::size_t byteLength = 0;
  std::string templateName;
};

struct FloatBinaryExpr final : Expr {
  FloatBinaryExpr(std::unique_ptr<Expr> left, std::string op,
                  std::unique_ptr<Expr> right, std::size_t byteLength);

  std::unique_ptr<Expr> left;
  std::string op;
  std::unique_ptr<Expr> right;
  std::size_t byteLength = 0;
};

struct FloatCompareExpr final : Expr {
  FloatCompareExpr(std::unique_ptr<Expr> left, std::string op,
                   std::unique_ptr<Expr> right,
                   std::size_t operandByteLength);

  std::unique_ptr<Expr> left;
  std::string op;
  std::unique_ptr<Expr> right;
  std::size_t operandByteLength = 0;
};

struct ToFloatExpr final : Expr {
  ToFloatExpr(std::unique_ptr<Expr> operand, std::size_t byteLength,
              bool sourceUnsigned = false, bool sourceIsFloating = false);

  std::unique_ptr<Expr> operand;
  std::size_t byteLength = 0;
  bool sourceUnsigned = false;
  bool sourceIsFloating = false;
};

struct ToIntExpr final : Expr {
  ToIntExpr(std::unique_ptr<Expr> operand, std::size_t floatByteLength,
            std::size_t byteLength, bool isUnsigned = false);

  std::unique_ptr<Expr> operand;
  std::size_t floatByteLength = 0;
  std::size_t byteLength = 0;
  bool isUnsigned = false;
};

// Native format calls retain this interpretation because a dynamic format
// string cannot otherwise recover it from a raw View at code generation time.
enum class FormatArgKind {
  Bytes,
  Float,
  String,
};

// A user `op format` receives the actual destination selected by its caller.
// File stores a HIR expression whose View bytes contain the native FILE*;
// stdout has no expression because codegen loads the process stdout handle.
enum class FormatOutputSink {
  Stdout,
  File,
};

std::string_view toString(FormatOutputSink sink);

// An expression form of a user `op format` call. The result is its fixed i32
// View; `sink` and `file` preserve the selected native output destination.
struct UserTemplateFormatCallExpr final : Expr {
  UserTemplateFormatCallExpr(std::string callee, std::unique_ptr<Expr> value,
                             FormatOutputSink sink, std::unique_ptr<Expr> file,
                             std::size_t byteLength);

  std::string callee;
  std::unique_ptr<Expr> value;
  FormatOutputSink sink = FormatOutputSink::Stdout;
  std::unique_ptr<Expr> file;
  std::size_t byteLength = 0;
};

struct CallExpr final : Expr {
  CallExpr(std::string callee, std::vector<std::unique_ptr<Expr>> arguments,
           std::size_t byteLength, bool isFloating = false,
           stdlib::BuiltinId builtin = stdlib::BuiltinId::None,
           std::vector<FormatArgKind> formatArgumentKinds = {});

  std::string callee;
  std::vector<std::unique_ptr<Expr>> arguments;
  std::size_t byteLength = 0;
  bool isFloating = false;
  stdlib::BuiltinId builtin = stdlib::BuiltinId::None;
  std::vector<FormatArgKind> formatArgumentKinds;
};

enum class DynamicByteViewOperation : std::uint8_t {
  ResizeBytes,
  ByteSwap,
};

std::string_view toString(DynamicByteViewOperation operation);

// A View whose byte length is only known after evaluating the expression.
// `runtimeLength` is present for resize_bytes; byte_swap inherits its source
// length at runtime.
struct DynamicByteViewExpr final : Expr {
  DynamicByteViewExpr(DynamicByteViewOperation operation,
                      std::unique_ptr<Expr> source,
                      std::unique_ptr<Expr> runtimeLength);

  DynamicByteViewOperation operation = DynamicByteViewOperation::ResizeBytes;
  std::unique_ptr<Expr> source;
  std::unique_ptr<Expr> runtimeLength;
};

// The non-intrinsic byte-order reversal path for fixed View lengths other
// than the native bswap widths.
struct ByteSwapExpr final : Expr {
  ByteSwapExpr(std::unique_ptr<Expr> source, std::size_t byteLength);

  std::unique_ptr<Expr> source;
  std::size_t byteLength = 0;
};

struct Stmt {
  virtual ~Stmt() = default;
};

struct AssignmentExpr final : Expr {
  AssignmentExpr(std::vector<std::unique_ptr<Stmt>> stores,
                 std::unique_ptr<Expr> result,
                 std::size_t byteLength);

  std::vector<std::unique_ptr<Stmt>> stores;
  std::unique_ptr<Expr> result;
  std::size_t byteLength = 0;
};

struct StatementList final : Stmt {
  explicit StatementList(std::vector<std::unique_ptr<Stmt>> statements);

  std::vector<std::unique_ptr<Stmt>> statements;
};

struct GlobalMemory final {
  GlobalMemory(std::string name, std::string bindingName,
               std::size_t byteLength);
  GlobalMemory(std::string name, std::string bindingName,
               std::size_t byteLength, bool isExtern);

  std::string name;
  std::string bindingName;
  std::size_t byteLength = 0;
  bool isExtern = false;
  Linkage linkage = Linkage::External;
  std::optional<AbiType> abiType;
};

struct StructMemberLayout final {
  StructMemberLayout(std::string name, std::size_t byteLength,
                     std::size_t offset);

  std::string name;
  std::size_t byteLength = 0;
  std::size_t offset = 0;
};

struct StructLayout final {
  StructLayout(std::string name, std::vector<StructMemberLayout> members,
               std::size_t byteLength);

  std::string name;
  std::vector<StructMemberLayout> members;
  std::size_t byteLength = 0;
};

struct ViewMember final {
  ViewMember(std::string name, std::size_t byteLength, std::size_t offset,
             std::string templateName);

  std::string name;
  std::size_t byteLength = 0;
  std::size_t offset = 0;
  std::string templateName;
};

struct ViewTemplate final {
  ViewTemplate(std::string name, std::vector<ViewMember> members,
               std::size_t byteLength);

  std::string name;
  std::vector<ViewMember> members;
  std::size_t byteLength = 0;
};

struct ImplOpParam final {
  ImplOpParam(std::string name, std::string templateName, bool isMutable);

  std::string name;
  std::string templateName;
  bool isMutable = false;
};

struct ImplOpBinding final {
  ImplOpBinding(std::string implTemplate, std::string op, std::string symbolName,
                std::vector<ImplOpParam> params,
                std::vector<std::size_t> returnByteLengths);

  std::string implTemplate;
  std::string op;
  std::string symbolName;
  std::vector<ImplOpParam> params;
  std::vector<std::size_t> returnByteLengths;
};

struct Parameter final {
  Parameter(std::string name, std::string bindingName, std::size_t byteLength);

  std::string name;
  std::string bindingName;
  std::size_t byteLength = 0;
};

struct ExternFunction final {
  ExternFunction(std::string name, std::vector<std::size_t> parameterByteLengths,
                 std::vector<std::size_t> returnByteLengths);

  std::string name;
  std::vector<std::size_t> parameterByteLengths;
  std::vector<std::size_t> returnByteLengths;
  std::optional<FunctionAbiSignature> abiSignature;
};

struct LocalMemory final : Stmt {
  LocalMemory(std::string name, std::size_t byteLength);
  LocalMemory(std::string name, std::string bindingName, std::size_t byteLength,
              MemoryStorage storage);
  LocalMemory(std::string name, std::string bindingName, std::size_t byteLength,
              MemoryStorage storage, std::string templateName);

  std::string name;
  std::string bindingName;
  std::size_t byteLength = 0;
  MemoryStorage storage = MemoryStorage::Local;
  std::string templateName;
};

struct IntegerStore final : Stmt {
  IntegerStore(std::string target, std::size_t targetByteLength,
               std::unique_ptr<Expr> value);
  IntegerStore(std::string target, std::string bindingName,
               std::size_t targetByteLength, MemoryStorage storage,
               std::unique_ptr<Expr> value);
  IntegerStore(std::string target, std::string bindingName,
               std::size_t targetByteLength, MemoryStorage storage,
               std::size_t offset, std::unique_ptr<Expr> value);

  std::string target;
  std::string bindingName;
  std::size_t targetByteLength = 0;
  MemoryStorage storage = MemoryStorage::Local;
  std::size_t offset = 0;
  std::unique_ptr<Expr> value;
};

struct FloatStore final : Stmt {
  FloatStore(std::string target, std::string bindingName,
             std::size_t targetByteLength, MemoryStorage storage,
             std::unique_ptr<Expr> value);
  FloatStore(std::string target, std::string bindingName,
             std::size_t targetByteLength, MemoryStorage storage,
             std::size_t offset, std::unique_ptr<Expr> value);

  std::string target;
  std::string bindingName;
  std::size_t targetByteLength = 0;
  MemoryStorage storage = MemoryStorage::Local;
  std::size_t offset = 0;
  std::unique_ptr<Expr> value;
};

struct StringStore final : Stmt {
  StringStore(std::string target, std::string bindingName,
              std::size_t targetByteLength, MemoryStorage storage,
              std::string value);
  StringStore(std::string target, std::string bindingName,
              std::size_t targetByteLength, MemoryStorage storage,
              std::size_t offset, std::string value);

  std::string target;
  std::string bindingName;
  std::size_t targetByteLength = 0;
  MemoryStorage storage = MemoryStorage::Local;
  std::size_t offset = 0;
  std::string value;
};

struct StringCopyStore final : Stmt {
  StringCopyStore(std::string target, std::string bindingName,
                  std::size_t targetByteLength, MemoryStorage targetStorage,
                  std::size_t targetOffset,
                  std::string source, std::string sourceBindingName,
                  std::size_t sourceByteLength, MemoryStorage sourceStorage,
                  std::size_t sourceOffset);

  std::string target;
  std::string bindingName;
  std::size_t targetByteLength = 0;
  MemoryStorage targetStorage = MemoryStorage::Local;
  std::size_t targetOffset = 0;
  std::string source;
  std::string sourceBindingName;
  std::size_t sourceByteLength = 0;
  MemoryStorage sourceStorage = MemoryStorage::Local;
  std::size_t sourceOffset = 0;
};

struct BoolStore final : Stmt {
  BoolStore(std::string target, std::string bindingName,
            std::size_t targetByteLength, MemoryStorage storage,
            std::unique_ptr<Expr> value);
  BoolStore(std::string target, std::string bindingName,
            std::size_t targetByteLength, MemoryStorage storage,
            std::size_t offset, std::unique_ptr<Expr> value);

  std::string target;
  std::string bindingName;
  std::size_t targetByteLength = 0;
  MemoryStorage storage = MemoryStorage::Local;
  std::size_t offset = 0;
  std::unique_ptr<Expr> value;
};

struct PointerStore final : Stmt {
  PointerStore(std::unique_ptr<Expr> address, std::size_t targetByteLength,
               std::unique_ptr<Expr> value);

  std::unique_ptr<Expr> address;
  std::size_t targetByteLength = 0;
  std::unique_ptr<Expr> value;
};

struct Call final : Stmt {
  Call(std::string callee, std::vector<std::unique_ptr<Expr>> arguments,
       stdlib::BuiltinId builtin = stdlib::BuiltinId::None,
       std::vector<FormatArgKind> formatArgumentKinds = {});

  std::string callee;
  std::vector<std::unique_ptr<Expr>> arguments;
  stdlib::BuiltinId builtin = stdlib::BuiltinId::None;
  std::vector<FormatArgKind> formatArgumentKinds;
};

// Statement form of an internal `impl op` call. It preserves the operation's
// write-through effect while deliberately discarding its ABI result View.
struct UserTemplateOpCall final : Stmt {
  UserTemplateOpCall(std::string callee,
                     std::vector<std::unique_ptr<Expr>> arguments,
                     std::size_t resultByteLength);

  std::string callee;
  std::vector<std::unique_ptr<Expr>> arguments;
  std::size_t resultByteLength = 0;
};

// Statement form of a resolved user `op format`. Unlike a generic Call, this
// records both the stable internal impl symbol and the selected output sink,
// so codegen never infers formatting behavior from a callee string.
struct UserTemplateFormatCall final : Stmt {
  UserTemplateFormatCall(std::string callee, std::unique_ptr<Expr> value,
                         FormatOutputSink sink, std::unique_ptr<Expr> file,
                         std::size_t resultByteLength);

  std::string callee;
  std::unique_ptr<Expr> value;
  FormatOutputSink sink = FormatOutputSink::Stdout;
  std::unique_ptr<Expr> file;
  std::size_t resultByteLength = 4;
};

struct MultiReturnCallStore final : Stmt {
  struct Target {
    Target(std::string name, std::string bindingName, std::size_t byteLength,
           MemoryStorage storage, std::size_t returnIndex);

    std::string name;
    std::string bindingName;
    std::size_t byteLength = 0;
    MemoryStorage storage = MemoryStorage::Local;
    std::size_t returnIndex = 0;
  };

  MultiReturnCallStore(std::string callee,
                       std::vector<std::unique_ptr<Expr>> arguments,
                       std::vector<Target> targets);

  std::string callee;
  std::vector<std::unique_ptr<Expr>> arguments;
  std::vector<Target> targets;
};

struct InputCallStore final : Stmt {
  struct Target {
    Target(std::string name, std::string bindingName, std::size_t byteLength,
           MemoryStorage storage, std::size_t offset,
           std::string templateName = {});

    std::string name;
    std::string bindingName;
    std::size_t byteLength = 0;
    MemoryStorage storage = MemoryStorage::Local;
    std::size_t offset = 0;
    std::string templateName;
  };

  InputCallStore(std::string callee, std::unique_ptr<Expr> file,
                 std::unique_ptr<Expr> format,
                 std::vector<Target> countTargets,
                 std::vector<Target> scanTargets,
                 stdlib::BuiltinId builtin = stdlib::BuiltinId::None);

  std::string callee;
  std::unique_ptr<Expr> file;
  std::unique_ptr<Expr> format;
  std::vector<Target> countTargets;
  std::vector<Target> scanTargets;
  stdlib::BuiltinId builtin = stdlib::BuiltinId::None;
};

struct Return final : Stmt {
  explicit Return(std::vector<std::unique_ptr<Expr>> values);

  std::vector<std::unique_ptr<Expr>> values;
};

struct If final : Stmt {
  If(std::unique_ptr<Expr> condition, std::unique_ptr<Block> thenBlock,
     std::unique_ptr<Block> elseBlock);

  std::unique_ptr<Expr> condition;
  std::unique_ptr<Block> thenBlock;
  std::unique_ptr<Block> elseBlock;
};

struct While final : Stmt {
  While(std::unique_ptr<Expr> condition, std::unique_ptr<Block> body);

  std::unique_ptr<Expr> condition;
  std::unique_ptr<Block> body;
};

struct For final : Stmt {
  For(std::unique_ptr<Stmt> init, std::unique_ptr<Expr> condition,
      std::vector<std::unique_ptr<Stmt>> post, std::unique_ptr<Block> body);

  std::unique_ptr<Stmt> init;
  std::unique_ptr<Expr> condition;
  std::vector<std::unique_ptr<Stmt>> post;
  std::unique_ptr<Block> body;
};

struct Break final : Stmt {};

struct Continue final : Stmt {};

struct Goto final : Stmt {
  explicit Goto(std::string label);

  std::string label;
};

struct Label final : Stmt {
  Label(std::string label, std::unique_ptr<Stmt> statement);

  std::string label;
  std::unique_ptr<Stmt> statement;
};

struct Throw final : Stmt {
  Throw(std::unique_ptr<Stmt> delivery, std::string sourceTemplateName,
        std::size_t sourceByteLength, std::string targetTemplateName,
        std::size_t targetByteLength);

  std::unique_ptr<Stmt> delivery;
  std::string sourceTemplateName;
  std::size_t sourceByteLength = 0;
  std::string targetTemplateName;
  std::size_t targetByteLength = 0;
};

struct TryCatch final : Stmt {
  TryCatch(std::unique_ptr<Block> tryBlock, std::string errorName,
           std::string errorBindingName, std::string errorTemplateName,
           std::size_t errorByteLength,
           std::unique_ptr<Block> catchBlock);

  std::unique_ptr<Block> tryBlock;
  std::string errorName;
  std::string errorBindingName;
  std::string errorTemplateName;
  std::size_t errorByteLength = 0;
  std::unique_ptr<Block> catchBlock;
};

struct Block final {
  explicit Block(std::vector<std::unique_ptr<Stmt>> statements);

  std::vector<std::unique_ptr<Stmt>> statements;
};

struct Function final {
  Function(std::string name, std::unique_ptr<Block> body);
  Function(std::string name, std::vector<Parameter> parameters,
           std::vector<std::size_t> returnByteLengths,
           std::unique_ptr<Block> body);

  std::string name;
  std::vector<Parameter> parameters;
  std::vector<std::size_t> returnByteLengths;
  std::unique_ptr<Block> body;
  Linkage linkage = Linkage::External;
  std::optional<FunctionAbiSignature> abiSignature;
  bool usesViewAbi = false;
  std::size_t viewResultByteLength = 0;
  bool viewParametersAreCopies = false;
};

struct TranslationUnit final {
  explicit TranslationUnit(std::vector<std::unique_ptr<Function>> functions);
  TranslationUnit(std::vector<GlobalMemory> globals,
                  std::vector<std::unique_ptr<Function>> functions);
  TranslationUnit(std::vector<GlobalMemory> globals,
                  std::vector<StructLayout> structs,
                  std::vector<ExternFunction> externFunctions,
                  std::vector<std::unique_ptr<Function>> functions);
  TranslationUnit(std::vector<GlobalMemory> globals,
                  std::vector<StructLayout> structs,
                  std::vector<ViewTemplate> viewTemplates,
                  std::vector<ImplOpBinding> implOps,
                  std::vector<ExternFunction> externFunctions,
                  std::vector<std::unique_ptr<Function>> functions);
  TranslationUnit(std::vector<GlobalMemory> globals,
                  std::vector<StructLayout> structs,
                  std::vector<ViewTemplate> viewTemplates,
                  std::vector<ImplOpBinding> implOps,
                  std::vector<ExternFunction> externFunctions,
                  std::vector<std::unique_ptr<Function>> functions,
                  std::unique_ptr<Block> globalInit);
  TranslationUnit(std::vector<GlobalMemory> globals,
                  std::vector<ExternFunction> externFunctions,
                  std::vector<std::unique_ptr<Function>> functions);

  std::vector<GlobalMemory> globals;
  std::vector<StructLayout> structs;
  std::vector<ViewTemplate> viewTemplates;
  std::vector<ImplOpBinding> implOps;
  std::vector<ExternFunction> externFunctions;
  std::vector<std::unique_ptr<Function>> functions;
  std::unique_ptr<Block> globalInit;
};

// Applies overrides only to definitions in a single HIR translation unit.
// The operation is atomic: a bad override leaves the unit unchanged.  An
// extern declaration must remain a declaration and is therefore rejected as
// an override target.
std::vector<diagnostic::Diagnostic>
applyLinkageOverrides(TranslationUnit &unit,
                      const std::vector<LinkageOverride> &overrides);

// Applies C/FFI ABI shapes after sema.  The operation is atomic and validates
// every ABI byte length against the existing HIR signature before changing the
// unit.  This prevents an ABI sidecar from silently changing core semantics.
std::vector<diagnostic::Diagnostic>
applyAbiOverrides(TranslationUnit &unit,
                  const std::vector<AbiOverride> &overrides);

void dump(const TranslationUnit &unit, std::ostream &out);
std::string dumpToString(const TranslationUnit &unit);

} // namespace hitsimple::hir
