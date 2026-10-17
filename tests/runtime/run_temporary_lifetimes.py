#!/usr/bin/env python3
"""Portable registry lifecycle observations; no expired object is dereferenced."""
import argparse
from pathlib import Path
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    executable = str(parser.parse_args().executable.resolve())
    cases = ("nested", "repeated4096", "empty", "shrink", "replace", "expired", "nested_expired")
    failures = []
    for case in cases:
        code = 120 if "expired" in case else 0
        error = ("hitsimple runtime error: invalid checked load at "
                 "temporary-lifetime.hs:17:9\n") if code else ""
        result = subprocess.run([executable, case], capture_output=True, text=True,
                                check=False, timeout=30)
        if (result.returncode, result.stdout, result.stderr) != (code, "", error):
            failures.append(f"{case}: exit={result.returncode}, stdout={result.stdout!r}, stderr={result.stderr!r}")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"All {len(cases)} temporary lifetime registry cases passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
