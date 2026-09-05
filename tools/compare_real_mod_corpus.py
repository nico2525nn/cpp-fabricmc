#!/usr/bin/env python3
"""Compare unmodified public Fabric 1.21.4 mods on two server runtimes.

The harness deliberately has two separate gates:

* the reference side launches the locked Mojang 1.21.4 server through the
  pinned Fabric Loader/Knot/Mixin runtime; and
* the cppfm side launches the same JARs through cppfm's existing embedded JVM
  command line.

Metadata inspection is not execution success.  A report can be PASS only if
both sides complete load, initialization, a mod-specific behavior probe, and
clean shutdown for every corpus entry and for the combined set.  Missing
offline inputs are reported as SKIP (exit status 2), while an attempted but
unsupported cppfm execution is reported as FAIL (also exit status 1).  This
tool never downloads anything; use ``fetch_real_mod_corpus.py --provision``
explicitly to populate the ignored cache first.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import selectors
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
from typing import Any, Iterable
import zipfile


TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
DEFAULT_LOCK = ROOT / "tests/real_mod_corpus/corpus.lock.json"
DEFAULT_CACHE = ROOT / "build/real-mod-corpus"
DEFAULT_FABRIC_CACHE = ROOT / "build/fabric-runtime"
DEFAULT_REPORT = DEFAULT_CACHE / "real-mod-corpus-report.json"
DEFAULT_EVIDENCE = DEFAULT_CACHE / "evidence"
REPORT_SCHEMA = "cppfm.real-mod-corpus.report.v1"
SUMMARY_SCHEMA = "cppfm.real-mod-corpus.cache.v1"
PHASES = ("load", "initialize", "behavior", "shutdown")
STATUS = {"PASS", "FAIL", "SKIP"}

sys.path.insert(0, str(TOOLS))
import fetch_fabric_runtime as fabric_runtime  # noqa: E402
import fetch_real_mod_corpus as corpus  # noqa: E402


class HarnessError(RuntimeError):
    """A deterministic harness/preflight error."""


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _is_under(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def _display_path(path: Path, cache_dir: Path | None = None) -> str:
    resolved = path.resolve()
    if cache_dir is not None and _is_under(resolved, cache_dir):
        return "<corpus-cache>/" + resolved.relative_to(cache_dir.resolve()).as_posix()
    if _is_under(resolved, ROOT):
        return "<repo>/" + resolved.relative_to(ROOT.resolve()).as_posix()
    return str(resolved)


def _normalize_text(text: str, temp_root: Path | None = None,
                    cache_dir: Path | None = None) -> str:
    value = text
    for path, token in (
        (temp_root, "<run-workdir>"),
        (cache_dir, "<corpus-cache>"),
        (ROOT, "<repo>"),
    ):
        if path is not None:
            value = value.replace(str(path.resolve()), token)
    return value


def _string_list(value: Any) -> list[str]:
    if isinstance(value, list) and all(isinstance(item, str) for item in value):
        return list(value)
    return []


def _normalize_entrypoints(value: Any) -> dict[str, list[str]]:
    if not isinstance(value, dict):
        return {}
    normalized: dict[str, list[str]] = {}
    for key, raw in value.items():
        if isinstance(raw, str):
            normalized[str(key)] = [raw]
        elif isinstance(raw, list) and all(isinstance(item, str) for item in raw):
            normalized[str(key)] = list(raw)
    return normalized


def _entrypoint_class(entrypoint: str) -> str:
    return entrypoint.split("::", 1)[0]


def _mixin_config_info(archive: zipfile.ZipFile, resource: str) -> dict[str, Any]:
    try:
        raw = archive.read(resource)
    except KeyError:
        return {"status": "FAIL", "reason": "resource is missing", "resource": resource}
    try:
        parsed = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        return {"status": "FAIL", "reason": f"invalid JSON: {exc}", "resource": resource}
    if not isinstance(parsed, dict):
        return {"status": "FAIL", "reason": "config root is not an object", "resource": resource}

    class_names: list[str] = []
    counts: dict[str, int] = {}
    for key in ("mixins", "common", "server", "client"):
        raw_classes = parsed.get(key, [])
        if isinstance(raw_classes, str):
            raw_classes = [raw_classes]
        if not isinstance(raw_classes, list) or not all(isinstance(item, str) for item in raw_classes):
            return {
                "status": "FAIL",
                "reason": f"{key} is not a string list",
                "resource": resource,
            }
        counts[key] = len(raw_classes)
        class_names.extend(raw_classes)
    return {
        "status": "PASS",
        "resource": resource,
        "package": parsed.get("package", ""),
        "compatibilityLevel": parsed.get("compatibilityLevel", ""),
        "required": bool(parsed.get("required", False)),
        "counts": counts,
        "mixinClassCount": len(class_names),
        "mixinClassesSha256": hashlib.sha256(
            "\n".join(class_names).encode("utf-8")
        ).hexdigest(),
    }


def inspect_mod(locked: dict[str, Any], path: Path) -> dict[str, Any]:
    """Inspect the immutable archive and compare its declared surface to lock."""
    errors: list[str] = []
    actual: dict[str, Any] = {}
    locked_metadata = locked["metadata"]
    try:
        archive_sha = _sha256(path)
        with zipfile.ZipFile(path) as archive:
            bad = archive.testzip()
            if bad is not None:
                errors.append(f"corrupt JAR member: {bad}")
            try:
                metadata_bytes = archive.read("fabric.mod.json")
                metadata = json.loads(metadata_bytes.decode("utf-8"))
            except (KeyError, UnicodeDecodeError, json.JSONDecodeError) as exc:
                metadata_bytes = b""
                metadata = {}
                errors.append(f"fabric.mod.json unreadable: {exc}")
            if not isinstance(metadata, dict):
                metadata = {}
                errors.append("fabric.mod.json root is not an object")

            actual_entrypoints = _normalize_entrypoints(metadata.get("entrypoints", {}))
            actual_mixins = _string_list(metadata.get("mixins", []))
            actual_depends = metadata.get("depends", {})
            if not isinstance(actual_depends, dict):
                actual_depends = {}
                errors.append("depends is not an object")
            actual_depends = {str(key): str(value) for key, value in actual_depends.items()}
            actual_access_widener = metadata.get("accessWidener")
            if actual_access_widener is not None and not isinstance(actual_access_widener, str):
                errors.append("accessWidener is not a string or null")
                actual_access_widener = None

            actual = {
                "id": metadata.get("id"),
                "version": metadata.get("version"),
                "environment": metadata.get("environment"),
                "entrypoints": actual_entrypoints,
                "depends": actual_depends,
                "mixins": actual_mixins,
                "accessWidener": actual_access_widener,
            }
            for key in ("id", "version", "environment", "entrypoints", "depends", "mixins", "accessWidener"):
                if actual.get(key) != locked_metadata.get(key):
                    errors.append(
                        f"metadata {key} differs: actual={actual.get(key)!r} "
                        f"locked={locked_metadata.get(key)!r}"
                    )
            metadata_sha = hashlib.sha256(metadata_bytes).hexdigest()
            if metadata_sha != locked_metadata["sha256"]:
                errors.append(f"fabric.mod.json SHA-256 differs: {metadata_sha}")

            config_info: dict[str, dict[str, Any]] = {}
            for resource in actual_mixins:
                config_info[resource] = _mixin_config_info(archive, resource)
                if config_info[resource]["status"] != "PASS":
                    errors.append(
                        f"mixin resource {resource}: {config_info[resource].get('reason', 'invalid')}"
                    )
            widener_present = (
                actual_access_widener is not None
                and actual_access_widener in archive.namelist()
            )
            if actual_access_widener is not None and not widener_present:
                errors.append(f"access widener resource is missing: {actual_access_widener}")

            entrypoint_classes: list[dict[str, Any]] = []
            members = set(archive.namelist())
            for phase, entrypoints in actual_entrypoints.items():
                for entrypoint in entrypoints:
                    class_name = _entrypoint_class(entrypoint)
                    member = class_name.replace(".", "/") + ".class"
                    present = member in members
                    entrypoint_classes.append({
                        "phase": phase,
                        "entrypoint": entrypoint,
                        "class": class_name,
                        "present": present,
                    })
                    if not present:
                        errors.append(f"entrypoint class is missing: {member}")
    except (OSError, zipfile.BadZipFile) as exc:
        errors.append(f"cannot inspect JAR: {exc}")
        archive_sha = ""
        metadata_bytes = b""
        config_info = {}
        widener_present = False
        entrypoint_classes = []

    unmodified = archive_sha == locked["sha256"]
    if not unmodified:
        errors.append(f"archive SHA-256 differs: {archive_sha or '<unreadable>'}")
    return {
        "status": "PASS" if not errors else "FAIL",
        "reason": "locked archive and metadata match" if not errors else "; ".join(errors),
        "unmodified": unmodified,
        "archiveSha256": archive_sha,
        "archiveSize": path.stat().st_size if path.exists() else 0,
        "metadataSha256": hashlib.sha256(metadata_bytes).hexdigest() if metadata_bytes else "",
        "metadata": actual,
        "mixinConfigs": config_info,
        "accessWidenerPresent": widener_present,
        "entrypointClasses": entrypoint_classes,
    }


def _phase(status: str, reason: str, **details: Any) -> dict[str, Any]:
    result: dict[str, Any] = {"status": status, "reason": reason}
    result.update(details)
    return result


def _skip_phases(reason: str) -> dict[str, dict[str, Any]]:
    return {name: _phase("SKIP", reason) for name in PHASES}


def _skip_side(reason: str) -> dict[str, Any]:
    return {
        "status": "SKIP",
        "reason": reason,
        "process": {
            "status": "SKIP",
            "reason": reason,
            "attempted": False,
            "exitCode": None,
            "timedOut": False,
        },
        "phases": _skip_phases(reason),
    }


def _regex_results(patterns: Iterable[str], text: str) -> dict[str, bool]:
    result: dict[str, bool] = {}
    for pattern in patterns:
        try:
            result[pattern] = re.search(pattern, text, re.MULTILINE) is not None
        except re.error:
            result[pattern] = False
    return result


def _all_patterns(patterns: Iterable[str], text: str) -> tuple[bool, dict[str, bool]]:
    matches = _regex_results(patterns, text)
    return all(matches.values()), matches


def _required_count(entrypoints: dict[str, list[str]]) -> int:
    return sum(len(entrypoints.get(key, [])) for key in ("server", "main"))


def _unique_strings(values: Iterable[str]) -> list[str]:
    result: list[str] = []
    seen: set[str] = set()
    for value in values:
        if value not in seen:
            seen.add(value)
            result.append(value)
    return result


def _find_java(explicit: Path | None) -> Path | None:
    if explicit is not None:
        return explicit.expanduser().resolve()
    for variable in ("CPPFM_JAVA", "JAVA_HOME"):
        value = os.environ.get(variable)
        if not value:
            continue
        candidate = Path(value)
        if candidate.name != "java":
            candidate = candidate / "bin/java"
        if candidate.is_file():
            return candidate.resolve()
    found = shutil.which("java")
    return Path(found).resolve() if found else None


def _java_major(java: Path) -> tuple[int | None, str]:
    try:
        process = subprocess.run(
            [str(java), "-version"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=10,
            start_new_session=True,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return None, str(exc)
    text = process.stdout
    match = re.search(r'version\s+"(\d+)', text)
    if not match:
        match = re.search(r'openjdk\s+(\d+)', text)
    return (int(match.group(1)) if match else None), text.splitlines()[0] if text else ""


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def _reader_thread(stream: Any, output: queue.Queue[str | None]) -> None:
    try:
        for raw in iter(stream.readline, b""):
            output.put(raw.decode("utf-8", errors="replace").rstrip("\r\n"))
    finally:
        output.put(None)


def _kill_group(process: subprocess.Popen[bytes], signal_value: int = signal.SIGTERM) -> None:
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal_value)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pass


def _drain_queue(output: queue.Queue[str | None], lines: list[str]) -> bool:
    closed = False
    while True:
        try:
            item = output.get_nowait()
        except queue.Empty:
            break
        if item is None:
            closed = True
        else:
            lines.append(item)
    return closed


def _start_capture(command: list[str], cwd: Path) -> tuple[subprocess.Popen[bytes], queue.Queue[str | None], threading.Thread]:
    process = subprocess.Popen(
        command,
        cwd=str(cwd),
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    assert process.stdout is not None
    output: queue.Queue[str | None] = queue.Queue()
    reader = threading.Thread(target=_reader_thread, args=(process.stdout, output), daemon=True)
    reader.start()
    return process, output, reader


def _send_line(process: subprocess.Popen[bytes], line: str) -> bool:
    if process.stdin is None or process.poll() is not None:
        return False
    try:
        process.stdin.write((line + "\n").encode("utf-8"))
        process.stdin.flush()
        return True
    except (BrokenPipeError, OSError):
        return False


def _finish_capture(
    process: subprocess.Popen[bytes],
    output: queue.Queue[str | None],
    reader: threading.Thread,
    lines: list[str],
    deadline: float,
) -> tuple[int | None, bool]:
    timed_out = False
    while process.poll() is None and time.monotonic() < deadline:
        _drain_queue(output, lines)
        time.sleep(0.05)
    if process.poll() is None:
        timed_out = True
        _kill_group(process, signal.SIGKILL)
    _drain_queue(output, lines)
    reader.join(timeout=2)
    _drain_queue(output, lines)
    return process.returncode, timed_out


def _run_reference_process(
    command: list[str],
    cwd: Path,
    command_specs: list[dict[str, Any]],
    timeout: float,
) -> dict[str, Any]:
    try:
        process, output, reader = _start_capture(command, cwd)
    except OSError as exc:
        return {
            "attempted": True,
            "exitCode": None,
            "timedOut": False,
            "ready": False,
            "commandsSent": [],
            "commandResponses": [],
            "lines": [],
            "reason": f"could not start reference process: {exc}",
        }
    lines: list[str] = []
    start = time.monotonic()
    deadline = start + timeout
    ready = False
    stop_sent = False
    commands_sent = False
    command_deadline: float | None = None
    commands: list[str] = []
    closed = False
    while time.monotonic() < deadline:
        try:
            item = output.get(timeout=0.1)
            if item is None:
                closed = True
            else:
                lines.append(item)
        except queue.Empty:
            pass
        _drain_queue(output, lines)
        text = "\n".join(lines)
        if not ready and re.search(r"Done \([^\n]*\)! For help", text):
            ready = True
            if command_specs:
                for spec in command_specs:
                    command = str(spec["input"])
                    if _send_line(process, command):
                        commands.append(command)
                commands_sent = True
                command_deadline = time.monotonic() + 12.0
            else:
                _send_line(process, "stop")
                stop_sent = True
        if ready and commands_sent and not stop_sent:
            all_command_patterns = [
                pattern
                for spec in command_specs
                for pattern in spec.get("referenceLogRegex", [])
            ]
            complete, _ = _all_patterns(all_command_patterns, text)
            if complete or (command_deadline is not None and time.monotonic() >= command_deadline):
                _send_line(process, "stop")
                stop_sent = True
        if process.poll() is not None and (closed or output.empty()):
            break
    exit_code, timed_out = _finish_capture(process, output, reader, lines, deadline)
    return {
        "attempted": True,
        "exitCode": exit_code,
        "timedOut": timed_out,
        "ready": ready,
        "commandsSent": commands,
        "commandResponses": [],
        "lines": lines,
        "reason": "reference process completed" if not timed_out else f"reference timed out after {timeout}s",
    }


def _rcon_frame(packet_id: int, packet_type: int, body: str) -> bytes:
    payload = (
        int(packet_id).to_bytes(4, "little", signed=True)
        + int(packet_type).to_bytes(4, "little", signed=True)
        + body.encode("utf-8")
        + b"\x00\x00"
    )
    return len(payload).to_bytes(4, "little") + payload


def _rcon_recv(sock: socket.socket) -> tuple[int, int, str]:
    header = b""
    while len(header) < 4:
        part = sock.recv(4 - len(header))
        if not part:
            raise OSError("RCON connection closed while reading length")
        header += part
    size = int.from_bytes(header, "little")
    if size < 10 or size > 4110:
        raise OSError(f"invalid RCON frame length {size}")
    payload = b""
    while len(payload) < size:
        part = sock.recv(size - len(payload))
        if not part:
            raise OSError("RCON connection closed while reading frame")
        payload += part
    packet_id = int.from_bytes(payload[0:4], "little", signed=True)
    packet_type = int.from_bytes(payload[4:8], "little", signed=True)
    body = payload[8:-2].decode("utf-8", errors="replace")
    return packet_id, packet_type, body


def _rcon_command(port: int, password: str, command: str, timeout: float = 8.0) -> dict[str, Any]:
    deadline = time.monotonic() + timeout
    last_error = "RCON did not become ready"
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5) as sock:
                sock.settimeout(0.8)
                sock.sendall(_rcon_frame(1, 3, password))
                auth_id, _, _ = _rcon_recv(sock)
                if auth_id != 1:
                    raise OSError("RCON authentication rejected")
                sock.sendall(_rcon_frame(2, 2, command))
                response_id, _, body = _rcon_recv(sock)
                if response_id != 2:
                    raise OSError(f"unexpected RCON response id {response_id}")
                return {"status": "PASS", "command": command, "response": body}
        except (OSError, TimeoutError) as exc:
            last_error = str(exc)
            time.sleep(0.1)
    return {"status": "FAIL", "command": command, "response": "", "reason": last_error}


def _run_cppfm_process(
    command: list[str],
    cwd: Path,
    command_specs: list[dict[str, Any]],
    rcon_port: int,
    timeout: float,
) -> dict[str, Any]:
    try:
        process, output, reader = _start_capture(command, cwd)
    except OSError as exc:
        return {
            "attempted": True,
            "exitCode": None,
            "timedOut": False,
            "ready": False,
            "commandsSent": [],
            "commandResponses": [],
            "lines": [],
            "reason": f"could not start cppfm process: {exc}",
        }
    lines: list[str] = []
    start = time.monotonic()
    deadline = start + timeout
    ready = False
    failure_seen = False
    command_responses: list[dict[str, Any]] = []
    stopped = False
    failure_patterns = (
        "asset load failed:",
        "KnotLauncher bootstrap failed",
        "Java mod bootstrap failed",
        "strict JVM startup failed",
        "[cppfm] fatal:",
    )
    while time.monotonic() < deadline:
        try:
            item = output.get(timeout=0.1)
            if item is None:
                pass
            else:
                lines.append(item)
        except queue.Empty:
            pass
        _drain_queue(output, lines)
        text = "\n".join(lines)
        if not ready and "embedded HotSpot started" in text:
            ready = True
            # Give the RCON worker a bounded window to bind after JVM startup.
            time.sleep(0.5)
            for spec in command_specs:
                command_response = _rcon_command(
                    rcon_port,
                    "cppfm-plan52",
                    str(spec["input"]),
                )
                command_responses.append(command_response)
            _kill_group(process, signal.SIGTERM)
            stopped = True
            break
        if any(pattern in text for pattern in failure_patterns):
            failure_seen = True
            if process.poll() is None:
                _kill_group(process, signal.SIGTERM)
            stopped = True
            break
        if process.poll() is not None:
            break
    exit_code, timed_out = _finish_capture(process, output, reader, lines, deadline)
    if timed_out and not stopped:
        reason = f"cppfm timed out after {timeout}s"
    elif failure_seen:
        reason = "cppfm reported a startup/bootstrap failure"
    elif ready:
        reason = "cppfm process completed"
    else:
        reason = "cppfm exited before embedded JVM startup"
    return {
        "attempted": True,
        "exitCode": exit_code,
        "timedOut": timed_out,
        "ready": ready,
        "commandsSent": [str(spec["input"]) for spec in command_specs],
        "commandResponses": command_responses,
        "lines": lines,
        "reason": reason,
    }


def _write_log(evidence_dir: Path, name: str, side: str, lines: list[str]) -> tuple[str, str]:
    evidence_dir.mkdir(parents=True, exist_ok=True)
    filename = f"{name}.{side}.log"
    path = evidence_dir / filename
    data = ("\n".join(lines) + "\n").encode("utf-8", errors="replace")
    path.write_bytes(data)
    return filename, hashlib.sha256(data).hexdigest()


def _process_summary(raw: dict[str, Any], evidence_dir: Path, log_name: str,
                     temp_root: Path, cache_dir: Path) -> dict[str, Any]:
    filename, digest = _write_log(evidence_dir, log_name, raw["side"], raw["lines"])
    tail = [_normalize_text(line, temp_root, cache_dir) for line in raw["lines"][-40:]]
    return {
        "status": "PASS" if raw["exitCode"] == 0 and not raw["timedOut"] else "FAIL",
        "reason": raw["reason"],
        "attempted": bool(raw["attempted"]),
        "exitCode": raw["exitCode"],
        "timedOut": bool(raw["timedOut"]),
        "ready": bool(raw["ready"]),
        "commandsSent": raw.get("commandsSent", []),
        "commandResponses": raw.get("commandResponses", []),
        "lineCount": len(raw["lines"]),
        "logSha256": digest,
        "evidenceFile": filename,
        "tail": tail,
    }


def _reference_side(
    scenario: dict[str, Any],
    server_jar: Path,
    runtime_paths: list[Path],
    java: Path,
    temp_root: Path,
    evidence_dir: Path,
    log_name: str,
    cache_dir: Path,
    timeout: float,
) -> dict[str, Any]:
    game_dir = temp_root / f"{log_name}.reference"
    mods_dir = game_dir / "mods"
    mods_dir.mkdir(parents=True, exist_ok=True)
    for path in scenario["modPaths"]:
        shutil.copy2(path, mods_dir / path.name)
    shutil.copy2(server_jar, game_dir / "server.jar")
    (game_dir / "fabric-server-launcher.properties").write_text(
        "serverJar=server.jar\n", encoding="utf-8"
    )
    port = _free_port()
    (game_dir / "eula.txt").write_text("eula=true\n", encoding="utf-8")
    (game_dir / "server.properties").write_text(
        "\n".join([
            "online-mode=false",
            "enforce-secure-profile=false",
            "motd=cppfm-plan52-real-corpus-reference",
            "level-name=world",
            "level-seed=cppfm-plan52",
            "level-type=flat",
            "generate-structures=false",
            "spawn-protection=0",
            "view-distance=4",
            "simulation-distance=4",
            f"server-port={port}",
            "max-players=1",
        ]) + "\n",
        encoding="utf-8",
    )
    classpath = os.pathsep.join([str(game_dir / "server.jar")] + [str(path) for path in runtime_paths])
    command = [
        str(java),
        "-Dmixin.debug.verbose=true",
        "-Xmx1G",
        "-cp",
        classpath,
        "net.fabricmc.loader.impl.launch.server.FabricServerLauncher",
        "--nogui",
    ]
    raw = _run_reference_process(command, game_dir, scenario["commands"], timeout)
    raw["side"] = "reference"
    process = _process_summary(raw, evidence_dir, log_name, temp_root, cache_dir)
    text = "\n".join(raw["lines"])
    expected_ids = scenario["ids"]
    load_patterns = [
        r"Loading Minecraft 1\.21\.4 with Fabric Loader 0\.16\.9",
        *[rf"- {re.escape(mod_id)}\s" for mod_id in expected_ids],
    ]
    load_ok, load_matches = _all_patterns(load_patterns, text)
    if not raw["ready"]:
        load_ok = False
    initialize_patterns = scenario["initializeLogRegex"]
    if initialize_patterns:
        initialize_ok, initialize_matches = _all_patterns(initialize_patterns, text)
    else:
        initialize_ok = raw["ready"] and scenario["entrypointCount"] == 0
        initialize_matches = {}
    behavior_ok, behavior_matches = _all_patterns(scenario["referenceLogRegex"], text)
    command_results: list[dict[str, Any]] = []
    for spec in scenario["commands"]:
        matches, pattern_results = _all_patterns(spec["referenceLogRegex"], text)
        command_results.append({
            "input": spec["input"],
            "status": "PASS" if matches else "FAIL",
            "reason": "reference command markers observed" if matches else "reference command markers missing",
            "matches": pattern_results,
        })
        behavior_ok = behavior_ok and matches
    shutdown_ok = (
        raw["exitCode"] == 0
        and not raw["timedOut"]
        and "Stopping the server" in text
    )
    phases = {
        "load": _phase(
            "PASS" if load_ok else "FAIL",
            "reference Fabric loaded the locked mod set" if load_ok else "reference load markers missing",
            matches=load_matches,
        ),
        "initialize": _phase(
            "PASS" if initialize_ok else "FAIL",
            "reference entrypoint/config initialization evidence observed" if initialize_ok else "reference initialization evidence missing",
            matches=initialize_matches,
            expectedEntrypointCount=scenario["entrypointCount"],
        ),
        "behavior": _phase(
            "PASS" if behavior_ok else "FAIL",
            "reference mixin/command behavior markers observed" if behavior_ok else "reference behavior markers missing",
            matches=behavior_matches,
            commands=command_results,
        ),
        "shutdown": _phase(
            "PASS" if shutdown_ok else "FAIL",
            "reference exited cleanly after stop" if shutdown_ok else "reference did not prove clean shutdown",
        ),
    }
    status = "PASS" if all(item["status"] == "PASS" for item in phases.values()) else "FAIL"
    return {
        "status": status,
        "reason": "all reference phases passed" if status == "PASS" else "one or more reference phases failed",
        "process": process,
        "phases": phases,
        "commandResponses": [],
    }


def _cppfm_side(
    scenario: dict[str, Any],
    binary: Path,
    classes: Path,
    java: Path,
    temp_root: Path,
    evidence_dir: Path,
    log_name: str,
    cache_dir: Path,
    timeout: float,
) -> dict[str, Any]:
    game_dir = temp_root / f"{log_name}.cppfm"
    mods_dir = game_dir / "mods"
    config_dir = game_dir / "config"
    world_dir = game_dir / "world"
    mods_dir.mkdir(parents=True, exist_ok=True)
    config_dir.mkdir(parents=True, exist_ok=True)
    world_dir.mkdir(parents=True, exist_ok=True)
    for path in scenario["modPaths"]:
        shutil.copy2(path, mods_dir / path.name)
    rcon_port = _free_port()
    command = [
        str(binary),
        "--jvm=true",
        "--jvm-strict=true",
        f"--jvm-java-home={java.parent.parent}",
        f"--jvm-classes={classes}",
        f"--jvm-mods={mods_dir}",
        f"--jvm-config={config_dir}",
        f"--world-dir={world_dir}",
        "--assets=" + str(ROOT / "assets" / "registry"),
        "--level-type=flat",
        "--view-distance=4",
        "--port=0",
        "--enable-rcon=true",
        f"--rcon.port={rcon_port}",
        "--rcon.password=cppfm-plan52",
    ]
    raw = _run_cppfm_process(command, ROOT, scenario["commands"], rcon_port, timeout)
    raw["side"] = "cppfm"
    process = _process_summary(raw, evidence_dir, log_name, temp_root, cache_dir)
    text = "\n".join(raw["lines"])
    expected_count = len(scenario["modPaths"])
    expected_entrypoints = scenario["entrypointCount"]
    load_pattern = rf"loaded {expected_count} mod candidate\(s\)"
    init_pattern = rf"initialized {expected_entrypoints} entrypoint\(s\)"
    load_ok = raw["ready"] and re.search(load_pattern, text) is not None
    init_ok = raw["ready"] and re.search(init_pattern, text) is not None
    behavior_ok, behavior_matches = _all_patterns(scenario["referenceLogRegex"], text)
    command_results: list[dict[str, Any]] = []
    for spec, response in zip(scenario["commands"], raw.get("commandResponses", [])):
        response_text = str(response.get("response", ""))
        matches, pattern_results = _all_patterns(spec["cppfmResponseRegex"], response_text)
        command_results.append({
            "input": spec["input"],
            "status": "PASS" if matches else "FAIL",
            "reason": "cppfm command response markers observed" if matches else (
                response.get("reason", "cppfm command response markers missing")
            ),
            "matches": pattern_results,
            "response": response_text,
        })
        behavior_ok = behavior_ok and matches
    if scenario["commands"] and len(command_results) != len(scenario["commands"]):
        behavior_ok = False
        command_results.append({
            "status": "FAIL",
            "reason": "cppfm did not return every required command response",
        })
    shutdown_ok = (
        raw["exitCode"] == 0
        and not raw["timedOut"]
        and "stopped cleanly" in text
        and "bye" in text
    )
    phases = {
        "load": _phase(
            "PASS" if load_ok else "FAIL",
            "cppfm reported all locked mod candidates" if load_ok else "cppfm did not load the locked mod set",
            expectedCandidates=expected_count,
            marker=load_pattern,
        ),
        "initialize": _phase(
            "PASS" if init_ok else "FAIL",
            "cppfm reported all expected entrypoints initialized" if init_ok else "cppfm initialization marker missing",
            expectedEntrypointCount=expected_entrypoints,
            marker=init_pattern,
        ),
        "behavior": _phase(
            "PASS" if behavior_ok else "FAIL",
            "cppfm behavior markers observed" if behavior_ok else "cppfm did not reproduce reference behavior markers",
            matches=behavior_matches,
            commands=command_results,
        ),
        "shutdown": _phase(
            "PASS" if shutdown_ok else "FAIL",
            "cppfm exited cleanly after SIGTERM" if shutdown_ok else "cppfm did not prove clean shutdown",
        ),
    }
    status = "PASS" if all(item["status"] == "PASS" for item in phases.values()) else "FAIL"
    return {
        "status": status,
        "reason": "all cppfm phases passed" if status == "PASS" else raw["reason"],
        "process": process,
        "phases": phases,
        "commandResponses": raw.get("commandResponses", []),
    }


def _scenario(mods: list[dict[str, Any]], paths: dict[str, Path], combined: bool = False) -> dict[str, Any]:
    selected = mods if combined else mods[:1]
    return {
        "ids": [str(mod["id"]) for mod in selected],
        "modPaths": [paths[str(mod["id"])] for mod in selected],
        "entrypointCount": sum(
            _required_count(mod["metadata"]["entrypoints"]) for mod in selected
        ),
        "initializeLogRegex": _unique_strings(
            pattern
            for mod in selected
            for pattern in mod["behavior"]["initializeLogRegex"]
        ),
        "referenceLogRegex": _unique_strings(
            pattern
            for mod in selected
            for pattern in mod["behavior"]["referenceLogRegex"]
        ),
        "commands": [
            command
            for mod in selected
            for command in mod["behavior"]["commands"]
        ],
    }


def _skeleton_case(mod: dict[str, Any], reason: str,
                   metadata: dict[str, Any] | None = None) -> dict[str, Any]:
    return {
        "id": mod["id"],
        "version": mod["version"],
        "filename": mod["filename"],
        "status": "SKIP",
        "metadata": metadata or {"status": "SKIP", "reason": reason},
        "reference": _skip_side(reason),
        "cppfm": _skip_side(reason),
    }


def _skeleton_combined(reason: str) -> dict[str, Any]:
    return {
        "id": "combined",
        "version": "",
        "filename": "",
        "status": "SKIP",
        "metadata": {"status": "SKIP", "reason": reason},
        "reference": _skip_side(reason),
        "cppfm": _skip_side(reason),
    }


def validate_report(report: Any) -> list[str]:
    errors: list[str] = []
    if not isinstance(report, dict):
        return ["report root must be an object"]
    if report.get("schema") != REPORT_SCHEMA:
        errors.append("report schema is not cppfm.real-mod-corpus.report.v1")
    if report.get("suite") != "cppfm-plan51-real-mod-corpus":
        errors.append("report suite is incorrect")
    if report.get("status") not in STATUS:
        errors.append("report status must be PASS, FAIL, or SKIP")
    if not isinstance(report.get("statusReason"), str) or not report["statusReason"]:
        errors.append("report statusReason must be non-empty")
    game = report.get("game")
    if not isinstance(game, dict) or game.get("id") != "minecraft" or game.get("version") != "1.21.4" or game.get("protocol") != 769:
        errors.append("report game must be Minecraft 1.21.4 / protocol 769")
    lock = report.get("lock")
    if not isinstance(lock, dict) or lock.get("modCount") != 3:
        errors.append("report lock.modCount must be 3")
    execution = report.get("execution")
    if not isinstance(execution, dict):
        errors.append("report execution is missing")
    else:
        if execution.get("networkAccessed") is not False:
            errors.append("comparison report must state networkAccessed=false")
        if not isinstance(execution.get("executionAttempted"), bool):
            errors.append("executionAttempted must be boolean")
        for key in ("reference", "cppfm", "cache", "fabricRuntime", "java"):
            if key not in execution or not isinstance(execution[key], dict):
                errors.append(f"execution.{key} is missing")
    artifacts = report.get("artifacts")
    if not isinstance(artifacts, list) or len(artifacts) != 3:
        errors.append("report must contain exactly three real mod cases")
        artifacts = []
    case_statuses: list[str] = []
    for index, case in enumerate(artifacts):
        if not isinstance(case, dict):
            errors.append(f"artifacts[{index}] is not an object")
            continue
        if not isinstance(case.get("id"), str) or case.get("status") not in STATUS:
            errors.append(f"artifacts[{index}] has invalid id/status")
        else:
            case_statuses.append(case["status"])
        for side_name in ("reference", "cppfm"):
            side = case.get(side_name)
            if not isinstance(side, dict) or side.get("status") not in STATUS:
                errors.append(f"artifacts[{index}].{side_name} is invalid")
                continue
            phases = side.get("phases")
            if not isinstance(phases, dict):
                errors.append(f"artifacts[{index}].{side_name}.phases is missing")
            else:
                for phase in PHASES:
                    if not isinstance(phases.get(phase), dict) or phases[phase].get("status") not in STATUS or not phases[phase].get("reason"):
                        errors.append(f"artifacts[{index}].{side_name}.{phase} is invalid")
            if not isinstance(side.get("process"), dict):
                errors.append(f"artifacts[{index}].{side_name}.process is missing")
    combined = report.get("combined")
    if not isinstance(combined, dict) or combined.get("status") not in STATUS:
        errors.append("combined case is invalid")
    summary = report.get("summary")
    if not isinstance(summary, dict) or summary.get("total") != 3:
        errors.append("summary.total must be 3")
    elif (
        summary.get("passed") != case_statuses.count("PASS")
        or summary.get("failed") != case_statuses.count("FAIL")
        or summary.get("skipped") != case_statuses.count("SKIP")
    ):
        errors.append("summary status counts do not match artifact cases")
    if not isinstance(report.get("limitations"), list) or not report["limitations"]:
        errors.append("limitations must be a non-empty list")

    if report.get("status") == "PASS":
        if not isinstance(execution, dict) or execution.get("executionAttempted") is not True:
            errors.append("PASS report cannot have executionAttempted=false")
        if case_statuses != ["PASS"] * 3:
            errors.append("PASS report must have PASS for every real mod case")
        if isinstance(combined, dict) and combined.get("status") != "PASS":
            errors.append("PASS report must have PASS for combined case")
        if isinstance(execution, dict):
            for key in ("cache", "fabricRuntime"):
                if execution.get(key, {}).get("status") != "VERIFIED":
                    errors.append(f"PASS report requires verified {key}")
    if report.get("status") == "SKIP" and isinstance(execution, dict):
        if execution.get("executionAttempted") is True:
            errors.append("SKIP report cannot claim executionAttempted=true")
    return errors


def _base_report(lock: dict[str, Any], lock_path: Path, reason: str,
                 execution: dict[str, Any], artifacts: list[dict[str, Any]],
                 combined: dict[str, Any], status: str) -> dict[str, Any]:
    passed = sum(case.get("status") == "PASS" for case in artifacts)
    failed = sum(case.get("status") == "FAIL" for case in artifacts)
    skipped = sum(case.get("status") == "SKIP" for case in artifacts)
    return {
        "schema": REPORT_SCHEMA,
        "suite": "cppfm-plan51-real-mod-corpus",
        "status": status,
        "statusReason": reason,
        "game": lock["game"],
        "lock": {
            "path": _display_path(lock_path),
            "sha256": _sha256(lock_path),
            "modCount": len(lock["mods"]),
        },
        "execution": execution,
        "summary": {
            "total": len(artifacts),
            "passed": passed,
            "failed": failed,
            "skipped": skipped,
            "combinedStatus": combined.get("status"),
        },
        "artifacts": artifacts,
        "combined": combined,
        "limitations": [
            "The corpus is real public mod JARs; the existing 25-case synthetic fixture corpus is not included or counted.",
            "The comparison is a bounded startup/behavior/shutdown probe, not proof of arbitrary Fabric mod compatibility.",
            "The reference server runs offline-mode with no client join; client-only behavior, GUI, packets, RNG parity, and 24-hour operation are outside this gate.",
            "JARs are immutable cache inputs and are intentionally not committed to Git.",
        ],
    }


def _write_report(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n", encoding="utf-8")


def _preflight(
    lock: dict[str, Any],
    cache_dir: Path,
    fabric_cache_dir: Path,
    binary: Path | None,
    classes: Path | None,
    java: Path | None,
) -> tuple[dict[str, Any], dict[str, Path], list[str], Path | None, list[Path]]:
    paths: dict[str, Path] = {}
    errors: list[str] = []
    try:
        cache_summary = corpus.verify_cache(lock, cache_dir)
        for mod in lock["mods"]:
            paths[str(mod["id"])] = corpus.cache_path(cache_dir, mod)
        server_path = corpus.cache_path(cache_dir, lock["runtime"]["referenceServer"])
    except corpus.MissingCache as exc:
        cache_summary = {
            "status": "SKIP",
            "networkAccessed": False,
            "reason": str(exc),
            "cachePath": _display_path(cache_dir),
            "artifactCount": 0,
        }
        server_path = None
        errors.append(str(exc))
    except (corpus.CorpusError, OSError) as exc:
        cache_summary = {
            "status": "FAIL",
            "networkAccessed": False,
            "reason": str(exc),
            "cachePath": _display_path(cache_dir),
            "artifactCount": 0,
        }
        server_path = None
        errors.append(str(exc))

    runtime_summary: dict[str, Any]
    runtime_paths: list[Path] = []
    runtime_lock_path = ROOT / "jvm/vendor/fabric-runtime.lock.json"
    try:
        runtime_manifest = fabric_runtime.load_manifest(runtime_lock_path)
        runtime_paths = fabric_runtime.verify_cache(runtime_manifest, fabric_cache_dir)
        runtime_summary = {
            "status": "VERIFIED",
            "loaderVersion": runtime_manifest["loader"]["version"],
            "mixinVersion": runtime_manifest["loader"]["mixinVersion"],
            "artifactCount": len(runtime_paths),
            "cachePath": _display_path(fabric_cache_dir),
        }
    except Exception as exc:  # verifier supplies user-facing deterministic errors
        runtime_summary = {
            "status": "SKIP",
            "reason": f"Fabric runtime cache unavailable offline: {exc}",
            "cachePath": _display_path(fabric_cache_dir),
        }
        errors.append(runtime_summary["reason"])

    binary_summary: dict[str, Any]
    if binary is None or not binary.is_file() or not os.access(binary, os.X_OK):
        binary_summary = {"status": "SKIP", "reason": "cppfm executable is not available"}
        errors.append(binary_summary["reason"])
    else:
        binary_summary = {
            "status": "VERIFIED",
            "path": _display_path(binary),
            "sha256": _sha256(binary),
        }

    classes_summary: dict[str, Any]
    if classes is None or not classes.is_dir():
        classes_summary = {"status": "SKIP", "reason": "cppfm JVM shadow classes directory is not available"}
        errors.append(classes_summary["reason"])
    else:
        classes_summary = {"status": "VERIFIED", "path": _display_path(classes)}

    java_summary: dict[str, Any]
    selected_java = java
    if selected_java is None or not selected_java.is_file() or not os.access(selected_java, os.X_OK):
        java_summary = {"status": "SKIP", "reason": "Java executable is not available"}
        errors.append(java_summary["reason"])
        selected_java = None
    else:
        major, version_line = _java_major(selected_java)
        minimum = lock["runtime"]["javaMajor"]["minimum"]
        maximum = lock["runtime"]["javaMajor"]["maximum"]
        if major is None:
            java_summary = {"status": "SKIP", "reason": "could not determine Java major version"}
            errors.append(java_summary["reason"])
        elif not minimum <= major <= maximum:
            java_summary = {
                "status": "SKIP",
                "reason": f"Java major {major} is outside locked reference range {minimum}..{maximum}",
                "major": major,
                "version": version_line,
                "path": _display_path(selected_java),
            }
            errors.append(java_summary["reason"])
        else:
            java_summary = {
                "status": "VERIFIED",
                "major": major,
                "version": version_line,
                "path": _display_path(selected_java),
            }
    execution = {
        "networkAccessed": False,
        "executionAttempted": False,
        "reference": {"status": "NOT_ATTEMPTED", "attemptedCases": 0},
        "cppfm": {"status": "NOT_ATTEMPTED", "attemptedCases": 0},
        "cache": cache_summary,
        "fabricRuntime": runtime_summary,
        "binary": binary_summary,
        "classes": classes_summary,
        "java": java_summary,
    }
    if server_path is not None:
        paths["reference-server"] = server_path
    return execution, paths, errors, selected_java, runtime_paths


def run_harness(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    lock_path = args.lock.resolve()
    try:
        lock = corpus.load_lock(lock_path)
    except (corpus.CorpusError, OSError, ValueError) as exc:
        raise HarnessError(str(exc)) from exc
    cache_dir = args.cache_dir.resolve()
    fabric_cache_dir = args.fabric_cache.resolve()
    binary = args.binary.resolve() if args.binary else None
    classes = args.classes.resolve() if args.classes else None
    java = _find_java(args.java)
    execution, paths, preflight_errors, selected_java, runtime_paths = _preflight(
        lock, cache_dir, fabric_cache_dir, binary, classes, java
    )
    evidence_dir = args.evidence_dir.resolve()
    report_path = args.report_output.resolve()
    artifacts: list[dict[str, Any]] = []
    metadata_by_id: dict[str, dict[str, Any]] = {}
    for mod in lock["mods"]:
        mod_id = str(mod["id"])
        if mod_id in paths:
            try:
                metadata = inspect_mod(mod, paths[mod_id])
            except (OSError, ValueError) as exc:
                metadata = {"status": "FAIL", "reason": f"metadata inspection failed: {exc}"}
            metadata_by_id[mod_id] = metadata
        else:
            metadata_by_id[mod_id] = {"status": "SKIP", "reason": "locked JAR is not available offline"}
        artifacts.append(_skeleton_case(mod, "execution not attempted", metadata_by_id[mod_id]))

    if not args.metadata_only and not preflight_errors:
        assert selected_java is not None
        assert binary is not None
        assert classes is not None
        assert paths.get("reference-server") is not None
        run_root = Path(tempfile.mkdtemp(prefix="cppfm-real-mod-corpus-"))
        try:
            mod_paths = {str(mod["id"]): paths[str(mod["id"])] for mod in lock["mods"]}
            for index, mod in enumerate(lock["mods"], start=1):
                scenario = _scenario([mod], mod_paths)
                name = f"{index:02d}-{mod['id']}"
                reference = _reference_side(
                    scenario,
                    paths["reference-server"],
                    runtime_paths,
                    selected_java,
                    run_root,
                    evidence_dir,
                    name,
                    cache_dir,
                    args.timeout,
                )
                cpp = _cppfm_side(
                    scenario,
                    binary,
                    classes,
                    selected_java,
                    run_root,
                    evidence_dir,
                    name,
                    cache_dir,
                    args.timeout,
                )
                case = artifacts[index - 1]
                case["reference"] = reference
                case["cppfm"] = cpp
                case["status"] = (
                    "PASS"
                    if case["metadata"]["status"] == "PASS"
                    and reference["status"] == "PASS"
                    and cpp["status"] == "PASS"
                    else "FAIL"
                )
                case["reason"] = (
                    "metadata and both runtime sides passed"
                    if case["status"] == "PASS"
                    else "metadata or one runtime side failed"
                )

            all_mods = lock["mods"]
            combined_scenario = _scenario(all_mods, mod_paths, combined=True)
            combined_reference = _reference_side(
                combined_scenario,
                paths["reference-server"],
                runtime_paths,
                selected_java,
                run_root,
                evidence_dir,
                "combined",
                cache_dir,
                args.timeout,
            )
            combined_cpp = _cppfm_side(
                combined_scenario,
                binary,
                classes,
                selected_java,
                run_root,
                evidence_dir,
                "combined",
                cache_dir,
                args.timeout,
            )
            combined = {
                "id": "combined",
                "version": ",".join(str(mod["version"]) for mod in all_mods),
                "filename": "",
                "status": "PASS" if (
                    all(case["status"] == "PASS" for case in artifacts)
                    and combined_reference["status"] == "PASS"
                    and combined_cpp["status"] == "PASS"
                ) else "FAIL",
                "reason": "all individual and combined probes passed" if (
                    all(case["status"] == "PASS" for case in artifacts)
                    and combined_reference["status"] == "PASS"
                    and combined_cpp["status"] == "PASS"
                ) else "individual or combined probe failed",
                "metadata": {
                    "status": "PASS" if all(item["status"] == "PASS" for item in metadata_by_id.values()) else "FAIL",
                    "modIds": [str(mod["id"]) for mod in all_mods],
                },
                "reference": combined_reference,
                "cppfm": combined_cpp,
            }
            execution["executionAttempted"] = True
            execution["reference"] = {"status": "ATTEMPTED", "attemptedCases": len(artifacts) + 1}
            execution["cppfm"] = {"status": "ATTEMPTED", "attemptedCases": len(artifacts) + 1}
        finally:
            shutil.rmtree(run_root, ignore_errors=True)
    else:
        reason = (
            "metadata-only mode requested; runtime execution intentionally skipped"
            if args.metadata_only
            else "; ".join(preflight_errors)
        )
        for case in artifacts:
            case["reference"] = _skip_side(reason)
            case["cppfm"] = _skip_side(reason)
            case["status"] = "SKIP"
            case["reason"] = reason
        combined = _skeleton_combined(reason)

    if not args.metadata_only and not preflight_errors:
        status = "PASS" if all(case["status"] == "PASS" for case in artifacts) and combined["status"] == "PASS" else "FAIL"
        reason = (
            "all real-mod corpus cases passed on reference Fabric and cppfm"
            if status == "PASS"
            else "one or more real-mod corpus comparisons failed; see per-phase evidence"
        )
        exit_code = 0 if status == "PASS" else 1
    else:
        status = "SKIP"
        reason = (
            "metadata-only mode does not claim runtime compatibility"
            if args.metadata_only
            else "runtime comparison not executed: " + "; ".join(preflight_errors)
        )
        exit_code = 2
    report = _base_report(lock, lock_path, reason, execution, artifacts, combined, status)
    schema_errors = validate_report(report)
    if schema_errors:
        raise HarnessError("generated report failed schema validation: " + "; ".join(schema_errors))
    _write_report(report_path, report)
    return report, exit_code


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", type=Path, default=DEFAULT_LOCK)
    parser.add_argument("--cache-dir", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--fabric-cache", type=Path, default=DEFAULT_FABRIC_CACHE)
    parser.add_argument("--binary", type=Path, help="cppfm executable; required for execution")
    parser.add_argument("--classes", type=Path, help="compiled cppfm JVM shadow classes")
    parser.add_argument("--java", type=Path, help="Java 21 executable; default uses CPPFM_JAVA/JAVA_HOME/java")
    parser.add_argument("--timeout", type=float, default=90.0, help="timeout per reference/cppfm case")
    parser.add_argument("--report-output", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--evidence-dir", type=Path, default=DEFAULT_EVIDENCE)
    parser.add_argument("--metadata-only", action="store_true", help="inspect locked JAR metadata but explicitly skip runtime execution")
    parser.add_argument("--validate-report", type=Path, help="validate an existing report and exit")
    parser.add_argument("--json", action="store_true", help="print the generated report summary as JSON")
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if args.validate_report is not None:
        try:
            report = json.loads(args.validate_report.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            print(f"real mod corpus report INVALID: {exc}", file=sys.stderr)
            return 2
        errors = validate_report(report)
        if errors:
            print("real mod corpus report INVALID: " + "; ".join(errors), file=sys.stderr)
            return 2
        print("real mod corpus report schema: PASS")
        return 0
    try:
        report, exit_code = run_harness(args)
    except (HarnessError, corpus.CorpusError, OSError, ValueError) as exc:
        print(f"real mod corpus harness ERROR: {exc}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(report, sort_keys=True, indent=2))
    else:
        print(
            f"real mod corpus: {report['status']} "
            f"(cases PASS={report['summary']['passed']} "
            f"FAIL={report['summary']['failed']} SKIP={report['summary']['skipped']}; "
            f"combined={report['combined']['status']})"
        )
        print(f"report={args.report_output.resolve()}")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
