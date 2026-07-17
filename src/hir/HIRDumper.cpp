#include "hitsimple/hir/HIR.h"

#include <sstream>
#include <utility>

namespace hitsimple::hir {
namespace {

class HirDumper {
public:
  explicit HirDumper(std::ostream &out) : out_(out) {}

  void dump(const TranslationUnit &unit) {
    line("TranslationUnit");
    ++indent_;
    for (const auto &global : unit.globals) {
      dump(global);
    }
    for (const auto &structure : unit.structs) {
      dump(structure);
    }
    for (const auto &viewTemplate : unit.viewTemplates) {
      dump(viewTemplate);
    }
    for (const auto &op : unit.implOps) {
      dump(op);
    }
    for (const auto &function : unit.externFunctions) {
      dump(function);
    }
    if (unit.globalInit) {
      line("GlobalInit");
      ++indent_;
      dump(*unit.globalInit);
      --indent_;
    }
    for (const auto &function : unit.functions) {
      dump(*function);
    }
    --indent_;
  }

private:
  void dump(const GlobalMemory &global) {
    line("GlobalMemory name=" + global.name + " binding=" + global.bindingName +
         " bytes=" + std::to_string(global.byteLength) +
         (global.isExtern ? " extern=true" : "") +
         (global.isExtern ? "" : " linkage=" +
                                std::string(toString(global.linkage))));
  }

  void dump(const ExternFunction &function) {
    line("ExternFunction name=" + function.name +
         (function.abiSignature && function.abiSignature->isCCompatibility
              ? " c_abi=true"
              : ""));
    ++indent_;
    for (const auto byteLength : function.parameterByteLengths) {
      line("Param bytes=" + std::to_string(byteLength));
    }
    for (const auto byteLength : function.returnByteLengths) {
      line("Return bytes=" + std::to_string(byteLength));
    }
    --indent_;
  }

  void dump(const StructLayout &structure) {
    line("StructLayout name=" + structure.name +
         " bytes=" + std::to_string(structure.byteLength));
    ++indent_;
    for (const auto &member : structure.members) {
      line("Member name=" + member.name +
           " offset=" + std::to_string(member.offset) +
           " bytes=" + std::to_string(member.byteLength));
    }
    --indent_;
  }

  void dump(const ViewTemplate &viewTemplate) {
    line("ViewTemplate name=" + viewTemplate.name +
         " bytes=" + std::to_string(viewTemplate.byteLength));
    ++indent_;
    for (const auto &member : viewTemplate.members) {
      line("ViewMember name=" + member.name +
           " offset=" + std::to_string(member.offset) +
           " bytes=" + std::to_string(member.byteLength) +
           (member.templateName.empty() ? "" : " template=" + member.templateName));
    }
    --indent_;
  }

  void dump(const ImplOpBinding &op) {
    line("ImplOp template=" + op.implTemplate + " op=" + op.op +
         " symbol=" + op.symbolName);
    ++indent_;
    for (const auto &param : op.params) {
      line("Param name=" + param.name + " template=" + param.templateName +
           (param.isMutable ? " mutable=true" : ""));
    }
    for (const auto byteLength : op.returnByteLengths) {
      line("Return bytes=" + std::to_string(byteLength));
    }
    --indent_;
  }

  void dump(const Function &function) {
    line("Function name=" + function.name + " linkage=" +
         std::string(toString(function.linkage)) +
         (function.abiSignature && function.abiSignature->isCCompatibility
              ? " c_abi=true"
              : "") +
         (function.usesViewAbi ? " view_abi=true" : ""));
    ++indent_;
    for (const auto &parameter : function.parameters) {
      line("Param name=" + parameter.name + " binding=" +
           parameter.bindingName + " bytes=" +
           std::to_string(parameter.byteLength));
    }
    for (const auto byteLength : function.returnByteLengths) {
      line("ReturnSignature bytes=" + std::to_string(byteLength));
    }
    dump(*function.body);
    --indent_;
  }

  void dump(const Block &block) {
    line("Block");
    ++indent_;
    for (const auto &statement : block.statements) {
      dump(*statement);
    }
    --indent_;
  }

