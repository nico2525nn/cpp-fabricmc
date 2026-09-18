#!/usr/bin/env python3
"""Plan53 lifecycle and negative gate.

This is intentionally an external-boundary test.  It does not call production
helpers directly: every server observation goes through a TCP socket, and
every child is owned by a process group created by this runner.

The output is JSONL.  A case is PASS only after its protocol/resource
assertions and cleanup assertions have both completed.  A failed cleanup is a
gate failure, even when the server appeared to work before it was stopped.
"""

from __future__ import annotations

import argparse
import datetime as _datetime
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field
from typing import Any, BinaryIO


HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent
sys.path.insert(0, str(HERE))

try:
    from mcproto import Conn, PROTOCOL, pack_string, read_varint, unpack_string, write_varint
except Exception as exc:  # pragma: no cover - reported as a structured gate failure
    Conn = None  # type: ignore[assignment,misc]
    PROTOCOL = 769
    pack_string = read_varint = unpack_string = write_varint = None  # type: ignore[assignment]
    _MC_PROTO_IMPORT_ERROR = repr(exc)
else:
    _MC_PROTO_IMPORT_ERROR = None


HOST = "127.0.0.1"
INFO_TIMEOUT = 12.0
READY_TIMEOUT = 40.0
HEALTH_TIMEOUT = 4.0
PLAY_TIMEOUT = 30.0
STOP_TIMEOUT = 18.0
KILL_TIMEOUT = 8.0
SETTLE_TIMEOUT = 3.0


class GateFailure(RuntimeError):
    """An expected gate assertion failed, with JSON-safe detail."""

    def __init__(self, message: str, details: dict[str, Any] | None = None):
        super().__init__(message)
        self.details = details or {}


def now_utc() -> str:
    return _datetime.datetime.now(_datetime.timezone.utc).isoformat()


def json_safe(value: Any) -> Any:
    if isinstance(value, Path):
        return str(value)
    if isinstance(value, bytes):
        return value.hex()
    if isinstance(value, dict):
        return {str(k): json_safe(v) for k, v in value.items()}
    if isinstance(value, (list, tuple, set)):
        return [json_safe(v) for v in value]
    return value


def read_git_head(root: Path) -> str:
    """Read HEAD without spawning an unowned git process."""
    try:
        dot_git = root / ".git"
        if dot_git.is_file():
            first = dot_git.read_text(encoding="utf-8").strip()
            if first.startswith("gitdir:"):
                git_dir = Path(first.split(":", 1)[1].strip())
                if not git_dir.is_absolute():
                    git_dir = (dot_git.parent / git_dir).resolve()
            else:
                git_dir = dot_git
        else:
            git_dir = dot_git
        head = (git_dir / "HEAD").read_text(encoding="utf-8").strip()
        if not head.startswith("ref:"):
            return head
        ref = head.split(":", 1)[1].strip()
        ref_path = git_dir / ref
        if ref_path.is_file():
            return ref_path.read_text(encoding="utf-8").strip()
        common_dir_file = git_dir / "commondir"
        if common_dir_file.is_file():
            common_dir = Path(common_dir_file.read_text(encoding="utf-8").strip())
            if not common_dir.is_absolute():
                common_dir = (git_dir / common_dir).resolve()
            common_ref = common_dir / ref
            if common_ref.is_file():
                return common_ref.read_text(encoding="utf-8").strip()
        packed_files = [git_dir / "packed-refs"]
        if common_dir_file.is_file():
            packed_files.append(common_dir / "packed-refs")
        for packed in packed_files:
            if packed.is_file():
                for line in packed.read_text(encoding="utf-8").splitlines():
                    if line and not line.startswith("#") and " " in line:
                        value, name = line.split(" ", 1)
                        if name == ref:
                            return value
        return f"unresolved:{ref}"
    except (OSError, UnicodeError) as exc:
        return f"unknown:{type(exc).__name__}:{exc}"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            block = stream.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def tree_snapshot(root: Path) -> list[dict[str, Any]]:
    """Return a deterministic snapshot, including unexpected side effects."""
    if not root.exists():
        return [{"path": ".", "kind": "missing"}]
    entries: list[dict[str, Any]] = []
    for path in sorted(root.rglob("*"), key=lambda item: str(item.relative_to(root))):
        rel = str(path.relative_to(root))
        try:
            if path.is_symlink():
                entries.append({"path": rel, "kind": "symlink", "target": os.readlink(path)})
            elif path.is_dir():
                entries.append({"path": rel, "kind": "directory"})
            elif path.is_file():
                entries.append({"path": rel, "kind": "file", "size": path.stat().st_size})
            else:
                entries.append({"path": rel, "kind": "other"})
        except OSError as exc:
            entries.append({"path": rel, "kind": "error", "error": repr(exc)})
    return entries


def world_manifest(root: Path) -> dict[str, Any]:
    """Hash world files while excluding the volatile diagnostic lock record."""
    files: list[dict[str, Any]] = []
    if not root.is_dir():
        return {"exists": False, "directory": None, "files": files}
    try:
        directory_stat = root.stat()
        directory = {
            "device": directory_stat.st_dev,
            "inode": directory_stat.st_ino,
        }
    except OSError as exc:
        directory = {"error": repr(exc)}
    for path in sorted(root.rglob("*"), key=lambda item: str(item.relative_to(root))):
        if not path.is_file() or path.name == "session.lock":
            continue
        rel = str(path.relative_to(root))
        try:
            files.append({
                "path": rel,
                "size": path.stat().st_size,
                "sha256": sha256_file(path),
            })
        except OSError as exc:
            files.append({"path": rel, "error": repr(exc)})
    return {"exists": True, "directory": directory, "files": files}


def relative_artifact(root: Path, path: Path) -> str:
    try:
        return str(path.relative_to(root))
    except ValueError:
        return str(path)


def monotonic_deadline(seconds: float, overall: float | None = None) -> float:
    deadline = time.monotonic() + max(0.0, seconds)
    return min(deadline, overall) if overall is not None else deadline


@dataclass
class ProcInfo:
    pid: int
    ppid: int
    pgrp: int
    state: str
    command: str

    def as_dict(self) -> dict[str, Any]:
        return {
            "pid": self.pid,
            "ppid": self.ppid,
            "pgrp": self.pgrp,
            "state": self.state,
            "command": self.command,
        }


