#!/usr/bin/env bash
set -euo pipefail

export LC_ALL=C

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${BENCHMARK_BUILD_DIR:-$root_dir/build}"
runs="${1:-15}"
warmups="${2:-3}"
cooldown_seconds="${3:-5}"

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

cpu_model="unknown"
if [[ -r /proc/cpuinfo ]]; then
    cpu_model="$(awk -F': ' '/model name|Model|Hardware/ { print $2; exit }' /proc/cpuinfo)"
fi

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

"$cxx" -O2 -ffp-contract=off -std=c++20 -Wall -Wextra -Wpedantic -Werror \
    "$root_dir/benchmark.cpp" -o "$build_dir/benchmark-cpp"
"$hsc" -O2 --unchecked "$root_dir/benchmark_mandelbrot.hs" -o "$build_dir/hs-mandelbrot"
"$hsc" -O2 --unchecked "$root_dir/benchmark_memory.hs" -o "$build_dir/hs-memory"

cpp_bin="$build_dir/benchmark-cpp"
hs_mandelbrot_bin="$build_dir/hs-mandelbrot"
hs_memory_bin="$build_dir/hs-memory"

"$cpp_bin" check >/dev/null

extract_checksum() {
    local label="$1"
    awk -F= -v label="$label" '$1 == label { print $2 }'
}

cpp_mandelbrot_checksum="$("$cpp_bin" mandelbrot | extract_checksum mandelbrot_checksum_i64)"
hs_mandelbrot_checksum="$("$hs_mandelbrot_bin" | extract_checksum mandelbrot_checksum_i64)"
cpp_memory_checksum="$("$cpp_bin" memory | extract_checksum memory_checksum_i64)"
hs_memory_checksum="$("$hs_memory_bin" | extract_checksum memory_checksum_i64)"

if [[ "$cpp_mandelbrot_checksum" != "$hs_mandelbrot_checksum" || "$cpp_memory_checksum" != "$hs_memory_checksum" ]]; then
    echo "校验失败：C++ 与 HitSimple 输出不一致。" >&2
    printf 'mandelbrot: C++=%s HS=%s\n' "$cpp_mandelbrot_checksum" "$hs_mandelbrot_checksum" >&2
    printf 'memory: C++=%s HS=%s\n' "$cpp_memory_checksum" "$hs_memory_checksum" >&2
    exit 1
fi

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
    printf '%-14s %-12s %5s %12s\n' "$workload" "$language" "$runs" "$statistics"
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

mandelbrot_cpp_samples="$samples_dir/mandelbrot-cpp.ns"
mandelbrot_hs_samples="$samples_dir/mandelbrot-hs.ns"
memory_cpp_samples="$samples_dir/memory-cpp.ns"
memory_hs_samples="$samples_dir/memory-hs.ns"
measurements_remaining=$((runs * 4))

warm_up "$cpp_bin" mandelbrot
warm_up "$hs_mandelbrot_bin"
warm_up "$cpp_bin" memory
warm_up "$hs_memory_bin"

for ((iteration = 1; iteration <= runs; ++iteration)); do
    if ((iteration % 2 == 1)); then
        record "$mandelbrot_cpp_samples" "$cpp_bin" mandelbrot
        record "$mandelbrot_hs_samples" "$hs_mandelbrot_bin"
        record "$memory_cpp_samples" "$cpp_bin" memory
        record "$memory_hs_samples" "$hs_memory_bin"
    else
        record "$mandelbrot_hs_samples" "$hs_mandelbrot_bin"
        record "$mandelbrot_cpp_samples" "$cpp_bin" mandelbrot
        record "$memory_hs_samples" "$hs_memory_bin"
        record "$memory_cpp_samples" "$cpp_bin" memory
    fi
done

printf '结果校验: 通过（Mandelbrot=%s，内存=%s）\n' "$hs_mandelbrot_checksum" "$hs_memory_checksum"
printf 'CPU: %s；预热: %s；测量: %s 次；相邻计时间隔: %s 秒\n' "$cpu" "$warmups" "$runs" "$cooldown_seconds"
printf '%-14s %-12s %5s %12s\n' '任务' '语言' '次数' '最小/中位/平均/最大 (ms)'
summarize 'mandelbrot' 'C++' "$mandelbrot_cpp_samples"
summarize 'mandelbrot' 'HitSimple' "$mandelbrot_hs_samples"
summarize 'memory' 'C++' "$memory_cpp_samples"
summarize 'memory' 'HitSimple' "$memory_hs_samples"

