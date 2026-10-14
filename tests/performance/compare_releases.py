#!/usr/bin/env python3
"""Coordinate pinned native release comparisons using the existing T8 benchmark.

This is an evidence collector, not another runtime benchmark harness. Runtime
sampling, correctness checks and trend thresholds remain owned by T8.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import struct
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

from check_benchmark_trend import quality_metrics

REVISIONS = {
    "v0.3.8": "e5fc8060c311c1e5cda71c3090e8a64d26060acf",
    "v0.3.9": "26c5a5c13c8f6ec47c020234f5e442055eea53c7",
    "master-2026-10-14": "d89c280a8e223a49dc21214977f3d33a9ef029d3",
}
WORKLOADS = {"mandelbrot", "memory", "bitops", "integer_sum", "branch",
             "function_call", "stdlib", "checked_loop", "c_abi"}
OBJECT_WORKLOADS = WORKLOADS - {"checked_loop"}
COMPILE_KEYS = {name + "/executable" for name in WORKLOADS} | {name + "/object" for name in OBJECT_WORKLOADS}
T8_REGEX = (
    r"^(hsc_harness_(runtime|differential|output_differential|checked_differential|"
    r"p3_host_semantics|aarch64_semantics)|hsc_property_metamorphic|"
    r"hsc_hs_smith_(unit_tests|first_phase)|hsc_whole_program_fuzzer|hsc_t8_.*)$"
)


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def read_json(path):
    return json.loads(path.read_text())


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stats(values):
    if not values or any(isinstance(v, bool) or not isinstance(v, (int, float))
                         or not math.isfinite(v) or v <= 0 for v in values):
        raise ValueError("measurements must be finite positive numbers")
    median = statistics.median(values)
    return {"n": len(values), "median": median, "min": min(values),
            "max": max(values),
            "median_absolute_deviation": statistics.median(abs(v - median) for v in values)}


def change(current, baseline):
    if baseline <= 0:
        raise ValueError("baseline must be positive")
    return (current / baseline - 1) * 100


def run(command, *, log=None, cwd=None, env=None, check=True):
    print("+ " + " ".join(map(str, command)), flush=True)
    if log:
        log.parent.mkdir(parents=True, exist_ok=True)
        with log.open("w") as output:
            result = subprocess.run(command, cwd=cwd, env=env, stdout=output,
                                    stderr=subprocess.STDOUT, check=False)
        if result.returncode:
            print(log.read_text()[-16000:], file=sys.stderr)
    else:
        result = subprocess.run(command, cwd=cwd, env=env, check=False)
    if check and result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}): {command}")
    return result.returncode


def source_hashes(source):
    paths = sorted((source / "hsvsc++/hsvsc++").glob("benchmark*"))
    paths += [source / p for p in [
        "hsvsc++/hsvsc++/run_benchmark.sh",
        "tests/performance/analyze_native_artifacts.py",
        "tests/performance/check_benchmark_trend.py",
    ]]
    return {str(p.relative_to(source)): digest(p) for p in paths if p.is_file()}


def verify_reports(reports, source_manifests, runs, warmups):
    baseline = reports["v0.3.8"]
    for label, report in reports.items():
        if report.get("version") != 2:
            raise ValueError("unsupported benchmark schema")
        if set(report["workloads"]) != WORKLOADS or set(quality_metrics(report)) != OBJECT_WORKLOADS:
            raise ValueError("incomplete T8 runtime or native-quality workload set")
        if report["git_commit"] != REVISIONS[label]:
            raise ValueError(f"wrong compiler/source revision for {label}")
        for field in ("target_triple", "optimization_level", "cpu", "runs", "warmups",
                      "cooldown_seconds"):
            if report[field] != baseline[field]:
                raise ValueError(f"incompatible {field}: {label}")
        if report["runs"] != runs or report["warmups"] != warmups:
            raise ValueError("unexpected runtime sampling counts")
        if report["optimization_level"] != "O2" or "aarch64" not in report["target_triple"]:
            raise ValueError("expected native AArch64 O2 report")
        if report["cxx"] != baseline["cxx"]:
            raise ValueError("incompatible C++ compiler")
        if source_manifests[label] != source_manifests["v0.3.8"]:
            raise ValueError(f"benchmark source mismatch: {label}")
        if report["workloads"].keys() != baseline["workloads"].keys():
            raise ValueError("workload set differs")
        for name, item in report["workloads"].items():
            expected_mode = "checked" if name == "checked_loop" else "unchecked"
            if item["safety_mode"] != expected_mode or not isinstance(item["checksum"], str) or not item["checksum"].strip():
                raise ValueError(f"invalid safety mode or checksum: {label}/{name}")
            for field in ("checksum", "safety_mode"):
                if item[field] != baseline["workloads"][name][field]:
                    raise ValueError(f"incompatible {name}.{field}: {label}")
            for implementation in ("hitsimple", "cpp"):
                samples = item[implementation]["samples_ns"]
                if len(samples) != runs or stats(samples)["median"] != item[implementation]["median_ns"]:
                    raise ValueError(f"invalid runtime samples: {label}/{name}")


def verify_junit(path):
    suite = ET.parse(path).getroot()
    cases = suite.findall(".//testcase")
    required = {"hsc_t8_aarch64_strict_alignment", "hsc_harness_aarch64_semantics",
                "hsc_t8_vectorization_remarks"}
    if not required.issubset({case.get("name") for case in cases}):
        raise ValueError("required native T8 tests did not run")
    if any(case.get("status") != "run" or case.find("failure") is not None
           or case.find("skipped") is not None for case in cases):
        raise ValueError("T8 tests failed or were skipped")
    return len(cases)


def compilation_command(source, compiler, workload, mode, kind, output, timing):
    command = [str(compiler), "-O2", "--" + mode, "--timing-json=" + str(timing)]
    if kind == "object":
        command.append("--emit-object")
    command.append(str(source / "hsvsc++/hsvsc++" / f"benchmark_{workload}.hs"))
    if workload == "c_abi" and kind == "executable":
        command += ["--c-source", str(source / "hsvsc++/hsvsc++/benchmark_c_abi_helper.c")]
    return command + ["-o", str(output)]


def timing_total(path):
    data = read_json(path)
    if data.get("schema_version") != 1 or data.get("outcome") != "success":
        raise ValueError(f"unsuccessful or unknown compilation timing: {path}")
    value = data["total_duration_ns"]
    stats([value])
    return value


def verify_elf(path, kind):
    with path.open("rb") as handle:
        header = handle.read(64)
    if len(header) != 64 or header[:6] != b"\x7fELF\x02\x01":
        raise ValueError(f"not a nonempty little-endian ELF64 artifact: {path}")
    elf_type, machine = struct.unpack_from("<HH", header, 16)
    if machine != 183 or elf_type not in ({1} if kind == "object" else {2, 3}):
        raise ValueError(f"wrong native ELF architecture or type: {path}")
    return {"type": elf_type, "machine": machine}


def verify_compilations(samples, runs, warmups):
    if set(samples) != set(REVISIONS):
        raise ValueError("incomplete compiler revision set")
    for label, measurements in samples.items():
        if set(measurements) != COMPILE_KEYS:
            raise ValueError(f"incomplete compiler workload set: {label}")
        for key, values in measurements.items():
            if len(values) != runs + warmups:
                raise ValueError(f"incomplete compiler samples: {label}/{key}")
            for index, value in enumerate(values):
                if value["iteration"] != index or value["warmup"] != (index < warmups):
                    raise ValueError("incorrect compile sample/warmup order")
                stats([value["total_duration_ns"], value["wall_duration_ns"], value["bytes"]])


def collect_compilations(states, reports, output, cpu, runs, warmups):
    samples = {label: {} for label in states}
    labels = list(states)
    # Rotate version order per iteration; every version receives identical warmups.
    for iteration in range(warmups + runs):
        order = labels[iteration % len(labels):] + labels[:iteration % len(labels)]
        for label in order:
            source, build = states[label]
            for workload, item in reports[label]["workloads"].items():
                # Match the existing T8 native object workload set (excludes checked_loop).
                kinds = ["executable"] + ([] if workload == "checked_loop" else ["object"])
                for kind in kinds:
                    key = workload + "/" + kind
                    directory = output / label / "compile-samples" / workload / kind / str(iteration)
                    directory.mkdir(parents=True)
                    binary, timing = directory / "output", directory / "timing.json"
                    command = ["taskset", "-c", str(cpu)] + compilation_command(
                        source, build / "hsc", workload, item["safety_mode"], kind, binary, timing)
                    start = time.perf_counter_ns()
                    run(command, log=directory / "compile.log")
                    wall = time.perf_counter_ns() - start
                    sample = {"iteration": iteration, "warmup": iteration < warmups,
                              "total_duration_ns": timing_total(timing), "wall_duration_ns": wall,
                              "elf": verify_elf(binary, kind),
                              "bytes": binary.stat().st_size, "sha256": digest(binary),
                              "timing_file": str(timing.relative_to(output)), "command": command}
                    samples[label].setdefault(key, []).append(sample)
    write_json(output / "compile-samples.json", samples)
    return samples


def summarize(reports, samples):
    result = {}
    for label, report in reports.items():
        metrics = {}
        for name, item in report["workloads"].items():
            base = reports["v0.3.8"]["workloads"][name]
            hs, cpp = item["hitsimple"]["median_ns"], item["cpp"]["median_ns"]
            metrics[name] = {"runtime_ns": stats(item["hitsimple"]["samples_ns"]),
                             "cpp_runtime_ns": stats(item["cpp"]["samples_ns"]),
                             "runtime_change_percent": change(hs, base["hitsimple"]["median_ns"]),
                             "hs_cpp_ratio": hs / cpp,
                             "ratio_change_percent": change(hs / cpp, base["hitsimple"]["median_ns"] / base["cpp"]["median_ns"])}
        compilation = {}
        for key, values in samples[label].items():
            measured = [v for v in values if not v["warmup"]]
            baseline = [v for v in samples["v0.3.8"][key] if not v["warmup"]]
            stats([v["bytes"] for v in measured])
            if len({v["bytes"] for v in measured}) != 1:
                raise ValueError(f"inconsistent emitted size for {label}/{key}")
            internal = stats([v["total_duration_ns"] for v in measured])
            compilation[key] = {
                "compiler_total_ns": internal,
                "wall_ns": stats([v["wall_duration_ns"] for v in measured]),
                "compile_change_percent": change(internal["median"], stats([v["total_duration_ns"] for v in baseline])["median"]),
                "bytes": measured[0]["bytes"],
                "size_change_percent": change(measured[0]["bytes"], baseline[0]["bytes"]),
            }
        result[label] = {"revision": REVISIONS[label], "runtime": metrics, "compilation": compilation}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--runtime-runs", type=int, default=15)
    parser.add_argument("--runtime-warmups", type=int, default=3)
    parser.add_argument("--compile-runs", type=int, default=9)
    parser.add_argument("--compile-warmups", type=int, default=2)
    args = parser.parse_args()
    if platform.machine() not in ("aarch64", "arm64"):
        parser.error("requires a native AArch64 runner; emulation is not evidence")
    if min(args.runtime_runs, args.compile_runs) < 1 or min(args.runtime_warmups, args.compile_warmups) < 0:
        parser.error("runs must be positive and warmups nonnegative")
    os.environ["LC_ALL"] = "C"
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    cpu = min(os.sched_getaffinity(0))
    llvm_dir = subprocess.check_output(["llvm-config-18", "--cmakedir"], text=True).strip()
    metadata = {"revisions": REVISIONS, "machine": platform.machine(), "uname": list(platform.uname()),
                "cpu_affinity": cpu, "parameters": {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
                "runner_name": os.environ.get("RUNNER_NAME"), "run_id": os.environ.get("GITHUB_RUN_ID"),
                "workflow_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
                "size_policy": "Unstripped O2 ELF executable (including linked runtime) and relocatable object file bytes; no size threshold.",
                "timing_policy": "Warm-cache repeated hsc --timing-json total; wall duration also retained. No compile-time threshold."}
    for command in (["lscpu"], ["clang++-18", "--version"], ["llvm-config-18", "--version"], ["cmake", "--version"]):
        metadata[command[0]] = subprocess.check_output(command, text=True)
    write_json(output / "session.json", metadata)
    states, manifests = {}, {}
    for index, (label, sha) in enumerate(REVISIONS.items()):
        # Checked programs embed source paths. Equal-length neutral directories
        # prevent release-label length from changing emitted bytes.
        source, build = output.parent / (output.name + "-sources") / f"r{index:02d}", output / label / "build"
        source.parent.mkdir(parents=True, exist_ok=True)
        run(["git", "worktree", "add", "--detach", str(source), sha], cwd=root)
        actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
        if actual != sha:
            raise ValueError(f"revision mismatch: {label}")
        manifests[label] = source_hashes(source)
        if manifests[label] != manifests["v0.3.8"]:
            raise ValueError(f"benchmark source mismatch: {label}")
        states[label] = source, build
        run(["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=ON",
             "-DHS_SMITH_REQUIRE_NETWORK_ISOLATION=ON", "-DLLVM_DIR=" + llvm_dir],
            log=output / label / "configure.log")
        run(["cmake", "--build", str(build), "--parallel", "4"], log=output / label / "build.log")
        run(["cmake", "-DBUILD_DIR=" + str(build), "-DTEST_REGEX=" + T8_REGEX,
             "-DJUNIT_OUTPUT_FILE=" + str(output / label / "t8-junit.xml"),
             "-P", str(root / "cmake/RunQuietTests.cmake")], log=output / label / "t8.log")
        verify_junit(output / label / "t8-junit.xml")
        write_json(output / label / "compiler.json", {"sha256": digest(build / "hsc"), "bytes": (build / "hsc").stat().st_size,
                  "version": subprocess.check_output([str(build / "hsc"), "--version"], text=True), "source_revision": actual})
    write_json(output / "source-sha256.json", manifests)
    reports = {}
    for label, (source, build) in states.items():
        bench = output / label / "benchmark"
        env = {**os.environ, "BENCHMARK_BUILD_DIR": str(bench), "BENCHMARK_REPORT": str(bench / "report.json"),
               "HSC": str(build / "hsc"), "CXX": "clang++-18", "OPT": "opt-18", "LLC": "llc-18",
               "LLVM_OBJDUMP": "llvm-objdump-18", "PERF_USE_SUDO": "1", "CPU": str(cpu)}
        run([str(source / "hsvsc++/hsvsc++/run_benchmark.sh"), str(args.runtime_runs),
             str(args.runtime_warmups), "0"], env=env, log=output / label / "benchmark.log")
        reports[label] = read_json(bench / "report.json")
    verify_reports(reports, manifests, args.runtime_runs, args.runtime_warmups)
    samples = collect_compilations(states, reports, output, cpu, args.compile_runs, args.compile_warmups)
    verify_compilations(samples, args.compile_runs, args.compile_warmups)
    summary = {"baseline": "v0.3.8", "session": metadata, "versions": summarize(reports, samples), "trend_exit_codes": {}}
    for label in list(REVISIONS)[1:]:
        summary["trend_exit_codes"][label] = run([
            sys.executable, str(root / "tests/performance/check_benchmark_trend.py"), "--gate",
            "--baseline", str(output / "v0.3.8/benchmark/report.json"),
            "--current", str(output / label / "benchmark/report.json"),
            "--output", str(output / label / "trend-vs-v0.3.8.json")],
            log=output / label / "trend.log", check=False)
    write_json(output / "comparison.json", summary)
    print(json.dumps(summary, indent=2))
    return int(any(summary["trend_exit_codes"].values()))


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
