#include "hitsimple/flowir/Analysis.h"
#include "hitsimple/flowir/Builder.h"
#include "hitsimple/flowir/Serialization.h"
#include "hitsimple/flowir/Verifier.h"
#include "hitsimple/gpu/GpuAnalysis.h"
#include "hitsimple/parser/Parser.h"
#include "hitsimple/preprocessor/Preprocessor.h"
#include "hitsimple/sema/Sema.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options final {
  std::filesystem::path input;
  std::filesystem::path output;
  std::size_t cpuWorkers = 1;
  std::size_t runs = 3;
};

struct Timings final {
  std::vector<std::uint64_t> cpu;
  std::vector<std::uint64_t> cold;
  std::vector<std::uint64_t> warm;
  std::vector<std::uint64_t> resident;
};

void printUsage(std::ostream& out) {
  out << "Usage: hsc_flowir_gpu_benchmark --input <path> --output <path> "
         "[--cpu-workers <count>] [--runs <count>]\n";
}

std::optional<std::size_t> parsePositive(std::string_view value) {
  std::size_t parsed = 0;
  const auto [position, error] =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (error != std::errc{} || position != value.data() + value.size() || parsed == 0U) {
    return std::nullopt;
  }
  return parsed;
}

std::optional<Options> parseArguments(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const auto requireValue = [&](std::string_view name) -> std::optional<std::string_view> {
      if (index + 1 >= argc) {
        std::cerr << "hsc_flowir_gpu_benchmark: " << name << " requires a value\n";
        return std::nullopt;
      }
      return std::string_view(argv[++index]);
    };
    if (argument == "--input") {
      const auto value = requireValue(argument);
      if (!value) {
        return std::nullopt;
      }
      options.input = std::string(*value);
    } else if (argument == "--output") {
      const auto value = requireValue(argument);
      if (!value) {
        return std::nullopt;
      }
      options.output = std::string(*value);
    } else if (argument == "--cpu-workers" || argument == "--runs") {
      const auto value = requireValue(argument);
      if (!value) {
        return std::nullopt;
      }
      const auto parsed = parsePositive(*value);
      if (!parsed) {
        std::cerr << "hsc_flowir_gpu_benchmark: " << argument
                  << " requires a positive integer\n";
        return std::nullopt;
      }
      if (argument == "--cpu-workers") {
        options.cpuWorkers = *parsed;
      } else {
        options.runs = *parsed;
      }
    } else if (argument == "--help" || argument == "-h") {
      printUsage(std::cout);
      return std::nullopt;
    } else {
      std::cerr << "hsc_flowir_gpu_benchmark: unknown option '" << argument << "'\n";
      return std::nullopt;
    }
  }
  if (options.input.empty() || options.output.empty()) {
    std::cerr << "hsc_flowir_gpu_benchmark: --input and --output are required\n";
    return std::nullopt;
  }
  return options;
}

std::optional<hitsimple::flowir::Module>
buildFlowIr(const std::filesystem::path& inputPath) {
  const auto fileName = inputPath.string();
  auto preprocessed = hitsimple::preprocessor::preprocessFile(fileName);
  if (!preprocessed.diagnostics.empty()) {
    std::cerr << "hsc_flowir_gpu_benchmark: preprocessing failed for '" << fileName
              << "'\n";
    return std::nullopt;
  }
  auto parsed = hitsimple::parser::parseSource(
      preprocessed.source, fileName, std::move(preprocessed.lineOrigins));
  if (!parsed.unit || !parsed.error.empty() || !parsed.diagnostics.empty()) {
    std::cerr << "hsc_flowir_gpu_benchmark: parse failed for '" << fileName << "'\n";
    return std::nullopt;
  }
  auto analyzed = hitsimple::sema::analyze(
      *parsed.unit, hitsimple::sema::AnalyzeOptions{
                        false, std::move(preprocessed.standardHeaders), false});
  if (!analyzed.unit || !analyzed.diagnostics.empty()) {
    std::cerr << "hsc_flowir_gpu_benchmark: semantic analysis failed for '" << fileName
              << "'\n";
    return std::nullopt;
  }
  auto built = hitsimple::flowir::build(*analyzed.unit);
  if (!built.module || !built.diagnostics.empty()) {
    std::cerr << "hsc_flowir_gpu_benchmark: FlowIR construction failed for '" << fileName
              << "'\n";
    return std::nullopt;
  }
  if (!hitsimple::flowir::verify(*built.module).empty()) {
    std::cerr << "hsc_flowir_gpu_benchmark: FlowIR verification failed for '" << fileName
              << "'\n";
    return std::nullopt;
  }
  return std::move(*built.module);
}

