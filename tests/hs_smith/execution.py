"""Bounded process execution and replayable process-artifact writing."""

from __future__ import annotations

from dataclasses import asdict, dataclass
import json
import os
from pathlib import Path
import signal
import subprocess
from typing import Any, Sequence

from sandbox import SandboxPlan, SandboxPolicy, preexec, wrap


@dataclass(frozen=True)
class ProcessResult:
    command: tuple[str, ...]
    exit_code: int | None
    signal: int | None
    stdout: str
    stderr: str
    timed_out: bool
    output_limited: bool
    network_isolated: bool


def write_text(path: Path, content: str) -> None:
    path.write_text(content, encoding="utf-8")


def write_json(path: Path, value: Any) -> None:
    write_text(path, json.dumps(value, indent=2, sort_keys=True) + "\n")


def write_process(directory: Path, phase: str, result: ProcessResult) -> None:
    write_json(directory / (phase + ".command.json"), list(result.command))
    write_json(directory / (phase + ".result.json"), asdict(result) | {"command": list(result.command)})
    write_text(directory / (phase + ".stdout"), result.stdout)
    write_text(directory / (phase + ".stderr"), result.stderr)


def run_process(
    command: Sequence[str],
    cwd: Path,
    policy: SandboxPolicy,
    plan: SandboxPlan,
) -> ProcessResult:
    actual_command = tuple(wrap(plan, command, policy))
    stdout_path = cwd / ".process.stdout"
    stderr_path = cwd / ".process.stderr"
    timed_out = False
    try:
        with stdout_path.open("wb") as stdout_file, stderr_path.open("wb") as stderr_file:
            process = subprocess.Popen(
                actual_command,
                cwd=str(cwd),
                stdin=subprocess.DEVNULL,
                stdout=stdout_file,
                stderr=stderr_file,
                env=os.environ | {"TMPDIR": str(cwd)},
                start_new_session=True,
                preexec_fn=None if plan.enabled else preexec(policy),
            )
            try:
                return_code = process.wait(timeout=policy.timeout_seconds)
            except subprocess.TimeoutExpired:
                timed_out = True
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                return_code = process.wait()
    except OSError as error:
        return ProcessResult(actual_command, None, None, "", str(error) + "\n", False, False, plan.network_isolated)

    stdout, stdout_limited = _read_limited(stdout_path, policy.output_limit_bytes)
    stderr, stderr_limited = _read_limited(stderr_path, policy.output_limit_bytes)
    stdout_path.unlink(missing_ok=True)
    stderr_path.unlink(missing_ok=True)
    return ProcessResult(
        actual_command,
        return_code if return_code >= 0 else None,
        -return_code if return_code < 0 else None,
        stdout,
        stderr,
        timed_out,
        stdout_limited or stderr_limited,
        plan.network_isolated,
    )


def _read_limited(path: Path, limit: int) -> tuple[str, bool]:
    data = path.read_bytes()
    limited = len(data) > limit
    if limited:
        data = data[:limit] + b"\n[HS-Smith output limit reached]\n"
    return data.decode("utf-8", errors="replace"), limited
