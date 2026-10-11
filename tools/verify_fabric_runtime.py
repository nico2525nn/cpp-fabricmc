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
import io
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import zipfile
from typing import Any, NamedTuple


TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import fetch_fabric_runtime as runtime  # noqa: E402
import fetch_yarn_mappings as yarn  # noqa: E402


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


def _output_text(value: str | bytes | None) -> str:
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return value or ""


def _collect_after_kill(process: subprocess.Popen[str]) -> tuple[str, str]:
    """Collect bounded output after the process group has been terminated.

    A descendant that inherited a pipe can keep ``communicate()`` open even
    after the Popen leader is gone.  The old unbounded call here was the source
    of the outer timeout appearing to crash; closing the streams after a second
    bounded wait makes cleanup finite and preserves whatever output is known.
    """
    try:
        stdout, stderr = process.communicate(timeout=5.0)
        return _output_text(stdout), _output_text(stderr)
    except subprocess.TimeoutExpired as exc:
        stdout = _output_text(exc.stdout)
        stderr = _output_text(exc.stderr)
        for stream in (process.stdout, process.stderr):
            if stream is not None:
                stream.close()
        try:
            process.wait(timeout=1.0)
        except subprocess.TimeoutExpired:
            pass
        return stdout, stderr


class ProbeBundle(NamedTuple):
    work_dir: Path
    game_dir: Path
    shadow_classes: Path
    provider_jar: Path
    probe_jar: Path
    fabric_api_module: Path | None
    fabric_networking_module: Path | None
    networking_probe: bool
    yarn_mappings_jar: Path


EXTERNAL_PROBE_MARKERS = (
    "CPPFM_OFFICIAL_PROVIDER_SELECTED",
    "CPPFM_OFFICIAL_LOADER_BOOTSTRAPPED",
    "CPPFM_OFFICIAL_PROVIDER=cppfm.vendor.fabric.CppfmGameProvider",
    "CPPFM_OFFICIAL_TARGET_CONTEXT classLoader=net.fabricmc.loader.impl.launch.knot.KnotClassLoader",
    "CPPFM_OFFICIAL_MAPPING namespace=named",
    "CPPFM_OFFICIAL_MOD_ENTRYPOINT",
    "CPPFM_OFFICIAL_MIXIN_RETURN value=0",
    "CPPFM_OFFICIAL_SHADOW_TARGET classLoader=net.fabricmc.loader.impl.launch.knot.KnotClassLoader",
    "CPPFM_FABRIC_API_DEPENDENT_FIXTURE_LOADED",
    "CPPFM_FABRIC_API_EVENT_FUNCTIONAL",
    "CPPFM_OFFICIAL_ENTRYPOINTS_DONE",
)

EMBEDDED_PROBE_MARKERS = (
    "CPPFM_OFFICIAL_PROVIDER_SELECTED",
    "CPPFM_OFFICIAL_LOADER_BOOTSTRAPPED",
    "CPPFM_OFFICIAL_PROVIDER=cppfm.vendor.fabric.CppfmGameProvider",
    "CPPFM_OFFICIAL_TARGET_CONTEXT classLoader=net.fabricmc.loader.impl.launch.knot.KnotClassLoader",
    "CPPFM_OFFICIAL_MAPPING namespace=named",
    "CPPFM_OFFICIAL_INPROCESS_HANDOFF targetClassLoader=net.fabricmc.loader.impl.launch.knot.KnotClassLoader bridgeClassLoader=net.fabricmc.loader.impl.launch.knot.KnotClassLoader",
    "CPPFM_OFFICIAL_MOD_ENTRYPOINT",
    "CPPFM_OFFICIAL_MIXIN_RETURN value=0",
    "CPPFM_OFFICIAL_SHADOW_TARGET classLoader=net.fabricmc.loader.impl.launch.knot.KnotClassLoader",
    "CPPFM_FABRIC_API_DEPENDENT_FIXTURE_LOADED",
    "CPPFM_FABRIC_API_EVENT_FUNCTIONAL",
    "CPPFM_OFFICIAL_EVENT_BRIDGE_READY classLoader=net.fabricmc.loader.impl.launch.knot.KnotClassLoader",
    "CPPFM_OFFICIAL_ENTRYPOINTS_DONE",
    "embedded HotSpot started",
    "[cppfm] server loop ready",
    "[cppfm] first server tick completed",
    "[cppfm] stopped cleanly",
)

