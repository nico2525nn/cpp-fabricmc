#!/usr/bin/env python3
"""Soak bot 1h 20 TPS — C-12 (move/combat/redstone/death, nightly). Dry 300s."""
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
import io
import math
import os
import select
import signal
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tests"))
import mcproto
from mcproto import Conn


PLAY_KEEP_ALIVE = mcproto.PLAY_CLIENTBOUND_KEEP_ALIVE
PLAY_KEEP_ALIVE_RESPONSE = mcproto.PLAY_SERVERBOUND_KEEP_ALIVE
PLAY_DISCONNECT = mcproto.PLAY_CLIENTBOUND_DISCONNECT
PLAY_LEVEL_CHUNK_WITH_LIGHT = mcproto.PLAY_CLIENTBOUND_LEVEL_CHUNK_WITH_LIGHT
PLAY_UPDATE_TIME = mcproto.PLAY_CLIENTBOUND_UPDATE_TIME
PLAY_CLIENTBOUND_PLAYER_POSITION = 0x42
PLAY_SERVERBOUND_ACCEPT_TELEPORTATION = 0x00
PLAY_CLIENTBOUND_CHUNK_BATCH_FINISHED = 0x0C
PLAY_SERVERBOUND_CHUNK_BATCH_RECEIVED = 0x09


@dataclass(frozen=True)
class PumpResult:
    status: str
    packets: int


def decode_disconnect_reason(data: bytes) -> str:
    """Decode the usual text-component string without hiding malformed data."""
    try:
        reason = mcproto.unpack_string(io.BytesIO(data))
        return reason[:400]
    except (UnicodeDecodeError, ValueError, IndexError):
        return f"<malformed:{data[:200].hex()}>"


