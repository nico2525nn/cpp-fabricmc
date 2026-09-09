#!/usr/bin/env python3
"""Audit a Fabric/JVM mod's class-file linkage before starting a server.

This is deliberately a conservative, dependency-free scanner.  It reads the
class-file constant pool and declared members instead of trying to execute a
mod.  A clean result means that every reference observed in the scanned class
files has a matching class/member in the supplied class path; it is not a
claim that the referenced implementation has the correct behaviour.

The scanner is useful for the bounded shadow runtime because it turns a late
``NoSuchMethodError`` into a reproducible report.  It does not parse method
bytecode, but it does resolve the declared superclass/interface graph.  Member
results are still conservative symbol checks and may contain false positives
for members introduced by transformation rather than present in raw class
files.
"""

from __future__ import annotations

import argparse
import io
import json
import struct
import sys
import zipfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterator


SCHEMA = "cppfm.mod-linkage-report.v1"
MAGIC = 0xCAFEBABE
JAVA_21_MAJOR = 65


class ClassFileError(ValueError):
    """Raised when a class file is truncated or structurally invalid."""


@dataclass(frozen=True)
class SymbolReference:
    kind: str
    owner: str
    name: str
    descriptor: str
    source: str


@dataclass
class ParsedClass:
    name: str
    major: int
    source: str
    super_name: str | None = None
    interfaces: tuple[str, ...] = ()
    class_references: set[str] = field(default_factory=set)
    symbols: list[SymbolReference] = field(default_factory=list)
    declared_fields: set[tuple[str, str]] = field(default_factory=set)
    declared_methods: set[tuple[str, str]] = field(default_factory=set)


@dataclass
class NamespaceMapping:
    """A source-to-target Tiny mapping for class and member symbols."""

    source_namespace: str
    target_namespace: str
    source: str
    classes: dict[str, str] = field(default_factory=dict)
    methods: dict[tuple[str, str, str], tuple[str, str, str]] = field(default_factory=dict)
    fields: dict[tuple[str, str, str], tuple[str, str, str]] = field(default_factory=dict)

    def map_class_name(self, name: str) -> str:
        return self.classes.get(name, name)

    def map_descriptor(self, descriptor: str) -> str:
        result: list[str] = []
        cursor = 0
        while cursor < len(descriptor):
            if descriptor[cursor] != "L":
                result.append(descriptor[cursor])
                cursor += 1
                continue
            end = descriptor.find(";", cursor + 1)
            if end < 0:
                raise ClassFileError(f"unterminated descriptor {descriptor!r}")
            result.append("L")
            result.append(self.map_class_name(descriptor[cursor + 1:end]))
            result.append(";")
            cursor = end + 1
        return "".join(result)

    def map_symbol(self, symbol: SymbolReference) -> SymbolReference:
        descriptor = symbol.descriptor
        key = (symbol.owner, symbol.name, descriptor)
        table = self.fields if symbol.kind == "field" else self.methods
        mapped = table.get(key)
        if mapped is not None:
            owner, name, descriptor = mapped
        else:
            # A bytecode reference commonly names a concrete Minecraft class
            # while the mapped member is declared by an inherited owner.
            # Resolve only a unique source-name+descriptor candidate; a
            # short-name guess would be unsafe for third-party libraries.
            candidates = {
                value
                for (_candidate_owner, candidate_name, candidate_descriptor), value
                in table.items()
                if candidate_name == symbol.name and candidate_descriptor == descriptor
            }
            candidate_signatures = {(value[1], value[2]) for value in candidates}
            if len(candidate_signatures) == 1:
                # Several inherited declarations may carry the same
                # intermediary symbol.  Their target signature is still
                # unambiguous; retain the concrete reference owner so the
                # provider hierarchy can resolve the declaration.
                name, descriptor = next(iter(candidate_signatures))
                owner = self.map_class_name(symbol.owner)
            else:
                owner = self.map_class_name(symbol.owner)
                descriptor = self.map_descriptor(descriptor)
                name = symbol.name
        return SymbolReference(symbol.kind, owner, name, descriptor, symbol.source)


