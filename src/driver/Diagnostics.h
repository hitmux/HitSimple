#pragma once

#include "driver/DriverOptions.h"
#include "hitsimple/diagnostic/Diagnostic.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hitsimple::driver {

class DiagnosticOutputFormatScope final {
public:
  explicit DiagnosticOutputFormatScope(DiagnosticOutputFormat format);
  ~DiagnosticOutputFormatScope();

  DiagnosticOutputFormatScope(const DiagnosticOutputFormatScope&) = delete;
  DiagnosticOutputFormatScope& operator=(const DiagnosticOutputFormatScope&) =
      delete;

private:
  DiagnosticOutputFormat previous_;
};

void printDiagnostic(const hitsimple::diagnostic::Diagnostic& diagnostic);
bool printDiagnostics(
    const std::vector<hitsimple::diagnostic::Diagnostic>& diagnostics);
hitsimple::diagnostic::SourceRange fileStartRange(std::string_view inputPath);
hitsimple::diagnostic::Diagnostic fileLevelDiagnostic(
    hitsimple::diagnostic::Stage stage, std::string message,
    std::string_view inputPath);
std::string escapeLexeme(std::string_view lexeme);
bool writeFile(const std::string& path, const std::string& content);
bool validateOutputParent(const std::string& path);
bool outputPathsConflict(const std::string& left, const std::string& right);
std::optional<std::string> readFile(const std::string& path);
bool prepareOptimizationRemarksOutput(const std::filesystem::path& path);
bool appendOptimizationRemarks(const std::filesystem::path& path,
                               const std::vector<std::string>& remarks);

} // namespace hitsimple::driver
