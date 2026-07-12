#pragma once

#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace hitsimple::ast {

class Dumper;

struct Node {
  virtual ~Node() = default;
};

struct Expr : Node {
  ~Expr() override = default;
};

struct Stmt : Node {
  ~Stmt() override = default;
};

struct TopLevelDecl : Node {
  ~TopLevelDecl() override = default;
};

struct BlockStmt;

struct IdentifierExpr final : Expr {
  explicit IdentifierExpr(std::string name);

  std::string name;
};

struct IntegerLiteral final : Expr {
  explicit IntegerLiteral(std::string value);

  std::string value;
};

struct StringLiteral final : Expr {
  explicit StringLiteral(std::string value);

  std::string value;
};

struct CharLiteral final : Expr {
  explicit CharLiteral(std::string value);

  std::string value;
};

struct FloatLiteral final : Expr {
  explicit FloatLiteral(std::string value);

  std::string value;
};

struct BoolLiteral final : Expr {
  explicit BoolLiteral(bool value);

  bool value;
};

struct UnaryExpr final : Expr {
  UnaryExpr(std::string op, std::unique_ptr<Expr> operand);

  std::string op;
  std::unique_ptr<Expr> operand;
};

struct BinaryExpr final : Expr {
  BinaryExpr(std::unique_ptr<Expr> left, std::string op,
             std::unique_ptr<Expr> right);

  std::unique_ptr<Expr> left;
  std::string op;
  std::unique_ptr<Expr> right;
};

struct TernaryExpr final : Expr {
  TernaryExpr(std::unique_ptr<Expr> condition, std::unique_ptr<Expr> thenExpr,
              std::unique_ptr<Expr> elseExpr);

  std::unique_ptr<Expr> condition;
  std::unique_ptr<Expr> thenExpr;
  std::unique_ptr<Expr> elseExpr;
};

struct UnsignedExpr final : Expr {
  explicit UnsignedExpr(std::unique_ptr<Expr> operand,
                        std::size_t byteLength = 0);

  std::unique_ptr<Expr> operand;
  // A non-zero length is supplied by compatibility lowering when a C
  // unsigned literal needs a width that cannot be inferred as a core signed
  // literal (for example, 0xffffffffU).
  std::size_t byteLength = 0;
};

struct IntegerCastExpr final : Expr {
  IntegerCastExpr(std::unique_ptr<Expr> operand, std::size_t byteLength,
                  bool isSigned);

  std::unique_ptr<Expr> operand;
  std::size_t byteLength = 0;
  bool isSigned = true;
};

struct AsExpr final : Expr {
  AsExpr(std::unique_ptr<Expr> operand, std::string templateName);

  std::unique_ptr<Expr> operand;
  std::string templateName;
};

struct IndexExpr final : Expr {
  IndexExpr(std::unique_ptr<Expr> base, std::unique_ptr<Expr> index);

  std::unique_ptr<Expr> base;
  std::unique_ptr<Expr> index;
};

struct SliceExpr final : Expr {
  SliceExpr(std::unique_ptr<Expr> base, std::unique_ptr<Expr> start,
            std::unique_ptr<Expr> end, bool lengthMode);

  std::unique_ptr<Expr> base;
  std::unique_ptr<Expr> start;
  std::unique_ptr<Expr> end;
  bool lengthMode = false;
};

struct MemberExpr final : Expr {
  MemberExpr(std::unique_ptr<Expr> base, std::string member);

  std::unique_ptr<Expr> base;
  std::string member;
};

struct SizeofExpr final : Expr {
  explicit SizeofExpr(std::string name);

  std::string name;
};

struct DerefExpr final : Expr {
  DerefExpr(std::string length, std::unique_ptr<Expr> address);

  std::string length;
  std::unique_ptr<Expr> address;
};

struct CallExpr final : Expr {
  CallExpr(std::string callee, std::vector<std::unique_ptr<Expr>> arguments);

  std::string callee;
  std::vector<std::unique_ptr<Expr>> arguments;
};

struct MethodCallExpr final : Expr {
  MethodCallExpr(std::unique_ptr<Expr> receiver, std::string method,
                 std::vector<std::unique_ptr<Expr>> arguments);

  std::unique_ptr<Expr> receiver;
  std::string method;
  std::vector<std::unique_ptr<Expr>> arguments;
};

struct AssignmentTarget final {
  AssignmentTarget() = default;
  AssignmentTarget(std::unique_ptr<Expr> target, std::string op);

  AssignmentTarget(AssignmentTarget &&) noexcept = default;
  AssignmentTarget &operator=(AssignmentTarget &&) noexcept = default;
  AssignmentTarget(const AssignmentTarget &) = delete;
  AssignmentTarget &operator=(const AssignmentTarget &) = delete;

