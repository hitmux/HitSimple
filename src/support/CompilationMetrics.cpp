#include "hitsimple/support/CompilationMetrics.h"

#include "hitsimple/support/Path.h"
#include "hitsimple/support/Process.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <ostream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace hitsimple::support {
namespace {

std::uint64_t durationNs(CompilationMetrics::TimePoint started,
                         CompilationMetrics::TimePoint finished) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started)
          .count());
}

std::string escapeJson(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const unsigned char ch : value) {
    switch (ch) {
    case '"': escaped += "\\\""; break;
    case '\\': escaped += "\\\\"; break;
    case '\b': escaped += "\\b"; break;
    case '\f': escaped += "\\f"; break;
    case '\n': escaped += "\\n"; break;
    case '\r': escaped += "\\r"; break;
    case '\t': escaped += "\\t"; break;
    default:
      if (ch < 0x20U) {
        static constexpr char hex[] = "0123456789abcdef";
        escaped += "\\u00";
        escaped += hex[ch >> 4U];
        escaped += hex[ch & 0x0fU];
      } else {
        escaped += static_cast<char>(ch);
      }
      break;
    }
  }
  return escaped;
}

void appendStageJson(std::string& json, std::string_view name,
                     const StageMetrics& stage, bool leadingComma) {
  if (leadingComma) {
    json += ',';
  }
  json += "\n        \"";
  json += name;
  json += "\": {\"status\": \"";
  json += toString(stage.status);
  json += "\", \"duration_ns\": ";
  json += std::to_string(stage.durationNs);
  json += '}';
}

void countExpr(const hir::Expr& expression, HirNodeMetrics& metrics);
void countStmt(const hir::Stmt& statement, HirNodeMetrics& metrics);

void countExprs(const std::vector<std::unique_ptr<hir::Expr>>& expressions,
                HirNodeMetrics& metrics) {
  for (const auto& expression : expressions) {
    countExpr(*expression, metrics);
  }
}

void countStmt(const hir::Stmt& statement, HirNodeMetrics& metrics) {
  ++metrics.total;
  ++metrics.statements;
  if (const auto* list = dynamic_cast<const hir::StatementList*>(&statement)) {
    for (const auto& item : list->statements) {
      countStmt(*item, metrics);
    }
  } else if (const auto* store =
                 dynamic_cast<const hir::IntegerStore*>(&statement)) {
    countExpr(*store->value, metrics);
  } else if (const auto* store = dynamic_cast<const hir::FloatStore*>(&statement)) {
    countExpr(*store->value, metrics);
  } else if (const auto* store = dynamic_cast<const hir::BoolStore*>(&statement)) {
    countExpr(*store->value, metrics);
  } else if (const auto* store =
                 dynamic_cast<const hir::PointerStore*>(&statement)) {
    countExpr(*store->address, metrics);
    countExpr(*store->value, metrics);
  } else if (const auto* call = dynamic_cast<const hir::Call*>(&statement)) {
    countExprs(call->arguments, metrics);
  } else if (const auto* call =
                 dynamic_cast<const hir::UserTemplateOpCall*>(&statement)) {
    countExprs(call->arguments, metrics);
  } else if (const auto* call =
                 dynamic_cast<const hir::UserTemplateFormatCall*>(&statement)) {
    countExpr(*call->value, metrics);
    if (call->file) {
      countExpr(*call->file, metrics);
    }
  } else if (const auto* call =
                 dynamic_cast<const hir::MultiReturnCallStore*>(&statement)) {
    countExprs(call->arguments, metrics);
  } else if (const auto* call =
                 dynamic_cast<const hir::InputCallStore*>(&statement)) {
    if (call->file) {
      countExpr(*call->file, metrics);
    }
    countExpr(*call->format, metrics);
  } else if (const auto* ret = dynamic_cast<const hir::Return*>(&statement)) {
    countExprs(ret->values, metrics);
  } else if (const auto* ifStmt = dynamic_cast<const hir::If*>(&statement)) {
    countExpr(*ifStmt->condition, metrics);
    for (const auto& item : ifStmt->thenBlock->statements) {
      countStmt(*item, metrics);
    }
    if (ifStmt->elseBlock) {
      for (const auto& item : ifStmt->elseBlock->statements) {
        countStmt(*item, metrics);
      }
    }
  } else if (const auto* whileStmt = dynamic_cast<const hir::While*>(&statement)) {
    countExpr(*whileStmt->condition, metrics);
    for (const auto& item : whileStmt->body->statements) {
      countStmt(*item, metrics);
    }
  } else if (const auto* forStmt = dynamic_cast<const hir::For*>(&statement)) {
    if (forStmt->init) {
      countStmt(*forStmt->init, metrics);
    }
    if (forStmt->condition) {
      countExpr(*forStmt->condition, metrics);
    }
    for (const auto& item : forStmt->post) {
      countStmt(*item, metrics);
    }
    for (const auto& item : forStmt->body->statements) {
      countStmt(*item, metrics);
    }
  } else if (const auto* label = dynamic_cast<const hir::Label*>(&statement)) {
    countStmt(*label->statement, metrics);
  } else if (const auto* throwStmt = dynamic_cast<const hir::Throw*>(&statement)) {
    if (throwStmt->delivery) {
      countStmt(*throwStmt->delivery, metrics);
    }
  } else if (const auto* tryCatch = dynamic_cast<const hir::TryCatch*>(&statement)) {
    for (const auto& item : tryCatch->tryBlock->statements) {
      countStmt(*item, metrics);
    }
    for (const auto& item : tryCatch->catchBlock->statements) {
      countStmt(*item, metrics);
    }
  }
}

