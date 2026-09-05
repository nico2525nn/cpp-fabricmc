#!/usr/bin/env python3
"""Build and run the complete plan51 directory-mod fixture corpus.

The CMake target intentionally compiles only the historical two-file fixture.
This runner owns the corpus build/staging step, so adding a fixture does not
require changing the production CMake graph.  All 25 mods are then loaded by
one cppfm process and judged by ``jvm_compatibility_report.py``.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from typing import Any


CASE_COUNT = 25
COMPILE_TIMEOUT = 90.0
SERVER_TIMEOUT = 45.0
STOP_TIMEOUT = 12.0


def load_json(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return value


def run_checked(command: list[str], *, cwd: Path | None = None,
                timeout: float = COMPILE_TIMEOUT) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=str(cwd) if cwd else None,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
        check=False,
    )


def validate_metadata(
    source_dir: Path,
    entry: dict[str, Any],
    *,
    require_entrypoint: bool = True,
) -> dict[str, Any]:
    metadata_path = source_dir / "fabric.mod.json"
    if not metadata_path.is_file():
        raise ValueError(f"{source_dir} is missing fabric.mod.json")
    metadata = load_json(metadata_path)
    if metadata.get("id") != entry.get("id"):
        raise ValueError(
            f"{source_dir}: metadata id {metadata.get('id')!r} does not match {entry.get('id')!r}"
        )
    entrypoints = metadata.get("entrypoints", {})
    server = entrypoints.get("server", []) if isinstance(entrypoints, dict) else []
    if isinstance(server, str):
        server = [server]
    expected_entrypoint = entry.get("entrypoint")
    if require_entrypoint:
        if not isinstance(expected_entrypoint, str) or expected_entrypoint not in server:
            raise ValueError(f"{source_dir}: server entrypoint is not listed in fabric.mod.json")
    elif expected_entrypoint is not None and expected_entrypoint not in server:
        raise ValueError(f"{source_dir}: auxiliary server entrypoint does not match metadata")
    dependencies = metadata.get("depends", {})
    declared = set(dependencies) if isinstance(dependencies, dict) else set()
    expected = set(entry.get("depends", []))
    if declared != expected:
        raise ValueError(
            f"{source_dir}: dependency metadata {sorted(declared)} != corpus {sorted(expected)}"
        )
    mixins = metadata.get("mixins", [])
    if isinstance(mixins, str):
        mixins = [mixins]
    if not isinstance(mixins, list) or not all(isinstance(item, str) for item in mixins):
        raise ValueError(f"{source_dir}: invalid mixin metadata")
    for config in mixins:
        if not (source_dir / config).is_file():
            raise ValueError(f"{source_dir}: missing mixin resource {config}")
    access_widener = metadata.get("accessWidener")
    if access_widener is not None and (
        not isinstance(access_widener, str) or not (source_dir / access_widener).is_file()
    ):
        raise ValueError(f"{source_dir}: missing access widener resource")
    return metadata


def copy_mod_resources(source_dir: Path, target_dir: Path) -> None:
    target_dir.mkdir(parents=True, exist_ok=False)
    for item in source_dir.iterdir():
        if item.name == "src":
            continue
        destination = target_dir / item.name
        if item.is_dir():
            shutil.copytree(item, destination)
        else:
            shutil.copy2(item, destination)


def compile_mod(
    source_dir: Path,
    target_name: str,
    entry: dict[str, Any],
    classes: Path,
    mods: Path,
    javac: str,
    *,
    require_entrypoint: bool,
) -> dict[str, Any]:
    metadata = validate_metadata(
        source_dir,
        entry,
        require_entrypoint=require_entrypoint,
    )
    sources = sorted(source_dir.glob("src/**/*.java"))
    if not sources:
        raise ValueError(f"fixture {target_name} has no Java source")
    target_dir = mods / target_name
    if target_dir.exists():
        raise ValueError(f"staged mod directory is duplicated: {target_name}")
    copy_mod_resources(source_dir, target_dir)
    result = run_checked(
        [javac, "--release", "17", "-g", "-cp", str(classes), "-d", str(target_dir),
         *(str(source) for source in sources)],
        cwd=source_dir,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"javac failed for {target_name} (exit {result.returncode})\n{result.stdout}"
        )
    return metadata


def auxiliary_entry(
    corpus_root: Path,
    primary: dict[str, Any],
    specification: Any,
) -> tuple[str, dict[str, Any]]:
    """Resolve an auxiliary directory mod and its optional expected metadata.

    A string keeps the inventory compact and derives the expected id/dependency
    set from that mod's metadata.  An object can pin those fields when a future
    corpus needs stricter hand-off validation.
    """
    if isinstance(specification, str):
        case = specification
        expected: dict[str, Any] = {}
    elif isinstance(specification, dict):
        case = specification.get("case", specification.get("directory"))
        if not isinstance(case, str) or not case:
            raise ValueError(f"auxiliary mod specification has no case: {specification!r}")
        expected = specification
    else:
        raise ValueError(f"auxiliary mod specification must be a string or object: {specification!r}")

    source_dir = corpus_root / case
    metadata_path = source_dir / "fabric.mod.json"
    if not metadata_path.is_file():
        raise ValueError(f"auxiliary fixture directory does not exist: {source_dir}")
    metadata = load_json(metadata_path)
    metadata_id = metadata.get("id")
    if not isinstance(metadata_id, str) or not metadata_id:
        raise ValueError(f"{source_dir}: auxiliary metadata id must be a non-empty string")
    dependencies = metadata.get("depends", {})
    declared = sorted(dependencies) if isinstance(dependencies, dict) else []
    expected_id = expected.get("id", metadata_id)
    expected_depends = expected.get("depends", declared)
    if not isinstance(expected_id, str) or not expected_id:
        raise ValueError(f"{source_dir}: auxiliary expected id must be a non-empty string")
    if not isinstance(expected_depends, list) or not all(
        isinstance(item, str) for item in expected_depends
    ):
        raise ValueError(f"{source_dir}: auxiliary expected depends must be a string list")
    # The primary's dependency boundary is the minimum relationship expected
    # for a same-stage auxiliary transformer.  An explicit object can pin the
    # complete dependency list, while compact string entries still receive a
    # useful graph sanity check.
    primary_depends = set(primary.get("depends", []))
    if not primary_depends.issubset(set(declared)):
        raise ValueError(
            f"{source_dir}: auxiliary dependencies {declared} do not include "
            f"primary dependencies {sorted(primary_depends)}"
        )
    return case, {
        "id": expected_id,
        "entrypoint": expected.get("entrypoint"),
        "depends": expected_depends,
    }


def compile_corpus(corpus_root: Path, corpus: dict[str, Any], base_classes: Path,
                   staging_root: Path, javac: str) -> Path:
    classes = staging_root / "classes"
    mods = staging_root / "mods"
    shutil.copytree(base_classes, classes)
    mods.mkdir()
    fixtures = corpus.get("fixtures")
    if not isinstance(fixtures, list) or len(fixtures) != CASE_COUNT:
        raise ValueError("corpus.json must enumerate exactly 25 fixtures")

    seen_cases: set[str] = set()
    seen_mod_ids: set[str] = set()
    for entry in fixtures:
        if not isinstance(entry, dict):
            raise ValueError("corpus fixture entry must be an object")
        case = entry.get("case")
        if not isinstance(case, str) or case in seen_cases:
            raise ValueError(f"invalid or duplicate fixture case: {case!r}")
        seen_cases.add(case)
        source_dir = corpus_root / case
        if not source_dir.is_dir():
            raise ValueError(f"fixture directory does not exist: {source_dir}")
        metadata = compile_mod(
            source_dir,
            case,
            entry,
            classes,
            mods,
            javac,
            require_entrypoint=True,
        )
        mod_id = metadata.get("id")
        if not isinstance(mod_id, str) or mod_id in seen_mod_ids:
            raise ValueError(f"duplicate or invalid staged mod id: {mod_id!r}")
        seen_mod_ids.add(mod_id)

    auxiliary_cases: set[str] = set()
    for primary in fixtures:
        specifications = primary.get("auxiliaryMods", [])
        if specifications is None:
            specifications = []
        if not isinstance(specifications, list):
            raise ValueError(f"fixture {primary.get('case')}: auxiliaryMods must be a list")
        for specification in specifications:
            auxiliary_case, auxiliary = auxiliary_entry(corpus_root, primary, specification)
            if auxiliary_case in seen_cases or auxiliary_case in auxiliary_cases:
                raise ValueError(f"duplicate auxiliary fixture directory: {auxiliary_case}")
            auxiliary_cases.add(auxiliary_case)
            source_dir = corpus_root / auxiliary_case
            metadata = compile_mod(
                source_dir,
                auxiliary_case,
                auxiliary,
                classes,
                mods,
                javac,
                require_entrypoint=False,
            )
            mod_id = metadata.get("id")
            if not isinstance(mod_id, str) or mod_id in seen_mod_ids:
                raise ValueError(f"duplicate or invalid staged auxiliary mod id: {mod_id!r}")
            seen_mod_ids.add(mod_id)

    expected = {str(entry.get("case")) for entry in fixtures if isinstance(entry, dict)}
    if seen_cases != expected:
        raise ValueError("corpus case inventory changed while staging")
    return mods


def copy_baseline_mods(source: Path | None, mods: Path) -> None:
    if source is None:
        return
    if not source.is_dir():
        raise ValueError(f"baseline mods directory does not exist: {source}")
    for item in sorted(source.iterdir()):
        if not (item.is_dir() or item.name.endswith(".jar")):
            continue
        destination = mods / item.name
        if destination.exists():
            raise ValueError(f"baseline mod collides with corpus entry: {destination.name}")
        if item.is_dir():
            shutil.copytree(item, destination)
        else:
            shutil.copy2(item, destination)


def refresh(log_file: Any) -> list[str]:
    log_file.flush()
    log_file.seek(0)
    return log_file.read().splitlines()


def stop_owned_process(proc: subprocess.Popen[str], timeout: float = STOP_TIMEOUT) -> None:
    """Terminate and reap only the Popen-owned server process."""
    if proc.poll() is not None:
        return
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5.0)


def run_server(binary: Path, classes: Path, mods: Path, world: Path,
               timeout: float) -> tuple[int | None, list[str]]:
    command = [
        str(binary),
        "--jvm=true",
        "--jvm-strict=true",
        "--jvm-classes=" + str(classes),
        "--jvm-mods=" + str(mods),
        "--world-dir=" + str(world),
        "--port=0",
    ]
    with tempfile.TemporaryFile(mode="w+", encoding="utf-8") as log_file:
        proc = subprocess.Popen(
            command,
            cwd=str(binary.parent.parent),
            stdout=log_file,
            stderr=subprocess.STDOUT,
            text=True,
        )
        lines: list[str] = []
        deadline = time.monotonic() + timeout
        try:
            while time.monotonic() < deadline and proc.poll() is None:
                lines = refresh(log_file)
                if any(
                    "CORPUS case=25 status=PASS phase=corpus-complete" in line
                    for line in lines
                ):
                    break
                time.sleep(0.1)
        finally:
            stop_owned_process(proc)
            lines = refresh(log_file)
        return proc.returncode, lines


def write_evidence(path: Path, lines: list[str]) -> None:
    path.write_text("\n".join(lines) + ("\n" if lines else ""), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--classes", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument(
        "--corpus", type=Path,
        default=Path(__file__).resolve().parent / "jvm_fixture" / "corpus",
    )
    parser.add_argument(
        "--report-tool", type=Path,
        default=Path(__file__).resolve().parents[1] / "tools" / "jvm_compatibility_report.py",
    )
    parser.add_argument(
        "--baseline-mods", type=Path,
        help="optional CMake-produced fixture-mods directory to run in the same JVM process",
    )
    parser.add_argument("--timeout", type=float, default=SERVER_TIMEOUT)
    parser.add_argument("--report-output", type=Path)
    parser.add_argument("--evidence-output", type=Path)
    args = parser.parse_args()

    binary = args.binary.resolve()
    base_classes = args.classes.resolve()
    corpus_root = args.corpus.resolve()
    manifest = args.manifest.resolve()
    report_tool = args.report_tool.resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        print(f"corpus smoke: binary is not executable: {binary}", file=sys.stderr)
        return 2
    if not base_classes.is_dir():
        print(f"corpus smoke: classes directory does not exist: {base_classes}", file=sys.stderr)
        return 2
    corpus_path = corpus_root / "corpus.json"
    if not corpus_path.is_file():
        print(f"corpus smoke: corpus inventory does not exist: {corpus_path}", file=sys.stderr)
        return 2
    javac = shutil.which("javac")
    if javac is None:
        print("corpus smoke: javac is not available", file=sys.stderr)
        return 2

    try:
        corpus = load_json(corpus_path)
        with tempfile.TemporaryDirectory(prefix="cppfm-jvm-corpus-") as temporary:
            staging = Path(temporary) / "staging"
            staging.mkdir()
            mods = compile_corpus(corpus_root, corpus, base_classes, staging, javac)
            copy_baseline_mods(
                args.baseline_mods.resolve() if args.baseline_mods else None,
                mods,
            )
            world = Path(temporary) / "world"
            world.mkdir()
            returncode, lines = run_server(binary, staging / "classes", mods, world, args.timeout)
            evidence = Path(temporary) / "corpus-output.log"
            write_evidence(evidence, lines)
            if args.evidence_output:
                args.evidence_output.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(evidence, args.evidence_output)
            report_output = args.report_output or (Path(temporary) / "compatibility-report.json")
            report_command = [
                sys.executable, str(report_tool),
                "--corpus", str(corpus_path),
                "--manifest", str(manifest),
                "--evidence", str(evidence),
                "--output", str(report_output),
            ]
            report = run_checked(report_command, timeout=30.0)
            if returncode != 0:
                print(f"corpus smoke: server exit code {returncode}", file=sys.stderr)
            if report.stdout:
                print(report.stdout, end="")
            if report.returncode != 0:
                if report.stderr:
                    print(report.stderr, file=sys.stderr, end="")
                return 1
            if returncode != 0:
                return 1
            print("jvm compatibility corpus: PASS")
            return 0
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"corpus smoke: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
