"""Best-effort network namespace plus mandatory POSIX resource limits."""

from __future__ import annotations

from dataclasses import dataclass
import math
import os
from pathlib import Path
import shutil
import subprocess
from typing import Callable, Sequence


@dataclass(frozen=True)
class SandboxPolicy:
    timeout_seconds: float = 5.0
    memory_bytes: int = 512 * 1024 * 1024
    process_limit: int = 16
    output_limit_bytes: int = 1024 * 1024


@dataclass(frozen=True)
class SandboxPlan:
    enabled: bool
    network_isolated: bool
    command_prefix: tuple[str, ...]
    reason: str


def detect(cwd: Path) -> SandboxPlan:
    """Use bubblewrap when the host permits a network namespace.

    The fallback still applies rlimits and an isolated working directory, but
    reports that it cannot make the network claim.  CI can make this a hard
    requirement with ``--require-network-isolation`` in the runner.
    """

    bwrap = shutil.which("bwrap")
    if not bwrap:
        return SandboxPlan(False, False, (), "bubblewrap is unavailable")
    probe = [bwrap, "--unshare-net", "--ro-bind", "/", "/", "--proc", "/proc", "--dev", "/dev", "/bin/true"]
    try:
        result = subprocess.run(probe, cwd=str(cwd), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=3, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return SandboxPlan(False, False, (), "bubblewrap probe failed")
    if result.returncode != 0:
        return SandboxPlan(False, False, (), "bubblewrap network namespace is unavailable")
    return SandboxPlan(
        True,
        True,
        (
            bwrap,
            "--die-with-parent",
            "--new-session",
            "--unshare-net",
            "--ro-bind",
            "/",
            "/",
            "--bind",
            str(cwd),
            str(cwd),
            "--chdir",
            str(cwd),
            "--proc",
            "/proc",
            "--dev",
            "/dev",
            "--",
        ),
        "bubblewrap network namespace",
    )


def wrap(plan: SandboxPlan, command: Sequence[str], policy: SandboxPolicy) -> list[str]:
    if not plan.enabled:
        return list(command)
    limiter = shutil.which("prlimit")
    if not limiter:
        # This branch is normally unreachable on a bubblewrap-capable Linux
        # worker.  The caller still records the plan and applies no wrapper
        # rlimits rather than breaking namespace setup before the command.
        return [*plan.command_prefix, *command]
    cpu_limit = max(1, math.ceil(policy.timeout_seconds) + 1)
    return [
        *plan.command_prefix,
        limiter,
        "--as=" + str(policy.memory_bytes),
        "--nproc=" + str(policy.process_limit),
        "--fsize=" + str(policy.output_limit_bytes),
        "--cpu=" + str(cpu_limit),
        "--",
        *command,
    ]


def preexec(policy: SandboxPolicy) -> Callable[[], None] | None:
    if os.name == "nt":
        return None

    def apply_limits() -> None:
        import resource

        resource.setrlimit(resource.RLIMIT_AS, (policy.memory_bytes, policy.memory_bytes))
        resource.setrlimit(resource.RLIMIT_NPROC, (policy.process_limit, policy.process_limit))
        resource.setrlimit(resource.RLIMIT_FSIZE, (policy.output_limit_bytes, policy.output_limit_bytes))
        cpu_limit = max(1, math.ceil(policy.timeout_seconds) + 1)
        resource.setrlimit(resource.RLIMIT_CPU, (cpu_limit, cpu_limit))

    return apply_limits
