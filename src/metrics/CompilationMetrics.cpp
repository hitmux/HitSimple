#include "hitsimple/metrics/CompilationMetrics.h"

#include "hitsimple/hir/HIR.h"

#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace hitsimple::metrics {
namespace {

std::string_view stageName(CompilationStage stage) {
  switch (stage) {
  case CompilationStage::Preprocess:
    return "preprocess";
  case CompilationStage::Parse:
    return "parse";
  case CompilationStage::CCompatParse:
    return "c_compat_parse";
  case CompilationStage::CCompatLowering:
    return "c_compat_lowering";
  case CompilationStage::Sema:
    return "sema";
  case CompilationStage::LlvmEmission:
    return "llvm_emission";
  case CompilationStage::TemporaryLlWrite:
    return "temporary_ll_write";
  case CompilationStage::ClangBackendLink:
    return "clang_backend_link";
  }
  return "unknown";
}

void writeJsonString(std::ostream& out, std::string_view text) {
  out.put('"');
  for (const unsigned char ch : text) {
    switch (ch) {
    case '"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\b':
      out << "\\b";
      break;
    case '\f':
      out << "\\f";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      if (ch < 0x20U) {
        static constexpr char hex[] = "0123456789abcdef";
        out << "\\u00" << hex[(ch >> 4U) & 0x0FU] << hex[ch & 0x0FU];
      } else {
        out.put(static_cast<char>(ch));
      }
      break;
    }
  }
  out.put('"');
}

void writeStages(std::ostream& out, const std::vector<StageTiming>& stages) {
  out << '{';
  for (std::size_t index = 0; index < stages.size(); ++index) {
    if (index != 0) {
      out << ',';
    }
    writeJsonString(out, stageName(stages[index].stage));
    out << ":{\"wall_time_ns\":" << stages[index].wallTime.count() << '}';
  }
  out << '}';
}

void writeHirStatistics(std::ostream& out, const HirStatistics& statistics) {
  out << "{\"function_count\":" << statistics.functionCount
      << ",\"global_count\":" << statistics.globalCount
      << ",\"statement_count\":" << statistics.statementCount
      << ",\"expression_count\":" << statistics.expressionCount
      << ",\"estimated_host_bytes\":" << statistics.estimatedHostBytes
      << '}';
}

void accumulate(HirStatistics& target, const HirStatistics& source) {
  target.functionCount += source.functionCount;
  target.globalCount += source.globalCount;
  target.statementCount += source.statementCount;
  target.expressionCount += source.expressionCount;
  target.estimatedHostBytes += source.estimatedHostBytes;
}

void countExpr(const hir::Expr* expression, HirStatistics& statistics);
void countStmt(const hir::Stmt* statement, HirStatistics& statistics);

void countBlock(const hir::Block* block, HirStatistics& statistics) {
  if (block == nullptr) {
    return;
  }
  statistics.estimatedHostBytes += sizeof(*block) +
                                   block->statements.capacity() *
                                       sizeof(block->statements.front());
  for (const auto& statement : block->statements) {
    countStmt(statement.get(), statistics);
  }
}

void countExprList(const std::vector<std::unique_ptr<hir::Expr>>& expressions,
                   HirStatistics& statistics) {
  for (const auto& expression : expressions) {
    countExpr(expression.get(), statistics);
  }
}

void countStmtList(const std::vector<std::unique_ptr<hir::Stmt>>& statements,
                   HirStatistics& statistics) {
  for (const auto& statement : statements) {
    countStmt(statement.get(), statistics);
  }
}

template <typename Node>
void countExprNode(const Node& node, HirStatistics& statistics) {
  ++statistics.expressionCount;
  statistics.estimatedHostBytes += sizeof(node);
}

template <typename Node>
void countStmtNode(const Node& node, HirStatistics& statistics) {
  ++statistics.statementCount;
  statistics.estimatedHostBytes += sizeof(node);
}

void countExpr(const hir::Expr* expression, HirStatistics& statistics) {
  if (expression == nullptr) {
    return;
  }
  if (const auto* node = dynamic_cast<const hir::IntegerLiteral*>(expression)) {
    countExprNode(*node, statistics);
  } else if (const auto* node =
                 dynamic_cast<const hir::StringLiteral*>(expression)) {
    countExprNode(*node, statistics);
  } else if (const auto* node =
                 dynamic_cast<const hir::FloatLiteral*>(expression)) {
    countExprNode(*node, statistics);
  } else if (const auto* node =
                 dynamic_cast<const hir::VariableRef*>(expression)) {
    countExprNode(*node, statistics);
  } else if (const auto* node =
                 dynamic_cast<const hir::AddressOfExpr*>(expression)) {
    countExprNode(*node, statistics);
  } else if (const auto* node = dynamic_cast<const hir::DerefExpr*>(expression)) {
    countExprNode(*node, statistics);
    countExpr(node->address.get(), statistics);
  } else if (const auto* node = dynamic_cast<const hir::BinaryExpr*>(expression)) {
    countExprNode(*node, statistics);
    countExpr(node->left.get(), statistics);
    countExpr(node->right.get(), statistics);
  } else if (const auto* node = dynamic_cast<const hir::UnaryExpr*>(expression)) {
    countExprNode(*node, statistics);
    countExpr(node->operand.get(), statistics);
  } else if (const auto* node =
                 dynamic_cast<const hir::TernaryExpr*>(expression)) {
    countExprNode(*node, statistics);
    countExpr(node->condition.get(), statistics);
    countExpr(node->thenExpr.get(), statistics);
    countExpr(node->elseExpr.get(), statistics);
  } else if (const auto* node =
                 dynamic_cast<const hir::UnsignedExpr*>(expression)) {
    countExprNode(*node, statistics);
    countExpr(node->operand.get(), statistics);
  } else if (const auto* node =
                 dynamic_cast<const hir::IntegerCastExpr*>(expression)) {
    countExprNode(*node, statistics);
    countExpr(node->operand.get(), statistics);
  } else if (const auto* node =
                 dynamic_cast<const hir::TemplateViewExpr*>(expression)) {
    countExprNode(*node, statistics);
    countExpr(node->operand.get(), statistics);
  } else if (const auto* node = dynamic_cast<const hir::UserTemplateOpCallExpr*>(
                 expression)) {
    countExprNode(*node, statistics);
    countExprList(node->arguments, statistics);
  } else if (const auto* node =
                 dynamic_cast<const hir::FloatBinaryExpr*>(expression)) {
    countExprNode(*node, statistics);
    countExpr(node->left.get(), statistics);
    countExpr(node->right.get(), statistics);
  } else if (const auto* node =
                 dynamic_cast<const hir::FloatCompareExpr*>(expression)) {
    countExprNode(*node, statistics);
    countExpr(node->left.get(), statistics);
    countExpr(node->right.get(), statistics);
  } else if (const auto* node = dynamic_cast<const hir::ToFloatExpr*>(expression)) {
    countExprNode(*node, statistics);
    countExpr(node->operand.get(), statistics);
  } else if (const auto* node = dynamic_cast<const hir::ToIntExpr*>(expression)) {
    countExprNode(*node, statistics);
    countExpr(node->operand.get(), statistics);
  } else if (const auto* node =
                 dynamic_cast<const hir::UserTemplateFormatCallExpr*>(expression)) {
    countExprNode(*node, statistics);
    countExpr(node->value.get(), statistics);
    countExpr(node->file.get(), statistics);
  } else if (const auto* node = dynamic_cast<const hir::CallExpr*>(expression)) {
    countExprNode(*node, statistics);
    countExprList(node->arguments, statistics);
  } else if (const auto* node =
                 dynamic_cast<const hir::DynamicByteViewExpr*>(expression)) {
    countExprNode(*node, statistics);
    countExpr(node->source.get(), statistics);
    countExpr(node->runtimeLength.get(), statistics);
  } else if (const auto* node =
                 dynamic_cast<const hir::ByteSwapExpr*>(expression)) {
    countExprNode(*node, statistics);
    countExpr(node->source.get(), statistics);
  } else if (const auto* node =
                 dynamic_cast<const hir::AssignmentExpr*>(expression)) {
    countExprNode(*node, statistics);
    countStmtList(node->stores, statistics);
    countExpr(node->result.get(), statistics);
  }
}

void countStmt(const hir::Stmt* statement, HirStatistics& statistics) {
  if (statement == nullptr) {
    return;
  }
  if (const auto* node = dynamic_cast<const hir::StatementList*>(statement)) {
    countStmtNode(*node, statistics);
    countStmtList(node->statements, statistics);
  } else if (const auto* node = dynamic_cast<const hir::LocalMemory*>(statement)) {
    countStmtNode(*node, statistics);
  } else if (const auto* node = dynamic_cast<const hir::IntegerStore*>(statement)) {
    countStmtNode(*node, statistics);
    countExpr(node->value.get(), statistics);
  } else if (const auto* node = dynamic_cast<const hir::FloatStore*>(statement)) {
    countStmtNode(*node, statistics);
    countExpr(node->value.get(), statistics);
  } else if (const auto* node = dynamic_cast<const hir::StringStore*>(statement)) {
    countStmtNode(*node, statistics);
  } else if (const auto* node =
                 dynamic_cast<const hir::StringCopyStore*>(statement)) {
    countStmtNode(*node, statistics);
  } else if (const auto* node = dynamic_cast<const hir::BoolStore*>(statement)) {
    countStmtNode(*node, statistics);
    countExpr(node->value.get(), statistics);
  } else if (const auto* node = dynamic_cast<const hir::PointerStore*>(statement)) {
    countStmtNode(*node, statistics);
    countExpr(node->address.get(), statistics);
    countExpr(node->value.get(), statistics);
  } else if (const auto* node = dynamic_cast<const hir::Call*>(statement)) {
    countStmtNode(*node, statistics);
    countExprList(node->arguments, statistics);
  } else if (const auto* node =
                 dynamic_cast<const hir::UserTemplateOpCall*>(statement)) {
    countStmtNode(*node, statistics);
    countExprList(node->arguments, statistics);
  } else if (const auto* node =
                 dynamic_cast<const hir::UserTemplateFormatCall*>(statement)) {
    countStmtNode(*node, statistics);
    countExpr(node->value.get(), statistics);
    countExpr(node->file.get(), statistics);
  } else if (const auto* node =
                 dynamic_cast<const hir::MultiReturnCallStore*>(statement)) {
    countStmtNode(*node, statistics);
    countExprList(node->arguments, statistics);
  } else if (const auto* node =
                 dynamic_cast<const hir::InputCallStore*>(statement)) {
    countStmtNode(*node, statistics);
    countExpr(node->file.get(), statistics);
    countExpr(node->format.get(), statistics);
  } else if (const auto* node = dynamic_cast<const hir::Return*>(statement)) {
    countStmtNode(*node, statistics);
    countExprList(node->values, statistics);
  } else if (const auto* node = dynamic_cast<const hir::If*>(statement)) {
    countStmtNode(*node, statistics);
    countExpr(node->condition.get(), statistics);
    countBlock(node->thenBlock.get(), statistics);
    countBlock(node->elseBlock.get(), statistics);
  } else if (const auto* node = dynamic_cast<const hir::While*>(statement)) {
    countStmtNode(*node, statistics);
    countExpr(node->condition.get(), statistics);
    countBlock(node->body.get(), statistics);
  } else if (const auto* node = dynamic_cast<const hir::For*>(statement)) {
    countStmtNode(*node, statistics);
    countStmt(node->init.get(), statistics);
    countExpr(node->condition.get(), statistics);
    countStmtList(node->post, statistics);
    countBlock(node->body.get(), statistics);
  } else if (const auto* node = dynamic_cast<const hir::Break*>(statement)) {
    countStmtNode(*node, statistics);
  } else if (const auto* node = dynamic_cast<const hir::Continue*>(statement)) {
    countStmtNode(*node, statistics);
  } else if (const auto* node = dynamic_cast<const hir::Goto*>(statement)) {
    countStmtNode(*node, statistics);
  } else if (const auto* node = dynamic_cast<const hir::Label*>(statement)) {
    countStmtNode(*node, statistics);
    countStmt(node->statement.get(), statistics);
  } else if (const auto* node = dynamic_cast<const hir::Throw*>(statement)) {
    countStmtNode(*node, statistics);
    countStmt(node->delivery.get(), statistics);
  } else if (const auto* node = dynamic_cast<const hir::TryCatch*>(statement)) {
    countStmtNode(*node, statistics);
    countBlock(node->tryBlock.get(), statistics);
    countBlock(node->catchBlock.get(), statistics);
  }
}

} // namespace

