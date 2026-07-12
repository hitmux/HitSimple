#pragma once

#include "hitsimple/ast/AST.h"
#include "hitsimple/diagnostic/Diagnostic.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hitsimple::compat {

// These nodes model the C compatibility surface only.  Lowering must consume
// them completely before the core semantic analyzer runs.
enum class StorageClass {
  None,
  Static,
  Extern,
};

enum class Linkage {
  None,
  Internal,
  External,
};

enum class BaseType {
  Char,
  SignedChar,
  UnsignedChar,
  Short,
  UnsignedShort,
  Int,
  UnsignedInt,
  Long,
  UnsignedLong,
  LongLong,
  UnsignedLongLong,
  Float,
  Double,
  Void,
  Struct,
  TypedefName,
};

struct CType {
  BaseType base = BaseType::Int;
  std::string name;
  bool isConst = false;
  bool isVolatile = false;
  diagnostic::SourceRange range;
};

struct Declarator {
  std::string name;
  std::size_t pointerDepth = 0;
  std::optional<std::size_t> arrayCount;
  diagnostic::SourceRange range;
};

struct Node {
  explicit Node(diagnostic::SourceRange range = {});
  virtual ~Node() = default;

  diagnostic::SourceRange range;
};

struct Expr : Node {
  using Node::Node;
  ~Expr() override = default;
};

struct IdentifierExpr final : Expr {
  IdentifierExpr(std::string name, diagnostic::SourceRange range = {});

  std::string name;
};

struct IntegerLiteralExpr final : Expr {
  IntegerLiteralExpr(std::string value, diagnostic::SourceRange range = {});

  std::string value;
};

struct FloatLiteralExpr final : Expr {
  FloatLiteralExpr(std::string value, diagnostic::SourceRange range = {});

  std::string value;
};

struct StringLiteralExpr final : Expr {
  StringLiteralExpr(std::string value, diagnostic::SourceRange range = {});

  std::string value;
};

struct CharLiteralExpr final : Expr {
  CharLiteralExpr(std::string value, diagnostic::SourceRange range = {});

  std::string value;
};

struct UnaryExpr final : Expr {
  UnaryExpr(std::string op,
            std::unique_ptr<Expr> operand,
            diagnostic::SourceRange range = {});

  std::string op;
  std::unique_ptr<Expr> operand;
};

struct BinaryExpr final : Expr {
  BinaryExpr(std::unique_ptr<Expr> left,
             std::string op,
             std::unique_ptr<Expr> right,
             diagnostic::SourceRange range = {});

  std::unique_ptr<Expr> left;
  std::string op;
  std::unique_ptr<Expr> right;
};

struct ConditionalExpr final : Expr {
  ConditionalExpr(std::unique_ptr<Expr> condition,
                  std::unique_ptr<Expr> thenExpr,
                  std::unique_ptr<Expr> elseExpr,
                  diagnostic::SourceRange range = {});

  std::unique_ptr<Expr> condition;
  std::unique_ptr<Expr> thenExpr;
  std::unique_ptr<Expr> elseExpr;
};

struct AssignmentExpr final : Expr {
  AssignmentExpr(std::unique_ptr<Expr> target,
                 std::string op,
                 std::unique_ptr<Expr> value,
                 diagnostic::SourceRange range = {});

  std::unique_ptr<Expr> target;
  std::string op;
  std::unique_ptr<Expr> value;
};

struct CastExpr final : Expr {
  CastExpr(CType type,
           std::size_t pointerDepth,
           std::unique_ptr<Expr> operand,
           diagnostic::SourceRange range = {});

  CType type;
  std::size_t pointerDepth = 0;
  std::unique_ptr<Expr> operand;
};

struct SizeofExpr final : Expr {
  SizeofExpr(CType type, diagnostic::SourceRange range = {});
  SizeofExpr(std::string identifier, diagnostic::SourceRange range = {});

  std::optional<CType> type;
  std::string identifier;
};