std::uint64_t elapsedNs(Clock::time_point started) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count());
}

bool matchesCpu(const hitsimple::gpu::GpuAnalysisExecution& execution,
                const hitsimple::flowir::AnalysisResult& reference,
                std::string_view label) {
  if (execution.report.executedBackend != hitsimple::gpu::GpuAnalysisBackend::OpenCl ||
      execution.report.fallbackReason != hitsimple::gpu::GpuFallbackReason::None ||
      !execution.report.gpuFactsVerified) {
    std::cerr << "hsc_flowir_gpu_benchmark: " << label
              << " did not execute verified OpenCL: " << execution.report.detail << '\n';
    return false;
  }
  if (hitsimple::flowir::serializeAnalysis(execution.analysis) !=
      hitsimple::flowir::serializeAnalysis(reference)) {
    std::cerr << "hsc_flowir_gpu_benchmark: " << label
              << " differs from the CPU reference\n";
    return false;
  }
  return true;
}

std::uint64_t median(std::vector<std::uint64_t> values) {
  std::sort(values.begin(), values.end());
  return values[values.size() / 2U];
}

void writeJsonString(std::ostream& out, std::string_view value) {
  out << '"';
  for (const char character : value) {
    if (character == '"' || character == '\\') {
      out << '\\';
    }
    out << character;
  }
  out << '"';
}

void writeSamples(std::ostream& out, const std::vector<std::uint64_t>& values) {
  out << '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0U) {
      out << ',';
    }
    out << values[index];
  }
  out << ']';
}

bool writeReport(const Options& options, const hitsimple::flowir::Module& module,
                 const Timings& timings) {
  std::error_code error;
  std::filesystem::create_directories(options.output.parent_path(), error);
  if (error) {
    std::cerr << "hsc_flowir_gpu_benchmark: cannot create output directory: "
              << error.message() << '\n';
    return false;
  }
  std::ofstream output(options.output, std::ios::binary);
  if (!output) {
    std::cerr << "hsc_flowir_gpu_benchmark: cannot write '" << options.output.string()
              << "'\n";
    return false;
  }
  const auto stats = hitsimple::flowir::statistics(module);
  const auto cpuMedian = median(timings.cpu);
  const auto residentMedian = median(timings.resident);
  output << "{\n  \"schema_version\": 1,\n"
         << "  \"kind\": \"hitsimple_flowir_gpu_resident_benchmark\",\n"
         << "  \"input\": ";
  writeJsonString(output, options.input.string());
  output << ",\n  \"configuration\": {\"runs\": " << options.runs
         << ", \"cpu_workers\": " << options.cpuWorkers << "},\n"
         << "  \"flowir\": {\"functions\": " << stats.functionCount
         << ", \"blocks\": " << stats.blockCount
         << ", \"instructions\": " << stats.instructionCount
         << ", \"edges\": " << stats.edgeCount << "},\n"
         << "  \"timings_ns\": {\n    \"cpu_multi\": {\"samples\": ";
  writeSamples(output, timings.cpu);
  output << ", \"median\": " << cpuMedian << "},\n"
         << "    \"gpu_cold\": {\"samples\": ";
  writeSamples(output, timings.cold);
  output << ", \"median\": " << median(timings.cold) << "},\n"
         << "    \"gpu_warm_nonresident\": {\"samples\": ";
  writeSamples(output, timings.warm);
  output << ", \"median\": " << median(timings.warm) << "},\n"
         << "    \"gpu_resident\": {\"samples\": ";
  writeSamples(output, timings.resident);
  output << ", \"median\": " << residentMedian << "}\n  },\n"
         << "  \"resident_speedup_vs_cpu_multi\": "
         << static_cast<double>(cpuMedian) / static_cast<double>(residentMedian) << "\n}\n";
  return static_cast<bool>(output);
}

} // namespace

