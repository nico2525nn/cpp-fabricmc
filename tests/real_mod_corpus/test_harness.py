#!/usr/bin/env python3
"""Self-test the plan52 real-mod corpus gates without network or JAR writes.

This test deliberately keeps the historical 25-case synthetic corpus separate
from the real-mod report.  It proves two contracts:

* an incomplete offline cache produces SKIP and never PASS; and
* the existing synthetic corpus report rejects evidence that contains only a
  load/entrypoint marker, while accepting the complete per-case behavior
  assertion stream.

The actual C++/Fabric process runs are performed by the two dedicated runners;
this file only exercises their deterministic report contracts.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
REAL_LOCK = ROOT / "tests/real_mod_corpus/corpus.lock.json"
REAL_COMPARE = TOOLS / "compare_real_mod_corpus.py"
REAL_FETCH = TOOLS / "fetch_real_mod_corpus.py"
SYNTHETIC_CORPUS = ROOT / "tests/jvm_fixture/corpus/corpus.json"
SYNTHETIC_REPORT = TOOLS / "jvm_compatibility_report.py"


def run_bounded(command: list[str], timeout: float = 30.0) -> subprocess.CompletedProcess[str]:
    """Run one owned helper with a hard timeout and process-group cleanup."""
    process = subprocess.Popen(
        command,
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        start_new_session=True,
    )
    try:
        stdout, _ = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        stdout, _ = process.communicate(timeout=5.0)
        raise AssertionError(f"command timed out after {timeout}s: {' '.join(command)}\n{stdout}")
    return subprocess.CompletedProcess(command, process.returncode, stdout, "")


def load(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise AssertionError(f"expected JSON object: {path}")
    return value


def assert_real_missing_cache_is_skip() -> None:
    with tempfile.TemporaryDirectory(prefix="cppfm-plan52-selftest-") as temporary:
        root = Path(temporary)
        cache = root / "empty-cache"
        report = root / "report.json"
        evidence = root / "evidence"
        result = run_bounded([
            sys.executable,
            str(REAL_COMPARE),
            "--lock", str(REAL_LOCK),
            "--cache-dir", str(cache),
            "--fabric-cache", str(root / "empty-fabric-cache"),
            "--report-output", str(report),
            "--evidence-dir", str(evidence),
            "--timeout", "2",
        ])
        assert result.returncode == 2, result.stdout
        payload = load(report)
        sys.path.insert(0, str(TOOLS))
        import compare_real_mod_corpus as real_compare  # noqa: PLC0415

        errors = real_compare.validate_report(payload)
        assert not errors, errors
        assert payload["status"] == "SKIP", payload
        assert payload["execution"]["networkAccessed"] is False, payload
        assert payload["execution"]["executionAttempted"] is False, payload
        assert all(case["status"] == "SKIP" for case in payload["artifacts"]), payload
        assert payload["combined"]["status"] == "SKIP", payload

        fetch = run_bounded([
            sys.executable,
            str(REAL_FETCH),
            "--offline",
            "--json",
            "--lock", str(REAL_LOCK),
            "--cache-dir", str(cache),
        ])
        assert fetch.returncode == 2, fetch.stdout
        fetch_payload = json.loads(fetch.stdout)
        assert fetch_payload["status"] == "SKIP", fetch.stdout
        assert fetch_payload["networkAccessed"] is False, fetch.stdout


def synthetic_manifest(corpus: dict[str, Any]) -> dict[str, Any]:
    coverage = []
    for check in corpus["manifestChecks"]:
        coverage.append({
            "class": check["class"],
            "method": check["method"],
            "mixinLevels": list(check["mixinLevels"]),
            "structuredBytecode": bool(check.get("structuredBytecode", False)),
            "nativeBackend": True,
        })
    return {
        "manifestVersion": 1,
        "gameVersion": corpus["gameVersion"],
        "protocol": corpus["protocol"],
        "methodCoverage": coverage,
        "mixin": {
            "supportedInjectionPoints": list(corpus["requiredMixinPoints"]),
            "supportedTransformers": list(corpus["requiredTransformers"]),
        },
    }


def synthetic_evidence(corpus: dict[str, Any]) -> str:
    # Importing the existing validator keeps this self-test coupled to the
    # actual required phase contract rather than duplicating 25 phase lists.
    sys.path.insert(0, str(TOOLS))
    import jvm_compatibility_report as synthetic_report  # noqa: PLC0415

    lines: list[str] = []
    for number, phases in synthetic_report.REQUIRED_PHASES.items():
        for phase in phases:
            lines.append(f"CORPUS case={number} status=PASS phase={phase}")
    lines.append("INFO corpus24 intentional callback failure was isolated")
    return "\n".join(lines) + "\n"


def assert_synthetic_requires_behavior_assertions() -> None:
    corpus = load(SYNTHETIC_CORPUS)
    with tempfile.TemporaryDirectory(prefix="cppfm-plan52-synthetic-contract-") as temporary:
        root = Path(temporary)
        corpus_copy = root / "corpus.json"
        manifest = root / "manifest.json"
        evidence = root / "evidence.log"
        output = root / "report.json"
        shutil.copy2(SYNTHETIC_CORPUS, corpus_copy)
        manifest.write_text(json.dumps(synthetic_manifest(corpus)), encoding="utf-8")

        # A loader/entrypoint-only stream is explicitly not a complete corpus.
        evidence.write_text(
            "CORPUS case=01 status=PASS phase=entrypoint\n",
            encoding="utf-8",
        )
        load_only = run_bounded([
            sys.executable,
            str(SYNTHETIC_REPORT),
            "--corpus", str(corpus_copy),
            "--manifest", str(manifest),
            "--evidence", str(evidence),
            "--output", str(output),
        ])
        assert load_only.returncode == 1, load_only.stdout
        load_only_report = load(output)
        assert load_only_report["status"] == "FAIL", load_only_report
        assert any("missing phases" in error for error in load_only_report["errors"]), load_only_report

        evidence.write_text(synthetic_evidence(corpus), encoding="utf-8")
        complete = run_bounded([
            sys.executable,
            str(SYNTHETIC_REPORT),
            "--corpus", str(corpus_copy),
            "--manifest", str(manifest),
            "--evidence", str(evidence),
            "--output", str(output),
        ])
        assert complete.returncode == 0, complete.stdout
        complete_report = load(output)
        assert complete_report["status"] == "PASS", complete_report
        assert complete_report["summary"]["fixtureCount"] == 25, complete_report
        assert complete_report["summary"]["fixturePass"] == 25, complete_report


def main() -> int:
    assert_real_missing_cache_is_skip()
    assert_synthetic_requires_behavior_assertions()
    print("real-mod corpus harness self-test: PASS")
    print("offline missing-cache: SKIP (no false PASS)")
    print("synthetic 25-case load-only: FAIL; complete function assertions: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
