#include "hitsimple/ast/AST.h"

#include <sstream>
#include <utility>

namespace hitsimple::ast {
namespace {

class AstDumper {
public:
  explicit AstDumper(std::ostream &out) : out_(out) {}

  void dump(const TranslationUnit &unit) {
    line("TranslationUnit");
    ++indent_;
    for (const auto &declaration : unit.declarations) {
      dump(*declaration);
    }
    --indent_;
  }

private:
  void dump(const TopLevelDecl &declaration) {
    if (const auto *function =
            dynamic_cast<const FunctionDecl *>(&declaration)) {
      dump(*function);
      return;
    }

    if (const auto *globalNew =
            dynamic_cast<const GlobalNewDecl *>(&declaration)) {
      std::string text = "GlobalNewDecl name=" + globalNew->name;
      if (!globalNew->length.empty()) {
        text += " length=" + globalNew->length;
      }
      if (!globalNew->assignmentOp.empty()) {
        text += " op=" + globalNew->assignmentOp;
      }
      if (!globalNew->templateName.empty()) {
        text += " template=" + globalNew->templateName;
      }
      line(text);
      if (globalNew->initializer) {
        ++indent_;
        dump(*globalNew->initializer);
        --indent_;
      }
      return;
    }

    if (const auto *externVar =
            dynamic_cast<const ExternVarDecl *>(&declaration)) {
      std::string text = "ExternVarDecl name=" + externVar->name;
      if (!externVar->length.empty()) {
        text += " length=" + externVar->length;
      }
      if (!externVar->templateName.empty()) {
        text += " template=" + externVar->templateName;
      }
      line(text);
      return;
    }

    if (const auto *externFunction =
            dynamic_cast<const ExternFunctionDecl *>(&declaration)) {
      line("ExternFunctionDecl name=" + externFunction->name);
      ++indent_;
      for (const auto &param : externFunction->params) {
        dump(param);
      }
      for (const auto &item : externFunction->returns) {
        dump(item);
      }
      --indent_;
      return;
    }

    if (const auto *structDecl =
            dynamic_cast<const StructDecl *>(&declaration)) {
      line("StructDecl name=" + structDecl->name);
      ++indent_;
      for (const auto &member : structDecl->members) {
        line("StructMember name=" + member.name + " length=" + member.length);
      }
      --indent_;
      return;
    }

    if (const auto *templateDecl =
            dynamic_cast<const TemplateDecl *>(&declaration)) {
      line("TemplateDecl name=" + templateDecl->name);
      ++indent_;
      for (const auto &member : templateDecl->members) {
        std::string text = "TemplateMember name=" + member.name;
        if (!member.length.empty()) {
          text += " length=" + member.length;
        }
        if (!member.templateName.empty()) {
          text += " template=" + member.templateName;
        }
        line(text);
      }
      --indent_;
      return;
    }

    if (const auto *implDecl = dynamic_cast<const ImplDecl *>(&declaration)) {
      line(std::string("ImplDecl name=") + implDecl->name +
           (implDecl->containsOp() ? " containsOp=true" : ""));
      ++indent_;
      for (const auto &method : implDecl->methods) {
        line("ImplMethodDecl");
        ++indent_;
        dump(*method);
        --indent_;
      }
      for (const auto &op : implDecl->ops) {
        line("ImplOpDecl op=" + op.op);
        ++indent_;
        for (const auto &param : op.params) {
          line("ImplOpParam name=" + param.name + " template=" +
               param.templateName + (param.isMutable ? " mutable=true" : ""));
        }
        for (const auto &item : op.returns) {
          dump(item);
        }
        --indent_;
      }
      --indent_;
    }
  }

  void dump(const FunctionDecl &function) {
    line("FunctionDecl name=" + function.name);
    ++indent_;
    for (const auto &param : function.params) {
      dump(param);
    }
    for (const auto &item : function.returns) {
      dump(item);
    }
    dump(*function.body);
    --indent_;
  }