def _u1(data: bytes, offset: int) -> tuple[int, int]:
    if offset + 1 > len(data):
        raise ClassFileError("truncated u1")
    return data[offset], offset + 1


def _u2(data: bytes, offset: int) -> tuple[int, int]:
    if offset + 2 > len(data):
        raise ClassFileError("truncated u2")
    return struct.unpack_from(">H", data, offset)[0], offset + 2


def _u4(data: bytes, offset: int) -> tuple[int, int]:
    if offset + 4 > len(data):
        raise ClassFileError("truncated u4")
    return struct.unpack_from(">I", data, offset)[0], offset + 4


def _skip(data: bytes, offset: int, count: int) -> int:
    if count < 0 or offset + count > len(data):
        raise ClassFileError("truncated attribute")
    return offset + count


def _decode_modified_utf8(raw: bytes, index: int) -> str:
    """Decode the modified UTF-8 used by ``CONSTANT_Utf8`` entries.

    The class-file format uses Java's modified UTF-8 rather than the regular
    UTF-8 codec: NUL is encoded as ``C0 80`` and supplementary code points are
    represented by UTF-16 surrogate pairs.  Decode with surrogate support and
    combine valid pairs while retaining legal lone UTF-16 code units.
    """
    normalized = raw.replace(b"\xc0\x80", b"\x00")
    try:
        decoded = normalized.decode("utf-8", errors="surrogatepass")
    except UnicodeDecodeError as exc:
        raise ClassFileError(
            f"invalid modified UTF-8 constant {index}: {exc}"
        ) from exc
    result: list[str] = []
    cursor = 0
    while cursor < len(decoded):
        codepoint = ord(decoded[cursor])
        if 0xD800 <= codepoint <= 0xDBFF:
            if cursor + 1 < len(decoded):
                low = ord(decoded[cursor + 1])
                if 0xDC00 <= low <= 0xDFFF:
                    result.append(chr(
                        0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00)
                    ))
                    cursor += 2
                    continue
            # Java's modified UTF-8 represents UTF-16 code units directly;
            # an unpaired surrogate is therefore legal in a CONSTANT_Utf8.
            result.append(decoded[cursor])
            cursor += 1
            continue
        result.append(decoded[cursor])
        cursor += 1
    return "".join(result)


def _utf8(cp: list[object | None], index: int) -> str:
    if index <= 0 or index >= len(cp) or not isinstance(cp[index], tuple):
        raise ClassFileError(f"invalid UTF-8 constant index {index}")
    value = cp[index]
    if value[0] != "Utf8":
        raise ClassFileError(f"constant {index} is not UTF-8")
    return value[1]


def _class_name(cp: list[object | None], index: int) -> str:
    if index <= 0 or index >= len(cp) or not isinstance(cp[index], tuple):
        raise ClassFileError(f"invalid class constant index {index}")
    value = cp[index]
    if value[0] != "Class":
        raise ClassFileError(f"constant {index} is not a class")
    return _utf8(cp, value[1])


def _name_and_type(cp: list[object | None], index: int) -> tuple[str, str]:
    if index <= 0 or index >= len(cp) or not isinstance(cp[index], tuple):
        raise ClassFileError(f"invalid name-and-type constant index {index}")
    value = cp[index]
    if value[0] != "NameAndType":
        raise ClassFileError(f"constant {index} is not name-and-type")
    return _utf8(cp, value[1]), _utf8(cp, value[2])


def normalize_class_name(value: str) -> str:
    """Convert JVM internal names/descriptors to a slash-separated class name."""

    if value.startswith("["):
        objects = descriptor_classes(value)
        # Primitive arrays (for example ``[J``) have no class symbol to
        # resolve.  Returning an empty name lets callers ignore them instead
        # of reporting a JVM descriptor as a missing class.
        return next(iter(objects), "")
    if value.startswith("L") and value.endswith(";"):
        return value[1:-1]
    return value