mandelbrot_cpp_median="$(median_ns "$mandelbrot_cpp_samples")"
mandelbrot_hs_median="$(median_ns "$mandelbrot_hs_samples")"
memory_cpp_median="$(median_ns "$memory_cpp_samples")"
memory_hs_median="$(median_ns "$memory_hs_samples")"

printf 'HS/C++ 中位数比: Mandelbrot=%sx，内存=%sx\n' \
    "$(awk -v hs="$mandelbrot_hs_median" -v cpp="$mandelbrot_cpp_median" 'BEGIN { printf "%.3f", hs / cpp }')" \
    "$(awk -v hs="$memory_hs_median" -v cpp="$memory_cpp_median" 'BEGIN { printf "%.3f", hs / cpp }')"

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
BENCHMARK_MANDELBROT_CPP="$mandelbrot_cpp_samples" \
BENCHMARK_MANDELBROT_HS="$mandelbrot_hs_samples" \
BENCHMARK_MEMORY_CPP="$memory_cpp_samples" \
BENCHMARK_MEMORY_HS="$memory_hs_samples" \
BENCHMARK_MANDELBROT_CHECKSUM="$hs_mandelbrot_checksum" \
BENCHMARK_MEMORY_CHECKSUM="$hs_memory_checksum" \
BENCHMARK_MANDELBROT_CPP_MEDIAN="$mandelbrot_cpp_median" \
BENCHMARK_MANDELBROT_HS_MEDIAN="$mandelbrot_hs_median" \
BENCHMARK_MEMORY_CPP_MEDIAN="$memory_cpp_median" \
BENCHMARK_MEMORY_HS_MEDIAN="$memory_hs_median" \
python3 - "$benchmark_report" <<'PY'
import json
import os
import sys


def samples(variable):
    with open(os.environ[variable], encoding="utf-8") as handle:
        return [int(line.strip()) for line in handle if line.strip()]


def workload(prefix, checksum):
    cpp_samples = samples("BENCHMARK_" + prefix + "_CPP")
    hs_samples = samples("BENCHMARK_" + prefix + "_HS")
    return {
        "checksum": checksum,
        "cpp": {
            "median_ns": float(os.environ["BENCHMARK_" + prefix + "_CPP_MEDIAN"]),
            "samples_ns": cpp_samples,
        },
        "hitsimple": {
            "median_ns": float(os.environ["BENCHMARK_" + prefix + "_HS_MEDIAN"]),
            "samples_ns": hs_samples,
        },
    }


report = {
    "version": 1,
    "timestamp_utc": os.environ["BENCHMARK_TIMESTAMP"],
    "git_commit": os.environ["BENCHMARK_GIT_COMMIT"],
    "hsc": {"path": os.environ["BENCHMARK_HSC"], "version": os.environ["BENCHMARK_HSC_VERSION"]},
    "cxx": {"path": os.environ["BENCHMARK_CXX"], "version": os.environ["BENCHMARK_CXX_VERSION"]},
    "target_triple": os.environ["BENCHMARK_TARGET_TRIPLE"],
    "safety_mode": "unchecked",
    "optimization_level": "O2",
    "cpu": {
        "model": os.environ["BENCHMARK_CPU_MODEL"],
        "governor": os.environ["BENCHMARK_CPU_GOVERNOR"],
        "affinity": int(os.environ["BENCHMARK_CPU_AFFINITY"]),
    },
    "warmups": int(os.environ["BENCHMARK_WARMUPS"]),
    "runs": int(os.environ["BENCHMARK_RUNS"]),
    "cooldown_seconds": int(os.environ["BENCHMARK_COOLDOWN_SECONDS"]),
    "workloads": {
        "mandelbrot": workload("MANDELBROT", os.environ["BENCHMARK_MANDELBROT_CHECKSUM"]),
        "memory": workload("MEMORY", os.environ["BENCHMARK_MEMORY_CHECKSUM"]),
    },
}
with open(sys.argv[1], "w", encoding="utf-8") as handle:
    json.dump(report, handle, indent=2, sort_keys=True)
    handle.write("\n")
PY
