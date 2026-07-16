#include "hitsimple/gpu/GpuAnalysis.h"

#include "hitsimple/support/Path.h"

#include <fstream>

namespace hitsimple::gpu {
namespace {

void appendJsonString(std::ostream& out, const std::string& value) {
  out << '"';
  for (const char character : value) {
    switch (character) {
    case '"': out << "\\\""; break;
    case '\\': out << "\\\\"; break;
    case '\n': out << "\\n"; break;
    case '\r': out << "\\r"; break;
    case '\t': out << "\\t"; break;
    default: out << character; break;
    }
  }
  out << '"';
}

void writeTimings(std::ostream& out, const GpuAnalysisTimings& timings) {
  out << "{\"preparation_ns\":" << timings.preparationNs
      << ",\"upload_ns\":" << timings.uploadNs
      << ",\"kernel_ns\":" << timings.kernelNs
      << ",\"download_ns\":" << timings.downloadNs
      << ",\"verification_ns\":" << timings.verificationNs
      << ",\"cpu_fallback_ns\":" << timings.cpuFallbackNs << '}';
}

} // namespace

bool writeGpuAnalysisReportJson(const GpuAnalysisReport& report,
                                const std::string& path,
                                std::string& error) {
  std::ofstream output(support::pathFromUtf8(path), std::ios::binary);
  if (!output) {
    error = "cannot write GPU analysis report '" + path + "'";
    return false;
  }
  output << "{\n  \"schema_version\": 1,\n  \"kind\": \"hitsimple_flowir_gpu_analysis\",\n"
         << "  \"requested_mode\": \"" << toString(report.requestedMode) << "\",\n"
         << "  \"executed_backend\": \"" << toString(report.executedBackend) << "\",\n"
         << "  \"fallback_reason\": \"" << toString(report.fallbackReason) << "\",\n"
         << "  \"estimated_device_bytes\": " << report.estimatedDeviceBytes << ",\n"
         << "  \"reused_resident_buffers\": "
         << (report.reusedResidentBuffers ? "true" : "false") << ",\n"
         << "  \"gpu_facts_verified\": "
         << (report.gpuFactsVerified ? "true" : "false") << ",\n"
         << "  \"iterations\": {\"reachability\": "
         << report.reachabilityIterations << ", \"liveness\": "
         << report.livenessIterations << ", \"view_range\": "
         << report.viewRangeIterations << "},\n  \"timings_ns\": ";
  writeTimings(output, report.timings);
  output << ",\n  \"detail\": ";
  appendJsonString(output, report.detail);
  if (report.device) {
    output << ",\n  \"device\": {\"ordinal\": " << report.device->ordinal
           << ", \"name\": ";
    appendJsonString(output, report.device->name);
    output << ", \"api_version\": ";
    appendJsonString(output, report.device->apiVersion);
    output << ", \"total_memory_bytes\": " << report.device->totalMemoryBytes
           << ", \"available_memory_bytes\": " << report.device->availableMemoryBytes
           << ", \"available_memory_is_estimated\": "
           << (report.device->availableMemoryIsEstimated ? "true" : "false")
           << '}';
  }
  output << "\n}\n";
  if (!output) {
    error = "cannot finish GPU analysis report '" + path + "'";
    return false;
  }
  return true;
}

} // namespace hitsimple::gpu
