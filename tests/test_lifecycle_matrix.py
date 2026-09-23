#!/usr/bin/env python3
"""Fail-closed public-boundary lifecycle and negative matrix for ``cppfm``.

This runner deliberately does not import the other integration harnesses.  Its
process ownership, deadline, socket framing, and cleanup rules are local so a
future change to another helper cannot turn an orphan or a liveness-only probe
into a pass here.

The matrix is intentionally bounded and synthetic.  It proves the executable's
observable lifecycle/negative contract at the selected build, not vanilla
equivalence, a real-client session, or long-run behavior.  Every case emits one
JSON object (JSONL) and the process exits non-zero for a failed or ambiguous
observation.

Usage::

    timeout --foreground --kill-after=5 600 \
      python3 tests/test_lifecycle_matrix.py --binary ./build/cppfm

``--keep-artifacts`` retains the temporary roots and combined server logs after
the run.  It never permits an owned process or listener to remain alive.
"""

from __future__ import annotations

import argparse
import datetime as _datetime
import hashlib
import json
import os
import platform
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, NoReturn


HOST = "127.0.0.1"
PROTOCOL = 769
VERSION = "1.21.4"
MAX_FRAME = 8 * 1024 * 1024
READ_CHUNK = 64 * 1024
READINESS_SECONDS = 45.0
SHUTDOWN_SECONDS = 15.0
KILL_GRACE_SECONDS = 4.0
MALFORMED_PROBE_SECONDS = 3.0


class MatrixFailure(RuntimeError):
    """An assertion failure, unsafe ambiguity, or cleanup failure."""


class DeadlineFailure(MatrixFailure):
    """An operation exceeded its monotonic deadline."""


def fail(message: str) -> NoReturn:
    raise MatrixFailure(message)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def remaining(deadline: float) -> float:
    value = deadline - time.monotonic()
    if value <= 0:
        raise DeadlineFailure("deadline expired")
    return value


def bounded_sleep(deadline: float, seconds: float = 0.05) -> None:
    time.sleep(min(seconds, remaining(deadline)))


@dataclass(frozen=True)
class ProcInfo:
    pid: int
    ppid: int
    pgid: int
    sid: int
    state: str
    start_time: str


def _proc_info(pid: int) -> ProcInfo | None:
    """Read one Linux process identity without invoking a process-wide tool.

    The start-time token makes individual cleanup safe against PID reuse.  The
    runner is intentionally Linux/POSIX oriented because the target lifecycle
    gate requires process groups and /proc ownership inspection.
    """

    if os.name != "posix":
        return None
    try:
        text = Path(f"/proc/{pid}/stat").read_text(encoding="ascii")
    except (FileNotFoundError, PermissionError, OSError, UnicodeError):
        return None
    closing = text.rfind(")")
    if closing < 0 or closing + 2 >= len(text):
        return None
    rest = text[closing + 2 :].split()
    # state is field 3; ppid/pgrp/session are fields 4/5/6; starttime is 22.
    if len(rest) <= 19:
        return None
    try:
        return ProcInfo(
            pid=pid,
            ppid=int(rest[1]),
            pgid=int(rest[2]),
            sid=int(rest[3]),
            state=rest[0],
            start_time=rest[19],
        )
    except (TypeError, ValueError):
        return None


def _all_proc_info() -> dict[int, ProcInfo]:
    if os.name != "posix":
        return {}
    result: dict[int, ProcInfo] = {}
    try:
        entries = list(Path("/proc").iterdir())
    except OSError:
        return result
    for entry in entries:
        if not entry.name.isdigit():
            continue
        info = _proc_info(int(entry.name))
        if info is not None:
            result[info.pid] = info
    return result


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            block = stream.read(READ_CHUNK)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def _tree_signature(root: Path) -> list[dict[str, object]]:
    """Return a deterministic signature of a harness-observed tree."""

    if not root.exists() and not root.is_symlink():
        return []
    if root.is_symlink():
        return [{"path": ".", "type": "symlink", "target": os.readlink(root)}]
    if not root.is_dir():
        return [{"path": ".", "type": "file", "size": root.stat().st_size}]
    rows: list[dict[str, object]] = []
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
            rows.append({"path": relative, "type": "symlink", "target": os.readlink(path)})
        elif path.is_dir():
            rows.append({"path": relative, "type": "directory"})
        elif path.is_file():
            rows.append({"path": relative, "type": "file", "size": path.stat().st_size})
        else:
            rows.append({"path": relative, "type": "other"})
    return rows


def _world_signature(world: Path) -> dict[str, object]:
    """Capture persistent identity without pretending it is a vanilla oracle."""

    require(world.exists() and world.is_dir(), f"world root is missing: {world}")
    files: dict[str, dict[str, object]] = {}
    for path in sorted(world.rglob("*")):
        if not path.is_file() or path.name == "session.lock":
            continue
        relative = path.relative_to(world).as_posix()
        files[relative] = {
            "size": path.stat().st_size,
            "sha256": _sha256_file(path),
        }
    marker = files.get("lifecycle-marker.bin")
    require(marker is not None, "lifecycle marker was not preserved in the world")
    region_files = sorted(
        name for name in files if name.startswith("region/") and name.endswith(".mca")
    )
    return {
        "file_count": len(files),
        "files": files,
        "marker": marker,
        "region_files": region_files,
        "level_dat": files.get("level.dat"),
    }