class PlayPacketPump:
    """Read and dispatch a bounded number of already-ready Play packets.

    The first packet may wait for ``idle_timeout``.  Once a packet arrives,
    additional buffered/readable packets are drained with a zero wait, but the
    fixed packet budget always returns control to the soak loop.  Conn keeps
    incomplete VarInts and frame bodies in its receive buffer across timeout.
    """

    def __init__(self, conn: Conn, *, max_packets: int = 64, idle_timeout: float = 0.05):
        if max_packets < 1:
            raise ValueError("max_packets must be positive")
        if idle_timeout <= 0:
            raise ValueError("idle_timeout must be positive")
        self.conn = conn
        self.max_packets = max_packets
        self.idle_timeout = idle_timeout
        self.state = "PLAY"
        self.packet_counts: Counter[int] = Counter()
        self.keepalive_ids: list[int] = []
        self._keepalive_seen: set[int] = set()
        self.keepalives = 0
        self.keepalive_echoes = 0
        self.duplicate_keepalives = 0
        self.disconnects = 0
        self.disconnect_reason = ""
        self.eof_count = 0
        self.timeout_count = 0
        self.socket_errors = 0
        self.send_errors = 0
        self.protocol_errors = 0
        self.server_exit_count = 0
        self.server_exit_code: int | None = None
        self.loop_iterations = 0
        self.drain_limit_hits = 0
        self.last_rx_monotonic: float | None = None
        self.last_error = ""

    def _wait_for_packet(self, timeout: float) -> bool:
        if self.conn.has_buffered_packet():
            return True
        readable, _, _ = select.select([self.conn.sock], [], [], timeout)
        return bool(readable)

    def _transport_result(self, status: str, packets: int, error: BaseException) -> PumpResult:
        self.socket_errors += 1
        self.last_error = f"{type(error).__name__}: {error}"
        self.state = "CLOSED"
        return PumpResult(status, packets)

    def _protocol_result(self, packets: int, error: BaseException) -> PumpResult:
        self.protocol_errors += 1
        self.last_error = f"{type(error).__name__}: {error}"
        self.state = "CLOSED"
        return PumpResult("protocol_error", packets)

    def mark_server_exited(self, returncode: int | None) -> None:
        self.server_exit_count += 1
        self.server_exit_code = returncode
        self.state = "SERVER_EXITED"

    def pump(self, on_packet=None) -> PumpResult:
        """Drain at most ``max_packets`` and return a classified outcome."""
        self.loop_iterations += 1
        drained = 0
        while drained < self.max_packets:
            try:
                if not self._wait_for_packet(self.idle_timeout if drained == 0 else 0.0):
                    self.timeout_count += 1
                    return PumpResult("timeout", drained)
                pid, data = self.conn.recv_packet()
            except socket.timeout:
                # Conn deliberately leaves any partial VarInt/frame in rxbuf.
                self.timeout_count += 1
                return PumpResult("timeout", drained)
            except EOFError as error:
                self.eof_count += 1
                self.last_error = str(error)
                self.state = "CLOSED"
                return PumpResult("eof", drained)
            except OSError as error:
                return self._transport_result("socket_error", drained, error)
            except Exception as error:
                # Malformed framing/decompression is a protocol failure, never
                # an idle timeout.
                return self._protocol_result(drained, error)

            self.packet_counts[pid] += 1
            self.last_rx_monotonic = time.monotonic()
            drained += 1

            terminal_status = None
            if pid == PLAY_KEEP_ALIVE:
                self.keepalives += 1
                if len(data) != 8:
                    return self._protocol_result(
                        drained, ValueError(f"KeepAlive payload has {len(data)} bytes")
                    )
                keepalive_id = struct.unpack(">q", data)[0]
                if keepalive_id in self._keepalive_seen:
                    self.duplicate_keepalives += 1
                    return self._protocol_result(
                        drained, ValueError(f"duplicate KeepAlive id {keepalive_id}")
                    )
                self._keepalive_seen.add(keepalive_id)
                self.keepalive_ids.append(keepalive_id)
                try:
                    # Play clientbound 0x27 is echoed as serverbound 0x1A.
                    self.conn.send_packet_raw(PLAY_KEEP_ALIVE_RESPONSE, data)
                except (EOFError, OSError) as error:
                    self.send_errors += 1
                    return self._transport_result("send_error", drained, error)
                self.keepalive_echoes += 1
            elif pid == PLAY_CLIENTBOUND_CHUNK_BATCH_FINISHED:
                try:
                    # 1.21.4 clientbound ChunkBatchFinished carries the
                    # number of chunks in the just-completed batch. The
                    # client answers with its preferred next batch size.
                    self.conn.send_packet_raw(
                        PLAY_SERVERBOUND_CHUNK_BATCH_RECEIVED,
                        struct.pack(">f", 8.0),
                    )
                except (EOFError, OSError) as error:
                    self.send_errors += 1
                    return self._transport_result("send_error", drained, error)
            elif pid == PLAY_DISCONNECT:
                self.disconnects += 1
                self.disconnect_reason = decode_disconnect_reason(data)
                terminal_status = "disconnect"

            if on_packet is not None:
                try:
                    on_packet(pid, data)
                except Exception as error:
                    return self._protocol_result(drained, error)

            if terminal_status is not None:
                self.state = "CLOSED"
                return PumpResult(terminal_status, drained)

        self.drain_limit_hits += 1
        return PumpResult("drain_limit", drained)