def descriptor_classes(descriptor: str) -> set[str]:
    """Return object types embedded in a field or method descriptor."""

    result: set[str] = set()
    cursor = 0
    while cursor < len(descriptor):
        if descriptor[cursor] != "L":
            cursor += 1
            continue
        end = descriptor.find(";", cursor + 1)
        if end < 0:
            raise ClassFileError(f"unterminated descriptor {descriptor!r}")
        name = descriptor[cursor + 1:end]
        if not name:
            raise ClassFileError(f"empty object descriptor {descriptor!r}")
        result.add(name)
        cursor = end + 1
    return result


def _remap_descriptor(descriptor: str, classes: dict[str, str]) -> str:
    """Remap object types in a JVM descriptor using a class-name table."""
    result: list[str] = []
    cursor = 0
    while cursor < len(descriptor):
        if descriptor[cursor] != "L":
            result.append(descriptor[cursor])
            cursor += 1
            continue
        end = descriptor.find(";", cursor + 1)
        if end < 0:
            raise ClassFileError(f"unterminated descriptor {descriptor!r}")
        result.append("L")
        result.append(classes.get(descriptor[cursor + 1:end], descriptor[cursor + 1:end]))
        result.append(";")
        cursor = end + 1
    return "".join(result)


def _mapping_text(path: Path) -> tuple[str, str]:
    """Read a Tiny mapping file from a file, directory, or mapping JAR."""
    if path.is_dir():
        candidates = (path / "mappings/mappings.tiny", path / "mappings.tiny")
        for candidate in candidates:
            if candidate.is_file():
                return candidate.read_text(encoding="utf-8"), str(candidate)
        raise ValueError(f"mapping file not found below {path}")
    if path.suffix.lower() in {".jar", ".zip"}:
        try:
            with zipfile.ZipFile(path) as archive:
                data = archive.read("mappings/mappings.tiny")
        except (OSError, KeyError, zipfile.BadZipFile) as exc:
            raise ValueError(f"cannot read Tiny mapping from {path}: {exc}") from exc
        try:
            return data.decode("utf-8"), f"{path}!/mappings/mappings.tiny"
        except UnicodeDecodeError as exc:
            raise ValueError(f"mapping is not UTF-8: {path}: {exc}") from exc
    try:
        return path.read_text(encoding="utf-8"), str(path)
    except (OSError, UnicodeDecodeError) as exc:
        raise ValueError(f"cannot read mapping {path}: {exc}") from exc