def _read_lock_record(world: Path) -> dict[str, object]:
    path = world / "session.lock"
    require(path.exists() and path.is_file(), f"session.lock is missing: {path}")
    text = path.read_text(encoding="ascii", errors="replace").strip()
    fields = text.split()
    require(len(fields) >= 2, f"session.lock has no PID/timestamp record: {text!r}")
    try:
        pid = int(fields[0])
        timestamp = int(fields[1])
    except ValueError as exc:
        fail(f"session.lock record is malformed: {text!r} ({exc})")
    require(pid > 0 and timestamp > 0, f"session.lock record is invalid: {text!r}")
    return {"pid": pid, "timestamp_ms": timestamp, "raw": text}


def _pid_exists_with_identity(pid: int, start_time: str | None = None) -> bool:
    info = _proc_info(pid)
    if info is None:
        return False
    return start_time is None or info.start_time == start_time


def _free_port() -> int:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.bind((HOST, 0))
        return int(sock.getsockname()[1])
    finally:
        sock.close()


def _socket_timeout(deadline: float, cap: float = 1.0) -> float:
    return max(0.001, min(cap, remaining(deadline)))


def _write_varint(value: int) -> bytes:
    require(value >= 0, f"negative unsigned VarInt requested: {value}")
    output = bytearray()
    number = value
    while True:
        byte = number & 0x7F
        number >>= 7
        if number:
            output.append(byte | 0x80)
        else:
            output.append(byte)
            return bytes(output)


def _read_varint(data: bytes, offset: int = 0) -> tuple[int, int]:
    value = 0
    for index in range(5):
        position = offset + index
        if position >= len(data):
            fail("truncated VarInt in status response")
        byte = data[position]
        value |= (byte & 0x7F) << (7 * index)
        if not byte & 0x80:
            return value, position + 1
    fail("oversized VarInt in status response")


def _pack_string(value: str) -> bytes:
    encoded = value.encode("utf-8")
    return _write_varint(len(encoded)) + encoded


def _frame(packet_id: int, payload: bytes = b"") -> bytes:
    body = _write_varint(packet_id) + payload
    return _write_varint(len(body)) + body


class _SocketReader:
    def __init__(self, sock: socket.socket, deadline: float):
        self.sock = sock
        self.deadline = deadline

    def exact(self, size: int) -> bytes:
        require(0 <= size <= MAX_FRAME, f"invalid read size: {size}")
        result = bytearray()
        while len(result) < size:
            self.sock.settimeout(_socket_timeout(self.deadline))
            try:
                chunk = self.sock.recv(min(READ_CHUNK, size - len(result)))
            except socket.timeout as exc:
                raise DeadlineFailure("status socket read timed out") from exc
            if not chunk:
                fail("status socket closed before the expected response")
            result.extend(chunk)
        return bytes(result)

    def varint(self) -> int:
        result = bytearray()
        for _ in range(5):
            result.extend(self.exact(1))
            if not result[-1] & 0x80:
                value, end = _read_varint(bytes(result))
                require(end == len(result), "status VarInt parser consumed the wrong bytes")
                return value
        fail("status response VarInt is too large")

    def frame(self) -> tuple[int, bytes]:
        length = self.varint()
        require(0 < length <= MAX_FRAME, f"invalid status frame length: {length}")
        body = self.exact(length)
        packet_id, offset = _read_varint(body)
        return packet_id, body[offset:]


def _status_probe(port: int, deadline: float) -> dict[str, object]:
    """Perform a complete status handshake and ping, not a TCP liveness check."""

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.settimeout(_socket_timeout(deadline))
        sock.connect((HOST, port))
        reader = _SocketReader(sock, deadline)
        handshake = (
            _write_varint(PROTOCOL)
            + _pack_string(HOST)
            + struct.pack(">H", port)
            + _write_varint(1)
        )
        sock.sendall(_frame(0x00, handshake))
        sock.sendall(_frame(0x00))
        packet_id, payload = reader.frame()
        require(packet_id == 0x00, f"status response packet id was 0x{packet_id:02x}")
        length, offset = _read_varint(payload)
        require(0 <= length <= 1_048_576, f"status JSON length is invalid: {length}")
        require(offset + length == len(payload), "status JSON has trailing or truncated bytes")
        try:
            status = json.loads(payload[offset : offset + length].decode("utf-8"))
        except (UnicodeError, json.JSONDecodeError) as exc:
            fail(f"status JSON is malformed: {exc}")
        require(isinstance(status, dict), "status response is not a JSON object")
        version = status.get("version")
        require(isinstance(version, dict), "status response has no version object")
        require(version.get("protocol") == PROTOCOL, f"status protocol mismatch: {version!r}")
        require(version.get("name") == VERSION, f"status version mismatch: {version!r}")
        players = status.get("players")
        require(isinstance(players, dict), "status response has no players object")
        require(isinstance(players.get("online"), int), "status players.online is not an integer")
        require("description" in status, "status response has no description")
        token = time.monotonic_ns() & ((1 << 63) - 1)
        sock.sendall(_frame(0x01, struct.pack(">q", token)))
        ping_id, ping_payload = reader.frame()
        require(ping_id == 0x01, f"status ping packet id was 0x{ping_id:02x}")
        require(ping_payload == struct.pack(">q", token), "status ping payload mismatch")
        return status
    finally:
        try:
            sock.close()
        except OSError:
            pass


