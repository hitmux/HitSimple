#!/usr/bin/env python3
"""Exercise native/Cargo scratch ownership and parallel builds on Linux.

Each worker seeds the old PID-only names, then execs hsc without changing PID.
This deterministically detects deletion of someone else's existing directory;
parallel workers additionally exercise shared-TMPDIR executable/archive builds.
It does not require PID namespace privileges or claim to simulate them.
"""

import argparse
from contextlib import ExitStack
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time


PREFIXES = ("hitsimple-link", "hitsimple-staticlib", "hitsimple-cargo")
SENTINEL = "pre-existing scratch directory\n"


def worker(arguments: list[str]) -> None:
    root = Path(os.environ["TMPDIR"])
    for prefix in PREFIXES:
        directory = root / f"{prefix}-{os.getpid()}"
        directory.mkdir()
        (directory / "keep.txt").write_text(SENTINEL, encoding="utf-8")
    if sys.stdin.buffer.read(1) != b"\n":
        raise RuntimeError("worker was not released")
    os.execv(arguments[0], arguments)


def run(command: list[str]) -> None:
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise RuntimeError(
            f"{command!r} returned {result.returncode}\n"
            f"{result.stdout}{result.stderr}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hsc", required=True, type=Path)
    parser.add_argument("--clang", required=True, type=Path)
    parser.add_argument("--llvm-ar", required=True, type=Path)
    args = parser.parse_args()
    hsc = str(args.hsc.resolve())
    clang = str(args.clang.resolve())
    llvm_ar = str(args.llvm_ar.resolve())
    fixtures = Path(__file__).resolve().parents[1] / "cases" / "interop"

    with tempfile.TemporaryDirectory(prefix="hitsimple temp 测试 ") as temporary:
        root = Path(temporary)
        scratch = root / "shared scratch"
        scratch.mkdir()
        source = root / "main.hs"
        source.write_text("func main() {\n    return 0\n}\n", encoding="utf-8")
        cargo = root / "cargo"
        cargo.mkdir()
        shutil.copyfile(fixtures / "rust_staticlib" / "Cargo.toml", cargo / "Cargo.toml")
        shutil.copytree(fixtures / "rust_staticlib" / "src", cargo / "src")
        environment = dict(os.environ, TMPDIR=str(scratch), TMP=str(scratch),
                           TEMP=str(scratch), HITSIMPLE_LLVM_AR=llvm_ar,
                           CARGO_TARGET_DIR=str(root / "cargo-target"),
                           RUSTC_WRAPPER="", RUSTC_WORKSPACE_WRAPPER="")
        jobs = []
        for repeat in range(2):
            for kind in ("native", "mixed", "archive", "cargo"):
                output = root / f"{kind}-{repeat}"
                if kind == "native":
                    options = [str(source)]
                elif kind == "mixed":
                    options = [str(fixtures / "c_abi_import.hs"), "--c-source",
                               str(fixtures / "c_abi_import_helper.c")]
                elif kind == "archive":
                    options = ["--crate-type=staticlib",
                               str(fixtures / "c_abi_export.hs")]
                else:
                    options = [str(fixtures / "hsc_calls_rust.hs"),
                               "--cargo-manifest", str(cargo / "Cargo.toml"),
                               "--cargo-features", "bonus"]
                jobs.append((kind, output, [hsc, "--clang", clang, *options,
                                             "-o", str(output)]))

        processes = []
        with ExitStack() as stack:
            try:
                for kind, output, command in jobs:
                    log_path = output.with_suffix(".log")
                    log = stack.enter_context(log_path.open("w", encoding="utf-8"))
                    process = subprocess.Popen(
                        [sys.executable, str(Path(__file__).resolve()), "--worker",
                         *command], stdin=subprocess.PIPE, stdout=log,
                        stderr=subprocess.STDOUT, env=environment, start_new_session=True,
                    )
                    processes.append((kind, output, log_path, process))
                for _, _, _, process in processes:
                    process.stdin.write(b"\n")
                    process.stdin.close()
                deadline = time.monotonic() + 120
                for kind, _, log_path, process in processes:
                    if process.wait(timeout=max(0, deadline - time.monotonic())):
                        raise RuntimeError(
                            f"{kind} build failed\n{log_path.read_text(encoding='utf-8')}"
                        )
            finally:
                for _, _, _, process in processes:
                    if process.poll() is None:
                        try:
                            os.killpg(process.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                    process.wait()

        expected = set()
        for kind, output, _, process in processes:
            for prefix in PREFIXES:
                directory = scratch / f"{prefix}-{process.pid}"
                expected.add(directory)
                sentinel = directory / "keep.txt"
                if not sentinel.is_file() or sentinel.read_text(encoding="utf-8") != SENTINEL:
                    raise RuntimeError(f"{kind} build removed or changed {sentinel}")
                if set(directory.iterdir()) != {sentinel}:
                    raise RuntimeError(f"{kind} build wrote into unowned {directory}")
            if kind == "archive":
                run([llvm_ar, "t", str(output)])
                executable = output.with_suffix(".exe")
                run([clang, str(fixtures / "c_abi_export_caller.c"),
                     str(output), "-lm", "-o", str(executable)])
                run([str(executable)])
            else:
                run([str(output)])
        if set(scratch.iterdir()) != expected:
            raise RuntimeError("a build leaked a temporary directory")
    print("PASS: 8 parallel native/mixed/archive/Cargo builds; existing paths preserved; owned paths cleaned")
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--worker":
        worker(sys.argv[2:])
    else:
        raise SystemExit(main())
