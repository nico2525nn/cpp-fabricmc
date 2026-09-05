#!/usr/bin/env python3
"""Provision and verify the pinned plan51 real Fabric mod corpus.

The lock is the authority for every input.  Normal operation is offline and
only verifies an existing cache.  Network access is possible only with the
explicit ``--provision`` flag, and downloaded JARs are kept under the ignored
``build/`` cache (or an explicitly supplied cache outside the repository).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
from typing import Any, Iterable, NoReturn
from urllib.parse import urlparse
import zipfile


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOCK = ROOT / "tests/real_mod_corpus/corpus.lock.json"
DEFAULT_CACHE = ROOT / "build/real-mod-corpus"
ALLOWED_ARTIFACT_HOSTS = {"cdn.modrinth.com", "piston-data.mojang.com"}
SHA_RE = re.compile(r"^[0-9a-f]{40}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


class CorpusError(RuntimeError):
    """A malformed lock or a cache/provisioning failure."""


class MissingCache(CorpusError):
    """The offline cache is incomplete; this is a SKIP, not a PASS."""


def _die(message: str) -> NoReturn:
    raise CorpusError(message)


def _safe_relative(raw: Any, label: str) -> Path:
    if not isinstance(raw, str) or not raw:
        _die(f"{label}: expected a non-empty relative path")
    path = Path(raw)
    if path.is_absolute() or ".." in path.parts or path == Path("."):
        _die(f"{label}: path must stay relative to the cache: {raw!r}")
    return path


def _validate_digest(raw: Any, label: str, pattern: re.Pattern[str]) -> str:
    if not isinstance(raw, str) or not pattern.fullmatch(raw):
        _die(f"{label}: expected a lowercase digest")
    return raw


def _validate_url(raw: Any, label: str, hosts: set[str]) -> str:
    if not isinstance(raw, str):
        _die(f"{label}: URL is missing")
    parsed = urlparse(raw)
    if (
        parsed.scheme != "https"
        or parsed.username
        or parsed.password
        or parsed.hostname not in hosts
        or parsed.port not in (None, 443)
    ):
        _die(f"{label}: URL is not an approved HTTPS artifact URL: {raw}")
    return raw


def _string_list(value: Any, label: str) -> list[str]:
    if not isinstance(value, list) or not all(isinstance(item, str) and item for item in value):
        _die(f"{label}: expected a list of non-empty strings")
    return list(value)


def _validate_behavior(value: Any, label: str) -> None:
    if not isinstance(value, dict):
        _die(f"{label}: behavior must be an object")
    if not isinstance(value.get("majorFunction"), str) or not value["majorFunction"]:
        _die(f"{label}.majorFunction: missing description")
    _string_list(value.get("initializeLogRegex", []), f"{label}.initializeLogRegex")
    _string_list(value.get("referenceLogRegex", []), f"{label}.referenceLogRegex")
    commands = value.get("commands", [])
    if not isinstance(commands, list):
        _die(f"{label}.commands: expected a list")
    for index, command in enumerate(commands):
        item_label = f"{label}.commands[{index}]"
        if not isinstance(command, dict) or not isinstance(command.get("input"), str):
            _die(f"{item_label}: input is required")
        _string_list(command.get("referenceLogRegex", []), f"{item_label}.referenceLogRegex")
        _string_list(command.get("cppfmResponseRegex", []), f"{item_label}.cppfmResponseRegex")


def _validate_metadata(metadata: Any, label: str) -> None:
    if not isinstance(metadata, dict):
        _die(f"{label}: metadata must be an object")
    _validate_digest(metadata.get("sha256"), f"{label}.sha256", SHA256_RE)
    for key in ("id", "version", "environment"):
        if not isinstance(metadata.get(key), str) or not metadata[key]:
            _die(f"{label}.{key}: missing non-empty string")
    entrypoints = metadata.get("entrypoints")
    if not isinstance(entrypoints, dict):
        _die(f"{label}.entrypoints: expected an object")
    for key, value in entrypoints.items():
        if not isinstance(key, str) or not key:
            _die(f"{label}.entrypoints: invalid key")
        _string_list(value, f"{label}.entrypoints.{key}")
    depends = metadata.get("depends")
    if not isinstance(depends, dict) or not all(
        isinstance(key, str) and key and isinstance(value, str) and value
        for key, value in depends.items()
    ):
        _die(f"{label}.depends: expected a string map")
    _string_list(metadata.get("mixins"), f"{label}.mixins")
    access_widener = metadata.get("accessWidener")
    if access_widener is not None and (
        not isinstance(access_widener, str) or not access_widener
    ):
        _die(f"{label}.accessWidener: expected null or a non-empty string")


def load_lock(path: Path = DEFAULT_LOCK) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        _die(f"lock file not found: {path}")
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        _die(f"cannot read lock file {path}: {exc}")
    if not isinstance(value, dict):
        _die("lock root must be an object")
    validate_lock(value)
    return value


def validate_lock(lock: dict[str, Any]) -> None:
    if lock.get("schemaVersion") != 1:
        _die("unsupported corpus lock schema")
    if lock.get("suite") != "cppfm-plan51-real-mod-corpus":
        _die("lock suite is not cppfm-plan51-real-mod-corpus")
    game = lock.get("game")
    if not isinstance(game, dict) or game.get("id") != "minecraft":
        _die("lock game id must be minecraft")
    if game.get("version") != "1.21.4" or game.get("protocol") != 769:
        _die("lock must target Minecraft 1.21.4 / protocol 769")
    source = lock.get("source")
    if not isinstance(source, dict) or source.get("provider") != "Modrinth":
        _die("lock source must be Modrinth")

    runtime = lock.get("runtime")
    if not isinstance(runtime, dict):
        _die("lock runtime must be an object")
    for key in ("loaderVersion", "mixinVersion", "fabricRuntimeLock", "referenceMainClass"):
        if not isinstance(runtime.get(key), str) or not runtime[key]:
            _die(f"runtime.{key}: missing non-empty string")
    java_major = runtime.get("javaMajor")
    if (
        not isinstance(java_major, dict)
        or not isinstance(java_major.get("minimum"), int)
        or not isinstance(java_major.get("maximum"), int)
        or java_major["minimum"] <= 0
        or java_major["maximum"] < java_major["minimum"]
    ):
        _die("runtime.javaMajor must contain a valid minimum/maximum")
    server = runtime.get("referenceServer")
    if not isinstance(server, dict):
        _die("runtime.referenceServer must be an object")
    _validate_url(server.get("url"), "runtime.referenceServer.url", {"piston-data.mojang.com"})
    server_path = _safe_relative(server.get("cachePath"), "runtime.referenceServer.cachePath")
    if server_path.suffix != ".jar" or server.get("filename") != server_path.name:
        _die("runtime.referenceServer filename/cachePath must name a JAR")
    _validate_digest(server.get("sha1"), "runtime.referenceServer.sha1", SHA_RE)
    _validate_digest(server.get("sha256"), "runtime.referenceServer.sha256", SHA256_RE)
    if not isinstance(server.get("size"), int) or server["size"] <= 0:
        _die("runtime.referenceServer.size must be positive")

    mods = lock.get("mods")
    if not isinstance(mods, list) or len(mods) != 3:
        _die("real corpus lock must contain exactly three public mods")
    ids: set[str] = set()
    paths: set[Path] = {server_path}
    for index, mod in enumerate(mods):
        label = f"mods[{index}]"
        if not isinstance(mod, dict):
            _die(f"{label}: expected an object")
        mod_id = mod.get("id")
        if not isinstance(mod_id, str) or not mod_id or mod_id in ids:
            _die(f"{label}.id: invalid or duplicate mod id")
        ids.add(mod_id)
        for key in ("projectId", "projectSlug", "versionId", "version", "filename"):
            if not isinstance(mod.get(key), str) or not mod[key]:
                _die(f"{label}.{key}: missing non-empty string")
        _validate_url(mod.get("url"), f"{label}.url", {"cdn.modrinth.com"})
        mod_path = _safe_relative(mod.get("cachePath"), f"{label}.cachePath")
        if mod_path.suffix != ".jar" or mod.get("filename") != mod_path.name:
            _die(f"{label}: filename/cachePath must name a JAR")
        if mod_path in paths:
            _die(f"{label}.cachePath: duplicate cache path")
        paths.add(mod_path)
        _validate_digest(mod.get("sha1"), f"{label}.sha1", SHA_RE)
        _validate_digest(mod.get("sha256"), f"{label}.sha256", SHA256_RE)
        if not isinstance(mod.get("size"), int) or mod["size"] <= 0:
            _die(f"{label}.size must be positive")
        if not isinstance(mod.get("environment"), str) or not mod["environment"]:
            _die(f"{label}.environment is missing")
        _validate_metadata(mod.get("metadata"), f"{label}.metadata")
        _validate_behavior(mod.get("behavior"), f"{label}.behavior")


def lock_artifacts(lock: dict[str, Any]) -> Iterable[tuple[str, dict[str, Any]]]:
    yield "reference-server", lock["runtime"]["referenceServer"]
    for mod in lock["mods"]:
        yield str(mod["id"]), mod


def cache_path(cache_dir: Path, artifact: dict[str, Any]) -> Path:
    root = cache_dir.resolve()
    relative = _safe_relative(artifact["cachePath"], "artifact.cachePath")
    candidate = (root / relative).resolve(strict=False)
    try:
        candidate.relative_to(root)
    except ValueError:
        _die(f"cache path escapes cache directory: {relative}")
    return candidate


def _digest(path: Path) -> tuple[int, str, str]:
    sha256 = hashlib.sha256()
    sha1 = hashlib.sha1()
    size = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            size += len(chunk)
            sha256.update(chunk)
            sha1.update(chunk)
    return size, sha1.hexdigest(), sha256.hexdigest()


def verify_artifact(artifact: dict[str, Any], path: Path, label: str) -> None:
    if not path.exists():
        raise MissingCache(f"missing {label}: {path}")
    if path.is_symlink() or not path.is_file():
        _die(f"cache item is not a regular file for {label}: {path}")
    size, sha1, sha256 = _digest(path)
    if size != artifact["size"] or sha1 != artifact["sha1"] or sha256 != artifact["sha256"]:
        _die(
            f"locked content mismatch for {label}: size={size}/{artifact['size']} "
            f"sha1={sha1}/{artifact['sha1']} sha256={sha256}/{artifact['sha256']}"
        )
    try:
        with zipfile.ZipFile(path) as archive:
            bad = archive.testzip()
            if bad is not None:
                _die(f"corrupt JAR member for {label}: {bad}")
            if label != "reference-server" and archive.getinfo("fabric.mod.json") is None:
                _die(f"{label}: fabric.mod.json is missing")
    except KeyError as exc:
        _die(f"{label}: fabric.mod.json is missing")
    except (OSError, zipfile.BadZipFile) as exc:
        _die(f"{label}: cannot open JAR: {exc}")


def verify_cache(lock: dict[str, Any], cache_dir: Path) -> dict[str, Any]:
    validate_lock(lock)
    cache_dir = cache_dir.resolve()
    paths: dict[str, Path] = {}
    missing: list[str] = []
    for label, artifact in lock_artifacts(lock):
        path = cache_path(cache_dir, artifact)
        paths[label] = path
        if not path.exists():
            missing.append(f"{label}: {path}")
    if missing:
        raise MissingCache(
            "offline cache is incomplete; run tools/fetch_real_mod_corpus.py "
            f"--provision --cache-dir {cache_dir}: " + "; ".join(missing)
        )
    for label, artifact in lock_artifacts(lock):
        verify_artifact(artifact, paths[label], label)
    return {
        "status": "VERIFIED",
        "networkAccessed": False,
        "cachePath": str(cache_dir),
        "artifactCount": len(paths),
        "artifacts": {
            label: {
                "path": str(path),
                "size": artifact["size"],
                "sha1": artifact["sha1"],
                "sha256": artifact["sha256"],
            }
            for label, artifact in lock_artifacts(lock)
            for path in [paths[label]]
        },
    }


def _run_bounded(command: list[str], timeout: float) -> tuple[int, str, str]:
    try:
        process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=True,
        )
    except OSError as exc:
        _die(f"could not start {' '.join(command)}: {exc}")
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        stdout, stderr = process.communicate()
        _die(f"command timed out after {timeout}s: {' '.join(command)}\n{stdout}{stderr}")
    return process.returncode, stdout, stderr


def _download(
    artifact: dict[str, Any],
    destination: Path,
    label: str,
    timeout: float,
) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    temp_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb",
            prefix=f".{destination.name}.",
            suffix=".part",
            dir=destination.parent,
            delete=False,
        ) as temp:
            temp_name = temp.name
        command = [
            "curl",
            "--fail",
            "--silent",
            "--show-error",
            "--location",
            "--proto",
            "=https",
            "--tlsv1.2",
            "--connect-timeout",
            str(max(1.0, timeout)),
            "--max-time",
            str(max(1.0, timeout)),
            "--user-agent",
            "cpp-fabricmc-plan52-real-corpus/1",
            "--output",
            temp_name,
            "--write-out",
            "%{url_effective}",
            artifact["url"],
        ]
        returncode, stdout, stderr = _run_bounded(command, timeout + 5.0)
        if returncode != 0:
            detail = stderr.strip() or f"curl exit {returncode}"
            _die(f"download failed for {label}: {detail}")
        final_url = urlparse(stdout.strip())
        if (
            final_url.scheme != "https"
            or final_url.hostname not in ALLOWED_ARTIFACT_HOSTS
            or final_url.port not in (None, 443)
        ):
            _die(f"refusing redirect outside approved hosts for {label}: {stdout.strip()}")
        size, sha1, sha256 = _digest(Path(temp_name))
        if size != artifact["size"] or sha1 != artifact["sha1"] or sha256 != artifact["sha256"]:
            _die(
                f"download verification failed for {label}: size={size}/{artifact['size']} "
                f"sha1={sha1}/{artifact['sha1']} sha256={sha256}/{artifact['sha256']}"
            )
        os.replace(temp_name, destination)
        temp_name = None
    finally:
        if temp_name is not None:
            try:
                os.unlink(temp_name)
            except FileNotFoundError:
                pass


def _cache_is_safe(cache_dir: Path) -> None:
    """Keep repository-local JAR output in the already ignored build tree."""
    resolved = cache_dir.resolve()
    try:
        relative = resolved.relative_to(ROOT)
    except ValueError:
        return
    if not relative.parts or relative.parts[0] != "build":
        _die(
            f"refusing to place JAR cache inside tracked repository paths: {resolved}; "
            "use build/real-mod-corpus or a cache outside the repository"
        )


def provision(lock: dict[str, Any], cache_dir: Path, timeout: float, force: bool) -> bool:
    _cache_is_safe(cache_dir)
    network_accessed = False
    for label, artifact in lock_artifacts(lock):
        destination = cache_path(cache_dir, artifact)
        if destination.exists() and not force:
            try:
                verify_artifact(artifact, destination, label)
            except MissingCache:
                pass
            print(f"cached {label}: {destination}")
            continue
        print(f"provision {label}: {artifact['url']} -> {destination}")
        _download(artifact, destination, label, timeout)
        verify_artifact(artifact, destination, label)
        network_accessed = True
    return network_accessed


def _summary(lock: dict[str, Any], cache_dir: Path, network_accessed: bool) -> dict[str, Any]:
    verified = verify_cache(lock, cache_dir)
    return {
        "schema": "cppfm.real-mod-corpus.cache.v1",
        "status": verified["status"],
        "networkAccessed": network_accessed,
        "game": lock["game"],
        "lock": {
            "path": str(DEFAULT_LOCK),
            "modCount": len(lock["mods"]),
        },
        "cache": verified,
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--provision", action="store_true", help="download locked files; network is explicit")
    mode.add_argument("--offline", action="store_true", help="verify only; never use the network")
    parser.add_argument("--lock", type=Path, default=DEFAULT_LOCK)
    parser.add_argument("--cache-dir", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--timeout", type=float, default=90.0, help="timeout per download in seconds")
    parser.add_argument("--force", action="store_true", help="replace mismatched files with --provision")
    parser.add_argument("--json", action="store_true", help="print machine-readable status")
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.force and not args.provision:
        parser.error("--force is only valid with --provision")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        lock = load_lock(args.lock.resolve())
        cache_dir = args.cache_dir.resolve()
        network_accessed = False
        if args.provision:
            network_accessed = provision(lock, cache_dir, args.timeout, args.force)
        summary = _summary(lock, cache_dir, network_accessed=network_accessed)
        if args.json:
            print(json.dumps(summary, sort_keys=True, indent=2))
        else:
            print(
                f"real mod corpus cache {summary['status']}: "
                f"{len(lock['mods'])} mods + reference server; offline verification complete"
            )
        return 0
    except MissingCache as exc:
        payload = {
            "schema": "cppfm.real-mod-corpus.cache.v1",
            "status": "SKIP",
            "networkAccessed": bool(args.provision),
            "game": {"id": "minecraft", "version": "1.21.4", "protocol": 769},
            "reason": str(exc),
        }
        if args.json:
            print(json.dumps(payload, sort_keys=True, indent=2))
        else:
            print(f"real mod corpus cache SKIP: {exc}", file=sys.stderr)
        return 2
    except (CorpusError, OSError, ValueError) as exc:
        payload = {
            "schema": "cppfm.real-mod-corpus.cache.v1",
            "status": "FAIL",
            "networkAccessed": bool(args.provision),
            "game": {"id": "minecraft", "version": "1.21.4", "protocol": 769},
            "reason": str(exc),
        }
        if args.json:
            print(json.dumps(payload, sort_keys=True, indent=2))
        else:
            print(f"real mod corpus cache FAIL: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