def _malformed_probe(port: int, payload: bytes, deadline: float) -> dict[str, object]:
    """Send malformed public-boundary bytes and require an explicit close.

    An idle timeout is never interpreted as a safe rejection.  The server may
    send a disconnect frame before closing; the bytes are retained in the
    evidence record, but the close itself remains mandatory.
    """

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    received = bytearray()
    closed = False
    try:
        sock.settimeout(_socket_timeout(deadline))
        sock.connect((HOST, port))
        sock.sendall(payload)
        try:
            sock.shutdown(socket.SHUT_WR)
        except OSError:
            pass
        while True:
            sock.settimeout(_socket_timeout(deadline))
            try:
                chunk = sock.recv(READ_CHUNK)
            except socket.timeout as exc:
                raise DeadlineFailure(
                    "malformed protocol peer did not close before the deadline"
                ) from exc
            except ConnectionResetError:
                closed = True
                break
            if not chunk:
                closed = True
                break
            received.extend(chunk)
            require(len(received) <= MAX_FRAME, "malformed response exceeded evidence bound")
        require(closed, "malformed protocol probe did not produce a close")
        return {
            "sent_sha256": hashlib.sha256(payload).hexdigest(),
            "sent_bytes": payload.hex(),
            "received_sha256": hashlib.sha256(received).hexdigest(),
            "received_bytes": bytes(received).hex(),
            "closed": closed,
        }
    finally:
        try:
            sock.close()
        except OSError:
            pass


def _port_release(port: int, timeout: float = 5.0) -> dict[str, object]:
    """Require both connection refusal and a successful immediate rebind."""

    deadline = time.monotonic() + timeout
    last_error = ""
    while time.monotonic() < deadline:
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            probe.settimeout(_socket_timeout(deadline, 0.25))
            try:
                probe.connect((HOST, port))
            except ConnectionRefusedError:
                pass
            except socket.timeout as exc:
                fail(f"port {port} probe timed out after shutdown: {exc}")
            except OSError as exc:
                fail(f"port {port} probe was ambiguous after shutdown: {exc}")
            else:
                fail(f"port {port} still accepts connections after shutdown")
        finally:
            probe.close()

        binder = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            # The server sets SO_REUSEADDR.  A just-closed listener can leave
            # status-ping connections in TIME_WAIT; that is not a listener
            # leak and must not be confused with failure to release the port.
            # SO_REUSEADDR still rejects an active listener, while proving the
            # requested immediate port-reuse contract.
            binder.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                binder.bind((HOST, port))
                return {"port": port, "connect": "refused", "rebind": "pass"}
            except OSError as exc:
                last_error = str(exc)
        finally:
            binder.close()
        bounded_sleep(deadline, 0.05)
    fail(f"port {port} could not be rebound before deadline: {last_error}")


def _find_asset_source(binary: Path, repository: Path) -> Path | None:
    candidates = [
        repository / "assets",
        binary.parent.parent / "assets",
        binary.parent / "assets",
    ]
    for candidate in candidates:
        if candidate.is_dir():
            return candidate.resolve()
    return None


def _install_asset_link(server_root: Path, asset_source: Path | None) -> None:
    if asset_source is None:
        return
    link = server_root / "assets"
    if link.exists() or link.is_symlink():
        return
    try:
        link.symlink_to(asset_source, target_is_directory=True)
    except OSError as exc:
        fail(f"could not install isolated asset link {link}: {exc}")