  void dump(const Param &param) {
    std::string text = "Param name=" + param.name;
    if (!param.length.empty()) {
      text += " length=" + param.length;
    }
    if (!param.templateName.empty()) {
      text += " template=" + param.templateName;
    }
    line(text);
  }

  void dump(const ReturnItem &item) {
    std::string text = "ReturnItem";
    if (!item.name.empty()) {
      text += " name=" + item.name;
    }
    if (!item.length.empty()) {
      text += " length=" + item.length;
    }
    if (!item.templateName.empty()) {
      text += " template=" + item.templateName;
    }
    line(text);
  }

  void dump(const BlockStmt &block) {
    line("BlockStmt");
    ++indent_;
    for (const auto &statement : block.statements) {
      dump(*statement);
    }
    --indent_;
  }

  void dump(const Stmt &statement) {
    if (const auto *newDecl = dynamic_cast<const NewDecl *>(&statement)) {
      line("NewDecl name=" + newDecl->name + " length=" + newDecl->length);
      return;
    }

    if (const auto *varDecl = dynamic_cast<const VarDeclStmt *>(&statement)) {
      if (varDecl->storage == "new" && varDecl->items.size() == 1U) {
        const auto &item = varDecl->items.front();
        if (item.assignmentOp.empty() && item.initializer == nullptr &&
            item.templateName.empty() && !item.length.empty()) {
          line("NewDecl name=" + item.name + " length=" + item.length);
          return;
        }
      }

      line("VarDeclStmt storage=" + varDecl->storage);
      ++indent_;
      for (const auto &item : varDecl->items) {
        std::string text = "DeclItem name=" + item.name;
        if (!item.length.empty()) {
          text += " length=" + item.length;
        }
        if (!item.assignmentOp.empty()) {
          text += " op=" + item.assignmentOp;
        }
        if (!item.templateName.empty()) {
          text += " template=" + item.templateName;
        }
        line(text);
        if (item.initializer) {
          ++indent_;
          dump(*item.initializer);
          --indent_;
        }
      }
      --indent_;
      return;
    }

    if (const auto *assign = dynamic_cast<const AssignStmt *>(&statement)) {
      if (!assign->target.empty()) {
        line("AssignStmt target=" + assign->target + " op=" + assign->op);
      } else {
        line("AssignStmt");
      }
      ++indent_;
      dump(*assign->assignment);
      --indent_;
      return;
    }

    if (const auto *expr = dynamic_cast<const ExprStmt *>(&statement)) {
      line("ExprStmt");
      ++indent_;
      dump(*expr->expression);
      --indent_;
      return;
    }

    if (const auto *ret = dynamic_cast<const ReturnStmt *>(&statement)) {
      line("ReturnStmt");
      ++indent_;
      for (const auto &value : ret->values) {
        dump(*value);
      }
      --indent_;
      return;
    }

    if (const auto *ifStmt = dynamic_cast<const IfStmt *>(&statement)) {
      line("IfStmt");
      ++indent_;
      dump(*ifStmt->condition);
      dump(*ifStmt->thenBlock);
      if (ifStmt->elseBlock) {
        dump(*ifStmt->elseBlock);
      }
      --indent_;
      return;
    }

    if (const auto *whileStmt = dynamic_cast<const WhileStmt *>(&statement)) {
      line("WhileStmt");
      ++indent_;
      dump(*whileStmt->condition);
      dump(*whileStmt->body);
      --indent_;
      return;
    }

    if (const auto *forStmt = dynamic_cast<const ForStmt *>(&statement)) {
      line("ForStmt");
      ++indent_;
      if (forStmt->init) {
        dump(*forStmt->init);
      }
      if (forStmt->condition) {
        dump(*forStmt->condition);
      }
      for (const auto &post : forStmt->post) {
        dump(*post);
      }
      dump(*forStmt->body);
      --indent_;
      return;
    }

    if (dynamic_cast<const BreakStmt *>(&statement) != nullptr) {
      line("BreakStmt");
      return;
    }

    if (dynamic_cast<const ContinueStmt *>(&statement) != nullptr) {
      line("ContinueStmt");
      return;
    }

    if (const auto *gotoStmt = dynamic_cast<const GotoStmt *>(&statement)) {
      line("GotoStmt label=" + gotoStmt->label);
      return;
    }

    if (const auto *labelStmt = dynamic_cast<const LabelStmt *>(&statement)) {
      line("LabelStmt label=" + labelStmt->label);
      if (labelStmt->statement) {
        ++indent_;
        dump(*labelStmt->statement);
        --indent_;
      }
      return;
    }

    if (dynamic_cast<const EmptyStmt *>(&statement) != nullptr) {
      line("EmptyStmt");
      return;
    }

    if (const auto *throwStmt = dynamic_cast<const ThrowStmt *>(&statement)) {
      line("ThrowStmt");
      ++indent_;
      dump(*throwStmt->value);
      --indent_;
      return;
    }

    if (const auto *tryCatch = dynamic_cast<const TryCatchStmt *>(&statement)) {
      std::string text = "TryCatchStmt error=" + tryCatch->errorName;
      if (!tryCatch->errorLength.empty()) {
        text += " length=" + tryCatch->errorLength;
      }
      if (!tryCatch->errorTemplateName.empty()) {
        text += " template=" + tryCatch->errorTemplateName;
      }
      line(text);
      ++indent_;
      dump(*tryCatch->tryBlock);
      dump(*tryCatch->catchBlock);
      --indent_;
      return;
    }

    if (const auto *setStmt = dynamic_cast<const SetStmt *>(&statement)) {
      line("SetStmt template=" + setStmt->templateName);
      ++indent_;
      dump(*setStmt->target);
      --indent_;
    }
  }

