#!/usr/bin/env python3
"""Guard CI workflow contracts that GitHub Actions cannot validate locally."""

from __future__ import annotations

import json
import re
import unittest
from pathlib import Path


SOURCE_ROOT = Path(__file__).resolve().parents[2]


class WorkflowContractTests(unittest.TestCase):
    def release_version(self) -> str:
        cmake = (SOURCE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        match = re.search(r"^  VERSION (\d+\.\d+\.\d+)$", cmake, re.MULTILINE)
        self.assertIsNotNone(match)
        assert match is not None
        return match.group(1)

    def test_release_versions_match_compiler_extension_and_dispatch(self) -> None:
        version = self.release_version()
        extension = SOURCE_ROOT / "vscode/hitsimple"
        package = json.loads((extension / "package.json").read_text(encoding="utf-8"))
        lock = json.loads((extension / "package-lock.json").read_text(encoding="utf-8"))
        self.assertEqual(package["version"], version)
        self.assertEqual(lock["version"], version)
        self.assertEqual(lock["packages"][""]["version"], version)

        workflow = (SOURCE_ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        dispatch_version = re.search(
            r'^      version:\n(?:(?!^      \w).)*?^        default: "(\d+\.\d+\.\d+)"$',
            workflow,
            re.MULTILINE | re.DOTALL,
        )
        self.assertIsNotNone(dispatch_version)
        assert dispatch_version is not None
        self.assertEqual(dispatch_version.group(1), version)

    def test_release_package_documentation_matches_compiler(self) -> None:
        version = self.release_version()
        for name in ("README.md", "USAGE.md"):
            with self.subTest(document=name):
                document = (SOURCE_ROOT / name).read_text(encoding="utf-8")
                versions = re.findall(r"hitsimple(?:-vscode)?[-_](\d+\.\d+\.\d+)", document)
                self.assertTrue(versions)
                self.assertEqual(set(versions), {version})

    def test_release_bundle_uses_versioned_patch_notes(self) -> None:
        version = self.release_version()
        notes = (SOURCE_ROOT / "docs/releases" / f"{version}.md").read_text(encoding="utf-8")
        self.assertTrue(notes.startswith(f"# HitSimple {version}\n"))
        workflow = (SOURCE_ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        self.assertIn(
            'cp "$GITHUB_WORKSPACE/docs/releases/${HITSIMPLE_VERSION}.md" ../release-notes.txt',
            workflow,
        )
        cmake = (SOURCE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn(
            "install(FILES docs/releases/${PROJECT_VERSION}.md\n"
            "  DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs/releases\n)",
            cmake,
        )

    def test_checked_file_handle_documentation_is_installed(self) -> None:
        document = "docs/checked-file-handles.md"
        self.assertTrue((SOURCE_ROOT / document).is_file())
        cmake = (SOURCE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn(
            f"install(FILES {document}\n"
            "  DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs\n)", cmake,
        )
        self.assertIn("hsc_installed_file_handle_docs", cmake)
        for name in ("README.md", "USAGE.md"):
            self.assertIn(f"]({document})", (SOURCE_ROOT / name).read_text(encoding="utf-8"))

    def test_file_handle_diagnostics_accept_only_native_line_endings(self) -> None:
        manifest = json.loads((SOURCE_ROOT / "tests/support/harness/manifests/file-handles.json").read_text(encoding="utf-8"))
        for case in manifest["tests"]:
            pattern = case["expect_by_mode"]["checked"]["run"].get("stderr_regex")
            if pattern is None:
                continue
            name = Path(case["source"]).name
            for ending in ("\n", "\r\n"):
                diagnostic = f"hitsimple runtime error: closed file handle at D:/src/{name}:10:5{ending}"
                with self.subTest(case=case["name"], ending=repr(ending)):
                    self.assertIsNotNone(re.fullmatch(pattern, diagnostic))
                    self.assertIsNone(re.fullmatch(pattern, diagnostic.replace("closed file handle", "invalid file handle")))
                    self.assertIsNone(re.fullmatch(pattern, diagnostic.replace(":10:5", "")))
                    self.assertIsNone(re.fullmatch(pattern, diagnostic + "unexpected output"))

    def test_file_handle_cases_run_in_selective_platform_suites(self) -> None:
        workflow = (SOURCE_ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        for start, end in (
            ("      - name: Run aarch64 front-end and executable regression", "      - name: Verify native preprocessor macro"),
            ("      - name: Run portable Windows CTest cases", "      - name: Assemble full and slim packages"),
        ):
            block = workflow.split(start, 1)[1].split(end, 1)[0]
            patterns = re.findall(r"-DTEST_REGEX=([^']+)", block)
            for case in ("hsc_runtime_file_handles", "hsc_harness_file_handles"):
                self.assertTrue(any(re.fullmatch(pattern, case) for pattern in patterns), case)
        windows = workflow.split("      - name: Run portable Windows CTest cases", 1)[1]
        windows = windows.split("      - name: Assemble full and slim packages", 1)[0]
        self.assertIn("$env:HITSIMPLE_CLANG = (Resolve-Path tools/llvm-mingw-", windows)

    def test_manual_publication_is_opt_in_and_gated_by_the_complete_bundle(self) -> None:
        workflow = (SOURCE_ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        publish = re.search(r"^      publish:\n(?P<body>.*?)(?=^      \w)", workflow, re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(publish)
        assert publish is not None
        self.assertIn("        type: boolean\n", publish.group("body"))
        self.assertIn("        default: false\n", publish.group("body"))
        release = workflow.split("\n  release:\n", 1)[1]
        self.assertIn("      needs.release_request.outputs.publish == 'true' &&\n", release)
        self.assertIn("      (github.event_name == 'push' ||\n", release)
        self.assertIn("       (github.event_name == 'workflow_dispatch' && inputs.publish))\n", release)
        self.assertIn("    needs: [assemble, release_request]\n", release)
        self.assertIn("RELEASE_TAG: ${{ needs.release_request.outputs.tag }}", release)
        self.assertIn("group: hitsimple-release-${{ needs.release_request.outputs.tag }}", release)
        self.assertIn("cancel-in-progress: false", release)
        self.assertIn("python3 .github/scripts/release_guard.py prepare", release)
        self.assertNotIn("$GITHUB_REF_NAME", release)
        self.assertNotIn("gh release delete", release)
        self.assertEqual(workflow.count("      contents: write\n"), 1)
        self.assertNotIn("actions: write", workflow)
        self.assertNotIn("secrets.", workflow)
        for job in ("linux", "macos", "rpm", "windows"):
            self.assertIn(f"  {job}:\n    needs: release_request\n", workflow)
        for value in ("PUBLISH", "VERSION", "SCOPE", "EXPECTED_COMMIT"):
            self.assertEqual(workflow.count(f"HITSIMPLE_{value}: ${{{{ inputs."), 2)


    def test_mutation_cmake_argument_binds_a_dash_prefixed_value(self) -> None:
        workflow = (SOURCE_ROOT / ".github/workflows/mutation.yml").read_text(encoding="utf-8")
        self.assertIn(
            '--cmake-arg="-DLLVM_DIR=$(llvm-config-18 --cmakedir)"',
            workflow,
        )

    def test_weekly_fuzz_keeps_a_half_hour_of_hosted_runner_headroom(self) -> None:
        workflow = (SOURCE_ROOT / ".github/workflows/fuzz-campaign.yml").read_text(encoding="utf-8")
        weekly = re.search(r"^  weekly:\n(?P<body>.*?)(?=^  [a-z][a-z_-]*:|\Z)", workflow, re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(weekly)
        assert weekly is not None

        timeout = re.search(r"^    timeout-minutes: (\d+)$", weekly.group("body"), re.MULTILINE)
        seconds = re.search(r"^            --seconds-per-target (\d+)", weekly.group("body"), re.MULTILINE)
        self.assertIsNotNone(timeout)
        self.assertIsNotNone(seconds)
        assert timeout is not None and seconds is not None

        timeout_minutes = int(timeout.group(1))
        campaign_seconds = int(seconds.group(1))
        self.assertLessEqual(timeout_minutes, 360)
        self.assertGreaterEqual(timeout_minutes * 60 - campaign_seconds, 30 * 60)

    def test_daily_fuzz_keeps_forty_five_minutes_of_hosted_runner_headroom(self) -> None:
        workflow = (SOURCE_ROOT / ".github/workflows/fuzz-campaign.yml").read_text(encoding="utf-8")
        daily = re.search(r"^  daily:\n(?P<body>.*?)(?=^  [a-z][a-z_-]*:|\Z)", workflow, re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(daily)
        assert daily is not None

        timeout = re.search(r"^    timeout-minutes: (\d+)$", daily.group("body"), re.MULTILINE)
        seconds = re.search(r"^            --seconds-per-target (\d+)", daily.group("body"), re.MULTILINE)
        self.assertIsNotNone(timeout)
        self.assertIsNotNone(seconds)
        assert timeout is not None and seconds is not None

        timeout_minutes = int(timeout.group(1))
        campaign_seconds = int(seconds.group(1))
        self.assertLessEqual(timeout_minutes, 360)
        self.assertGreaterEqual(timeout_minutes * 60 - campaign_seconds, 45 * 60)


if __name__ == "__main__":
    unittest.main()
