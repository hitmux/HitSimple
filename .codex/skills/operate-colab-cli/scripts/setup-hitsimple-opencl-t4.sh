#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 || -z "${1:-}" ]]; then
  printf '用法: %s <已创建的 T4 session 名称>\n' "$0" >&2
  exit 2
fi

session=$1
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/../../../.." && pwd)
remote_root=${HITSIMPLE_REMOTE_ROOT:-"/content/hitsimple-${session}"}
remote_source=${HITSIMPLE_DRIVE_REMOTE:-"Google:ColabArchives/HitSimple/${session}/source.tar.gz"}
build_jobs=${HITSIMPLE_BUILD_JOBS:-2}
reuse_remote_root=${HITSIMPLE_REUSE_REMOTE_ROOT:-0}
temp_dir=$(mktemp -d "${TMPDIR:-/tmp}/hitsimple-colab.XXXXXX")
archive="$temp_dir/hitsimple-source.tar.gz"

cleanup() {
  rm -rf "$temp_dir"
}
trap cleanup EXIT

require_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    printf '错误: 找不到命令 %s\n' "$1" >&2
    exit 127
  fi
}

for command in colab rclone sha256sum tar; do
  require_command "$command"
done

if [[ ! -f "$HOME/.config/rclone/rclone.conf" ]]; then
  printf '错误: 找不到 rclone 配置 %s\n' "$HOME/.config/rclone/rclone.conf" >&2
  exit 1
fi
if [[ ! "$build_jobs" =~ ^[1-9][0-9]*$ ]]; then
  printf '错误: HITSIMPLE_BUILD_JOBS 必须为正整数\n' >&2
  exit 2
fi
if [[ ! "$remote_root" =~ ^/content/[A-Za-z0-9._/-]+$ ]]; then
  printf '错误: HITSIMPLE_REMOTE_ROOT 必须位于 /content 下且不包含空格\n' >&2
  exit 2
fi

status=$(colab status -s "$session")
printf '%s\n' "$status"
if [[ "$status" != *"Hardware: T4 | Variant: GPU"* ]]; then
  printf '错误: session %s 不是已确认的 T4 GPU runtime\n' "$session" >&2
  exit 1
fi

tar \
  --exclude='./.git' \
  --exclude='./.codex' \
  --exclude='./build*' \
  --exclude='./cmake-build-*' \
  --exclude='./_CPack_Packages' \
  --exclude='./vscode' \
  --exclude='./research/gpu-compiler/papers' \
  --exclude='./*.tar.gz' \
  --exclude='./*.deb' \
  --exclude='./*.rpm' \
  -czf "$archive" -C "$repo_root" .

archive_sha256=$(sha256sum "$archive" | awk '{print $1}')
rclone copyto "$archive" "$remote_source"

mount_state=$(colab exec -s "$session" --timeout 30 <<'PY' || true
from pathlib import Path
ready = (Path('/content/.colab-rclone').is_file() and
         Path('/content/.colab-rclone.conf').is_file() and
         Path('/content/drive/MyDrive').is_mount())
print('mounted' if ready else 'missing')
PY
)
if [[ "$mount_state" != *"mounted"* ]]; then
  mounted=0
  for attempt in 1 2 3; do
    if bash "$script_dir/mount-rclone-drive.sh" "$session"; then
      mounted=1
      break
    fi
    sleep 2
  done
  if [[ "$mounted" -ne 1 ]]; then
    printf '错误: 无法为 session %s 挂载 Google Drive\n' "$session" >&2
    exit 1
  fi
fi

remote_root_b64=$(printf '%s' "$remote_root" | base64 -w0)
remote_source_b64=$(printf '%s' "$remote_source" | base64 -w0)
archive_sha256_b64=$(printf '%s' "$archive_sha256" | base64 -w0)
reuse_remote_root_b64=$(printf '%s' "$reuse_remote_root" | base64 -w0)
build_jobs_b64=$(printf '%s' "$build_jobs" | base64 -w0)

