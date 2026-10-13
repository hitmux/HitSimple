#!/usr/bin/env python3
"""Run isolated, portable checked-file lifecycle cases against a C test driver.

Example:
  cc -std=c11 -Wall -Wextra -Wpedantic tests/runtime/CheckedFileHandleTests.c \
      -o /tmp/checked-file-handles
  python3 tests/runtime/run_checked_file_handles.py /tmp/checked-file-handles

Each negative case has its own process. The driver's stdio stubs ensure that a
missing runtime check fails safely instead of invoking undefined host libc I/O.
"""

import argparse
from pathlib import Path
import subprocess
import sys


OPERATIONS = (
    "check", "close", "get", "flush", "eof", "error", "seek", "tell",
    "read", "write", "put", "format", "scan", "scan_target",
    "read_zero", "write_zero", "put_zero", "format_empty", "scan_empty",
)
CONSOLE_OPERATIONS = {"format", "scan", "scan_target", "format_empty", "scan_empty"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    executable = str(args.executable.resolve())

    cases = [
        ((scenario,), 0, "")
        for scenario in (
            "open", "unknown", "reuse", "growth", "console", "unchecked",
            "failed_open",
        )
    ]
    closed_error = "hitsimple runtime error: closed file handle\n"
    for scenario in (
        "closed", "failed_close", "alias", "growth_closed", "unknown_closed",
        "unknown_failed_close",
    ):
        cases.extend(((scenario, operation), 120, closed_error)
                     for operation in OPERATIONS)
    cases.extend(
        (("null", operation), 120,
         "hitsimple runtime error: invalid file handle\n")
        for operation in OPERATIONS if operation not in CONSOLE_OPERATIONS
    )
    cases.append((("close_reentrant",), 120, closed_error))

    failures = 0
    for case, expected_code, expected_error in cases:
        try:
            result = subprocess.run(
                [executable, *case], capture_output=True, text=True,
                timeout=30, check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            print(f"FAIL {' '.join(case)}: {error}", file=sys.stderr)
            failures += 1
            continue
        if (result.returncode != expected_code or result.stdout != "" or
                result.stderr != expected_error):
            print(
                f"FAIL {' '.join(case)}: expected exit {expected_code}, "
                f"stderr {expected_error!r}; got exit {result.returncode}, "
                f"stdout {result.stdout!r}, stderr {result.stderr!r}",
                file=sys.stderr,
            )
            failures += 1
    if failures:
        print(f"{failures}/{len(cases)} checked-file cases failed", file=sys.stderr)
        return 1
    print(f"All {len(cases)} checked-file lifecycle cases passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