def read_proc_info(pid: int) -> ProcInfo | None:
    """Read Linux process metadata for exact ownership reporting."""
    try:
        stat_text = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
        close_paren = stat_text.rfind(")")
        if close_paren < 0:
            return None
        fields = stat_text[close_paren + 2 :].split()
        # After the comm field: state, ppid, pgrp, session, ...
        if len(fields) < 4:
            return None
        command_bytes = Path(f"/proc/{pid}/cmdline").read_bytes()
        command = command_bytes.replace(b"\x00", b" ").decode(errors="replace").strip()
        if not command:
            command = stat_text[stat_text.find("(") + 1 : close_paren]
        return ProcInfo(
            pid=pid,
            ppid=int(fields[1]),
            pgrp=int(fields[2]),
            state=fields[0],
            command=command,
        )
    except (OSError, ValueError, UnicodeError):
        return None


def process_table() -> dict[int, ProcInfo]:
    table: dict[int, ProcInfo] = {}
    proc = Path("/proc")
    if not proc.is_dir():
        return table
    for entry in proc.iterdir():
        if not entry.name.isdigit():
            continue
        info = read_proc_info(int(entry.name))
        if info is not None:
            table[info.pid] = info
    return table


def descendants(table: dict[int, ProcInfo], root_pid: int) -> set[int]:
    owned = {root_pid}
    changed = True
    while changed:
        changed = False
        for info in table.values():
            if info.pid not in owned and info.ppid in owned:
                owned.add(info.pid)
                changed = True
    return owned


def group_members(table: dict[int, ProcInfo], pgid: int) -> list[ProcInfo]:
    return sorted((info for info in table.values() if info.pgrp == pgid), key=lambda item: item.pid)


def socket_inodes(pid: int) -> set[str]:
    inodes: set[str] = set()
    fd_root = Path(f"/proc/{pid}/fd")
    try:
        for fd in fd_root.iterdir():
            try:
                target = os.readlink(fd)
            except OSError:
                continue
            match = re.fullmatch(r"socket:\[(\d+)\]", target)
            if match:
                inodes.add(match.group(1))
    except OSError:
        pass
    return inodes


def proc_listeners(pids: set[int]) -> list[dict[str, Any]]:
    """Map listening TCP sockets back to descriptors owned by exact PIDs."""
    if not pids:
        return []
    inodes: dict[str, list[int]] = {}
    for pid in sorted(pids):
        for inode in socket_inodes(pid):
            inodes.setdefault(inode, []).append(pid)
    if not inodes:
        return []
    rows: list[dict[str, Any]] = []
    try:
        tcp = Path("/proc/net/tcp").read_text(encoding="ascii")
    except OSError:
        return []
    for line in tcp.splitlines()[1:]:
        fields = line.split()
        if len(fields) < 10 or fields[3] != "0A":
            continue
        try:
            address, port_hex = fields[1].split(":", 1)
            inode = fields[9]
            if inode not in inodes:
                continue
            rows.append({
                "address": address,
                "port": int(port_hex, 16),
                "inode": inode,
                "pids": inodes[inode],
            })
        except (ValueError, IndexError):
            continue
    return sorted(rows, key=lambda item: (item["port"], item["inode"]))


@dataclass
class OwnedProcess:
    command: list[str]
    cwd: Path
    log_path: Path
    log_file: BinaryIO
    proc: subprocess.Popen[bytes]
    pgid: int
    seen_pids: set[int] = field(default_factory=set)
    seen_ports: set[int] = field(default_factory=set)
    port: int | None = None
    ready: dict[str, Any] | None = None
    stop_record: dict[str, Any] | None = None

    def observe_ownership(self) -> tuple[dict[int, ProcInfo], list[ProcInfo], list[dict[str, Any]]]:
        table = process_table()
        owned = descendants(table, self.proc.pid)
        owned.update(info.pid for info in group_members(table, self.pgid))
        self.seen_pids.update(owned)
        members = group_members(table, self.pgid)
        listeners = proc_listeners(set(owned))
        self.seen_ports.update(int(item["port"]) for item in listeners)
        return table, members, listeners

    def log_text(self) -> str:
        try:
            self.log_file.flush()
        except (OSError, ValueError):
            pass
        try:
            return self.log_path.read_text(encoding="utf-8", errors="replace")
        except OSError as exc:
            return f"<log read failed: {exc!r}>"


def _send_group(pgid: int, sig: signal.Signals) -> bool:
    try:
        os.killpg(pgid, sig)
        return True
    except ProcessLookupError:
        return False
    except PermissionError as exc:
        raise GateFailure(
            f"cannot signal owned process group {pgid}: {exc}",
            {"pgid": pgid, "signal": sig.name},
        ) from exc


def port_probe(port: int, timeout: float = 0.25) -> dict[str, Any]:
    started = time.monotonic()
    sock: socket.socket | None = None
    try:
        sock = socket.create_connection((HOST, port), timeout=max(0.01, timeout))
        return {"open": True, "elapsed_ms": round((time.monotonic() - started) * 1000, 3)}
    except OSError as exc:
        return {
            "open": False,
            "elapsed_ms": round((time.monotonic() - started) * 1000, 3),
            "error": f"{type(exc).__name__}: {exc}",
        }
    finally:
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass


def wait_port_closed(port: int, deadline: float) -> dict[str, Any]:
    probes: list[dict[str, Any]] = []
    while time.monotonic() < deadline:
        probe = port_probe(port, min(0.25, max(0.01, deadline - time.monotonic())))
        probes.append(probe)
        if not probe["open"]:
            return {"closed": True, "probes": probes[-8:]}
        time.sleep(min(0.05, max(0.01, deadline - time.monotonic())))
    return {"closed": False, "probes": probes[-8:]}