class OwnedProcess:
    """A Popen plus its private process-group/descendant ownership record."""

    def __init__(
        self,
        proc: subprocess.Popen[bytes],
        log_path: Path,
        log_file: object,
        pgid: int,
    ):
        self.proc = proc
        self.pid = proc.pid
        self.pgid = pgid
        self.log_path = log_path
        self._log_file = log_file
        self._closed_log = False
        self._identities: dict[int, str] = {}
        self._capture_processes()

    @property
    def returncode(self) -> int | None:
        return self.proc.returncode

    def _capture_processes(self) -> None:
        infos = _all_proc_info()
        root = infos.get(self.pid)
        if root is not None:
            self._identities.setdefault(root.pid, root.start_time)
        known = set(self._identities)
        changed = True
        while changed:
            changed = False
            for info in infos.values():
                if info.pgid == self.pgid or info.ppid in known:
                    if info.pid not in self._identities:
                        self._identities[info.pid] = info.start_time
                        known.add(info.pid)
                        changed = True

    def _current_owned(self) -> list[ProcInfo]:
        infos = _all_proc_info()
        self._capture_processes()
        result: list[ProcInfo] = []
        for pid, start_time in self._identities.items():
            info = infos.get(pid)
            if info is not None and info.start_time == start_time:
                result.append(info)
        # A process created in our private group after the last capture is also
        # owned.  The group is private because the root Popen used start_new_session.
        for info in infos.values():
            if info.pgid == self.pgid and all(item.pid != info.pid for item in result):
                result.append(info)
        return result

    def _send_group(self, sig: signal.Signals) -> list[str]:
        issues: list[str] = []
        if os.name != "posix":
            issues.append("POSIX process groups are unavailable")
            return issues
        try:
            os.killpg(self.pgid, sig)
        except ProcessLookupError:
            pass
        except OSError as exc:
            issues.append(f"killpg({self.pgid}, {sig.name}) failed: {exc}")
        return issues

    def _send_owned_individual(self, sig: signal.Signals) -> list[str]:
        issues: list[str] = []
        for pid, start_time in list(self._identities.items()):
            if pid == self.pid:
                continue
            info = _proc_info(pid)
            if info is None or info.start_time != start_time:
                continue
            try:
                os.kill(pid, sig)
            except ProcessLookupError:
                pass
            except OSError as exc:
                issues.append(f"owned pid {pid} signal {sig.name} failed: {exc}")
        return issues

    def _close_log(self) -> list[str]:
        if self._closed_log:
            return []
        self._closed_log = True
        try:
            self._log_file.close()  # type: ignore[union-attr]
        except OSError as exc:
            return [f"could not close log {self.log_path}: {exc}"]
        return []

    def log_text(self) -> str:
        try:
            data = self.log_path.read_bytes()
        except OSError as exc:
            return f"<log unavailable: {exc}>"
        if len(data) > 128 * 1024:
            data = data[-128 * 1024 :]
        return data.decode("utf-8", errors="replace")

    def reap_naturally(self, timeout: float) -> dict[str, object]:
        """Reap an expected natural exit, escalating only if it hangs."""

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self._capture_processes()
            self.proc.poll()
            current = self._current_owned()
            if self.proc.returncode is not None and not current:
                issues = self._close_log()
                if issues:
                    fail("; ".join(issues))
                return {
                    "returncode": self.proc.returncode,
                    "escalated": False,
                    "owned_after": [],
                }
            bounded_sleep(deadline, 0.05)

        result = self.shutdown(signal.SIGTERM, KILL_GRACE_SECONDS)
        result["natural_exit_timeout"] = True
        return result

    def shutdown(self, requested: signal.Signals | None, timeout: float) -> dict[str, object]:
        """Signal/reap the private group and fail if any owned process remains."""

        issues: list[str] = []
        self._capture_processes()
        if requested is not None and (self.proc.poll() is None or self._current_owned()):
            issues.extend(self._send_group(requested))

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.proc.poll()
            current = self._current_owned()
            if self.proc.returncode is not None and not current:
                issues.extend(self._close_log())
                return {
                    "returncode": self.proc.returncode,
                    "escalated": False,
                    "owned_after": [],
                    "issues": issues,
                }
            bounded_sleep(deadline, 0.05)

        # A graceful signal did not finish in time.  Escalation is itself
        # evidence of a failed clean-shutdown gate, but it is still required to
        # avoid leaking the owned process into the next isolated case.
        issues.append("graceful shutdown deadline expired; escalated to SIGKILL")
        issues.extend(self._send_group(signal.SIGKILL))
        issues.extend(self._send_owned_individual(signal.SIGKILL))
        kill_deadline = time.monotonic() + KILL_GRACE_SECONDS
        while time.monotonic() < kill_deadline:
            self.proc.poll()
            current = self._current_owned()
            if self.proc.returncode is not None and not current:
                issues.extend(self._close_log())
                return {
                    "returncode": self.proc.returncode,
                    "escalated": True,
                    "owned_after": [],
                    "issues": issues,
                }
            bounded_sleep(kill_deadline, 0.05)

        self.proc.poll()
        current = self._current_owned()
        issues.append(
            "owned process cleanup is ambiguous: "
            + repr([info.pid for info in current])
        )
        issues.extend(self._close_log())
        return {
            "returncode": self.proc.returncode,
            "escalated": True,
            "owned_after": [info.pid for info in current],
            "issues": issues,
        }


class Runner:
    def __init__(self, binary: Path, artifact_root: Path, repository: Path):
        self.binary = binary
        self.artifact_root = artifact_root
        self.repository = repository
        self.asset_source = _find_asset_source(binary, repository)
        self.active: list[OwnedProcess] = []

    def launch(
        self,
        command: list[str],
        cwd: Path,
        env: dict[str, str],
        log_path: Path,
    ) -> OwnedProcess:
        cwd.mkdir(parents=True, exist_ok=True)
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_file = log_path.open("wb")
        try:
            proc = subprocess.Popen(
                command,
                cwd=str(cwd),
                env=env,
                stdin=subprocess.DEVNULL,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                close_fds=True,
                start_new_session=True,
            )
        except (OSError, ValueError):
            log_file.close()
            raise
        try:
            pgid = os.getpgid(proc.pid)
        except OSError:
            proc.kill()
            proc.wait()
            log_file.close()
            raise MatrixFailure(f"could not inspect process group for pid {proc.pid}")
        owned = OwnedProcess(proc, log_path, log_file, pgid)
        self.active.append(owned)
        return owned

    def server_root(self, path: Path) -> tuple[Path, Path]:
        root = path / "server-root"
        world = path / "world-save"
        root.mkdir(parents=True, exist_ok=True)
        world.mkdir(parents=True, exist_ok=True)
        _install_asset_link(root, self.asset_source)
        return root, world

    def environment(self, server_root: Path) -> dict[str, str]:
        env = os.environ.copy()
        env["CPPFM_SERVER_DIR"] = str(server_root)
        env["LC_ALL"] = "C"
        return env

    def forget(self, owned: OwnedProcess) -> None:
        if owned in self.active and owned.returncode is not None:
            self.active.remove(owned)

    def cleanup_all(self) -> list[str]:
        issues: list[str] = []
        for owned in reversed(self.active):
            result = owned.shutdown(signal.SIGTERM, SHUTDOWN_SECONDS)
            if result.get("owned_after"):
                issues.append(f"owned pid(s) remain for {owned.log_path}: {result['owned_after']}")
            if result.get("issues"):
                issues.extend(str(item) for item in result["issues"])
            if owned.returncode is not None:
                self.forget(owned)
        return issues