def wait_for_server(proc: subprocess.Popen, host: str, port: int, timeout: float = 10.0) -> None:
    """Wait for a real status response, or fail early if the child exits."""
    deadline = time.monotonic() + timeout
    last_error: BaseException | None = None
    while time.monotonic() < deadline:
        returncode = proc.poll()
        if returncode is not None:
            raise RuntimeError(f"server exited before readiness probe (exit={returncode})")
        client = None
        try:
            # Conn.status() performs several socket operations. Bound each
            # operation by a fraction of the remaining monotonic deadline so
            # one failed probe cannot extend the readiness timeout by seconds.
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            probe_timeout = min(1.0, remaining / 6.0)
            client = Conn(host, port, timeout=probe_timeout)
            client.sock.settimeout(probe_timeout)
            client.status()
            return
        except (OSError, EOFError, ValueError, RuntimeError) as error:
            last_error = error
            remaining = deadline - time.monotonic()
            if remaining > 0:
                time.sleep(min(0.1, remaining))
        finally:
            if client is not None:
                client.close()
    detail = f": {last_error}" if last_error else ""
    raise TimeoutError(f"server readiness probe timed out on {host}:{port}{detail}")


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def _signal_owned_process_group(
    proc: subprocess.Popen, sig: signal.Signals, hard: bool = False
) -> bool:
    """Signal the session/process group created for the owned server."""
    try:
        if os.name == "posix":
            os.killpg(proc.pid, sig)
        elif hard:
            proc.kill()
        else:
            try:
                proc.send_signal(getattr(signal, "CTRL_BREAK_EVENT", sig))
            except (AttributeError, OSError, ValueError):
                proc.terminate()
    except ProcessLookupError:
        # The group may have exited between poll and signalling; that is a
        # successful cleanup state, not an orphan.
        return True
    except OSError:
        return False
    return True


def _owned_process_group_exists(proc: subprocess.Popen) -> bool:
    """Check whether the dedicated group created for ``proc`` still exists."""
    if os.name != "posix":
        # The Windows fallback can observe/reap the owned leader, but the
        # stdlib does not expose a portable query for the whole process tree.
        return proc.poll() is None
    try:
        os.killpg(proc.pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False
    return True


def _wait_for_process_group_exit(proc: subprocess.Popen, deadline: float) -> bool:
    """Wait for both the Popen leader and its dedicated POSIX group to exit."""
    while True:
        try:
            leader_exited = proc.poll() is not None
            group_exists = _owned_process_group_exists(proc)
        except Exception:
            return False
        if leader_exited and not group_exists:
            return True

        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return False
        if leader_exited:
            time.sleep(min(0.05, remaining))
            continue
        try:
            proc.wait(timeout=min(0.05, remaining))
        except subprocess.TimeoutExpired:
            pass
        except Exception:
            return False


def stop_process(
    proc: subprocess.Popen | None, timeout: float = 10.0, kill_timeout: float = 5.0
) -> bool:
    """Terminate the owned process group, escalate, and reap its leader."""
    if proc is None:
        return True
    try:
        timeout = max(0.0, float(timeout))
        kill_timeout = max(0.0, float(kill_timeout))
    except (OverflowError, TypeError, ValueError):
        return False
    if not math.isfinite(timeout) or not math.isfinite(kill_timeout):
        return False

    # The process may already have exited while descendants remain in its
    # dedicated session. Signal the group only while it still exists; this
    # also makes repeated cleanup safe after the whole group has disappeared.
    try:
        group_exists = _owned_process_group_exists(proc)
        term_ok = (
            _signal_owned_process_group(proc, signal.SIGTERM)
            if group_exists
            else True
        )
    except Exception:
        term_ok = False

    graceful_deadline = time.monotonic() + timeout
    try:
        if _wait_for_process_group_exit(proc, graceful_deadline):
            return term_ok
    except Exception:
        pass

    try:
        group_exists = _owned_process_group_exists(proc)
        kill_ok = (
            _signal_owned_process_group(
                proc, getattr(signal, "SIGKILL", signal.SIGTERM), hard=True
            )
            if group_exists
            else True
        )
    except Exception:
        kill_ok = False

    kill_deadline = time.monotonic() + kill_timeout
    try:
        killed_exit = _wait_for_process_group_exit(proc, kill_deadline)
    except Exception:
        killed_exit = False
    return term_ok and kill_ok and killed_exit


def _owned_process_group_kwargs() -> dict[str, int | bool]:
    if os.name == "posix":
        return {}
    return {"creationflags": getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)}


