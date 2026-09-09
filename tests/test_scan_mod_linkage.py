#!/usr/bin/env python3
"""Contract tests for tools/scan_mod_linkage.py.

The fixtures are deliberately tiny class files made by this test.  They keep
the test independent of a locally installed javac while still exercising the
real class-file parser, JAR reader, direct member lookup, JSON report, and
fail-closed error paths.
"""

from __future__ import annotations

import io
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "scan_mod_linkage.py"


class ConstantPool:
    def __init__(self) -> None:
        self.entries: list[tuple[str, object] | None] = [None]

    def add(self, entry: tuple[str, object]) -> int:
        self.entries.append(entry)
        if entry[0] in {"Long", "Double"}:
            self.entries.append(None)
        return len(self.entries) - (2 if entry[0] in {"Long", "Double"} else 1)

    def utf8(self, value: str) -> int:
        return self.add(("Utf8", value))

    def cls(self, value: str) -> int:
        return self.add(("Class", self.utf8(value)))

    def nat(self, name: str, descriptor: str) -> int:
        return self.add(("NameAndType", (self.utf8(name), self.utf8(descriptor))))

    def ref(self, kind: str, owner: str, name: str, descriptor: str) -> int:
        return self.add((kind, (self.cls(owner), self.nat(name, descriptor))))

    def encode(self) -> bytes:
        result = bytearray(struct.pack(">H", len(self.entries)))
        for entry in self.entries[1:]:
            if entry is None:
                continue
            kind, value = entry
            if kind == "Utf8":
                raw = str(value).encode("utf-8").replace(b"\x00", b"\xc0\x80")
                result.extend(b"\x01" + struct.pack(">H", len(raw)) + raw)
            elif kind == "Class":
                result.extend(b"\x07" + struct.pack(">H", int(value)))
            elif kind == "NameAndType":
                name, descriptor = value  # type: ignore[misc]
                result.extend(b"\x0c" + struct.pack(">HH", name, descriptor))
            elif kind == "Fieldref":
                owner, nat = value  # type: ignore[misc]
                result.extend(b"\x09" + struct.pack(">HH", owner, nat))
            elif kind == "Methodref":
                owner, nat = value  # type: ignore[misc]
                result.extend(b"\x0a" + struct.pack(">HH", owner, nat))
            else:
                raise AssertionError(f"unexpected test constant {kind}")
        return bytes(result)


def class_file(
    name: str,
    *,
    class_refs: tuple[str, ...] = (),
    method_refs: tuple[tuple[str, str, str], ...] = (),
    field_refs: tuple[tuple[str, str, str], ...] = (),
    declared_methods: tuple[tuple[str, str], ...] = (),
    declared_fields: tuple[tuple[str, str], ...] = (),
    super_name: str = "java/lang/Object",
    interfaces: tuple[str, ...] = (),
    major: int = 65,
) -> bytes:
    cp = ConstantPool()
    this_class = cp.cls(name)
    super_class = cp.cls(super_name) if super_name else 0
    # Keep an unused modified-UTF-8 constant in every synthetic class so the
    # parser contract covers the encoding used by real Java class files.
    cp.utf8("modified\x00utf8")
    for ref in class_refs:
        cp.cls(ref)
    for owner, method, descriptor in method_refs:
        cp.ref("Methodref", owner, method, descriptor)
    for owner, field, descriptor in field_refs:
        cp.ref("Fieldref", owner, field, descriptor)
    interface_indexes = [cp.cls(interface) for interface in interfaces]
    field_members = [(cp.utf8(n), cp.utf8(d)) for n, d in declared_fields]
    method_members = [(cp.utf8(n), cp.utf8(d)) for n, d in declared_methods]
    body = bytearray(struct.pack(">IHH", 0xCAFEBABE, 0, major))
    body.extend(cp.encode())
    body.extend(struct.pack(">HHH", 0x0021, this_class, super_class))
    body.extend(struct.pack(">H", len(interface_indexes)))
    for interface in interface_indexes:
        body.extend(struct.pack(">H", interface))
    body.extend(struct.pack(">H", len(field_members)))
    for field_name, descriptor in field_members:
        body.extend(struct.pack(">HHHH", 0x0001, field_name, descriptor, 0))
    body.extend(struct.pack(">H", len(method_members)))
    for method_name, descriptor in method_members:
        body.extend(struct.pack(">HHHH", 0x0001, method_name, descriptor, 0))
    body.extend(struct.pack(">H", 0))  # class attributes
    return bytes(body)


def write_jar(path: Path, entries: dict[str, bytes]) -> None:
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_STORED) as archive:
        for name, data in entries.items():
            archive.writestr(name, data)


def jar_bytes(entries: dict[str, bytes]) -> bytes:
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_STORED) as archive:
        for name, data in entries.items():
            archive.writestr(name, data)
    return output.getvalue()


