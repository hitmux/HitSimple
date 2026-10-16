#include "SemaAnalyzer.h"

namespace hitsimple::sema {

std::optional<AssignmentLowering> Analyzer::lowerMultiReturnAssignment(
    const ast::AssignmentExpr &assign, const ast::CallExpr &call,
    const FunctionSignature &signature) {
  if (assign.targets.size() != signature.returnByteLengths.size()) {
    addDiagnostic("multi-return call result count does not match target count");
    return std::nullopt;
  }
  for (const auto &target : assign.targets) {
    if (target.op == "&=") {
      addDiagnostic("multi-return call targets do not support address rebinding");
      return std::nullopt;
    }
  }
  auto arguments = analyzeCallArguments(call, signature);
  if (!result_.diagnostics.empty()) {
    return std::nullopt;
  }

  // Capture the complete source sequence before any destination write (7.10).
  // The internal names cannot be spelled by a source identifier. Their scope
  // exists only while ordinary assignment resolves source template/length.
  beginScope();
  auto lowerCaptured = [&]() -> std::optional<AssignmentLowering> {
    AssignmentLowering lowered;
    std::vector<Symbol> sources;
    std::vector<hir::MultiReturnCallStore::Target> captures;
    for (std::size_t index = 0; index < assign.targets.size(); ++index) {
      const auto name = makeBindingName(".multi-return");
      const auto length = signature.returnByteLengths[index];
      const auto templateName = index < signature.returnTemplateNames.size()
                                    ? signature.returnTemplateNames[index]
                                    : std::string{};
      Symbol source;
      if (!declare(name, length, hir::MemoryStorage::Local, templateName,
                   source, name)) {
        addDiagnostic("internal error: cannot bind multi-return source");
        return std::nullopt;
      }
      lowered.stores.push_back(std::make_unique<hir::LocalMemory>(
          source.name, source.bindingName, source.byteLength, source.storage,
          source.templateName));
      captures.emplace_back(source.name, source.bindingName, source.byteLength,
                            source.storage, index);
      sources.push_back(std::move(source));
    }
    lowered.stores.push_back(std::make_unique<hir::MultiReturnCallStore>(
        call.callee, std::move(arguments), std::move(captures)));

    const ast::Expr *lastTarget = nullptr;
    for (std::size_t index = 0; index < assign.targets.size(); ++index) {
      const auto &target = assign.targets[index];
      const auto *identifier =
          dynamic_cast<const ast::IdentifierExpr *>(target.target.get());
      if (identifier != nullptr && identifier->name == "_") {
        continue;
      }
      const auto &source = sources[index];
      const ast::IdentifierExpr sourceExpression(source.name);
      // Physical backing storage must not turn a function result into an
      // addressable or writable source View (7.2, 18.2.31).
      auto sourceValue = std::make_unique<hir::VariableRef>(
          source.name, source.bindingName, source.storage,
          fixedResult(source.templateName, source.byteLength));
      auto store = lowerAssignmentTarget(target, sourceExpression,
                                         std::move(sourceValue));
      if (!store) {
        return std::nullopt;
      }
      lowered.stores.push_back(std::move(store));
      lastTarget = target.target.get();
    }

    if (lastTarget != nullptr) {
      lowered.result = analyze(*lastTarget);
      if (!lowered.result) {
        return std::nullopt;
      }
      lowered.byteLength = lowered.result->result.staticByteLength;
    } else {
      lowered.byteLength = 1;
      lowered.result = std::make_unique<hir::IntegerLiteral>(
          "0", signedIntegerResult(1));
    }
    return lowered;
  };
  auto lowered = lowerCaptured();
  endScope();
  return lowered;
}

} // namespace hitsimple::sema
