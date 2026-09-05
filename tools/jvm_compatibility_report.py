#!/usr/bin/env python3
"""Compare plan51 fixture evidence with the generated JVM compatibility manifest.

The report is intentionally strict. A class being discovered is not evidence that
its behavior works. Every corpus case must emit a PASS assertion, and the
bytecode-sensitive cases must also be represented as structured coverage in the
generated manifest. The report therefore locks the bounded transformer contract to
the cases that have actually executed.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


EVIDENCE_RE = re.compile(
    r"\bCORPUS\s+case=(?P<case>\d{2})\s+status=(?P<status>PASS|FAIL)"
    r"\s+phase=(?P<phase>[^\s]+)"
)


def load_json(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return value


def parse_evidence(path: Path) -> tuple[dict[str, list[dict[str, str]]], list[str]]:
    by_case: dict[str, list[dict[str, str]]] = {f"{number:02d}": [] for number in range(1, 26)}
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    for line_number, line in enumerate(lines, start=1):
        match = EVIDENCE_RE.search(line)
        if match is None:
            continue
        event = {
            "status": match.group("status"),
            "phase": match.group("phase"),
            "line": str(line_number),
        }
        by_case[match.group("case")].append(event)
    return by_case, lines


def load_event_stream(path: Path) -> list[tuple[str, str, str]]:
    stream: list[tuple[str, str, str]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
        match = EVIDENCE_RE.search(line)
        if match:
            stream.append((match.group("case"), match.group("status"), match.group("phase")))
    return stream


def validate_corpus(corpus: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if corpus.get("schemaVersion") != 1:
        errors.append("corpus schemaVersion must be 1")
    if corpus.get("gameVersion") != "1.21.4":
        errors.append("corpus gameVersion must be 1.21.4")
    if corpus.get("protocol") != 769:
        errors.append("corpus protocol must be 769")
    fixtures = corpus.get("fixtures")
    if not isinstance(fixtures, list) or len(fixtures) != 25:
        errors.append("corpus must enumerate exactly 25 fixtures")
        return errors
    expected_cases = [f"{number:02d}" for number in range(1, 26)]
    actual_cases = [str(item.get("case", ""))[:2] for item in fixtures if isinstance(item, dict)]
    if actual_cases != expected_cases:
        errors.append(f"fixture case order is not 01..25: {actual_cases}")
    ids = [item.get("id") for item in fixtures if isinstance(item, dict)]
    if len(set(ids)) != len(ids):
        errors.append("fixture mod ids must be unique")
    primary_cases = set(item.get("case") for item in fixtures if isinstance(item, dict))
    auxiliary_cases: set[str] = set()
    for primary in fixtures:
        if not isinstance(primary, dict):
            continue
        specifications = primary.get("auxiliaryMods", [])
        if specifications is None:
            specifications = []
        if not isinstance(specifications, list):
            errors.append(f"fixture {primary.get('case')}: auxiliaryMods must be a list")
            continue
        for specification in specifications:
            if isinstance(specification, str):
                auxiliary_case = specification
            elif isinstance(specification, dict):
                auxiliary_case = specification.get(
                    "case", specification.get("directory", "")
                )
            else:
                auxiliary_case = ""
            if not isinstance(auxiliary_case, str) or not auxiliary_case:
                errors.append(
                    f"fixture {primary.get('case')}: auxiliary mod needs a case/directory"
                )
                continue
            if auxiliary_case in primary_cases or auxiliary_case in auxiliary_cases:
                errors.append(f"duplicate auxiliary fixture directory: {auxiliary_case}")
            auxiliary_cases.add(auxiliary_case)
    required_structured = corpus.get("requiredStructuredBytecodeCases", [])
    declared_structured = {
        item.get("case")
        for item in fixtures
        if isinstance(item, dict) and item.get("requiredStructuredBytecode") is True
    }
    if set(required_structured) != declared_structured:
        errors.append(
            "requiredStructuredBytecodeCases does not match fixture declarations"
        )
    return errors


def manifest_checks(corpus: dict[str, Any], manifest: dict[str, Any]) -> tuple[dict[str, Any], list[str]]:
    errors: list[str] = []
    if manifest.get("manifestVersion") != 1:
        errors.append("generated manifestVersion must be 1")
    if manifest.get("gameVersion") != "1.21.4":
        errors.append("generated manifest gameVersion must be 1.21.4")
    if manifest.get("protocol") != 769:
        errors.append("generated manifest protocol must be 769")

    coverage = manifest.get("methodCoverage")
    if not isinstance(coverage, list):
        errors.append("generated manifest methodCoverage must be a list")
        coverage = []
    by_method = {
        (item.get("class"), item.get("method")): item
        for item in coverage
        if isinstance(item, dict)
    }
    checks = corpus.get("manifestChecks", [])
    check_results: list[dict[str, Any]] = []
    for check in checks:
        if not isinstance(check, dict):
            errors.append("manifestChecks contains a non-object")
            continue
        key = (check.get("class"), check.get("method"))
        actual = by_method.get(key)
        required_levels = check.get("mixinLevels", [])
        result: dict[str, Any] = {
            "class": key[0],
            "method": key[1],
            "requiredMixinLevels": required_levels,
            "requiredStructuredBytecode": bool(check.get("structuredBytecode", False)),
        }
        if actual is None:
            result["status"] = "FAIL"
            result["reason"] = "missing method coverage"
            errors.append(f"manifest missing {key[0]}::{key[1]}")
            check_results.append(result)
            continue
        actual_levels = actual.get("mixinLevels", [])
        missing_levels = [level for level in required_levels if level not in actual_levels]
        structured = actual.get("structuredBytecode") is True
        result.update({
            "actualMixinLevels": actual_levels,
            "structuredBytecode": structured,
            "missingMixinLevels": missing_levels,
        })
        if missing_levels:
            result["status"] = "FAIL"
            result["reason"] = "mixin target level missing"
            errors.append(
                f"manifest {key[0]}::{key[1]} missing mixin levels {','.join(missing_levels)}"
            )
        elif check.get("structuredBytecode") and not structured:
            result["status"] = "FAIL"
            result["reason"] = "structured bytecode coverage is false"
            errors.append(f"manifest {key[0]}::{key[1]} is not structured bytecode")
        else:
            result["status"] = "PASS"
        check_results.append(result)

    mixin = manifest.get("mixin", {})
    if not isinstance(mixin, dict):
        mixin = {}
        errors.append("generated manifest mixin section must be an object")
    supported_points = mixin.get("supportedInjectionPoints", [])
    supported_transformers = mixin.get("supportedTransformers", [])
    for point in corpus.get("requiredMixinPoints", []):
        if point not in supported_points:
            errors.append(f"generated manifest does not support required injection point {point}")
    for transformer in corpus.get("requiredTransformers", []):
        if transformer not in supported_transformers:
            errors.append(f"generated manifest does not support required transformer {transformer}")

    structured_entries = sum(1 for item in coverage if item.get("structuredBytecode") is True)
    metrics = {
        "methodCoverage": len(coverage),
        "nativeBackend": sum(1 for item in coverage if item.get("nativeBackend") is True),
        "wrapperBackend": sum(1 for item in coverage if item.get("wrapperBackend") is True),
        "structuredBytecode": structured_entries,
        "structuredBytecodeRate": (structured_entries / len(coverage)) if coverage else 0.0,
        "requiredMixinPoints": list(corpus.get("requiredMixinPoints", [])),
        "supportedMixinPoints": list(supported_points),
        "requiredTransformers": list(corpus.get("requiredTransformers", [])),
        "supportedTransformers": list(supported_transformers),
        "requiredStructuredBytecodeCases": list(
            corpus.get("requiredStructuredBytecodeCases", [])
        ),
    }
    return {"metrics": metrics, "checks": check_results}, errors


REQUIRED_PHASES: dict[str, list[str]] = {
    "01": ["entrypoint"],
    "02": ["dependency"],
    "03": [
        "server-starting", "world-load-minecraft:overworld", "server-started",
        "packet-buffer", "tick-start-1", "tick-end-1", "command-cancel-callback",
        "command-cancel-return-true", "world-unload",
        "server-stopping", "server-stopped",
    ],
    "04": ["world-state-before"],
    "05": ["empty-entity-boundary"],
    "06": ["registry-identity"],
    "07": ["invoke-getTicks-"],
    "08": ["metadata-resource"],
    "09": ["accessor-runtime"],
    "10": ["invoker-runtime"],
    "11": ["mixin-head"],
    "12": ["mixin-return"],
    "13": ["invoke-handler", "mixin-invoke"],
    "14": ["field-handler", "mixin-field"],
    "15": ["redirect-handler", "mixin-redirect"],
    "16": ["mixin-overwrite-value-42"],
    "17": ["modify-arg-handler", "modify-arg"],
    "18": ["modify-constant-handler", "modify-constant"],
    "19": ["modify-variable-handler", "modify-variable"],
    "20": ["local-capture-handler"],
    "21": ["order-first", "order-second"],
    "22": ["outer-callback", "nested-java-command", "native-reentry-return-true"],
    "23": ["attached-thread-cppfm-corpus-23"],
    "24": ["throwing-callback-entered", "following-callback-recovered"],
    "25": ["wrapper-identity", "corpus-complete"],
}


def validate_evidence(
    by_case: dict[str, list[dict[str, str]]],
    stream: list[tuple[str, str, str]],
    raw_lines: list[str],
) -> tuple[list[dict[str, Any]], list[str]]:
    errors: list[str] = []
    results: list[dict[str, Any]] = []
    for number in range(1, 26):
        case = f"{number:02d}"
        events = by_case[case]
        pass_phases = [event["phase"] for event in events if event["status"] == "PASS"]
        fail_phases = [event["phase"] for event in events if event["status"] == "FAIL"]
        missing = [
            required
            for required in REQUIRED_PHASES[case]
            if not any(phase == required or phase.startswith(required) for phase in pass_phases)
        ]
        case_errors: list[str] = []
        if not pass_phases:
            case_errors.append("no PASS evidence")
        if fail_phases:
            case_errors.append("FAIL evidence: " + ",".join(fail_phases))
        if missing:
            case_errors.append("missing phases: " + ",".join(missing))
        results.append({
            "case": case,
            "status": "PASS" if not case_errors else "FAIL",
            "passPhases": pass_phases,
            "failPhases": fail_phases,
            "missingPhases": missing,
            "errors": case_errors,
        })
        errors.extend(f"case {case}: {message}" for message in case_errors)

    def require_order(case: str, phases: list[str]) -> None:
        positions: list[int] = []
        for phase in phases:
            position = next(
                (index for index, (observed_case, status, observed_phase) in enumerate(stream)
                 if observed_case == case and status == "PASS"
                 and (observed_phase == phase or observed_phase.startswith(phase))),
                None,
            )
            if position is None:
                return
            positions.append(position)
        if positions != sorted(positions) or len(set(positions)) != len(positions):
            errors.append(f"case {case}: callback order mismatch for {' < '.join(phases)}")

    require_order("03", [
        "server-starting", "world-load-minecraft:overworld", "server-started",
        "tick-start-1", "tick-end-1", "command-cancel-callback",
        "command-cancel-return-true", "world-unload", "server-stopping",
        "server-stopped",
    ])
    require_order("21", ["order-first", "order-second"])
    require_order("22", REQUIRED_PHASES["22"])
    require_order("24", REQUIRED_PHASES["24"])
    require_order("25", REQUIRED_PHASES["25"])

    if not any("corpus24 intentional callback failure" in line for line in raw_lines):
        errors.append("case 24: native log lacks callback exception isolation evidence")
    return results, errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--evidence", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        corpus = load_json(args.corpus)
        manifest = load_json(args.manifest)
        by_case, raw_lines = parse_evidence(args.evidence)
        stream = load_event_stream(args.evidence)
        errors = validate_corpus(corpus)
        manifest_report, manifest_errors = manifest_checks(corpus, manifest)
        fixture_report, evidence_errors = validate_evidence(by_case, stream, raw_lines)
        errors.extend(manifest_errors)
        errors.extend(evidence_errors)
        report = {
            "reportVersion": 1,
            "gameVersion": corpus.get("gameVersion"),
            "protocol": corpus.get("protocol"),
            "status": "PASS" if not errors else "FAIL",
            "summary": {
                "fixtureCount": len(fixture_report),
                "fixturePass": sum(1 for item in fixture_report if item["status"] == "PASS"),
                "fixtureFail": sum(1 for item in fixture_report if item["status"] == "FAIL"),
                "errorCount": len(errors),
            },
            "fixtures": fixture_report,
            "manifest": manifest_report,
            "errors": errors,
        }
        rendered = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered, encoding="utf-8")
        print(rendered, end="")
        return 0 if not errors else 1
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"jvm_compatibility_report: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
