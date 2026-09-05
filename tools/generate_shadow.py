#!/usr/bin/env python3
"""Validate the JVM shadow ABI and emit a manifest backed by class files.

The small ``classes`` section in :file:`jvm/shadow_api.json` is intentionally
the compatibility/routing contract for methods which have a native backend.
It is not, however, a complete description of the Java ABI.  A mod can observe
constructors, private fields, nested types, enum constants, and class
modifiers through reflection, access wideners, or mixins.  When
``--classes-dir`` is supplied this generator therefore records the *declared*
class-file surface for every shadow class.  The parser is deliberately local
and dependency-free so the audit does not need ASM or Mojang bytecode.

Without ``--classes-dir`` the manifest still records the source inventory.  The
normal native-only build can consequently generate the manifest before javac,
while the shadow ABI test can require the stronger class-file audit.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any


ACC_PUBLIC = 0x0001
ACC_PRIVATE = 0x0002
ACC_PROTECTED = 0x0004
ACC_STATIC = 0x0008
ACC_FINAL = 0x0010
ACC_INTERFACE = 0x0200
ACC_ABSTRACT = 0x0400
ACC_SYNTHETIC = 0x1000
ACC_ANNOTATION = 0x2000
ACC_ENUM = 0x4000
ACC_RECORD = 0x10000
ACC_BRIDGE = 0x0040
ACC_VARARGS = 0x0080
ACC_NATIVE = 0x0100


def _visibility(access: int) -> str:
    if access & ACC_PUBLIC:
        return "public"
    if access & ACC_PROTECTED:
        return "protected"
    if access & ACC_PRIVATE:
        return "private"
    return "package"


class _Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def read(self, size: int) -> bytes:
        end = self.offset + size
        if size < 0 or end > len(self.data):
            raise ValueError("truncated class file")
        value = self.data[self.offset:end]
        self.offset = end
        return value

    def u1(self) -> int:
        return self.read(1)[0]

    def u2(self) -> int:
        return int.from_bytes(self.read(2), "big")

    def u4(self) -> int:
        return int.from_bytes(self.read(4), "big")


def _cp_utf8(pool: list[Any], index: int) -> str:
    if index <= 0 or index >= len(pool) or pool[index][0] != "Utf8":
        raise ValueError(f"invalid UTF-8 constant-pool index {index}")
    return pool[index][1]


def _cp_class(pool: list[Any], index: int) -> str:
    if index <= 0 or index >= len(pool) or pool[index][0] != "Class":
        raise ValueError(f"invalid class constant-pool index {index}")
    return _cp_utf8(pool, pool[index][1]).replace("/", ".")


def _attributes(reader: _Reader, pool: list[Any]) -> dict[str, bytes]:
    attributes: dict[str, bytes] = {}
    for _ in range(reader.u2()):
        name = _cp_utf8(pool, reader.u2())
        attributes[name] = reader.read(reader.u4())
    return attributes


def _member(access: int, name: str, descriptor: str, *, kind: str,
            attributes: dict[str, bytes]) -> dict[str, Any]:
    return {
        "name": name,
        "descriptor": descriptor,
        "visibility": _visibility(access),
        "static": bool(access & ACC_STATIC),
        "dispatch": "static" if access & ACC_STATIC else "instance",
        "final": bool(access & ACC_FINAL),
        "abstract": bool(access & ACC_ABSTRACT),
        "native": bool(access & ACC_NATIVE),
        "synthetic": bool(access & ACC_SYNTHETIC or "Synthetic" in attributes),
        "bridge": bool(access & ACC_BRIDGE),
        "varargs": bool(access & ACC_VARARGS),
        "kind": kind,
    }


def _parse_classfile(data: bytes, relative_path: str) -> dict[str, Any]:
    reader = _Reader(data)
    if reader.u4() != 0xCAFEBABE:
        raise ValueError(f"invalid class-file magic: {relative_path}")
    reader.u2()  # minor
    reader.u2()  # major
    pool: list[Any] = [None]
    constant_pool_count = reader.u2()
    pool_index = 1
    while pool_index < constant_pool_count:
        tag = reader.u1()
        if tag == 1:  # CONSTANT_Utf8
            pool.append(("Utf8", reader.read(reader.u2()).decode("utf-8")))
        elif tag in (3, 4):  # Integer/Float
            reader.read(4)
            pool.append(("Scalar", None))
        elif tag in (5, 6):  # Long/Double occupy two entries
            reader.read(8)
            pool.extend([("Scalar", None), None])
            pool_index += 2
            continue
        elif tag == 7:  # Class
            pool.append(("Class", reader.u2()))
        elif tag == 8:  # String
            reader.read(2)
            pool.append(("Scalar", None))
        elif tag in (9, 10, 11, 12, 17, 18):
            reader.read(4)
            pool.append(("Scalar", None))
        elif tag in (15,):  # MethodHandle
            reader.read(3)
            pool.append(("Scalar", None))
        elif tag in (16, 19, 20):  # MethodType/Module/Package
            reader.read(2)
            pool.append(("Scalar", None))
        else:
            raise ValueError(f"unsupported constant-pool tag {tag} in {relative_path}")
        pool_index += 1

    access = reader.u2()
    this_class = _cp_class(pool, reader.u2())
    super_index = reader.u2()
    super_class = None if super_index == 0 else _cp_class(pool, super_index)
    interfaces = [_cp_class(pool, reader.u2()) for _ in range(reader.u2())]

    fields: list[dict[str, Any]] = []
    for _ in range(reader.u2()):
        field_access = reader.u2()
        name = _cp_utf8(pool, reader.u2())
        descriptor = _cp_utf8(pool, reader.u2())
        attrs = _attributes(reader, pool)
        fields.append(_member(field_access, name, descriptor, kind="field",
                              attributes=attrs))

    constructors: list[dict[str, Any]] = []
    methods: list[dict[str, Any]] = []
    for _ in range(reader.u2()):
        method_access = reader.u2()
        name = _cp_utf8(pool, reader.u2())
        descriptor = _cp_utf8(pool, reader.u2())
        attrs = _attributes(reader, pool)
        item = _member(method_access, name, descriptor, kind="method",
                       attributes=attrs)
        if name == "<init>":
            item["kind"] = "constructor"
            constructors.append(item)
        elif name != "<clinit>":
            methods.append(item)

    _attributes(reader, pool)  # class attributes; intentionally not surfaced
    enum_constants = [
        field["name"] for field in fields
        if access & ACC_ENUM and field["static"] and field["descriptor"] == f"L{this_class.replace('.', '/')};"
    ]
    return {
        "name": this_class,
        "classFile": relative_path,
        "classFileSha256": hashlib.sha256(data).hexdigest(),
        "visibility": _visibility(access),
        "abstract": bool(access & ACC_ABSTRACT),
        "final": bool(access & ACC_FINAL),
        "interface": bool(access & ACC_INTERFACE),
        "annotation": bool(access & ACC_ANNOTATION),
        "enum": bool(access & ACC_ENUM),
        "record": bool(access & ACC_RECORD),
        "synthetic": bool(access & ACC_SYNTHETIC),
        "superClass": super_class,
        "interfaces": interfaces,
        "constructors": constructors,
        "fields": fields,
        "methods": methods,
        "enumConstants": enum_constants,
    }


def _package_allowed(package: str, prefixes: list[str]) -> bool:
    return any(package == prefix or package.startswith(prefix + ".") for prefix in prefixes)


def _source_inventory(spec: dict, spec_path: Path | None) -> list[dict[str, str]]:
    root_name = spec.get("sourceRoot", "java")
    if not isinstance(root_name, str):
        raise ValueError("sourceRoot must be a string")
    prefixes = spec.get("shadowPackages", ["net.minecraft", "net.fabricmc.loader.api"])
    if not isinstance(prefixes, list) or not all(isinstance(item, str) for item in prefixes):
        raise ValueError("shadowPackages must be a list of strings")
    if spec_path is None:
        return []
    root = (spec_path.parent / root_name).resolve()
    if not root.is_dir():
        return []
    inventory: list[dict[str, str]] = []
    package_re = re.compile(r"\bpackage\s+([A-Za-z_][\w.]*)\s*;")
    for source in sorted(root.rglob("*.java")):
        text = source.read_text(encoding="utf-8")
        match = package_re.search(text)
        if match is None or not _package_allowed(match.group(1), prefixes):
            continue
        class_name = source.stem
        # Every current shadow source has a file-named top-level type.  Refuse
        # to silently invent an inventory entry if that invariant changes.
        declaration = re.search(
            rf"\b(?:class|interface|enum|record)\s+{re.escape(class_name)}\b", text)
        if declaration is None:
            raise ValueError(f"source has no file-named top-level type: {source}")
        inventory.append({
            "class": f"{match.group(1)}.{class_name}",
            "sourceFile": source.relative_to(root).as_posix(),
        })
    return inventory


def _classfile_surface(spec: dict, classes_dir: Path | None) -> dict[str, Any]:
    prefixes = spec.get("shadowPackages", ["net.minecraft", "net.fabricmc.loader.api"])
    if classes_dir is None:
        return {
            "status": "not-run",
            "classCount": 0,
            "classes": [],
            "note": "run generate_shadow.py with --classes-dir for the declared class-file audit",
        }
    root = classes_dir.resolve()
    if not root.is_dir():
        raise ValueError(f"classes directory is missing: {root}")
    classes: list[dict[str, Any]] = []
    prefix_paths = tuple(prefix.replace(".", "/") for prefix in prefixes)
    for class_file in sorted(root.rglob("*.class")):
        relative = class_file.relative_to(root).as_posix()
        if not any(relative.startswith(prefix + "/") for prefix in prefix_paths):
            continue
        classes.append(_parse_classfile(class_file.read_bytes(), relative))
    if not classes:
        raise ValueError(f"classes directory contains no configured shadow classes: {root}")
    return {
        "status": "verified",
        "classCount": len(classes),
        "classes": sorted(classes, key=lambda item: item["name"]),
    }


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
        native_methods = entry.get("nativeMethods", methods)
        if not isinstance(native_methods, list) or not all(
                isinstance(item, str) and item in methods for item in native_methods):
            raise ValueError(f"invalid native method list for {name}")
        wrapper_methods = entry.get("wrapperMethods", [])
        if not isinstance(wrapper_methods, list) or not all(
                isinstance(item, str) and item in methods for item in wrapper_methods):
            raise ValueError(f"invalid wrapper method list for {name}")
        if set(native_methods) & set(wrapper_methods):
            raise ValueError(f"native and wrapper method lists overlap for {name}")
    value["_specPath"] = path.resolve()
    return value


def render(spec: dict, *, classes_dir: Path | None = None) -> str:
    mixin = spec.get("mixin", {})
    if not isinstance(mixin, dict):
        raise ValueError("mixin section must be an object")
    execution = mixin.get("execution", "abi-only")
    method_coverage = []
    for entry in sorted(spec["classes"], key=lambda item: item["name"]):
        structured_methods = set(entry.get("structuredBytecodeMethods", []))
        native_methods = set(entry.get("nativeMethods", entry.get("methods", [])))
        wrapper_methods = set(entry.get("wrapperMethods", []))
        for method in entry.get("methods", []):
            structured = method in structured_methods
            native_backend = method in native_methods
            wrapper_backend = method in wrapper_methods
            method_coverage.append({
                "class": entry["name"],
                "method": method,
                "abi": True,
                "nativeBackend": native_backend,
                "wrapperBackend": wrapper_backend,
                "backend": "native" if native_backend else "wrapper" if wrapper_backend else "none",
                "structuredBytecode": structured,
                "structuredBytecodeMode": execution if structured else "abi-only",
                "mixinLevels": entry.get("mixinLevels", {}).get(method, []),
            })
    spec_path = spec.get("_specPath")
    if not isinstance(spec_path, Path):
        spec_path = None
    source_inventory = _source_inventory(spec, spec_path)
    classfile_surface = _classfile_surface(spec, classes_dir)
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
        "mapping": spec.get("mapping", {
            "runtimeNamespace": "named",
            "supportedNamespaces": ["named"],
            "remapping": "identity-only",
        }),
        "sourceInventory": {
            "sourceRoot": spec.get("sourceRoot", "java"),
            "packages": list(spec.get("shadowPackages", [])),
            "classCount": len(source_inventory),
            "classes": source_inventory,
        },
        "abiSurface": {
            "auditVersion": 1,
            "scope": "declared-class-file-members",
            "sourceClassCount": len(source_inventory),
            "classFileAudit": classfile_surface,
        },
    }
    return json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=False) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument(
        "--classes-dir", type=Path,
        help="compiled shadow classes to audit for modifiers and declared members")
    args = parser.parse_args()
    try:
        expected = render(load_spec(args.input), classes_dir=args.classes_dir)
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