struct IndexExpr final : Expr {
  IndexExpr(std::unique_ptr<Expr> base,
            std::unique_ptr<Expr> index,
            diagnostic::SourceRange range = {});

  std::unique_ptr<Expr> base;
  std::unique_ptr<Expr> index;
};

struct CallExpr final : Expr {
  CallExpr(std::unique_ptr<Expr> callee,
           std::vector<std::unique_ptr<Expr>> arguments,
           diagnostic::SourceRange range = {});

  std::unique_ptr<Expr> callee;
  std::vector<std::unique_ptr<Expr>> arguments;
};

struct MemberExpr final : Expr {
  MemberExpr(std::unique_ptr<Expr> base,
             std::string member,
             bool throughPointer,
             diagnostic::SourceRange range = {});

  std::unique_ptr<Expr> base;
  std::string member;
  bool throughPointer = false;
};

struct Stmt : Node {
  using Node::Node;
  ~Stmt() override = default;
};

struct VarDecl;

struct EmptyStmt final : Stmt {
  using Stmt::Stmt;
};

struct ExprStmt final : Stmt {
  ExprStmt(std::unique_ptr<Expr> expression,
           diagnostic::SourceRange range = {});

  std::unique_ptr<Expr> expression;
};

struct DeclStmt final : Stmt {
  DeclStmt(std::unique_ptr<VarDecl> declaration,
           diagnostic::SourceRange range = {});

  std::unique_ptr<VarDecl> declaration;
};

struct ReturnStmt final : Stmt {
  ReturnStmt(std::unique_ptr<Expr> value, diagnostic::SourceRange range = {});

  std::unique_ptr<Expr> value;
};

struct BlockStmt final : Stmt {
  BlockStmt(std::vector<std::unique_ptr<Stmt>> statements,
            diagnostic::SourceRange range = {});

  std::vector<std::unique_ptr<Stmt>> statements;
};

struct IfStmt final : Stmt {
  IfStmt(std::unique_ptr<Expr> condition,
         std::unique_ptr<Stmt> thenBranch,
         std::unique_ptr<Stmt> elseBranch,
         diagnostic::SourceRange range = {});

  std::unique_ptr<Expr> condition;
  std::unique_ptr<Stmt> thenBranch;
  std::unique_ptr<Stmt> elseBranch;
};

struct WhileStmt final : Stmt {
  WhileStmt(std::unique_ptr<Expr> condition,
            std::unique_ptr<Stmt> body,
            diagnostic::SourceRange range = {});

  std::unique_ptr<Expr> condition;
  std::unique_ptr<Stmt> body;
};

struct ForStmt final : Stmt {
  ForStmt(std::unique_ptr<Stmt> init,
          std::unique_ptr<Expr> condition,
          std::unique_ptr<Expr> post,
          std::unique_ptr<Stmt> body,
          diagnostic::SourceRange range = {});

  std::unique_ptr<Stmt> init;
  std::unique_ptr<Expr> condition;
  std::unique_ptr<Expr> post;
  std::unique_ptr<Stmt> body;
};

struct BreakStmt final : Stmt {
  using Stmt::Stmt;
};

struct ContinueStmt final : Stmt {
  using Stmt::Stmt;
};

struct GotoStmt final : Stmt {
  GotoStmt(std::string label, diagnostic::SourceRange range = {});

  std::string label;
};

struct LabelStmt final : Stmt {
  LabelStmt(std::string label,
            std::unique_ptr<Stmt> statement,
            diagnostic::SourceRange range = {});

  std::string label;
  std::unique_ptr<Stmt> statement;
};

struct Parameter {
  CType type;
  Declarator declarator;
  bool isVoidMarker = false;
  diagnostic::SourceRange range;
};

struct Decl : Node {
  using Node::Node;
  ~Decl() override = default;
};

struct VarDecl final : Decl {
  VarDecl(StorageClass storage,
          CType type,
          Declarator declarator,
          std::unique_ptr<Expr> initializer,
          diagnostic::SourceRange range = {});

