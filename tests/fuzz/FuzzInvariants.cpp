#include "FuzzInvariants.h"

#include "hitsimple/diagnostic/SourceLocation.h"

#include <llvm/AsmParser/Parser.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/SourceMgr.h>

#include <cstdlib>
#include <memory>
#include <sstream>
#include <utility>

namespace hitsimple::fuzz {
namespace {

bool isOrdered(const diagnostic::SourceLocation &begin,
               const diagnostic::SourceLocation &end) {
  if (begin.file != end.file) {
    return false;
  }
  if (begin.line != end.line) {
    return begin.line < end.line;
  }
  return begin.column <= end.column;
}

void assertValidRange(const diagnostic::SourceRange &range) {
  const bool hasBegin = diagnostic::hasFile(range.begin);
  const bool hasEnd = diagnostic::hasFile(range.end);
  require(hasBegin || hasEnd);
  if (hasBegin) {
    require(range.begin.line != 0U);
    require(range.begin.column != 0U);
  }
  if (hasEnd) {
    require(range.end.line != 0U);
    require(range.end.column != 0U);
  }
  if (hasBegin && hasEnd) {
    require(isOrdered(range.begin, range.end));
  }
}

void assertValidNodeRange(const ast::Node &node) {
  if (node.range) {
    assertValidRange(*node.range);
  }
}

void assertValidExpr(const ast::Expr &expression);
void assertValidStmt(const ast::Stmt &statement);

void assertValidBlock(const ast::BlockStmt &block) {
  assertValidNodeRange(block);
  for (const auto &statement : block.statements) {
    require(statement != nullptr);
    assertValidStmt(*statement);
  }
}

void assertValidExpr(const ast::Expr &expression) {
  assertValidNodeRange(expression);

  if (dynamic_cast<const ast::IdentifierExpr *>(&expression) != nullptr ||
      dynamic_cast<const ast::IntegerLiteral *>(&expression) != nullptr ||
      dynamic_cast<const ast::StringLiteral *>(&expression) != nullptr ||
      dynamic_cast<const ast::CharLiteral *>(&expression) != nullptr ||
      dynamic_cast<const ast::FloatLiteral *>(&expression) != nullptr ||
      dynamic_cast<const ast::BoolLiteral *>(&expression) != nullptr ||
      dynamic_cast<const ast::SizeofExpr *>(&expression) != nullptr) {
    return;
  }

  if (const auto *unary = dynamic_cast<const ast::UnaryExpr *>(&expression)) {
    require(unary->operand != nullptr);
    assertValidExpr(*unary->operand);
    return;
  }
  if (const auto *binary = dynamic_cast<const ast::BinaryExpr *>(&expression)) {
    require(binary->left != nullptr);
    require(binary->right != nullptr);
    assertValidExpr(*binary->left);
    assertValidExpr(*binary->right);
    return;
  }
  if (const auto *ternary =
          dynamic_cast<const ast::TernaryExpr *>(&expression)) {
    require(ternary->condition != nullptr);
    require(ternary->thenExpr != nullptr);
    require(ternary->elseExpr != nullptr);
    assertValidExpr(*ternary->condition);
    assertValidExpr(*ternary->thenExpr);
    assertValidExpr(*ternary->elseExpr);
    return;
  }
  if (const auto *unsignedExpr =
          dynamic_cast<const ast::UnsignedExpr *>(&expression)) {
    require(unsignedExpr->operand != nullptr);
    assertValidExpr(*unsignedExpr->operand);
    return;
  }
  if (const auto *cast =
          dynamic_cast<const ast::IntegerCastExpr *>(&expression)) {
    require(cast->operand != nullptr);
    assertValidExpr(*cast->operand);
    return;
  }
  if (const auto *asExpr = dynamic_cast<const ast::AsExpr *>(&expression)) {
    require(asExpr->operand != nullptr);
    assertValidExpr(*asExpr->operand);
    return;
  }
  if (const auto *index = dynamic_cast<const ast::IndexExpr *>(&expression)) {
    require(index->base != nullptr);
    require(index->index != nullptr);
    assertValidExpr(*index->base);
    assertValidExpr(*index->index);
    return;
  }
  if (const auto *slice = dynamic_cast<const ast::SliceExpr *>(&expression)) {
    require(slice->base != nullptr);
    require(slice->start != nullptr);
    require(slice->end != nullptr);
    assertValidExpr(*slice->base);
    assertValidExpr(*slice->start);
    assertValidExpr(*slice->end);
    return;
  }
  if (const auto *member = dynamic_cast<const ast::MemberExpr *>(&expression)) {
    require(member->base != nullptr);
    assertValidExpr(*member->base);
    return;
  }
  if (const auto *deref = dynamic_cast<const ast::DerefExpr *>(&expression)) {
    require(deref->address != nullptr);
    assertValidExpr(*deref->address);
    return;
  }
  if (const auto *call = dynamic_cast<const ast::CallExpr *>(&expression)) {
    for (const auto &argument : call->arguments) {
      require(argument != nullptr);
      assertValidExpr(*argument);
    }
    return;
  }
  if (const auto *call =
          dynamic_cast<const ast::MethodCallExpr *>(&expression)) {
    require(call->receiver != nullptr);
    assertValidExpr(*call->receiver);
    for (const auto &argument : call->arguments) {
      require(argument != nullptr);
      assertValidExpr(*argument);
    }
    return;
  }
  if (const auto *assignment =
          dynamic_cast<const ast::AssignmentExpr *>(&expression)) {
    for (const auto &target : assignment->targets) {
      require(target.target != nullptr);
      assertValidExpr(*target.target);
    }
    for (const auto &value : assignment->values) {
      require(value != nullptr);
      assertValidExpr(*value);
    }
    return;
  }

  require(false);
}

void assertValidStmt(const ast::Stmt &statement) {
  assertValidNodeRange(statement);

  if (dynamic_cast<const ast::NewDecl *>(&statement) != nullptr ||
      dynamic_cast<const ast::BreakStmt *>(&statement) != nullptr ||
      dynamic_cast<const ast::ContinueStmt *>(&statement) != nullptr ||
      dynamic_cast<const ast::GotoStmt *>(&statement) != nullptr ||
      dynamic_cast<const ast::EmptyStmt *>(&statement) != nullptr) {
    return;
  }
  if (const auto *varDecl =
          dynamic_cast<const ast::VarDeclStmt *>(&statement)) {
    for (const auto &item : varDecl->items) {
      if (item.initializer) {
        assertValidExpr(*item.initializer);
      }
    }
    return;
  }
  if (const auto *assign = dynamic_cast<const ast::AssignStmt *>(&statement)) {
    if (assign->targetExpr) {
      assertValidExpr(*assign->targetExpr);
    }
    if (assign->value) {
      assertValidExpr(*assign->value);
    }
    if (assign->assignment) {
      assertValidExpr(*assign->assignment);
    }
    return;
  }
  if (const auto *expr = dynamic_cast<const ast::ExprStmt *>(&statement)) {
    require(expr->expression != nullptr);
    assertValidExpr(*expr->expression);
    return;
  }
  if (const auto *ret = dynamic_cast<const ast::ReturnStmt *>(&statement)) {
    if (ret->value) {
      assertValidExpr(*ret->value);
    }
    for (const auto &value : ret->values) {
      require(value != nullptr);
      assertValidExpr(*value);
    }
    return;
  }
  if (const auto *ifStmt = dynamic_cast<const ast::IfStmt *>(&statement)) {
    require(ifStmt->condition != nullptr);
    require(ifStmt->thenBlock != nullptr);
    assertValidExpr(*ifStmt->condition);
    assertValidBlock(*ifStmt->thenBlock);
    if (ifStmt->elseBlock) {
      assertValidBlock(*ifStmt->elseBlock);
    }
    return;
  }
  if (const auto *whileStmt =
          dynamic_cast<const ast::WhileStmt *>(&statement)) {
    require(whileStmt->condition != nullptr);
    require(whileStmt->body != nullptr);
    assertValidExpr(*whileStmt->condition);
    assertValidBlock(*whileStmt->body);
    return;
  }
  if (const auto *forStmt = dynamic_cast<const ast::ForStmt *>(&statement)) {
    if (forStmt->init) {
      assertValidStmt(*forStmt->init);
    }
    if (forStmt->condition) {
      assertValidExpr(*forStmt->condition);
    }
    for (const auto &post : forStmt->post) {
      require(post != nullptr);
      assertValidExpr(*post);
    }
    require(forStmt->body != nullptr);
    assertValidBlock(*forStmt->body);
    return;
  }
  if (const auto *label = dynamic_cast<const ast::LabelStmt *>(&statement)) {
    require(label->statement != nullptr);
    assertValidStmt(*label->statement);
    return;
  }
  if (const auto *throwStmt =
          dynamic_cast<const ast::ThrowStmt *>(&statement)) {
    require(throwStmt->value != nullptr);
    assertValidExpr(*throwStmt->value);
    return;
  }
  if (const auto *tryCatch =
          dynamic_cast<const ast::TryCatchStmt *>(&statement)) {
    require(tryCatch->tryBlock != nullptr);
    require(tryCatch->catchBlock != nullptr);
    assertValidBlock(*tryCatch->tryBlock);
    assertValidBlock(*tryCatch->catchBlock);
    return;
  }
  if (const auto *set = dynamic_cast<const ast::SetStmt *>(&statement)) {
    require(set->target != nullptr);
    assertValidExpr(*set->target);
    return;
  }

  require(false);
}

void assertValidTopLevel(const ast::TopLevelDecl &declaration) {
  assertValidNodeRange(declaration);

  if (const auto *function =
          dynamic_cast<const ast::FunctionDecl *>(&declaration)) {
    require(function->body != nullptr);
    assertValidBlock(*function->body);
    return;
  }
  if (const auto *global =
          dynamic_cast<const ast::GlobalNewDecl *>(&declaration)) {
    if (global->initializer) {
      assertValidExpr(*global->initializer);
    }
    return;
  }
  if (dynamic_cast<const ast::ExternVarDecl *>(&declaration) != nullptr ||
      dynamic_cast<const ast::ExternFunctionDecl *>(&declaration) != nullptr ||
      dynamic_cast<const ast::StructDecl *>(&declaration) != nullptr ||
      dynamic_cast<const ast::TemplateDecl *>(&declaration) != nullptr) {
    return;
  }
  if (const auto *impl = dynamic_cast<const ast::ImplDecl *>(&declaration)) {
    for (const auto &op : impl->ops) {
      require(op.body != nullptr);
      assertValidBlock(*op.body);
    }
    for (const auto &method : impl->methods) {
      require(method != nullptr);
      assertValidNodeRange(*method);
      require(method->body != nullptr);
      assertValidBlock(*method->body);
    }
    return;
  }

  require(false);
}

} // namespace

std::string sourceFromBytes(const std::uint8_t *data, std::size_t size) {
  require(size <= maximumInputBytes);
  require(data != nullptr || size == 0U);
  if (size == 0U) {
    return {};
  }
  return {reinterpret_cast<const char *>(data), size};
}

void require(bool condition) {
  if (!condition) {
    std::abort();
  }
}

void assertValidToken(const lexer::Token &token) {
  assertValidRange(token.range);
  assertValidRange(token.generatedRange);
}

void assertValidDiagnostics(
    const std::vector<diagnostic::Diagnostic> &diagnostics,
    std::size_t inputSize) {
  require(diagnostics.size() <= inputSize + 1U);
  for (const auto &diagnostic : diagnostics) {
    require(diagnostic.message.find("internal error") == std::string::npos);
    if (diagnostic.range) {
      assertValidRange(*diagnostic.range);
    }
    for (const auto &label : diagnostic.labels) {
      assertValidRange(label.range);
    }
  }
}

void assertValidAstSourceRanges(const ast::TranslationUnit &unit) {
  assertValidNodeRange(unit);
  for (const auto &declaration : unit.declarations) {
    require(declaration != nullptr);
    assertValidTopLevel(*declaration);
  }
}

void assertValidLlvmIr(std::string_view llvmIr) {
  require(!llvmIr.empty());

  llvm::LLVMContext context;
  llvm::SMDiagnostic parseDiagnostic;
  std::unique_ptr<llvm::Module> module = llvm::parseAssemblyString(
      llvm::StringRef(llvmIr), parseDiagnostic, context);
  require(module != nullptr);
  require(!llvm::verifyModule(*module, nullptr));
}

std::string
diagnosticFingerprint(const std::vector<diagnostic::Diagnostic> &diagnostics) {
  std::ostringstream out;
  for (const auto &diagnostic : diagnostics) {
    out << static_cast<int>(diagnostic.severity) << ':'
        << static_cast<int>(diagnostic.stage) << ':' << diagnostic.message
        << '\n';
    if (diagnostic.range) {
      const auto &range = *diagnostic.range;
      out << range.begin.file << ':' << range.begin.line << ':'
          << range.begin.column << '-' << range.end.file << ':'
          << range.end.line << ':' << range.end.column << '\n';
    }
    for (const auto &label : diagnostic.labels) {
      out << label.message << ':' << label.range.begin.file << ':'
          << label.range.begin.line << ':' << label.range.begin.column << '-'
          << label.range.end.file << ':' << label.range.end.line << ':'
          << label.range.end.column << '\n';
    }
  }
  return out.str();
}

} // namespace hitsimple::fuzz