def stop_owned(
    handle: OwnedProcess,
    *,
    request_signal: bool,
    expected_returncode: int | None,
    wait_seconds: float = STOP_TIMEOUT,
    port_close_seconds: float = SETTLE_TIMEOUT,
) -> dict[str, Any]:
    """Terminate a whole owned process group, reap its Popen, and inspect leaks."""
    if handle.stop_record is not None:
        return handle.stop_record
    _, members_before, listeners_before = handle.observe_ownership()
    signals_sent: list[str] = []
    forced = False
    natural_exit = handle.proc.poll() is not None

    if handle.proc.poll() is None and request_signal:
        if _send_group(handle.pgid, signal.SIGTERM):
            signals_sent.append("SIGTERM")

    deadline = monotonic_deadline(wait_seconds)
    while handle.proc.poll() is None and time.monotonic() < deadline:
        handle.observe_ownership()
        try:
            handle.proc.wait(timeout=min(0.15, max(0.01, deadline - time.monotonic())))
        except subprocess.TimeoutExpired:
            continue
    if handle.proc.poll() is None:
        forced = True
        if _send_group(handle.pgid, signal.SIGKILL):
            signals_sent.append("SIGKILL")
        kill_deadline = monotonic_deadline(KILL_TIMEOUT)
        while handle.proc.poll() is None and time.monotonic() < kill_deadline:
            handle.observe_ownership()
            try:
                handle.proc.wait(timeout=min(0.15, max(0.01, kill_deadline - time.monotonic())))
            except subprocess.TimeoutExpired:
                continue

    reaped = False
    if handle.proc.poll() is not None:
        reap_deadline = monotonic_deadline(2.0)
        while time.monotonic() < reap_deadline:
            try:
                handle.proc.wait(timeout=min(0.15, max(0.01, reap_deadline - time.monotonic())))
                reaped = handle.proc.returncode is not None
                break
            except subprocess.TimeoutExpired:
                continue

    settle_deadline = monotonic_deadline(SETTLE_TIMEOUT)
    table_after: dict[int, ProcInfo] = {}
    members_after: list[ProcInfo] = []
    listeners_after: list[dict[str, Any]] = []
    while time.monotonic() < settle_deadline:
        table_after, members_after, listeners_after = handle.observe_ownership()
        known_alive = [info for pid, info in table_after.items() if pid in handle.seen_pids]
        if not members_after and not known_alive and not listeners_after:
            break
        time.sleep(min(0.05, max(0.01, settle_deadline - time.monotonic())))
    table_after, members_after, listeners_after = handle.observe_ownership()
    known_alive = [info for pid, info in table_after.items() if pid in handle.seen_pids]

    port_probes: dict[str, Any] = {}
    for port in sorted(handle.seen_ports | ({handle.port} if handle.port else set())):
        port_probes[str(port)] = wait_port_closed(port, monotonic_deadline(port_close_seconds))

    try:
        handle.log_file.flush()
        handle.log_file.close()
    except OSError:
        pass

    exact = bool(
        reaped
        and not members_after
        and not known_alive
        and not listeners_after
        and all(item.get("closed", False) for item in port_probes.values())
    )
    returncode = handle.proc.returncode
    expected_ok = expected_returncode is None or returncode == expected_returncode
    record = {
        "pid": handle.proc.pid,
        "pgid": handle.pgid,
        "command": handle.command,
        "cwd": handle.cwd,
        "port": handle.port,
        "natural_exit_before_cleanup": natural_exit,
        "signals_sent": signals_sent,
        "forced_escalation": forced,
        "returncode": returncode,
        "reaped": reaped,
        "expected_returncode": expected_returncode,
        "expected_returncode_ok": expected_ok,
        "seen_pids": sorted(handle.seen_pids),
        "group_before": [item.as_dict() for item in members_before],
        "listeners_before": listeners_before,
        "group_after": [item.as_dict() for item in members_after],
        "owned_processes_after": [item.as_dict() for item in known_alive],
        "orphan_pids": sorted(item.pid for item in known_alive),
        "orphan_group_pids": sorted(item.pid for item in members_after),
        "listeners_after": listeners_after,
        "orphan_listener_ports": sorted({int(item["port"]) for item in listeners_after}),
        "port_probes": port_probes,
        "exact_no_orphan_cleanup": exact,
    }
    handle.stop_record = record
    return record


def recv_exact(sock: socket.socket, size: int, deadline: float) -> bytes:
    data = bytearray()
    while len(data) < size:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("socket read deadline expired")
        sock.settimeout(min(0.25, remaining))
        try:
            chunk = sock.recv(size - len(data))
        except socket.timeout:
            continue
        if not chunk:
            raise EOFError("peer closed while reading frame")
        data.extend(chunk)
    return bytes(data)


def recv_wire_varint(sock: socket.socket, deadline: float) -> tuple[int, bytes]:
    raw = bytearray()
    for _ in range(5):
        byte = recv_exact(sock, 1, deadline)
        raw.extend(byte)
        if not (byte[0] & 0x80):
            value, _ = read_varint(bytes(raw), 0)
            return value, bytes(raw)
    raise ValueError("wire VarInt overflow")


def recv_frame(sock: socket.socket, deadline: float) -> tuple[bytes, bytes]:
    length, length_bytes = recv_wire_varint(sock, deadline)
    if length <= 0 or length > 8 * 1024 * 1024:
        raise ValueError(f"invalid frame length {length}")
    return length_bytes, recv_exact(sock, length, deadline)


def send_packet(sock: socket.socket, packet_id: int, payload: bytes, deadline: float) -> None:
    body = write_varint(packet_id) + payload
    wire = write_varint(len(body)) + body
    if time.monotonic() >= deadline:
        raise TimeoutError("socket write deadline expired")
    sock.settimeout(min(0.5, max(0.01, deadline - time.monotonic())))
    sock.sendall(wire)


def one_status_probe(port: int, timeout: float) -> dict[str, Any]:
    """Require a valid status response and matching ping, not just connect()."""
    deadline = monotonic_deadline(timeout)
    started = time.monotonic()
    sock: socket.socket | None = None
    try:
        remaining = max(0.05, deadline - time.monotonic())
        sock = socket.create_connection((HOST, port), timeout=min(0.75, remaining))
        sock.settimeout(min(0.5, remaining))
        handshake = (
            write_varint(PROTOCOL)
            + pack_string(HOST)
            + struct.pack(">H", port)
            + write_varint(1)
        )
        send_packet(sock, 0x00, handshake, deadline)
        send_packet(sock, 0x00, b"", deadline)
        _, frame = recv_frame(sock, deadline)
        packet_id, offset = read_varint(frame, 0)
        if packet_id != 0x00:
            raise GateFailure("status response packet id mismatch", {"packet_id": packet_id})
        payload = io.BytesIO(frame[offset:])
        status = json.loads(unpack_string(payload))
        if payload.tell() != len(frame[offset:]):
            raise GateFailure("status response contains trailing bytes")
        ping_value = int(time.time() * 1000) & 0x7FFFFFFFFFFFFFFF
        send_packet(sock, 0x01, struct.pack(">q", ping_value), deadline)
        _, pong_frame = recv_frame(sock, deadline)
        pong_id, pong_offset = read_varint(pong_frame, 0)
        if pong_id != 0x01 or pong_frame[pong_offset:] != struct.pack(">q", ping_value):
            raise GateFailure("status ping response mismatch", {"packet_id": pong_id})
        version = status.get("version") if isinstance(status, dict) else None
        players = status.get("players") if isinstance(status, dict) else None
        if not isinstance(version, dict) or version.get("protocol") != PROTOCOL or version.get("name") != "1.21.4":
            raise GateFailure("status version is not protocol 769 / 1.21.4", {"status": status})
        if not isinstance(players, dict) or not isinstance(players.get("online"), int):
            raise GateFailure("status players.online is missing or not an integer", {"status": status})
        if "description" not in status:
            raise GateFailure("status description is missing", {"status": status})
        return {
            "ok": True,
            "port": port,
            "elapsed_ms": round((time.monotonic() - started) * 1000, 3),
            "version": version,
            "players": players,
            "description_present": True,
            "ping_verified": True,
        }
    finally:
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass


