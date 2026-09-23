#!/usr/bin/env python3
"""Owned live entry/settings matrix for the shipped ``cppfm`` executable.

This is intentionally an integration runner, not a socket stub: every
observation comes from a process started through the product entry point and
the existing protocol client in ``tests/mcproto.py``.  The two launches share
one world directory so the second launch also exercises restart/recovery.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE))

from mcproto import (  # noqa: E402 - the helper is deliberately reused
    Conn,
    PROTOCOL,
    offline_uuid,
    pack_string,
    parse_login_success,
    read_varint,
)

VERSION = "1.21.4"
HOST = "127.0.0.1"
STARTUP_SECONDS = 45.0
SHUTDOWN_SECONDS = 12.0
PLAY_SECONDS = 12.0
START_LINE = re.compile(r"CppFabricMC starting: port=(\d+) view=(\d+).* level=(\w+)")


def free_port(exclude: set[int] | None = None) -> int:
    excluded = exclude or set()
    while True:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            sock.bind((HOST, 0))
            port = int(sock.getsockname()[1])
        finally:
            sock.close()
        if port != 25565 and port not in excluded:
            return port


def digest(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


class OwnedServer:
    """One process and one process group owned by this test instance."""

    def __init__(self, command: list[str], root: Path, log_path: Path) -> None:
        self.command = command
        self.root = root
        self.log_path = log_path
        self.root.mkdir(parents=True, exist_ok=True)
        source_assets = REPO / "assets"
        asset_link = self.root / "assets"
        if source_assets.is_dir() and not asset_link.exists():
            asset_link.symlink_to(source_assets, target_is_directory=True)
        self.log_file = log_path.open("wb")
        env = os.environ.copy()
        env["CPPFM_SERVER_DIR"] = str(root)
        env["LC_ALL"] = "C"
        try:
            self.proc = subprocess.Popen(
                command,
                cwd=str(root),
                env=env,
                stdin=subprocess.DEVNULL,
                stdout=self.log_file,
                stderr=subprocess.STDOUT,
                close_fds=True,
                start_new_session=True,
            )
            self.pgid = os.getpgid(self.proc.pid)
        except BaseException:
            self.log_file.close()
            raise

    def stop(self) -> dict[str, object]:
        """Bound cleanup to this server's process group, never global PIDs."""
        escalated = False
        issues: list[str] = []
        try:
            if self.proc.poll() is None:
                try:
                    os.killpg(self.pgid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    self.proc.wait(timeout=SHUTDOWN_SECONDS)
                except subprocess.TimeoutExpired:
                    escalated = True
                    try:
                        os.killpg(self.pgid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    try:
                        self.proc.wait(timeout=3.0)
                    except subprocess.TimeoutExpired:
                        issues.append("owned process group did not exit after SIGKILL")
            if self.proc.poll() is None:
                issues.append(f"owned pid {self.proc.pid} remains")
            try:
                os.killpg(self.pgid, 0)
            except ProcessLookupError:
                pass
            except PermissionError:
                issues.append(f"could not inspect owned process group {self.pgid}")
            else:
                issues.append(f"owned process group {self.pgid} remains")
        finally:
            self.log_file.close()
        return {
            "pid": self.proc.pid,
            "pgid": self.pgid,
            "returncode": self.proc.returncode,
            "escalated": escalated,
            "issues": issues,
        }

    def log_text(self) -> str:
        try:
            return self.log_path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            return ""


def wait_status(server: OwnedServer, port: int) -> dict[str, object]:
    deadline = time.monotonic() + STARTUP_SECONDS
    errors: list[str] = []
    while time.monotonic() < deadline:
        if server.proc.poll() is not None:
            raise RuntimeError(f"cppfm exited before readiness: {server.log_text()[-1600:]}")
        conn: Conn | None = None
        try:
            conn = Conn(HOST, port, timeout=3)
            status = conn.status()
            version = status.get("version")
            if not isinstance(version, dict):
                raise RuntimeError(f"status has no version object: {status!r}")
            if version.get("protocol") != PROTOCOL or version.get("name") != VERSION:
                raise RuntimeError(f"unexpected status version: {version!r}")
            if not status.get("description"):
                raise RuntimeError("status description was empty")
            return {
                "protocol": version["protocol"],
                "name": version["name"],
                "description": status["description"],
                "players": status.get("players"),
                "enforcesSecureChat": status.get("enforcesSecureChat"),
            }
        except (OSError, EOFError, ValueError, RuntimeError) as exc:
            errors.append(f"{type(exc).__name__}: {exc}")
            time.sleep(0.1)
        finally:
            if conn is not None:
                conn.close()
    raise TimeoutError(f"status readiness expired on {port}: {errors[-5:]}")


def protocol_flow(port: int, username: str) -> dict[str, object]:
    """Run status was checked separately; then exercise login/config/play."""
    conn = Conn(HOST, port, timeout=20)
    login_packets: list[dict[str, object]] = []
    config_packets: list[dict[str, object]] = []
    play_packets: list[dict[str, object]] = []
    try:
        conn.handshake(2)
        conn.send_packet_raw(0x00, pack_string(username) + bytes.fromhex(offline_uuid(username)))
        login_success: dict[str, object] | None = None
        compression: int | None = None
        while login_success is None:
            packet_id, payload = conn.recv_packet()
            login_packets.append({"id": packet_id, "length": len(payload), "sha256": digest(payload)})
            if packet_id == 0x03:
                compression, _ = read_varint(payload, 0)
                conn.compression_threshold = compression
            elif packet_id == 0x02:
                login_success = parse_login_success(payload)
            elif packet_id == 0x00:
                raise RuntimeError(f"login rejected: {payload[:400].hex()}")
            else:
                raise RuntimeError(f"unexpected login packet 0x{packet_id:02x}")
        conn.send_packet_raw(0x03, b"")

        def save_config(packet_id: int, payload: bytes) -> None:
            config_packets.append({"id": packet_id, "length": len(payload), "sha256": digest(payload)})

        conn.config_finish(sink=save_config, max_seconds=25)
        if not config_packets or not any(item["length"] for item in config_packets):
            raise RuntimeError("configuration produced no non-empty server response")

        deadline = time.monotonic() + PLAY_SECONDS
        conn.sock.settimeout(2.0)
        join_seen = False
        while time.monotonic() < deadline and len(play_packets) < 32:
            conn.sock.settimeout(max(0.1, min(2.0, deadline - time.monotonic())))
            try:
                packet_id, payload = conn.recv_packet()
            except socket.timeout:
                continue
            play_packets.append({"id": packet_id, "length": len(payload), "sha256": digest(payload)})
            if packet_id == 0x2C:  # Login (play join-game)
                join_seen = True
            elif packet_id == 0x27:  # server keep-alive
                conn.send_packet_raw(0x1A, payload)
            if join_seen and len(play_packets) >= 2:
                break
        if not play_packets or not any(item["length"] for item in play_packets):
            raise RuntimeError("play phase produced no non-empty server response")
        if not join_seen:
            raise RuntimeError(f"play phase never sent Join Game: {play_packets!r}")
        return {
            "login": {
                "success": {"uuid": login_success["uuid"], "name": login_success["name"]},
                "compression": compression,
                "packets": login_packets,
            },
            "configuration": {
                "packets": config_packets,
                "non_empty_packets": sum(1 for item in config_packets if item["length"]),
            },
            "play": {
                "packets": play_packets,
                "join_game": join_seen,
                "keep_alive_replies": sum(1 for item in play_packets if item["id"] == 0x27),
            },
        }
    finally:
        conn.close()


def startup_observation(server: OwnedServer, expected_port: int, expected_view: int) -> dict[str, object]:
    match = START_LINE.search(server.log_text())
    if not match:
        raise RuntimeError(f"startup line missing for pid {server.proc.pid}")
    observed = {"port": int(match.group(1)), "view_distance": int(match.group(2)), "level_type": match.group(3)}
    if observed != {"port": expected_port, "view_distance": expected_view, "level_type": "flat"}:
        raise RuntimeError(f"CLI/properties startup result mismatch: {observed!r}")
    return observed


def run(binary: Path, artifact_root: Path) -> dict[str, object]:
    artifact_root.mkdir(parents=True, exist_ok=True)
    world = artifact_root / "shared-world"
    world.mkdir()
    marker = world / "goal-live-marker.bin"
    marker.write_bytes(b"goal-live-matrix-v1\n")
    marker_hash = digest(marker.read_bytes())
    used_ports: set[int] = set()
    port_a = free_port(used_ports)
    used_ports.add(port_a)
    property_port_a = free_port(used_ports)
    used_ports.add(property_port_a)
    port_b = free_port(used_ports)
    launches: list[dict[str, object]] = []
    active: list[OwnedServer] = []

    def launch(name: str, port: int, property_port: int, view: int, motd: str) -> tuple[OwnedServer, dict[str, object]]:
        root = artifact_root / name
        root.mkdir()
        (root / "server.properties").write_text(
            "\n".join([
                f"server-port={property_port}",
                "level-type=flat",
                "view-distance=2",
                "simulation-distance=2",
                "online-mode=false",
                "enforce-secure-profile=false",
                "jvm=false",
                "compression-threshold=256",
                "level-seed=goal-live-seed",
                "motd=properties-motd",
                "",
            ]), encoding="utf-8")
        command = [
            str(binary), f"--port={port}", f"--world-dir={world}",
            "--level-type=flat", f"--view-distance={view}",
            "--simulation-distance=2", f"--motd={motd}", "--jvm=false",
            "--online-mode=false", "--enforce-secure-profile=false",
        ]
        server = OwnedServer(command, root, root / "server.log")
        active.append(server)
        try:
            status = wait_status(server, port)
            flow = protocol_flow(port, f"GoalLive{name[-1].upper()}")
            return server, {"name": name, "command": command, "status": status, "flow": flow}
        except BaseException:
            server.stop()
            active.remove(server)
            raise

    try:
        first, first_record = launch("launch-a", port_a, property_port_a, 3, "goal-live-cli-a")
        first_stop = first.stop()
        active.remove(first)
        first_record["startup"] = startup_observation(first, port_a, 3)
        first_record["shutdown"] = first_stop
        if first_stop["returncode"] != 0 or first_stop["escalated"] or first_stop["issues"]:
            raise RuntimeError(f"first launch cleanup failed: {first_stop!r}")
        launches.append(first_record)

        second, second_record = launch("launch-b", port_b, port_b, 2, "goal-live-cli-b")
        second_stop = second.stop()
        active.remove(second)
        second_record["startup"] = startup_observation(second, port_b, 2)
        second_record["shutdown"] = second_stop
        if second_stop["returncode"] != 0 or second_stop["escalated"] or second_stop["issues"]:
            raise RuntimeError(f"second launch cleanup failed: {second_stop!r}")
        launches.append(second_record)
    finally:
        for server in reversed(active):
            server.stop()

    recovered_hash = digest(marker.read_bytes())
    if recovered_hash != marker_hash:
        raise RuntimeError("restart changed the pre-existing world marker")
    level_dat = world / "level.dat"
    return {
        "matrix": "GOAL-LIVE-ENTRY-SETTINGS",
        "status": "PASS",
        "binary": str(binary),
        "protocol": PROTOCOL,
        "version": VERSION,
        "launches": launches,
        "recovery": {
            "same_world": True,
            "marker_sha256_before": marker_hash,
            "marker_sha256_after": recovered_hash,
            "level_dat_present": level_dat.is_file(),
            "level_dat_sha256": digest(level_dat.read_bytes()) if level_dat.is_file() else None,
        },
        "unavailable": {
            "real_vanilla_client_gui": "UNAVAILABLE: this headless environment has no client/GUI; protocol evidence is not a rendering claim",
            "client_rendering_and_input": "UNAVAILABLE: no fake client responses are generated",
        },
        "artifacts": str(artifact_root),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=REPO / "build" / "cppfm")
    parser.add_argument("--artifact-root", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        print(json.dumps({"matrix": "GOAL-LIVE-ENTRY-SETTINGS", "status": "BLOCKED", "error": f"binary is not executable: {binary}"}, sort_keys=True))
        return 2
    artifact_root = args.artifact_root or Path(tempfile.mkdtemp(prefix="cppfm-goal-live-"))
    def interrupt(signum: int, _frame: object) -> None:
        raise KeyboardInterrupt(f"received signal {signum}")

    old_sigint = signal.getsignal(signal.SIGINT)
    old_sigterm = signal.getsignal(signal.SIGTERM)
    signal.signal(signal.SIGINT, interrupt)
    signal.signal(signal.SIGTERM, interrupt)
    try:
        result = run(binary, artifact_root)
    except BaseException as exc:
        result = {
            "matrix": "GOAL-LIVE-ENTRY-SETTINGS",
            "status": "FAIL",
            "error": f"{type(exc).__name__}: {exc}",
            "artifacts": str(artifact_root),
        }
    finally:
        signal.signal(signal.SIGINT, old_sigint)
        signal.signal(signal.SIGTERM, old_sigterm)
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
