#include "hitsimple/flowir/Analysis.h"

#include <sstream>

namespace hitsimple::flowir {
namespace {

void appendU8(std::vector<std::uint8_t>& out, std::uint8_t value) {
  out.push_back(value);
}

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    appendU8(out, static_cast<std::uint8_t>(value >> shift));
  }
}

void appendIds(std::vector<std::uint8_t>& out, const std::vector<ValueId>& ids) {
  appendU32(out, static_cast<std::uint32_t>(ids.size()));
  for (const auto id : ids) {
    appendU32(out, id);
  }
}

} // namespace

std::string dumpAnalysisToString(const AnalysisResult& result) {
  std::ostringstream out;
  out << "FlowIRAnalysis schema=1\n";
  for (const auto& fact : result.reachability) {
    out << "Reachability function=" << fact.function << " block=" << fact.block
        << " reachable=" << (fact.reachable ? "true" : "false") << '\n';
  }
  for (const auto& fact : result.liveness) {
    out << "Liveness function=" << fact.function << " block=" << fact.block << " in=";
    for (const auto value : fact.liveIn) out << value << ',';
    out << " out=";
    for (const auto value : fact.liveOut) out << value << ',';
    out << '\n';
  }
  for (const auto& fact : result.viewRanges) {
    out << "ViewRange value=" << fact.value << " state="
        << (fact.state == RangeState::Known ? "known" : "unknown")
        << " object=" << fact.object << " offset=" << fact.offset
        << " bytes=" << fact.byteLength << '\n';
  }
  for (const auto& fact : result.lifetimes) {
    out << "Lifetime function=" << fact.function << " block=" << fact.block
        << " object=" << fact.object << " entry=" << static_cast<unsigned>(fact.entry)
        << " exit=" << static_cast<unsigned>(fact.exit) << '\n';
  }
  for (const auto& fact : result.initializedBytes) {
    out << "Initialized function=" << fact.function << " block=" << fact.block
        << " object=" << fact.object << " offset=" << fact.offset
        << " bytes=" << fact.byteLength << '\n';
  }
  for (const auto& summary : result.effects) {
    out << "Effects function=" << summary.function << " flags=" << summary.flags
        << " declared=" << summary.declaredFlags
        << " explicit=" << (summary.hasExplicitContract ? "true" : "false")
        << " callees=";
    for (const auto callee : summary.callees) out << callee << ',';
    out << '\n';
  }
  for (const auto& convergence : result.convergence) {
    out << "Convergence function=" << convergence.function
        << " reachability=" << convergence.reachabilityIterations
        << " liveness=" << convergence.livenessIterations
        << " view-range=" << convergence.viewRangeIterations
        << " lifetime=" << convergence.lifetimeIterations
        << " initialization=" << convergence.initializationIterations << '\n';
  }
  return out.str();
}

std::vector<std::uint8_t> serializeAnalysis(const AnalysisResult& result) {
  std::vector<std::uint8_t> out;
  out.insert(out.end(), {'A', 'N', 'L', 'F'});
  appendU32(out, 1);
  appendU32(out, static_cast<std::uint32_t>(result.reachability.size()));
  appendU32(out, static_cast<std::uint32_t>(result.liveness.size()));
  appendU32(out, static_cast<std::uint32_t>(result.viewRanges.size()));
  appendU32(out, static_cast<std::uint32_t>(result.lifetimes.size()));
  appendU32(out, static_cast<std::uint32_t>(result.initializedBytes.size()));
  appendU32(out, static_cast<std::uint32_t>(result.effects.size()));
  appendU32(out, static_cast<std::uint32_t>(result.convergence.size()));
  for (const auto& fact : result.reachability) {
    appendU32(out, fact.function); appendU32(out, fact.block); appendU8(out, fact.reachable);
  }
  for (const auto& fact : result.liveness) {
    appendU32(out, fact.function); appendU32(out, fact.block);
    appendIds(out, fact.liveIn); appendIds(out, fact.liveOut);
  }
  for (const auto& fact : result.viewRanges) {
    appendU32(out, fact.value); appendU8(out, static_cast<std::uint8_t>(fact.state));
    appendU32(out, fact.object); appendU32(out, fact.offset); appendU32(out, fact.byteLength);
  }
  for (const auto& fact : result.lifetimes) {
    appendU32(out, fact.function); appendU32(out, fact.block); appendU32(out, fact.object);
    appendU8(out, static_cast<std::uint8_t>(fact.entry)); appendU8(out, static_cast<std::uint8_t>(fact.exit));
  }
  for (const auto& fact : result.initializedBytes) {
    appendU32(out, fact.function); appendU32(out, fact.block); appendU32(out, fact.object);
    appendU32(out, fact.offset); appendU32(out, fact.byteLength);
  }
  for (const auto& summary : result.effects) {
    appendU32(out, summary.function); appendU32(out, summary.flags);
    appendU32(out, summary.declaredFlags);
    appendU8(out, summary.hasExplicitContract ? 1U : 0U);
    appendU32(out, static_cast<std::uint32_t>(summary.callees.size()));
    for (const auto callee : summary.callees) appendU32(out, callee);
  }
  for (const auto& fact : result.convergence) {
    appendU32(out, fact.function); appendU32(out, fact.reachabilityIterations);
    appendU32(out, fact.livenessIterations); appendU32(out, fact.viewRangeIterations);
    appendU32(out, fact.lifetimeIterations);
    appendU32(out, fact.initializationIterations);
  }
  return out;
}

} // namespace hitsimple::flowir