def remove_world_dir(world_dir: str | None) -> bool:
    if world_dir is None:
        return True
    try:
        shutil.rmtree(world_dir)
    except FileNotFoundError:
        return True
    except OSError:
        return False
    return not os.path.exists(world_dir)


def send_action(pump: PlayPacketPump, pid: int, payload: bytes) -> bool:
    try:
        pump.conn.send_packet_raw(pid, payload)
        return True
    except (EOFError, OSError) as error:
        pump.send_errors += 1
        pump.socket_errors += 1
        pump.last_error = f"{type(error).__name__}: {error}"
        pump.state = "CLOSED"
        return False


def acknowledge_server_position(pump: PlayPacketPump, payload: bytes) -> int:
    """Acknowledge the teleport ID prefix of a Play PlayerPosition packet."""
    teleport_id, _ = mcproto.read_varint(io.BytesIO(payload))
    if not send_action(
        pump,
        PLAY_SERVERBOUND_ACCEPT_TELEPORTATION,
        mcproto.write_varint(teleport_id),
    ):
        raise OSError("failed to acknowledge server PlayerPosition")
    return teleport_id


def handle_position_packet(
    pump: PlayPacketPump, packet_id: int, payload: bytes
) -> int | None:
    """Route the initial server position packet to its mandatory client ACK."""
    if packet_id != PLAY_CLIENTBOUND_PLAYER_POSITION:
        return None
    return acknowledge_server_position(pump, payload)


