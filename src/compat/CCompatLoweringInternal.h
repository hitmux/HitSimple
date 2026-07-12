#pragma once

#include "hitsimple/compat/CCompat.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace hitsimple::compat::detail {

struct TypeInfo {
  CAbiType abi;
  std::string coreTemplate;
  std::shared_ptr<TypeInfo> pointee;
};

struct ObjectInfo {
  TypeInfo type;
  bool isArray = false;
  std::size_t arrayCount = 0;

  std::size_t byteLength() const;
};

struct FieldInfo {
  std::string name;
  ObjectInfo object;
  std::size_t offset = 0;
};

struct StructInfo {
  const StructDecl* declaration = nullptr;
  bool resolving = false;
  bool resolved = false;
  std::size_t byteLength = 0;
  std::size_t alignment = 1;
  std::vector<FieldInfo> fields;
  CAbiType abi;
};

struct AliasInfo {
  const TypedefDecl* declaration = nullptr;
  bool resolving = false;
  bool resolved = false;
  std::optional<ObjectInfo> object;
};

struct FunctionInfo {
  TypeInfo returnType;
  std::vector<TypeInfo> parameters;
  Linkage linkage = Linkage::External;
  bool isDefinition = false;
};

struct ExprResult {
  std::unique_ptr<ast::Expr> expression;
  TypeInfo type;
  bool isLvalue = false;
  bool isArray = false;
  std::size_t arrayCount = 0;
  bool arrayBackedByAddress = false;
};

class Lowerer {
public:
  Lowerer(const TranslationUnit& unit, LoweringOptions options);

  LoweringResult lower();

private:
  void error(const diagnostic::SourceRange& range, std::string message);
  bool hasErrors() const;

  void collectDeclarations();
  std::optional<TypeInfo> resolveType(const CType& type,
                                      std::size_t pointerDepth = 0);
  std::optional<TypeInfo> resolveBaseType(const CType& type);
  std::optional<TypeInfo> resolveAlias(std::string_view name,
                                       const diagnostic::SourceRange& range);
  std::optional<ObjectInfo> resolveAliasObject(
      std::string_view name, const diagnostic::SourceRange& range);
  bool ensureStructComplete(TypeInfo& type,
                            const diagnostic::SourceRange& range);
  bool resolveStruct(std::string_view name,
                     const diagnostic::SourceRange& range);
  std::optional<ObjectInfo> resolveObject(const CType& type,
                                          const Declarator& declarator);
  static TypeInfo makePointer(TypeInfo pointee, std::size_t pointerByteLength);
  static bool sameType(const TypeInfo& left, const TypeInfo& right);
  static bool isInteger(const TypeInfo& type);
  static bool isFloating(const TypeInfo& type);
  static bool isPointer(const TypeInfo& type);
  static bool isAggregate(const TypeInfo& type);
  static bool isVoid(const TypeInfo& type);
  static std::string byteLengthText(std::size_t byteLength,
                                    std::size_t pointerByteLength);
  static std::string integerOperator(std::size_t byteLength,
                                     std::string_view op);
  static std::string floatOperator(std::size_t byteLength,
                                   std::string_view op);

  void pushScope();
  void popScope();
  void bindObject(std::string name, ObjectInfo object);
  std::optional<ObjectInfo> lookupObject(std::string_view name) const;

  std::optional<ExprResult> lowerExpr(const Expr& expression);
  std::optional<ExprResult> lowerLvalue(const Expr& expression);
  std::optional<ExprResult> decayArray(ExprResult value,
                                       const diagnostic::SourceRange& range);
  std::optional<ExprResult> lowerUnary(const UnaryExpr& expression);
  std::optional<ExprResult> lowerBinary(const BinaryExpr& expression);
  std::optional<ExprResult> lowerAssignment(const AssignmentExpr& expression);
  std::optional<ExprResult> lowerCast(const CastExpr& expression);
  std::optional<ExprResult> lowerIndex(const IndexExpr& expression);
  std::optional<ExprResult> lowerMember(const MemberExpr& expression,
                                        bool requireLvalue);
  std::optional<ExprResult> lowerCall(const CallExpr& expression);
  std::optional<ExprResult> lowerSizeof(const SizeofExpr& expression);

  std::unique_ptr<ast::Stmt> lowerStmt(const Stmt& statement);
  std::unique_ptr<ast::BlockStmt> lowerBlock(const BlockStmt& block);
  std::unique_ptr<ast::Stmt> lowerLocalDeclaration(const VarDecl& declaration);
  bool lowerTopLevel(const Decl& declaration);
  bool lowerFunction(const FunctionDecl& declaration);
  bool lowerGlobal(const VarDecl& declaration);
  bool lowerStruct(const StructDecl& declaration);
  void addLinkage(std::string name,
                  Linkage linkage,
                  bool isFunction,
                  bool isDefinition,
                  std::optional<CAbiType> objectType,
                  std::vector<CAbiType> parameterTypes,
                  std::optional<CAbiType> returnType,
                  const diagnostic::SourceRange& range);

  const TranslationUnit& source_;
  LoweringOptions options_;
  std::unordered_map<std::string, AliasInfo> aliases_;
  std::unordered_map<std::string, StructInfo> structs_;
  std::unordered_map<std::string, FunctionInfo> functions_;
  std::vector<std::unordered_map<std::string, ObjectInfo>> scopes_;
  std::vector<std::unique_ptr<ast::TopLevelDecl>> declarations_;
  std::vector<LinkageMetadata> linkage_;
  std::vector<diagnostic::Diagnostic> diagnostics_;
};

} // namespace hitsimple::compat::detail
