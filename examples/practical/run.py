#!/usr/bin/env python3
"""Run the practical examples through the repository's existing test harnesses."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
CLI_EXAMPLES = ("c-import", "cpp-wrapper", "rust-staticlib", "checked-error", "ctype-providers")
EXAMPLES = (*CLI_EXAMPLES, "vscode-debug")


def successful_process() -> dict:
    return {"exit_code": 0, "stdout": "", "stderr": ""}


def manifest_for(example: str) -> dict:
    """Use canonical fixtures and the runtime harness's strict output oracle."""
    def case(name: str, source: str, args: list[str] | None = None) -> dict:
        return {
            "name": name,
            "source": str(ROOT / source),
            **({"compile_args": args} if args else {}),
            "expect_by_mode": {"unchecked": {
                "compile": successful_process(), "run": successful_process(),
            }},
        }

    interop = ROOT / "tests/cases/interop"
    if example == "c-import":
        cases = [case(example, "tests/cases/interop/c_abi_import.hs", [
            "--c-source", str(interop / "c_abi_import_helper.c"),
        ])]
    elif example == "cpp-wrapper":
        cases = [case(example, "tests/cases/interop/hsc_calls_cpp.hs", [
            "--cxx-source", str(interop / "cpp_wrapper.cpp"),
        ])]
    elif example == "rust-staticlib":
        cases = [case(example, "tests/cases/interop/hsc_calls_rust.hs", [
            "--cargo-manifest", str(interop / "rust_staticlib/Cargo.toml"),
            "--cargo-profile", "release", "--cargo-features", "bonus",
        ])]
        # Cargo progress is toolchain-dependent; executable output remains exact.
        cases[0]["expect_by_mode"]["unchecked"]["compile"] = {
            "exit_code": 0, "stdout_regex": "(?s).*", "stderr_regex": "(?s).*",
        }
    elif example == "checked-error":
        # Keep the safety oracle in its existing manifest, including exit 120,
        # error class and source location. Never select an unchecked variant.
        safety = json.loads((ROOT / "tests/support/harness/manifests/checked-differential.json").read_text())
        original = next(item for item in safety["tests"]
                        if item["name"] == "dynamic_out_of_bounds_store")
        item = case(example, "tests/cases/run/checked_oob.hs")
        item["safety"] = "checked"
        item["expect_by_mode"] = {"checked": original["expect_by_mode"]["checked"]}
        cases = [item]
    elif example == "ctype-providers":
        cases = []
        for provider in ("optimized", "reference"):
            item = case(provider, "tests/cases/run/ctype_reference.hs", [
                "--stdlib-provider=" + provider,
            ])
            item["safety"] = ["unchecked", "static-checked", "checked"]
            item["expect_by_mode"] = {
                mode: {"compile": successful_process(), "run": successful_process()}
                for mode in item["safety"]
            }
            cases.append(item)
    else:
        raise ValueError("not a CLI example: " + example)
    return {
        "version": 2,
        "suite": "practical-" + example,
        "defaults": {
            "targets": ["host"], "safety": "unchecked",
            "optimization": "O0", "timeout_seconds": 120,
        },
        "tests": cases,
    }


def run_cli(example: str, hsc: Path, artifacts: Path) -> int:
    artifacts.mkdir(parents=True, exist_ok=True)
    manifest = artifacts / (example + ".json")
    manifest.write_text(json.dumps(manifest_for(example), indent=2) + "\n", encoding="utf-8")
    return subprocess.call([
        sys.executable, str(ROOT / "tests/support/harness/run_tests.py"),
        "--hsc", str(hsc), "--manifest", str(manifest),
        "--artifacts", str(artifacts),
    ])


def run_debug(hsc: Path) -> int:
    extension = ROOT / "vscode/hitsimple"
    if not (extension / "node_modules/@vscode/test-electron").is_dir():
        raise ValueError("first run: npm ci --prefix vscode/hitsimple")
    npm = shutil.which("npm")
    if npm is None:
        raise ValueError("vscode-debug requires Node.js and npm")
    # This is the real extension-host breakpoint/stack/local-variable suite,
    # not a CLI smoke test or a simulated debug adapter.
    return subprocess.call(
        [npm, "run", "test:extension"], cwd=extension,
        env={**os.environ, "HSC_PATH": str(hsc)},
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("example", choices=(*EXAMPLES, "cli", "all"),
                        help="cli runs the first five; all also runs the VS Code suite")
    parser.add_argument("--hsc", type=Path, default=ROOT / "build/hsc")
    parser.add_argument("--artifacts", type=Path, default=ROOT / "build/practical-examples")
    args = parser.parse_args(argv)
    if platform.system() != "Linux" or platform.machine().lower() not in ("x86_64", "aarch64", "arm64"):
        raise ValueError("these practical entry commands require native Linux x86_64 or AArch64")
    hsc = args.hsc.resolve()
    if not hsc.is_file() or not os.access(hsc, os.X_OK):
        raise ValueError("--hsc must name an executable compiler: " + str(hsc))
    selected = EXAMPLES if args.example == "all" else (
        CLI_EXAMPLES if args.example == "cli" else (args.example,))
    for example in selected:
        print("=== " + example + " ===", flush=True)
        result = run_debug(hsc) if example == "vscode-debug" else run_cli(
            example, hsc, args.artifacts.resolve())
        if result != 0:
            return result
        print("PASS " + example, flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print("practical examples: " + str(error), file=sys.stderr)
        raise SystemExit(2)