def load_mapping(path: Path) -> NamespaceMapping:
    """Load Tiny v1 official->intermediary or Tiny v2 intermediary->named."""
    text, source = _mapping_text(path)
    lines = text.splitlines()
    if not lines:
        raise ValueError(f"empty Tiny mapping: {path}")
    header = lines[0].split("\t")
    if header[:3] == ["v1", "official", "intermediary"]:
        namespaces = header[1:]
        source_index = namespaces.index("intermediary")
        target_index = namespaces.index("official")
        class_target_to_source: dict[str, str] = {}
        class_source_to_target: dict[str, str] = {}
        rows: list[tuple[str, str, str, str, str]] = []
        for line in lines[1:]:
            fields = line.split("\t")
            if not fields or fields[0] == "CLASS":
                if len(fields) >= 3 and fields[0] == "CLASS":
                    target_name, source_name = fields[1], fields[2]
                    if target_name and source_name:
                        class_target_to_source[target_name] = source_name
                        class_source_to_target[source_name] = target_name
                continue
            if fields[0] in {"FIELD", "METHOD"} and len(fields) >= 5:
                rows.append((fields[0], fields[1], fields[2], fields[3], fields[4]))
        mapping = NamespaceMapping(
            "intermediary", "official", source,
            classes=class_source_to_target,
        )
        for kind, owner, descriptor, target_name, source_name in rows:
            if not owner or not descriptor or not target_name or not source_name:
                continue
            source_owner = class_target_to_source.get(owner, owner)
            source_descriptor = _remap_descriptor(descriptor, class_target_to_source)
            target = (owner, target_name, descriptor)
            key = (source_owner, source_name, source_descriptor)
            (mapping.fields if kind == "FIELD" else mapping.methods)[key] = target
        if not mapping.classes:
            raise ValueError(f"Tiny v1 mapping contains no classes: {path}")
        return mapping

    if header[:3] != ["tiny", "2", "0"]:
        raise ValueError(f"unsupported Tiny mapping header in {path}")
    namespaces = header[3:]
    if "intermediary" not in namespaces:
        raise ValueError(f"Tiny v2 mapping has no intermediary namespace: {path}")
    source_index = namespaces.index("intermediary")
    target_namespaces = [name for name in namespaces if name != "intermediary"]
    if not target_namespaces:
        raise ValueError(f"Tiny v2 mapping has no target namespace: {path}")
    target_namespace = "named" if "named" in namespaces else target_namespaces[0]
    target_index = namespaces.index(target_namespace)
    classes: dict[str, str] = {}
    current_source: str | None = None
    current_target: str | None = None
    mapping = NamespaceMapping(
        "intermediary", target_namespace, source, classes=classes
    )
    member_rows: list[tuple[str, str, str, str, str, str]] = []
    for line in lines[1:]:
        fields = line.split("\t")
        if not fields:
            continue
        if fields[0] == "c":
            names = fields[1:]
            if len(names) <= max(source_index, target_index):
                continue
            current_source = names[source_index]
            current_target = names[target_index]
            if current_source and current_target:
                classes[current_source] = current_target
            continue
        if len(fields) < 4 or current_source is None or current_target is None:
            continue
        kind = fields[1]
        if kind not in {"m", "f"}:
            continue
        descriptor = fields[2]
        names = fields[3:]
        if len(names) <= max(source_index, target_index):
            continue
        source_name = names[source_index]
        target_name = names[target_index]
        if not source_name or not target_name:
            continue
        source_descriptor = descriptor
        member_rows.append((
            kind, current_source, current_target, source_name, target_name, descriptor
        ))
    for kind, owner, target_owner, source_name, target_name, descriptor in member_rows:
        source_descriptor = descriptor
        target_descriptor = _remap_descriptor(descriptor, classes)
        key = (owner, source_name, source_descriptor)
        target = (target_owner, target_name, target_descriptor)
        (mapping.methods if kind == "m" else mapping.fields)[key] = target
    if not mapping.classes:
        raise ValueError(f"Tiny v2 mapping contains no classes: {path}")
    return mapping


def _read_constant_pool(data: bytes, offset: int) -> tuple[list[object | None], int]:
    count, offset = _u2(data, offset)
    if count == 0:
        raise ClassFileError("constant pool count is zero")
    cp: list[object | None] = [None] * count
    index = 1
    while index < count:
        tag, offset = _u1(data, offset)
        if tag == 1:  # CONSTANT_Utf8
            length, offset = _u2(data, offset)
            raw = data[offset:offset + length]
            offset = _skip(data, offset, length)
            value = _decode_modified_utf8(raw, index)
            cp[index] = ("Utf8", value)
        elif tag == 3:
            value, offset = _u4(data, offset)
            cp[index] = ("Integer", value)
        elif tag == 4:
            value, offset = _u4(data, offset)
            cp[index] = ("Float", value)
        elif tag in (5, 6):  # long/double occupy two entries
            offset = _skip(data, offset, 8)
            cp[index] = ("Long" if tag == 5 else "Double",)
            index += 1
            if index >= count:
                raise ClassFileError("wide constant has no reserved slot")
        elif tag == 7:
            value, offset = _u2(data, offset)
            cp[index] = ("Class", value)
        elif tag == 8:
            value, offset = _u2(data, offset)
            cp[index] = ("String", value)
        elif tag in (9, 10, 11):
            owner, offset = _u2(data, offset)
            nat, offset = _u2(data, offset)
            cp[index] = ({9: "Fieldref", 10: "Methodref", 11: "InterfaceMethodref"}[tag], owner, nat)
        elif tag == 12:
            name, offset = _u2(data, offset)
            descriptor, offset = _u2(data, offset)
            cp[index] = ("NameAndType", name, descriptor)
        elif tag == 15:
            kind, offset = _u1(data, offset)
            reference, offset = _u2(data, offset)
            cp[index] = ("MethodHandle", kind, reference)
        elif tag == 16:
            value, offset = _u2(data, offset)
            cp[index] = ("MethodType", value)
        elif tag in (17, 18):
            bootstrap, offset = _u2(data, offset)
            nat, offset = _u2(data, offset)
            cp[index] = ("Dynamic" if tag == 17 else "InvokeDynamic", bootstrap, nat)
        elif tag in (19, 20):
            value, offset = _u2(data, offset)
            cp[index] = ("Module" if tag == 19 else "Package", value)
        else:
            raise ClassFileError(f"unsupported constant-pool tag {tag} at index {index}")
        index += 1
    return cp, offset


