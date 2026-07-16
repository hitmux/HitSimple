#!/usr/bin/env bash
set -u

if [[ $# -ne 1 || -z "${1:-}" ]]; then
  printf '用法: %s <session-name>\n' "$0" >&2
  exit 2
fi

session=$1

if ! command -v colab >/dev/null 2>&1; then
  printf '错误: 找不到 colab CLI。\n' >&2
  exit 127
fi

printf '正在停止 Colab session: %s\n' "$session"
if ! colab stop -s "$session"; then
  printf '警告: stop 返回非零状态，继续查询服务端会话以确认是否已释放。\n' >&2
fi

if ! sessions_output=$(colab sessions 2>&1); then
  printf '错误: 无法查询服务端 session 列表。\n%s\n' "$sessions_output" >&2
  exit 1
fi

if grep -Fq "[$session]" <<<"$sessions_output"; then
  printf '错误: session %s 仍在服务端活动列表中。\n%s\n' "$session" "$sessions_output" >&2
  exit 1
fi

printf '已确认: session %s 不在服务端活动列表中。\n' "$session"