  void dump(const Expr &expression) {
    if (const auto *identifier =
            dynamic_cast<const IdentifierExpr *>(&expression)) {
      line("IdentifierExpr name=" + identifier->name);
      return;
    }

    if (const auto *integer =
            dynamic_cast<const IntegerLiteral *>(&expression)) {
      line("IntegerLiteral value=" + integer->value);
      return;
    }

    if (const auto *string = dynamic_cast<const StringLiteral *>(&expression)) {
      line("StringLiteral value=" + string->value);
      return;
    }

    if (const auto *character =
            dynamic_cast<const CharLiteral *>(&expression)) {
      line("CharLiteral value=" + character->value);
      return;
    }

    if (const auto *floating =
            dynamic_cast<const FloatLiteral *>(&expression)) {
      line("FloatLiteral value=" + floating->value);
      return;
    }

    if (const auto *boolean = dynamic_cast<const BoolLiteral *>(&expression)) {
      line(std::string("BoolLiteral value=") +
           (boolean->value ? "true" : "false"));
      return;
    }

    if (const auto *unary = dynamic_cast<const UnaryExpr *>(&expression)) {
      line("UnaryExpr op=" + unary->op);
      ++indent_;
      dump(*unary->operand);
      --indent_;
      return;
    }

    if (const auto *binary = dynamic_cast<const BinaryExpr *>(&expression)) {
      line("BinaryExpr op=" + binary->op);
      ++indent_;
      dump(*binary->left);
      dump(*binary->right);
      --indent_;
      return;
    }

    if (const auto *ternary = dynamic_cast<const TernaryExpr *>(&expression)) {
      line("TernaryExpr");
      ++indent_;
      dump(*ternary->condition);
      dump(*ternary->thenExpr);
      dump(*ternary->elseExpr);
      --indent_;
      return;
    }

    if (const auto *unsignedExpr =
            dynamic_cast<const UnsignedExpr *>(&expression)) {
      line("UnsignedExpr");
      ++indent_;
      dump(*unsignedExpr->operand);
      --indent_;
      return;
    }

    if (const auto *cast =
            dynamic_cast<const IntegerCastExpr *>(&expression)) {
      line("IntegerCastExpr bytes=" + std::to_string(cast->byteLength) +
           " signed=" + (cast->isSigned ? "true" : "false"));
      ++indent_;
      dump(*cast->operand);
      --indent_;
      return;
    }

    if (const auto *asExpr = dynamic_cast<const AsExpr *>(&expression)) {
      line("AsExpr template=" + asExpr->templateName);
      ++indent_;
      dump(*asExpr->operand);
      --indent_;
      return;
    }

    if (const auto *index = dynamic_cast<const IndexExpr *>(&expression)) {
      line("IndexExpr");
      ++indent_;
      dump(*index->base);
      dump(*index->index);
      --indent_;
      return;
    }

    if (const auto *slice = dynamic_cast<const SliceExpr *>(&expression)) {
      line(std::string("SliceExpr mode=") +
           (slice->lengthMode ? "length" : "end"));
      ++indent_;
      dump(*slice->base);
      dump(*slice->start);
      dump(*slice->end);
      --indent_;
      return;
    }

    if (const auto *member = dynamic_cast<const MemberExpr *>(&expression)) {
      line("MemberExpr member=" + member->member);
      ++indent_;
      dump(*member->base);
      --indent_;
      return;
    }

    if (const auto *sizeofExpr =
            dynamic_cast<const SizeofExpr *>(&expression)) {
      line("SizeofExpr name=" + sizeofExpr->name);
      return;
    }

    if (const auto *deref = dynamic_cast<const DerefExpr *>(&expression)) {
      line("DerefExpr length=" + deref->length);
      ++indent_;
      dump(*deref->address);
      --indent_;
      return;
    }

    if (const auto *call = dynamic_cast<const CallExpr *>(&expression)) {
      line("CallExpr callee=" + call->callee);
      ++indent_;
      for (const auto &argument : call->arguments) {
        dump(*argument);
      }
      --indent_;
      return;
    }

    if (const auto *call = dynamic_cast<const MethodCallExpr *>(&expression)) {
      line("MethodCallExpr method=" + call->method);
      ++indent_;
      dump(*call->receiver);
      for (const auto &argument : call->arguments) {
        dump(*argument);
      }
      --indent_;
      return;
    }

    if (const auto *assignment =
            dynamic_cast<const AssignmentExpr *>(&expression)) {
      line("AssignmentExpr");
      ++indent_;
      for (const auto &target : assignment->targets) {
        if (const auto *identifier =
                dynamic_cast<const IdentifierExpr *>(target.target.get())) {
          line("AssignmentTarget target=" + identifier->name +
               " op=" + target.op);
        } else {
          line("AssignmentTarget target=<expr> op=" + target.op);
          ++indent_;
          dump(*target.target);
          --indent_;
        }
      }
      for (const auto &value : assignment->values) {
        dump(*value);
      }
      --indent_;
    }
  }