def run_tool(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(TOOL), *args],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=20,
        check=False,
    )


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="cppfm-linkage-test-") as raw_root:
        root = Path(raw_root)
        mod = root / "mod.jar"
        provider = root / "provider.jar"
        report_path = root / "report.json"
        write_jar(mod, {
            "a/Uses.class": class_file(
                "a/Uses",
                class_refs=("net/minecraft/client/MinecraftClient", "[Lnet/minecraft/Foo;", "[J"),
                method_refs=(("net/minecraft/Foo", "bar", "(I)V"),),
                field_refs=(("net/minecraft/Foo", "value", "I"),),
            )
        })
        write_jar(provider, {
            "net/minecraft/Foo.class": class_file(
                "net/minecraft/Foo",
                declared_methods=(("bar", "(I)V"),),
                declared_fields=(("value", "I"),),
            )
        })

        result = run_tool(
            "--mod", str(mod), "--classpath", str(provider),
            "--json-out", str(report_path),
        )
        check(result.returncode == 1, "missing class must fail the audit")
        report = json.loads(report_path.read_text(encoding="utf-8"))
        check(report["schema"] == "cppfm.mod-linkage-report.v1", "schema mismatch")
        check(report["status"] == "FAIL", "report must fail for missing client class")
        check("net/minecraft/client/MinecraftClient" in report["missing"]["classes"],
              "missing client class not reported")
        check(not report["missing"]["members"], "provided Foo members should resolve")
        check("net/minecraft/client/MinecraftClient" in report["references"]["clientOnly"],
              "client-only reference not classified")

        pass_mod = root / "pass.jar"
        pass_report = root / "pass.json"
        write_jar(pass_mod, {
            "a/Uses.class": class_file(
                "a/Uses", class_refs=("[Lnet/minecraft/Foo;",),
                method_refs=(("net/minecraft/Foo", "bar", "(I)V"),),
                field_refs=(("net/minecraft/Foo", "value", "I"),),
            )
        })
        result = run_tool(
            "--mod", str(pass_mod), "--classpath", str(provider),
            "--json-out", str(pass_report), "--json",
        )
        check(result.returncode == 0, "fully provided symbols must pass")
        check(json.loads(pass_report.read_text(encoding="utf-8"))["status"] == "PASS",
              "pass report status mismatch")
        check('"status": "PASS"' in result.stdout, "--json did not emit JSON")

        nested_provider = root / "nested-provider.jar"
        write_jar(nested_provider, {
            "META-INF/versions/1.21.4/server-1.21.4.jar": jar_bytes({
                "net/minecraft/Foo.class": class_file(
                    "net/minecraft/Foo",
                    declared_methods=(("bar", "(I)V"),),
                    declared_fields=(("value", "I"),),
                )
            })
        })
        result = run_tool(
            "--mod", str(pass_mod), "--classpath", str(nested_provider),
        )
        check(result.returncode == 0, "nested provider JAR symbols must resolve")

        provider_dir = root / "provider-dir"
        provider_dir.mkdir()
        shutil.copy2(nested_provider, provider_dir / nested_provider.name)
        result = run_tool("--mod", str(pass_mod), "--classpath", str(provider_dir))
        check(result.returncode == 0, "classpath directories must include nested JARs")

        inherited_provider = root / "inherited-provider.jar"
        write_jar(inherited_provider, {
            "net/minecraft/Base.class": class_file(
                "net/minecraft/Base",
                declared_methods=(("bar", "(I)V"),),
                declared_fields=(("value", "I"),),
            ),
            "net/minecraft/Foo.class": class_file(
                "net/minecraft/Foo", super_name="net/minecraft/Base",
            ),
        })
        result = run_tool(
            "--mod", str(pass_mod), "--classpath", str(inherited_provider),
        )
        check(result.returncode == 0, "inherited provider members must resolve")

        intermediary_mod = root / "intermediary-mod.jar"
        official_provider = root / "official-provider.jar"
        mapping = root / "official-to-intermediary.tiny"
        write_jar(intermediary_mod, {
            "a/Uses.class": class_file(
                "a/Uses",
                class_refs=("net/minecraft/class_1",),
                method_refs=(("net/minecraft/class_1", "method_1", "(I)V"),),
                field_refs=(("net/minecraft/class_1", "field_1", "I"),),
            )
        })
        write_jar(official_provider, {
            "a.class": class_file(
                "a", declared_methods=(("m", "(I)V"),),
                declared_fields=(("f", "I"),),
            )
        })
        mapping.write_text(
            "v1\tofficial\tintermediary\n"
            "CLASS\ta\tnet/minecraft/class_1\n"
            "FIELD\ta\tI\tf\tfield_1\n"
            "METHOD\ta\t(I)V\tm\tmethod_1\n",
            encoding="utf-8",
        )
        result = run_tool(
            "--mod", str(intermediary_mod), "--classpath", str(official_provider),
            "--mapping", str(mapping),
        )
        check(result.returncode == 0, "Tiny namespace mapping must resolve official symbols")

        missing_member_provider = root / "missing-member.jar"
        write_jar(missing_member_provider, {
            "net/minecraft/Foo.class": class_file("net/minecraft/Foo")
        })
        result = run_tool("--mod", str(pass_mod), "--classpath", str(missing_member_provider))
        check(result.returncode == 1, "missing direct member must fail")
        check("missing method: net/minecraft/Foo.bar(I)V" in result.stdout,
              "missing method was not rendered")

        malformed = root / "malformed.jar"
        write_jar(malformed, {"a/Bad.class": b"not-a-class"})
        result = run_tool("--mod", str(malformed))
        check(result.returncode == 1, "malformed class must fail closed")
        check("malformed:" in result.stdout, "malformed class was not reported")

        too_new = root / "too-new.jar"
        write_jar(too_new, {"a/New.class": class_file("a/New", major=66)})
        result = run_tool("--mod", str(too_new))
        check(result.returncode == 1, "newer Java major must fail")
        check("[66]" in result.stdout, "new Java major was not rendered")

    print("mod linkage scanner: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
