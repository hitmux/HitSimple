#!/usr/bin/env python3
"""Validate release intent, then create only a missing, explicitly pinned tag."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import NamedTuple


SOURCE_ROOT = Path(__file__).resolve().parents[2]
VERSION = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+")
SHA = re.compile(r"[0-9a-f]{40}")


class GuardError(RuntimeError):
    pass


class ReleaseRequest(NamedTuple):
    publish: bool
    tag: str = ""
    commit: str = ""
    create_tag: bool = False


def source_version(root: Path) -> str:
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"^  VERSION ([0-9]+\.[0-9]+\.[0-9]+)$", cmake, re.MULTILINE)
    if match is None:
        raise GuardError("Cannot determine the source version")
    version = match.group(1)
    extension = root / "vscode/hitsimple"
    package = json.loads((extension / "package.json").read_text(encoding="utf-8"))
    lock = json.loads((extension / "package-lock.json").read_text(encoding="utf-8"))
    if any(value != version for value in (
        package["version"], lock["version"], lock["packages"][""]["version"],
    )):
        raise GuardError("Compiler and extension versions do not match")
    return version


def validate_request(env: dict[str, str], root: Path = SOURCE_ROOT) -> ReleaseRequest:
    event = env.get("GITHUB_EVENT_NAME", "")
    ref = env.get("GITHUB_REF", "")
    commit = env.get("GITHUB_SHA", "")
    if event == "pull_request":
        return ReleaseRequest(False)
    if event == "workflow_dispatch":
        publish = env.get("HITSIMPLE_PUBLISH", "false")
        if publish in ("", "false"):
            return ReleaseRequest(False)
        if publish != "true":
            raise GuardError("publish must be an explicit boolean true or false")
        if ref != "refs/heads/master":
            raise GuardError("Manual publication must run on master")
        if env.get("HITSIMPLE_SCOPE") != "all":
            raise GuardError("Manual publication requires scope=all")
        expected = env.get("HITSIMPLE_EXPECTED_COMMIT", "")
        if not SHA.fullmatch(expected) or expected != commit:
            raise GuardError("expected_commit must exactly match this run's full commit SHA")
        version = env.get("HITSIMPLE_VERSION", "")
        create_tag = True
    elif event == "push":
        prefix = "refs/tags/v"
        if not ref.startswith(prefix):
            raise GuardError("Only version-tag pushes can publish")
        version = ref[len(prefix):]
        create_tag = False
    else:
        raise GuardError(f"Unsupported release event: {event}")
    if not SHA.fullmatch(commit):
        raise GuardError("The run must identify a full commit SHA")
    if not VERSION.fullmatch(version) or version != source_version(root):
        raise GuardError("Requested version must exactly match source metadata")
    return ReleaseRequest(True, f"v{version}", commit, create_tag)


def github_api(repository: str, suffix: str, fields: dict[str, str] | None = None) -> dict | list | None:
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise GuardError("Invalid repository identity")
    method = "POST" if fields is not None else "GET"
    command = ["gh", "api", "--include", "--method", method, f"repos/{repository}/{suffix}"]
    for name, value in (fields or {}).items():
        command.extend(["-f", f"{name}={value}"])
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    response = result.stdout.replace("\r\n", "\n")
    header, separator, body = response.partition("\n\n")
    status = re.match(r"HTTP/\S+ (\d{3})(?: [^\n]*)?(?:\n|$)", header)
    if not status or not separator:
        raise GuardError(f"Cannot determine HTTP status for {method} {suffix}")
    code = int(status.group(1))
    if method == "GET" and code == 404:
        return None
    expected_status = 201 if method == "POST" else 200
    if result.returncode != 0 or code != expected_status:
        raise GuardError(f"GitHub rejected {method} {suffix} (HTTP {code})")
    try:
        payload = json.loads(body)
    except json.JSONDecodeError as error:
        raise GuardError(f"Invalid GitHub response for {method} {suffix}") from error
    if not isinstance(payload, (dict, list)):
        raise GuardError(f"Unexpected GitHub response for {method} {suffix}")
    return payload


def reject_existing_release(repository: str, tag: str) -> None:
    # The tag lookup endpoint can omit drafts. Listing with this job's existing
    # contents:write permission includes drafts and must be paginated.
    page = 1
    while True:
        releases = github_api(repository, f"releases?per_page=100&page={page}")
        if not isinstance(releases, list) or any(
            not isinstance(item, dict) or not isinstance(item.get("tag_name"), str)
            for item in releases
        ):
            raise GuardError("Cannot inspect existing releases")
        if any(item.get("tag_name") == tag for item in releases):
            raise GuardError("A release already exists; refusing to replace it")
        if len(releases) < 100:
            return
        page += 1


def tag_commit(repository: str, reference: dict) -> str:
    if not isinstance(reference, dict):
        raise GuardError("Invalid tag reference")
    obj = reference.get("object", {})
    if not isinstance(obj, dict):
        raise GuardError("Invalid tag target")
    seen: set[str] = set()
    # Existing annotated tags are supported without ever updating their refs.
    while obj.get("type") == "tag":
        sha = obj.get("sha", "")
        if not SHA.fullmatch(sha) or sha in seen or len(seen) >= 8:
            raise GuardError("Invalid or cyclic annotated tag")
        seen.add(sha)
        tag = github_api(repository, f"git/tags/{sha}")
        if not isinstance(tag, dict):
            raise GuardError("Annotated tag object is missing")
        obj = tag.get("object", {})
        if not isinstance(obj, dict):
            raise GuardError("Invalid annotated tag target")
    if obj.get("type") != "commit" or not SHA.fullmatch(obj.get("sha", "")):
        raise GuardError("Release tag does not resolve to a commit")
    return obj["sha"]


def prepare_release(request: ReleaseRequest, repository: str) -> None:
    if not request.publish:
        raise GuardError("This request does not authorize publication")
    # Never replace a draft, prerelease, or published release from another run.
    reject_existing_release(repository, request.tag)
    reference = github_api(repository, f"git/ref/tags/{request.tag}")
    if request.create_tag:
        if reference is not None:
            raise GuardError("The tag already exists; refusing to replace it")
        reference = github_api(repository, "git/refs", {
            "ref": f"refs/tags/{request.tag}", "sha": request.commit,
        })
        if not isinstance(reference, dict) or reference.get("ref") != f"refs/tags/{request.tag}":
            raise GuardError("Tag creation returned an unexpected reference")
    elif reference is None:
        raise GuardError("The pushed release tag is missing")
    if tag_commit(repository, reference) != request.commit:
        raise GuardError("Release tag does not match the validated commit")


def main() -> int:
    try:
        if len(sys.argv) != 2 or sys.argv[1] not in ("request", "prepare"):
            raise GuardError("Usage: release_guard.py request|prepare")
        request = validate_request(dict(os.environ))
        if sys.argv[1] == "prepare":
            prepare_release(request, os.environ.get("GITHUB_REPOSITORY", ""))
            print(f"Verified release target {request.tag} at {request.commit}")
        else:
            output = f"publish={str(request.publish).lower()}\ntag={request.tag}\n"
            if os.environ.get("GITHUB_OUTPUT"):
                with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as stream:
                    stream.write(output)
            else:
                print(output, end="")
    except (GuardError, OSError, KeyError, ValueError) as error:
        print(f"Release guard: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