void countExpr(const hir::Expr& expression, HirNodeMetrics& metrics) {
  ++metrics.total;
  ++metrics.expressions;
  if (const auto* deref = dynamic_cast<const hir::DerefExpr*>(&expression)) {
    countExpr(*deref->address, metrics);
  } else if (const auto* binary = dynamic_cast<const hir::BinaryExpr*>(&expression)) {
    countExpr(*binary->left, metrics);
    countExpr(*binary->right, metrics);
  } else if (const auto* unary = dynamic_cast<const hir::UnaryExpr*>(&expression)) {
    countExpr(*unary->operand, metrics);
  } else if (const auto* ternary = dynamic_cast<const hir::TernaryExpr*>(&expression)) {
    countExpr(*ternary->condition, metrics);
    countExpr(*ternary->thenExpr, metrics);
    countExpr(*ternary->elseExpr, metrics);
  } else if (const auto* unsignedExpr =
                 dynamic_cast<const hir::UnsignedExpr*>(&expression)) {
    countExpr(*unsignedExpr->operand, metrics);
  } else if (const auto* cast = dynamic_cast<const hir::IntegerCastExpr*>(&expression)) {
    countExpr(*cast->operand, metrics);
  } else if (const auto* view =
                 dynamic_cast<const hir::TemplateViewExpr*>(&expression)) {
    countExpr(*view->operand, metrics);
  } else if (const auto* call =
                 dynamic_cast<const hir::UserTemplateOpCallExpr*>(&expression)) {
    countExprs(call->arguments, metrics);
  } else if (const auto* binary =
                 dynamic_cast<const hir::FloatBinaryExpr*>(&expression)) {
    countExpr(*binary->left, metrics);
    countExpr(*binary->right, metrics);
  } else if (const auto* compare =
                 dynamic_cast<const hir::FloatCompareExpr*>(&expression)) {
    countExpr(*compare->left, metrics);
    countExpr(*compare->right, metrics);
  } else if (const auto* conversion =
                 dynamic_cast<const hir::ToFloatExpr*>(&expression)) {
    countExpr(*conversion->operand, metrics);
  } else if (const auto* conversion = dynamic_cast<const hir::ToIntExpr*>(&expression)) {
    countExpr(*conversion->operand, metrics);
  } else if (const auto* call =
                 dynamic_cast<const hir::UserTemplateFormatCallExpr*>(&expression)) {
    countExpr(*call->value, metrics);
    if (call->file) {
      countExpr(*call->file, metrics);
    }
  } else if (const auto* call = dynamic_cast<const hir::CallExpr*>(&expression)) {
    countExprs(call->arguments, metrics);
  } else if (const auto* view =
                 dynamic_cast<const hir::DynamicByteViewExpr*>(&expression)) {
    countExpr(*view->source, metrics);
    if (view->runtimeLength) {
      countExpr(*view->runtimeLength, metrics);
    }
  } else if (const auto* swap = dynamic_cast<const hir::ByteSwapExpr*>(&expression)) {
    countExpr(*swap->source, metrics);
  } else if (const auto* assignment =
                 dynamic_cast<const hir::AssignmentExpr*>(&expression)) {
    for (const auto& store : assignment->stores) {
      countStmt(*store, metrics);
    }
    countExpr(*assignment->result, metrics);
  }
}

} // namespace

std::string_view toString(MetricStageStatus status) {
  switch (status) {
  case MetricStageStatus::NotStarted: return "not_started";
  case MetricStageStatus::Skipped: return "skipped";
  case MetricStageStatus::Completed: return "completed";
  case MetricStageStatus::Failed: return "failed";
  }
  return "not_started";
}