def health_probe(port: int, timeout: float = HEALTH_TIMEOUT) -> dict[str, Any]:
    started = time.monotonic()
    try:
        result = one_status_probe(port, timeout)
        result["attempts"] = 1
        return result
    except Exception as exc:
        return {
            "ok": False,
            "port": port,
            "elapsed_ms": round((time.monotonic() - started) * 1000, 3),
            "error": f"{type(exc).__name__}: {exc}",
        }


def wait_for_ready(handle: OwnedProcess, timeout: float, expected_port: int | None = None) -> dict[str, Any]:
    started = time.monotonic()
    deadline = monotonic_deadline(timeout)
    errors: list[str] = []
    attempts = 0
    while time.monotonic() < deadline:
        handle.observe_ownership()
        if handle.proc.poll() is not None:
            raise GateFailure(
                "server exited before protocol readiness",
                {"returncode": handle.proc.returncode, "log": handle.log_text()[-4000:]},
            )
        _, _, listeners = handle.observe_ownership()
        ports = sorted({int(item["port"]) for item in listeners})
        if expected_port is not None and expected_port not in ports:
            ports = []
        if len(ports) == 1:
            port = ports[0]
            attempts += 1
            try:
                health = one_status_probe(port, min(1.5, max(0.1, deadline - time.monotonic())))
                handle.port = port
                handle.ready = health
                return {
                    "ready": True,
                    "port": port,
                    "attempts": attempts,
                    "health": health,
                    "elapsed_ms": round((time.monotonic() - started) * 1000, 3),
                }
            except Exception as exc:
                errors.append(f"port {port}: {type(exc).__name__}: {exc}")
        elif len(ports) > 1:
            raise GateFailure("owned server exposed more than one listener", {"listeners": listeners})
        else:
            errors.append("no owned listener yet")
        time.sleep(min(0.05, max(0.01, deadline - time.monotonic())))
    raise GateFailure(
        "server did not reach protocol readiness before deadline",
        {"errors": errors[-12:], "seen_ports": sorted(handle.seen_ports), "log": handle.log_text()[-4000:]},
    )


def play_probe(port: int, name: str, timeout: float = PLAY_TIMEOUT) -> dict[str, Any]:
    """Require login, configuration completion, JoinGame, and a chunk packet."""
    if Conn is None:
        raise GateFailure("mcproto import failed", {"error": _MC_PROTO_IMPORT_ERROR})
    deadline = monotonic_deadline(timeout)
    client = None
    packets: list[int] = []
    try:
        client = Conn(HOST, port, timeout=min(2.0, max(0.1, deadline - time.monotonic())))
        client.login(name)
        client.config_finish(max_seconds=max(0.1, deadline - time.monotonic()))
        joined = False
        chunk = False
        while time.monotonic() < deadline and not (joined and chunk):
            client.sock.settimeout(min(0.5, max(0.05, deadline - time.monotonic())))
            try:
                packet_id, payload = client.recv_packet()
            except socket.timeout:
                continue
            packets.append(packet_id)
            if packet_id == 0x2C:
                joined = True
            elif packet_id == 0x28:
                chunk = True
            elif packet_id == 0x42:
                teleport_id, _ = read_varint(io.BytesIO(payload))
                client.send_packet_raw(0x00, write_varint(teleport_id))
            elif packet_id == 0x27:
                client.send_packet_raw(0x1A, payload)
        if not joined or not chunk:
            raise GateFailure(
                "login/configuration did not reach JoinGame and LevelChunkWithLight",
                {"joined": joined, "chunk": chunk, "packets": packets},
            )
        return {
            "ok": True,
            "name": name,
            "joined": joined,
            "chunk_received": chunk,
            "packets": packets,
        }
    finally:
        if client is not None:
            try:
                client.close()
            except OSError:
                pass


def malformed_protocol_probe(port: int, input_bytes: bytes, timeout: float = 2.5) -> dict[str, Any]:
    """Send one malformed public-boundary frame and require close/response."""
    started = time.monotonic()
    sock: socket.socket | None = None
    received = bytearray()
    observation = ""
    error = None
    try:
        sock = socket.create_connection((HOST, port), timeout=0.75)
        sock.sendall(input_bytes)
        deadline = monotonic_deadline(timeout)
        while time.monotonic() < deadline:
            sock.settimeout(min(0.25, max(0.01, deadline - time.monotonic())))
            try:
                data = sock.recv(4096)
            except socket.timeout:
                continue
            if not data:
                observation = "eof"
                break
            received.extend(data[:4096])
            observation = "response"
            break
        else:
            observation = "timeout"
    except Exception as exc:
        error = f"{type(exc).__name__}: {exc}"
        observation = "connect-or-write-error"
    finally:
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass
    return {
        "input_hex": input_bytes.hex(),
        "observation": observation,
        "rejected_or_closed": observation in {"eof", "response"},
        "received_hex": bytes(received).hex()[:256],
        "error": error,
        "elapsed_ms": round((time.monotonic() - started) * 1000, 3),
    }


def launch(
    binary: Path,
    cwd: Path,
    artifact_root: Path,
    label: str,
    args: list[str],
) -> OwnedProcess:
    cwd.mkdir(parents=True, exist_ok=True)
    log_path = artifact_root / "logs" / f"{label}.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_file = log_path.open("wb")
    env = os.environ.copy()
    env["CPPFM_SERVER_DIR"] = str(cwd)
    command = [str(binary), *args]
    try:
        process = subprocess.Popen(
            command,
            cwd=str(cwd),
            env=env,
            stdout=log_file,
            stderr=subprocess.STDOUT,
            close_fds=True,
            start_new_session=True,
        )
    except BaseException:
        log_file.close()
        raise
    handle = OwnedProcess(
        command=command,
        cwd=cwd,
        log_path=log_path,
        log_file=log_file,
        proc=process,
        pgid=process.pid,
    )
    handle.observe_ownership()
    return handle