def _skip_attributes(data: bytes, offset: int, count: int) -> int:
    for _ in range(count):
        _name, offset = _u2(data, offset)
        length, offset = _u4(data, offset)
        offset = _skip(data, offset, length)
    return offset


def _read_members(
    data: bytes,
    offset: int,
    cp: list[object | None],
    class_name: str,
    kind: str,
) -> tuple[int, set[tuple[str, str]]]:
    count, offset = _u2(data, offset)
    members: set[tuple[str, str]] = set()
    for _ in range(count):
        _access, offset = _u2(data, offset)
        name_index, offset = _u2(data, offset)
        descriptor_index, offset = _u2(data, offset)
        name = _utf8(cp, name_index)
        descriptor = _utf8(cp, descriptor_index)
        if kind == "field":
            descriptor_classes(descriptor)
        else:
            descriptor_classes(descriptor)
        members.add((name, descriptor))
        attribute_count, offset = _u2(data, offset)
        offset = _skip_attributes(data, offset, attribute_count)
    return offset, members


def parse_class(data: bytes, source: str) -> ParsedClass:
    if len(data) < 10:
        raise ClassFileError("class file is shorter than the header")
    magic, offset = _u4(data, 0)
    if magic != MAGIC:
        raise ClassFileError("bad class-file magic")
    _minor, offset = _u2(data, offset)
    major, offset = _u2(data, offset)
    cp, offset = _read_constant_pool(data, offset)
    _access, offset = _u2(data, offset)
    this_class, offset = _u2(data, offset)
    _super_class, offset = _u2(data, offset)
    class_name = normalize_class_name(_class_name(cp, this_class))
    if not class_name or class_name.startswith("["):
        raise ClassFileError("class has an invalid binary name")
    super_name = (
        normalize_class_name(_class_name(cp, _super_class))
        if _super_class
        else None
    )

    interface_count, offset = _u2(data, offset)
    interfaces: list[str] = []
    for _ in range(interface_count):
        interface, offset = _u2(data, offset)
        interface_name = normalize_class_name(_class_name(cp, interface))
        if not interface_name:
            raise ClassFileError("class has an invalid interface name")
        interfaces.append(interface_name)
    offset, fields = _read_members(data, offset, cp, class_name, "field")
    offset, methods = _read_members(data, offset, cp, class_name, "method")
    attribute_count, offset = _u2(data, offset)
    offset = _skip_attributes(data, offset, attribute_count)
    if offset != len(data):
        # Extra bytes are not legal class-file structure and commonly indicate
        # a truncated/concatenated archive entry.  Keep the audit fail-closed.
        raise ClassFileError(f"trailing bytes after class structure: {len(data) - offset}")

    parsed = ParsedClass(
        class_name,
        major,
        source,
        super_name=super_name,
        interfaces=tuple(interfaces),
        declared_fields=fields,
        declared_methods=methods,
    )
    for index, value in enumerate(cp):
        if not isinstance(value, tuple):
            continue
        if value[0] == "Class":
            name = normalize_class_name(_utf8(cp, value[1]))
            if name and not name.startswith("["):
                parsed.class_references.add(name)
        elif value[0] in {"Fieldref", "Methodref", "InterfaceMethodref"}:
            owner = normalize_class_name(_class_name(cp, value[1]))
            name, descriptor = _name_and_type(cp, value[2])
            parsed.symbols.append(SymbolReference(
                "field" if value[0] == "Fieldref" else "method",
                owner, name, descriptor, source,
            ))
            if owner:
                parsed.class_references.add(owner)
            parsed.class_references.update(descriptor_classes(descriptor))
        elif value[0] in {"NameAndType", "MethodType"}:
            descriptor = _utf8(cp, value[2] if value[0] == "NameAndType" else value[1])
            parsed.class_references.update(descriptor_classes(descriptor))
    parsed.class_references.discard(class_name)
    return parsed


