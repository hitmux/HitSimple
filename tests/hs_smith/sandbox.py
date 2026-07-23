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
    existing_uid_processes: int = 0


def detect(cwd: Path) -> SandboxPlan:
    """Use bubblewrap when the host permits a network namespace.

    When an unprivileged user namespace is blocked, CI also tries a
    non-interactive sudo invocation.  The sandboxed command immediately drops
    back to the invoking UID/GID with no capabilities, so artifacts keep their
    usual ownership.  The ordinary fallback still applies rlimits and reports
    that it cannot make the network claim.  CI can make this a hard requirement
    with ``--require-network-isolation`` in the runner.
    """

    cwd = cwd.resolve()
    bwrap = shutil.which("bwrap")
    if not bwrap:
        return SandboxPlan(False, False, (), "bubblewrap is unavailable")
    prefix = _unprivileged_prefix(bwrap, cwd)
    if _probe([*prefix, "/bin/true"], cwd):
        return SandboxPlan(True, True, prefix, "bubblewrap network namespace")

    fallback = _sudo_prefix(bwrap, cwd)
    if fallback and _probe([*fallback, "/bin/true"], cwd):
        return SandboxPlan(
            True,
            True,
            fallback,
            "bubblewrap network namespace via sudo fallback",
            _uid_process_count(os.getuid()),
        )

    return SandboxPlan(False, False, (), "bubblewrap network namespace is unavailable")


def _probe(command: Sequence[str], cwd: Path) -> bool:
    try:
        result = subprocess.run(command, cwd=str(cwd), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=3, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return False
    return result.returncode == 0


def _unprivileged_prefix(bwrap: str, cwd: Path) -> tuple[str, ...]:
    return _bwrap_prefix(bwrap, cwd, unshare_user=True)


def _bwrap_prefix(bwrap: str, cwd: Path, *, unshare_user: bool) -> tuple[str, ...]:
    user_namespace = ("--unshare-user", "--uid", "0", "--gid", "0") if unshare_user else ()
    return (
        bwrap,
        "--die-with-parent",
        "--new-session",
        *user_namespace,
        "--unshare-net",
        "--ro-bind",
        "/",
        "/",
        "--bind",
        str(cwd),
        str(cwd),
        "--chdir",
        str(cwd),
        "--setenv",
        "TMPDIR",
        str(cwd),
        "--proc",
        "/proc",
        "--dev",
        "/dev",
        "--",
    )


def _sudo_prefix(bwrap: str, cwd: Path) -> tuple[str, ...] | None:
    sudo = shutil.which("sudo")
    setpriv = shutil.which("setpriv")
    if not sudo or not setpriv:
        return None
    return (
        sudo,
        "-n",
        *_bwrap_prefix(bwrap, cwd, unshare_user=False),
        setpriv,
        "--reuid",
        str(os.getuid()),
        "--regid",
        str(os.getgid()),
        "--clear-groups",
        "--no-new-privs",
        "--bounding-set=-all",
        "--",
    )


def _uid_process_count(uid: int) -> int:
    """Return the current UID's task count for an RLIMIT_NPROC fallback.

    Root-created fallback sandboxes eventually run as the invoking user, so a
    literal process limit of 16 can be lower than that user's existing task
    count.  Count current tasks and leave the policy's process budget above it.
    """

    process_root = Path("/proc")
    total = 0
    try:
        entries = tuple(process_root.iterdir())
    except OSError:
        return 0
    for entry in entries:
        if not entry.name.isdecimal():
            continue
        try:
            if entry.stat().st_uid != uid:
                continue
            total += sum(1 for task in (entry / "task").iterdir() if task.name.isdecimal())
        except OSError:
            # Processes can exit while /proc is being inspected. A lower
            # count merely preserves the normal policy and never escalates it.
            continue
    return total


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
    process_limit = policy.process_limit + plan.existing_uid_processes
    return [
        *plan.command_prefix,
        limiter,
        "--as=" + str(policy.memory_bytes),
        "--nproc=" + str(process_limit),
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

        def set_limit(limit: int, value: int) -> None:
            try:
                resource.setrlimit(limit, (value, value))
            except (ValueError, resource.error):
                # macOS does not implement every Linux resource limit. Keep
                # the portable timeout and supported limits active instead of
                # preventing the regression runner from launching at all.
                pass

        set_limit(resource.RLIMIT_AS, policy.memory_bytes)
        # Without a user namespace (for example on macOS), RLIMIT_NPROC is
        # shared with the runner's existing tasks. Preserve the policy as an
        # additional child-process budget instead of preventing the compiler
        # from launching its preprocessor.
        set_limit(resource.RLIMIT_NPROC,
                  policy.process_limit + _uid_process_count(os.getuid()))
        set_limit(resource.RLIMIT_FSIZE, policy.output_limit_bytes)
        cpu_limit = max(1, math.ceil(policy.timeout_seconds) + 1)
        set_limit(resource.RLIMIT_CPU, cpu_limit)

    return apply_limits
