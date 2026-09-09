#!/usr/bin/env python3
"""Exercise the deterministic contracts of the plan52 Modrinth probe.

The real archives and JVM processes are intentionally not a CTest dependency:
they are cached, version-pinned evidence and may be unavailable on a clean
checkout.  This test covers the fail-closed offline decision and the archive
classification rules without network access or repository writes.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import zipfile
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
LOCK = ROOT / "tests/real_mod_corpus/compatibility_candidates.lock.json"
PROBE = TOOLS / "probe_modrinth_candidates.py"


def run_bounded(command: list[str], timeout: float = 30.0) -> subprocess.CompletedProcess[str]:
    """Run an owned helper with a hard timeout and process-group cleanup."""
    process = subprocess.Popen(
        command,
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        stdin=subprocess.DEVNULL,
        text=True,
        start_new_session=True,
    )
    try:
        stdout, _ = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired as failure:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        stdout, _ = process.communicate(timeout=5.0)
        raise AssertionError(
            f"command timed out after {timeout}s: {' '.join(command)}\n{stdout}"
        ) from failure
    return subprocess.CompletedProcess(command, process.returncode, stdout, "")


def load(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise AssertionError(f"expected JSON object: {path}")
    return value


def probe_module() -> Any:
    sys.path.insert(0, str(TOOLS))
    import probe_modrinth_candidates as probe  # noqa: PLC0415

    return probe


def artifact_for(path: Path, expected: str = "TARGET_COMPATIBLE") -> dict[str, Any]:
    data = path.read_bytes()
    return {
        "id": "fixture-candidate",
        "projectId": "fixture-project",
        "versionId": "fixture-version",
        "filename": path.name,
        "url": f"https://cdn.modrinth.com/data/fixture-project/versions/fixture-version/{path.name}",
        "sha1": hashlib.sha1(data).hexdigest(),
        "sha512": hashlib.sha512(data).hexdigest(),
        "size": len(data),
        "modrinthGameVersions": ["1.21.4"],
        "modrinthLoaders": ["fabric"],
        "expected": expected,
        "runWith": [],
    }


def target_manifest(artifact: dict[str, Any]) -> dict[str, Any]:
    return {
        "target": {
            "game": "minecraft",
            "version": "1.21.4",
            "protocol": 769,
            "javaMajor": 21,
            "loader": "fabric",
        },
        "artifacts": [artifact],
    }


def assert_lock_is_well_formed() -> None:
    probe = probe_module()
    manifest = load(LOCK)
    artifacts = probe._validate_manifest(manifest)
    assert len(artifacts) == 12, len(artifacts)
    assert sum(item["expected"] == "TARGET_COMPATIBLE" for item in artifacts) == 8
    assert sum(item["expected"] == "INCOMPATIBLE_TARGET" for item in artifacts) == 2
    assert sum(item["expected"] == "INCOMPATIBLE_RUNTIME" for item in artifacts) == 1
    assert sum(item["expected"] == "INVALID_METADATA" for item in artifacts) == 1


def assert_missing_cache_is_skip() -> None:
    """An offline run must never turn unavailable archives into PASS."""
    with tempfile.TemporaryDirectory(prefix="cppfm-candidate-contract-") as temporary:
        root = Path(temporary)
        report = root / "report.json"
        result = run_bounded([
            sys.executable,
            str(PROBE),
            "--manifest", str(LOCK),
            "--cache-dir", str(root / "cache"),
            "--report-output", str(report),
            "--evidence-dir", str(root / "evidence"),
        ])
        assert result.returncode == 2, result.stdout
        payload = load(report)
        assert payload["schema"] == "cppfm.modrinth-candidates.report.v1", payload
        assert payload["status"] == "SKIP", payload
        assert payload["options"]["networkAccessed"] is False, payload
        assert payload["summary"] == {
            "total": 12,
            "pass": 0,
            "fail": 0,
            "skip": 12,
            "targetCompatible": 0,
            "runtimePass": 0,
            "runtimeFail": 0,
            "runtimeSkip": 0,
            "incompatibleTarget": 0,
            "incompatibleRuntime": 0,
            "invalidMetadata": 0,
        }, payload
        assert all(item["status"] == "SKIP" for item in payload["artifacts"]), payload


def assert_archive_classifications_are_fail_closed() -> None:
    probe = probe_module()
    with tempfile.TemporaryDirectory(prefix="cppfm-candidate-archive-") as temporary:
        root = Path(temporary)
        valid = root / "valid.jar"
        with zipfile.ZipFile(valid, "w") as archive:
            archive.writestr(
                "fabric.mod.json",
                json.dumps({
                    "schemaVersion": 1,
                    "id": "fixture",
                    "version": "1.0.0",
                    "environment": "server",
                }),
            )
            archive.writestr("fixture/Marker.class", b"\xca\xfe\xba\xbe\x00\x00\x00\x3d")
        valid_result = probe._inspect(artifact_for(valid), valid, 21)
        assert valid_result["classification"] == "TARGET_COMPATIBLE", valid_result
        assert valid_result["integrity"]["status"] == "PASS", valid_result

        neoforge = root / "neoforge.jar"
        with zipfile.ZipFile(neoforge, "w") as archive:
            archive.writestr("META-INF/neoforge.mods.toml", "modLoader=\"javafml\"\n")
        target_result = probe._inspect(artifact_for(neoforge, "INCOMPATIBLE_TARGET"), neoforge, 21)
        assert target_result["classification"] == "INCOMPATIBLE_TARGET", target_result

        invalid = root / "invalid.jar"
        with zipfile.ZipFile(invalid, "w") as archive:
            archive.writestr("fabric.mod.json", "{not-json")
        invalid_result = probe._inspect(artifact_for(invalid, "INVALID_METADATA"), invalid, 21)
        assert invalid_result["classification"] == "INVALID_METADATA", invalid_result

        corrupt = root / "corrupt.jar"
        corrupt.write_bytes(b"not a zip archive")
        corrupt_result = probe._inspect(artifact_for(corrupt), corrupt, 21)
        assert corrupt_result["classification"] == "INTEGRITY_FAILURE", corrupt_result


def assert_manifest_rejects_unsafe_inputs() -> None:
    probe = probe_module()
    with tempfile.TemporaryDirectory(prefix="cppfm-candidate-manifest-") as temporary:
        path = Path(temporary) / "fixture.jar"
        path.write_bytes(b"fixture")
        artifact = artifact_for(path)

        invalid_target = target_manifest(artifact)
        invalid_target["target"]["protocol"] = 776
        try:
            probe._validate_manifest(invalid_target)
        except probe.CandidateError:
            pass
        else:
            raise AssertionError("protocol 776 candidate manifest was accepted")

        unsafe_url = target_manifest(artifact)
        unsafe_url["artifacts"][0]["url"] = "https://example.invalid/fixture.jar"
        try:
            probe._validate_manifest(unsafe_url)
        except probe.CandidateError:
            pass
        else:
            raise AssertionError("URL outside the Modrinth CDN allowlist was accepted")


def main() -> int:
    assert_lock_is_well_formed()
    assert_missing_cache_is_skip()
    assert_archive_classifications_are_fail_closed()
    assert_manifest_rejects_unsafe_inputs()
    print("candidate probe contract: PASS")
    print("locked 12-entry manifest: PASS")
    print("offline missing-cache decision: SKIP (no false PASS)")
    print("archive classification and input allowlist: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
