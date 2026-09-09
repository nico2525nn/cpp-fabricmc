#!/usr/bin/env python3
"""Inspect and optionally execute pinned Modrinth Fabric server candidates.

The candidate suite is intentionally separate from the locked three-mod
reference corpus.  It answers a different question: which real, unmodified
JARs can be identified, placed on the cppfm class path, and started through
the embedded JVM on the target host?  It does not turn a successful JVM
bootstrap into gameplay parity.

The default mode is offline and never downloads anything.  ``--provision`` is
an explicit network opt-in limited to the Modrinth CDN URLs in the lock file.
``--run`` starts each target-compatible candidate (and its declared runtime
companions) in an owned process group, waits for the JVM/mod bootstrap marker,
then requests a clean shutdown.  All process output is retained as evidence.
Create entries are expected to be reported as ``INCOMPATIBLE_TARGET`` when a
NeoForge or an older Minecraft/Fabric artifact is inspected; they are never
counted as a Fabric 1.21.4 PASS.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from typing import Any, Iterable
from urllib.parse import urlparse
from urllib.request import Request, urlopen
import zipfile


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "tests/real_mod_corpus/compatibility_candidates.lock.json"
DEFAULT_CACHE = ROOT / "build/real-mod-candidates"
DEFAULT_REPORT = DEFAULT_CACHE / "compatibility-candidates-report.json"
DEFAULT_EVIDENCE = DEFAULT_CACHE / "evidence"
REPORT_SCHEMA = "cppfm.modrinth-candidates.report.v1"
ALLOWED_HOST = "cdn.modrinth.com"
ALLOWED_PREFIX = "/data/"
VALID_CLASSIFICATION = {
    "TARGET_COMPATIBLE",
    "INCOMPATIBLE_TARGET",
    "INCOMPATIBLE_RUNTIME",
    "INVALID_METADATA",
    "INTEGRITY_FAILURE",
    "UNAVAILABLE",
}


class CandidateError(RuntimeError):
    """A manifest, archive, or process-contract error."""


def _sha(path: Path, algorithm: str) -> str:
    digest = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _safe_filename(value: Any) -> str:
    if not isinstance(value, str) or not value or Path(value).name != value:
        raise CandidateError(f"unsafe artifact filename: {value!r}")
    if value in {".", ".."} or "\\" in value or value.startswith("/"):
        raise CandidateError(f"unsafe artifact filename: {value!r}")
    return value


def _validate_url(artifact: dict[str, Any]) -> str:
    value = artifact.get("url")
    if not isinstance(value, str):
        raise CandidateError(f"{artifact.get('id', '<unknown>')} has no URL")
    parsed = urlparse(value)
    if parsed.scheme != "https" or parsed.hostname != ALLOWED_HOST:
        raise CandidateError(f"URL is outside the Modrinth CDN allowlist: {value}")
    if not parsed.path.startswith(ALLOWED_PREFIX):
        raise CandidateError(f"URL is outside the Modrinth data path: {value}")
    project = artifact.get("projectId")
    version = artifact.get("versionId")
    if not isinstance(project, str) or not isinstance(version, str):
        raise CandidateError(f"{artifact.get('id', '<unknown>')} lacks project/version IDs")
    expected = f"/data/{project}/versions/{version}/"
    if not parsed.path.startswith(expected):
        raise CandidateError(f"URL project/version does not match lock: {value}")
    return value


def _read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise CandidateError(f"cannot read JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise CandidateError(f"JSON root is not an object: {path}")
    return value


def _validate_manifest(manifest: dict[str, Any]) -> list[dict[str, Any]]:
    target = manifest.get("target")
    if not isinstance(target, dict) or target.get("version") != "1.21.4" \
            or target.get("protocol") != 769 or target.get("javaMajor") != 21 \
            or target.get("loader") != "fabric":
        raise CandidateError("candidate lock target must be Fabric Minecraft 1.21.4 / protocol 769 / Java 21")
    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        raise CandidateError("candidate lock has no artifacts")
    result: list[dict[str, Any]] = []
    seen: set[str] = set()
    valid_expected = {
        "TARGET_COMPATIBLE", "INCOMPATIBLE_TARGET", "INCOMPATIBLE_RUNTIME", "INVALID_METADATA"
    }
    for raw in artifacts:
        if not isinstance(raw, dict):
            raise CandidateError("candidate entry is not an object")
        item = dict(raw)
        identifier = item.get("id")
        if not isinstance(identifier, str) or not re.fullmatch(r"[a-z0-9][a-z0-9._+-]*", identifier):
            raise CandidateError(f"invalid candidate id: {identifier!r}")
        if identifier in seen:
            raise CandidateError(f"duplicate candidate id: {identifier}")
        seen.add(identifier)
        _safe_filename(item.get("filename"))
        _validate_url(item)
        for key, length in (("sha1", 40), ("sha512", 128)):
            value = item.get(key)
            if not isinstance(value, str) or not re.fullmatch(rf"[0-9a-f]{{{length}}}", value):
                raise CandidateError(f"{identifier} has invalid {key}")
        if not isinstance(item.get("size"), int) or item["size"] <= 0:
            raise CandidateError(f"{identifier} has invalid size")
        if item.get("expected") not in valid_expected:
            raise CandidateError(f"{identifier} has invalid expected classification")
        runtime_requirements = item.get("runtimeRequirements", {})
        if not isinstance(runtime_requirements, dict):
            raise CandidateError(f"{identifier} has invalid runtimeRequirements")
        java_major_min = runtime_requirements.get("javaMajorMin")
        if java_major_min is not None and (
            not isinstance(java_major_min, int) or isinstance(java_major_min, bool) or java_major_min < 1
        ):
            raise CandidateError(f"{identifier} has invalid runtimeRequirements.javaMajorMin")
        for key in ("modrinthGameVersions", "modrinthLoaders", "runWith"):
            if not isinstance(item.get(key), list) or not all(isinstance(value, str) for value in item[key]):
                raise CandidateError(f"{identifier} has invalid {key}")
        result.append(item)
    known = {item["id"] for item in result}
    for item in result:
        for companion in item["runWith"]:
            if companion not in known:
                raise CandidateError(f"{item['id']} names unknown runtime companion {companion}")
    return result


def _download(artifact: dict[str, Any], destination: Path) -> None:
    url = _validate_url(artifact)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", prefix=f".{destination.name}.", suffix=".part",
            dir=destination.parent, delete=False
        ) as output:
            temporary = Path(output.name)
            request = Request(url, headers={"User-Agent": "cpp-fabricmc/compatibility-probe"})
            with urlopen(request, timeout=45) as response:
                total = 0
                while True:
                    chunk = response.read(1024 * 1024)
                    if not chunk:
                        break
                    total += len(chunk)
                    if total > artifact["size"]:
                        raise CandidateError(f"download exceeds locked size for {artifact['id']}")
                    output.write(chunk)
            if total != artifact["size"]:
                raise CandidateError(
                    f"download size mismatch for {artifact['id']}: {total} != {artifact['size']}"
                )
        _verify_integrity(artifact, temporary)
        os.replace(temporary, destination)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def _verify_integrity(artifact: dict[str, Any], path: Path) -> dict[str, Any]:
    size = path.stat().st_size if path.is_file() else 0
    sha1 = _sha(path, "sha1") if size else ""
    sha512 = _sha(path, "sha512") if size else ""
    errors: list[str] = []
    if size != artifact["size"]:
        errors.append(f"size {size} != {artifact['size']}")
    if sha1 != artifact["sha1"]:
        errors.append(f"sha1 {sha1 or '<missing>'} != {artifact['sha1']}")
    if sha512 != artifact["sha512"]:
        errors.append(f"sha512 {sha512 or '<missing>'} != {artifact['sha512']}")
    return {
        "status": "PASS" if not errors else "FAIL",
        "size": size,
        "sha1": sha1,
        "sha512": sha512,
        "reason": "locked size and hashes match" if not errors else "; ".join(errors),
    }


def _metadata_summary(metadata: dict[str, Any]) -> dict[str, Any]:
    entrypoints = metadata.get("entrypoints", {})
    entrypoint_count = 0
    if isinstance(entrypoints, dict):
        for value in entrypoints.values():
            if isinstance(value, str):
                entrypoint_count += 1
            elif isinstance(value, list):
                entrypoint_count += sum(isinstance(item, (str, dict)) for item in value)
    mixins = metadata.get("mixins", [])
    mixin_count = len(mixins) if isinstance(mixins, list) else (1 if isinstance(mixins, str) else 0)
    return {
        "id": metadata.get("id"),
        "version": metadata.get("version"),
        "environment": metadata.get("environment"),
        "entrypointCount": entrypoint_count,
        "mixinConfigCount": mixin_count,
        "depends": metadata.get("depends", {}),
        "breaks": metadata.get("breaks", {}),
    }


def _nested_summary(archive: zipfile.ZipFile, names: Iterable[str]) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for name in sorted(names):
        if not name.startswith("META-INF/jars/") or not name.endswith(".jar"):
            continue
        info = archive.getinfo(name)
        entry: dict[str, Any] = {"name": name, "size": info.file_size}
        if info.file_size > 128 * 1024 * 1024:
            entry["metadataStatus"] = "SKIP_SIZE_LIMIT"
        else:
            try:
                with zipfile.ZipFile(io.BytesIO(archive.read(name))) as nested:
                    raw = nested.read("fabric.mod.json")
                    metadata = json.loads(raw.decode("utf-8"))
                    if isinstance(metadata, dict):
                        entry["mod"] = _metadata_summary(metadata)
                        entry["metadataStatus"] = "PASS"
                    else:
                        entry["metadataStatus"] = "FAIL_ROOT"
            except KeyError:
                entry["metadataStatus"] = "NO_FABRIC_METADATA"
            except (UnicodeError, json.JSONDecodeError, zipfile.BadZipFile, OSError) as exc:
                entry["metadataStatus"] = "FAIL"
                entry["reason"] = str(exc)
        result.append(entry)
    return result


def _max_class_major(archive: zipfile.ZipFile, names: Iterable[str]) -> int | None:
    maximum: int | None = None
    for name in names:
        if not name.endswith(".class"):
            continue
        try:
            header = archive.read(name)[:8]
        except KeyError:
            continue
        if len(header) == 8 and header[:4] == b"\xca\xfe\xba\xbe":
            major = int.from_bytes(header[6:8], "big")
            maximum = major if maximum is None else max(maximum, major)
    return maximum


def _inspect(artifact: dict[str, Any], path: Path, target_java_major: int) -> dict[str, Any]:
    integrity = _verify_integrity(artifact, path)
    base: dict[str, Any] = {
        "integrity": integrity,
        "archive": {},
        "classification": "INTEGRITY_FAILURE" if integrity["status"] != "PASS" else "UNAVAILABLE",
        "reason": integrity["reason"],
    }
    if integrity["status"] != "PASS":
        return base
    try:
        with zipfile.ZipFile(path) as archive:
            bad = archive.testzip()
            names = archive.namelist()
            neo = sorted(name for name in names if name.endswith("neoforge.mods.toml"))
            forge = sorted(name for name in names if name.endswith("mods.toml"))
            base["archive"] = {
                "memberCount": len(names),
                "nestedJarCount": sum(
                    name.startswith("META-INF/jars/") and name.endswith(".jar")
                    for name in names
                ),
                "nested": _nested_summary(archive, names),
                "maxClassMajor": _max_class_major(archive, names),
                "neoforgeMetadata": neo,
                "forgeMetadata": forge,
                "corruptMember": bad,
            }
            if bad is not None:
                base["classification"] = "INTEGRITY_FAILURE"
                base["reason"] = f"corrupt JAR member: {bad}"
                return base
            try:
                raw_metadata = archive.read("fabric.mod.json")
                metadata = json.loads(raw_metadata.decode("utf-8"))
            except KeyError as exc:
                metadata = None
                base["classification"] = "INCOMPATIBLE_TARGET"
                formats = "NeoForge" if neo else ("Forge" if forge else "unknown loader")
                base["reason"] = f"fabric.mod.json is missing; detected {formats} archive metadata"
            except (UnicodeError, json.JSONDecodeError) as exc:
                metadata = None
                base["classification"] = "INVALID_METADATA"
                base["reason"] = f"fabric.mod.json is not valid UTF-8/JSON: {exc}"
            if isinstance(metadata, dict):
                base["metadata"] = _metadata_summary(metadata)
                fabric_loaders = artifact["modrinthLoaders"]
                target_versions = artifact["modrinthGameVersions"]
                environment = metadata.get("environment", "*")
                if not isinstance(metadata.get("id"), str) or not metadata["id"]:
                    base["classification"] = "INVALID_METADATA"
                    base["reason"] = "fabric.mod.json has no valid mod id"
                elif "fabric" not in fabric_loaders:
                    base["classification"] = "INCOMPATIBLE_TARGET"
                    base["reason"] = "Modrinth lock does not provide a Fabric artifact"
                elif "1.21.4" not in target_versions:
                    base["classification"] = "INCOMPATIBLE_TARGET"
                    base["reason"] = "Modrinth artifact does not target Minecraft 1.21.4"
                elif environment == "client":
                    base["classification"] = "INCOMPATIBLE_TARGET"
                    base["reason"] = "Fabric metadata declares a client-only environment"
                else:
                    runtime_requirements = artifact.get("runtimeRequirements", {})
                    java_major_min = runtime_requirements.get("javaMajorMin")
                    if isinstance(java_major_min, int) and java_major_min > target_java_major:
                        base["classification"] = "INCOMPATIBLE_RUNTIME"
                        base["reason"] = (
                            f"artifact requires Java {java_major_min}+ but the locked runtime is Java "
                            f"{target_java_major}"
                        )
                    else:
                        base["classification"] = "TARGET_COMPATIBLE"
                        base["reason"] = "valid Fabric metadata and locked Modrinth target are compatible"
            return base
    except (OSError, zipfile.BadZipFile) as exc:
        base["classification"] = "INTEGRITY_FAILURE"
        base["reason"] = f"cannot inspect JAR: {exc}"
        return base


def _link_or_copy(source: Path, destination: Path) -> None:
    try:
        destination.symlink_to(source.resolve())
    except (OSError, NotImplementedError):
        shutil.copy2(source, destination)


def _read_log(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def _stop_owned(process: subprocess.Popen[Any]) -> tuple[int | None, bool]:
    if process.poll() is not None:
        return process.returncode, False
    timed_out = False
    try:
        if os.name == "nt":
            process.send_signal(signal.CTRL_BREAK_EVENT)
        else:
            os.killpg(process.pid, signal.SIGINT)
        process.wait(timeout=8)
    except (subprocess.TimeoutExpired, ProcessLookupError):
        timed_out = True
        try:
            if os.name == "nt":
                process.kill()
            else:
                os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pass
    return process.returncode, timed_out


def _run_process(
    candidate_id: str,
    artifact_ids: list[str],
    artifacts_by_id: dict[str, dict[str, Any]],
    paths: dict[str, Path],
    binary: Path,
    classes: Path,
    libraries: Path | None,
    java_home: Path | None,
    evidence_dir: Path,
    timeout_seconds: float,
) -> dict[str, Any]:
    evidence_dir.mkdir(parents=True, exist_ok=True)
    log_path = evidence_dir / f"{re.sub(r'[^a-zA-Z0-9._-]+', '_', candidate_id)}.log"
    with tempfile.TemporaryDirectory(prefix="cppfm-mod-probe-") as temporary:
        root = Path(temporary)
        mods = root / "mods"
        mods.mkdir()
        world = root / "world"
        for artifact_id in artifact_ids:
            _link_or_copy(paths[artifact_id], mods / artifacts_by_id[artifact_id]["filename"])
        command = [
            str(binary),
            "--port=0",
            f"--world-dir={world}",
            "--jvm=true",
            "--jvm-strict=true",
            f"--jvm-classes={classes}",
            f"--jvm-mods={mods}",
        ]
        if libraries is not None:
            command.append(f"--jvm-libraries={libraries}")
        if java_home is not None:
            command.append(f"--jvm-java-home={java_home}")
        with log_path.open("w", encoding="utf-8") as log:
            environment = os.environ.copy()
            environment["CPPFM_SERVER_DIR"] = str(root)
            try:
                process = subprocess.Popen(
                    command,
                    cwd=str(ROOT),
                    env=environment,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    start_new_session=True,
                )
            except OSError as exc:
                return {
                    "status": "FAIL",
                    "reason": f"cannot start cppfm: {exc}",
                    "command": command,
                    "log": str(log_path),
                }
            deadline = time.monotonic() + timeout_seconds
            ready = False
            while time.monotonic() < deadline and process.poll() is None:
                text = _read_log(log_path)
                if "embedded HotSpot started" in text and "loaded " in text:
                    ready = True
                    break
                time.sleep(0.05)
            startup_timed_out = not ready
            if ready:
                time.sleep(min(0.5, max(0.0, deadline - time.monotonic())))
            returncode, stop_timed_out = _stop_owned(process)
        output = _read_log(log_path)
    loaded_match = re.search(r"loaded (\d+) mod candidate\(s\), initialized (\d+) entrypoint\(s\)", output)
    errors = [line for line in output.splitlines() if (
        "Knot bootstrap failed" in line
        or "mod bootstrap failed" in line
        or "[cppfm] fatal:" in line
        or "[cppfm][jvm][ERROR]" in line
    )]
    clean = "stopped cleanly" in output and "[cppfm] bye" in output
    markers = {
        "jvmStarted": "embedded HotSpot started" in output,
        "modBootstrap": loaded_match is not None,
        "cleanShutdown": clean,
        "processExitCode": returncode,
        "startupTimedOut": startup_timed_out,
        "stopTimedOut": stop_timed_out,
        "loadedMods": int(loaded_match.group(1)) if loaded_match else None,
        "initializedEntrypoints": int(loaded_match.group(2)) if loaded_match else None,
    }
    passed = (
        not startup_timed_out
        and not stop_timed_out
        and returncode == 0
        and markers["jvmStarted"]
        and markers["modBootstrap"]
        and markers["cleanShutdown"]
        and not errors
    )
    return {
        "status": "PASS" if passed else "FAIL",
        "reason": "JVM/mod bootstrap and owned clean shutdown markers passed"
        if passed else "runtime markers or clean-shutdown contract failed",
        "artifacts": artifact_ids,
        "command": command,
        "log": str(log_path),
        "markers": markers,
        "errors": errors[-20:],
        "outputBytes": len(output.encode("utf-8")),
    }


def _write_report(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--cache-dir", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--report-output", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--evidence-dir", type=Path, default=DEFAULT_EVIDENCE)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--classes", type=Path)
    parser.add_argument("--jvm-libraries", type=Path)
    parser.add_argument("--java-home", type=Path)
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument("--provision", action="store_true", help="download locked artifacts from the Modrinth CDN")
    parser.add_argument("--run", action="store_true", help="execute target-compatible artifacts through cppfm")
    args = parser.parse_args(argv)
    if args.timeout <= 0 or args.timeout > 600:
        parser.error("--timeout must be between 0 and 600 seconds")
    try:
        manifest = _read_json(args.manifest)
        artifacts = _validate_manifest(manifest)
    except CandidateError as exc:
        print(f"candidate probe: FAIL: {exc}", file=sys.stderr)
        return 1

    args.cache_dir.mkdir(parents=True, exist_ok=True)
    paths: dict[str, Path] = {}
    records: list[dict[str, Any]] = []
    available: dict[str, Path] = {}
    network_accessed = False
    for artifact in artifacts:
        identifier = artifact["id"]
        path = args.cache_dir / _safe_filename(artifact["filename"])
        availability = "PASS"
        availability_reason = "locked file is present"
        if not path.is_file():
            if args.provision:
                try:
                    _download(artifact, path)
                    network_accessed = True
                    availability_reason = "downloaded from the locked Modrinth CDN URL"
                except (CandidateError, OSError, ValueError) as exc:
                    availability = "FAIL"
                    availability_reason = str(exc)
            else:
                availability = "SKIP"
                availability_reason = "offline cache is missing; rerun with --provision"
        if availability == "PASS" and path.is_file():
            integrity = _verify_integrity(artifact, path)
            if integrity["status"] != "PASS":
                availability = "FAIL"
                availability_reason = integrity["reason"]
            else:
                available[identifier] = path
                paths[identifier] = path
        records.append({
            "lock": {
                "id": identifier,
                "projectId": artifact["projectId"],
                "versionId": artifact["versionId"],
                "filename": artifact["filename"],
                "url": artifact["url"],
            },
            "availability": {"status": availability, "reason": availability_reason},
        })

    by_id = {artifact["id"]: artifact for artifact in artifacts}
    record_by_id = {record["lock"]["id"]: record for record in records}
    for artifact in artifacts:
        identifier = artifact["id"]
        record = record_by_id[identifier]
        if identifier not in available:
            record["inspection"] = {
                "classification": "UNAVAILABLE",
                "reason": record["availability"]["reason"],
            }
            record["expected"] = artifact["expected"]
            record["runtime"] = {"status": "SKIP", "reason": "archive is unavailable"}
            record["status"] = "FAIL" if record["availability"]["status"] == "FAIL" else "SKIP"
            continue
        inspection = _inspect(artifact, available[identifier], manifest["target"]["javaMajor"])
        record["integrity"] = inspection.pop("integrity")
        record["inspection"] = inspection
        actual = inspection["classification"]
        expected = artifact["expected"]
        record["expected"] = expected
        record["classificationMatch"] = actual == expected
        if actual == "TARGET_COMPATIBLE" and args.run:
            companion_ids = list(dict.fromkeys([*artifact["runWith"], identifier]))
            missing_companion = [item for item in companion_ids if item not in available]
            if missing_companion:
                runtime = {
                    "status": "SKIP",
                    "reason": f"runtime companion archive unavailable: {', '.join(missing_companion)}",
                    "artifacts": companion_ids,
                }
            else:
                runtime = _run_process(
                    identifier, companion_ids, by_id, paths,
                    args.binary.resolve() if args.binary else ROOT / "build/cppfm",
                    args.classes.resolve() if args.classes else ROOT / "build/jvm/classes",
                    args.jvm_libraries.resolve() if args.jvm_libraries else None,
                    args.java_home.resolve() if args.java_home else None,
                    args.evidence_dir, args.timeout,
                )
            record["runtime"] = runtime
        elif actual == "TARGET_COMPATIBLE":
            record["runtime"] = {"status": "SKIP", "reason": "inspection only; rerun with --run"}
        else:
            record["runtime"] = {"status": "SKIP", "reason": "not applicable to a Fabric 1.21.4 runtime"}
        if actual == expected and record["runtime"]["status"] in {"PASS", "SKIP"}:
            record["status"] = "PASS" if actual != "TARGET_COMPATIBLE" or record["runtime"]["status"] == "PASS" else "SKIP"
        else:
            record["status"] = "FAIL"

    target_records = [record for record in records if record["inspection"]["classification"] == "TARGET_COMPATIBLE"]
    runtime_pass = sum(record["runtime"]["status"] == "PASS" for record in target_records)
    runtime_fail = sum(record["runtime"]["status"] == "FAIL" for record in target_records)
    runtime_skip = sum(record["runtime"]["status"] == "SKIP" for record in target_records)
    mismatches = [record["lock"]["id"] for record in records if not record.get("classificationMatch", True)]
    failed = [record["lock"]["id"] for record in records if record["status"] == "FAIL"]
    skipped = [record["lock"]["id"] for record in records if record["status"] == "SKIP"]
    if mismatches or failed:
        status = "FAIL"
        reason = "one or more locked classifications or runtime gates failed"
    elif skipped:
        status = "SKIP"
        reason = "some archives or requested runtime gates were unavailable/not executed"
    else:
        status = "PASS"
        reason = "all locked archive classifications and requested runtime gates passed"
    report = {
        "schema": REPORT_SCHEMA,
        "suite": manifest.get("suite"),
        "status": status,
        "statusReason": reason,
        "target": manifest["target"],
        "source": manifest.get("source", {}),
        "options": {
            "networkAccessed": network_accessed,
            "provision": args.provision,
            "run": args.run,
            "binary": str(args.binary) if args.binary else None,
            "classes": str(args.classes) if args.classes else None,
            "jvmLibraries": str(args.jvm_libraries) if args.jvm_libraries else None,
            "javaHome": str(args.java_home) if args.java_home else None,
        },
        "summary": {
            "total": len(records),
            "pass": sum(record["status"] == "PASS" for record in records),
            "fail": sum(record["status"] == "FAIL" for record in records),
            "skip": sum(record["status"] == "SKIP" for record in records),
            "targetCompatible": len(target_records),
            "runtimePass": runtime_pass,
            "runtimeFail": runtime_fail,
            "runtimeSkip": runtime_skip,
            "incompatibleTarget": sum(
                record["inspection"]["classification"] == "INCOMPATIBLE_TARGET" for record in records
            ),
            "incompatibleRuntime": sum(
                record["inspection"]["classification"] == "INCOMPATIBLE_RUNTIME" for record in records
            ),
            "invalidMetadata": sum(
                record["inspection"]["classification"] == "INVALID_METADATA" for record in records
            ),
        },
        "artifacts": records,
        "limitations": [
            "A bootstrap PASS proves JVM/mod loading and clean shutdown only; it does not prove vanilla gameplay, rendering, registry, or client compatibility.",
            "The fallback class loader is not the official Mojang GameProvider. Official Fabric Loader/Knot remains an explicit separate runtime path.",
            "Create 6.x is represented by the available NeoForge 1.21.1 file and Fabric 1.20.1 file; neither is a Fabric 1.21.4 artifact.",
        ],
    }
    _write_report(args.report_output, report)
    for record in records:
        print(
            f"{record['lock']['id']}: {record['status']} "
            f"classification={record['inspection']['classification']} "
            f"runtime={record['runtime']['status']}"
        )
    print(
        f"candidate probe: {status} "
        f"(total={len(records)} pass={report['summary']['pass']} "
        f"fail={report['summary']['fail']} skip={report['summary']['skip']})"
    )
    return {"PASS": 0, "FAIL": 1, "SKIP": 2}[status]


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
