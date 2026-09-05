#!/usr/bin/env python3
"""Audit the machine-readable plan51 contract without inventing runtime PASS.

This gate checks version boundaries, corpus inventory, manifest declarations, and
the presence of all implementation phases.  Dynamic behavior is intentionally
left to the JVM/native/official-loader test gates; a successful audit is not a
claim of arbitrary Fabric-mod compatibility.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


EXPECTED_CASES = [
    "01-loader-entrypoint",
    "02-dependency",
    "03-fabric-api-event",
    "04-world-api",
    "05-entity-api",
    "06-registry-api",
    "07-reflection",
    "08-access-widener",
    "09-accessor-mixin",
    "10-invoker-mixin",
    "11-inject-head",
    "12-inject-return",
    "13-inject-invoke",
    "14-inject-field",
    "15-redirect",
    "16-overwrite",
    "17-modify-arg",
    "18-modify-constant",
    "19-modify-variable",
    "20-local-capture",
    "21-two-mod-transform-order",
    "22-reentrant-callback",
    "23-threading",
    "24-exception",
    "25-object-identity",
]


def load_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"cannot read JSON {path}: {exc}") from exc


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--corpus", type=Path)
    parser.add_argument("--plan", type=Path)
    args = parser.parse_args()

    root = args.root.resolve()
    manifest_path = (args.manifest or root / "build/jvm/compatibility-manifest.json").resolve()
    corpus_path = (args.corpus or root / "tests/jvm_fixture/corpus/corpus.json").resolve()
    plan_path = (args.plan or root / "plan/plan51.md").resolve()

    failures: list[str] = []
    passed = 0

    def check(condition: bool, message: str) -> None:
        nonlocal passed
        if condition:
            passed += 1
        else:
            failures.append(message)

    manifest = load_json(manifest_path)
    corpus = load_json(corpus_path)
    try:
        plan_text = plan_path.read_text(encoding="utf-8")
    except OSError as exc:
        if args.plan:
            raise SystemExit(f"cannot read plan {plan_path}: {exc}") from exc
        plan_text = ""

    check(manifest.get("manifestVersion") == 1, "manifestVersion must be 1")
    check(manifest.get("gameVersion") == "1.21.4", "manifest gameVersion must be 1.21.4")
    check(manifest.get("protocol") == 769, "manifest protocol must be 769")
    check(manifest.get("dataVersion") == 4189, "manifest dataVersion must be 4189")

    fixtures = corpus.get("fixtures") if isinstance(corpus, dict) else None
    check(isinstance(fixtures, list) and len(fixtures) == 25, "corpus must contain exactly 25 fixtures")
    case_names = [item.get("case") for item in fixtures or [] if isinstance(item, dict)]
    check(case_names == EXPECTED_CASES, "corpus case order/inventory differs from plan51 §26")
    check(len({item.get("id") for item in fixtures or [] if isinstance(item, dict)}) == 25,
          "corpus fixture ids must be unique")
    for case in EXPECTED_CASES:
        case_dir = root / "tests/jvm_fixture/corpus" / case
        check((case_dir / "fabric.mod.json").is_file(),
              f"corpus metadata is missing for {case}")
        check(any(case_dir.rglob("*.java")),
              f"corpus source is missing for {case}")

    manifest_checks = corpus.get("manifestChecks") if isinstance(corpus, dict) else None
    coverage = manifest.get("methodCoverage")
    check(isinstance(manifest_checks, list) and isinstance(coverage, list),
          "corpus manifestChecks and generated methodCoverage are required")
    coverage_keys = {
        (item.get("class"), item.get("method")): item
        for item in coverage or [] if isinstance(item, dict)
    }
    for item in manifest_checks or []:
        if not isinstance(item, dict):
            check(False, "manifestChecks contains a non-object")
            continue
        observed = coverage_keys.get((item.get("class"), item.get("method")))
        check(observed is not None,
              f"manifest is missing {item.get('class')}#{item.get('method')}")
        if observed is None:
            continue
        if item.get("structuredBytecode") is True:
            check(observed.get("structuredBytecode") is True,
                  f"structured bytecode missing for {item.get('class')}#{item.get('method')}")
        required_points = set(item.get("mixinLevels", []))
        actual_points = set(observed.get("mixinLevels", []))
        check(required_points <= actual_points,
              f"mixin point coverage missing for {item.get('class')}#{item.get('method')}")

    metrics = manifest.get("mixin", {})
    required_points = set(corpus.get("requiredMixinPoints", []))
    required_transformers = set(corpus.get("requiredTransformers", []))
    check(required_points <= set(metrics.get("supportedInjectionPoints", [])),
          "required injection point is absent from manifest")
    check(required_transformers <= set(metrics.get("supportedTransformers", [])),
          "required transformer is absent from manifest")
    check(metrics.get("declaredButNotTransformed") == [],
          "manifest declares a transformer/method that is not transformed")

    structured = sum(1 for item in coverage or []
                     if isinstance(item, dict) and item.get("structuredBytecode"))
    native = sum(1 for item in coverage or []
                 if isinstance(item, dict) and item.get("nativeBackend"))
    wrapper = sum(1 for item in coverage or []
                  if isinstance(item, dict) and item.get("wrapperBackend"))
    backend_complete = all(
        isinstance(item, dict)
        and item.get("backend") in {"native", "wrapper"}
        and bool(item.get("nativeBackend")) != bool(item.get("wrapperBackend"))
        for item in coverage or []
    )
    check(backend_complete,
          "every declared method must have exactly one native or wrapper backend")
    if coverage and "structuredBytecodeRate" in manifest:
        declared_rate = manifest.get("structuredBytecodeRate")
        actual_rate = structured / len(coverage)
        check(isinstance(declared_rate, (int, float)) and
              abs(float(declared_rate) - actual_rate) < 1e-9,
              "manifest structuredBytecodeRate is inconsistent with methodCoverage")
    if plan_text:
        for phase in range(8):
            check(re.search(rf"^#{{2,4}}\s+Phase {phase}(?:\s|—|-|$)", plan_text, re.MULTILINE) is not None,
                  f"plan51 is missing Phase {phase}")

    active_files = [
        root / "README.md",
        root / "CMakeLists.txt",
        root / "jvm/shadow_api.json",
        root / "docs/CURRENT_STATE.md",
        root / "docs/PLAN51_JVM.md",
    ]
    version_776 = re.compile(r"protocol(?:\s|[-_:])+776\b", re.IGNORECASE)
    for path in active_files:
        try:
            text = path.read_text(encoding="utf-8")
        except OSError:
            check(False, f"active contract file missing: {path}")
            continue
        check(version_776.search(text) is None,
              f"active contract contains an unintended protocol 776 reference: {path}")

    if failures:
        print(f"PLAN51 CONTRACT AUDIT: FAIL ({len(failures)} failures, {passed} checks)")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print(f"PLAN51 CONTRACT AUDIT: PASS ({passed} checks; {structured} structured/{len(coverage or [])} methods, {native} native/{wrapper} wrapper)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
