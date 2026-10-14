# Character-provider low-byte contract

Plan: PR 21
Issues: STDLIB-001

## Scope and evidence

Standard §14.7 classifies the lowest byte and returns an ASCII case-converted
`u8`, leaving other bytes unchanged. This applies when an input contains high
bits or is negative. For example, `is_digit(0x130)` is true, `to_upper(0x161)` is
65, and `to_lower(0x141)` is 97. The corresponding negative inputs -208, -159,
and -191 have the same results.

The optimized provider already masks its input with `0xff`. The reference
`stdlib/ctype.hs` previously compared the whole `i32`, producing false for
those classifications and skipping case conversion. Its primitive operations
now normalize before testing ranges; `is_alnum` composes the normalized digit
and alpha operations. No language rule or optimized lowering is changed.

## Regression gate

`tests/cases/run/ctype_reference.hs` retains the original boundary checks and
checks all 256 byte values with six prefixes: zero, 256, -256, `0x123400`,
`0x7fffff00`, and -2147483648. Each input is checked against independently
computed ASCII expectations for all six functions, including unchanged NUL,
DEL, and non-ASCII bytes. Boolean expectations use logical truth tests.

The `ctype-providers` manifest runs both providers across unchecked,
static-checked, and checked modes at O0 and O2, with exact successful compile
and execution results: exit 0 and empty stdout/stderr. It uses the existing
harness's optimization and safety-mode differential checks and preserves each
variant's artifacts.

The former nested shell loop could return success after an early compilation
or execution failure if its final variant succeeded. The CTest entry now uses
the harness's aggregate result. Harness unit tests inject a compilation
failure, execution failure, wrong stdout, and wrong stderr into each of the
12 variants and require a failing aggregate result each time. They also check
the exact matrix and clean baseline.

## Acceptance

```sh
python3 tests/support/harness/test_run_tests.py
ctest --test-dir build -R '^hsc_ctype_' --output-on-failure
cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake
```

The low-byte fixture fails with the old reference library and passes with the
fixed library in every matrix cell. Full native builds and CI for the exact PR
head are required before merge; a released compiler used for local object
compilation is only focused evidence, not a replacement for that gate.