CompilationMetrics::CompilationMetrics() : startedAt_(std::chrono::steady_clock::now()) {}

std::size_t CompilationMetrics::beginTranslationUnit(std::string inputPath,
                                                      std::size_t inputBytes) {
  translationUnits_.push_back(
      TranslationUnitMetrics{std::move(inputPath), inputBytes, {}, std::nullopt});
  return translationUnits_.size() - 1U;
}

void CompilationMetrics::recordTranslationUnitStage(
    std::size_t index, CompilationStage stage, std::chrono::nanoseconds wallTime) {
  if (index < translationUnits_.size()) {
    translationUnits_[index].stages.push_back({stage, wallTime});
  }
}

void CompilationMetrics::recordProgramStage(CompilationStage stage,
                                            std::chrono::nanoseconds wallTime) {
  programStages_.push_back({stage, wallTime});
}

void CompilationMetrics::recordHirStatistics(std::size_t index,
                                              HirStatistics statistics) {
  if (index < translationUnits_.size()) {
    translationUnits_[index].hir = statistics;
  }
}

void CompilationMetrics::finish(bool succeeded) {
  if (finished_) {
    return;
  }
  totalWallTime_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - startedAt_);
  succeeded_ = succeeded;
  finished_ = true;
}

bool CompilationMetrics::writeJson(const std::filesystem::path& path,
                                   std::string& error) const {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "cannot write timing JSON '" + path.string() + "'";
    return false;
  }

  output << "{\"schema_version\":" << kTimingJsonSchemaVersion
         << ",\"completed\":" << (finished_ ? "true" : "false")
         << ",\"succeeded\":" << (succeeded_ ? "true" : "false")
         << ",\"translation_units\":[";
  for (std::size_t index = 0; index < translationUnits_.size(); ++index) {
    if (index != 0) {
      output << ',';
    }
    const auto& unit = translationUnits_[index];
    output << "{\"input_path\":";
    writeJsonString(output, unit.inputPath);
    output << ",\"input_bytes\":" << unit.inputBytes << ",\"stages\":";
    writeStages(output, unit.stages);
    if (unit.hir) {
      output << ",\"hir\":";
      writeHirStatistics(output, *unit.hir);
    }
    output << '}';
  }

  std::size_t totalInputBytes = 0;
  HirStatistics totalHir{};
  bool hasHirStatistics = false;
  for (const auto& unit : translationUnits_) {
    totalInputBytes += unit.inputBytes;
    if (unit.hir) {
      accumulate(totalHir, *unit.hir);
      hasHirStatistics = true;
    }
  }

  output << "],\"program\":{\"translation_unit_count\":"
         << translationUnits_.size() << ",\"input_bytes\":" << totalInputBytes
         << ",\"stages\":";
  writeStages(output, programStages_);
  if (hasHirStatistics) {
    output << ",\"hir\":";
    writeHirStatistics(output, totalHir);
  }
  output << ",\"wall_time_ns\":" << totalWallTime_.count() << "}}\n";

  if (!output) {
    error = "cannot finish timing JSON '" + path.string() + "'";
    return false;
  }
  return true;
}

