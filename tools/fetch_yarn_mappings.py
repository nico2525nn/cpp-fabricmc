#!/usr/bin/env python3
"""Provision and verify the pinned Yarn intermediary -> named mapping.

The loader never downloads mappings.  This tool is the explicit cache
provisioning boundary and also checks that Yarn's intermediary class set is a
subset of the already pinned Fabric intermediary 1.21.4 artifact.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
from urllib.parse import urlparse
from urllib.request import Request, urlopen
from zipfile import ZipFile


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOCK = ROOT / "jvm/vendor/yarn-mappings.lock.json"
DEFAULT_CACHE = ROOT / "build/fabric-runtime"
ALLOWED_HOST = "maven.fabricmc.net"


class MappingError(RuntimeError):
    pass


def fail(message: str) -> None:
    raise MappingError(message)


def load_lock(path: Path) -> dict:
    try:
        lock = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot read mapping lock {path}: {exc}")
    if not isinstance(lock, dict) or lock.get("schema") != 1:
        fail("unsupported Yarn mapping lock schema")
    game = lock.get("game")
    if not isinstance(game, dict) or game.get("id") != "minecraft" or game.get("version") != "1.21.4":
        fail("mapping lock must target Minecraft 1.21.4")
    if game.get("protocol") != 769:
        fail("mapping lock must target protocol 769")
    mapping = lock.get("mapping")
    if not isinstance(mapping, dict):
        fail("mapping lock has no mapping entry")
    if mapping.get("coordinates") != "net.fabricmc:yarn:1.21.4+build.8":
        fail("mapping lock must pin Yarn 1.21.4+build.8")
    if mapping.get("sourceNamespace") != "intermediary" or mapping.get("targetNamespace") != "named":
        fail("Yarn mapping must be intermediary -> named")
    url = mapping.get("url")
    parsed = urlparse(url if isinstance(url, str) else "")
    if parsed.scheme != "https" or parsed.hostname != ALLOWED_HOST or parsed.username or parsed.password:
        fail("mapping URL must be an unauthenticated HTTPS URL on maven.fabricmc.net")
    if mapping.get("entry") != "mappings/mappings.tiny":
        fail("mapping entry must be mappings/mappings.tiny")
    path = mapping.get("cachePath")
    if not isinstance(path, str) or not path or Path(path).is_absolute() or ".." in Path(path).parts:
        fail("mapping cachePath must be a relative path")
    digest = mapping.get("sha256")
    if not isinstance(digest, str) or len(digest) != 64 or digest.lower() != digest:
        fail("mapping sha256 must be a lowercase SHA-256 digest")
    if not isinstance(mapping.get("size"), int) or mapping["size"] <= 0:
        fail("mapping size must be positive")
    intermediary = lock.get("intermediary")
    if not isinstance(intermediary, dict) or intermediary.get("targetNamespace") != "intermediary":
        fail("lock must record the pinned intermediary artifact")
    return lock


def cache_path(cache_dir: Path, relative: str) -> Path:
    root = cache_dir.resolve()
    candidate = (cache_dir / relative).resolve()
    try:
        candidate.relative_to(root)
    except ValueError:
        fail(f"cache path escapes cache directory: {relative}")
    return candidate


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_file(path: Path, expected_size: int, expected_digest: str, label: str) -> None:
    if path.is_symlink() or not path.is_file():
        fail(f"{label} is missing or is not a regular file: {path}")
    actual_size = path.stat().st_size
    if actual_size != expected_size:
        fail(f"{label} size mismatch: expected {expected_size}, got {actual_size}")
    actual_digest = sha256(path)
    if actual_digest != expected_digest:
        fail(f"{label} SHA-256 mismatch: expected {expected_digest}, got {actual_digest}")


def download(url: str, destination: Path) -> None:
    request = Request(url, headers={"User-Agent": "cpp-fabricmc-plan51-mapping-provisioner/1"})
    destination.parent.mkdir(parents=True, exist_ok=True)
    with urlopen(request, timeout=90) as response:
        with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as temporary:
            temporary_path = Path(temporary.name)
            shutil.copyfileobj(response, temporary)
    try:
        temporary_path.replace(destination)
    finally:
        if temporary_path.exists():
            temporary_path.unlink()


def tiny_classes(path: Path, entry: str, expected_source: str, expected_target: str) -> dict[str, str]:
    try:
        with ZipFile(path) as archive:
            data = archive.read(entry).decode("utf-8")
    except (OSError, KeyError, UnicodeDecodeError) as exc:
        fail(f"cannot read {entry} from {path}: {exc}")
    lines = data.splitlines()
    if not lines:
        fail(f"empty Tiny mapping in {path}")
    header = lines[0].split("\t")
    if header[:3] != ["tiny", "2", "0"]:
        fail(f"Yarn mapping is not Tiny v2: {path}")
    namespaces = header[3:]
    if expected_source not in namespaces or expected_target not in namespaces:
        fail(f"Tiny mapping namespaces are not {expected_source} -> {expected_target}: {path}")
    source_index = namespaces.index(expected_source)
    target_index = namespaces.index(expected_target)
    classes: dict[str, str] = {}
    for line in lines[1:]:
        columns = line.split("\t")
        if len(columns) >= 3 and columns[0] == "c":
            source = columns[1 + source_index]
            target = columns[1 + target_index]
            if source and target:
                classes[source] = target
    if not classes:
        fail(f"Tiny mapping contains no mapped classes: {path}")
    return classes


def intermediary_classes(path: Path, entry: str) -> set[str]:
    try:
        with ZipFile(path) as archive:
            data = archive.read(entry).decode("utf-8")
    except (OSError, KeyError, UnicodeDecodeError) as exc:
        fail(f"cannot read pinned intermediary mapping {path}: {exc}")
    lines = data.splitlines()
    if not lines or lines[0].split("\t")[:3] != ["v1", "official", "intermediary"]:
        fail("pinned intermediary artifact does not expose official -> intermediary Tiny v1")
    classes: set[str] = set()
    for line in lines[1:]:
        columns = line.split("\t")
        if len(columns) >= 3 and columns[0] == "CLASS" and columns[2]:
            classes.add(columns[2])
    if not classes:
        fail("pinned intermediary mapping contains no classes")
    return classes


def verify_alignment(lock: dict, cache_dir: Path, mapping_path: Path) -> tuple[int, int]:
    mapping = lock["mapping"]
    intermediary_spec = lock["intermediary"]
    intermediary_path = cache_path(cache_dir, intermediary_spec["cachePath"])
    verify_file(
        intermediary_path,
        int(intermediary_spec["size"]),
        str(intermediary_spec["sha256"]),
        "pinned intermediary artifact",
    )
    yarn = tiny_classes(mapping_path, str(mapping["entry"]), "intermediary", "named")
    intermediary = intermediary_classes(intermediary_path, str(intermediary_spec["entry"]))
    # A small number of Mojang-owned entrypoints/JFR classes are unchanged in
    # intermediary and therefore intentionally do not have a CLASS row in the
    # intermediary jar.  Only a Yarn class that actually changes namespace
    # must be present in the pinned intermediary class set.
    missing = sorted(source for source, target in yarn.items()
                     if source != target and source not in intermediary)
    if missing:
        fail("Yarn/intermediary class-set mismatch; first missing classes: " + ", ".join(missing[:8]))
    samples = {
        "net/minecraft/class_1922",
        "net/minecraft/class_2248",
        "net/minecraft/class_4970",
    }
    if not samples.issubset(yarn):
        fail("Yarn mapping is missing the required 1.21.4 corpus classes")
    return len(yarn), len(intermediary)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", type=Path, default=DEFAULT_LOCK)
    parser.add_argument("--cache-dir", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--provision", action="store_true")
    parser.add_argument("--offline", action="store_true")
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--print-path", action="store_true")
    args = parser.parse_args(argv)
    if args.provision and args.offline:
        parser.error("--provision and --offline are mutually exclusive")
    try:
        lock = load_lock(args.lock.resolve())
        mapping = lock["mapping"]
        path = cache_path(args.cache_dir.resolve(), str(mapping["cachePath"]))
        if args.provision and (args.force or not path.exists()):
            download(str(mapping["url"]), path)
        verify_file(path, int(mapping["size"]), str(mapping["sha256"]), "Yarn mapping artifact")
        class_count, intermediary_count = verify_alignment(lock, args.cache_dir.resolve(), path)
        result = {
            "status": "PASS",
            "version": mapping["version"],
            "path": str(path),
            "entry": mapping["entry"],
            "sourceNamespace": mapping["sourceNamespace"],
            "targetNamespace": mapping["targetNamespace"],
            "yarnClassCount": class_count,
            "intermediaryClassCount": intermediary_count,
            "aligned": True,
        }
        if args.print_path:
            print(path)
        else:
            print(json.dumps(result, sort_keys=True))
        return 0
    except MappingError as exc:
        print(f"Yarn mapping INVALID: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