  void dump(const Stmt &statement) {
    if (const auto *list = dynamic_cast<const StatementList *>(&statement)) {
      for (const auto &item : list->statements) {
        dump(*item);
      }
      return;
    }

    if (const auto *local = dynamic_cast<const LocalMemory *>(&statement)) {
      line("LocalMemory name=" + local->name + " binding=" +
           local->bindingName + " bytes=" + std::to_string(local->byteLength) +
           " storage=" + std::string(toString(local->storage)) +
           (local->templateName.empty() ? "" : " template=" + local->templateName));
      return;
    }

    if (const auto *store = dynamic_cast<const IntegerStore *>(&statement)) {
      line("IntegerStore target=" + store->target +
           " binding=" + store->bindingName +
           " bytes=" + std::to_string(store->targetByteLength) +
           " storage=" + std::string(toString(store->storage)) +
           (store->offset == 0 ? "" : " offset=" + std::to_string(store->offset)));
      ++indent_;
      dump(*store->value);
      --indent_;
      return;
    }

    if (const auto *store = dynamic_cast<const FloatStore *>(&statement)) {
      line("FloatStore target=" + store->target +
           " binding=" + store->bindingName +
           " bytes=" + std::to_string(store->targetByteLength) +
           " storage=" + std::string(toString(store->storage)) +
           (store->offset == 0 ? "" : " offset=" + std::to_string(store->offset)));
      ++indent_;
      dump(*store->value);
      --indent_;
      return;
    }

    if (const auto *store = dynamic_cast<const StringStore *>(&statement)) {
      line("StringStore target=" + store->target +
           " binding=" + store->bindingName +
           " bytes=" + std::to_string(store->targetByteLength) + " storage=" +
           std::string(toString(store->storage)) +
           (store->offset == 0 ? "" : " offset=" + std::to_string(store->offset)) +
           " value=" + store->value);
      return;
    }

    if (const auto *store = dynamic_cast<const StringCopyStore *>(&statement)) {
      line("StringCopyStore target=" + store->target +
           " binding=" + store->bindingName +
           " bytes=" + std::to_string(store->targetByteLength) +
           " storage=" + std::string(toString(store->targetStorage)) +
           (store->targetOffset == 0
                ? ""
                : " offset=" + std::to_string(store->targetOffset)) +
           " source=" + store->source +
           " sourceBinding=" + store->sourceBindingName +
           " sourceBytes=" + std::to_string(store->sourceByteLength) +
           (store->sourceOffset == 0
                ? ""
                : " sourceOffset=" + std::to_string(store->sourceOffset)));
      return;
    }

    if (const auto *store = dynamic_cast<const BoolStore *>(&statement)) {
      line("BoolStore target=" + store->target +
           " binding=" + store->bindingName +
           " bytes=" + std::to_string(store->targetByteLength) +
           " storage=" + std::string(toString(store->storage)) +
           (store->offset == 0 ? "" : " offset=" + std::to_string(store->offset)));
      ++indent_;
      dump(*store->value);
      --indent_;
      return;
    }

    if (const auto *store = dynamic_cast<const PointerStore *>(&statement)) {
      line("PointerStore bytes=" + std::to_string(store->targetByteLength));
      ++indent_;
      line("Address");
      ++indent_;
      dump(*store->address);
      --indent_;
      line("Value");
      ++indent_;
      dump(*store->value);
      --indent_;
      --indent_;
      return;
    }

    if (const auto *call = dynamic_cast<const Call *>(&statement)) {
      line("Call callee=" + call->callee);
      ++indent_;
      for (const auto &argument : call->arguments) {
        dump(*argument);
      }
      --indent_;
      return;
    }

    if (const auto *call = dynamic_cast<const UserTemplateOpCall *>(&statement)) {
      line("UserTemplateOpCall callee=" + call->callee + " resultBytes=" +
           std::to_string(call->resultByteLength));
      ++indent_;
      for (const auto &argument : call->arguments) {
        dump(*argument);
      }
      --indent_;
      return;
    }

    if (const auto *call =
            dynamic_cast<const UserTemplateFormatCall *>(&statement)) {
      line("UserTemplateFormatCall callee=" + call->callee + " sink=" +
           std::string(toString(call->sink)) + " resultBytes=" +
           std::to_string(call->resultByteLength));
      ++indent_;
      line("Value");
      ++indent_;
      dump(*call->value);
      --indent_;
      if (call->file) {
        line("File");
        ++indent_;
        dump(*call->file);
        --indent_;
      }
      --indent_;
      return;
    }

    if (const auto *call =
            dynamic_cast<const MultiReturnCallStore *>(&statement)) {
      line("MultiReturnCallStore callee=" + call->callee);
      ++indent_;
      for (const auto &argument : call->arguments) {
        dump(*argument);
      }
      for (const auto &target : call->targets) {
        line("Target name=" + target.name + " binding=" +
             target.bindingName + " bytes=" +
             std::to_string(target.byteLength) + " storage=" +
             std::string(toString(target.storage)) + " index=" +
             std::to_string(target.returnIndex));
      }
      --indent_;
      return;
    }

    if (const auto *call = dynamic_cast<const InputCallStore *>(&statement)) {
      line("InputCallStore callee=" + call->callee);
      ++indent_;
      if (call->file) {
        dump(*call->file);
      }
      dump(*call->format);
      for (const auto &target : call->countTargets) {
        line("CountTarget name=" + target.name + " binding=" +
             target.bindingName + " bytes=" +
             std::to_string(target.byteLength) + " storage=" +
             std::string(toString(target.storage)) + " offset=" +
             std::to_string(target.offset) + " template=" +
             target.templateName);
      }
      for (const auto &target : call->scanTargets) {
        line("ScanTarget name=" + target.name + " binding=" +
             target.bindingName + " bytes=" +
             std::to_string(target.byteLength) + " storage=" +
             std::string(toString(target.storage)) + " offset=" +
             std::to_string(target.offset));
      }
      --indent_;
      return;
    }

    if (const auto *ret = dynamic_cast<const Return *>(&statement)) {
      line("Return");
      ++indent_;
      for (const auto &value : ret->values) {
        dump(*value);
      }
      --indent_;
      return;
    }

    if (const auto *ifStmt = dynamic_cast<const If *>(&statement)) {
      line("If");
      ++indent_;
      dump(*ifStmt->condition);
      dump(*ifStmt->thenBlock);
      if (ifStmt->elseBlock) {
        dump(*ifStmt->elseBlock);
      }
      --indent_;
      return;
    }

    if (const auto *whileStmt = dynamic_cast<const While *>(&statement)) {
      line("While");
      ++indent_;
      dump(*whileStmt->condition);
      dump(*whileStmt->body);
      --indent_;
      return;
    }

    if (const auto *forStmt = dynamic_cast<const For *>(&statement)) {
      line("For");
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

    if (dynamic_cast<const Break *>(&statement) != nullptr) {
      line("Break");
      return;
    }

    if (dynamic_cast<const Continue *>(&statement) != nullptr) {
      line("Continue");
      return;
    }

    if (const auto *gotoStmt = dynamic_cast<const Goto *>(&statement)) {
      line("Goto label=" + gotoStmt->label);
      return;
    }

    if (const auto *label = dynamic_cast<const Label *>(&statement)) {
      line("Label label=" + label->label);
      ++indent_;
      dump(*label->statement);
      --indent_;
      return;
    }

    if (const auto *throwStmt = dynamic_cast<const Throw *>(&statement)) {
      const auto sourceTemplate = throwStmt->sourceTemplateName.empty()
                                      ? "none"
                                      : throwStmt->sourceTemplateName;
      const auto targetTemplate = throwStmt->targetTemplateName.empty()
                                      ? "none"
                                      : throwStmt->targetTemplateName;
      line("Throw source_template=" + sourceTemplate +
           " source_bytes=" + std::to_string(throwStmt->sourceByteLength) +
           " target_template=" + targetTemplate +
           " target_bytes=" + std::to_string(throwStmt->targetByteLength));
      if (throwStmt->delivery) {
        ++indent_;
        dump(*throwStmt->delivery);
        --indent_;
      }
      return;
    }

    if (const auto *tryCatch = dynamic_cast<const TryCatch *>(&statement)) {
      line("TryCatch error=" + tryCatch->errorName +
           " binding=" + tryCatch->errorBindingName +
           " template=" + (tryCatch->errorTemplateName.empty()
                                 ? "none"
                                 : tryCatch->errorTemplateName) +
           " bytes=" + std::to_string(tryCatch->errorByteLength));
      ++indent_;
      dump(*tryCatch->tryBlock);
      dump(*tryCatch->catchBlock);
      --indent_;
      return;
    }
  }

  void dump(const Expr &expression) {
    if (const auto *integer =
            dynamic_cast<const IntegerLiteral *>(&expression)) {
      line("IntegerLiteral value=" + integer->value +
           " bytes=" + std::to_string(integer->byteLength));
      return;
    }

    if (const auto *string = dynamic_cast<const StringLiteral *>(&expression)) {
      line("StringLiteral value=" + string->value +
           " bytes=" + std::to_string(string->byteLength));
      return;
    }

    if (const auto *floating =
            dynamic_cast<const FloatLiteral *>(&expression)) {
      line("FloatLiteral value=" + floating->value +
           " bytes=" + std::to_string(floating->byteLength));
      return;
    }

    if (const auto *variable = dynamic_cast<const VariableRef *>(&expression)) {
      line("VariableRef name=" + variable->name +
           " binding=" + variable->bindingName +
           " bytes=" + std::to_string(variable->byteLength) +
           " storage=" + std::string(toString(variable->storage)) +
           (variable->offset == 0 ? "" : " offset=" + std::to_string(variable->offset)));
      return;
    }

    if (const auto *address = dynamic_cast<const AddressOfExpr *>(&expression)) {
      line("AddressOf name=" + address->name +
           " binding=" + address->bindingName +
           " targetBytes=" + std::to_string(address->targetByteLength) +
           " storage=" + std::string(toString(address->storage)) +
           (address->offset == 0 ? "" : " offset=" + std::to_string(address->offset)) +
           " bytes=" + std::to_string(address->byteLength));
      return;
    }

    if (const auto *deref = dynamic_cast<const DerefExpr *>(&expression)) {
      line("DerefExpr bytes=" + std::to_string(deref->byteLength));
      ++indent_;
      dump(*deref->address);
      --indent_;
      return;
    }

    if (const auto *binary = dynamic_cast<const BinaryExpr *>(&expression)) {
      line("BinaryExpr op=" + binary->op +
           " bytes=" + std::to_string(binary->byteLength));
      ++indent_;
      dump(*binary->left);
      dump(*binary->right);
      --indent_;
      return;
    }

    if (const auto *unary = dynamic_cast<const UnaryExpr *>(&expression)) {
      line("UnaryExpr op=" + unary->op +
           " bytes=" + std::to_string(unary->byteLength));
      ++indent_;
      dump(*unary->operand);
      --indent_;
      return;
    }

    if (const auto *ternary = dynamic_cast<const TernaryExpr *>(&expression)) {
      line("TernaryExpr bytes=" + std::to_string(ternary->byteLength));
      ++indent_;
      dump(*ternary->condition);
      dump(*ternary->thenExpr);
      dump(*ternary->elseExpr);
      --indent_;
      return;
    }

    if (const auto *unsignedExpr =
            dynamic_cast<const UnsignedExpr *>(&expression)) {
      line("UnsignedExpr bytes=" + std::to_string(unsignedExpr->byteLength));
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

    if (const auto *view = dynamic_cast<const TemplateViewExpr *>(&expression)) {
      line("TemplateViewExpr template=" + view->templateName +
           " bytes=" + std::to_string(view->byteLength) +
           " addressable=" + (view->isAddressable ? "true" : "false"));
      ++indent_;
      dump(*view->operand);
      --indent_;
      return;
    }

    if (const auto *call =
            dynamic_cast<const UserTemplateOpCallExpr *>(&expression)) {
      line("UserTemplateOpCallExpr template=" + call->templateName +
           " bytes=" + std::to_string(call->byteLength) +
           " callee=" + call->callee);
      ++indent_;
      for (const auto &argument : call->arguments) {
        dump(*argument);
      }
      --indent_;
      return;
    }

    if (const auto *call =
            dynamic_cast<const UserTemplateFormatCallExpr *>(&expression)) {
      line("UserTemplateFormatCallExpr callee=" + call->callee + " sink=" +
           std::string(toString(call->sink)) + " bytes=" +
           std::to_string(call->byteLength));
      ++indent_;
      dump(*call->value);
      if (call->file) {
        dump(*call->file);
      }
      --indent_;
      return;
    }

    if (const auto *floating =
            dynamic_cast<const FloatBinaryExpr *>(&expression)) {
      line("FloatBinaryExpr op=" + floating->op +
           " bytes=" + std::to_string(floating->byteLength));
      ++indent_;
      dump(*floating->left);
      dump(*floating->right);
      --indent_;
      return;
    }

    if (const auto *comparison =
            dynamic_cast<const FloatCompareExpr *>(&expression)) {
      line("FloatCompareExpr op=" + comparison->op +
           " floatBytes=" + std::to_string(comparison->operandByteLength));
      ++indent_;
      dump(*comparison->left);
      dump(*comparison->right);
      --indent_;
      return;
    }

    if (const auto *conversion =
            dynamic_cast<const ToFloatExpr *>(&expression)) {
      line("ToFloatExpr bytes=" + std::to_string(conversion->byteLength));
      ++indent_;
      dump(*conversion->operand);
      --indent_;
      return;
    }

    if (const auto *conversion = dynamic_cast<const ToIntExpr *>(&expression)) {
      line("ToIntExpr floatBytes=" +
           std::to_string(conversion->floatByteLength) +
           " bytes=" + std::to_string(conversion->byteLength));
      ++indent_;
      dump(*conversion->operand);
      --indent_;
      return;
    }

    if (const auto *call = dynamic_cast<const CallExpr *>(&expression)) {
      line("CallExpr callee=" + call->callee +
           " bytes=" + std::to_string(call->byteLength) +
           (call->isFloating ? " floating=true" : ""));
      ++indent_;
      for (const auto &argument : call->arguments) {
        dump(*argument);
      }
      --indent_;
      return;
    }

    if (const auto *dynamic =
            dynamic_cast<const DynamicByteViewExpr *>(&expression)) {
      line("DynamicByteViewExpr op=" + std::string(toString(dynamic->operation)));
      ++indent_;
      dump(*dynamic->source);
      if (dynamic->runtimeLength) {
        dump(*dynamic->runtimeLength);
      }
      --indent_;
      return;
    }

    if (const auto *swap = dynamic_cast<const ByteSwapExpr *>(&expression)) {
      line("ByteSwapExpr bytes=" + std::to_string(swap->byteLength));
      ++indent_;
      dump(*swap->source);
      --indent_;
      return;
    }

    if (const auto *assignment =
            dynamic_cast<const AssignmentExpr *>(&expression)) {
      line("AssignmentExpr bytes=" + std::to_string(assignment->byteLength));
      ++indent_;
      for (const auto &store : assignment->stores) {
        dump(*store);
      }
      if (assignment->result) {
        dump(*assignment->result);
      }
      --indent_;
    }
  }

  void line(const std::string &text) {
    for (int i = 0; i < indent_; ++i) {
      out_ << "  ";
    }
    out_ << text << '\n';
  }

  std::ostream &out_;
  int indent_ = 0;
};

} // namespace

void dump(const TranslationUnit &unit, std::ostream &out) {
  HirDumper(out).dump(unit);
}

std::string dumpToString(const TranslationUnit &unit) {
  std::ostringstream out;
  dump(unit, out);
  return out.str();
}

} // namespace hitsimple::hir