FABRIC_NETWORKING_PROBE_MARKER = "CPPFM_FABRIC_API_NETWORKING_FUNCTIONAL"
FABRIC_NETWORKING_PROBE_MARKERS = (
    "CPPFM_FABRIC_API_PLAY_NETWORKING_FUNCTIONAL",
    "CPPFM_FABRIC_API_CONFIGURATION_NETWORKING_FUNCTIONAL",
    FABRIC_NETWORKING_PROBE_MARKER,
)


def _runtime_library_paths(libraries_dir: Path | None) -> list[Path]:
    if libraries_dir is None:
        return []
    root = libraries_dir.resolve()
    if not root.is_dir():
        raise VerificationError(f"Minecraft runtime libraries directory is missing: {root}")
    return sorted(
        path for path in root.iterdir()
        if (path.is_dir() or (path.is_file() and path.suffix == ".jar"))
        and not (path.is_file() and
                 (path.name.lower().startswith("asm-") or "-asm-" in path.name.lower()))
    )


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
        stdout, stderr = _collect_after_kill(process)
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
    value = value.replace(str(TOOLS.parent.resolve()), "<repo>")
    return value


def _fabric_api_class_names(*modules: Path | None) -> set[str]:
    """Return the API/implementation classes physically supplied by a real module."""
    result: set[str] = set()
    for module in modules:
        if module is None:
            continue
        try:
            with zipfile.ZipFile(module) as archive:
                result.update(
                    name
                    for name in archive.namelist()
                    if name.endswith(".class")
                    and name.startswith((
                        "net/fabricmc/fabric/api/",
                        "net/fabricmc/fabric/impl/",
                    ))
                )
        except (OSError, zipfile.BadZipFile) as exc:
            raise VerificationError(f"cannot inspect Fabric API module {module}: {exc}") from exc
    return result


def _stage_shadow_classes(
    source: Path, destination: Path, official_fabric_classes: set[str] | None = None
) -> None:
    """Stage local game/Fabric classes without shadowing the official loader."""
    official_fabric_classes = official_fabric_classes or set()
    for item in source.rglob("*"):
        relative = item.relative_to(source)
        # The probe supplies Loader and Mixin from their pinned official jars.
        # Fabric API and cppfm's internal lifecycle interfaces are local
        # shadow classes, however; excluding the whole net.fabricmc tree makes
        # a Minecraft shadow class fail to link as soon as it implements one.
        if relative.parts[:3] in (("net", "fabricmc", "loader"), ("net", "fabricmc", "api")) \
                or relative.parts[:2] == ("org", "spongepowered"):
            continue
        if relative.as_posix() in official_fabric_classes:
            continue
        target = destination / relative
        if item.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        elif item.is_file():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, target)


def _extract_fabric_api_module(
    artifact: Path, work_dir: Path, module_id: str,
    required_class: str, output_name: str,
) -> Path:
    """Stage one genuine Fabric API module from a module or aggregate jar."""
    if not artifact.is_file():
        raise VerificationError(f"Fabric API jar does not exist: {artifact}")

    def is_api_base(data: bytes) -> bool:
        try:
            with zipfile.ZipFile(io.BytesIO(data)) as module:
                if required_class not in module.namelist():
                    return False
                metadata = json.loads(module.read("fabric.mod.json"))
        except (KeyError, UnicodeDecodeError, json.JSONDecodeError, zipfile.BadZipFile) as exc:
            raise VerificationError(f"invalid Fabric API module in {artifact}: {exc}") from exc
        if metadata.get("id") != module_id:
            raise VerificationError(
                f"{artifact} contains {required_class} outside {module_id}"
            )
        return True

    try:
        contents = artifact.read_bytes()
        selected = contents if is_api_base(contents) else None
        if selected is None:
            with zipfile.ZipFile(io.BytesIO(contents)) as aggregate:
                for member in sorted(aggregate.namelist()):
                    if not member.startswith("META-INF/jars/") or not member.endswith(".jar"):
                        continue
                    nested = aggregate.read(member)
                    if is_api_base(nested):
                        selected = nested
                        break
    except (OSError, zipfile.BadZipFile) as exc:
        raise VerificationError(f"cannot read Fabric API jar {artifact}: {exc}") from exc

    if selected is None:
        raise VerificationError(
            f"{artifact} is neither fabric-api-base nor an aggregate containing it"
        )
    module_path = work_dir / output_name
    module_path.write_bytes(selected)
    return module_path