def _iter_archive_classes(
    archive: zipfile.ZipFile,
    source_prefix: str,
    *,
    depth: int = 0,
) -> Iterator[tuple[str, bytes]]:
    """Yield classes from an archive and its bounded nested JAR entries."""
    for name in sorted(archive.namelist()):
        if name.endswith(".class") and not name.endswith("module-info.class"):
            yield f"{source_prefix}!/{name}", archive.read(name)
            continue
        if depth >= 2 or not name.lower().endswith((".jar", ".zip")):
            continue
        data = archive.read(name)
        # A hostile archive should not make the audit allocate without bound.
        if len(data) > 128 * 1024 * 1024:
            raise ClassFileError(
                f"nested archive is too large ({len(data)} bytes): {name}"
            )
        try:
            with zipfile.ZipFile(io.BytesIO(data)) as nested:
                yield from _iter_archive_classes(
                    nested, f"{source_prefix}!/{name}", depth=depth + 1
                )
        except zipfile.BadZipFile as exc:
            raise ClassFileError(
                f"nested archive is not a valid ZIP: {source_prefix}!/{name}"
            ) from exc


def _iter_source_classes(path: Path) -> Iterator[tuple[str, bytes]]:
    if path.is_dir():
        for class_path in sorted(path.rglob("*.class")):
            yield str(class_path), class_path.read_bytes()
        for archive_path in sorted(path.rglob("*.jar")):
            yield from _iter_source_classes(archive_path)
        for archive_path in sorted(path.rglob("*.zip")):
            yield from _iter_source_classes(archive_path)
        return
    if path.suffix.lower() != ".jar" and path.suffix.lower() != ".zip":
        raise ValueError(f"class path entry is not a directory or JAR/ZIP: {path}")
    try:
        with zipfile.ZipFile(path) as archive:
            yield from _iter_archive_classes(archive, str(path))
    except (OSError, zipfile.BadZipFile) as exc:
        raise ValueError(f"cannot read archive {path}: {exc}") from exc


def _scan_path(path: Path, errors: list[dict[str, str]]) -> list[ParsedClass]:
    parsed: list[ParsedClass] = []
    try:
        sources = _iter_source_classes(path)
        for source, data in sources:
            try:
                parsed.append(parse_class(data, source))
            except (ClassFileError, OSError) as exc:
                errors.append({"source": source, "error": str(exc)})
    except (OSError, ValueError) as exc:
        errors.append({"source": str(path), "error": str(exc)})
    return parsed


def _runtime_class(name: str) -> bool:
    return name.startswith((
        "java/", "javax/", "jdk/", "sun/", "com/sun/", "org/w3c/",
        "org/xml/", "org/ietf/",
    ))


def _host_runtime_class(name: str) -> bool:
    """Return whether the host Java runtime supplies this class hierarchy."""
    return name.startswith((
        "java/", "javax/", "jdk/", "sun/", "com/sun/", "org/w3c/",
        "org/xml/", "org/ietf/",
    ))


_OBJECT_METHODS = {
    ("getClass", "()Ljava/lang/Class;"),
    ("hashCode", "()I"),
    ("equals", "(Ljava/lang/Object;)Z"),
    ("clone", "()Ljava/lang/Object;"),
    ("toString", "()Ljava/lang/String;"),
    ("finalize", "()V"),
    ("notify", "()V"),
    ("notifyAll", "()V"),
    ("wait", "()V"),
    ("wait", "(J)V"),
    ("wait", "(JI)V"),
}


def _client_only(name: str) -> bool:
    return name.startswith((
        "net/minecraft/client/",
        "net/fabricmc/fabric/api/client/",
        "net/fabricmc/fabric/impl/client/",
        "net/fabricmc/api/client/",
    ))


def _symbol_json(symbol: SymbolReference) -> dict[str, str]:
    return {
        "kind": symbol.kind,
        "owner": symbol.owner,
        "name": symbol.name,
        "descriptor": symbol.descriptor,
        "source": symbol.source,
    }