def server_args(port: int, world: Path, extra: list[str] | None = None) -> list[str]:
    args = [
        f"--port={port}",
        f"--world-dir={world}",
        "--level-type=flat",
        "--view-distance=2",
        "--jvm=false",
        "--online-mode=false",
    ]
    if extra:
        args.extend(extra)
    return args


def start_server(
    binary: Path,
    root: Path,
    artifact_root: Path,
    label: str,
    world: Path,
    port: int,
    extra: list[str] | None = None,
    expected_port: int | None = None,
) -> tuple[OwnedProcess, dict[str, Any]]:
    handle = launch(binary, root, artifact_root, label, server_args(port, world, extra))
    try:
        ready = wait_for_ready(handle, READY_TIMEOUT, expected_port=expected_port)
        return handle, ready
    except BaseException as exc:
        cleanup = stop_owned(handle, request_signal=True, expected_returncode=None)
        if isinstance(exc, GateFailure):
            details = dict(exc.details)
            details["startup_cleanup"] = cleanup
            raise GateFailure(str(exc), details) from exc
        raise GateFailure(
            f"server startup failed: {type(exc).__name__}: {exc}",
            {"startup_cleanup": cleanup},
        ) from exc


def run_info_case(
    binary: Path,
    artifact_root: Path,
    label: str,
    flag: str,
    token: str,
) -> dict[str, Any]:
    cwd = artifact_root / "info" / label.lower()
    cwd.mkdir(parents=True, exist_ok=True)
    before = tree_snapshot(cwd)
    handle = launch(binary, cwd, artifact_root, label.lower(), [flag])
    cleanup = stop_owned(handle, request_signal=False, expected_returncode=0, wait_seconds=INFO_TIMEOUT)
    output = handle.log_text()
    after = tree_snapshot(cwd)
    no_side_effect = before == after and before == []
    ok = bool(
        cleanup["exact_no_orphan_cleanup"]
        and cleanup["expected_returncode_ok"]
        and token in output
        and no_side_effect
    )
    return {
        "matrix_id": f"P53-{label.upper()}",
        "missing_target": ["#7", "#9", "#72"],
        "input": {"flag": flag, "isolated_cwd": str(cwd)},
        "command": handle.command,
        "observation": {
            "exit_code": cleanup["returncode"],
            "token_present": token in output,
            "stdout_stderr": output[-4000:],
            "before_tree": before,
            "after_tree": after,
            "no_side_effect": no_side_effect,
            "cleanup": cleanup,
        },
        "provenance": "IMPLEMENTATION",
        "status": "PASS" if ok else "FAIL",
        "limitation": "Proves this binary's early informational path, not JVM/mod startup.",
        "artifacts": [relative_artifact(artifact_root, handle.log_path)],
        "error": None if ok else "help/version side-effect or cleanup assertion failed",
    }


def base_record(
    matrix_id: str,
    missing_target: list[str],
    input_value: dict[str, Any],
    command: Any,
    observation: dict[str, Any],
    status: str,
    limitation: str,
    artifacts: list[str] | None = None,
    error: str | None = None,
) -> dict[str, Any]:
    return {
        "matrix_id": matrix_id,
        "missing_target": missing_target,
        "input": input_value,
        "command": command,
        "observation": observation,
        "provenance": "IMPLEMENTATION",
        "status": status,
        "limitation": limitation,
        "artifacts": artifacts or [],
        "error": error,
    }


