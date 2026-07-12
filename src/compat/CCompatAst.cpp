#include "hitsimple/compat/CCompat.h"

#include <utility>

namespace hitsimple::compat {

Node::Node(diagnostic::SourceRange range) : range(std::move(range)) {}

IdentifierExpr::IdentifierExpr(std::string name, diagnostic::SourceRange range)
    : Expr(std::move(range)), name(std::move(name)) {}

IntegerLiteralExpr::IntegerLiteralExpr(std::string value,
                                       diagnostic::SourceRange range)
    : Expr(std::move(range)), value(std::move(value)) {}

FloatLiteralExpr::FloatLiteralExpr(std::string value,
                                   diagnostic::SourceRange range)
    : Expr(std::move(range)), value(std::move(value)) {}

StringLiteralExpr::StringLiteralExpr(std::string value,
                                     diagnostic::SourceRange range)
    : Expr(std::move(range)), value(std::move(value)) {}

CharLiteralExpr::CharLiteralExpr(std::string value,
                                 diagnostic::SourceRange range)
    : Expr(std::move(range)), value(std::move(value)) {}

UnaryExpr::UnaryExpr(std::string op,
                     std::unique_ptr<Expr> operand,
                     diagnostic::SourceRange range)
    : Expr(std::move(range)), op(std::move(op)), operand(std::move(operand)) {}

BinaryExpr::BinaryExpr(std::unique_ptr<Expr> left,
                       std::string op,
                       std::unique_ptr<Expr> right,
                       diagnostic::SourceRange range)
    : Expr(std::move(range)),
      left(std::move(left)),
      op(std::move(op)),
      right(std::move(right)) {}

ConditionalExpr::ConditionalExpr(std::unique_ptr<Expr> condition,
                                 std::unique_ptr<Expr> thenExpr,
                                 std::unique_ptr<Expr> elseExpr,
                                 diagnostic::SourceRange range)
    : Expr(std::move(range)),
      condition(std::move(condition)),
      thenExpr(std::move(thenExpr)),
      elseExpr(std::move(elseExpr)) {}

AssignmentExpr::AssignmentExpr(std::unique_ptr<Expr> target,
                               std::string op,
                               std::unique_ptr<Expr> value,
                               diagnostic::SourceRange range)
    : Expr(std::move(range)),
      target(std::move(target)),
      op(std::move(op)),
      value(std::move(value)) {}

CastExpr::CastExpr(CType type,
                   std::size_t pointerDepth,
                   std::unique_ptr<Expr> operand,
                   diagnostic::SourceRange range)
    : Expr(std::move(range)),
      type(std::move(type)),
      pointerDepth(pointerDepth),
      operand(std::move(operand)) {}

SizeofExpr::SizeofExpr(CType type, diagnostic::SourceRange range)
    : Expr(std::move(range)), type(std::move(type)) {}

SizeofExpr::SizeofExpr(std::string identifier, diagnostic::SourceRange range)
    : Expr(std::move(range)), identifier(std::move(identifier)) {}

IndexExpr::IndexExpr(std::unique_ptr<Expr> base,
                     std::unique_ptr<Expr> index,
                     diagnostic::SourceRange range)
    : Expr(std::move(range)), base(std::move(base)), index(std::move(index)) {}

CallExpr::CallExpr(std::unique_ptr<Expr> callee,
                   std::vector<std::unique_ptr<Expr>> arguments,
                   diagnostic::SourceRange range)
    : Expr(std::move(range)),
      callee(std::move(callee)),
      arguments(std::move(arguments)) {}

MemberExpr::MemberExpr(std::unique_ptr<Expr> base,
                       std::string member,
                       bool throughPointer,
                       diagnostic::SourceRange range)
    : Expr(std::move(range)),
      base(std::move(base)),
      member(std::move(member)),
      throughPointer(throughPointer) {}

ExprStmt::ExprStmt(std::unique_ptr<Expr> expression,
                   diagnostic::SourceRange range)
    : Stmt(std::move(range)), expression(std::move(expression)) {}

DeclStmt::DeclStmt(std::unique_ptr<VarDecl> declaration,
                   diagnostic::SourceRange range)
    : Stmt(std::move(range)), declaration(std::move(declaration)) {}

ReturnStmt::ReturnStmt(std::unique_ptr<Expr> value,
                       diagnostic::SourceRange range)
    : Stmt(std::move(range)), value(std::move(value)) {}

BlockStmt::BlockStmt(std::vector<std::unique_ptr<Stmt>> statements,
                     diagnostic::SourceRange range)
    : Stmt(std::move(range)), statements(std::move(statements)) {}

IfStmt::IfStmt(std::unique_ptr<Expr> condition,
               std::unique_ptr<Stmt> thenBranch,
               std::unique_ptr<Stmt> elseBranch,
               diagnostic::SourceRange range)
    : Stmt(std::move(range)),
      condition(std::move(condition)),
      thenBranch(std::move(thenBranch)),
      elseBranch(std::move(elseBranch)) {}

WhileStmt::WhileStmt(std::unique_ptr<Expr> condition,
                     std::unique_ptr<Stmt> body,
                     diagnostic::SourceRange range)
    : Stmt(std::move(range)),
      condition(std::move(condition)),
      body(std::move(body)) {}

ForStmt::ForStmt(std::unique_ptr<Stmt> init,
                 std::unique_ptr<Expr> condition,
                 std::unique_ptr<Expr> post,
                 std::unique_ptr<Stmt> body,
                 diagnostic::SourceRange range)
    : Stmt(std::move(range)),
      init(std::move(init)),
      condition(std::move(condition)),
      post(std::move(post)),
      body(std::move(body)) {}

GotoStmt::GotoStmt(std::string label, diagnostic::SourceRange range)
    : Stmt(std::move(range)), label(std::move(label)) {}

LabelStmt::LabelStmt(std::string label,
                     std::unique_ptr<Stmt> statement,
                     diagnostic::SourceRange range)
    : Stmt(std::move(range)),
      label(std::move(label)),
      statement(std::move(statement)) {}

VarDecl::VarDecl(StorageClass storage,
                 CType type,
                 Declarator declarator,
                 std::unique_ptr<Expr> initializer,
                 diagnostic::SourceRange range)
    : Decl(std::move(range)),
      storage(storage),
      type(std::move(type)),
      declarator(std::move(declarator)),
      initializer(std::move(initializer)) {}

FunctionDecl::FunctionDecl(StorageClass storage,
                           CType returnType,
                           Declarator declarator,
                           std::vector<Parameter> parameters,
                           std::unique_ptr<BlockStmt> body,
                           diagnostic::SourceRange range)
    : Decl(std::move(range)),
      storage(storage),
      returnType(std::move(returnType)),
      declarator(std::move(declarator)),
      parameters(std::move(parameters)),
      body(std::move(body)) {}

bool FunctionDecl::isDefinition() const { return body != nullptr; }

StructDecl::StructDecl(std::string name,
                       std::vector<FieldDecl> fields,
                       diagnostic::SourceRange range)
    : Decl(std::move(range)), name(std::move(name)), fields(std::move(fields)) {}

TypedefDecl::TypedefDecl(CType type,
                         Declarator declarator,
                         diagnostic::SourceRange range)
    : Decl(std::move(range)),
      type(std::move(type)),
      declarator(std::move(declarator)) {}

TranslationUnit::TranslationUnit(std::vector<std::unique_ptr<Decl>> declarations,
                                 diagnostic::SourceRange range)
    : Node(std::move(range)), declarations(std::move(declarations)) {}

bool ParseResult::ok() const { return unit != nullptr && diagnostics.empty(); }

bool LoweringResult::ok() const {
  return unit != nullptr && diagnostics.empty();
}

} // namespace hitsimple::compat