def _extract_fabric_api_base(artifact: Path, work_dir: Path) -> Path:
    return _extract_fabric_api_module(
        artifact, work_dir, "fabric-api-base",
        "net/fabricmc/fabric/api/event/Event.class", "fabric-api-base.jar")


def _extract_fabric_networking_api(artifact: Path, work_dir: Path) -> Path | None:
    try:
        return _extract_fabric_api_module(
            artifact, work_dir, "fabric-networking-api-v1",
            "net/fabricmc/fabric/api/networking/v1/ServerPlayNetworking.class",
            "fabric-networking-api-v1.jar")
    except VerificationError as exc:
        if "is neither fabric-networking-api-v1 nor an aggregate containing it" in str(exc):
            return None
        raise


def _without_module_mixins(module: Path, destination: Path, module_id: str) -> Path:
    """Disable game-target mixins for API-only probes using the shadow provider."""
    try:
        with zipfile.ZipFile(module) as source:
            metadata = json.loads(source.read("fabric.mod.json"))
            if not isinstance(metadata, dict) or metadata.get("id") != module_id:
                raise VerificationError(f"unexpected Fabric API module metadata in {module}")
            if "mixins" not in metadata:
                raise VerificationError(f"{module_id} declares no mixin configs to isolate")
            metadata.pop("mixins")
            with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as output:
                for entry in source.infolist():
                    content = source.read(entry.filename)
                    if entry.filename == "fabric.mod.json":
                        content = (json.dumps(metadata, indent=2) + "\n").encode("utf-8")
                    output.writestr(entry, content)
    except (OSError, KeyError, UnicodeDecodeError, json.JSONDecodeError,
            zipfile.BadZipFile) as exc:
        raise VerificationError(f"cannot isolate {module_id} mixin configs: {exc}") from exc
    return destination


