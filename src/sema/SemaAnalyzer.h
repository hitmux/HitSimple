#pragma once

#include "hitsimple/ast/AST.h"
#include "hitsimple/hir/HIR.h"
#include "hitsimple/sema/Sema.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hitsimple::sema {

struct Symbol {
  std::string name;
  std::string bindingName;
  std::size_t byteLength = 0;
  hir::MemoryStorage storage = hir::MemoryStorage::Local;
  std::string templateName;
};

struct StructMemberInfo {
  std::string name;
  std::size_t byteLength = 0;
  std::size_t offset = 0;
  std::string templateName;
};

struct StructInfo {
  std::string name;
  std::vector<StructMemberInfo> members;
  std::size_t byteLength = 0;
};

struct TemplateInfo {
  std::string name;
  std::vector<StructMemberInfo> members;
  std::size_t byteLength = 0;
};

struct MemoryReference {
  std::string name;
  std::string bindingName;
  std::size_t byteLength = 0;
  hir::MemoryStorage storage = hir::MemoryStorage::Local;
  std::size_t offset = 0;
  std::string templateName;
};

struct SliceLowering {
  std::unique_ptr<hir::Expr> address;
  std::size_t byteLength = 0;
};

struct AssignmentOperator {
  std::size_t byteLength = 0;
  char compoundOp = '\0';
};

struct AssignmentLowering {
  std::vector<std::unique_ptr<hir::Stmt>> stores;
  std::unique_ptr<hir::Expr> result;
  std::size_t byteLength = 0;
};

struct FixedViewAssignmentLowering {
  std::unique_ptr<hir::Expr> value;
  std::string sourceTemplateName;
  std::size_t sourceByteLength = 0;
  bool isFloating = false;
};

struct UserTemplateFormatCallLowering {
  std::string callee;
  std::unique_ptr<hir::Expr> value;
  hir::FormatOutputSink sink = hir::FormatOutputSink::Stdout;
  std::unique_ptr<hir::Expr> file;
  std::size_t resultByteLength = 4;
};

struct FunctionSignature {
  std::string name;
  std::vector<std::size_t> parameterByteLengths;
  std::vector<std::string> parameterTemplateNames;
  std::vector<bool> stringParameters;
  std::vector<std::size_t> returnByteLengths;
  std::vector<std::string> returnTemplateNames;
  std::vector<bool> returnHasExplicitUserTemplate;
  bool returnsKnown = false;
  bool returnsExplicit = false;
  bool isExtern = false;
  bool isCAbi = false;
  stdlib::BuiltinId builtin = stdlib::BuiltinId::None;
};

struct ImplOpInfo {
  const ast::ImplOpDecl *declaration = nullptr;
  std::string symbolName;
  std::string implTemplate;
  std::vector<std::size_t> returnByteLengths;
  std::vector<std::string> returnTemplateNames;
  std::vector<bool> returnHasExplicitUserTemplate;
};

struct ImplMethodInfo {
  const ast::FunctionDecl *declaration = nullptr;
  std::string implTemplate;
  std::string symbolName;
  std::vector<std::size_t> parameterByteLengths;
  std::vector<std::string> parameterTemplateNames;
  std::vector<std::size_t> returnByteLengths;
  std::vector<std::string> returnTemplateNames;
  std::string overloadKey;
};

struct MethodCallLowering {
  const ImplMethodInfo *method = nullptr;
  std::vector<std::unique_ptr<hir::Expr>> arguments;
};

enum class UserTemplateViewAssignmentCompatibility {
  Compatible,
  SourceIsNotUserTemplate,
  TemplateMismatch,
};

std::optional<hir::FunctionAbiSignature>
floatingAbiSignature(const FunctionSignature &signature);

std::optional<hir::FunctionAbiSignature>
cAbiSignature(const FunctionSignature &signature);

struct LabelInfo {
  std::size_t blockDepth = 0;
};

struct PendingGoto {
  std::string label;
  std::size_t blockDepth = 0;
};

struct CatchViewContract {
  std::string errorName;
  std::string errorBindingName;
  std::string templateName;
  std::size_t byteLength = 0;
};

class Analyzer {
public:
  AnalyzeResult analyze(const ast::TranslationUnit &unit,
                        const AnalyzeOptions &options);

private:
  class CurrentRangeGuard final {
  public:
    CurrentRangeGuard(Analyzer &analyzer, const ast::Node &node)
        : analyzer_(analyzer), previous_(std::move(analyzer.currentRange_)) {
      analyzer_.currentRange_ = node.range;
      hir::setActiveSourceRange(analyzer_.currentRange_);
    }

