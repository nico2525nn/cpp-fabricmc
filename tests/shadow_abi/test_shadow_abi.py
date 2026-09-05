#!/usr/bin/env python3
"""Compile and inspect the version-locked Java Shadow ABI.

This test intentionally builds the shadow sources and the existing JVM fixture
sources together.  It then generates a class-file-backed manifest in a
temporary directory and exercises reflection, MethodHandle lookup, canonical
package names, constructors, fields, inheritance, interfaces, and constants.
Nothing produced by the test is written to the repository.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


def run(command: list[str], *, timeout: int) -> subprocess.CompletedProcess[str]:
    try:
        result = subprocess.run(command, text=True, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        raise AssertionError(f"timed out after {timeout}s: {' '.join(command)}") from error
    if result.returncode != 0:
        detail = (result.stdout + result.stderr).strip()
        raise AssertionError(f"command failed ({result.returncode}): {' '.join(command)}\n{detail}")
    return result


def fail(message: str) -> None:
    raise AssertionError(message)


def member_map(item: dict[str, Any], key: str) -> dict[tuple[str, str], dict[str, Any]]:
    return {(entry["name"], entry["descriptor"]): entry for entry in item[key]}


def resolve_method(
    name: str,
    descriptor: str,
    class_name: str,
    classes: dict[str, dict[str, Any]],
    seen: set[str] | None = None,
) -> dict[str, Any] | None:
    seen = set() if seen is None else seen
    if class_name in seen:
        return None
    seen.add(class_name)
    item = classes.get(class_name)
    if item is None:
        return None
    found = member_map(item, "methods").get((name, descriptor))
    if found is not None:
        return found
    for parent in [item.get("superClass"), *item.get("interfaces", [])]:
        if parent is not None:
            found = resolve_method(name, descriptor, parent, classes, seen)
            if found is not None:
                return found
    return None


def compile_sources(repo: Path, output: Path, fixture: Path) -> int:
    shadow_sources = sorted((repo / "jvm" / "java").rglob("*.java"))
    fixture_sources = sorted((repo / "tests" / "jvm_fixture").rglob("*.java"))
    sources = [str(path) for path in (*shadow_sources, *fixture_sources, fixture)]
    if not shadow_sources:
        fail("jvm/java contains no Java sources")
    run(["javac", "--release", "17", "-d", str(output), *sources], timeout=180)
    return len(shadow_sources)


def check_manifest(repo: Path, manifest_path: Path, classes_dir: Path) -> tuple[int, int, int]:
    spec_path = repo / "jvm" / "shadow_api.json"
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if spec.get("protocol") != 769 or manifest.get("protocol") != 769:
        fail("shadow ABI must remain protocol 769")
    if spec.get("gameVersion") != "1.21.4" or manifest.get("gameVersion") != "1.21.4":
        fail("shadow ABI must target Minecraft 1.21.4")
    mapping = manifest.get("mapping")
    if mapping != {
        "runtimeNamespace": "named",
        "supportedNamespaces": ["named"],
        "remapping": "identity-only",
        "version": "1.21.4",
    }:
        fail(f"mapping is not the declared named-only contract: {mapping!r}")

    inventory = manifest.get("sourceInventory", {})
    source_classes = {entry.get("class") for entry in inventory.get("classes", [])}
    if inventory.get("classCount") != len(source_classes) or not source_classes:
        fail("source inventory is missing or contains duplicate classes")

    audit = manifest.get("abiSurface", {}).get("classFileAudit", {})
    if audit.get("status") != "verified":
        fail(f"class-file audit did not run: {audit.get('status')!r}")
    audited = audit.get("classes", [])
    classes = {entry.get("name"): entry for entry in audited}
    if len(classes) != len(audited) or not classes:
        fail("class-file audit contains duplicate or no classes")
    if audit.get("classCount") != len(classes):
        fail("class-file audit class count is stale")

    member_count = constructor_count = field_count = method_count = 0
    for class_name, item in classes.items():
        for key in ("constructors", "fields", "methods", "interfaces"):
            if key not in item:
                fail(f"{class_name} lacks {key} ABI data")
        relative = item.get("classFile")
        class_file = classes_dir / relative
        if not class_file.is_file():
            fail(f"missing class file for {class_name}: {relative}")
        digest = hashlib.sha256(class_file.read_bytes()).hexdigest()
        if digest != item.get("classFileSha256"):
            fail(f"stale class-file hash for {class_name}")
        for key in ("constructors", "fields", "methods"):
            for member in item[key]:
                if not member.get("descriptor") or member.get("visibility") not in {
                    "public", "protected", "package", "private"
                }:
                    fail(f"incomplete {key[:-1]} metadata in {class_name}: {member}")
                if member.get("dispatch") != ("static" if member.get("static") else "instance"):
                    fail(f"static/instance dispatch mismatch in {class_name}: {member}")
        fields = member_map(item, "fields")
        for enum_constant in item.get("enumConstants", []):
            if (enum_constant, next((field["descriptor"] for field in item["fields"]
                                     if field["name"] == enum_constant), "")) not in fields:
                fail(f"enum constant is absent from field surface: {class_name}.{enum_constant}")
        constructor_count += len(item["constructors"])
        field_count += len(item["fields"])
        method_count += len(item["methods"])
        member_count += len(item["constructors"]) + len(item["fields"]) + len(item["methods"])

    required = spec.get("abiAudit", {}).get("requiredClasses", [])
    missing = [name for name in required if name not in classes]
    if missing:
        fail("required classes absent from class-file audit: " + ", ".join(missing))

    for entry in spec.get("classes", []):
        class_name = entry["name"]
        if class_name not in classes:
            fail(f"manifest class entry has no compiled class: {class_name}")
        for selector in entry.get("methods", []):
            name, descriptor = selector.split("(", 1)
            descriptor = "(" + descriptor
            if resolve_method(name, descriptor, class_name, classes) is None:
                fail(f"manifest method is absent from declared/inherited ABI: {class_name}::{selector}")

    checks = {
        "net.minecraft.block.AbstractBlock": (False, "java.lang.Object"),
        "net.minecraft.block.Block": (False, "net.minecraft.block.AbstractBlock"),
        "net.minecraft.registry.entry.RegistryEntry": (True, None),
        "net.minecraft.world.World": (False, None),
    }
    for name, (is_interface, parent) in checks.items():
        item = classes[name]
        if item.get("interface") is not is_interface:
            fail(f"interface flag mismatch for {name}")
        if parent is not None and item.get("superClass") != parent:
            fail(f"superclass mismatch for {name}: {item.get('superClass')!r}")
    world_interfaces = set(classes["net.minecraft.world.World"].get("interfaces", []))
    if world_interfaces != {
        "net.minecraft.world.BlockView", "net.minecraft.world.WorldView", "net.minecraft.world.WorldAccess"
    }:
        fail(f"World interface set mismatch: {world_interfaces!r}")

    abstract_fields = member_map(classes["net.minecraft.block.AbstractBlock"], "fields")
    settings = abstract_fields.get(("settings", "Lnet/minecraft/block/AbstractBlock$Settings;"))
    if settings is None or settings["visibility"] != "protected" or settings["static"]:
        fail("AbstractBlock.settings is not protected instance ABI")
    inventory_fields = member_map(classes["net.minecraft.entity.player.PlayerInventory"], "fields")
    selected_slot = inventory_fields.get(("selectedSlot", "I"))
    if selected_slot is None or selected_slot["visibility"] != "public" or selected_slot["static"]:
        fail("PlayerInventory.selectedSlot ABI mismatch")

    enum_classes = [
        "net.minecraft.util.Formatting", "net.minecraft.util.Rarity",
        "net.minecraft.item.consume.UseAction", "net.minecraft.block.piston.PistonBehavior",
        "net.minecraft.world.border.WorldBorderStage",
    ]
    for name in enum_classes:
        item = classes[name]
        if not item.get("enum") or not item.get("enumConstants"):
            fail(f"enum/constants surface missing for {name}")

    return len(classes), member_count, constructor_count + field_count + method_count


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    repo = args.repo.resolve()
    generator = repo / "tools" / "generate_shadow.py"
    spec = repo / "jvm" / "shadow_api.json"
    fixture = repo / "tests" / "shadow_abi" / "ShadowAbiFixture.java"
    with tempfile.TemporaryDirectory(prefix="cppfm-shadow-abi-") as temporary:
        temporary_path = Path(temporary)
        classes_dir = temporary_path / "classes"
        manifest = temporary_path / "manifest.json"
        classes_dir.mkdir()
        source_count = compile_sources(repo, classes_dir, fixture)
        run([
            sys.executable, str(generator), "--input", str(spec), "--output", str(manifest),
            "--classes-dir", str(classes_dir),
        ], timeout=30)
        class_count, member_count, declared_count = check_manifest(repo, manifest, classes_dir)
        run(["java", "-cp", str(classes_dir), "cppfm.shadowabi.ShadowAbiFixture"], timeout=30)
    print(
        f"SHADOW ABI PASS: sourceClasses={source_count} classFiles={class_count} "
        f"declaredMembers={declared_count} auditedMembers={member_count}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, json.JSONDecodeError) as error:
        print(f"shadow_abi: {error}", file=sys.stderr)
        raise SystemExit(1)
