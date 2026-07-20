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
timestamp_log="$build_dir/benchmark-$timestamp.log"
exec > >(tee "$timestamp_log" "$build_dir/benchmark-latest.log") 2>&1

cpu_model="unknown"
if [[ -r /proc/cpuinfo ]]; then
    cpu_model="$(awk -F': ' '/model name/ { print $2; exit }' /proc/cpuinfo)"
fi

printf '权威基准口径: 同一主机、同一 CPU pinning、C++ 与 HitSimple 均使用 -O2；HitSimple 固定使用 --unchecked。\n'
printf 'resolved hsc path: %s\n' "$hsc"
"$hsc" --version
printf 'resolved C++ compiler path: %s\n' "$cxx"
"$cxx" --version | sed -n '1p'
printf 'HS flags: -O2 --unchecked\n'
printf 'C++ flags: -O2 -std=c++20 -Wall -Wextra -Wpedantic -Werror\n'
printf 'CPU model: %s\n' "$cpu_model"
printf 'target triple: %s\n' "$("$cxx" -dumpmachine)"
printf 'OS/kernel: %s\n' "$(uname -srmo)"
printf 'log archive: %s\n' "$timestamp_log"

"$cxx" -O2 -std=c++20 -Wall -Wextra -Wpedantic -Werror \
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

cpu="${CPU:-}"
if [[ -z "$cpu" ]]; then
    cpu="$(taskset -pc "$$" | awk -F': ' '{ split($2, groups, ","); split(groups[1], bounds, "-"); print bounds[1] }')"
fi

if ! [[ "$cpu" =~ ^[0-9]+$ ]]; then
    echo "无法确定可用 CPU；请使用 CPU=<编号> $0 运行。" >&2
    exit 2
fi

samples_dir="$(mktemp -d "$build_dir/.benchmark.XXXXXX")"
trap 'rm -rf "$samples_dir"' EXIT

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
