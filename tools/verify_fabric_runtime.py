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
import threading
import time
import zipfile
from typing import Any, NamedTuple


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


class ProbeBundle(NamedTuple):
    work_dir: Path
    game_dir: Path
    shadow_classes: Path
    provider_jar: Path
    probe_jar: Path


EXTERNAL_PROBE_MARKERS = (
    "CPPFM_OFFICIAL_PROVIDER_SELECTED",
    "CPPFM_OFFICIAL_LOADER_BOOTSTRAPPED",
    "CPPFM_OFFICIAL_PROVIDER=cppfm.vendor.fabric.CppfmGameProvider",
    "CPPFM_OFFICIAL_TARGET_CONTEXT classLoader=net.fabricmc.loader.impl.launch.knot.KnotClassLoader",
    "CPPFM_OFFICIAL_MAPPING namespace=named",
    "CPPFM_OFFICIAL_MOD_ENTRYPOINT",
    "CPPFM_OFFICIAL_MIXIN_RETURN value=0",
    "CPPFM_OFFICIAL_SHADOW_TARGET classLoader=net.fabricmc.loader.impl.launch.knot.KnotClassLoader",
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
    "CPPFM_OFFICIAL_EVENT_BRIDGE_READY classLoader=net.fabricmc.loader.impl.launch.knot.KnotClassLoader",
    "CPPFM_OFFICIAL_ENTRYPOINTS_DONE",
    "embedded HotSpot started",
    "[cppfm] stopped cleanly",
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
    value = value.replace(str(TOOLS.parent.resolve()), "<repo>")
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


def _build_probe_bundle(
    manifest: dict[str, Any],
    artifact_paths: list[Path],
    shadow_classes: Path,
    work_dir: Path,
    timeout: float,
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
    probe_sources = sorted((probe_root / "src").rglob("*.java"))
    if not provider_sources or not probe_sources:
        raise VerificationError("probe source tree is incomplete")

    game_dir = work_dir / "game"
    probe_shadow_classes = work_dir / "shadow-classes"
    provider_classes = work_dir / "provider-classes"
    probe_classes = work_dir / "probe-classes"
    provider_jar = work_dir / "cppfm-provider.jar"
    probe_jar = work_dir / "cppfm-probe.jar"
    provider_classes.mkdir()
    probe_classes.mkdir()
    game_dir.mkdir()
    probe_shadow_classes.mkdir()
    _stage_shadow_classes(shadow_classes, probe_shadow_classes)

    _run_command(
        [
            "javac",
            "--release",
            "17",
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
    mods = game_dir / "mods"
    mods.mkdir()
    shutil.copy2(probe_jar, mods / probe_jar.name)
    return ProbeBundle(work_dir, game_dir, probe_shadow_classes, provider_jar, probe_jar)


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

    work_dir = Path(tempfile.mkdtemp(prefix="cppfm-official-probe-"))
    game_dir = work_dir / "game"
    probe_shadow_classes = work_dir / "shadow-classes"
    try:
        bundle = _build_probe_bundle(
            manifest, artifact_paths, shadow_classes, work_dir, timeout
        )
        game_dir = bundle.game_dir
        probe_shadow_classes = bundle.shadow_classes
        provider_jar = bundle.provider_jar
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
        expected_markers = list(EXTERNAL_PROBE_MARKERS)
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
    """Stop and reap only the server process created by this verifier."""
    if process.poll() is not None:
        return
    process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=max(1.0, min(15.0, timeout)))
    except subprocess.TimeoutExpired:
        process.kill()
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
        marker for marker in ("Knot callback failed", "strict JVM startup failed")
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
    markers = (*EMBEDDED_PROBE_MARKERS, "Knot callback failed", "strict JVM startup failed")
    return "\n".join(
        line for line in output.splitlines() if any(marker in line for marker in markers)
    )


def run_embedded(
    manifest: dict[str, Any],
    artifact_paths: list[Path],
    binary: Path,
    classes: Path,
    evidence_path: Path,
    timeout: float,
    keep_workdir: bool,
    cache_dir: Path,
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
        bundle = _build_probe_bundle(manifest, artifact_paths, classes, work_dir, timeout)
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
        environment = os.environ.copy()
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
            EMBEDDED_PROBE_MARKERS,
        )
        evidence = _probe_evidence(
            manifest,
            "passed",
            "launch-embedded-cpp",
            command,
            _embedded_evidence_output(output),
            mode="embedded-cpp",
            observed=list(EMBEDDED_PROBE_MARKERS),
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
    parser.add_argument("--shadow-classes", type=Path, default=TOOLS.parent / "build/jvm/classes")
    parser.add_argument(
        "--embedded-binary",
        type=Path,
        help="run the production C++ embedded HotSpot/official Knot gate",
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