HirNodeMetrics collectHirNodeMetrics(const hir::TranslationUnit& unit) {
  HirNodeMetrics metrics;
  metrics.globals = unit.globals.size();
  metrics.functions = unit.functions.size();
  metrics.total += metrics.globals + metrics.functions;
  if (unit.globalInit) {
    for (const auto& statement : unit.globalInit->statements) {
      countStmt(*statement, metrics);
    }
  }
  for (const auto& function : unit.functions) {
    for (const auto& statement : function->body->statements) {
      countStmt(*statement, metrics);
    }
  }
  return metrics;
}

TranslationUnitMetrics::TranslationUnitMetrics(std::string inputPath)
    : inputPath(std::move(inputPath)) {}

CompilationMetrics::CompilationMetrics() : started_(Clock::now()) {}

TranslationUnitMetrics& CompilationMetrics::addTranslationUnit(
    std::string inputPath) {
  translationUnits_.emplace_back(std::move(inputPath));
  return translationUnits_.back();
}

void CompilationMetrics::markSkipped(StageMetrics& stage) {
  stage.status = MetricStageStatus::Skipped;
  stage.durationNs = 0;
}

void CompilationMetrics::complete(StageMetrics& stage, TimePoint started) {
  stage.status = MetricStageStatus::Completed;
  stage.durationNs += durationNs(started, now());
}

void CompilationMetrics::fail(StageMetrics& stage, TimePoint started) {
  stage.status = MetricStageStatus::Failed;
  stage.durationNs += durationNs(started, now());
}

void CompilationMetrics::fail(std::string stage) {
  succeeded_ = false;
  if (!failedStage_) {
    failedStage_ = std::move(stage);
  }
}

void CompilationMetrics::succeed() {
  succeeded_ = true;
  failedStage_.reset();
}

CompilationMetrics::TimePoint CompilationMetrics::now() const { return Clock::now(); }

std::uint64_t CompilationMetrics::totalDurationNs() const {
  return durationNs(started_, now());
}

bool CompilationMetrics::succeeded() const { return succeeded_; }

const std::optional<std::string>& CompilationMetrics::failedStage() const {
  return failedStage_;
}

const std::vector<TranslationUnitMetrics>&
CompilationMetrics::translationUnits() const {
  return translationUnits_;
}

std::vector<TranslationUnitMetrics>& CompilationMetrics::translationUnits() {
  return translationUnits_;
}

StageMetrics& CompilationMetrics::llvmIrWrite() { return llvmIrWrite_; }

const StageMetrics& CompilationMetrics::llvmIrWrite() const {
  return llvmIrWrite_;
}

StageMetrics& CompilationMetrics::nativeOptimization() {
  return nativeOptimization_;
}

const StageMetrics& CompilationMetrics::nativeOptimization() const {
  return nativeOptimization_;
}

StageMetrics& CompilationMetrics::objectEmission() { return objectEmission_; }

const StageMetrics& CompilationMetrics::objectEmission() const {
  return objectEmission_;
}

StageMetrics& CompilationMetrics::clangBackendLink() { return clangBackendLink_; }

const StageMetrics& CompilationMetrics::clangBackendLink() const {
  return clangBackendLink_;
}

std::string CompilationMetrics::toJson() const {
  std::string json = "{\n  \"schema_version\": 1,\n  \"outcome\": \"";
  json += succeeded_ ? "success" : "failure";
  json += "\",\n  \"failed_stage\": ";
  if (failedStage_) {
    json += '"';
    json += escapeJson(*failedStage_);
    json += '"';
  } else {
    json += "null";
  }
  json += ",\n  \"total_duration_ns\": ";
  json += std::to_string(totalDurationNs());
  json += ",\n  \"translation_unit_count\": ";
  json += std::to_string(translationUnits_.size());
  json += ",\n  \"translation_units\": [";
  for (std::size_t index = 0; index < translationUnits_.size(); ++index) {
    const auto& unit = translationUnits_[index];
    json += index == 0 ? "\n    {" : ",\n    {";
    json += "\n      \"input\": \"" + escapeJson(unit.inputPath) + "\",";
    json += "\n      \"stages\": {";
    appendStageJson(json, "preprocess", unit.preprocess, false);
    appendStageJson(json, "parse", unit.parse, true);
    appendStageJson(json, "c_compat_lowering", unit.cCompatLowering, true);
    appendStageJson(json, "sema_hir", unit.semaHir, true);
    appendStageJson(json, "llvm_emission", unit.llvmEmission, true);
    json += "\n      },\n      \"hir_nodes\": {\"total\": ";
    json += std::to_string(unit.hirNodes.total);
    json += ", \"expressions\": ";
    json += std::to_string(unit.hirNodes.expressions);
    json += ", \"statements\": ";
    json += std::to_string(unit.hirNodes.statements);
    json += ", \"functions\": ";
    json += std::to_string(unit.hirNodes.functions);
    json += ", \"globals\": ";
    json += std::to_string(unit.hirNodes.globals);
    json += "},\n      \"llvm_ir_bytes\": ";
    json += std::to_string(unit.llvmIrBytes);
    json += "\n    }";
  }
  json += "\n  ],\n  \"global_stages\": {";
  appendStageJson(json, "llvm_ir_write", llvmIrWrite_, false);
  appendStageJson(json, "native_optimization", nativeOptimization_, true);
  appendStageJson(json, "object_emission", objectEmission_, true);
  appendStageJson(json, "clang_backend_link", clangBackendLink_, true);
  json += "\n  }\n}\n";
  return json;
}