colab exec -s "$session" --timeout 1800 <<PY
from pathlib import Path
import base64
import hashlib
import re
import subprocess
import tarfile

remote_root = Path(base64.b64decode('$remote_root_b64').decode())
remote_source = base64.b64decode('$remote_source_b64').decode()
expected_sha256 = base64.b64decode('$archive_sha256_b64').decode()
reuse_remote_root = base64.b64decode('$reuse_remote_root_b64').decode() == '1'
build_jobs = base64.b64decode('$build_jobs_b64').decode()
rclone = '/content/.colab-rclone'
config = '/content/.colab-rclone.conf'
archive = Path('/content/hitsimple-source.tar.gz')

subprocess.run([
    'nvidia-smi', '--query-gpu=name,memory.total,driver_version',
    '--format=csv,noheader',
], check=True)

llvm_config = Path('/usr/lib/llvm-19/lib/cmake/llvm/LLVMConfig.cmake')
if not llvm_config.is_file():
    source = 'deb [signed-by=/usr/share/keyrings/llvm-archive-keyring.gpg] https://apt.llvm.org/jammy/ llvm-toolchain-jammy-19 main\n'
    release = 'https://apt.llvm.org/jammy/dists/llvm-toolchain-jammy-19/Release'
    key_url = 'https://apt.llvm.org/llvm-snapshot.gpg.key'
    subprocess.run(['curl', '--fail', '--silent', '--show-error', '--head', release], check=True)
    key = subprocess.run(
        ['curl', '--fail', '--silent', '--show-error', '--location', key_url],
        check=True, stdout=subprocess.PIPE,
    ).stdout
    keyring = Path('/usr/share/keyrings/llvm-archive-keyring.gpg')
    keyring.write_bytes(subprocess.run(['gpg', '--dearmor'], input=key, check=True,
                                       stdout=subprocess.PIPE).stdout)
    Path('/etc/apt/sources.list.d/llvm-19.list').write_text(source)

subprocess.run(['apt-get', 'update', '-qq'], check=True)
subprocess.run([
    'apt-get', 'install', '-y', '-qq',
    'build-essential', 'bison', 're2c', 'ninja-build', 'clinfo',
    'llvm-19-dev', 'clang-19',
], check=True)

subprocess.run([
    rclone, 'copyto', remote_source, str(archive), f'--config={config}',
], check=True)
actual_sha256 = hashlib.sha256(archive.read_bytes()).hexdigest()
if actual_sha256 != expected_sha256:
    raise RuntimeError('Drive source archive SHA-256 mismatch')

if remote_root.exists() and not reuse_remote_root:
    raise RuntimeError(f'{remote_root} already exists; set HITSIMPLE_REUSE_REMOTE_ROOT=1 to reuse it')
remote_root.mkdir(parents=True, exist_ok=True)
with tarfile.open(archive, 'r:gz') as source_archive:
    source_archive.extractall(remote_root, filter='data')

build = remote_root / 'build-llvm19-gcc'
subprocess.run([
    'cmake', '-S', str(remote_root), '-B', str(build), '-G', 'Ninja',
    '-DLLVM_DIR=/usr/lib/llvm-19/lib/cmake/llvm',
    '-DCMAKE_C_COMPILER=/usr/bin/gcc',
    '-DCMAKE_CXX_COMPILER=/usr/bin/g++',
], check=True)
subprocess.run(['cmake', '--build', str(build), '--parallel', build_jobs], check=True)
subprocess.run([str(build / 'hsc'), '--version'], check=True)
clinfo = subprocess.run(['clinfo'], check=True, text=True, stdout=subprocess.PIPE).stdout
if not re.search(r'Device Type\\s+GPU', clinfo):
    raise RuntimeError('OpenCL T4 capability check failed: GPU device not found')
if not re.search(r'Device OpenCL C Version\\s+OpenCL C 1\\.2', clinfo):
    raise RuntimeError('OpenCL T4 capability check failed: OpenCL C 1.2 unavailable')
print(f'HitSimple ready: source={remote_root} build={build}')
PY
