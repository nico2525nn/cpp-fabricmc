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


def cpp_function_body(source: str, marker: str) -> str | None:
    """Extract one C++ function body while ignoring braces in comments/strings."""
    start = source.find(marker)
    if start < 0:
        return None
    opening = source.find("{", start)
    if opening < 0:
        return None

    depth = 1
    state = "code"
    escaped = False
    index = opening + 1
    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""
        if state == "line-comment":
            if char == "\n":
                state = "code"
        elif state == "block-comment":
            if char == "*" and next_char == "/":
                state = "code"
                index += 1
        elif state in {"string", "char"}:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif (state == "string" and char == '"') or (
                state == "char" and char == "'"
            ):
                state = "code"
        elif char == "/" and next_char == "/":
            state = "line-comment"
            index += 1
        elif char == "/" and next_char == "*":
            state = "block-comment"
            index += 1
        elif char == '"':
            state = "string"
        elif char == "'":
            state = "char"
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
        index += 1
    return None


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

    runtime_path = root / "src/jvm/JvmRuntime.cpp"
    try:
        runtime_source = runtime_path.read_text(encoding="utf-8")
    except OSError:
        runtime_source = ""
        check(False, f"JVM runtime source missing: {runtime_path}")

    # All selected native-to-Java callbacks made while the server is running
    # must use the synchronous owner-thread dispatcher. Keep the contract tied
    # to the actual dispatch sites instead of letting old concurrency prose
    # silently reappear in the public docs.
    for function in ("bool invokeVoid(", "bool invokeBoolean(",
                     "bool JvmRuntime::onChat(",
                     "bool JvmRuntime::onCommand("):
        body = cpp_function_body(runtime_source, function)
        check(body is not None, f"callback dispatch function not found: {function}")
        if body is None:
            continue
        check("runOnServerThreadDetailed" in body,
              f"{function} no longer dispatches to the server thread")
        check("ServerThreadTaskWaitPolicy::WaitIfRunning" in body,
              f"{function} no longer waits for an accepted callback")

    for operation in ("playerHandleLifetime.activate(",
                      "playerHandleLifetime.publish(",
                      "playerHandleLifetime.beginRetirement(",
                      "playerHandleLifetime.retire(",
                      "playerHandleLifetime.finishRetirement(",
                      "playerHandleLifetime.clear();"):
        check(operation in runtime_source,
              f"Player handle lifecycle operation missing: {operation}")
    check(re.search(
        r"\.registerObject\s*\([^;]*\bHandleKind::Player\b",
        runtime_source, re.DOTALL) is None,
        "JvmRuntime bypasses the Player live-lifetime publication gate")
    join_callback_body = cpp_function_body(
        runtime_source, "void JvmRuntime::onPlayerJoin(")
    quit_callback_body = cpp_function_body(
        runtime_source, "void JvmRuntime::onPlayerQuit(")
    check(join_callback_body is not None and
          0 <= join_callback_body.find("playerHandleLifetime.activate(") <
          join_callback_body.find("invokeVoid("),
          "Player lifecycle must activate before dispatching the join callback")
    check(quit_callback_body is not None and
          0 <= quit_callback_body.find("playerHandleLifetime.beginRetirement(") <
          quit_callback_body.find("invokeVoid(") <
          quit_callback_body.find("invalidatePlayer(player);"),
          "quit callback must receive the old handle before it is invalidated")

    lifetime_path = root / "src/jvm/PlayerHandleLifetime.hpp"
    try:
        lifetime_source = lifetime_path.read_text(encoding="utf-8")
    except OSError:
        lifetime_source = ""
        check(False, f"Player handle lifetime gate missing: {lifetime_path}")
    retire_body = cpp_function_body(
        lifetime_source, "std::optional<std::uint64_t> retire(")
    finish_body = cpp_function_body(
        lifetime_source, "bool finishRetirement(")
    check(retire_body is not None and "State::Retiring" in retire_body and
          "invalidateHandle" in retire_body and "states_.erase" not in retire_body,
          "Player invalidation must retain the retired tombstone")
    check(finish_body is not None and "HandleKind::Player" in finish_body and
          "states_.erase" in finish_body,
          "Player tombstone is released only by final retirement completion")

    player_directory_path = root / "src/game/GameServer_player_directory.cpp"
    try:
        player_directory_source = player_directory_path.read_text(encoding="utf-8")
    except OSError:
        player_directory_source = ""
        check(False, f"Player directory lifecycle source missing: {player_directory_path}")
    removal_at = player_directory_source.find("playerDirectory_.remove(player);")
    finish_after_removal_at = player_directory_source.find(
        "finishPlayerRetirement(*player);", removal_at + 1)
    check(removal_at >= 0 and finish_after_removal_at > removal_at,
          "Player tombstone must outlive directory membership removal")
    replacement_at = player_directory_source.find(
        "const bool added = playerDirectory_.add(")
    victim_finish_at = player_directory_source.find(
        "finishPlayerRetirement(*victim);", replacement_at + 1)
    check(replacement_at >= 0 and victim_finish_at > replacement_at and
          "if (added && jvmRuntime_)" in player_directory_source,
          "duplicate-login tombstone cleanup must follow committed roster replacement")

    handle_test_path = root / "tests/test_jvm_handles.cpp"
    try:
        handle_test_source = handle_test_path.read_text(encoding="utf-8")
    except OSError:
        handle_test_source = ""
        check(False, f"Player handle lifecycle regression test missing: {handle_test_path}")
    for expected in (
        "quit callback retains a valid handle after inPlay becomes false",
        "retired tombstone blocks stale join activation before roster removal",
        "directory removal permits final tombstone cleanup",
        "wrapper's initial handle check passes before retirement",
        "wrapper cannot hit cached object after handle retirement linearizes",
        "Player::inPlay must be atomic across session/JVM threads",
    ):
        check(expected in handle_test_source,
              f"Player handle retirement-order regression missing: {expected}")

    player_header_path = root / "src/game/Player.hpp"
    try:
        player_header_source = player_header_path.read_text(encoding="utf-8")
    except OSError:
        player_header_source = ""
        check(False, f"Player definition missing: {player_header_path}")
    check("#include <atomic>" in player_header_source and
          re.search(r"std::atomic\s*<\s*bool\s*>\s+inPlay", player_header_source),
          "Player::inPlay must use atomic storage across lifecycle threads")

    cache_transaction_path = root / "src/jvm/HandleCacheTransaction.hpp"
    try:
        cache_transaction_source = cache_transaction_path.read_text(encoding="utf-8")
    except OSError:
        cache_transaction_source = ""
        check(False, f"handle/cache transaction helper missing: {cache_transaction_path}")
    check("std::lock_guard cacheLock(objectCacheMutex);" in cache_transaction_source and
          "auto handle = std::forward<Invalidate>(invalidate)();" in cache_transaction_source and
          "std::forward<Erase>(erase)(*handle);" in cache_transaction_source,
          "handle retirement and cached-wrapper erasure must share objectCacheMutex")

    invalidate_player_body = cpp_function_body(
        runtime_source, "void JvmRuntime::invalidatePlayer(")
    check(invalidate_player_body is not None and
          invalidate_player_body.find("RuntimeCallLease lease(this);") <
          invalidate_player_body.find("invalidateHandleAndEraseCache(") and
          "eraseCachedObjectHandle" in invalidate_player_body,
          "Player invalidation must acquire its runtime lease before the cache transaction")

    wrapper_body = cpp_function_body(runtime_source, "jobject wrapperForHandle(")
    check(wrapper_body is not None, "wrapperForHandle implementation missing")
    if wrapper_body is not None:
        cache_lookup_at = wrapper_body.find("impl.objects.with(handle, internalName")
        cache_lookup_check_at = wrapper_body.rfind(
            "if (!impl.handles.valid(handle))", 0, cache_lookup_at)
        cache_insert_at = wrapper_body.find("impl.objects.put(env, handle, internalName, global)")
        cache_insert_check_at = wrapper_body.rfind(
            "if (!impl.handles.valid(handle))", 0, cache_insert_at)
        cache_lock_before_insert = wrapper_body.rfind(
            "std::lock_guard cacheLock(impl.objectCacheMutex);", 0, cache_insert_at)
        check(cache_lookup_at >= 0 and cache_lookup_check_at >= 0,
              "cached wrapper lookup must revalidate its handle under objectCacheMutex")
        check(cache_insert_at >= 0 and cache_insert_check_at >= 0 and
              cache_lock_before_insert >= 0 and
              cache_lock_before_insert < cache_insert_check_at < cache_insert_at,
              "wrapper insertion must keep handle validation and cache put in one transaction")

    retirement_sources = {
        "src/game/GameServer_session.cpp": (
            "runtime->beginPlayerRetirement(*self_)",
            "self_->inPlay = false",
        ),
        "src/game/GameServer_player_directory.cpp": (
            "beginPlayerRetirement(*victim)",
            "victim->inPlay = false",
        ),
        "src/game/GameServer_session.cpp#duplicate-handoff": (
            "runtime->beginPlayerRetirement(*player)",
            "player->inPlay = false",
        ),
    }
    for relative, (close_gate, mark_inactive) in retirement_sources.items():
        path = root / relative.split("#", maxsplit=1)[0]
        try:
            text = path.read_text(encoding="utf-8")
        except OSError:
            check(False, f"player retirement source missing: {path}")
            continue
        gate_at = text.find(close_gate)
        inactive_at = text.find(mark_inactive)
        check(gate_at >= 0 and inactive_at >= 0 and gate_at < inactive_at,
              f"Player handle publication must close before inactive transition: {relative}")

    stale_overlap_claim = re.compile(
        r"(?:\bsession(?:-originated)?\b.{0,120}\b(?:may|can)\s+overlap\b.{0,80}\btick\b"
        r"|\bcallbacks?\b.{0,100}\b(?:may|can)\s+overlap\b.{0,80}\b(?:across|between)\b.{0,40}\b(?:session|tick)\b)",
        re.IGNORECASE | re.DOTALL,
    )
    for relative in ("README.md", "docs/README.md", "docs/DEVELOPMENT.md",
                     "docs/CURRENT_STATE.md", "docs/MISSING_FEATURES_1_21_4.md"):
        path = root / relative
        try:
            text = path.read_text(encoding="utf-8")
        except OSError:
            check(False, f"callback threading contract document missing: {path}")
            continue
        check(stale_overlap_claim.search(text) is None,
              f"callback threading docs claim overlapping session/tick callbacks: {path}")

    if failures:
        print(f"PLAN51 CONTRACT AUDIT: FAIL ({len(failures)} failures, {passed} checks)")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print(f"PLAN51 CONTRACT AUDIT: PASS ({passed} checks; {structured} structured/{len(coverage or [])} methods, {native} native/{wrapper} wrapper)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
