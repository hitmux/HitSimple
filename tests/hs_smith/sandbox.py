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
    resource_limiter: str | None = None


def detect(cwd: Path) -> SandboxPlan:
    """Use bubblewrap plus prlimit when the host permits a network namespace.

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
    limiter = shutil.which("prlimit")
    if not limiter:
        return SandboxPlan(False, False, (), "prlimit is unavailable")
    prefix = _unprivileged_prefix(bwrap, cwd)
    if (_probe([*prefix, "/bin/true"], cwd) and
            _probe([*prefix, limiter, "--cpu=1", "--", "/bin/true"], cwd)):
        return SandboxPlan(
            True,
            True,
            prefix,
            "bubblewrap network namespace with prlimit resource limits",
            resource_limiter=limiter,
        )

    fallback = _sudo_prefix(bwrap, cwd)
    if (fallback and _probe([*fallback, "/bin/true"], cwd) and
            _probe([*fallback, limiter, "--cpu=1", "--", "/bin/true"], cwd)):
        return SandboxPlan(
            True,
            True,
            fallback,
            "bubblewrap network namespace via sudo fallback with prlimit resource limits",
            _uid_process_count(os.getuid()),
            limiter,
        )

    return SandboxPlan(
        False,
        False,
        (),
        "bubblewrap network namespace or prlimit resource setup is unavailable",
    )


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
    if not plan.resource_limiter:
        raise RuntimeError("enabled sandbox is missing its required prlimit wrapper")
    cpu_limit = max(1, math.ceil(policy.timeout_seconds) + 1)
    process_limit = policy.process_limit + plan.existing_uid_processes
    return [
        *plan.command_prefix,
        plan.resource_limiter,
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
        # Without a user namespace, RLIMIT_NPROC is shared with the runner's
        # existing tasks. Preserve the policy as an additional child-process
        # budget when /proc lets us count those tasks. macOS has no /proc
        # equivalent available here, so do not install an unusably low limit.
        existing_processes = _uid_process_count(os.getuid())
        if existing_processes:
            set_limit(resource.RLIMIT_NPROC,
                      policy.process_limit + existing_processes)
        set_limit(resource.RLIMIT_FSIZE, policy.output_limit_bytes)
        cpu_limit = max(1, math.ceil(policy.timeout_seconds) + 1)
        set_limit(resource.RLIMIT_CPU, cpu_limit)

    return apply_limits