ScopedStageTimer::ScopedStageTimer(
    CompilationMetrics* metrics, std::optional<std::size_t> translationUnitIndex,
    CompilationStage stage)
    : metrics_(metrics), translationUnitIndex_(translationUnitIndex), stage_(stage) {
  if (metrics_ != nullptr) {
    startedAt_ = std::chrono::steady_clock::now();
  }
}

ScopedStageTimer::~ScopedStageTimer() {
  if (metrics_ == nullptr) {
    return;
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - startedAt_);
  if (translationUnitIndex_) {
    metrics_->recordTranslationUnitStage(*translationUnitIndex_, stage_, elapsed);
  } else {
    metrics_->recordProgramStage(stage_, elapsed);
  }
}

HirStatistics collectHirStatistics(const hir::TranslationUnit& unit) {
  HirStatistics statistics;
  statistics.globalCount = unit.globals.size();
  statistics.estimatedHostBytes += sizeof(unit) +
                                   unit.globals.capacity() * sizeof(hir::GlobalMemory) +
                                   unit.functions.capacity() *
                                       sizeof(std::unique_ptr<hir::Function>);
  countBlock(unit.globalInit.get(), statistics);
  for (const auto& function : unit.functions) {
    if (!function) {
      continue;
    }
    ++statistics.functionCount;
    statistics.estimatedHostBytes += sizeof(*function) +
                                     function->parameters.capacity() *
                                         sizeof(hir::Parameter) +
                                     function->returnByteLengths.capacity() *
                                         sizeof(std::size_t);
    countBlock(function->body.get(), statistics);
  }
  return statistics;
}

} // namespace hitsimple::metrics