    ~CurrentRangeGuard() {
      analyzer_.currentRange_ = std::move(previous_);
      hir::setActiveSourceRange(analyzer_.currentRange_);
    }

    CurrentRangeGuard(const CurrentRangeGuard &) = delete;
    CurrentRangeGuard &operator=(const CurrentRangeGuard &) = delete;

  private:
    Analyzer &analyzer_;
    std::optional<diagnostic::SourceRange> previous_;
  };

  std::unique_ptr<hir::Function> analyze(const ast::FunctionDecl &function);
  std::unique_ptr<hir::Block> analyze(const ast::BlockStmt &block);
  std::unique_ptr<hir::Stmt> analyze(const ast::Stmt &statement);
  std::unique_ptr<hir::Stmt> analyze(const ast::NewDecl &decl);
  std::unique_ptr<hir::Stmt> analyze(const ast::VarDeclStmt &decl);
  std::unique_ptr<hir::Stmt> analyzeDeclItem(const ast::DeclItem &item,
                                             std::string_view storage);
  std::unique_ptr<hir::Stmt> analyzeStringInitializer(const ast::DeclItem &item,
                                                      const Symbol &target);
  std::unique_ptr<hir::Stmt> analyzeBoolInitializer(const ast::DeclItem &item,
                                                    const Symbol &target);
  std::unique_ptr<hir::Stmt> analyze(const ast::AssignStmt &assign);
  std::optional<AssignmentLowering>
  lowerAssignmentExpression(const ast::AssignmentExpr &assign);
  std::unique_ptr<hir::Stmt> lowerAssignmentTarget(
      const ast::AssignmentTarget &target, const ast::Expr &value,
      std::unique_ptr<hir::Expr> loweredValue);
  std::unique_ptr<hir::Stmt> lowerAssignmentTarget(
      const ast::Expr &target, std::string_view op, bool unsignedTarget,
      const ast::Expr &value, std::unique_ptr<hir::Expr> loweredValue,
      const MemoryReference *directTarget = nullptr,
      std::string_view lengthDiagnosticSubject = "right operand of '='",
      std::string_view targetLengthSubject = "target");
  std::optional<FixedViewAssignmentLowering> lowerFixedViewAssignment(
      const MemoryReference &target, const ast::Expr &value,
      std::unique_ptr<hir::Expr> loweredValue,
      std::string_view lengthDiagnosticSubject,
      std::string_view targetLengthSubject);
  std::unique_ptr<hir::Stmt> analyzeStringAssign(const ast::AssignStmt &assign,
                                                 const Symbol &target);
  std::unique_ptr<hir::Stmt> analyzeBoolAssign(const ast::AssignStmt &assign,
                                               const Symbol &target);
  std::unique_ptr<hir::Stmt>
  analyzeCompoundAssign(const ast::AssignStmt &assign, const Symbol &target,
                        const AssignmentOperator &assignmentOp);
  std::unique_ptr<hir::Stmt> analyze(const ast::ExprStmt &statement);
  std::unique_ptr<hir::Stmt> analyze(const ast::ReturnStmt &statement);
  std::unique_ptr<hir::Stmt> analyze(const ast::IfStmt &statement);
  std::unique_ptr<hir::Stmt> analyze(const ast::WhileStmt &statement);
  std::unique_ptr<hir::Stmt> analyze(const ast::ForStmt &statement);
  std::unique_ptr<hir::Stmt> analyzeBreak();
  std::unique_ptr<hir::Stmt> analyzeContinue();
  std::unique_ptr<hir::Stmt> analyze(const ast::GotoStmt &statement);
  std::unique_ptr<hir::Stmt> analyze(const ast::LabelStmt &statement);
  std::unique_ptr<hir::Stmt> analyze(const ast::ThrowStmt &statement);
  std::unique_ptr<hir::Stmt> analyze(const ast::TryCatchStmt &statement);
  std::unique_ptr<hir::Stmt> analyze(const ast::SetStmt &statement);
  std::unique_ptr<hir::Stmt> analyzeCall(const ast::CallExpr &call);
  std::unique_ptr<hir::Stmt> analyzeCallStatement(const ast::CallExpr &call);
  std::unique_ptr<hir::Stmt>
  analyzeTemplatePrintCall(const ast::CallExpr &call);
  std::optional<UserTemplateFormatCallLowering>
  lowerUserTemplateFormatCall(const ast::CallExpr &call,
                              stdlib::BuiltinId builtin,
                              std::string_view templateName,
                              std::size_t valueIndex);
  std::optional<AssignmentLowering>
  lowerInputLeftContext(const ast::AssignmentExpr &assign,
                        const ast::CallExpr &call);
  std::unique_ptr<hir::Stmt> analyzeExpressionStatementExpr(
      const ast::Expr &expression);
  std::unique_ptr<hir::Stmt> analyzeIncrementStatement(
      const ast::UnaryExpr &expression);

