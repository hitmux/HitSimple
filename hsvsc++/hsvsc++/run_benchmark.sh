#!/usr/bin/env bash
set -euo pipefail

export LC_ALL=C

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${BENCHMARK_BUILD_DIR:-$root_dir/build}"
runs="${1:-15}"
warmups="${2:-3}"
cooldown_seconds="${3:-1}"

resolve_executable() {
    local candidate="$1"
    local resolved

    if [[ "$candidate" == */* ]]; then
        if [[ ! -x "$candidate" ]]; then
            echo "不可执行文件：$candidate" >&2
            return 1
        fi
        resolved="$(cd "$(dirname "$candidate")" && pwd -P)/$(basename "$candidate")"
    else
        resolved="$(command -v "$candidate" || true)"
        if [[ -z "$resolved" ]]; then
            echo "未在 PATH 中找到可执行文件：$candidate" >&2
            return 1
        fi
    fi

    printf '%s\n' "$resolved"
}

default_hsc="$root_dir/../../build/hsc"
hsc="$(resolve_executable "${HSC:-$default_hsc}")"
cxx="$(resolve_executable "${CXX:-clang++-18}")"

if ! [[ "$runs" =~ ^[1-9][0-9]*$ && "$warmups" =~ ^[0-9]+$ && "$cooldown_seconds" =~ ^[0-9]+$ ]]; then
    echo "用法: $0 [测量次数>=1] [预热次数>=0] [测量间隔秒数>=0]" >&2
    exit 2
fi

mkdir -p "$build_dir"

timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
run_id="$timestamp-$$"
timestamp_log="$build_dir/benchmark-$run_id.log"
benchmark_report="${BENCHMARK_REPORT:-$build_dir/benchmark-$run_id.json}"
samples_dir="$build_dir/benchmark-$run_id-samples"
mkdir -p "$(dirname "$benchmark_report")" "$samples_dir"
exec > >(tee "$timestamp_log" "$build_dir/benchmark-latest.log") 2>&1

cpu_model=""
if [[ -r /proc/cpuinfo ]]; then
    cpu_model="$(awk -F: '
        $1 ~ /^[[:space:]]*(model name|Model|Hardware)[[:space:]]*$/ {
            value = $2
            sub(/^[[:space:]]+/, "", value)
            if (value != "") {
                print value
                exit
            }
        }
    ' /proc/cpuinfo)"
fi
if [[ -z "$cpu_model" ]] && command -v lscpu >/dev/null 2>&1; then
    cpu_model="$(LC_ALL=C lscpu | awk -F: '
        $1 ~ /^[[:space:]]*(Model name|Model|Hardware)[[:space:]]*$/ {
            value = $2
            sub(/^[[:space:]]+/, "", value)
            if (value != "") {
                print value
                exit
            }
        }
    ')"
fi
cpu_model="${cpu_model:-unknown}"

cpu="${CPU:-}"
if [[ -z "$cpu" ]]; then
    cpu="$(taskset -pc "$$" | awk -F': ' '{ split($2, groups, ","); split(groups[1], bounds, "-"); print bounds[1] }')"
fi

if ! [[ "$cpu" =~ ^[0-9]+$ ]]; then
    echo "无法确定可用 CPU；请使用 CPU=<编号> $0 运行。" >&2
    exit 2
fi

cpu_governor="unknown"
governor_path="/sys/devices/system/cpu/cpu${cpu}/cpufreq/scaling_governor"
if [[ -r "$governor_path" ]]; then
    cpu_governor="$(<"$governor_path")"
fi

git_commit="$(git -C "$root_dir/../.." rev-parse HEAD 2>/dev/null || printf 'unavailable')"
hsc_version="$("$hsc" --version)"
cxx_version="$("$cxx" --version | sed -n '1p')"
target_triple="$("$cxx" -dumpmachine)"
opt="$(resolve_executable "${OPT:-opt}")"
llc="$(resolve_executable "${LLC:-llc}")"
llvm_objdump="$(resolve_executable "${LLVM_OBJDUMP:-llvm-objdump}")"

printf '权威基准口径: 同一主机、同一 CPU pinning、C++ 与 HitSimple 均使用 -O2；HitSimple 固定使用 --unchecked。\n'
printf 'resolved hsc path: %s\n' "$hsc"
printf '%s\n' "$hsc_version"
printf 'resolved C++ compiler path: %s\n' "$cxx"
printf '%s\n' "$cxx_version"
printf 'Git commit: %s\n' "$git_commit"
printf 'HS flags: -O2 --unchecked\n'
printf 'C++ flags: -O2 -ffp-contract=off -std=c++20 -Wall -Wextra -Wpedantic -Werror\n'
printf 'CPU model: %s\n' "$cpu_model"
printf 'CPU governor: %s\n' "$cpu_governor"
printf 'CPU affinity: %s\n' "$cpu"
printf 'target triple: %s\n' "$target_triple"
printf 'OS/kernel: %s\n' "$(uname -srmo)"
printf 'log archive: %s\n' "$timestamp_log"
printf 'raw samples: %s\n' "$samples_dir"
printf 'JSON report: %s\n' "$benchmark_report"
metrics_dir="$build_dir/benchmark-$run_id-timing"
native_artifacts_dir="$build_dir/benchmark-$run_id-native-artifacts"
mkdir -p "$metrics_dir" "$native_artifacts_dir"
native_quality_report="$native_artifacts_dir/quality.json"

declare -a workloads=(mandelbrot memory bitops integer_sum branch function_call stdlib checked_loop c_abi)
declare -A labels=(
    [mandelbrot]=mandelbrot [memory]=memory [bitops]=bitops
    [integer_sum]=integer_sum [branch]=branch [function_call]=function_call
    [stdlib]=stdlib [checked_loop]=checked_loop [c_abi]=c_abi
)
declare -A cpp_modes=(
    [mandelbrot]=mandelbrot [memory]=memory [bitops]=bitops
    [integer_sum]=integer_sum [branch]=branch [function_call]=function_call
    [stdlib]=stdlib [checked_loop]=checked_loop [c_abi]=c_abi
)
declare -A hs_sources=(
    [mandelbrot]="$root_dir/benchmark_mandelbrot.hs"
    [memory]="$root_dir/benchmark_memory.hs"
    [bitops]="$root_dir/benchmark_bitops.hs"
    [integer_sum]="$root_dir/benchmark_integer_sum.hs"
    [branch]="$root_dir/benchmark_branch.hs"
    [function_call]="$root_dir/benchmark_function_call.hs"
    [stdlib]="$root_dir/benchmark_stdlib.hs"
    [checked_loop]="$root_dir/benchmark_checked_loop.hs"
    [c_abi]="$root_dir/benchmark_c_abi.hs"
)
declare -A hs_bins
declare -A safety_modes

helper_object="$build_dir/benchmark-c-abi-helper.o"
"$cxx" -x c -O2 -Wall -Wextra -Wpedantic -Werror -c \
    "$root_dir/benchmark_c_abi_helper.c" -o "$helper_object"
"$cxx" -O2 -ffp-contract=off -std=c++20 -Wall -Wextra -Wpedantic -Werror \
    "$root_dir/benchmark.cpp" "$helper_object" -o "$build_dir/benchmark-cpp"

for workload in "${workloads[@]}"; do
    hs_bins[$workload]="$build_dir/hs-$workload"
    safety_modes[$workload]=unchecked
done
safety_modes[checked_loop]=checked

for workload in "${workloads[@]}"; do
    timing_json="$metrics_dir/$workload.json"
    if [[ "$workload" == c_abi ]]; then
        "$hsc" -O2 --unchecked "--timing-json=$timing_json" "${hs_sources[$workload]}" \
            --c-source "$root_dir/benchmark_c_abi_helper.c" -o "${hs_bins[$workload]}"
    else
        "$hsc" -O2 "--${safety_modes[$workload]}" "--timing-json=$timing_json" \
            "${hs_sources[$workload]}" -o "${hs_bins[$workload]}"
    fi
done

declare -a native_artifact_workloads=(mandelbrot memory bitops integer_sum branch function_call stdlib c_abi)
for workload in "${native_artifact_workloads[@]}"; do
    artifact_dir="$native_artifacts_dir/$workload"
    mkdir -p "$artifact_dir"
    for level in O0 O2 O3; do
        "$hsc" "-$level" --unchecked \
            "--timing-json=$metrics_dir/$workload-$level-object.json" \
            --emit-object "${hs_sources[$workload]}" -o "$artifact_dir/hsc-$level.o"
        "$llvm_objdump" -d --no-show-raw-insn "$artifact_dir/hsc-$level.o" \
            >"$artifact_dir/hsc-$level.objdump"
    done
    "$hsc" --emit-llvm "${hs_sources[$workload]}" -o "$artifact_dir/raw.ll"
    "$opt" -O2 -S "$artifact_dir/raw.ll" -o "$artifact_dir/optimized-O2.ll"
    for level in O0 O2; do
        "$llc" "-$level" -filetype=obj "$artifact_dir/optimized-O2.ll" \
            -o "$artifact_dir/llc-$level.o"
        "$llvm_objdump" -d --no-show-raw-insn "$artifact_dir/llc-$level.o" \
            >"$artifact_dir/llc-$level.objdump"
    done
done
python3 "$root_dir/../../tests/performance/analyze_native_artifacts.py" \
    --artifacts "$native_artifacts_dir" --output "$native_quality_report"

cpp_bin="$build_dir/benchmark-cpp"
"$cpp_bin" check >/dev/null

extract_checksum() {
    local label="$1"
    awk -F= -v label="$label" '$1 == label { print $2 }'
}

declare -A checksums
for workload in "${workloads[@]}"; do
    label="${labels[$workload]}"
    cpp_checksum="$("$cpp_bin" "${cpp_modes[$workload]}" | extract_checksum "${label}_checksum_i64")"
    hs_checksum="$("${hs_bins[$workload]}" | extract_checksum "${label}_checksum_i64")"
    if [[ -z "$cpp_checksum" || -z "$hs_checksum" || "$cpp_checksum" != "$hs_checksum" ]]; then
        printf '校验失败：%s C++=%s HS=%s\n' "$workload" "$cpp_checksum" "$hs_checksum" >&2
        exit 1
    fi
    checksums[$workload]="$hs_checksum"
done

time_ns() {
    local start_ns end_ns
    start_ns="$(date +%s%N)"
    taskset -c "$cpu" "$@" >/dev/null
    end_ns="$(date +%s%N)"
    printf '%s\n' "$((end_ns - start_ns))"
}

warm_up() {
    local executable="$1"
    shift
    local iteration
    for ((iteration = 0; iteration < warmups; ++iteration)); do
        taskset -c "$cpu" "$executable" "$@" >/dev/null
    done
}

record() {
    local output_file="$1"
    shift
    time_ns "$@" >>"$output_file"
    measurements_remaining=$((measurements_remaining - 1))
    if ((measurements_remaining > 0 && cooldown_seconds > 0)); then
        sleep "$cooldown_seconds"
    fi
}

summarize() {
    local workload="$1"
    local language="$2"
    local sample_file="$3"
    local statistics
    statistics="$(sort -n "$sample_file" | awk '
        { samples[NR] = $1; total += $1 }
        END {
            if (NR % 2 == 1) {
                median = samples[(NR + 1) / 2]
            } else {
                median = (samples[NR / 2] + samples[NR / 2 + 1]) / 2
            }
            printf "%.3f %.3f %.3f %.3f", samples[1] / 1000000, median / 1000000, total / NR / 1000000, samples[NR] / 1000000
        }
    ')"
    printf '%-16s %-12s %5s %12s\n' "$workload" "$language" "$runs" "$statistics"
}

median_ns() {
    sort -n "$1" | awk '
        { samples[NR] = $1 }
        END {
            if (NR % 2 == 1) {
                print samples[(NR + 1) / 2]
            } else {
                print (samples[NR / 2] + samples[NR / 2 + 1]) / 2
            }
        }
    '
}

declare -A cpp_samples hs_samples
measurements_remaining=$((runs * ${#workloads[@]} * 2))
for workload in "${workloads[@]}"; do
    cpp_samples[$workload]="$samples_dir/$workload-cpp.ns"
    hs_samples[$workload]="$samples_dir/$workload-hs.ns"
    warm_up "$cpp_bin" "${cpp_modes[$workload]}"
    warm_up "${hs_bins[$workload]}"
done

for ((iteration = 1; iteration <= runs; ++iteration)); do
    for workload in "${workloads[@]}"; do
        if ((iteration % 2 == 1)); then
            record "${cpp_samples[$workload]}" "$cpp_bin" "${cpp_modes[$workload]}"
            record "${hs_samples[$workload]}" "${hs_bins[$workload]}"
        else
            record "${hs_samples[$workload]}" "${hs_bins[$workload]}"
            record "${cpp_samples[$workload]}" "$cpp_bin" "${cpp_modes[$workload]}"
        fi
    done
done

workload_metadata="$samples_dir/workloads.tsv"
: >"$workload_metadata"
printf '结果校验: 所有 %s 项工作负载均与 C++ 参照一致。\n' "${#workloads[@]}"
printf 'CPU: %s；预热: %s；测量: %s 次；相邻计时间隔: %s 秒\n' "$cpu" "$warmups" "$runs" "$cooldown_seconds"
printf '%-16s %-12s %5s %12s\n' '任务' '语言' '次数' '最小/中位/平均/最大 (ms)'
for workload in "${workloads[@]}"; do
    summarize "$workload" 'C++' "${cpp_samples[$workload]}"
    summarize "$workload" 'HitSimple' "${hs_samples[$workload]}"
    cpp_median="$(median_ns "${cpp_samples[$workload]}")"
    hs_median="$(median_ns "${hs_samples[$workload]}")"
    printf 'HS/C++ 中位数比: %s=%sx\n' "$workload" \
        "$(awk -v hs="$hs_median" -v cpp="$cpp_median" 'BEGIN { printf "%.3f", hs / cpp }')"
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$workload" "${safety_modes[$workload]}" "${checksums[$workload]}" \
        "${cpp_samples[$workload]}" "${hs_samples[$workload]}" \
        "$metrics_dir/$workload.json" >>"$workload_metadata"
done

perf_dir="$build_dir/benchmark-$run_id-perf"
mkdir -p "$perf_dir"
perf_status=unavailable
if perf_path="$(command -v "${PERF:-perf}" 2>/dev/null)"; then
    perf_command=("$perf_path")
    if [[ "${PERF_USE_SUDO:-0}" == 1 ]]; then
        perf_command=(sudo -n "$perf_path")
    fi
    perf_status=collected
    for workload in mandelbrot memory; do
        if ! taskset -c "$cpu" "${perf_command[@]}" stat -r "$runs" \
            -e cycles,instructions,branches,branch-misses,cache-misses \
            -o "$perf_dir/$workload.txt" -- "${hs_bins[$workload]}"; then
            perf_status=unavailable_or_denied
        fi
    done
else
    printf 'perf is unavailable on this runner.\n' >"$perf_dir/status.txt"
fi

BENCHMARK_TIMESTAMP="$timestamp" \
BENCHMARK_GIT_COMMIT="$git_commit" \
BENCHMARK_HSC="$hsc" \
BENCHMARK_HSC_VERSION="$hsc_version" \
BENCHMARK_CXX="$cxx" \
BENCHMARK_CXX_VERSION="$cxx_version" \
BENCHMARK_TARGET_TRIPLE="$target_triple" \
BENCHMARK_CPU_MODEL="$cpu_model" \
BENCHMARK_CPU_GOVERNOR="$cpu_governor" \
BENCHMARK_CPU_AFFINITY="$cpu" \
BENCHMARK_RUNS="$runs" \
BENCHMARK_WARMUPS="$warmups" \
BENCHMARK_COOLDOWN_SECONDS="$cooldown_seconds" \
BENCHMARK_WORKLOAD_METADATA="$workload_metadata" \
BENCHMARK_NATIVE_ARTIFACTS="$native_artifacts_dir" \
BENCHMARK_NATIVE_QUALITY_REPORT="$native_quality_report" \
BENCHMARK_PERF_DIR="$perf_dir" \
BENCHMARK_PERF_STATUS="$perf_status" \
python3 - "$benchmark_report" <<'PY'
import json
import os
import sys


def samples(path):
    with open(path, encoding="utf-8") as handle:
        return [int(line.strip()) for line in handle if line.strip()]


def median(values):
    ordered = sorted(values)
    midpoint = len(ordered) // 2
    if len(ordered) % 2:
        return float(ordered[midpoint])
    return (ordered[midpoint - 1] + ordered[midpoint]) / 2.0


workloads = {}
with open(os.environ["BENCHMARK_WORKLOAD_METADATA"], encoding="utf-8") as handle:
    for line in handle:
        name, safety_mode, checksum, cpp_path, hs_path, timing_path = line.rstrip("\n").split("\t")
        cpp_samples = samples(cpp_path)
        hs_samples = samples(hs_path)
        workloads[name] = {
            "checksum": checksum,
            "safety_mode": safety_mode,
            "compilation_timing_json": timing_path,
            "cpp": {"median_ns": median(cpp_samples), "samples_ns": cpp_samples},
            "hitsimple": {"median_ns": median(hs_samples), "samples_ns": hs_samples},
        }


report = {
    "version": 2,
    "timestamp_utc": os.environ["BENCHMARK_TIMESTAMP"],
    "git_commit": os.environ["BENCHMARK_GIT_COMMIT"],
    "hsc": {"path": os.environ["BENCHMARK_HSC"], "version": os.environ["BENCHMARK_HSC_VERSION"]},
    "cxx": {"path": os.environ["BENCHMARK_CXX"], "version": os.environ["BENCHMARK_CXX_VERSION"]},
    "target_triple": os.environ["BENCHMARK_TARGET_TRIPLE"],
    "optimization_level": "O2",
    "cpu": {
        "model": os.environ["BENCHMARK_CPU_MODEL"],
        "governor": os.environ["BENCHMARK_CPU_GOVERNOR"],
        "affinity": int(os.environ["BENCHMARK_CPU_AFFINITY"]),
    },
    "warmups": int(os.environ["BENCHMARK_WARMUPS"]),
    "runs": int(os.environ["BENCHMARK_RUNS"]),
    "cooldown_seconds": int(os.environ["BENCHMARK_COOLDOWN_SECONDS"]),
    "native_artifacts": os.environ["BENCHMARK_NATIVE_ARTIFACTS"],
    "machine_quality": json.loads(
        open(os.environ["BENCHMARK_NATIVE_QUALITY_REPORT"], encoding="utf-8").read()
    ),
    "perf": {"status": os.environ["BENCHMARK_PERF_STATUS"], "directory": os.environ["BENCHMARK_PERF_DIR"]},
    "workloads": workloads,
}
with open(sys.argv[1], "w", encoding="utf-8") as handle:
    json.dump(report, handle, indent=2, sort_keys=True)
    handle.write("\n")
PY