def _server_command(
    binary: Path,
    port: int,
    world: Path,
    *extra: str,
) -> list[str]:
    return [
        str(binary),
        f"--port={port}",
        f"--world-dir={world}",
        "--level-type=flat",
        "--view-distance=2",
        "--jvm=false",
        "--online-mode=false",
        "--enforce-secure-profile=false",
        *extra,
    ]


def _start_server(
    runner: Runner,
    case_dir: Path,
    port: int,
    *,
    extra: Iterable[str] = (),
    properties: str | None = None,
) -> tuple[OwnedProcess, Path, Path, list[str]]:
    root, world = runner.server_root(case_dir)
    if properties is not None:
        (root / "server.properties").write_text(properties, encoding="utf-8")
    command = _server_command(runner.binary, port, world, *list(extra))
    process = runner.launch(command, root, runner.environment(root), case_dir / "server.log")
    return process, root, world, command


def _wait_ready(process: OwnedProcess, port: int, timeout: float = READINESS_SECONDS) -> dict[str, object]:
    deadline = time.monotonic() + timeout
    errors: list[str] = []
    while time.monotonic() < deadline:
        if process.proc.poll() is not None:
            log = process.log_text()
            fail(
                f"server exited before protocol readiness (exit={process.returncode}); "
                f"log tail={log[-2000:]!r}"
            )
        try:
            return _status_probe(port, deadline)
        except (OSError, MatrixFailure, ValueError, json.JSONDecodeError) as exc:
            errors.append(f"{type(exc).__name__}: {exc}")
            if len(errors) > 8:
                errors = errors[-8:]
            bounded_sleep(deadline, 0.1)
    fail(f"protocol readiness deadline expired on {port}: {errors!r}")


def _stop_server(
    runner: Runner,
    process: OwnedProcess,
    world: Path,
    port: int,
    sig: signal.Signals,
) -> dict[str, object]:
    result = process.shutdown(sig, SHUTDOWN_SECONDS)
    runner.forget(process)
    require(result.get("returncode") == 0, f"clean shutdown exit was {result.get('returncode')}")
    require(not result.get("escalated"), f"clean {sig.name} shutdown required escalation")
    require(not result.get("owned_after"), f"owned process(es) remain: {result.get('owned_after')}")
    issues = result.get("issues") or []
    require(not issues, f"shutdown reported cleanup issues: {issues!r}")
    log = process.log_text()
    require("[cppfm] bye" in log, "shutdown log has no final bye marker")
    lock = _read_lock_record(world)
    lock_pid = int(lock["pid"])
    require(
        not _pid_exists_with_identity(lock_pid),
        f"session.lock PID {lock_pid} is still alive after owned shutdown",
    )
    port_result = _port_release(port)
    return {
        "signal": sig.name,
        "exit": result.get("returncode"),
        "log_bye": True,
        "lock": lock,
        "port_release": port_result,
        "log_tail": log[-1200:],
    }


def _write_marker(world: Path) -> str:
    marker = b"plan53 lifecycle marker\x00\x01\x02\xff\n"
    path = world / "lifecycle-marker.bin"
    path.write_bytes(marker)
    return hashlib.sha256(marker).hexdigest()


def _case_info(runner: Runner, case_dir: Path, flag: str) -> dict[str, object]:
    cwd = case_dir / "probe-cwd"
    runtime_root = case_dir / "runtime-root"
    cwd.mkdir(parents=True, exist_ok=True)
    before_cwd = _tree_signature(cwd)
    before_runtime = _tree_signature(runtime_root)
    env = os.environ.copy()
    env["CPPFM_SERVER_DIR"] = str(runtime_root)
    env["LC_ALL"] = "C"
    command = [str(runner.binary), flag]
    process = runner.launch(command, cwd, env, case_dir / "info.log")
    result = process.reap_naturally(10.0)
    runner.forget(process)
    require(result.get("returncode") == 0, f"{flag} exit was {result.get('returncode')}")
    require(not result.get("escalated"), f"{flag} needed forced cleanup")
    require(not result.get("owned_after"), f"{flag} left owned processes")
    output = process.log_text()
    if flag == "--help":
        require("Usage:" in output and "--version" in output, "help output is incomplete")
    else:
        require("CppFabricMC" in output and "protocol 769" in output, "version output is incomplete")
    after_cwd = _tree_signature(cwd)
    after_runtime = _tree_signature(runtime_root)
    require(after_cwd == before_cwd, f"{flag} changed its current-directory tree")
    require(after_runtime == before_runtime, f"{flag} created or changed runtime files")
    require(not (runtime_root / ".cppfm" / "server.lock").exists(), f"{flag} acquired a runtime lock")
    require(not (runtime_root / "session.lock").exists(), f"{flag} created a session lock")
    return {
        "command": command,
        "exit": result.get("returncode"),
        "output_sha256": hashlib.sha256(output.encode("utf-8")).hexdigest(),
        "output_tail": output[-1000:],
        "cwd_tree_unchanged": True,
        "runtime_tree_unchanged": True,
        "no_runtime_lock": True,
    }


def _case_startup_sigterm(runner: Runner, case_dir: Path) -> dict[str, object]:
    port = _free_port()
    process, root, world, command = _start_server(runner, case_dir, port)
    try:
        status = _wait_ready(process, port)
        lock = _read_lock_record(world)
        require(int(lock["pid"]) == process.pid, "session.lock does not identify the owned server")
        stopped = _stop_server(runner, process, world, port, signal.SIGTERM)
        return {
            "command": command,
            "port": port,
            "pid": process.pid,
            "readiness": {
                "status_protocol": status["version"]["protocol"],
                "status_name": status["version"]["name"],
                "ping": "matched",
            },
            "shutdown": stopped,
        }
    finally:
        runner.cleanup_all()