  std::unique_ptr<hir::Expr> analyze(const ast::Expr &expression);
  std::unique_ptr<hir::Expr> analyzeCallExpr(const ast::CallExpr &call);
  std::unique_ptr<hir::Expr> analyze(const ast::BinaryExpr &expression);
  std::unique_ptr<hir::Expr> analyze(const ast::UnaryExpr &expression);
  std::unique_ptr<hir::Expr> analyze(const ast::TernaryExpr &expression);
  std::unique_ptr<hir::Expr> analyze(const ast::UnsignedExpr &expression);
  std::unique_ptr<hir::Expr> analyze(const ast::IntegerCastExpr &expression);
  std::unique_ptr<hir::Expr> analyze(const ast::AssignmentExpr &expression);
  std::unique_ptr<hir::Expr> analyzeFloatOperand(const ast::Expr &expression,
                                                 std::size_t byteLength);
  hir::FormatArgKind formatArgumentKind(const ast::Expr &expression,
                                        const hir::Expr &lowered);

  void beginScope();
  void endScope();
  Symbol *lookup(std::string_view name);
  const Symbol *lookup(std::string_view name) const;
  bool declare(std::string_view name, std::size_t byteLength,
               hir::MemoryStorage storage, Symbol &out);
  bool declare(std::string_view name, std::size_t byteLength,
               hir::MemoryStorage storage, std::string templateName,
               Symbol &out, std::string bindingName = {});
  std::string makeBindingName(std::string_view name);
  bool collectStructLayouts(const ast::TranslationUnit &unit,
                            std::vector<hir::StructLayout> &layouts);
  bool collectViewTemplates(const ast::TranslationUnit &unit,
                            std::vector<hir::ViewTemplate> &viewTemplates);
  bool collectImplOps(const ast::TranslationUnit &unit,
                      std::vector<hir::ImplOpBinding> &implOps);
  bool lowerImplOpBodies(
      std::vector<std::unique_ptr<hir::Function>> &functions);
  bool collectImplMethods(const ast::TranslationUnit &unit);
  bool lowerImplMethodBodies(
      std::vector<std::unique_ptr<hir::Function>> &functions);
  std::optional<MethodCallLowering>
  lowerImplMethodCall(const ast::MethodCallExpr &call);
  std::unique_ptr<hir::Expr>
  analyzeMethodCallExpr(const ast::MethodCallExpr &call);
  const ImplMethodInfo *findImplMethod(
      std::string_view implTemplate, std::string_view methodName,
      const std::vector<std::string> &parameterTemplateNames) const;
  bool registerTopLevelName(std::string_view name);
  std::optional<std::size_t> parseDeclaredLength(std::string_view length,
                                                 std::string_view templateName);
  std::optional<std::size_t> templateByteLength(std::string_view templateName) const;
  std::optional<std::string> expressionTemplateName(const ast::Expr &expression);
  std::optional<std::string> operatorTemplateName(const ast::Expr &expression);
  UserTemplateViewAssignmentCompatibility
  userTemplateViewAssignmentCompatibility(
      std::string_view destinationTemplate, const ast::Expr &source);
  bool isHandleExpression(const ast::Expr &expression);
  bool isCStringExpression(const ast::Expr &expression);
  bool validateHandleCallArguments(const ast::CallExpr &call,
                                   const FunctionSignature &signature);
  bool validateBuiltinViewArguments(const ast::CallExpr &call,
                                    const FunctionSignature &signature);
  std::optional<MemoryReference> resolveMemoryReference(const ast::Expr &expr);
  std::optional<MemoryReference> resolveAddressableReference(
      const ast::Expr &expr);
  std::unique_ptr<hir::Expr> lowerIndexAddress(const ast::IndexExpr &expr);
  std::optional<SliceLowering> lowerSlice(const ast::SliceExpr &expr);
  std::optional<std::size_t> structIndex(const ast::Expr &expr) const;
  std::optional<std::size_t> inferByteLength(const ast::Expr &expression);
  bool collectFunctionSignatures(const ast::TranslationUnit &unit,
                                 std::vector<hir::ExternFunction> &externs);
  std::unique_ptr<hir::Block>
  lowerGlobalInitializers(const ast::TranslationUnit &unit);
  std::optional<std::vector<std::size_t>>
  parseReturnSignature(const std::vector<ast::ReturnItem> &returns,
                       std::string_view owner, bool cAbi = false);
  std::vector<std::unique_ptr<hir::Expr>>
  analyzeCallArguments(const ast::CallExpr &call,
                       const FunctionSignature &signature);
  bool isStandardHeaderIncluded(stdlib::StandardHeader header) const;
  std::optional<stdlib::BuiltinId>
  builtinForCall(const ast::CallExpr &call) const;
  std::uint16_t builtinOverloadIndex(const ast::CallExpr &call,
                                     stdlib::BuiltinId builtin);
  bool rejectUnavailableStandardBuiltin(const ast::CallExpr &call);
  bool registerReturnLengths(const std::vector<std::size_t> &byteLengths);
  void addDiagnostic(std::string diagnostic);