def run_lifecycle_and_negative(
    binary: Path,
    artifact_root: Path,
) -> list[dict[str, Any]]:
    """Run the shared-world lifecycle while retaining independent gate rows."""
    root = artifact_root / "lifecycle"
    world = root / "shared-world"
    primary_root = root / "primary-root"
    primary: OwnedProcess | None = None
    restart: OwnedProcess | None = None
    cli_probe: OwnedProcess | None = None
    collision: OwnedProcess | None = None
    records: dict[str, dict[str, Any]] = {}
    timeline: list[dict[str, Any]] = []
    first_cleanup: dict[str, Any] | None = None
    second_cleanup: dict[str, Any] | None = None
    first_ready: dict[str, Any] | None = None
    second_ready: dict[str, Any] | None = None
    first_play: dict[str, Any] | None = None
    second_play: dict[str, Any] | None = None
    first_port: int | None = None
    first_manifest: dict[str, Any] | None = None
    second_manifest: dict[str, Any] | None = None
    first_world_identity: dict[str, Any] | None = None
    errors: list[str] = []

    protocol_observations: list[dict[str, Any]] = []
    cli_observation: dict[str, Any] = {}
    lock_observation: dict[str, Any] = {}

    def mark(stage: str, **values: Any) -> None:
        timeline.append({"stage": stage, "t_monotonic": round(time.monotonic(), 6), **json_safe(values)})

    def failed_record(matrix_id: str, missing: list[str], reason: str) -> dict[str, Any]:
        return base_record(
            matrix_id,
            missing,
            {"world": str(world)},
            [],
            {"timeline": timeline, "reason": reason},
            "FAIL",
            "The required lifecycle/negative path was not independently completed.",
            error=reason,
        )

    try:
        world.mkdir(parents=True, exist_ok=True)
        mark("created", world=world)
        primary, first_ready = start_server(
            binary, primary_root, artifact_root, "primary-first", world, 0
        )
        first_port = int(first_ready["port"])
        mark("listening", pid=primary.proc.pid, port=first_port)
        first_play = play_probe(first_port, "P53LifeFirst")
        mark("play", health=first_ready["health"], play=first_play)

        lifecycle_start_observation = {
            "requested_port": 0,
            "discovered_port": first_port,
            "readiness": first_ready,
            "play": first_play,
            "pid": primary.proc.pid,
        }

        # Negative protocol corpus: every malformed input is followed by a
        # fresh, fully parsed status/ping probe on the still-live server.
        for negative_id, input_bytes in (
            ("outer-varint-high-bits", b"\xff\xff\xff\xff\x0f"),
            ("zero-length-frame", b"\x00"),
        ):
            malformed = malformed_protocol_probe(first_port, input_bytes)
            after = health_probe(first_port)
            item = {
                "id": negative_id,
                "malformed": malformed,
                "valid_health_after": after,
                "pass": bool(malformed["rejected_or_closed"] and after["ok"]),
            }
            protocol_observations.append(item)
            mark("malformed-protocol", id=negative_id, pass_value=item["pass"])
            if not item["pass"]:
                errors.append(f"protocol negative {negative_id} did not fail closed")
                raise GateFailure(
                    f"protocol negative {negative_id} failed its reject/health gate",
                    {"protocol_observations": protocol_observations},
                )

        # Malformed CLI value: the current parser emits a diagnostic and keeps
        # the other valid options.  The resulting process still needs a real
        # status/ping and a fully owned cleanup; a diagnostic alone is not a pass.
        cli_root = root / "cli-negative-root"
        cli_world = root / "cli-negative-world"
        cli_probe, cli_ready = start_server(
            binary,
            cli_root,
            artifact_root,
            "cli-negative",
            cli_world,
            0,
            extra=["--view-distance=not-an-integer"],
        )
        cli_log = cli_probe.log_text()
        cli_health = health_probe(int(cli_ready["port"]))
        cli_cleanup = stop_owned(cli_probe, request_signal=True, expected_returncode=0)
        cli_probe = None
        primary_health_after_cli = health_probe(first_port)
        cli_diagnostic = "invalid command-line value for --view-distance" in cli_log
        cli_ok = bool(
            cli_diagnostic
            and cli_ready["health"]["ok"]
            and cli_health["ok"]
            and cli_cleanup["exact_no_orphan_cleanup"]
            and cli_cleanup["expected_returncode_ok"]
            and primary_health_after_cli["ok"]
        )
        cli_observation = {
            "malformed_argument": "--view-distance=not-an-integer",
            "diagnostic_present": cli_diagnostic,
            "diagnostic_log": cli_log[-4000:],
            "server_health_after_malformed_cli": cli_health,
            "primary_valid_health_after_probe": primary_health_after_cli,
            "cleanup": cli_cleanup,
            "pass": cli_ok,
        }
        mark("malformed-cli", pass_value=cli_ok)
        if not cli_ok:
            errors.append("malformed CLI probe lacked diagnostic, health, or exact cleanup")
            raise GateFailure("malformed CLI probe failed its diagnostic/health/cleanup gate", cli_observation)

        # The second process uses a distinct runtime root but the exact same
        # world path, so its failure is a live session-lock collision rather
        # than a duplicate runtime-layout collision.
        lock_path = world / "session.lock"
        lock_before = lock_path.read_text(encoding="utf-8", errors="replace") if lock_path.exists() else ""
        collision_root = root / "collision-root"
        collision, _ = (launch(
            binary,
            collision_root,
            artifact_root,
            "session-lock-collision",
            server_args(0, world),
        ), None)
        collision_deadline = monotonic_deadline(12.0)
        while collision.proc.poll() is None and time.monotonic() < collision_deadline:
            collision.observe_ownership()
            time.sleep(min(0.05, max(0.01, collision_deadline - time.monotonic())))
        collision_cleanup = stop_owned(
            collision,
            request_signal=False,
            expected_returncode=1,
            wait_seconds=2.0,
        )
        collision_log = collision.log_text()
        lock_after = lock_path.read_text(encoding="utf-8", errors="replace") if lock_path.exists() else ""
        primary_health_after_collision = health_probe(first_port)
        lock_error = (
            "session.lock is already held" in collision_log
            or "world is already in use" in collision_log
            or "could not acquire world session lock" in collision_log
        )
        lock_observation = {
            "world": world,
            "lock_path": lock_path,
            "lock_exists_before": bool(lock_before),
            "lock_record_before": lock_before,
            "lock_unchanged_after_collision": bool(lock_before) and lock_before == lock_after,
            "collision_command": collision.command,
            "collision_pid": collision.proc.pid,
            "collision_returncode": collision_cleanup["returncode"],
            "collision_error_present": lock_error,
            "collision_log": collision_log[-4000:],
            "collision_cleanup": collision_cleanup,
            "primary_valid_health_after_collision": primary_health_after_collision,
        }
        lock_ok = bool(
            lock_before
            and lock_before.split()[0].isdigit()
            and int(lock_before.split()[0]) == primary.proc.pid
            and lock_observation["lock_unchanged_after_collision"]
            and lock_error
            and collision_cleanup["expected_returncode_ok"]
            and collision_cleanup["exact_no_orphan_cleanup"]
            and not collision_cleanup["listeners_before"]
            and primary_health_after_collision["ok"]
        )
        mark("session-lock-collision", pass_value=lock_ok)
        if not lock_ok:
            errors.append("live session-lock collision did not fail closed")
            raise GateFailure("live session-lock collision failed its reject/health/cleanup gate", lock_observation)
        collision = None

        # Save an identity-bearing manifest before the first clean stop.  The
        # directory inode and persisted file set are checked after restart;
        # merely starting another empty world is not sufficient.
        first_manifest = world_manifest(world)
        try:
            first_world_identity = {
                "device": world.stat().st_dev,
                "inode": world.stat().st_ino,
            }
        except OSError as exc:
            first_world_identity = {"error": repr(exc)}
        mark("saving", manifest=first_manifest)

        first_cleanup = stop_owned(primary, request_signal=True, expected_returncode=0)
        first_log = primary.log_text()
        first_bye = "[cppfm] bye" in first_log
        primary = None
        mark("stopping", cleanup=first_cleanup)
        if not first_cleanup["exact_no_orphan_cleanup"] or "SIGTERM" not in first_cleanup["signals_sent"] or not first_bye:
            errors.append("first SIGTERM shutdown was not exact")

        # Reuse the observed ephemeral port, not a fixed shared port.  This is
        # only attempted after the port-close evidence above says it is free.
        if first_port is None or not all(
            item.get("closed", False) for item in first_cleanup["port_probes"].values()
        ):
            raise GateFailure("cannot attempt port reuse after failed port-close proof", {"cleanup": first_cleanup})
        restart_root = root / "restart-root"
        restart, second_ready = start_server(
            binary,
            restart_root,
            artifact_root,
            "restart-same-world",
            world,
            first_port,
            expected_port=first_port,
        )
        second_play = play_probe(first_port, "P53LifeAgain")
        mark("restart", pid=restart.proc.pid, port=first_port, play=second_play)
        second_manifest = world_manifest(world)
        second_cleanup = stop_owned(restart, request_signal=True, expected_returncode=0)
        second_log = restart.log_text()
        second_bye = "[cppfm] bye" in second_log
        restart = None
        mark("exited", cleanup=second_cleanup)

        preserved_paths = sorted(
            {item.get("path") for item in first_manifest.get("files", [])}
            & {item.get("path") for item in second_manifest.get("files", [])}
        ) if first_manifest and second_manifest else []
        try:
            second_stat = world.stat()
            same_directory = bool(
                first_world_identity
                and first_world_identity.get("device") == second_stat.st_dev
                and first_world_identity.get("inode") == second_stat.st_ino
            )
        except OSError:
            same_directory = False
        world_identity = {
            "path": world,
            "same_directory_inode": same_directory,
            "first": first_manifest,
            "second": second_manifest,
            "preserved_paths": preserved_paths,
            "persisted_file_proof": bool(preserved_paths),
        }
        lifecycle_ok = bool(
            first_ready
            and first_ready["health"]["ok"]
            and first_play
            and first_play["ok"]
            and not errors
            and first_cleanup
            and first_cleanup["exact_no_orphan_cleanup"]
            and first_cleanup["expected_returncode_ok"]
            and "SIGTERM" in first_cleanup["signals_sent"]
            and first_bye
            and second_ready
            and second_ready["health"]["ok"]
            and second_play
            and second_play["ok"]
            and second_cleanup
            and second_cleanup["exact_no_orphan_cleanup"]
            and second_cleanup["expected_returncode_ok"]
            and "SIGTERM" in second_cleanup["signals_sent"]
            and second_bye
            and same_directory
            and bool(preserved_paths)
        )
        lifecycle_observation = {
            "timeline": timeline,
            "start": lifecycle_start_observation,
            "first_shutdown": first_cleanup,
            "first_bye_log": first_bye,
            "port_reuse": {
                "reused_port": first_port,
                "closed_before_reuse": all(
                    item.get("closed", False) for item in first_cleanup["port_probes"].values()
                ),
                "restart_readiness": second_ready,
            },
            "restart_play": second_play,
            "second_shutdown": second_cleanup,
            "second_bye_log": second_bye,
            "world_identity": world_identity,
            "errors": errors,
        }
        records["lifecycle"] = base_record(
            "P53-LIFECYCLE",
            ["#7", "#9", "#72", "#76"],
            {"requested_initial_port": 0, "world": world},
            {
                "initial": first_cleanup["command"],
                "restart": second_cleanup["command"],
            },
            lifecycle_observation,
            "PASS" if lifecycle_ok else "FAIL",
            "This is an implementation lifecycle gate; it does not prove arbitrary Fabric JVM-mod compatibility or long-run soak behavior.",
            artifacts=[
                "logs/primary-first.log",
                "logs/restart-same-world.log",
            ],
            error=None if lifecycle_ok else "lifecycle stage, persistence identity, or exact cleanup failed",
        )
        records["protocol"] = base_record(
            "P53-NEGATIVE-PROTOCOL",
            ["#71", "#72", "#73", "#74", "#75", "#76", "#77", "#78", "#79"],
            {"cases": [item["id"] for item in protocol_observations], "port": first_port},
            {"primary": first_cleanup["command"]},
            {
                "cases": protocol_observations,
                "all_rejected_and_healthy": bool(protocol_observations) and all(item["pass"] for item in protocol_observations),
                "primary_final_cleanup": second_cleanup,
            },
            "PASS" if protocol_observations and all(item["pass"] for item in protocol_observations) else "FAIL",
            "The corpus is intentionally small and does not prove all decoder mutations, compression bombs, or sanitizer coverage.",
            artifacts=["logs/primary-first.log"],
            error=None if protocol_observations and all(item["pass"] for item in protocol_observations) else "malformed protocol or post-case health assertion failed",
        )
        records["cli"] = base_record(
            "P53-NEGATIVE-CLI",
            ["#5", "#6", "#9", "#10", "#72"],
            {"argument": "--view-distance=not-an-integer", "port": 0},
            cli_observation.get("cleanup", {}).get("command", []),
            cli_observation,
            "PASS" if cli_observation.get("pass") else "FAIL",
            "The current CLI contract reports an invalid value and retains the valid ephemeral-port configuration; other malformed property permutations remain outside this row.",
            artifacts=["logs/cli-negative.log"],
            error=None if cli_observation.get("pass") else "malformed CLI or valid-after health assertion failed",
        )
        records["lock"] = base_record(
            "P53-SESSION-LOCK",
            ["#7", "#9", "#72", "#76"],
            {"world": world, "same_world_second_process": True},
            lock_observation.get("collision_command", []),
            lock_observation,
            "PASS" if lock_ok else "FAIL",
            "This proves the live POSIX session-lock boundary for this implementation, not crash recovery or stale-lock replacement.",
            artifacts=["logs/session-lock-collision.log", "logs/primary-first.log"],
            error=None if lock_ok else "live session-lock collision or primary health assertion failed",
        )
    except BaseException as exc:
        reason = f"{type(exc).__name__}: {exc}"
        details = exc.details if isinstance(exc, GateFailure) else {}
        errors.append(reason)
        for key, matrix_id, missing in (
            ("lifecycle", "P53-LIFECYCLE", ["#7", "#9", "#72", "#76"]),
            ("protocol", "P53-NEGATIVE-PROTOCOL", ["#71", "#72", "#73", "#74", "#75", "#76", "#77", "#78", "#79"]),
            ("cli", "P53-NEGATIVE-CLI", ["#5", "#6", "#9", "#10", "#72"]),
            ("lock", "P53-SESSION-LOCK", ["#7", "#9", "#72", "#76"]),
        ):
            records.setdefault(
                key,
                base_record(
                    matrix_id,
                    missing,
                    {"world": world},
                    [],
                    {"timeline": timeline, "details": details, "errors": errors},
                    "FAIL",
                    "The required path was not completed; no partial observation is promoted to PASS.",
                    error=reason,
                ),
            )
    finally:
        # Every handle is stopped by PID-owned process-group operations.  This
        # fallback is deliberately exact and never searches or signals by a
        # command-line substring.
        for handle in (collision, cli_probe, restart, primary):
            if handle is not None and handle.stop_record is None:
                try:
                    cleanup = stop_owned(handle, request_signal=True, expected_returncode=None)
                    errors.append(
                        f"fallback cleanup for pid {handle.proc.pid}: exact={cleanup['exact_no_orphan_cleanup']}"
                    )
                except BaseException as exc:
                    errors.append(f"fallback cleanup failed for pid {handle.proc.pid}: {exc!r}")

    # If an exception happened after a handle had been stopped but before its
    # normal result was assembled, retain all cleanup evidence in the aggregate.
    if "lifecycle" in records:
        records["lifecycle"]["observation"]["fallback_errors"] = errors
        if errors:
            records["lifecycle"]["status"] = "FAIL"
            records["lifecycle"]["error"] = "lifecycle gate recorded one or more failures"
    ordered = ["lifecycle", "protocol", "cli", "lock"]
    return [records[key] for key in ordered if key in records]