def _case_startup_sigint(runner: Runner, case_dir: Path) -> dict[str, object]:
    port = _free_port()
    process, root, world, command = _start_server(runner, case_dir, port)
    try:
        status = _wait_ready(process, port)
        lock = _read_lock_record(world)
        require(int(lock["pid"]) == process.pid, "SIGINT case lock does not identify the server")
        stopped = _stop_server(runner, process, world, port, signal.SIGINT)
        return {
            "command": command,
            "port": port,
            "pid": process.pid,
            "readiness": {
                "status_protocol": status["version"]["protocol"],
                "status_name": status["version"]["name"],
                "ping": "matched",
            },
            "shutdown": stopped,
        }
    finally:
        runner.cleanup_all()


def _case_restart(runner: Runner, case_dir: Path) -> dict[str, object]:
    port = _free_port()
    root, world = runner.server_root(case_dir)
    marker_hash = _write_marker(world)
    command = _server_command(runner.binary, port, world)
    first = runner.launch(command, root, runner.environment(root), case_dir / "first.log")
    try:
        first_status = _wait_ready(first, port)
        require((world / "lifecycle-marker.bin").is_file(), "first server lost the world marker")
        first_stop = _stop_server(runner, first, world, port, signal.SIGTERM)
        after_first = _world_signature(world)

        second = runner.launch(command, root, runner.environment(root), case_dir / "second.log")
        try:
            second_status = _wait_ready(second, port)
            after_second = _world_signature(world)
            require(after_second["marker"] == after_first["marker"], "world marker changed across restart")
            if after_first.get("level_dat") is not None:
                require(
                    after_second.get("level_dat") == after_first.get("level_dat"),
                    "level.dat changed before any restart-side save was requested",
                )
            second_stop = _stop_server(runner, second, world, port, signal.SIGTERM)
        finally:
            runner.cleanup_all()
        return {
            "command": command,
            "port": port,
            "marker_sha256": marker_hash,
            "first": {
                "status_protocol": first_status["version"]["protocol"],
                "status_name": first_status["version"]["name"],
                "world": after_first,
                "shutdown": first_stop,
            },
            "second": {
                "status_protocol": second_status["version"]["protocol"],
                "status_name": second_status["version"]["name"],
                "world": after_second,
                "shutdown": second_stop,
            },
            "same_world": {
                "marker_preserved": True,
                "level_dat_compared": after_first.get("level_dat") is not None,
                "limitation": "No gameplay edit or vanilla oracle is claimed by this lifecycle continuity check.",
            },
        }
    finally:
        runner.cleanup_all()


def _case_live_lock(runner: Runner, case_dir: Path) -> dict[str, object]:
    owner_dir = case_dir / "owner"
    contender_dir = case_dir / "contender"
    owner_root, world = runner.server_root(owner_dir)
    contender_root, _ = runner.server_root(contender_dir)
    (world / "lifecycle-marker.bin").write_bytes(b"live-lock-marker\n")
    owner_port = _free_port()
    contender_port = _free_port()
    owner_command = _server_command(runner.binary, owner_port, world)
    contender_command = _server_command(runner.binary, contender_port, world)
    owner = runner.launch(owner_command, owner_root, runner.environment(owner_root), owner_dir / "server.log")
    try:
        owner_status = _wait_ready(owner, owner_port)
        owner_lock = _read_lock_record(world)
        require(int(owner_lock["pid"]) == owner.pid, "owner lock PID mismatch")
        contender = runner.launch(
            contender_command,
            contender_root,
            runner.environment(contender_root),
            contender_dir / "server.log",
        )
        contender_result = contender.reap_naturally(15.0)
        runner.forget(contender)
        require(contender_result.get("returncode") not in (None, 0), "live-lock contender unexpectedly succeeded")
        require(not contender_result.get("escalated"), "live-lock contender hung instead of failing")
        require(not contender_result.get("owned_after"), "live-lock contender left an owned child")
        contender_log = contender.log_text()
        require(
            "session.lock is already held" in contender_log
            or "world is already in use" in contender_log,
            "live-lock collision had no explicit rejection diagnostic",
        )
        contender_port_state = _port_release(contender_port)
        owner_health = _status_probe(owner_port, time.monotonic() + 5.0)
        owner_stop = _stop_server(runner, owner, world, owner_port, signal.SIGTERM)
        return {
            "owner": {
                "command": owner_command,
                "pid": owner.pid,
                "port": owner_port,
                "status_protocol": owner_status["version"]["protocol"],
                "lock": owner_lock,
                "health_after_collision": owner_health["version"]["protocol"] == PROTOCOL,
                "shutdown": owner_stop,
            },
            "contender": {
                "command": contender_command,
                "pid": contender.pid,
                "port": contender_port,
                "exit": contender_result.get("returncode"),
                "rejected": True,
                "diagnostic": contender_log[-1600:],
                "port_release": contender_port_state,
            },
        }
    finally:
        runner.cleanup_all()