  void line(std::string_view text) {
    for (int i = 0; i < indent_; ++i) {
      out_ << "  ";
    }
    out_ << text << '\n';
  }

  std::ostream &out_;
  int indent_ = 0;
};

} // namespace

IdentifierExpr::IdentifierExpr(std::string name) : name(std::move(name)) {}

IntegerLiteral::IntegerLiteral(std::string value) : value(std::move(value)) {}

StringLiteral::StringLiteral(std::string value) : value(std::move(value)) {}

CharLiteral::CharLiteral(std::string value) : value(std::move(value)) {}

FloatLiteral::FloatLiteral(std::string value) : value(std::move(value)) {}

BoolLiteral::BoolLiteral(bool value) : value(value) {}

UnaryExpr::UnaryExpr(std::string op, std::unique_ptr<Expr> operand)
    : op(std::move(op)), operand(std::move(operand)) {}

BinaryExpr::BinaryExpr(std::unique_ptr<Expr> left, std::string op,
                       std::unique_ptr<Expr> right)
    : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}

TernaryExpr::TernaryExpr(std::unique_ptr<Expr> condition,
                         std::unique_ptr<Expr> thenExpr,
                         std::unique_ptr<Expr> elseExpr)
    : condition(std::move(condition)), thenExpr(std::move(thenExpr)),
      elseExpr(std::move(elseExpr)) {}