def _package_nested_api_module(base_module: Path, networking_module: Path | None,
                               destination: Path) -> Path:
    """Wrap the genuine module in a minimal Fabric nested-mod container.

    A full Fabric API aggregate also declares a Minecraft dependency. The
    focused Knot classpath test needs only the genuine API module, so this
    wrapper exercises Loader's nested-jar path without requiring a vanilla
    game provider that this C++ shadow provider deliberately does not claim.
    """
    metadata = {
        "schemaVersion": 1,
        "id": "cppfm_fabric_api_nested_container",
        "version": "1.0.0",
        "name": "CppFabricMC Nested Fabric API Probe Container",
        "environment": "server",
        "depends": {"fabricloader": ">=0.16.9"},
        "jars": [{"file": "META-INF/jars/fabric-api-base.jar"}],
    }
    if networking_module is not None:
        metadata["jars"].append(
            {"file": "META-INF/jars/fabric-networking-api-v1.jar"})
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("META-INF/MANIFEST.MF", "Manifest-Version: 1.0\n\n")
        archive.writestr(
            "fabric.mod.json",
            json.dumps(metadata, indent=2) + "\n",
        )
        archive.write(base_module, "META-INF/jars/fabric-api-base.jar")
        if networking_module is not None:
            archive.write(networking_module,
                          "META-INF/jars/fabric-networking-api-v1.jar")
    return destination


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
    mode: str = "offline-process-probe",
) -> dict[str, Any]:
    def clean(value: str) -> str:
        if work_dir is None or shadow_classes is None or game_dir is None:
            return value
        return _canonical(value, work_dir, shadow_classes, game_dir, cache_dir)

    if mode == "embedded-cpp":
        limitations = [
            "The built-in MinecraftGameProvider was not used because it requires a Mojang server.jar.",
            "The adapter targets Loader 0.16.9's internal GameProvider API and must be re-probed on Loader changes.",
            "This embedded gate proves one C++-owned HotSpot can hand the native bridge to the official Knot target loader and run the fixture Mixin; it does not establish arbitrary Fabric mod compatibility.",
            "It does not establish official client/GUI behavior, vanilla RNG parity, 24-hour operation, or Mojang server.jar compatibility.",
        ]
    else:
        limitations = [
            "The built-in MinecraftGameProvider was not used because it requires a Mojang server.jar.",
            "The adapter targets Loader 0.16.9's internal GameProvider API and must be re-probed on Loader changes.",
            "This does not establish arbitrary Fabric mod, client/GUI, RNG, long-running, or in-process C++ compatibility.",
        ]
    if observed and FABRIC_NETWORKING_PROBE_MARKER in observed:
        limitations.append(
            "The networking check uses the genuine fabric-networking-api-v1 classes and registration API, but strips that module's mixin declarations because this shadow-provider probe does not supply vanilla Minecraft method bodies; it does not test Minecraft packet interception or the module's connection mixins."
        )
    evidence: dict[str, Any] = {
        "schema": 1,
        "status": status,
        "stage": stage,
        "mode": mode,
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
        "limitations": limitations,
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


def _verified_yarn_mappings(cache_dir: Path) -> Path:
    """Return the pinned intermediary-to-named mapping required by Knot."""
    try:
        lock = yarn.load_lock(yarn.DEFAULT_LOCK.resolve())
        mapping = lock["mapping"]
        root = cache_dir.resolve()
        path = yarn.cache_path(root, str(mapping["cachePath"]))
        yarn.verify_file(path, int(mapping["size"]), str(mapping["sha256"]),
                         "Yarn mapping artifact")
        yarn.verify_alignment(lock, root, path)
        return path
    except (yarn.MappingError, OSError, KeyError, TypeError, ValueError) as exc:
        raise VerificationError(f"pinned Yarn mappings are unavailable or invalid: {exc}") from exc


def _build_probe_bundle(
    manifest: dict[str, Any],
    artifact_paths: list[Path],
    shadow_classes: Path,
    work_dir: Path,
    timeout: float,
    fabric_api_jar: Path | None = None,
    yarn_mappings_jar: Path | None = None,
    libraries_dir: Path | None = None,
) -> ProbeBundle:
    """Compile the provider and fixture mod into one disposable bundle.

    Both official-stack modes use this exact bundle.  The process probe puts
    the staged shadow path directly on Java's classpath; the embedded mode
    gives the original shadow classes to KnotLauncher, which performs its own
    in-process staging.  Keeping compilation shared prevents the two gates
    from quietly testing different provider or Mixin fixtures.
    """
    shadow_classes = shadow_classes.resolve()
    if not shadow_classes.is_dir():
        raise VerificationError(f"shadow class directory does not exist: {shadow_classes}")
    if yarn_mappings_jar is None or not yarn_mappings_jar.is_file():
        raise VerificationError("pinned Yarn mappings are required for the official Knot probe")

    by_coordinates = {
        artifact["coordinates"]: path
        for artifact, path in zip(manifest["artifacts"], artifact_paths)
    }
    loader = by_coordinates["net.fabricmc:fabric-loader:0.16.9"]
    mixin = by_coordinates["net.fabricmc:sponge-mixin:0.15.4+mixin.0.8.7"]
    asm_paths = [
        path for artifact, path in zip(manifest["artifacts"], artifact_paths)
        if artifact["coordinates"].startswith("org.ow2.asm:")
    ]
    provider_root = TOOLS.parent / "jvm/vendor/provider"
    probe_root = TOOLS.parent / "jvm/vendor/probe"
    provider_sources = sorted((provider_root / "src").rglob("*.java"))
    probe_sources = sorted(
        path for path in (probe_root / "src").rglob("*.java")
        if path.name not in (
            "FabricApiDependentMod.java",
            "FabricNetworkingDependentMod.java",
        )
    )
    if not provider_sources or not probe_sources:
        raise VerificationError("probe source tree is incomplete")

    game_dir = work_dir / "game"
    probe_shadow_classes = work_dir / "shadow-classes"
    provider_classes = work_dir / "provider-classes"
    probe_classes = work_dir / "probe-classes"
    provider_jar = work_dir / "cppfm-provider.jar"
    probe_jar = work_dir / "cppfm-probe.jar"
    fabric_api_module = (
        _extract_fabric_api_base(fabric_api_jar, work_dir)
        if fabric_api_jar is not None else None
    )
    runtime_libraries = _runtime_library_paths(libraries_dir)
    fabric_networking_module = None
    if fabric_api_jar is not None and libraries_dir is not None:
        fabric_networking_module = _extract_fabric_networking_api(
            fabric_api_jar, work_dir)
        if fabric_networking_module is not None:
            fabric_networking_module = _without_module_mixins(
                fabric_networking_module,
                work_dir / "fabric-networking-api-v1-api-only.jar",
                "fabric-networking-api-v1",
            )
    networking_probe = fabric_networking_module is not None
    official_fabric_classes = _fabric_api_class_names(
        fabric_api_module, fabric_networking_module)
    provider_classes.mkdir()
    probe_classes.mkdir()
    game_dir.mkdir()
    probe_shadow_classes.mkdir()
    _stage_shadow_classes(shadow_classes, probe_shadow_classes, official_fabric_classes)

    _run_command(
        [
            "javac",
            "--release",
            "17",
            "-proc:none",
            "-encoding",
            "UTF-8",
            "-cp",
            _classpath([loader]),
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

    _run_command(
        [
            "javac",
            "--release",
            "17",
            "-proc:none",
            "-encoding",
            "UTF-8",
            "-cp",
            _classpath([loader, mixin, *asm_paths]),
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
    api_fixture_source = probe_root / "src/cppfm/vendor/probe/FabricApiDependentMod.java"
    api_fixture_classes = work_dir / "fabric-api-dependent-classes"
    api_fixture_jar = work_dir / "cppfm-fabric-api-dependent-probe.jar"
    api_fixture_classes.mkdir()
    api_compile_classpath = [loader]
    if fabric_api_module is not None:
        api_compile_classpath.append(fabric_api_module)
    api_compile_classpath.extend(runtime_libraries)
    api_compile_classpath.append(probe_shadow_classes)
    _run_command(
        [
            "javac",
            "--release",
            "17",
            "-proc:none",
            "-encoding",
            "UTF-8",
            "-cp",
            _classpath(api_compile_classpath),
            "-d",
            str(api_fixture_classes),
            str(api_fixture_source),
        ],
        "compile-fabric-api-dependent-probe",
        work_dir,
        timeout,
    )
    if networking_probe:
        networking_fixture_source = probe_root / "src/cppfm/vendor/probe/FabricNetworkingDependentMod.java"
        _run_command(
            [
                "javac",
                "--release",
                "17",
                "-proc:none",
                "-encoding",
                "UTF-8",
                "-cp",
                _classpath([
                    loader, fabric_networking_module, *runtime_libraries,
                    probe_shadow_classes,
                ]),
                "-d",
                str(api_fixture_classes),
                str(networking_fixture_source),
            ],
            "compile-fabric-networking-dependent-probe",
            work_dir,
            timeout,
        )
    api_fixture_resources = work_dir / "fabric-api-dependent-resources"
    api_fixture_resources.mkdir()
    api_fixture_metadata: dict[str, Any] = {
        "schemaVersion": 1,
        "id": "cppfm_fabric_api_dependent_probe",
        "version": "1.0.0",
        "name": "CppFabricMC Fabric API Linkage Probe",
        "environment": "server",
        "entrypoints": {
            "main": ["cppfm.vendor.probe.FabricApiDependentMod"],
        },
        "depends": {"fabricloader": ">=0.16.9"},
    }
    if fabric_api_module is not None:
        api_fixture_metadata["depends"]["fabric-api-base"] = "*"
    if networking_probe:
        api_fixture_metadata["entrypoints"]["main"].append(
            "cppfm.vendor.probe.FabricNetworkingDependentMod")
        api_fixture_metadata["depends"]["fabric-networking-api-v1"] = "*"
    (api_fixture_resources / "fabric.mod.json").write_text(
        json.dumps(api_fixture_metadata, indent=2) + "\n",
        encoding="utf-8",
    )
    _run_command(
        [
            "jar",
            "--create",
            "--file",
            str(api_fixture_jar),
            "-C",
            str(api_fixture_classes),
            ".",
            "-C",
            str(api_fixture_resources),
            "fabric.mod.json",
        ],
        "package-fabric-api-dependent-probe",
        work_dir,
        timeout,
    )
    mods = game_dir / "mods"
    mods.mkdir()
    shutil.copy2(probe_jar, mods / probe_jar.name)
    shutil.copy2(api_fixture_jar, mods / api_fixture_jar.name)
    if fabric_api_module is not None:
        api_container = _package_nested_api_module(
            fabric_api_module, fabric_networking_module,
            work_dir / "cppfm-fabric-api-container.jar"
        )
        shutil.copy2(api_container, mods / api_container.name)
    return ProbeBundle(
        work_dir,
        game_dir,
        probe_shadow_classes,
        provider_jar,
        probe_jar,
        fabric_api_module,
        fabric_networking_module,
        networking_probe,
        yarn_mappings_jar.resolve(),
    )


def run_probe(
    manifest: dict[str, Any],
    artifact_paths: list[Path],
    shadow_classes: Path,
    evidence_path: Path,
    timeout: float,
    keep_workdir: bool,
    cache_dir: Path | None = None,
    fabric_api_jar: Path | None = None,
    libraries_dir: Path | None = None,
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

    work_dir = Path(tempfile.mkdtemp(prefix="cppfm-official-probe-"))
    game_dir = work_dir / "game"
    probe_shadow_classes = work_dir / "shadow-classes"
    try:
        yarn_mappings_jar = _verified_yarn_mappings(cache_dir or runtime.DEFAULT_CACHE)
        bundle = _build_probe_bundle(
            manifest, artifact_paths, shadow_classes, work_dir, timeout,
            fabric_api_jar, yarn_mappings_jar, libraries_dir,
        )
        game_dir = bundle.game_dir
        probe_shadow_classes = bundle.shadow_classes
        provider_jar = bundle.provider_jar
        staged_mods = game_dir / "staged-mods"
        _run_command(
            [
                "java",
                "-cp",
                _classpath([probe_shadow_classes]),
                "cppfm.loader.ModJarStagerMain",
                str(bundle.yarn_mappings_jar),
                str(game_dir / "mods"),
                str(staged_mods),
            ],
            "stage-probe-mods",
            work_dir,
            timeout,
        )
        by_coordinates = {
            artifact["coordinates"]: path
            for artifact, path in zip(manifest["artifacts"], artifact_paths)
        }
        loader = by_coordinates["net.fabricmc:fabric-loader:0.16.9"]
        mixin = by_coordinates["net.fabricmc:sponge-mixin:0.15.4+mixin.0.8.7"]
        asm_paths = [
            path for artifact, path in zip(manifest["artifacts"], artifact_paths)
            if artifact["coordinates"].startswith("org.ow2.asm:")
        ]
        intermediary = by_coordinates["net.fabricmc:intermediary:1.21.4"]
        runtime_libraries = _runtime_library_paths(libraries_dir)

        java_classpath = [provider_jar, loader, mixin, *asm_paths,
                          bundle.yarn_mappings_jar, intermediary, *runtime_libraries]
        command = [
            "java",
            "-cp",
            _classpath(java_classpath),
            "-Dfabric.development=true",
            "-Dfabric.skipMcProvider=true",
            f"-Dfabric.modsFolder={staged_mods}",
            "-Dcppfm.game-provider=shadow",
            f"-Dcppfm.official.fabric-api-present={str(bundle.fabric_api_module is not None).lower()}",
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
            *EXTERNAL_PROBE_MARKERS,
            "CPPFM_FABRIC_API_CLASS_SOURCE="
            + ("official-api" if bundle.fabric_api_module is not None else "shadow-fallback"),
        ]
        if bundle.networking_probe:
            expected_markers.extend(FABRIC_NETWORKING_PROBE_MARKERS)
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


def _stop_owned_process(process: subprocess.Popen[str], timeout: float) -> None:
    """Stop and reap the process group created by this verifier.

    The embedded probe may load a launcher that creates descendants. Signaling
    only the Popen leader can leave descendants holding the output pipe and
    make a caller report a timeout after the leader was reaped.
    """
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=max(1.0, min(15.0, timeout)))
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=5.0)


def _run_embedded_server(
    command: list[str],
    environment: dict[str, str],
    cwd: Path,
    timeout: float,
    required_markers: tuple[str, ...],
) -> str:
    """Run the persistent C++ server until its embedded gate is observable."""
    try:
        process = subprocess.Popen(
            command,
            cwd=cwd,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            start_new_session=True,
        )
    except OSError as exc:
        raise ProbeCommandError(
            "launch-embedded-cpp", command, "", "", f"could not start command: {exc}"
        ) from exc

    ready_markers = tuple(
        marker for marker in required_markers if marker != "[cppfm] stopped cleanly"
    )
    output_lines: list[str] = []

    def drain_output() -> None:
        if process.stdout is None:
            return
        for line in process.stdout:
            output_lines.append(line)

    reader = threading.Thread(target=drain_output, name="cppfm-official-output", daemon=True)
    reader.start()
    timed_out = False
    try:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and process.poll() is None:
            output = "".join(output_lines)
            if all(marker in output for marker in ready_markers):
                break
            time.sleep(0.1)
        else:
            timed_out = process.poll() is None
    finally:
        _stop_owned_process(process, timeout)
        reader.join(timeout=5.0)

    output = "".join(output_lines)
    if timed_out:
        raise ProbeCommandError(
            "launch-embedded-cpp", command, output, "", f"timed out after {timeout}s"
        )
    missing = [marker for marker in required_markers if marker not in output]
    if missing:
        raise ProbeCommandError(
            "launch-embedded-cpp",
            command,
            output,
            "",
            "process stopped but expected markers were absent: " + ", ".join(missing),
        )
    forbidden = [
        marker
        for marker in (
            "Knot callback failed",
            "strict JVM startup failed",
            "[cppfm][jvm][ERROR]",
            "NoClassDefFoundError",
            "Exception in thread \"main\"",
        )
        if marker in output
    ]
    if forbidden:
        raise ProbeCommandError(
            "launch-embedded-cpp",
            command,
            output,
            "",
            "forbidden failure marker observed: " + ", ".join(forbidden),
        )
    if process.returncode != 0:
        raise ProbeCommandError(
            "launch-embedded-cpp",
            command,
            output,
            "",
            f"exit code {process.returncode}",
        )
    return output


def _embedded_evidence_output(output: str) -> str:
    markers = (
        *EMBEDDED_PROBE_MARKERS,
        "CPPFM_FABRIC_API_CLASS_SOURCE=official-api",
        "CPPFM_FABRIC_API_CLASS_SOURCE=shadow-fallback",
        "CPPFM_FABRIC_API_EVENT_FUNCTIONAL",
        "Knot callback failed",
        "strict JVM startup failed",
    )
    diagnostics = (
        "[cppfm][jvm]",
        "[Persistence]",
        "server loop ready",
        "first server tick",
        "CppFabricMC starting",
        "shutdown incomplete",
        "initialization failed",
        "fatal:",
        "stopping tick loop",
        "stopping persistence",
        "stopped cleanly",
        "stopped with unsaved",
        "[cppfm] bye",
    )
    lines = output.splitlines()
    selected = {
        index
        for index, line in enumerate(lines)
        if any(marker in line for marker in markers + diagnostics)
    }
    # Keep recent unprefixed Java stack frames too. Filtering only prefixed
    # marker lines erased the root cause from failed embedded-launch evidence.
    selected.update(range(max(0, len(lines) - 240), len(lines)))
    return "\n".join(line for index, line in enumerate(lines) if index in selected)


def run_embedded(
    manifest: dict[str, Any],
    artifact_paths: list[Path],
    binary: Path,
    classes: Path,
    evidence_path: Path,
    timeout: float,
    keep_workdir: bool,
    cache_dir: Path,
    fabric_api_jar: Path | None = None,
    libraries_dir: Path | None = None,
) -> dict[str, Any]:
    """Prove the production C++ -> HotSpot -> official Knot handoff."""
    binary = binary.resolve()
    classes = classes.resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        evidence = _probe_evidence(
            manifest,
            "failed",
            "preflight",
            failure_reason=f"embedded binary is not executable: {binary}",
            mode="embedded-cpp",
        )
        _write_evidence(evidence_path, evidence)
        raise VerificationError(evidence["failureReason"])
    if not classes.is_dir():
        evidence = _probe_evidence(
            manifest,
            "failed",
            "preflight",
            failure_reason=f"embedded class directory does not exist: {classes}",
            mode="embedded-cpp",
        )
        _write_evidence(evidence_path, evidence)
        raise VerificationError(evidence["failureReason"])

    work_dir = Path(tempfile.mkdtemp(prefix="cppfm-official-embedded-"))
    try:
        yarn_mappings_jar = _verified_yarn_mappings(cache_dir)
        bundle = _build_probe_bundle(
            manifest, artifact_paths, classes, work_dir, timeout,
            fabric_api_jar, yarn_mappings_jar, libraries_dir,
        )
        expected_markers = [
            *EMBEDDED_PROBE_MARKERS,
            "CPPFM_FABRIC_API_CLASS_SOURCE="
            + ("official-api" if bundle.fabric_api_module is not None else "shadow-fallback"),
        ]
        if bundle.networking_probe:
            expected_markers.extend(FABRIC_NETWORKING_PROBE_MARKERS)
        config_dir = bundle.game_dir / "config"
        world_dir = bundle.game_dir / "world"
        command = [
            str(binary),
            "--jvm=true",
            "--jvm-strict=true",
            f"--jvm-classes={classes}",
            f"--jvm-mods={bundle.game_dir / 'mods'}",
            f"--jvm-config={config_dir}",
            f"--world-dir={world_dir}",
            "--port=0",
        ]
        if libraries_dir is not None:
            command.append(f"--jvm-libraries={libraries_dir.resolve()}")
        environment = os.environ.copy()
        jvm_temp_dir = work_dir / "java-tmp"
        jvm_temp_dir.mkdir()
        tool_options = environment.get("JAVA_TOOL_OPTIONS", "").strip()
        temp_option = f"-Djava.io.tmpdir={jvm_temp_dir}"
        environment["JAVA_TOOL_OPTIONS"] = " ".join(
            value for value in (tool_options, temp_option) if value
        )
        environment.update(
            {
                "CPPFM_FABRIC_RUNTIME": str(cache_dir.resolve()),
                "CPPFM_FABRIC_PROVIDER_JAR": str(bundle.provider_jar),
                "CPPFM_FABRIC_GAME_DIR": str(bundle.game_dir),
            }
        )
        output = _run_embedded_server(
            command,
            environment,
            TOOLS.parent,
            timeout,
            expected_markers,
        )
        evidence = _probe_evidence(
            manifest,
            "passed",
            "launch-embedded-cpp",
            command,
            _embedded_evidence_output(output),
            mode="embedded-cpp",
            observed=expected_markers,
            work_dir=work_dir,
            shadow_classes=classes,
            game_dir=bundle.game_dir,
            cache_dir=cache_dir,
        )
        evidence["bridge"] = {
            "hostProcess": "cppfm",
            "jvm": "same HotSpot process",
            "targetClassLoader": "net.fabricmc.loader.impl.launch.knot.KnotClassLoader",
            "mappingNamespace": "named",
            "provider": "cppfm.vendor.fabric.CppfmGameProvider",
        }
        _write_evidence(evidence_path, evidence)
        return evidence
    except ProbeCommandError as exc:
        evidence = _probe_evidence(
            manifest,
            "failed",
            exc.stage,
            exc.command,
            _embedded_evidence_output(exc.stdout),
            exc.stderr,
            exc.reason,
            work_dir=work_dir,
            shadow_classes=classes,
            game_dir=work_dir / "game",
            cache_dir=cache_dir,
            mode="embedded-cpp",
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
    parser.add_argument(
        "--fabric-api-jar",
        type=Path,
        help="also test a genuine fabric-api-base module or Fabric API aggregate jar",
    )
    parser.add_argument("--shadow-classes", type=Path, default=TOOLS.parent / "build/jvm/classes")
    parser.add_argument(
        "--embedded-binary",
        type=Path,
        help="run the production C++ embedded HotSpot/official Knot gate",
    )
    parser.add_argument(
        "--jvm-libraries",
        type=Path,
        help="real Minecraft runtime libraries for either Knot probe (required to exercise many Fabric APIs)",
    )
    parser.add_argument(
        "--embedded-classes",
        type=Path,
        default=TOOLS.parent / "build/jvm/classes",
        help="compiled Java classes supplied to the production C++ gate",
    )
    parser.add_argument("--evidence", type=Path, default=TOOLS.parent / "jvm/vendor/probe-evidence.json")
    parser.add_argument("--timeout", type=float, default=60.0, help="timeout per javac/jar/java operation in seconds")
    parser.add_argument("--keep-workdir", action="store_true", help="retain temporary probe build files for debugging")
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.probe and args.embedded_binary is not None:
        parser.error("--probe and --embedded-binary are separate evidence modes")
    if args.fabric_api_jar is not None and not (args.probe or args.embedded_binary is not None):
        parser.error("--fabric-api-jar requires --probe or --embedded-binary")
    if args.jvm_libraries is not None and args.embedded_binary is None and not args.probe:
        parser.error("--jvm-libraries requires --probe or --embedded-binary")
    if args.jvm_libraries is not None and not args.jvm_libraries.is_dir():
        parser.error(f"--jvm-libraries is not a directory: {args.jvm_libraries}")
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
                args.fabric_api_jar,
                args.jvm_libraries,
            )
            print(f"probe {evidence['status']}; evidence={args.evidence.resolve()}")
            return 0 if evidence["status"] == "passed" else 2
        if args.embedded_binary is not None:
            evidence = run_embedded(
                manifest,
                paths,
                args.embedded_binary,
                args.embedded_classes,
                args.evidence,
                args.timeout,
                args.keep_workdir,
                args.cache_dir.resolve(),
                args.fabric_api_jar,
                args.jvm_libraries,
            )
            print(f"embedded {evidence['status']}; evidence={args.evidence.resolve()}")
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