  std::unique_ptr<Expr> target;
  std::string op = "=";
  // Compatibility lowering records the C lvalue interpretation here so
  // compound assignment can zero-extend an unsigned target before reading it.
  bool unsignedTarget = false;
};

struct AssignmentExpr final : Expr {
  AssignmentExpr(std::vector<AssignmentTarget> targets,
                 std::vector<std::unique_ptr<Expr>> values);
  AssignmentExpr(std::unique_ptr<Expr> target, std::string op,
                 std::unique_ptr<Expr> value);

  std::vector<AssignmentTarget> targets;
  std::vector<std::unique_ptr<Expr>> values;
};

struct NewDecl final : Stmt {
  NewDecl(std::string name, std::string length);

  std::string name;
  std::string length;
};

struct DeclItem final {
  DeclItem() = default;
  DeclItem(std::string name, std::string length, std::string assignmentOp,
           std::unique_ptr<Expr> initializer, std::string templateName);

  DeclItem(DeclItem &&) noexcept = default;
  DeclItem &operator=(DeclItem &&) noexcept = default;
  DeclItem(const DeclItem &) = delete;
  DeclItem &operator=(const DeclItem &) = delete;

  std::string name;
  std::string length;
  std::string assignmentOp;
  std::unique_ptr<Expr> initializer;
  std::string templateName;
};

struct VarDeclStmt final : Stmt {
  VarDeclStmt(std::string storage, std::vector<DeclItem> items);

  std::string storage;
  std::vector<DeclItem> items;
};

struct AssignStmt final : Stmt {
  AssignStmt(std::string target, std::string op, std::unique_ptr<Expr> value);
  AssignStmt(std::unique_ptr<Expr> targetExpr, std::string op,
             std::unique_ptr<Expr> value);
  explicit AssignStmt(std::unique_ptr<AssignmentExpr> assignment);

  std::string target;
  std::unique_ptr<Expr> targetExpr;
  std::string op;
  std::unique_ptr<Expr> value;
  std::unique_ptr<AssignmentExpr> assignment;
};

struct ExprStmt final : Stmt {
  explicit ExprStmt(std::unique_ptr<Expr> expression);

  std::unique_ptr<Expr> expression;
};

struct ReturnStmt final : Stmt {
  explicit ReturnStmt(std::unique_ptr<Expr> value);
  explicit ReturnStmt(std::vector<std::unique_ptr<Expr>> values);

  std::unique_ptr<Expr> value;
  std::vector<std::unique_ptr<Expr>> values;
};

struct IfStmt final : Stmt {
  IfStmt(std::unique_ptr<Expr> condition, std::unique_ptr<BlockStmt> thenBlock,
         std::unique_ptr<BlockStmt> elseBlock);

  std::unique_ptr<Expr> condition;
  std::unique_ptr<BlockStmt> thenBlock;
  std::unique_ptr<BlockStmt> elseBlock;
};

struct WhileStmt final : Stmt {
  WhileStmt(std::unique_ptr<Expr> condition, std::unique_ptr<BlockStmt> body);

  std::unique_ptr<Expr> condition;
  std::unique_ptr<BlockStmt> body;
};

struct ForStmt final : Stmt {
  ForStmt(std::unique_ptr<Stmt> init, std::unique_ptr<Expr> condition,
          std::vector<std::unique_ptr<Expr>> post,
          std::unique_ptr<BlockStmt> body);

  std::unique_ptr<Stmt> init;
  std::unique_ptr<Expr> condition;
  std::vector<std::unique_ptr<Expr>> post;
  std::unique_ptr<BlockStmt> body;
};

struct BreakStmt final : Stmt {};

struct ContinueStmt final : Stmt {};

struct GotoStmt final : Stmt {
  explicit GotoStmt(std::string label);

  std::string label;
};

struct LabelStmt final : Stmt {
  LabelStmt(std::string label, std::unique_ptr<Stmt> statement);

  std::string label;
  std::unique_ptr<Stmt> statement;
};

struct EmptyStmt final : Stmt {};

struct ThrowStmt final : Stmt {
  explicit ThrowStmt(std::unique_ptr<Expr> value);

  std::unique_ptr<Expr> value;
};

struct TryCatchStmt final : Stmt {
  TryCatchStmt(std::unique_ptr<BlockStmt> tryBlock, std::string errorName,
               std::string errorLength, std::string errorTemplateName,
               std::unique_ptr<BlockStmt> catchBlock);

  std::unique_ptr<BlockStmt> tryBlock;
  std::string errorName;
  std::string errorLength;
  std::string errorTemplateName;
  std::unique_ptr<BlockStmt> catchBlock;
};

