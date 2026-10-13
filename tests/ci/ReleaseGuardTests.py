#!/usr/bin/env python3
"""Exercise release intent and create-only API behavior without credentials."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


SOURCE_ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "release_guard", SOURCE_ROOT / ".github/scripts/release_guard.py",
)
assert spec is not None and spec.loader is not None
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)

COMMIT = "a" * 40
OTHER_COMMIT = "b" * 40
TAG_OBJECT = "c" * 40
REPOSITORY = "hitmux/HitSimple"
VERSION = guard.source_version(SOURCE_ROOT)
TAG = f"v{VERSION}"


def environment(**updates: str) -> dict[str, str]:
    result = {
        "GITHUB_EVENT_NAME": "workflow_dispatch",
        "GITHUB_REF": "refs/heads/master",
        "GITHUB_SHA": COMMIT,
        "HITSIMPLE_PUBLISH": "true",
        "HITSIMPLE_VERSION": VERSION,
        "HITSIMPLE_SCOPE": "all",
        "HITSIMPLE_EXPECTED_COMMIT": COMMIT,
    }
    result.update(updates)
    return result


def response(status: int, body: dict | list, returncode: int | None = None) -> subprocess.CompletedProcess:
    if returncode is None:
        returncode = 0 if 200 <= status < 300 else 1
    return subprocess.CompletedProcess(
        [], returncode,
        f"HTTP/2.0 {status} Test\r\nContent-Type: application/json\r\n\r\n{json.dumps(body)}\n",
        "",
    )


def reference(commit: str = COMMIT, kind: str = "commit") -> dict:
    return {"ref": f"refs/tags/{TAG}", "object": {"type": kind, "sha": commit}}


class ReleaseRequestTests(unittest.TestCase):
    def test_explicit_pinned_full_master_dispatch_can_publish(self) -> None:
        self.assertEqual(
            guard.validate_request(environment()),
            guard.ReleaseRequest(True, TAG, COMMIT, True),
        )

    def test_dispatch_is_validation_only_by_default(self) -> None:
        for value in (None, "", "false"):
            env = environment(HITSIMPLE_EXPECTED_COMMIT="")
            if value is None:
                del env["HITSIMPLE_PUBLISH"]
            else:
                env["HITSIMPLE_PUBLISH"] = value
            self.assertFalse(guard.validate_request(env).publish)

    def test_pull_request_never_publishes_even_with_publish_input(self) -> None:
        self.assertFalse(guard.validate_request(environment(GITHUB_EVENT_NAME="pull_request")).publish)

    def test_manual_publication_rejects_each_invalid_gate(self) -> None:
        cases = (
            {"HITSIMPLE_PUBLISH": "yes"},
            {"GITHUB_REF": "refs/heads/feature"},
            {"GITHUB_REF": f"refs/tags/{TAG}"},
            {"HITSIMPLE_SCOPE": "windows"},
            {"HITSIMPLE_SCOPE": ""},
            {"HITSIMPLE_EXPECTED_COMMIT": ""},
            {"HITSIMPLE_EXPECTED_COMMIT": COMMIT[:7]},
            {"HITSIMPLE_EXPECTED_COMMIT": OTHER_COMMIT},
            {"HITSIMPLE_VERSION": "0.0.0"},
            {"HITSIMPLE_VERSION": f"{VERSION}; echo unsafe"},
            {"HITSIMPLE_VERSION": f"v{VERSION}"},
            {"GITHUB_SHA": "master", "HITSIMPLE_EXPECTED_COMMIT": "master"},
            {"GITHUB_EVENT_NAME": "repository_dispatch"},
        )
        for update in cases:
            with self.subTest(update=update), self.assertRaises(guard.GuardError):
                guard.validate_request(environment(**update))

    def test_original_version_tag_push_is_preserved(self) -> None:
        request = guard.validate_request(environment(
            GITHUB_EVENT_NAME="push", GITHUB_REF=f"refs/tags/{TAG}",
            HITSIMPLE_PUBLISH="", HITSIMPLE_EXPECTED_COMMIT="", HITSIMPLE_VERSION="",
        ))
        self.assertEqual(request, guard.ReleaseRequest(True, TAG, COMMIT, False))

    def test_push_rejects_branches_invalid_tags_and_wrong_versions(self) -> None:
        for ref in ("refs/heads/master", "refs/tags/v0.0.0", f"refs/tags/{TAG}-rc1", "refs/tags/v1"):
            with self.subTest(ref=ref), self.assertRaises(guard.GuardError):
                guard.validate_request(environment(GITHUB_EVENT_NAME="push", GITHUB_REF=ref))

    def test_extension_and_each_lock_version_must_match_cmake(self) -> None:
        for target in ("package", "lock", "lock_root"):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                extension = root / "vscode/hitsimple"
                extension.mkdir(parents=True)
                (root / "CMakeLists.txt").write_text(f"project(HitSimple\n  VERSION {VERSION}\n)\n")
                package = {"version": VERSION}
                lock = {"version": VERSION, "packages": {"": {"version": VERSION}}}
                if target == "package":
                    package["version"] = "0.0.0"
                elif target == "lock":
                    lock["version"] = "0.0.0"
                else:
                    lock["packages"][""]["version"] = "0.0.0"
                (extension / "package.json").write_text(json.dumps(package))
                (extension / "package-lock.json").write_text(json.dumps(lock))
                with self.assertRaises(guard.GuardError):
                    guard.validate_request(environment(), root)


class ReleaseApiTests(unittest.TestCase):
    def setUp(self) -> None:
        self.manual = guard.ReleaseRequest(True, TAG, COMMIT, True)
        self.push = guard.ReleaseRequest(True, TAG, COMMIT, False)

    def test_dispatch_creates_only_the_exact_missing_tag(self) -> None:
        with patch.object(guard.subprocess, "run", side_effect=[
            response(200, []), response(404, {}), response(201, reference()),
        ]) as run:
            guard.prepare_release(self.manual, REPOSITORY)
        commands = [call.args[0] for call in run.call_args_list]
        self.assertEqual(len(commands), 3)
        self.assertEqual(commands[-1], [
            "gh", "api", "--include", "--method", "POST", f"repos/{REPOSITORY}/git/refs",
            "-f", f"ref=refs/tags/{TAG}", "-f", f"sha={COMMIT}",
        ])

    def test_existing_releases_are_never_replaced_in_either_path(self) -> None:
        for request in (self.manual, self.push):
            for draft, prerelease in ((True, False), (False, True), (False, False)):
                with self.subTest(request=request, draft=draft, prerelease=prerelease):
                    with patch.object(guard.subprocess, "run", return_value=response(200, [{
                        "tag_name": TAG, "draft": draft, "prerelease": prerelease,
                    }])) as run, self.assertRaises(guard.GuardError):
                        guard.prepare_release(request, REPOSITORY)
                    self.assertEqual(run.call_count, 1)

    def test_release_search_checks_drafts_beyond_the_first_page(self) -> None:
        first = [{"tag_name": f"old-{index}"} for index in range(100)]
        with patch.object(guard.subprocess, "run", side_effect=[
            response(200, first), response(200, [{"tag_name": TAG, "draft": True}]),
        ]) as run, self.assertRaises(guard.GuardError):
            guard.prepare_release(self.manual, REPOSITORY)
        self.assertIn("page=2", run.call_args_list[-1].args[0][-1])

    def test_existing_manual_tag_is_rejected_even_if_the_commit_matches(self) -> None:
        for commit in (COMMIT, OTHER_COMMIT):
            with self.subTest(commit=commit):
                with patch.object(guard.subprocess, "run", side_effect=[
                    response(200, []), response(200, reference(commit)),
                ]) as run, self.assertRaises(guard.GuardError):
                    guard.prepare_release(self.manual, REPOSITORY)
                self.assertEqual(run.call_count, 2)

    def test_non_404_tag_errors_never_create_a_tag(self) -> None:
        for status in (401, 403, 429, 500, 502):
            with self.subTest(status=status):
                with patch.object(guard.subprocess, "run", side_effect=[
                    response(200, []), response(status, {}),
                ]) as run, self.assertRaises(guard.GuardError):
                    guard.prepare_release(self.manual, REPOSITORY)
                self.assertEqual(run.call_count, 2)

    def test_release_listing_errors_fail_closed(self) -> None:
        for value in (response(404, {}), response(403, {}), response(200, {}), response(200, [None]), response(200, [{}])):
            with self.subTest(response=value):
                with patch.object(guard.subprocess, "run", return_value=value) as run:
                    with self.assertRaises(guard.GuardError):
                        guard.prepare_release(self.manual, REPOSITORY)
                    self.assertEqual(run.call_count, 1)

    def test_network_and_malformed_responses_fail_closed(self) -> None:
        for value in (
            subprocess.CompletedProcess([], 1, "", "network failed"),
            subprocess.CompletedProcess([], 0, "HTTP/2.0 200 OK\n\nnot JSON", ""),
            response(200, [], returncode=1),
        ):
            with self.subTest(response=value):
                with patch.object(guard.subprocess, "run", return_value=value) as run:
                    with self.assertRaises(guard.GuardError):
                        guard.prepare_release(self.manual, REPOSITORY)
                    self.assertEqual(run.call_count, 1)

    def test_create_race_or_wrong_result_is_not_retried_or_overwritten(self) -> None:
        for value in (response(422, {}), response(201, reference(OTHER_COMMIT)), response(201, {})):
            with self.subTest(response=value):
                with patch.object(guard.subprocess, "run", side_effect=[
                    response(200, []), response(404, {}), value,
                ]) as run, self.assertRaises(guard.GuardError):
                    guard.prepare_release(self.manual, REPOSITORY)
                self.assertEqual(run.call_count, 3)

    def test_original_lightweight_tag_push_does_not_write_a_ref(self) -> None:
        with patch.object(guard.subprocess, "run", side_effect=[
            response(200, []), response(200, reference()),
        ]) as run:
            guard.prepare_release(self.push, REPOSITORY)
        self.assertTrue(all(call.args[0][4] == "GET" for call in run.call_args_list))

    def test_original_annotated_tag_push_resolves_the_commit_without_writes(self) -> None:
        with patch.object(guard.subprocess, "run", side_effect=[
            response(200, []), response(200, reference(TAG_OBJECT, "tag")),
            response(200, {"object": {"type": "commit", "sha": COMMIT}}),
        ]) as run:
            guard.prepare_release(self.push, REPOSITORY)
        self.assertEqual(run.call_args_list[-1].args[0][-1], f"repos/{REPOSITORY}/git/tags/{TAG_OBJECT}")
        self.assertTrue(all(call.args[0][4] == "GET" for call in run.call_args_list))

    def test_push_rejects_missing_mismatched_and_cyclic_tags(self) -> None:
        replies = (
            [response(404, {})],
            [response(200, reference(OTHER_COMMIT))],
            [response(200, reference(TAG_OBJECT, "tag")), response(200, {"object": {"type": "tag", "sha": TAG_OBJECT}})],
        )
        for values in replies:
            with self.subTest(replies=values):
                with patch.object(guard.subprocess, "run", side_effect=[response(200, []), *values]) as run:
                    with self.assertRaises(guard.GuardError):
                        guard.prepare_release(self.push, REPOSITORY)
                    self.assertTrue(all(call.args[0][4] == "GET" for call in run.call_args_list))

    def test_validation_only_request_never_calls_github(self) -> None:
        with patch.object(guard.subprocess, "run") as run, self.assertRaises(guard.GuardError):
            guard.prepare_release(guard.ReleaseRequest(False), REPOSITORY)
        run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