  AnalyzeResult result_;
  std::optional<diagnostic::SourceRange> currentRange_;
  std::vector<std::unordered_map<std::string, Symbol>> scopes_;
  std::unordered_map<std::string, std::size_t> bindingCounts_;
  std::unordered_map<std::string, FunctionSignature> functions_;
  std::vector<stdlib::StandardHeader> standardHeaders_;
  bool cCompatibilityMode_ = false;
  bool internalStandardModule_ = false;
  std::unordered_map<std::string, StructInfo> structs_;
  std::unordered_map<std::string, TemplateInfo> templates_;
  std::unordered_set<std::string> topLevelNames_;
  std::unordered_set<std::string> implOpKeys_;
  std::vector<ImplOpInfo> implOpInfos_;
  std::unordered_map<std::string, std::size_t> implOpIndexes_;
  std::vector<ImplMethodInfo> implMethodInfos_;
  std::unordered_map<std::string, std::size_t> implMethodIndexes_;
  std::unordered_map<std::string, std::string> userTemplateBindings_;
  std::unordered_map<std::string, std::optional<std::string>>
      memberTemplateOverrides_;
  std::vector<hir::GlobalMemory> globals_;
  FunctionSignature *currentFunction_ = nullptr;
  std::vector<hir::Parameter> currentParameters_;
  std::size_t loopDepth_ = 0;
  std::vector<CatchViewContract> catchContracts_;
  std::size_t blockDepth_ = 0;
  std::unordered_map<std::string, LabelInfo> labels_;
  std::vector<PendingGoto> pendingGotos_;
};

std::size_t parseByteLength(std::string_view text);
std::optional<std::size_t> integerByteLengthForOperator(std::string_view op);
std::optional<std::size_t> floatByteLengthForOperator(std::string_view op);
std::optional<AssignmentOperator>
integerAssignmentOperator(std::string_view op);
std::optional<std::size_t> floatAssignmentByteLength(std::string_view op);
bool isDivisionOperator(std::string_view op);
bool isDivisionOperator(char op);
std::string compoundBinaryOperator(std::string_view assignmentOp);
bool isIntegerExpression(const hir::Expr &expression);
bool hasRuntimeDynamicView(const hir::Expr &expression);
bool isUnsignedExpression(const hir::Expr &expression);
bool isFloatExpression(const hir::Expr &expression);
std::optional<std::size_t> integerExpressionByteLength(const hir::Expr &expr);
std::optional<std::size_t> floatExpressionByteLength(const hir::Expr &expr);
bool integerFits(const ast::IntegerLiteral &integer, std::size_t byteLength);
std::size_t inferIntegerLiteralByteLength(const ast::IntegerLiteral &integer);
std::size_t inferStringLiteralByteLength(std::string_view literal);
std::size_t pointerByteLength();
bool isUnsupportedStandardFunction(std::string_view name);

} // namespace hitsimple::sema