UnsignedExpr::UnsignedExpr(std::unique_ptr<Expr> operand,
                           std::size_t byteLength)
    : operand(std::move(operand)), byteLength(byteLength) {}

IntegerCastExpr::IntegerCastExpr(std::unique_ptr<Expr> operand,
                                 std::size_t byteLength, bool isSigned)
    : operand(std::move(operand)), byteLength(byteLength), isSigned(isSigned) {}

AsExpr::AsExpr(std::unique_ptr<Expr> operand, std::string templateName)
    : operand(std::move(operand)), templateName(std::move(templateName)) {}

IndexExpr::IndexExpr(std::unique_ptr<Expr> base, std::unique_ptr<Expr> index)
    : base(std::move(base)), index(std::move(index)) {}

SliceExpr::SliceExpr(std::unique_ptr<Expr> base, std::unique_ptr<Expr> start,
                     std::unique_ptr<Expr> end, bool lengthMode)
    : base(std::move(base)), start(std::move(start)), end(std::move(end)),
      lengthMode(lengthMode) {}

MemberExpr::MemberExpr(std::unique_ptr<Expr> base, std::string member)
    : base(std::move(base)), member(std::move(member)) {}

SizeofExpr::SizeofExpr(std::string name) : name(std::move(name)) {}

DerefExpr::DerefExpr(std::string length, std::unique_ptr<Expr> address)
    : length(std::move(length)), address(std::move(address)) {}

CallExpr::CallExpr(std::string callee,
                   std::vector<std::unique_ptr<Expr>> arguments)
    : callee(std::move(callee)), arguments(std::move(arguments)) {}

MethodCallExpr::MethodCallExpr(
    std::unique_ptr<Expr> receiver, std::string method,
    std::vector<std::unique_ptr<Expr>> arguments)
    : receiver(std::move(receiver)), method(std::move(method)),
      arguments(std::move(arguments)) {}

AssignmentTarget::AssignmentTarget(std::unique_ptr<Expr> target, std::string op)
    : target(std::move(target)), op(std::move(op)) {}

AssignmentExpr::AssignmentExpr(std::vector<AssignmentTarget> targets,
                               std::vector<std::unique_ptr<Expr>> values)
    : targets(std::move(targets)), values(std::move(values)) {}

AssignmentExpr::AssignmentExpr(std::unique_ptr<Expr> target, std::string op,
                               std::unique_ptr<Expr> value) {
  targets.emplace_back(std::move(target), std::move(op));
  values.push_back(std::move(value));
}

NewDecl::NewDecl(std::string name, std::string length)
    : name(std::move(name)), length(std::move(length)) {}

DeclItem::DeclItem(std::string name, std::string length,
                   std::string assignmentOp, std::unique_ptr<Expr> initializer,
                   std::string templateName)
    : name(std::move(name)), length(std::move(length)),
      assignmentOp(std::move(assignmentOp)),
      initializer(std::move(initializer)),
      templateName(std::move(templateName)) {}

VarDeclStmt::VarDeclStmt(std::string storage, std::vector<DeclItem> items)
    : storage(std::move(storage)), items(std::move(items)) {}

AssignStmt::AssignStmt(std::string target, std::string op,
                       std::unique_ptr<Expr> value)
    : target(std::move(target)),
      targetExpr(std::make_unique<IdentifierExpr>(this->target)),
      op(std::move(op)), value(std::move(value)) {
  assignment = std::make_unique<AssignmentExpr>(std::move(targetExpr), this->op,
                                                std::move(this->value));
}

AssignStmt::AssignStmt(std::unique_ptr<Expr> targetExpr, std::string op,
                       std::unique_ptr<Expr> value)
    : targetExpr(std::move(targetExpr)), op(std::move(op)),
      value(std::move(value)) {
  if (const auto *identifier =
          dynamic_cast<const IdentifierExpr *>(this->targetExpr.get())) {
    target = identifier->name;
  }
  assignment = std::make_unique<AssignmentExpr>(
      std::move(this->targetExpr), this->op, std::move(this->value));
  targetExpr = nullptr;
  value = nullptr;
}

