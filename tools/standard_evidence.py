#!/usr/bin/env python3
"""Generate and validate a bounded source-evidence map, not a conformance score."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import platform
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CROSSWALK = "docs/evidence/standard-witnesses.json"
INVENTORY = "docs/evidence/standard-inventory.json"
REPORT = "docs/standard-evidence.md"
KINDS = {"structural", "diagnostic", "IR", "runtime", "docs", "workflow"}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def source(root: Path, path: str) -> Path:
    require(isinstance(path, str) and bool(path), "empty source path")
    require(not Path(path).is_absolute() and ".." not in Path(path).parts,
            f"source must be repository-relative: {path}")
    candidate = (root / path).resolve()
    require(candidate.is_relative_to(root.resolve()), f"source escapes repository: {path}")
    require(candidate.is_file(), f"missing source: {path}")
    return candidate


def digest(path: Path) -> str:
    # Git checkout may use CRLF on Windows. Review the text, not checkout EOLs.
    return hashlib.sha256(path.read_text(encoding="utf-8").encode("utf-8")).hexdigest()


def headings(text: str) -> list[dict]:
    result = []
    seen: set[str] = set()
    slugs: dict[str, int] = {}
    fence: tuple[str, int] | None = None
    for line, value in enumerate(text.splitlines(), 1):
        marker = re.match(r"^ {0,3}(`{3,}|~{3,})(.*)$", value)
        if marker:
            token, tail = marker.groups()
            if fence is None:
                fence = (token[0], len(token))
            elif token[0] == fence[0] and len(token) >= fence[1] and not tail.strip():
                fence = None
            continue
        if fence:
            continue
        match = re.fullmatch(r"(#{2,6})\s+(\d+(?:\.\d+)*)(?:\.)?\s+(.+)", value)
        if not match:
            continue
        section, title = match.group(2, 3)
        require(section not in seen, f"duplicate Standard section: {section}")
        seen.add(section)
        heading = value[len(match.group(1)):].strip()
        slug = re.sub(r"[^\w\- ]", "", heading.lower()).replace(" ", "-")
        duplicate = slugs.get(slug, 0)
        slugs[slug] = duplicate + 1
        if duplicate:
            slug += f"-{duplicate}"
        result.append({"section": section, "title": title, "line": line, "anchor": slug,
                       "parent": section.rsplit(".", 1)[0] if "." in section else None})
    require(bool(result), "no numbered Standard headings found")
    for entry in result:
        require(entry["parent"] is None or entry["parent"] in seen,
                f"missing parent section: {entry['section']}")
    return result


def unit_tests(root: Path, cmake: str) -> dict[str, str]:
    block = re.search(r"add_executable\(hsc_unit_tests\b([^)]*)\)", cmake, re.S)
    require(block is not None, "cannot identify hsc_unit_tests sources")
    found: dict[str, str] = {}
    assert block is not None
    for path in re.findall(r"^[ \t]*(tests/\S+\.cpp)[ \t]*$", block.group(1), re.M):
        text = source(root, path).read_text(encoding="utf-8")
        for name in re.findall(r"^HS_TEST\(\s*(\w+)\s*\)\s*\{", text, re.M):
            require(name not in found, f"duplicate HS_TEST: {name}")
            found[name] = path
    require(bool(found), "no registered HS_TEST definitions found")
    return found


def ctest_block(cmake: str, name: str) -> str:
    require(re.fullmatch(r"[A-Za-z0-9_]+", name) is not None, f"invalid CTest name: {name}")
    blocks = re.findall(r"add_test\(\s*NAME\s+" + re.escape(name) + r"\s+(.*?)\)\s*", cmake, re.S)
    require(len(blocks) == 1, f"expected one literal add_test for {name}")
    return blocks[0]


def build(root: Path) -> tuple[dict, dict]:
    crosswalk = json.loads(source(root, CROSSWALK).read_text(encoding="utf-8"))
    require(crosswalk.get("version") == 1, "unsupported witness schema")
    require(crosswalk.get("standard", "Standard.md") == "Standard.md", "unsupported Standard source")
    standard = source(root, "Standard.md")
    sections = headings(standard.read_text(encoding="utf-8"))
    section_ids = {entry["section"] for entry in sections}
    cmake = source(root, "CMakeLists.txt").read_text(encoding="utf-8")
    tests = unit_tests(root, cmake)
    manifest = json.loads(source(root, "stdlib/StandardLibraryManifest.json").read_text(encoding="utf-8"))
    inputs = {"Standard.md", "CMakeLists.txt", CROSSWALK, "stdlib/StandardLibraryManifest.json"}
    reviewed = {"Standard.md", "CMakeLists.txt", "tests/support/harness/run_tests.py"}
    witnesses = crosswalk.get("witnesses", [])
    require(isinstance(witnesses, list) and bool(witnesses), "empty witness crosswalk")
    ids: set[str] = set()
    for witness in witnesses:
        identity = witness.get("id", "")
        require(re.fullmatch(r"[a-z][a-z0-9-]*", identity) is not None, "invalid witness id")
        require(identity not in ids, f"duplicate witness: {identity}")
        ids.add(identity)
        for field in ("claim", "limitation"):
            require(isinstance(witness.get(field), str) and bool(witness[field].strip()),
                    f"{identity}: missing {field}")
        require(bool(witness.get("sections")), f"{identity}: missing sections")
        for section in witness["sections"]:
            require(section in section_ids, f"{identity}: unknown Standard section {section}")
        require(bool(witness.get("implementation")), f"{identity}: missing implementation")
        require(bool(witness.get("evidence")), f"{identity}: missing evidence")
        for ref in witness["implementation"] + witness.get("docs", []):
            path = ref["path"]
            text = source(root, path).read_text(encoding="utf-8")
            require(bool(ref.get("anchor")) and text.count(ref["anchor"]) == 1,
                    f"{identity}: anchor must occur once in {path}: {ref.get('anchor')}")
            reviewed.add(path)
        for evidence in witness["evidence"]:
            path = evidence["path"]
            source(root, path)
            reviewed.add(path)
            require(evidence.get("kind") in KINDS, f"{identity}: invalid evidence kind")
            if "test" in evidence:
                name = evidence["test"]
                require(evidence["kind"] in {"structural", "diagnostic", "IR"},
                        f"{identity}: HS_TEST cannot be promoted to runtime evidence")
                require(tests.get(name) == path, f"{identity}: missing exact HS_TEST {name} in {path}")
                require([test for test in tests if name in test] == [name],
                        f"{identity}: HS_TEST substring filter selects multiple tests: {name}")
            elif evidence["kind"] == "runtime":
                block = ctest_block(cmake, evidence["ctest"])
                if path.endswith(".json"):
                    runtime = json.loads(source(root, path).read_text(encoding="utf-8"))
                    suite = runtime["suite"]
                    require(path == f"tests/support/harness/manifests/{suite}.json",
                            f"{identity}: runtime manifest is not the suite-selected path")
                    require("${PROJECT_SOURCE_DIR}/tests/support/harness/run_tests.py" in block,
                            f"{identity}: CTest does not invoke the harness runner")
                    require(re.search(r"--suite\s+" + re.escape(suite) + r"(?:\s|$)", block) is not None,
                            f"{identity}: CTest does not select manifest suite {suite}")
                    cases = {case["name"]: case for case in runtime["tests"]}
                    require(len(cases) == len(runtime["tests"]), f"{identity}: duplicate runtime cases")
                    require(bool(evidence.get("cases")), f"{identity}: no exact runtime cases")
                    for name in evidence["cases"]:
                        require(name in cases, f"{identity}: missing runtime case {name}")
                        fixture = (source(root, path).parent / cases[name]["source"]).resolve()
                        fixture_path = fixture.relative_to(root.resolve()).as_posix()
                        source(root, fixture_path)
                        reviewed.add(fixture_path)
                else:
                    compile_name, fixture = evidence["compile_ctest"], evidence["fixture"]
                    compile_block = ctest_block(cmake, compile_name)
                    require("${PROJECT_SOURCE_DIR}/" + path in compile_block,
                            f"{identity}: compile CTest does not reference source")
                    output = re.search(r"(?:^|\s)-o\s+(\S+)", compile_block)
                    command = re.match(r"COMMAND\s+(\S+)", block)
                    require(output is not None and command is not None
                            and output.group(1) == command.group(1),
                            f"{identity}: run CTest does not execute the compiled output")
                    for name, kind in ((compile_name, "SETUP"), (evidence["ctest"], "REQUIRED")):
                        require(re.search(r"set_tests_properties\(" + re.escape(name)
                                          + r"\s+PROPERTIES\s+FIXTURES_" + kind + r"\s+"
                                          + re.escape(fixture) + r"\s*\)", cmake) is not None,
                                f"{identity}: missing CTest fixture {kind} association")
                require(bool(evidence.get("limitation")), f"{identity}: runtime limitations required")
            else:
                require(bool(evidence.get("anchor")), f"{identity}: source evidence needs an anchor")
                text = source(root, path).read_text(encoding="utf-8")
                require(text.count(evidence["anchor"]) == 1, f"{identity}: nonunique evidence anchor")
    recorded = crosswalk.get("reviewed_sha256", {})
    require(set(recorded) == reviewed, "reviewed_sha256 must list exactly the reviewed source files")
    for path in sorted(reviewed):
        require(recorded[path] == digest(source(root, path)),
                f"review stale: {path}; inspect its change before updating reviewed_sha256")
    inputs.update(reviewed)
    # Test registration/name changes must also invalidate generated inventories.
    inputs.update(tests.values())
    for entry in sections:
        entry["witnesses"] = [w["id"] for w in witnesses if entry["section"] in w["sections"]]
        entry["mapping_status"] = "inspected" if entry["witnesses"] else "unmapped"
    headers = {entry["id"]: entry["file"] for entry in manifest["headers"]}
    apis = []
    extensions = []
    api_names: set[str] = set()
    for api in manifest["builtins"]:
        require(api["name"] not in api_names, f"duplicate API: {api['name']}")
        api_names.add(api["name"])
        extension = api.get("extensionDocument", "")
        require(isinstance(extension, str), f"invalid extension document: {api['name']}")
        require(bool(api["standardSection"]) != bool(extension),
                f"API must declare exactly one Standard section or extension document: {api['name']}")
        if extension:
            require(extension.startswith("docs/") and extension.endswith(".md"),
                    f"invalid extension document: {api['name']}")
            source(root, extension)
            inputs.add(extension)
        else:
            require(api["standardSection"] in section_ids, f"unknown API section: {api['name']}")
        owners = api.get("testOwners") or manifest.get("defaultTestOwners", [])
        for owner in owners:
            source(root, owner)
            inputs.add(owner)
        entry = {"id": api.get("id", api["name"]), "name": api["name"], "visibility": api["visibility"],
                     "section": api["standardSection"], "header": headers[api["header"]],
                     "provider": api["provider"], "implementation_symbol": api["implementationSymbol"],
                     "reference_provider": api.get("referenceProvider", "None"),
                     "source_module": api.get("sourceModule", ""),
                     "test_owners": owners, "owners_inherited": not bool(api.get("testOwners")),
                     "mapping_status": "declared", "evidence_kind": "structural"}
        if extension:
            entry["extension_document"] = extension
            extensions.append(entry)
        else:
            apis.append(entry)
    inventory = {"version": 1, "purpose": "navigation and bounded evidence, not independent requirements or conformance",
                 "inputs_sha256": {p: digest(source(root, p)) for p in sorted(inputs)},
                 "sections": sections, "apis": apis, "extensions": extensions,
                 "witnesses": [{"id": w["id"], "mapping_status": "inspected",
                                "execution_status": "not_recorded"} for w in witnesses]}
    return inventory, crosswalk


def link(path: str, label: str | None = None, anchor: str | None = None) -> str:
    return f"[{label or path}](../{path}" + (f"#{anchor}" if anchor else "") + ")"


def render(inventory: dict, crosswalk: dict, root: Path) -> str:
    sections = {s["section"]: s for s in inventory["sections"]}
    lines = ["# Standard evidence map", "", "Generated by `python3 tools/standard_evidence.py --write`. Do not edit this file.", "",
             "This is a navigation inventory and a first, bounded, manually inspected witness crosswalk.",
             "Numbered headings are not independent requirements. Manifest API ownership is structural",
             "metadata, not behavioral coverage. There is no conformance percentage.", "",
             "- `declared`: metadata names an owner; behavior has not been established by that declaration.",
             "- `inspected`: the listed source and assertions were reviewed for the narrow claim only.",
             "- `unmapped`: this crosswalk has no witness for that heading; it does not mean unimplemented.",
             "- Evidence kinds: `structural`, `diagnostic`, `IR`, `runtime`, `docs`, `workflow`.",
             "- Execution is separate: every committed row is `not_recorded`. Source inspection and",
             "  a freshness check do not establish a test pass. Use exact-head CI logs or a local run report.", "",
             f"Inventory: {len(sections)} numbered headings; {sum('.' not in s for s in sections)} chapters; "
             f"{len(inventory['apis'])} Standard APIs; {len(inventory['extensions'])} optional extension APIs; "
             f"{len(crosswalk['witnesses'])} inspected witnesses.", "",
             "## Validate and run", "", "```sh", "python3 tools/standard_evidence.py --check",
             "python3 tests/evidence/test_standard_evidence.py",
             "python3 tools/standard_evidence.py --run-unit-witnesses build/hsc_unit_tests --results build/standard-evidence-run.json",
             "cmake -DBUILD_DIR=build -P cmake/RunQuietTests.cmake", "```", "",
             "The unit runner validates exact names against registered source files, rejects substring",
             "fan-out, runs each witness separately, and requires exactly `1/1 PASS` plus exit zero.",
             "An unknown unit filter can otherwise return `0/0 PASS`. Its JSON report identifies the",
             "binary hash, platform, source fingerprints, and per-test outcome; it is not a portable",
             "conformance certificate. Use a freshly built binary from the same checkout.", "",
             "Runtime links below name existing CTest suites and exact manifest cases. They are not run",
             "by the unit runner. Inspect the harness artifacts for provider, safety mode, optimization,",
             "target, compile-only, skipped, and executed variants. Windows disables shell CTests;",
             "registration alone does not show availability or success. Do not infer a pass from an empty",
             "CTest selection. The portable evidence CTests run in the native release-validation gates.", "",
             "## Maintain this map", "",
             f"Edit {link(CROSSWALK, 'the witness crosswalk')} after inspecting the Standard, implementation,",
             "and assertions together. Keep claims narrow and limitations explicit. Reviewed source-text",
             "SHA-256 fingerprints intentionally invalidate a witness when any referenced file changes",
             "(LF/CRLF differences are ignored). Review the diff before updating `reviewed_sha256`;",
             "`--write` never updates those review fingerprints. Then regenerate and run the checks.",
             "A matching fingerprint establishes freshness, not correctness. The validator checks source",
             "references and test identities; human review is still required for semantic adequacy.", "",
             "## Reviewed witnesses", ""]
    for witness in crosswalk["witnesses"]:
        lines += [f"### {witness['id']}", "", witness["claim"], "",
                  "Standard: " + ", ".join(link("Standard.md", "§" + s, sections[s]["anchor"]) for s in witness["sections"]) + ".",
                  "Mapping: `inspected`. Execution: `not_recorded`.", "", "Implementation:"]
        for ref in witness["implementation"]:
            text = source(root, ref["path"]).read_text(encoding="utf-8")
            line = text[:text.index(ref["anchor"])].count("\n") + 1
            lines += ["- " + link(ref["path"], ref["path"], f"L{line}") + f" (`{ref['anchor']}`)"]
        lines += ["", "Evidence:"]
        for item in witness["evidence"]:
            label = item.get("test", item.get("ctest", item["path"]))
            text = source(root, item["path"]).read_text(encoding="utf-8")
            anchor = item.get("anchor", "HS_TEST(" + item["test"] + ")" if "test" in item else None)
            line = text[:text.index(anchor)].count("\n") + 1 if anchor else None
            lines += [f"- `{item['kind']}`: " + link(item["path"], label, f"L{line}" if line else None)
                      + ("; cases: " + ", ".join(f"`{name}`" for name in item["cases"]) if "cases" in item else "")]
            if item.get("assertions"):
                lines += ["  " + item["assertions"]]
            if item.get("limitation"):
                lines += ["  " + item["limitation"]]
        if witness.get("docs"):
            lines += ["", "Context: " + ", ".join(link(ref["path"]) for ref in witness["docs"]) + "."]
        lines += ["", "Limit: " + witness["limitation"], ""]
    lines += ["## Standard navigation inventory", "", "A mapped parent never implicitly maps its children or vice versa.", "",
              "| Section | Heading | Mapping | Witnesses |", "| --- | --- | --- | --- |"]
    for item in inventory["sections"]:
        lines += [f"| {item['section']} | " + link("Standard.md", item["title"].replace("|", "\\|"), item["anchor"])
                  + f" | {item['mapping_status']} | " + ", ".join(f"[{w}](#{w})" for w in item["witnesses"]) + " |"]
    lines += ["", "## Manifest API inventory", "", "All rows below are `declared` / `structural`, regardless of nearby runtime witnesses.",
              "Owner paths come from the manifest, including inherited defaults. A shared Standard section",
              "does not transfer a witness to every API in that section.", "",
              "| API | Standard | Header | Provider / symbol | Test owners |", "| --- | --- | --- | --- | --- |"]
    for api in inventory["apis"]:
        lines += [f"| `{api['name']}` | " + link("Standard.md", "§" + api["section"], sections[api["section"]]["anchor"])
                  + f" | `{api['header']}` | {api['provider']} / `{api['implementation_symbol']}` | "
                  + ", ".join(link(owner) for owner in api["test_owners"])
                  + (" (inherited)" if api["owners_inherited"] else "") + " |"]
    if inventory["extensions"]:
        lines += ["", "## Optional implementation extensions", "",
                  "These APIs are outside Beta.21 and excluded from the Standard API count above.",
                  "Their metadata is `declared` / `structural`, not a conformance claim.", "",
                  "| API | Extension contract | Header | Provider / symbol | Test owners |",
                  "| --- | --- | --- | --- | --- |"]
        for api in inventory["extensions"]:
            lines += [f"| `{api['name']}` | " + link(api["extension_document"])
                      + f" | `{api['header']}` | {api['provider']} / `{api['implementation_symbol']}` | "
                      + ", ".join(link(owner) for owner in api["test_owners"]) + " |"]
    return "\n".join(lines) + "\n"


def run_units(binary: Path, crosswalk: dict, inventory: dict) -> dict:
    require(binary.is_file(), f"missing unit binary: {binary}")
    names = sorted({e["test"] for w in crosswalk["witnesses"] for e in w["evidence"] if "test" in e})
    require(bool(names), "no unit witnesses selected")
    results = []
    for name in names:
        try:
            run = subprocess.run([str(binary.resolve()), name], capture_output=True, text=True, timeout=120)
            passed = run.returncode == 0 and run.stdout.strip() == "1/1 PASS" and not run.stderr.strip()
            results.append({"test": name, "execution_status": "passed" if passed else "failed",
                            "returncode": run.returncode, "stdout": run.stdout, "stderr": run.stderr})
        except (OSError, subprocess.TimeoutExpired) as error:
            results.append({"test": name, "execution_status": "failed", "error": str(error)})
    return {"version": 1, "scope": "unit witnesses only; runtime cases are not executed here",
            "recorded_at": datetime.now(timezone.utc).isoformat(),
            "platform": platform.platform(), "binary": str(binary.resolve()),
            "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            "inputs_sha256": inventory["inputs_sha256"], "results": results}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--write", action="store_true")
    action.add_argument("--check", action="store_true")
    action.add_argument("--run-unit-witnesses", type=Path)
    parser.add_argument("--results", type=Path)
    args = parser.parse_args()
    try:
        inventory, crosswalk = build(ROOT)
        generated = {INVENTORY: json.dumps(inventory, indent=2, ensure_ascii=False) + "\n",
                     REPORT: render(inventory, crosswalk, ROOT)}
        if args.run_unit_witnesses:
            require(args.results is not None, "--results is required for unit execution")
            report = run_units(args.run_unit_witnesses, crosswalk, inventory)
            args.results.parent.mkdir(parents=True, exist_ok=True)
            args.results.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
            passed = sum(r["execution_status"] == "passed" for r in report["results"])
            print(f"{passed}/{len(report['results'])} unit witnesses passed; report: {args.results}")
            return 0 if passed == len(report["results"]) else 1
        for path, value in generated.items():
            if args.write:
                (ROOT / path).write_text(value, encoding="utf-8")
            else:
                require(source(ROOT, path).read_text(encoding="utf-8") == value,
                        f"generated file stale: {path}; run --write after reviewing changes")
        print(f"evidence {'generated' if args.write else 'validated'}: "
              f"{len(inventory['sections'])} headings, {len(inventory['apis'])} Standard APIs, "
              f"{len(inventory['extensions'])} extensions, "
              f"{len(crosswalk['witnesses'])} witnesses; no execution claim")
        return 0
    except (ValueError, KeyError, TypeError, OSError) as error:
        print(f"standard evidence: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