int main(int argc, char** argv) {
  const auto options = parseArguments(argc, argv);
  if (!options) {
    return argc == 2 && (std::string_view(argv[1]) == "--help" ||
                         std::string_view(argv[1]) == "-h")
               ? 0
               : 1;
  }
  const auto module = buildFlowIr(options->input);
  if (!module) {
    return 1;
  }
  auto alternate = *module;
  alternate.strings.push_back("warm-nonresident-input");
  if (!hitsimple::flowir::verify(alternate).empty()) {
    std::cerr << "hsc_flowir_gpu_benchmark: alternate FlowIR verification failed\n";
    return 1;
  }

  hitsimple::gpu::GpuAnalysisOptions gpuOptions;
  gpuOptions.mode = hitsimple::gpu::GpuAnalysisMode::OpenCl;
  gpuOptions.cpuWorkerCount = options->cpuWorkers;
  Timings timings;
  for (std::size_t run = 0; run < options->runs; ++run) {
    const auto cpuStarted = Clock::now();
    const auto reference = hitsimple::flowir::analyze(
        *module, hitsimple::flowir::AnalysisOptions{options->cpuWorkers});
    timings.cpu.push_back(elapsedNs(cpuStarted));
    if (!reference.diagnostics.empty()) {
      std::cerr << "hsc_flowir_gpu_benchmark: CPU reference produced diagnostics\n";
      return 1;
    }

    hitsimple::gpu::GpuAnalyzer coldAnalyzer(gpuOptions);
    const auto coldStarted = Clock::now();
    const auto cold = coldAnalyzer.analyze(*module);
    timings.cold.push_back(elapsedNs(coldStarted));
    if (!matchesCpu(cold, reference, "cold")) {
      return 1;
    }

    hitsimple::gpu::GpuAnalyzer warmAnalyzer(gpuOptions);
    const auto warmup = warmAnalyzer.analyze(*module);
    if (!matchesCpu(warmup, reference, "warmup")) {
      return 1;
    }
    const auto warmStarted = Clock::now();
    const auto warm = warmAnalyzer.analyze(alternate);
    timings.warm.push_back(elapsedNs(warmStarted));
    if (!matchesCpu(warm, reference, "warm nonresident") ||
        warm.report.reusedResidentBuffers) {
      std::cerr << "hsc_flowir_gpu_benchmark: warm run unexpectedly reused resident buffers\n";
      return 1;
    }
    const auto residentPrime = warmAnalyzer.analyze(*module);
    if (!matchesCpu(residentPrime, reference, "resident prime") ||
        residentPrime.report.reusedResidentBuffers) {
      std::cerr << "hsc_flowir_gpu_benchmark: resident prime unexpectedly reused buffers\n";
      return 1;
    }
    const auto residentStarted = Clock::now();
    const auto resident = warmAnalyzer.analyze(*module);
    timings.resident.push_back(elapsedNs(residentStarted));
    if (!matchesCpu(resident, reference, "resident") ||
        !resident.report.reusedResidentBuffers) {
      std::cerr << "hsc_flowir_gpu_benchmark: resident run did not reuse buffers\n";
      return 1;
    }
  }
  return writeReport(*options, *module, timings) ? 0 : 1;
}