struct SetStmt final : Stmt {
  SetStmt(std::unique_ptr<Expr> target, std::string templateName);

  std::unique_ptr<Expr> target;
  std::string templateName;
};

struct BlockStmt final : Node {
  explicit BlockStmt(std::vector<std::unique_ptr<Stmt>> statements);

  std::vector<std::unique_ptr<Stmt>> statements;
};

struct Param final {
  Param() = default;
  Param(std::string name, std::string length, std::string templateName = "",
        bool isMutable = false);

  std::string name;
  std::string length;
  std::string templateName;
  bool isMutable = false;
};

struct ReturnItem final {
  ReturnItem() = default;
  ReturnItem(std::string name, std::string length,
             std::string templateName = "");

  std::string name;
  std::string length;
  std::string templateName;
};

struct FunctionDecl final : TopLevelDecl {
  FunctionDecl(std::string name, std::vector<Param> params,
               std::vector<ReturnItem> returns,
               std::unique_ptr<BlockStmt> body);

  std::string name;
  std::vector<Param> params;
  std::vector<ReturnItem> returns;
  std::unique_ptr<BlockStmt> body;
};

struct GlobalNewDecl final : TopLevelDecl {
  GlobalNewDecl(std::string name, std::string length,
                std::string templateName = "", std::string assignmentOp = "",
                std::unique_ptr<Expr> initializer = nullptr);

  std::string name;
  std::string length;
  std::string templateName;
  std::string assignmentOp;
  std::unique_ptr<Expr> initializer;
};

struct ExternVarDecl final : TopLevelDecl {
  ExternVarDecl(std::string name, std::string length,
                std::string templateName = "");

  std::string name;
  std::string length;
  std::string templateName;
};

struct ExternFunctionDecl final : TopLevelDecl {
  ExternFunctionDecl(std::string name, std::vector<Param> params,
                     std::vector<ReturnItem> returns);

  std::string name;
  std::vector<Param> params;
  std::vector<ReturnItem> returns;
};

struct StructMember final {
  StructMember() = default;
  StructMember(std::string name, std::string length);

  std::string name;
  std::string length;
};

struct StructDecl final : TopLevelDecl {
  StructDecl(std::string name, std::vector<StructMember> members);

  std::string name;
  std::vector<StructMember> members;
};

struct TemplateMember final {
  TemplateMember() = default;
  TemplateMember(std::string name, std::string length,
                 std::string templateName);

  std::string name;
  std::string length;
  std::string templateName;
};

struct TemplateDecl final : TopLevelDecl {
  TemplateDecl(std::string name, std::vector<TemplateMember> members);

  std::string name;
  std::vector<TemplateMember> members;
};

struct ImplOpParam final {
  ImplOpParam() = default;
  ImplOpParam(std::string name, std::string templateName, bool isMutable);

  std::string name;
  std::string templateName;
  bool isMutable = false;
};

struct ImplOpDecl final {
  ImplOpDecl() = default;
  ImplOpDecl(std::string op, std::vector<ImplOpParam> params,
             std::vector<ReturnItem> returns, std::unique_ptr<BlockStmt> body);

  ImplOpDecl(ImplOpDecl &&) noexcept = default;
  ImplOpDecl &operator=(ImplOpDecl &&) noexcept = default;
  ImplOpDecl(const ImplOpDecl &) = delete;
  ImplOpDecl &operator=(const ImplOpDecl &) = delete;

  std::string op;
  std::vector<ImplOpParam> params;
  std::vector<ReturnItem> returns;
  std::unique_ptr<BlockStmt> body;
};

struct ImplDecl final : TopLevelDecl {
  ImplDecl(std::string name, std::vector<ImplOpDecl> ops,
           std::vector<std::unique_ptr<FunctionDecl>> methods = {});

  std::string name;
  std::vector<ImplOpDecl> ops;
  std::vector<std::unique_ptr<FunctionDecl>> methods;

  bool containsOp() const;
};

struct TranslationUnit final : Node {
  explicit TranslationUnit(
      std::vector<std::unique_ptr<FunctionDecl>> functions);
  explicit TranslationUnit(
      std::vector<std::unique_ptr<TopLevelDecl>> declarations);

  std::vector<std::unique_ptr<TopLevelDecl>> declarations;
  std::vector<FunctionDecl *> functions;
  std::vector<GlobalNewDecl *> globalNews;
  std::vector<ExternVarDecl *> externVariables;
  std::vector<ExternFunctionDecl *> externFunctions;
  std::vector<StructDecl *> structs;
  std::vector<TemplateDecl *> templates;
  std::vector<ImplDecl *> impls;
};

void dump(const TranslationUnit &unit, std::ostream &out);
std::string dumpToString(const TranslationUnit &unit);

} // namespace hitsimple::ast