def audit(
    mod: Path,
    classpaths: list[Path],
    max_java_major: int,
    mappings: list[NamespaceMapping] | None = None,
) -> dict[str, object]:
    mappings = list(mappings or [])

    def map_class(name: str) -> str:
        for mapping in mappings:
            name = mapping.map_class_name(name)
        return name

    def map_descriptor(descriptor: str) -> str:
        for mapping in mappings:
            descriptor = mapping.map_descriptor(descriptor)
        return descriptor

    def map_symbol(symbol: SymbolReference) -> SymbolReference:
        for mapping in mappings:
            symbol = mapping.map_symbol(symbol)
        return symbol

    errors: list[dict[str, str]] = []
    mod_classes = _scan_path(mod, errors)
    provider_classes: list[ParsedClass] = []
    for path in classpaths:
        provider_classes.extend(_scan_path(path, errors))

    # A class provided by the mod itself satisfies its own references.  A
    # duplicate provider is retained in the report because class-loader order
    # is observable and should not be silently hidden.
    class_sources: dict[str, list[str]] = {}
    declarations: dict[str, dict[str, Any]] = {}

    def add_declarations(item: ParsedClass, *, mod_namespace: bool) -> None:
        name = map_class(item.name) if mod_namespace else item.name
        class_sources.setdefault(name, []).append(item.source)
        entry = declarations.setdefault(name, {
            "fields": set(),
            "methods": set(),
            "parents": set(),
        })
        descriptor = map_descriptor if mod_namespace else (lambda value: value)
        entry["fields"].update(
            (member_name, descriptor(member_descriptor))
            for member_name, member_descriptor in item.declared_fields
        )
        entry["methods"].update(
            (member_name, descriptor(member_descriptor))
            for member_name, member_descriptor in item.declared_methods
        )
        if item.super_name:
            entry["parents"].add(
                map_class(item.super_name) if mod_namespace else item.super_name
            )
        entry["parents"].update(
            map_class(parent) if mod_namespace else parent
            for parent in item.interfaces
        )

    for item in mod_classes:
        add_declarations(item, mod_namespace=True)
    for item in provider_classes:
        add_declarations(item, mod_namespace=False)
    all_classes = mod_classes + provider_classes

    refs: dict[tuple[str, str], set[str]] = {}
    symbols: dict[tuple[str, str, str], SymbolReference] = {}
    for item in mod_classes:
        for name in item.class_references:
            name = map_class(name)
            if name != item.name and not _runtime_class(name):
                refs.setdefault((name, "class"), set()).add(item.name)
        for symbol in item.symbols:
            symbol = map_symbol(symbol)
            if _runtime_class(symbol.owner):
                continue
            symbols[(symbol.kind, symbol.owner, symbol.name + "\0" + symbol.descriptor)] = symbol

    missing_classes = sorted(
        name for (name, kind) in refs
        if kind == "class" and name not in declarations
    )
    missing_members: list[dict[str, object]] = []

    def has_member(
        owner: str,
        kind: str,
        name: str,
        descriptor: str,
        visiting: set[str] | None = None,
    ) -> bool:
        if owner == "java/lang/Object":
            return kind == "method" and (name, descriptor) in _OBJECT_METHODS
        if _host_runtime_class(owner):
            # The scanner is run against a Java 21 process.  Host-runtime
            # classes are guaranteed to be present even when their class
            # files are not included in the explicit provider classpath.
            # Direct mod references to these classes are already filtered by
            # ``_runtime_class`` above; this branch only resolves inherited
            # members such as Object.clone or List.add.
            return True
        entry = declarations.get(owner)
        if entry is None:
            return False
        table = entry["fields"] if kind == "field" else entry["methods"]
        if (name, descriptor) in table:
            return True
        seen = set() if visiting is None else visiting
        if owner in seen:
            return False
        next_seen = seen | {owner}
        return any(
            has_member(parent, kind, name, descriptor, next_seen)
            for parent in entry["parents"]
        )

    for symbol in sorted(symbols.values(), key=lambda item: (item.owner, item.kind, item.name, item.descriptor)):
        members = declarations.get(symbol.owner)
        if members is None:
            continue
        if not has_member(symbol.owner, symbol.kind, symbol.name, symbol.descriptor):
            missing_members.append(_symbol_json(symbol))

    all_mod_refs = sorted({
        map_class(name) for item in mod_classes for name in item.class_references
    })
    client_refs = [name for name in all_mod_refs if _client_only(name)]
    majors = sorted({item.major for item in all_classes})
    duplicate_classes = sorted(name for name, sources in class_sources.items() if len(sources) > 1)
    malformed = list(errors)
    too_new = [major for major in majors if major > max_java_major]
    status = "PASS" if not malformed and not missing_classes and not missing_members and not too_new else "FAIL"
    return {
        "schema": SCHEMA,
        "status": status,
        "mod": str(mod.resolve()),
        "classpath": [str(path.resolve()) for path in classpaths],
        "mappings": [{
            "sourceNamespace": mapping.source_namespace,
            "targetNamespace": mapping.target_namespace,
            "source": mapping.source,
            "classCount": len(mapping.classes),
            "methodCount": len(mapping.methods),
            "fieldCount": len(mapping.fields),
        } for mapping in mappings],
        "java": {
            "maxMajor": max_java_major,
            "observedMajors": majors,
            "tooNewMajors": too_new,
        },
        "classes": {
            "mod": len(mod_classes),
            "classpath": len(provider_classes),
            "unique": len(declarations),
            "duplicates": duplicate_classes,
        },
        "references": {
            "classes": all_mod_refs,
            "clientOnly": client_refs,
            "symbols": [_symbol_json(symbol) for symbol in sorted(
                symbols.values(), key=lambda item: (item.owner, item.kind, item.name, item.descriptor)
            )],
        },
        "missing": {
            "classes": missing_classes,
            "members": missing_members,
        },
        "errors": malformed,
    }