def main() -> int:
    ap = argparse.ArgumentParser(description="C-12 soak_bot 1h 20 TPS")
    ap.add_argument("--host", default=os.environ.get("CPPFM_HOST", "127.0.0.1"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("CPPFM_PORT", "25577")))
    ap.add_argument("--binary", type=str, default=None, help="path to cppfm")
    ap.add_argument("--duration", type=int, default=300, help="seconds (3600 nightly, 300 dry)")
    ap.add_argument("--view-distance", type=int, default=6)
    args = ap.parse_args()

    host = args.host
    port = args.port
    duration = args.duration
    binary = args.binary
    proc: subprocess.Popen | None = None
    world_dir: str | None = None
    conn: Conn | None = None
    pump: PlayPacketPump | None = None
    process_cleanup_ok = True
    world_cleanup_ok = True
    fatal_error = ""
    fails = 0

    if binary and not os.path.exists(binary):
        print(f"[soak_bot] binary {binary} not found", file=sys.stderr)
        return 1

    launch_in_progress = False
    termination_requested = False
    termination_signum = signal.SIGTERM
    termination_signals = (signal.SIGTERM, signal.SIGINT)
    previous_handlers = {
        signum: signal.getsignal(signum) for signum in termination_signals
    }

    def handle_termination(signum, _frame):
        nonlocal termination_requested, termination_signum
        if launch_in_progress:
            # Finish world/log setup and Popen registration before acting on
            # the signal, so cleanup never races an untracked owned resource.
            if not termination_requested:
                termination_requested = True
                termination_signum = signum
            return
        for handled_signum in termination_signals:
            signal.signal(handled_signum, signal.SIG_IGN)
        child = proc
        if child is not None:
            # The outer timeout uses a five-second kill-after window.  Finish
            # the owned group cleanup inside that window while retaining the
            # normal TERM -> grace -> KILL -> wait order.
            stop_process(child, timeout=1.0, kill_timeout=1.0)
        raise SystemExit(128 + signum)

    def check(cond: bool, msg: str) -> None:
        nonlocal fails
        print(("  ok  " if cond else "  FAIL ") + msg)
        if not cond:
            fails += 1

    chunks = 0
    times = 0
    teleport_confirms = 0
    moves_seen = 0
    deaths = 0
    updates = 0
    ticks = 0
    elapsed = 0.0

    def observe_packet(pid: int, data: bytes) -> None:
        nonlocal chunks, times, teleport_confirms, moves_seen, deaths, updates
        if pid == PLAY_LEVEL_CHUNK_WITH_LIGHT:
            chunks += 1
        elif pid == PLAY_UPDATE_TIME:
            times += 1
        elif handle_position_packet(pump, pid, data) is not None:
            teleport_confirms += 1
        elif pid in (0x2F, 0x30, 0x32, 0x77, 0x4D):
            moves_seen += 1
        elif pid == 0x09:
            updates += 1
        elif pid == 0x4C:
            deaths += 1

    try:
        for signum in termination_signals:
            signal.signal(signum, handle_termination)
        if binary:
            port = free_port()
            host = "127.0.0.1"
            launch_in_progress = True
            try:
                world_dir = tempfile.mkdtemp(prefix=f"soak_bot_{os.getpid()}_")
                print(
                    f"[soak_bot] spawning {binary} --port {port} --view-distance "
                    f"{args.view_distance} --world-dir {world_dir} for {duration}s"
                )
                cwd = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
                environment = os.environ.copy()
                environment["CPPFM_SERVER_DIR"] = world_dir
                if termination_requested:
                    raise SystemExit(128 + termination_signum)
                proc = subprocess.Popen(
                    [
                        binary,
                        f"--port={port}",
                        f"--view-distance={args.view_distance}",
                        f"--world-dir={world_dir}",
                        "--level-type=flat",
                        "--online-mode=false",
                        "--enforce-secure-profile=false",
                    ],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    stdin=subprocess.DEVNULL,
                    cwd=cwd,
                    env=environment,
                    start_new_session=True,
                    **_owned_process_group_kwargs(),
                )
            finally:
                launch_in_progress = False
            if termination_requested:
                stop_process(proc, timeout=1.0, kill_timeout=1.0)
                raise SystemExit(128 + termination_signum)
            wait_for_server(proc, host, port)
            print(f"[soak_bot] readiness probe passed host={host} port={port}")

        conn = Conn(host, port)
        conn.sock.settimeout(1.0)
        conn.login("SoakBot")
        conn.config_finish(sink=lambda _pid, _data: None)
        conn.sock.settimeout(0.05)
        pump = PlayPacketPump(conn)

        print(f"[soak_bot] start {duration}s 20 TPS host={host} port={port}")
        start = time.monotonic()
        deadline = start + duration
        last_move = 0.0
        last_chat = 0.0
        last_combat = 0.0

        while time.monotonic() < deadline:
            if proc is not None and proc.poll() is not None:
                pump.mark_server_exited(proc.returncode)
                break

            ticks += 1
            now = time.monotonic()
            if now - last_move > 1.5:
                last_move = now
                x = 8.5 + (ticks % 40) * 0.5
                if not send_action(
                    pump,
                    0x1C,
                    struct.pack(">ddd", x, -60.0, 8.5) + b"\x01",
                ):
                    break
            if now - last_chat > 30:
                last_chat = now
                if not send_action(
                    pump,
                    0x07,
                    mcproto.pack_string(f"soak tick {ticks}")
                    + struct.pack(">qq", int(time.time() * 1000), 0)
                    + b"\x00"
                    + mcproto.write_varint(0)
                    + b"\x00\x00\x00",
                ):
                    break
            if now - last_combat > 5:
                last_combat = now
                if not send_action(pump, 0x3A, struct.pack(">b", 0)):
                    break

            result = pump.pump(on_packet=observe_packet)
            if result.status not in ("timeout", "drain_limit"):
                break

        elapsed = time.monotonic() - start
        if proc is not None and proc.poll() is not None and pump.server_exit_count == 0:
            pump.mark_server_exited(proc.returncode)
    except Exception as error:
        if termination_requested:
            raise SystemExit(128 + termination_signum)
        fatal_error = f"{type(error).__name__}: {error}"
    finally:
        for signum in termination_signals:
            signal.signal(signum, signal.SIG_IGN)
        try:
            try:
                if conn is not None:
                    conn.close()
            except Exception as error:
                fatal_error = fatal_error or f"{type(error).__name__}: {error}"
            finally:
                try:
                    process_cleanup_ok = stop_process(proc)
                except Exception as error:
                    process_cleanup_ok = False
                    fatal_error = fatal_error or f"{type(error).__name__}: {error}"
                if process_cleanup_ok:
                    try:
                        world_cleanup_ok = remove_world_dir(world_dir)
                    except Exception as error:
                        world_cleanup_ok = False
                        fatal_error = fatal_error or f"{type(error).__name__}: {error}"
                else:
                    world_cleanup_ok = False
                    print(
                        "[soak_bot] refusing to remove world-dir while owned process cleanup is incomplete",
                        file=sys.stderr,
                    )
        finally:
            for signum, previous_handler in previous_handlers.items():
                signal.signal(signum, previous_handler)

    if fatal_error:
        check(False, f"runner completed without fatal error ({fatal_error})")

    if pump is None:
        check(False, "Play connection reached packet pump")
    else:
        keepalives = pump.keepalives
        kicks = pump.disconnects
        expected_keepalives = max(1, duration // 40)
        packet_counts = ",".join(
            f"0x{pid:02x}={count}" for pid, count in sorted(pump.packet_counts.items())
        ) or "none"
        print(
            f"[soak_bot] elapsed {elapsed:.1f}s ticks {ticks} keepAlives {keepalives} "
            f"chunks {chunks} times {times} teleportConfirms {teleport_confirms} "
            f"updates {updates} deaths {deaths} kicks {kicks}"
        )
        print(
            f"[soak_bot] pump state={pump.state} packets={packet_counts} "
            f"timeouts={pump.timeout_count} eof={pump.eof_count} "
            f"disconnects={pump.disconnects} drainLimitHits={pump.drain_limit_hits} "
            f"loops={pump.loop_iterations} lastRx={pump.last_rx_monotonic}"
        )
        if pump.disconnect_reason:
            print(f"[soak_bot] disconnectReason={pump.disconnect_reason}")
        if pump.last_error:
            print(f"[soak_bot] lastError={pump.last_error}")

        # Keep the original gates unchanged; diagnostics below make transport
        # failures visible instead of turning them into timeout successes.
        check(
            keepalives >= expected_keepalives,
            f"keepAlives {keepalives} >= {expected_keepalives} (periodic KeepAlive 0x27/0x1a)",
        )
        check(kicks == 0, f"kicks 0 (got {kicks}; clientbound Disconnect 0x1d)")
        check(pump.eof_count == 0, f"EOF 0 (got {pump.eof_count})")
        check(pump.server_exit_count == 0, f"server exits 0 (got {pump.server_exit_count})")
        check(
            pump.socket_errors == 0 and pump.send_errors == 0,
            f"transport errors 0 (socket={pump.socket_errors} send={pump.send_errors})",
        )
        check(pump.protocol_errors == 0, f"protocol errors 0 (got {pump.protocol_errors})")
        check(
            pump.keepalive_echoes == keepalives,
            f"KeepAlive echoes {pump.keepalive_echoes} == received {keepalives} (0x27 -> 0x1a)",
        )
        check(
            teleport_confirms >= 1,
            f"teleport confirms {teleport_confirms} >= 1 (PlayerPosition 0x42 -> AcceptTeleportation 0x00)",
        )
        check(chunks >= 10, f"chunks {chunks} >=10 (LevelChunkWithLight 0x28)")
        check(times >= 1 or ticks > 100, f"time updates {times} or ticks {ticks} (UpdateTime 0x6b)")

    if binary:
        check(process_cleanup_ok, "owned server subprocess reaped in finally")
        check(world_cleanup_ok, f"owned world-dir removed in finally ({world_dir})")

    status = "PASS" if fails == 0 else "FAIL"
    print(f"[soak_bot] {status} ({fails} failures) duration={duration}s")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