AssignStmt::AssignStmt(std::unique_ptr<AssignmentExpr> assignment)
    : assignment(std::move(assignment)) {
  if (!this->assignment || this->assignment->targets.empty() ||
      this->assignment->values.empty()) {
    return;
  }
  const auto &firstTarget = this->assignment->targets.front();
  if (const auto *identifier =
          dynamic_cast<const IdentifierExpr *>(firstTarget.target.get())) {
    target = identifier->name;
  }
  op = firstTarget.op;
}

ExprStmt::ExprStmt(std::unique_ptr<Expr> expression)
    : expression(std::move(expression)) {}

ReturnStmt::ReturnStmt(std::unique_ptr<Expr> value) : value(std::move(value)) {
  values.push_back(std::move(this->value));
}

ReturnStmt::ReturnStmt(std::vector<std::unique_ptr<Expr>> values)
    : values(std::move(values)) {}

IfStmt::IfStmt(std::unique_ptr<Expr> condition,
               std::unique_ptr<BlockStmt> thenBlock,
               std::unique_ptr<BlockStmt> elseBlock)
    : condition(std::move(condition)), thenBlock(std::move(thenBlock)),
      elseBlock(std::move(elseBlock)) {}

WhileStmt::WhileStmt(std::unique_ptr<Expr> condition,
                     std::unique_ptr<BlockStmt> body)
    : condition(std::move(condition)), body(std::move(body)) {}

ForStmt::ForStmt(std::unique_ptr<Stmt> init, std::unique_ptr<Expr> condition,
                 std::vector<std::unique_ptr<Expr>> post,
                 std::unique_ptr<BlockStmt> body)
    : init(std::move(init)), condition(std::move(condition)),
      post(std::move(post)), body(std::move(body)) {}

GotoStmt::GotoStmt(std::string label) : label(std::move(label)) {}

LabelStmt::LabelStmt(std::string label, std::unique_ptr<Stmt> statement)
    : label(std::move(label)), statement(std::move(statement)) {}

ThrowStmt::ThrowStmt(std::unique_ptr<Expr> value) : value(std::move(value)) {}

TryCatchStmt::TryCatchStmt(std::unique_ptr<BlockStmt> tryBlock,
                           std::string errorName, std::string errorLength,
                           std::string errorTemplateName,
                           std::unique_ptr<BlockStmt> catchBlock)
    : tryBlock(std::move(tryBlock)), errorName(std::move(errorName)),
      errorLength(std::move(errorLength)),
      errorTemplateName(std::move(errorTemplateName)),
      catchBlock(std::move(catchBlock)) {}

SetStmt::SetStmt(std::unique_ptr<Expr> target, std::string templateName)
    : target(std::move(target)), templateName(std::move(templateName)) {}

BlockStmt::BlockStmt(std::vector<std::unique_ptr<Stmt>> statements)
    : statements(std::move(statements)) {}

Param::Param(std::string name, std::string length, std::string templateName,
             bool isMutable)
    : name(std::move(name)), length(std::move(length)),
      templateName(std::move(templateName)), isMutable(isMutable) {}

ReturnItem::ReturnItem(std::string name, std::string length,
                       std::string templateName)
    : name(std::move(name)), length(std::move(length)),
      templateName(std::move(templateName)) {}

EffectItem::EffectItem(std::string name, std::string object, std::string range)
    : name(std::move(name)), object(std::move(object)), range(std::move(range)) {}

FunctionDecl::FunctionDecl(std::string name, std::vector<Param> params,
                           std::vector<ReturnItem> returns,
                           std::unique_ptr<BlockStmt> body,
                           std::optional<EffectClause> effects)
    : name(std::move(name)), params(std::move(params)),
      returns(std::move(returns)), body(std::move(body)),
      effects(std::move(effects)) {}

GlobalNewDecl::GlobalNewDecl(std::string name, std::string length,
                             std::string templateName, std::string assignmentOp,
                             std::unique_ptr<Expr> initializer)
    : name(std::move(name)), length(std::move(length)),
      templateName(std::move(templateName)),
      assignmentOp(std::move(assignmentOp)),
      initializer(std::move(initializer)) {}

