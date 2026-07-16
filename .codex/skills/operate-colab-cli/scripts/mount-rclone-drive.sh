#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 || -z "${1:-}" ]]; then
  printf '用法: %s <session-name>\n' "$0" >&2
  exit 2
fi

session=$1
rclone_binary=${RCLONE_BINARY:-$(command -v rclone || true)}
rclone_config=${RCLONE_CONFIG:-$HOME/.config/rclone/rclone.conf}
rclone_remote=${RCLONE_REMOTE:-Google:}
mount_path=${RCLONE_MOUNT_PATH:-/content/drive/MyDrive}

if ! command -v colab >/dev/null 2>&1; then
  printf '错误: 找不到 colab CLI。\n' >&2
  exit 127
fi
if [[ -z "$rclone_binary" || ! -x "$rclone_binary" ]]; then
  printf '错误: 找不到可执行的 rclone: %s\n' "$rclone_binary" >&2
  exit 1
fi
if [[ ! -f "$rclone_config" ]]; then
  printf '错误: 找不到 rclone 配置: %s\n' "$rclone_config" >&2
  exit 1
fi
if [[ ! "$rclone_remote" =~ ^[A-Za-z0-9._-]+:$ ]]; then
  printf '错误: RCLONE_REMOTE 必须是简单 remote 名称，例如 Google:\n' >&2
  exit 2
fi
if [[ ! "$mount_path" =~ ^/content/[A-Za-z0-9._/-]+$ ]]; then
  printf '错误: RCLONE_MOUNT_PATH 必须位于 /content 下且不包含空格。\n' >&2
  exit 2
fi

printf '正在向 Colab session %s 上传 rclone 和现有凭据。\n' "$session"
colab upload -s "$session" "$rclone_binary" /content/.colab-rclone
colab upload -s "$session" "$rclone_config" /content/.colab-rclone.conf

remote_b64=$(printf '%s' "$rclone_remote" | base64 -w0)
mount_b64=$(printf '%s' "$mount_path" | base64 -w0)

colab exec -s "$session" --timeout 90 <<PY
from pathlib import Path
import base64
import os
import subprocess
import time

remote = base64.b64decode('$remote_b64').decode()
mount_path = base64.b64decode('$mount_b64').decode()
binary = '/content/.colab-rclone'
config = '/content/.colab-rclone.conf'
cache = '/content/.colab-rclone-cache'
log = '/content/.colab-rclone.log'

os.chmod(binary, 0o700)
os.chmod(config, 0o600)
Path(mount_path).mkdir(parents=True, exist_ok=True)
Path(cache).mkdir(parents=True, exist_ok=True)

if not Path('/dev/fuse').exists():
    raise RuntimeError('/dev/fuse 不存在，当前实例不支持 FUSE mount')

subprocess.run([binary, 'about', remote, f'--config={config}'], check=True,
               stdout=subprocess.DEVNULL)

if subprocess.run(['mountpoint', '-q', mount_path]).returncode != 0:
    command = [
        binary, 'mount', remote, mount_path,
        f'--config={config}',
        '--vfs-cache-mode=full',
        f'--cache-dir={cache}',
        '--dir-cache-time=12h',
        '--poll-interval=1m',
        '--daemon',
        '--daemon-wait=20s',
        f'--log-file={log}',
        '--log-level=INFO',
    ]
    subprocess.run(command, check=True, timeout=30)

for _ in range(20):
    if subprocess.run(['mountpoint', '-q', mount_path]).returncode == 0:
        print(f'Google Drive mounted at {mount_path}')
        break
    time.sleep(1)
else:
    details = Path(log).read_text(errors='replace')[-4000:] if Path(log).exists() else ''
    raise RuntimeError(f'rclone mount 失败\n{details}')
PY