def _case_malformed_protocol(runner: Runner, case_dir: Path) -> dict[str, object]:
    port = _free_port()
    process, _root, world, command = _start_server(runner, case_dir, port)
    probes = [
        ("truncated_frame_body", _write_varint(5) + b"\x00"),
        ("oversized_frame_varint", b"\x80\x80\x80\x80\x80"),
        ("unknown_handshake_packet", _frame(0x7F, b"")),
    ]
    observations: list[dict[str, object]] = []
    try:
        initial = _wait_ready(process, port)
        for name, payload in probes:
            probe = _malformed_probe(port, payload, time.monotonic() + MALFORMED_PROBE_SECONDS)
            health = _status_probe(port, time.monotonic() + 5.0)
            require(health["version"]["protocol"] == PROTOCOL, f"health probe failed after {name}")
            require(health["version"]["name"] == VERSION, f"health version failed after {name}")
            probe["name"] = name
            probe["health_after"] = {"protocol": health["version"]["protocol"], "name": health["version"]["name"]}
            observations.append(probe)
        stopped = _stop_server(runner, process, world, port, signal.SIGTERM)
        return {
            "command": command,
            "port": port,
            "initial_health": {"protocol": initial["version"]["protocol"], "name": initial["version"]["name"]},
            "probes": observations,
            "server_survived_each_probe": True,
            "shutdown": stopped,
        }
    finally:
        runner.cleanup_all()


def _case_malformed_cli(runner: Runner, case_dir: Path) -> dict[str, object]:
    port = _free_port()
    properties = (
        f"server-port={port}\n"
        "view-distance=2\n"
        "simulation-distance=2\n"
        "level-type=flat\n"
        "jvm=false\n"
    )
    extra = (
        "--port=not-a-port",
        "--view-distance=not-an-integer",
        "--unknown-plan53-option",
    )
    process, _root, world, command = _start_server(
        runner,
        case_dir,
        port,
        extra=extra,
        properties=properties,
    )
    try:
        status = _wait_ready(process, port)
        stopped = _stop_server(runner, process, world, port, signal.SIGTERM)
        log = process.log_text()
        require(
            "invalid command-line value for --port: not-a-port" in log,
            "malformed port did not produce an explicit diagnostic",
        )
        require(
            "invalid command-line value for --view-distance: not-an-integer" in log,
            "malformed view distance did not produce an explicit diagnostic",
        )
        require(
            "missing value for --unknown-plan53-option" in log,
            "unknown CLI option without a value was not diagnosed",
        )
        return {
            "command": command,
            "configured_fallback_port": port,
            "status": {"protocol": status["version"]["protocol"], "name": status["version"]["name"]},
            "diagnostics": {
                "invalid_port": True,
                "invalid_view_distance": True,
                "unknown_missing_value": True,
            },
            "policy": "malformed typed values are diagnosed and retain the explicit property fallback; no silent pass is inferred",
            "shutdown": stopped,
        }
    finally:
        runner.cleanup_all()


@dataclass
class Evidence:
    matrix_id: str
    status: str
    snapshot: dict[str, object]
    command: str
    observation: dict[str, object]
    artifact_root: str
    error: str | None = None

    def as_dict(self) -> dict[str, object]:
        result: dict[str, object] = {
            "matrix_id": self.matrix_id,
            "missing_target": "#7,#9,#72,#76; plan53 Ch12 negative/cleanup residuals",
            "snapshot": self.snapshot,
            "command": self.command,
            "observation": self.observation,
            "provenance": "IMPLEMENTATION",
            "status": self.status,
            "limitation": (
                "Bounded public-boundary lifecycle/negative evidence at this build; "
                "not a vanilla oracle, real-client proof, or accepted long-run result."
            ),
            "artifact_root": self.artifact_root,
        }
        if self.error is not None:
            result["error"] = self.error
        return result


def _snapshot(binary: Path, repository: Path) -> dict[str, object]:
    head = "unavailable"
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=str(repository),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=5,
            check=False,
        )
        if result.returncode == 0:
            head = result.stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        head = "unavailable"
    return {
        "git_head": head,
        "binary": str(binary),
        "binary_size": binary.stat().st_size,
        "binary_sha256": _sha256_file(binary),
        "python": sys.version.split()[0],
        "host": platform.platform(),
        "pid": os.getpid(),
        "utc": _datetime.datetime.now(_datetime.timezone.utc).isoformat(),
    }


def _emit(record: dict[str, object]) -> None:
    print(json.dumps(record, sort_keys=True, separators=(",", ":")), flush=True)