ExternVarDecl::ExternVarDecl(std::string name, std::string length,
                             std::string templateName)
    : name(std::move(name)), length(std::move(length)),
      templateName(std::move(templateName)) {}

ExternFunctionDecl::ExternFunctionDecl(std::string name,
                                       std::vector<Param> params,
                                       std::vector<ReturnItem> returns,
                                       std::optional<EffectClause> effects)
    : name(std::move(name)), params(std::move(params)),
      returns(std::move(returns)), effects(std::move(effects)) {}

StructMember::StructMember(std::string name, std::string length)
    : name(std::move(name)), length(std::move(length)) {}

StructDecl::StructDecl(std::string name, std::vector<StructMember> members)
    : name(std::move(name)), members(std::move(members)) {}

TemplateMember::TemplateMember(std::string name, std::string length,
                               std::string templateName)
    : name(std::move(name)), length(std::move(length)),
      templateName(std::move(templateName)) {}

TemplateDecl::TemplateDecl(std::string name,
                           std::vector<TemplateMember> members)
    : name(std::move(name)), members(std::move(members)) {}

ImplOpParam::ImplOpParam(std::string name, std::string templateName,
                         bool isMutable)
    : name(std::move(name)), templateName(std::move(templateName)),
      isMutable(isMutable) {}

ImplOpDecl::ImplOpDecl(std::string op, std::vector<ImplOpParam> params,
                       std::vector<ReturnItem> returns,
                       std::unique_ptr<BlockStmt> body,
                       std::optional<EffectClause> effects)
    : op(std::move(op)), params(std::move(params)), returns(std::move(returns)),
      body(std::move(body)), effects(std::move(effects)) {}

ImplDecl::ImplDecl(std::string name, std::vector<ImplOpDecl> ops,
                   std::vector<std::unique_ptr<FunctionDecl>> methods)
    : name(std::move(name)), ops(std::move(ops)), methods(std::move(methods)) {}

bool ImplDecl::containsOp() const { return !ops.empty(); }

TranslationUnit::TranslationUnit(
    std::vector<std::unique_ptr<FunctionDecl>> functions)
    : TranslationUnit([&functions] {
        std::vector<std::unique_ptr<TopLevelDecl>> declarations;
        declarations.reserve(functions.size());
        for (auto &function : functions) {
          declarations.push_back(std::move(function));
        }
        return declarations;
      }()) {}

TranslationUnit::TranslationUnit(
    std::vector<std::unique_ptr<TopLevelDecl>> declarations)
    : declarations(std::move(declarations)) {
  for (const auto &declaration : this->declarations) {
    if (auto *function = dynamic_cast<FunctionDecl *>(declaration.get())) {
      functions.push_back(function);
      continue;
    }
    if (auto *globalNew = dynamic_cast<GlobalNewDecl *>(declaration.get())) {
      globalNews.push_back(globalNew);
      continue;
    }
    if (auto *externVariable =
            dynamic_cast<ExternVarDecl *>(declaration.get())) {
      externVariables.push_back(externVariable);
      continue;
    }
    if (auto *externFunction =
            dynamic_cast<ExternFunctionDecl *>(declaration.get())) {
      externFunctions.push_back(externFunction);
      continue;
    }
    if (auto *structDecl = dynamic_cast<StructDecl *>(declaration.get())) {
      structs.push_back(structDecl);
      continue;
    }
    if (auto *templateDecl = dynamic_cast<TemplateDecl *>(declaration.get())) {
      templates.push_back(templateDecl);
      continue;
    }
    if (auto *implDecl = dynamic_cast<ImplDecl *>(declaration.get())) {
      impls.push_back(implDecl);
    }
  }
}

void dump(const TranslationUnit &unit, std::ostream &out) {
  AstDumper(out).dump(unit);
}

std::string dumpToString(const TranslationUnit &unit) {
  std::ostringstream out;
  dump(unit, out);
  return out.str();
}

} // namespace hitsimple::ast
