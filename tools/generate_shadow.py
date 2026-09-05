#!/usr/bin/env python3
"""Validate the declarative JVM shadow ABI and emit a stable manifest."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


def load_spec(path: Path) -> dict:
    with path.open(encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError("shadow spec must be an object")
    if value.get("gameVersion") != "1.21.4":
        raise ValueError("shadow spec must target Minecraft 1.21.4")
    if value.get("protocol") != 769:
        raise ValueError("shadow spec must target protocol 769")
    classes = value.get("classes")
    if not isinstance(classes, list) or not classes:
        raise ValueError("shadow spec needs at least one class")
    names: set[str] = set()
    for entry in classes:
        if not isinstance(entry, dict) or not isinstance(entry.get("name"), str):
            raise ValueError("every class entry needs a name")
        name = entry["name"]
        if name in names:
            raise ValueError(f"duplicate shadow class: {name}")
        names.add(name)
        methods = entry.get("methods", [])
        if not isinstance(methods, list) or not all(isinstance(item, str) for item in methods):
            raise ValueError(f"invalid method list for {name}")
        mixin_levels = entry.get("mixinLevels", {})
        if not isinstance(mixin_levels, dict):
            raise ValueError(f"invalid mixin level map for {name}")
        for method, levels in mixin_levels.items():
            if method not in methods or not isinstance(levels, list) or not all(
                    isinstance(level, str) for level in levels):
                raise ValueError(f"invalid mixin levels for {name}::{method}")
        structured_methods = entry.get("structuredBytecodeMethods", [])
        if not isinstance(structured_methods, list) or not all(
                isinstance(item, str) and item in methods for item in structured_methods):
            raise ValueError(f"invalid structured bytecode method list for {name}")
    return value


def render(spec: dict) -> str:
    mixin = spec.get("mixin", {})
    if not isinstance(mixin, dict):
        raise ValueError("mixin section must be an object")
    execution = mixin.get("execution", "abi-only")
    method_coverage = []
    for entry in sorted(spec["classes"], key=lambda item: item["name"]):
        structured_methods = set(entry.get("structuredBytecodeMethods", []))
        for method in entry.get("methods", []):
            structured = method in structured_methods
            method_coverage.append({
                "class": entry["name"],
                "method": method,
                "abi": True,
                "nativeBackend": True,
                "structuredBytecode": structured,
                "structuredBytecodeMode": execution if structured else "abi-only",
                "mixinLevels": entry.get("mixinLevels", {}).get(method, []),
            })
    manifest = {
        "manifestVersion": 1,
        "gameVersion": spec["gameVersion"],
        "protocol": spec["protocol"],
        "dataVersion": spec.get("dataVersion"),
        "backend": "cppfm-embedded-jni",
        "classes": sorted(spec["classes"], key=lambda item: item["name"]),
        "methodCoverage": method_coverage,
        "events": sorted(spec.get("events", [])),
        "mixin": mixin,
        "limitations": list(spec.get("limitations", [])),
    }
    return json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=False) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        expected = render(load_spec(args.input))
        if args.check:
            actual = args.output.read_text(encoding="utf-8")
            if actual != expected:
                raise ValueError(f"manifest is stale: {args.output}")
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(expected, encoding="utf-8")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"generate_shadow: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