  StorageClass storage = StorageClass::None;
  CType type;
  Declarator declarator;
  std::unique_ptr<Expr> initializer;
};

struct FunctionDecl final : Decl {
  FunctionDecl(StorageClass storage,
               CType returnType,
               Declarator declarator,
               std::vector<Parameter> parameters,
               std::unique_ptr<BlockStmt> body,
               diagnostic::SourceRange range = {});

  StorageClass storage = StorageClass::None;
  CType returnType;
  Declarator declarator;
  std::vector<Parameter> parameters;
  std::unique_ptr<BlockStmt> body;

  bool isDefinition() const;
};

struct FieldDecl {
  CType type;
  Declarator declarator;
  diagnostic::SourceRange range;
};

struct StructDecl final : Decl {
  StructDecl(std::string name,
             std::vector<FieldDecl> fields,
             diagnostic::SourceRange range = {});

  std::string name;
  std::vector<FieldDecl> fields;
};

struct TypedefDecl final : Decl {
  TypedefDecl(CType type,
              Declarator declarator,
              diagnostic::SourceRange range = {});

  CType type;
  Declarator declarator;
};

struct TranslationUnit final : Node {
  TranslationUnit(std::vector<std::unique_ptr<Decl>> declarations,
                  diagnostic::SourceRange range = {});

  std::vector<std::unique_ptr<Decl>> declarations;
};

struct ParseResult {
  std::unique_ptr<TranslationUnit> unit;
  std::vector<diagnostic::Diagnostic> diagnostics;

  bool ok() const;
};

struct LoweringOptions {
  std::size_t pointerByteLength = sizeof(void*);
  bool rejectQualifiers = true;
  bool allowHostFloatExternAbi = false;
};

// This is deliberately separate from core byte-length signatures.  It records
// the host C ABI shape that codegen must use for C declarations and calls.
enum class CAbiValueKind {
  Void,
  Integer,
  Floating,
  Pointer,
  Aggregate,
};

struct CAbiType {
  CAbiType() = default;
  CAbiType(CAbiValueKind kind, std::size_t byteLength, bool isSigned,
           std::string aggregateName = {})
      : kind(kind), byteLength(byteLength), isSigned(isSigned),
        aggregateName(std::move(aggregateName)) {}

  CAbiValueKind kind = CAbiValueKind::Integer;
  std::size_t byteLength = 0;
  bool isSigned = false;
  std::string aggregateName;
  // C aggregate fields retain their native element representation.  Arrays
  // use elementCount instead of flattening to bytes so LLVM can preserve the
  // host ABI classification for float and aggregate fields.
  std::size_t alignment = 1;
  std::size_t elementCount = 1;
  std::vector<CAbiType> aggregateFields;
  std::vector<std::size_t> aggregateFieldOffsets;
};

struct LinkageMetadata {
  std::string sourceName;
  std::string coreName;
  Linkage linkage = Linkage::None;
  bool isFunction = false;
  bool isDefinition = false;
  std::optional<CAbiType> objectType;
  std::vector<CAbiType> parameterTypes;
  std::optional<CAbiType> returnType;
  diagnostic::SourceRange range;
};

struct LoweringResult {
  std::unique_ptr<ast::TranslationUnit> unit;
  std::vector<LinkageMetadata> linkage;
  std::vector<diagnostic::Diagnostic> diagnostics;

  bool ok() const;
};

// The C parser is deliberately separate from parser::parseSource.  Callers
// must lower its result before invoking sema::analyze.
ParseResult parseCCompatSource(std::string_view source, std::string fileName);

// Produces only core AST nodes plus out-of-band linkage metadata.  C types,
// declarators, typedef names and compatibility expressions never appear in
// the returned AST.
LoweringResult lowerCCompatToCore(const TranslationUnit& unit,
                                  LoweringOptions options = {});

} // namespace hitsimple::compat
