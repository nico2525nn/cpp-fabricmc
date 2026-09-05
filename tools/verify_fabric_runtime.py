#!/usr/bin/env python3
"""Offline integrity and metadata verifier for the pinned Fabric runtime.

This first stage deliberately has no provisioning path.  It imports the lock
and cache verifier, then inspects the jar contents without opening a network
connection.  The optional Knot probe is added below this integrity gate and
also consumes only the already-verified local paths.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import sys
import tempfile
import zipfile
from typing import Any


TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import fetch_fabric_runtime as runtime  # noqa: E402


class VerificationError(RuntimeError):
    """A deterministic offline verification failure."""


class ProbeCommandError(VerificationError):
    def __init__(self, stage: str, command: list[str], stdout: str, stderr: str, reason: str):
        super().__init__(f"{stage}: {reason}")
        self.stage = stage
        self.command = command
        self.stdout = stdout
        self.stderr = stderr
        self.reason = reason


def _jar_bytes(archive: zipfile.ZipFile, member: str, label: str) -> bytes:
    try:
        return archive.read(member)
    except KeyError as exc:
        raise VerificationError(f"{label}: missing required zip member {member}") from exc


def _verify_jar(artifact: dict[str, Any], path: Path) -> None:
    label = artifact["coordinates"]
    try:
        archive = zipfile.ZipFile(path)
    except (OSError, zipfile.BadZipFile) as exc:
        raise VerificationError(f"{label}: cannot open verified jar {path}: {exc}") from exc
    with archive:
        for member in artifact.get("requiredClasses", []):
            _jar_bytes(archive, member, label)
        for member in artifact.get("requiredServices", []):
            data = _jar_bytes(archive, member, label).decode("utf-8")
            if not any(line.strip() and not line.lstrip().startswith("#") for line in data.splitlines()):
                raise VerificationError(f"{label}: service file is empty: {member}")
        for embedded in artifact.get("embedded", []):
            data = _jar_bytes(archive, embedded["path"], label)
            digest = hashlib.sha256(data).hexdigest()
            if len(data) != embedded["size"] or digest != embedded["sha256"]:
                raise VerificationError(
                    f"{label}: embedded {embedded['path']} mismatch: "
                    f"size={len(data)} sha256={digest}; expected "
                    f"size={embedded['size']} sha256={embedded['sha256']}"
                )


def _verify_profile(manifest: dict[str, Any], cache_dir: Path) -> dict[str, Any]:
    profile_path = runtime.profile_path(manifest, cache_dir)
    try:
        profile = json.loads(profile_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise VerificationError(f"profile JSON cannot be read: {profile_path}: {exc}") from exc
    expected = manifest["profile"]
    if not isinstance(profile, dict):
        raise VerificationError(f"profile JSON must be an object: {profile_path}")
    expected_id = f"fabric-loader-{manifest['loader']['version']}-{manifest['game']['version']}"
    if profile.get("id") != expected_id:
        raise VerificationError(f"profile id mismatch: {profile.get('id')!r}")
    if profile.get("mainClass") != expected["mainClass"]:
        raise VerificationError(f"profile mainClass mismatch: {profile.get('mainClass')!r}")
    libraries = profile.get("libraries")
    if not isinstance(libraries, list):
        raise VerificationError("profile libraries is not a list")
    by_name = {entry.get("name"): entry for entry in libraries if isinstance(entry, dict)}
    for artifact in manifest["artifacts"]:
        entry = by_name.get(artifact["coordinates"])
        if entry is None:
            raise VerificationError(f"profile does not list locked artifact {artifact['coordinates']}")
        # Fabric's profile omits checksums for the loader and intermediary;
        # where it supplies them, require exact agreement with our lock.
        if "sha256" in entry and entry["sha256"] != artifact["sha256"]:
            raise VerificationError(f"profile SHA-256 disagrees with lock for {artifact['coordinates']}")
        if "size" in entry and entry["size"] != artifact["size"]:
            raise VerificationError(f"profile size disagrees with lock for {artifact['coordinates']}")
    return profile


def verify_offline(manifest_path: Path, cache_dir: Path) -> tuple[dict[str, Any], list[Path]]:
    manifest = runtime.load_manifest(manifest_path.resolve())
    paths = runtime.verify_cache(manifest, cache_dir.resolve())
    _verify_profile(manifest, cache_dir.resolve())
    for artifact in manifest["artifacts"]:
        _verify_jar(artifact, runtime.artifact_path(manifest, cache_dir.resolve(), artifact))
    return manifest, paths


def _run_command(command: list[str], stage: str, cwd: Path, timeout: float) -> tuple[str, str]:
    try:
        process = subprocess.Popen(
            command,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=True,
        )
    except OSError as exc:
        raise ProbeCommandError(stage, command, "", "", f"could not start command: {exc}") from exc
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        stdout, stderr = process.communicate()
        raise ProbeCommandError(stage, command, stdout, stderr, f"timed out after {timeout}s")
    if process.returncode != 0:
        raise ProbeCommandError(stage, command, stdout, stderr, f"exit code {process.returncode}")
    return stdout, stderr


def _classpath(paths: list[Path]) -> str:
    return os.pathsep.join(str(path) for path in paths)


def _canonical(
    text: str,
    work_dir: Path,
    shadow_classes: Path,
    game_dir: Path,
    cache_dir: Path | None = None,
) -> str:
    value = (
        text.replace(str(work_dir), "<probe-workdir>")
        .replace(str(shadow_classes), "<shadow-classes>")
        .replace(str(game_dir), "<game-dir>")
    )
    if cache_dir is not None:
        value = value.replace(str(cache_dir), "<fabric-runtime-cache>")
    return value


def _stage_shadow_classes(source: Path, destination: Path) -> None:
    """Exclude duplicate official API/Mixin stubs from the probe class path."""
    for item in source.rglob("*"):
        relative = item.relative_to(source)
        if relative.parts[:2] in (("net", "fabricmc"), ("org", "spongepowered")):
            continue
        target = destination / relative
        if item.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        elif item.is_file():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, target)


def _probe_evidence(
    manifest: dict[str, Any],
    status: str,
    stage: str,
    command: list[str] | None = None,
    stdout: str = "",
    stderr: str = "",
    failure_reason: str | None = None,
    observed: list[str] | None = None,
    work_dir: Path | None = None,
    shadow_classes: Path | None = None,
    game_dir: Path | None = None,
    cache_dir: Path | None = None,
) -> dict[str, Any]:
    def clean(value: str) -> str:
        if work_dir is None or shadow_classes is None or game_dir is None:
            return value
        return _canonical(value, work_dir, shadow_classes, game_dir, cache_dir)

    evidence: dict[str, Any] = {
        "schema": 1,
        "status": status,
        "stage": stage,
        "game": manifest["game"],
        "loader": manifest["loader"],
        "profile": {
            "sha256": manifest["profile"]["sha256"],
            "size": manifest["profile"]["size"],
            "mainClass": manifest["profile"]["mainClass"],
        },
        "artifacts": [
            {"coordinates": item["coordinates"], "sha256": item["sha256"], "size": item["size"]}
            for item in manifest["artifacts"]
        ],
        "officialProvider": "cppfm.vendor.fabric.CppfmGameProvider",
        "observedMarkers": observed or [],
        "limitations": [
            "The built-in MinecraftGameProvider was not used because it requires a Mojang server.jar.",
            "The adapter targets Loader 0.16.9's internal GameProvider API and must be re-probed on Loader changes.",
            "This does not establish arbitrary Fabric mod, client/GUI, RNG, long-running, or in-process C++ compatibility.",
        ],
    }
    if command is not None:
        evidence["command"] = [clean(part) for part in command]
    if stdout:
        evidence["stdout"] = clean(stdout)
    if stderr:
        evidence["stderr"] = clean(stderr)
    if failure_reason is not None:
        evidence["failureReason"] = failure_reason
    return evidence


def _write_evidence(path: Path, evidence: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(evidence, sort_keys=True, indent=2) + "\n", encoding="utf-8")


def run_probe(
    manifest: dict[str, Any],
    artifact_paths: list[Path],
    shadow_classes: Path,
    evidence_path: Path,
    timeout: float,
    keep_workdir: bool,
    cache_dir: Path | None = None,
) -> dict[str, Any]:
    shadow_classes = shadow_classes.resolve()
    if not shadow_classes.is_dir():
        evidence = _probe_evidence(
            manifest,
            "failed",
            "preflight",
            failure_reason=f"shadow class directory does not exist: {shadow_classes}",
        )
        _write_evidence(evidence_path, evidence)
        raise VerificationError(evidence["failureReason"])

    by_coordinates = {artifact["coordinates"]: path for artifact, path in zip(manifest["artifacts"], artifact_paths)}
    loader = by_coordinates["net.fabricmc:fabric-loader:0.16.9"]
    mixin = by_coordinates["net.fabricmc:sponge-mixin:0.15.4+mixin.0.8.7"]
    asm_paths = [path for artifact, path in zip(manifest["artifacts"], artifact_paths) if artifact["coordinates"].startswith("org.ow2.asm:")]
    intermediary = by_coordinates["net.fabricmc:intermediary:1.21.4"]
    provider_root = TOOLS.parent / "jvm/vendor/provider"
    probe_root = TOOLS.parent / "jvm/vendor/probe"
    provider_sources = sorted((provider_root / "src").rglob("*.java"))
    probe_sources = sorted((probe_root / "src").rglob("*.java"))
    if not provider_sources or not probe_sources:
        raise VerificationError("probe source tree is incomplete")

    work_dir = Path(tempfile.mkdtemp(prefix="cppfm-official-probe-"))
    game_dir = work_dir / "game"
    probe_shadow_classes = work_dir / "shadow-classes"
    provider_classes = work_dir / "provider-classes"
    probe_classes = work_dir / "probe-classes"
    provider_jar = work_dir / "cppfm-provider.jar"
    probe_jar = work_dir / "cppfm-probe.jar"
    try:
        provider_classes.mkdir()
        probe_classes.mkdir()
        game_dir.mkdir()
        probe_shadow_classes.mkdir()
        _stage_shadow_classes(shadow_classes, probe_shadow_classes)
        provider_classpath = [loader]
        _run_command(
            [
                "javac",
                "--release",
                "17",
                "-encoding",
                "UTF-8",
                "-cp",
                _classpath(provider_classpath),
                "-d",
                str(provider_classes),
                *(str(source) for source in provider_sources),
            ],
            "compile-provider",
            work_dir,
            timeout,
        )
        _run_command(
            [
                "jar",
                "--create",
                "--file",
                str(provider_jar),
                "-C",
                str(provider_classes),
                ".",
                "-C",
                str(provider_root),
                "META-INF/services/net.fabricmc.loader.impl.game.GameProvider",
            ],
            "package-provider",
            work_dir,
            timeout,
        )

        probe_classpath = [loader, mixin, *asm_paths]
        _run_command(
            [
                "javac",
                "--release",
                "17",
                "-encoding",
                "UTF-8",
                "-cp",
                _classpath(probe_classpath),
                "-d",
                str(probe_classes),
                *(str(source) for source in probe_sources),
            ],
            "compile-probe-mod",
            work_dir,
            timeout,
        )
        _run_command(
            [
                "jar",
                "--create",
                "--file",
                str(probe_jar),
                "-C",
                str(probe_classes),
                ".",
                "-C",
                str(probe_root / "resources"),
                "fabric.mod.json",
                "-C",
                str(probe_root / "resources"),
                "cppfm_official_probe.mixins.json",
            ],
            "package-probe-mod",
            work_dir,
            timeout,
        )
        mods = game_dir / "mods"
        mods.mkdir()
        shutil.copy2(probe_jar, mods / probe_jar.name)

        java_classpath = [provider_jar, loader, mixin, *asm_paths, intermediary]
        command = [
            "java",
            "-cp",
            _classpath(java_classpath),
            "-Dfabric.development=true",
            "-Dfabric.skipMcProvider=true",
            "-Dcppfm.game-provider=shadow",
            f"-Dcppfm.shadow-classes={probe_shadow_classes}",
            f"-Dcppfm.game-dir={game_dir}",
            "-Dcppfm.provider.entrypoint=cppfm.vendor.fabric.ShadowMain",
            "-Dmixin.env.remapRefMap=false",
            "-Dmixin.env.disableRefMap=true",
            "cppfm.vendor.fabric.CppfmKnotLauncher",
            "--gameDir",
            str(game_dir),
        ]
        stdout, stderr = _run_command(command, "launch-knot-provider", work_dir, timeout)
        combined = stdout + "\n" + stderr
        expected_markers = [
            "CPPFM_OFFICIAL_PROVIDER_SELECTED",
            "CPPFM_OFFICIAL_LOADER_BOOTSTRAPPED",
            "CPPFM_OFFICIAL_PROVIDER=cppfm.vendor.fabric.CppfmGameProvider",
            "CPPFM_OFFICIAL_MOD_ENTRYPOINT",
            "CPPFM_OFFICIAL_MIXIN_RETURN",
            "CPPFM_OFFICIAL_SHADOW_TARGET",
            "CPPFM_OFFICIAL_ENTRYPOINTS_DONE",
        ]
        missing = [marker for marker in expected_markers if marker not in combined]
        if missing:
            evidence = _probe_evidence(
                manifest,
                "failed",
                "launch-knot-provider",
                command,
                stdout,
                stderr,
                f"process exited successfully but expected markers were absent: {', '.join(missing)}",
                [marker for marker in expected_markers if marker in combined],
                work_dir,
                probe_shadow_classes,
                game_dir,
                cache_dir,
            )
            _write_evidence(evidence_path, evidence)
            raise VerificationError(evidence["failureReason"])
        evidence = _probe_evidence(
            manifest,
            "passed",
            "launch-knot-provider",
            command,
            stdout,
            stderr,
            observed=expected_markers,
            work_dir=work_dir,
            shadow_classes=probe_shadow_classes,
            game_dir=game_dir,
            cache_dir=cache_dir,
        )
        _write_evidence(evidence_path, evidence)
        return evidence
    except ProbeCommandError as exc:
        evidence = _probe_evidence(
            manifest,
            "failed",
            exc.stage,
            exc.command,
            exc.stdout,
            exc.stderr,
            exc.reason,
            work_dir=work_dir,
            shadow_classes=probe_shadow_classes,
            game_dir=game_dir,
            cache_dir=cache_dir,
        )
        _write_evidence(evidence_path, evidence)
        raise VerificationError(evidence["failureReason"]) from exc
    finally:
        if not keep_workdir:
            shutil.rmtree(work_dir, ignore_errors=True)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline", action="store_true", help="explicitly document that this command never uses the network")
    parser.add_argument("--manifest", type=Path, default=runtime.DEFAULT_MANIFEST)
    parser.add_argument("--cache-dir", type=Path, default=runtime.DEFAULT_CACHE)
    parser.add_argument("--json", action="store_true", help="print a machine-readable verification summary")
    parser.add_argument("--probe", action="store_true", help="compile and run the offline Knot/GameProvider/Mixin probe")
    parser.add_argument("--shadow-classes", type=Path, default=TOOLS.parent / "build/jvm/classes")
    parser.add_argument("--evidence", type=Path, default=TOOLS.parent / "jvm/vendor/probe-evidence.json")
    parser.add_argument("--timeout", type=float, default=60.0, help="timeout per javac/jar/java operation in seconds")
    parser.add_argument("--keep-workdir", action="store_true", help="retain temporary probe build files for debugging")
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        manifest, paths = verify_offline(args.manifest, args.cache_dir)
        if args.probe:
            evidence = run_probe(
                manifest,
                paths,
                args.shadow_classes,
                args.evidence,
                args.timeout,
                args.keep_workdir,
                args.cache_dir.resolve(),
            )
            print(f"probe {evidence['status']}; evidence={args.evidence.resolve()}")
            return 0 if evidence["status"] == "passed" else 2
        summary = runtime._summary(manifest, args.cache_dir.resolve(), paths)
        summary["jarContent"] = "required classes, services, embedded payloads, and profile library checks passed"
        if args.json:
            print(json.dumps(summary, sort_keys=True, indent=2))
        else:
            print(f"verified metadata/hash/classes for Fabric {manifest['loader']['version']} ({len(paths)} jars); offline")
        return 0
    except (runtime.RuntimeError_, VerificationError, OSError, ValueError) as exc:
        print(f"verify_fabric_runtime: ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