def _run_case(
    runner: Runner,
    case_id: str,
    case_dir: Path,
    snapshot: dict[str, object],
    fn: Callable[[Runner, Path], dict[str, object]],
    command_label: str,
    keep_artifacts: bool,
) -> bool:
    case_dir.mkdir(parents=True, exist_ok=True)
    status = "PASS"
    observation: dict[str, object] = {}
    error: str | None = None
    started = time.monotonic()
    try:
        observation = fn(runner, case_dir)
    except (MatrixFailure, DeadlineFailure, OSError, ValueError, json.JSONDecodeError) as exc:
        status = "FAIL"
        error = f"{type(exc).__name__}: {exc}"
    except Exception as exc:  # fail closed for unexpected harness/runtime errors
        status = "FAIL"
        error = f"unexpected {type(exc).__name__}: {exc}"
    cleanup_issues = runner.cleanup_all()
    if cleanup_issues:
        status = "FAIL"
        suffix = "; ".join(cleanup_issues)
        error = f"{error}; cleanup: {suffix}" if error else f"cleanup: {suffix}"
    observation["elapsed_seconds"] = round(time.monotonic() - started, 3)
    observation["cleanup_issues"] = cleanup_issues
    record = Evidence(
        matrix_id=case_id,
        status=status,
        snapshot=snapshot,
        command=command_label,
        observation=observation,
        artifact_root=str(runner.artifact_root),
        error=error,
    ).as_dict()
    _emit(record)
    return status == "PASS"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, help="path to the cppfm executable")
    parser.add_argument(
        "--keep-artifacts",
        action="store_true",
        help="retain isolated roots and logs after all owned processes are gone",
    )
    args = parser.parse_args(argv)

    if os.name != "posix" or not Path("/proc").is_dir():
        print("FAIL: plan53 lifecycle matrix requires POSIX /proc process ownership inspection", file=sys.stderr)
        return 2

    binary = Path(args.binary).expanduser().resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        print(f"FAIL: executable is missing or not executable: {binary}", file=sys.stderr)
        return 2
    repository = Path(__file__).resolve().parents[1]
    artifact_root = Path(tempfile.mkdtemp(prefix="cppfm-plan53-lifecycle-"))
    runner = Runner(binary, artifact_root, repository)
    snapshot = _snapshot(binary, repository)
    cases: list[tuple[str, str, Callable[[Runner, Path], dict[str, object]]]] = [
        ("P53-LIFECYCLE-HELP", "--help", lambda r, d: _case_info(r, d, "--help")),
        ("P53-LIFECYCLE-VERSION", "--version", lambda r, d: _case_info(r, d, "--version")),
        (
            "P53-LIFECYCLE-START-SIGTERM",
            "server --status -- SIGTERM -- owned cleanup",
            _case_startup_sigterm,
        ),
        (
            "P53-LIFECYCLE-START-SIGINT",
            "server --status -- SIGINT -- owned cleanup",
            _case_startup_sigint,
        ),
        (
            "P53-LIFECYCLE-RESTART",
            "server --status -- SIGTERM -- same-world restart -- owned cleanup",
            _case_restart,
        ),
        (
            "P53-LIFECYCLE-LIVE-LOCK",
            "owner status -- contender same-world collision -- owner health -- cleanup",
            _case_live_lock,
        ),
        (
            "P53-NEGATIVE-PROTOCOL",
            "status -- malformed public frames -- status after each -- cleanup",
            _case_malformed_protocol,
        ),
        (
            "P53-NEGATIVE-CLI",
            "properties fallback -- malformed/unknown CLI diagnostics -- status -- cleanup",
            _case_malformed_cli,
        ),
    ]

    passed = 0
    interrupted = False

    def _interrupt(signum: int, _frame: object) -> NoReturn:
        raise KeyboardInterrupt(f"runner received signal {signum}")

    old_sigint = signal.getsignal(signal.SIGINT)
    old_sigterm = signal.getsignal(signal.SIGTERM)
    signal.signal(signal.SIGINT, _interrupt)
    signal.signal(signal.SIGTERM, _interrupt)
    try:
        for index, (case_id, label, function) in enumerate(cases, start=1):
            case_dir = artifact_root / f"{index:02d}-{case_id.lower()}"
            if _run_case(runner, case_id, case_dir, snapshot, function, label, args.keep_artifacts):
                passed += 1
    except KeyboardInterrupt as exc:
        interrupted = True
        _emit(
            Evidence(
                matrix_id="P53-LIFECYCLE-INTERRUPTED",
                status="FAIL",
                snapshot=snapshot,
                command="runner interruption",
                observation={},
                artifact_root=str(artifact_root),
                error=str(exc),
            ).as_dict()
        )
    finally:
        signal.signal(signal.SIGINT, old_sigint)
        signal.signal(signal.SIGTERM, old_sigterm)
        cleanup_issues = runner.cleanup_all()
        if cleanup_issues:
            _emit(
                Evidence(
                    matrix_id="P53-LIFECYCLE-CLEANUP",
                    status="FAIL",
                    snapshot=snapshot,
                    command="owned process-group cleanup",
                    observation={"cleanup_issues": cleanup_issues},
                    artifact_root=str(artifact_root),
                    error="; ".join(cleanup_issues),
                ).as_dict()
            )
            interrupted = True

    root_kept = args.keep_artifacts
    root_error: str | None = None
    if not args.keep_artifacts:
        try:
            shutil.rmtree(artifact_root)
        except OSError as exc:
            root_error = f"could not remove exact artifact root {artifact_root}: {exc}"
        if artifact_root.exists():
            root_error = root_error or f"artifact root remains after cleanup: {artifact_root}"
    if root_error:
        _emit(
            Evidence(
                matrix_id="P53-LIFECYCLE-ROOT-CLEANUP",
                status="FAIL",
                snapshot=snapshot,
                command="exact temporary-root cleanup",
                observation={"root": str(artifact_root), "kept": False},
                artifact_root=str(artifact_root),
                error=root_error,
            ).as_dict()
        )
        interrupted = True

    summary = {
        "matrix_id": "P53-LIFECYCLE-SUMMARY",
        "status": "FAIL" if interrupted or passed != len(cases) else "PASS",
        "cases": len(cases),
        "passed": passed,
        "failed": len(cases) - passed,
        "artifact_root": str(artifact_root),
        "artifact_root_kept": root_kept,
        "note": "A PASS requires protocol/status evidence, explicit negative rejection, and owned cleanup; liveness alone is not used.",
    }
    _emit(summary)
    return 1 if summary["status"] != "PASS" else 0


if __name__ == "__main__":
    raise SystemExit(main())