def _print_human(report: dict[str, object]) -> None:
    java = report["java"]
    classes = report["classes"]
    missing = report["missing"]
    references = report["references"]
    print(f"linkage audit: {report['status']}")
    print(f"  classes: mod={classes['mod']} classpath={classes['classpath']} unique={classes['unique']}")
    print(f"  java majors: {java['observedMajors']} (max={java['maxMajor']})")
    print(f"  references: classes={len(references['classes'])} symbols={len(references['symbols'])}")
    if references["clientOnly"]:
        print(f"  client-only references: {len(references['clientOnly'])}")
        for name in references["clientOnly"]:
            print(f"    client-only: {name}")
    for name in missing["classes"]:
        print(f"  missing class: {name}")
    for symbol in missing["members"]:
        print(f"  missing {symbol['kind']}: {symbol['owner']}.{symbol['name']}{symbol['descriptor']}")
    for error in report["errors"]:
        print(f"  malformed: {error['source']}: {error['error']}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mod", "--jar", dest="mod", type=Path, required=True,
                        help="mod JAR/ZIP or exploded classes directory")
    parser.add_argument("--classpath", "--classes", dest="classpath", type=Path,
                        action="append", default=[],
                        help="provided classes directory or JAR; may be repeated")
    parser.add_argument("--mapping", type=Path, action="append", default=[],
                        help="Tiny v1/v2 mapping used to normalize mod symbols; may be repeated")
    parser.add_argument("--max-java-major", type=int, default=JAVA_21_MAJOR)
    parser.add_argument("--json-out", type=Path,
                        help="write the complete report as JSON")
    parser.add_argument("--json", action="store_true", help="print JSON instead of human output")
    args = parser.parse_args(argv)
    if args.max_java_major < 45:
        parser.error("--max-java-major must be at least 45")
    try:
        mappings = [load_mapping(path.resolve()) for path in args.mapping]
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    report = audit(
        args.mod.resolve(),
        [path.resolve() for path in args.classpath],
        args.max_java_major,
        mappings,
    )
    encoded = json.dumps(report, ensure_ascii=False, sort_keys=True, indent=2) + "\n"
    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(encoded, encoding="utf-8")
    if args.json:
        print(encoded, end="")
    else:
        _print_human(report)
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