void CompilationMetrics::printSummary(std::ostream& out) const {
  out << "hsc: timing outcome=" << (succeeded_ ? "success" : "failure")
      << " total_ns=" << totalDurationNs();
  if (failedStage_) {
    out << " failed_stage=" << *failedStage_;
  }
  out << '\n';
  for (const auto& unit : translationUnits_) {
    out << "hsc: timing tu=" << unit.inputPath
        << " preprocess=" << toString(unit.preprocess.status) << ':'
        << unit.preprocess.durationNs << " parse=" << toString(unit.parse.status)
        << ':' << unit.parse.durationNs << " c_compat_lowering="
        << toString(unit.cCompatLowering.status) << ':'
        << unit.cCompatLowering.durationNs << " sema_hir="
        << toString(unit.semaHir.status) << ':' << unit.semaHir.durationNs
        << " llvm_emission=" << toString(unit.llvmEmission.status) << ':'
        << unit.llvmEmission.durationNs << " hir_nodes=" << unit.hirNodes.total
        << " llvm_ir_bytes=" << unit.llvmIrBytes << '\n';
  }
  out << "hsc: timing global llvm_ir_write="
      << toString(llvmIrWrite_.status) << ':' << llvmIrWrite_.durationNs
      << " native_optimization=" << toString(nativeOptimization_.status) << ':'
      << nativeOptimization_.durationNs << " object_emission="
      << toString(objectEmission_.status) << ':' << objectEmission_.durationNs
      << " clang_backend_link=" << toString(clangBackendLink_.status) << ':'
      << clangBackendLink_.durationNs << '\n';
}

bool timingOutputPathIsValid(const std::filesystem::path& path,
                             std::string& error) {
  if (path.empty()) {
    error = "timing JSON path is empty";
    return false;
  }
  const auto parent = path.parent_path();
  if (!parent.empty() && !std::filesystem::exists(parent)) {
    error = "timing JSON directory does not exist '" + pathToUtf8(parent) + "'";
    return false;
  }
  if (!parent.empty() && !std::filesystem::is_directory(parent)) {
    error = "timing JSON parent is not a directory '" + pathToUtf8(parent) + "'";
    return false;
  }
  return true;
}

bool writeTimingJsonAtomically(const std::filesystem::path& path,
                               const CompilationMetrics& metrics,
                               std::string& error) {
  if (!timingOutputPathIsValid(path, error)) {
    return false;
  }
  const auto temporary = path.parent_path() /
                         (path.filename().string() + ".tmp-" +
                          std::to_string(currentProcessId()));
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      error = "cannot write timing JSON temporary file '" +
              pathToUtf8(temporary) + "'";
      return false;
    }
    output << metrics.toJson();
    output.flush();
    if (!output) {
      error = "cannot finish timing JSON temporary file '" +
              pathToUtf8(temporary) + "'";
      std::error_code removeError;
      std::filesystem::remove(temporary, removeError);
      return false;
    }
  }
  bool replaced = false;
#ifdef _WIN32
  replaced = MoveFileExW(temporary.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  if (!replaced) {
    error = "cannot replace timing JSON file '" + pathToUtf8(path) + "'";
  }
#else
  std::error_code renameError;
  std::filesystem::rename(temporary, path, renameError);
  replaced = !renameError;
  if (!replaced) {
    error = "cannot replace timing JSON file '" + pathToUtf8(path) + "': " +
            renameError.message();
  }
#endif
  if (!replaced) {
    std::error_code removeError;
    std::filesystem::remove(temporary, removeError);
    return false;
  }
  return true;
}

} // namespace hitsimple::support