class MatrixRunner:
    def __init__(self, binary: Path, keep_artifacts: bool, timeout: float):
        self.binary = binary
        self.keep_artifacts = keep_artifacts
        self.timeout = timeout
        self.started = time.monotonic()
        parent = Path("/tmp/opencode")
        if not parent.is_dir():
            parent = Path(tempfile.gettempdir())
        self.artifact_root = Path(tempfile.mkdtemp(prefix="cppfm-lifecycle-matrix-", dir=str(parent)))
        (self.artifact_root / "logs").mkdir()
        self.results_path = self.artifact_root / "results.jsonl"
        self.results_file = self.results_path.open("w", encoding="utf-8")
        self.records: list[dict[str, Any]] = []

    def snapshot(self) -> dict[str, Any]:
        try:
            binary_stat = self.binary.stat()
            binary_detail = {"path": self.binary, "size": binary_stat.st_size, "mode": stat.S_IMODE(binary_stat.st_mode)}
        except OSError as exc:
            binary_detail = {"path": self.binary, "error": repr(exc)}
        return {
            "head": read_git_head(REPO_ROOT),
            "binary": binary_detail,
            "python": sys.version,
            "platform": platform.platform(),
            "machine": platform.machine(),
            "host": platform.node(),
            "date_utc": now_utc(),
        }

    def emit(self, record: dict[str, Any]) -> None:
        record = json_safe(record)
        record["snapshot"] = json_safe(self.snapshot())
        record["artifact_root"] = relative_artifact(self.artifact_root.parent, self.artifact_root)
        safe_id = re.sub(r"[^A-Za-z0-9_.-]+", "_", str(record["matrix_id"]))
        case_path = self.artifact_root / "cases" / f"{safe_id}.json"
        case_path.parent.mkdir(parents=True, exist_ok=True)
        artifact_paths = list(record.get("artifacts", []))
        artifact_paths.append(relative_artifact(self.artifact_root, case_path))
        record["artifacts"] = sorted(set(artifact_paths))
        case_path.write_text(json.dumps(record, ensure_ascii=False, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        line = json.dumps(record, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        self.results_file.write(line + "\n")
        self.results_file.flush()
        print(line, flush=True)
        self.records.append(record)

    def finalise(self, fatal: str | None = None) -> int:
        failed = any(record.get("status") == "FAIL" for record in self.records)
        if fatal:
            failed = True
        summary = {
            "record": "summary",
            "matrix_id": "P53-SUMMARY",
            "status": "FAIL" if failed else "PASS",
            "cases": len(self.records),
            "pass": sum(record.get("status") == "PASS" for record in self.records),
            "fail": sum(record.get("status") == "FAIL" for record in self.records),
            "fatal": fatal,
            "elapsed_ms": round((time.monotonic() - self.started) * 1000, 3),
            "artifact_root": relative_artifact(self.artifact_root.parent, self.artifact_root),
            "artifacts_retained": bool(self.keep_artifacts or failed),
        }
        line = json.dumps(json_safe(summary), ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        self.results_file.write(line + "\n")
        self.results_file.flush()
        self.results_file.close()
        print(line, flush=True)
        retain = self.keep_artifacts or failed
        if not retain:
            try:
                shutil.rmtree(self.artifact_root)
            except OSError:
                # The result is already fail-closed; expose deletion failure in
                # the process exit rather than pretending cleanup was exact.
                return 1
        return 1 if failed else 0

    def run(self) -> int:
        if os.name != "posix":
            self.emit(base_record(
                "P53-PLATFORM",
                ["#7", "#72"],
                {"os_name": os.name},
                [],
                {"error": "POSIX process groups and /proc ownership inspection are required"},
                "FAIL",
                "This runner intentionally fails closed on hosts without the required ownership primitives.",
                error="unsupported host platform",
            ))
            return self.finalise()
        if _MC_PROTO_IMPORT_ERROR:
            self.emit(base_record(
                "P53-PROTOCOL-HELPER",
                ["#71", "#72"],
                {},
                [],
                {"error": _MC_PROTO_IMPORT_ERROR},
                "FAIL",
                "The existing tests/mcproto.py helper is required for the play gate.",
                error="mcproto import failed",
            ))
            return self.finalise()

        try:
            self.emit(run_info_case(self.binary, self.artifact_root, "help", "--help", "Usage:"))
            self.emit(run_info_case(self.binary, self.artifact_root, "version", "--version", "protocol 769"))
            for record in run_lifecycle_and_negative(self.binary, self.artifact_root):
                self.emit(record)
            if time.monotonic() - self.started > self.timeout:
                raise GateFailure("runner overall monotonic deadline expired")
            return self.finalise()
        except BaseException as exc:
            fatal = f"{type(exc).__name__}: {exc}"
            self.emit(base_record(
                "P53-RUNNER",
                ["#7", "#72", "#76"],
                {"binary": self.binary},
                [],
                {"error": fatal},
                "FAIL",
                "Unexpected harness failure is never converted to SKIP or PASS.",
                error=fatal,
            ))
            return self.finalise(fatal)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, help="path to the cppfm executable")
    parser.add_argument(
        "--keep-artifacts",
        action="store_true",
        help="retain the per-run artifact directory (failed runs retain it automatically)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=240.0,
        help="overall monotonic runner deadline in seconds (default: 240)",
    )
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv if argv is not None else sys.argv[1:])
    binary = Path(args.binary).expanduser().resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        # Keep argument failures structured too, while still avoiding a child.
        print(json.dumps({
            "record": "summary",
            "matrix_id": "P53-SUMMARY",
            "status": "FAIL",
            "cases": 0,
            "pass": 0,
            "fail": 1,
            "fatal": f"binary is not executable: {binary}",
        }, sort_keys=True, separators=(",", ":")))
        return 1
    runner = MatrixRunner(binary, bool(args.keep_artifacts), float(args.timeout))
    return runner.run()


if __name__ == "__main__":
    raise SystemExit(main())
