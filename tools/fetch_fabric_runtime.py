#!/usr/bin/env python3
"""Provision and verify the pinned official Fabric 1.21.4 runtime.

Network access is intentionally opt-in.  The default operation, and every
operation with --offline, only inspects an existing cache.  The lock file is
the authority for URLs, versions, sizes, and SHA-256 digests.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from typing import Any, Iterable, NoReturn
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "jvm/vendor/fabric-runtime.lock.json"
DEFAULT_CACHE = ROOT / "build/fabric-runtime"
ALLOWED_HOSTS = {"meta.fabricmc.net", "maven.fabricmc.net"}
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


class RuntimeError_(RuntimeError):
    """A user-actionable lock/cache error."""


def _die(message: str) -> NoReturn:
    raise RuntimeError_(message)


def _safe_relative_path(raw: Any, label: str) -> Path:
    if not isinstance(raw, str) or not raw:
        _die(f"{label}: expected a non-empty relative path")
    path = Path(raw)
    if path.is_absolute() or ".." in path.parts:
        _die(f"{label}: path must stay relative to the cache: {raw!r}")
    if path == Path("."):
        _die(f"{label}: path must name a file")
    return path


def _cache_path(cache_dir: Path, relative: Path, label: str) -> Path:
    root = cache_dir.resolve()
    candidate = (cache_dir / relative).resolve(strict=False)
    try:
        candidate.relative_to(root)
    except ValueError:
        _die(f"{label}: resolved path escapes cache directory: {relative}")
    return candidate


def _validate_https_url(raw: Any, label: str) -> str:
    if not isinstance(raw, str):
        _die(f"{label}: URL is missing")
    parsed = urlparse(raw)
    if parsed.scheme != "https" or parsed.username or parsed.password:
        _die(f"{label}: only https URLs without credentials are allowed: {raw}")
    if parsed.hostname not in ALLOWED_HOSTS or parsed.port not in (None, 443):
        _die(f"{label}: host is not an approved official host: {raw}")
    return raw


def _validate_sha(raw: Any, label: str) -> str:
    if not isinstance(raw, str) or not SHA256_RE.fullmatch(raw):
        _die(f"{label}: expected a lowercase SHA-256 digest")
    return raw


def load_manifest(path: Path = DEFAULT_MANIFEST) -> dict[str, Any]:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        _die(f"lock manifest not found: {path}")
    except json.JSONDecodeError as exc:
        _die(f"invalid lock manifest {path}: {exc}")
    if not isinstance(manifest, dict) or manifest.get("schema") != 1:
        _die(f"unsupported lock manifest schema in {path}")

    game = manifest.get("game")
    if not isinstance(game, dict) or game.get("id") != "minecraft" or game.get("version") != "1.21.4":
        _die("lock manifest must target Minecraft 1.21.4")
    if game.get("protocol") != 769:
        _die("lock manifest must target protocol 769")

    loader = manifest.get("loader")
    if not isinstance(loader, dict) or not loader.get("version"):
        _die("lock manifest has no pinned loader version")

    profile = manifest.get("profile")
    if not isinstance(profile, dict):
        _die("lock manifest has no profile")
    _validate_https_url(profile.get("url"), "profile.url")
    _validate_sha(profile.get("sha256"), "profile.sha256")
    if not isinstance(profile.get("size"), int) or profile["size"] <= 0:
        _die("profile.size must be a positive integer")
    profile_path = _safe_relative_path(profile.get("cachePath"), "profile.cachePath")
    if not isinstance(profile.get("mainClass"), str) or not profile["mainClass"]:
        _die("profile.mainClass is missing")
    profile["_cachePath"] = profile_path.as_posix()

    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        _die("lock manifest has no artifacts")
    seen: set[str] = set()
    for index, artifact in enumerate(artifacts):
        label = f"artifacts[{index}]"
        if not isinstance(artifact, dict):
            _die(f"{label}: expected an object")
        coordinates = artifact.get("coordinates")
        if not isinstance(coordinates, str) or not coordinates or coordinates in seen:
            _die(f"{label}: coordinates must be unique and non-empty")
        seen.add(coordinates)
        _validate_https_url(artifact.get("url"), f"{label}.url")
        artifact["_cachePath"] = _safe_relative_path(artifact.get("path"), f"{label}.path").as_posix()
        _validate_sha(artifact.get("sha256"), f"{label}.sha256")
        size = artifact.get("size")
        if not isinstance(size, int) or size <= 0:
            _die(f"{label}.size must be a positive integer")
        for embedded_index, embedded in enumerate(artifact.get("embedded", [])):
            if not isinstance(embedded, dict):
                _die(f"{label}.embedded[{embedded_index}]: expected an object")
            embedded_path = embedded.get("path")
            if not isinstance(embedded_path, str) or not embedded_path.startswith("META-INF/"):
                _die(f"{label}.embedded[{embedded_index}].path must be under META-INF/")
            _validate_sha(embedded.get("sha256"), f"{label}.embedded[{embedded_index}].sha256")
            embedded_size = embedded.get("size")
            if not isinstance(embedded_size, int) or embedded_size <= 0:
                _die(f"{label}.embedded[{embedded_index}].size must be positive")

    return manifest


def _digest(path: Path) -> tuple[int, str]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            size += len(chunk)
            digest.update(chunk)
    return size, digest.hexdigest()


def _check_file(path: Path, expected_size: int, expected_sha: str, label: str) -> tuple[bool, str]:
    if not path.exists():
        return False, f"missing {label}: {path}"
    if path.is_symlink():
        return False, f"refusing symlink cache item {label}: {path}"
    if not path.is_file():
        return False, f"cache item is not a regular file {label}: {path}"
    size, digest = _digest(path)
    if size != expected_size:
        return False, f"size mismatch for {label}: got {size}, expected {expected_size}"
    if digest != expected_sha:
        return False, f"SHA-256 mismatch for {label}: got {digest}, expected {expected_sha}"
    return True, "ok"


def profile_path(manifest: dict[str, Any], cache_dir: Path) -> Path:
    return _cache_path(cache_dir, Path(manifest["profile"]["_cachePath"]), "profile")


def artifact_path(manifest: dict[str, Any], cache_dir: Path, artifact: dict[str, Any]) -> Path:
    return _cache_path(cache_dir, Path(artifact["_cachePath"]), artifact["coordinates"])


def verify_cache(manifest: dict[str, Any], cache_dir: Path) -> list[Path]:
    """Verify the complete offline cache and return its artifact classpath."""
    cache_dir = cache_dir.resolve()
    profile = manifest["profile"]
    # Verify the immutable profile size/digest separately, then parse it below.
    profile_file = profile_path(manifest, cache_dir)
    if not profile_file.exists():
        _die(f"MISSING OFFLINE profile {profile['cachePath']}; run: python3 tools/fetch_fabric_runtime.py --provision --cache-dir {cache_dir}")
    if profile_file.is_symlink() or not profile_file.is_file():
        _die(f"OFFLINE profile is not a regular file: {profile_file}")
    profile_size, profile_digest = _digest(profile_file)
    if profile_size != profile["size"]:
        _die(f"OFFLINE profile size mismatch: got {profile_size}, expected {profile['size']}")
    if profile_digest != profile["sha256"]:
        _die(f"OFFLINE profile SHA-256 mismatch: got {profile_digest}, expected {profile['sha256']}")
    try:
        profile_json = json.loads(profile_file.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        _die(f"OFFLINE profile is not valid JSON: {profile_file}: {exc}")
    expected_profile_id = f"fabric-loader-{manifest['loader']['version']}-{manifest['game']['version']}"
    if not isinstance(profile_json, dict):
        _die(f"OFFLINE profile JSON must be an object: {profile_file}")
    if profile_json.get("id") != expected_profile_id:
        _die(f"OFFLINE profile id mismatch: {profile_json.get('id')!r}")
    if profile_json.get("mainClass") != profile["mainClass"]:
        _die(f"OFFLINE profile mainClass mismatch: {profile_json.get('mainClass')!r}")

    paths: list[Path] = []
    for artifact in manifest["artifacts"]:
        path = artifact_path(manifest, cache_dir, artifact)
        ok, reason = _check_file(path, artifact["size"], artifact["sha256"], artifact["coordinates"])
        if not ok:
            if reason.startswith("missing "):
                _die(f"MISSING OFFLINE artifact {artifact['coordinates']} at {path}; run: python3 tools/fetch_fabric_runtime.py --provision --cache-dir {cache_dir}")
            _die(f"OFFLINE artifact verification failed: {reason}")
        paths.append(path)
    return paths


def _curl_download_and_check(
    url: str,
    destination: Path,
    label: str,
    timeout: float,
    expected_size: int | None = None,
    expected_sha: str | None = None,
) -> None:
    """Use curl only for explicit provisioning, then atomically install checked bytes."""
    _validate_https_url(url, f"{label}.url")
    destination.parent.mkdir(parents=True, exist_ok=True)
    temp_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", prefix=f".{destination.name}.", suffix=".part", dir=destination.parent, delete=False
        ) as temp:
            temp_name = temp.name
        curl_timeout = str(max(1.0, timeout))
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
            curl_timeout,
            "--max-time",
            curl_timeout,
            "--user-agent",
            "cpp-fabricmc-pinned-runtime/1",
            "--output",
            temp_name,
            "--write-out",
            "%{url_effective}",
            url,
        ]
        try:
            result = subprocess.run(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=timeout + 5.0,
                check=False,
            )
        except FileNotFoundError:
            _die("curl is required for --provision but was not found")
        except subprocess.TimeoutExpired as exc:
            _die(f"curl timed out for {label} after {timeout}s: {exc}")
        if result.returncode != 0:
            detail = result.stderr.strip() or f"curl exit {result.returncode}"
            _die(f"curl failed for {label}: {detail}")
        final_url = result.stdout.strip()
        final = urlparse(final_url)
        if final.scheme != "https" or final.hostname not in ALLOWED_HOSTS or final.port not in (None, 443):
            _die(f"refusing redirect outside official hosts: {url} -> {final_url}")

        actual_size, actual_sha = _digest(Path(temp_name))
        if expected_size is not None and actual_size != expected_size:
            _die(f"download verification failed for {label}: got size={actual_size}, expected size={expected_size}")
        if expected_sha is not None and actual_sha != expected_sha:
            _die(f"download verification failed for {label}: got sha256={actual_sha}, expected sha256={expected_sha}")
        os.replace(temp_name, destination)
        temp_name = None
    finally:
        if temp_name is not None and os.path.exists(temp_name):
            os.unlink(temp_name)


def provision(manifest: dict[str, Any], cache_dir: Path, timeout: float, force: bool) -> None:
    cache_dir = cache_dir.resolve()
    profile = manifest["profile"]
    profile_file = profile_path(manifest, cache_dir)
    if not force and profile_file.exists():
        if profile_file.is_symlink() or not profile_file.is_file():
            _die(f"existing profile is not a regular file; use a clean cache or --force: {profile_file}")
        size, digest = _digest(profile_file)
        if size != profile["size"] or digest != profile["sha256"]:
            _die(f"existing profile has wrong SHA-256; use --force to replace explicitly: {profile_file}")
    else:
        print(f"provision profile {profile['url']} -> {profile_file}")
        _curl_download_and_check(
            profile["url"],
            profile_file,
            "profile",
            timeout,
            expected_size=profile["size"],
            expected_sha=profile["sha256"],
        )

    for artifact in manifest["artifacts"]:
        path = artifact_path(manifest, cache_dir, artifact)
        if not force and path.exists():
            ok, reason = _check_file(path, artifact["size"], artifact["sha256"], artifact["coordinates"])
            if ok:
                print(f"cached {artifact['coordinates']}")
                continue
            _die(f"existing artifact is not locked content ({reason}); use --force to replace explicitly: {path}")
        print(f"provision {artifact['coordinates']} -> {path}")
        _curl_download_and_check(
            artifact["url"],
            path,
            artifact["coordinates"],
            timeout,
            expected_size=artifact["size"],
            expected_sha=artifact["sha256"],
        )


def _summary(manifest: dict[str, Any], cache_dir: Path, paths: Iterable[Path]) -> dict[str, Any]:
    return {
        "status": "verified",
        "game": manifest["game"],
        "loader": manifest["loader"],
        "profile": {
            "path": str(profile_path(manifest, cache_dir)),
            "sha256": manifest["profile"]["sha256"],
            "size": manifest["profile"]["size"],
            "mainClass": manifest["profile"]["mainClass"],
        },
        "artifacts": [
            {
                "coordinates": artifact["coordinates"],
                "path": str(path),
                "sha256": artifact["sha256"],
                "size": artifact["size"],
            }
            for artifact, path in zip(manifest["artifacts"], paths)
        ],
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--provision", action="store_true", help="explicitly download locked inputs")
    mode.add_argument("--offline", action="store_true", help="verify only; never access the network")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--cache-dir", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--timeout", type=float, default=30.0, help="per network/process operation timeout in seconds")
    parser.add_argument("--force", action="store_true", help="with --provision, replace mismatched cache entries")
    parser.add_argument("--print-classpath", action="store_true", help="print the verified local jar classpath")
    parser.add_argument("--json", action="store_true", help="print a machine-readable verification summary")
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.force and not args.provision:
        parser.error("--force is only valid with --provision")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        manifest = load_manifest(args.manifest.resolve())
        cache_dir = args.cache_dir.resolve()
        if args.provision:
            provision(manifest, cache_dir, args.timeout, args.force)
        paths = verify_cache(manifest, cache_dir)
        if args.print_classpath:
            print(os.pathsep.join(str(path) for path in paths))
        if args.json:
            print(json.dumps(_summary(manifest, cache_dir, paths), sort_keys=True, indent=2))
        if not args.print_classpath and not args.json:
            print(f"verified Fabric {manifest['loader']['version']} for Minecraft {manifest['game']['version']} ({len(paths)} jars); offline")
        return 0
    except (RuntimeError_, OSError, ValueError) as exc:
        print(f"fetch_fabric_runtime: ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
