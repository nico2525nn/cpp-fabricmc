#!/usr/bin/env python3
"""Exercise the soak bot's mandatory Play-protocol acknowledgements."""
from __future__ import annotations

import pathlib
import contextlib
import io
import os
import shutil
import signal
import socket
import subprocess
import struct
import sys
import unittest
from unittest import mock

REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPOSITORY_ROOT))
sys.path.insert(0, str(REPOSITORY_ROOT / "tests"))

from tools import soak_bot
import soak_test


REAL_POPEN = subprocess.Popen


class BrokenConnection:
    def send_packet_raw(self, packet_id: int, payload: bytes) -> None:
        raise BrokenPipeError("test transport failure")


def conn_over_socket(sock: socket.socket) -> soak_bot.mcproto.Conn:
    """Build the real 1.21.4 frame codec around an already-connected socket."""
    conn = soak_bot.mcproto.Conn.__new__(soak_bot.mcproto.Conn)
    conn.sock = sock
    conn.compression_threshold = -1
    conn.rxbuf = b""
    return conn


class PlayerPositionAcknowledgementTest(unittest.TestCase):
    def test_pump_dispatches_position_and_sends_wire_ack(self) -> None:
        teleport_id = 300
        server_socket, client_socket = socket.socketpair()
        self.addCleanup(server_socket.close)
        self.addCleanup(client_socket.close)
        connection = conn_over_socket(client_socket)
        server = conn_over_socket(server_socket)

        # 1.21.4 PlayerPosition: teleport VarInt, position and delta-motion
        # doubles, rotation floats, and a fixed-width flags value; the ACK is
        # a separate serverbound AcceptTeleportation packet.
        payload = soak_bot.mcproto.write_varint(teleport_id) + struct.pack(
            ">ddddddffI", 8.5, -60.0, 8.5, 0.0, 0.0, 0.0, 45.0, 10.0, 0
        )
        body = soak_bot.mcproto.write_varint(0x42) + payload
        server_socket.sendall(soak_bot.mcproto.write_varint(len(body)) + body)
        self.assertEqual(len(payload), 62)

        pump = soak_bot.PlayPacketPump(connection, idle_timeout=0.001)  # type: ignore[arg-type]
        observed: list[int] = []

        def observe(packet_id: int, data: bytes) -> None:
            acknowledged = soak_bot.handle_position_packet(pump, packet_id, data)
            if acknowledged is not None:
                observed.append(acknowledged)

        result = pump.pump(on_packet=observe)

        self.assertEqual(soak_bot.PLAY_CLIENTBOUND_PLAYER_POSITION, 0x42)
        self.assertEqual(result.packets, 1)
        self.assertEqual(pump.packet_counts[0x42], 1)
        self.assertEqual(observed, [teleport_id])
        self.assertEqual(server.recv_packet(), (0x00, bytes((0xAC, 0x02))))
        self.assertEqual(pump.send_errors, 0)

    def test_pump_acknowledges_chunk_batches_with_preferred_size(self) -> None:
        server_socket, client_socket = socket.socketpair()
        self.addCleanup(server_socket.close)
        self.addCleanup(client_socket.close)
        connection = conn_over_socket(client_socket)
        server = conn_over_socket(server_socket)

        payload = soak_bot.mcproto.write_varint(8)
        body = soak_bot.mcproto.write_varint(
            soak_bot.PLAY_CLIENTBOUND_CHUNK_BATCH_FINISHED
        ) + payload
        server_socket.sendall(soak_bot.mcproto.write_varint(len(body)) + body)

        pump = soak_bot.PlayPacketPump(connection, idle_timeout=0.001)  # type: ignore[arg-type]
        result = pump.pump()

        self.assertEqual(result.packets, 1)
        self.assertEqual(pump.packet_counts[0x0C], 1)
        self.assertEqual(
            server.recv_packet(),
            (soak_bot.PLAY_SERVERBOUND_CHUNK_BATCH_RECEIVED, struct.pack(">f", 8.0)),
        )
        self.assertEqual(pump.send_errors, 0)

    def test_transport_failure_is_reported(self) -> None:
        pump = soak_bot.PlayPacketPump(BrokenConnection())  # type: ignore[arg-type]
        payload = soak_bot.mcproto.write_varint(7) + struct.pack(
            ">ddddddffI", 0.0, 64.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0
        )

        with self.assertRaises(OSError):
            soak_bot.acknowledge_server_position(pump, payload)

        self.assertEqual(pump.send_errors, 1)
        self.assertEqual(pump.socket_errors, 1)


class OwnedProcessLifecycleTest(unittest.TestCase):
    def test_stop_process_escalates_and_reaps_term_resistant_leader(self) -> None:
        if os.name != "posix":
            self.skipTest("process-group signal escalation is POSIX-specific")

        child_code = (
            "import signal,time; "
            "signal.signal(signal.SIGTERM, signal.SIG_IGN); "
            "print('ready', flush=True); time.sleep(60)"
        )
        for stop_process in (soak_bot.stop_process, soak_test.stop_process):
            with self.subTest(stop_process=stop_process.__module__):
                proc = REAL_POPEN(
                    [sys.executable, "-c", child_code],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.DEVNULL,
                    text=True,
                    start_new_session=True,
                )
                self.addCleanup(stop_process, proc, 0.1, 1.0)
                assert proc.stdout is not None
                self.assertEqual(proc.stdout.readline().strip(), "ready")

                self.assertTrue(stop_process(proc, timeout=0.05, kill_timeout=1.0))
                self.assertIsNotNone(proc.returncode)
                proc.stdout.close()

    def test_stop_process_kills_descendants_after_leader_was_reaped(self) -> None:
        if os.name != "posix":
            self.skipTest("dedicated POSIX process groups are required")
        self.assertIs(soak_test.stop_process, soak_bot.stop_process)

        child_code = """import os, signal, time
child = os.fork()
if child == 0:
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    while True:
        time.sleep(1)
os._exit(0)
"""
        proc = REAL_POPEN(
            [sys.executable, "-c", child_code],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )

        def force_group_cleanup() -> None:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                proc.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=1.0)

        self.addCleanup(force_group_cleanup)
        proc.wait(timeout=2.0)
        os.killpg(proc.pid, 0)  # the descendant keeps the owned group alive

        self.assertTrue(soak_bot.stop_process(proc, timeout=0.05, kill_timeout=1.0))
        self.assertIsNotNone(proc.returncode)
        with self.assertRaises(ProcessLookupError):
            os.killpg(proc.pid, 0)

    def test_non_finite_cleanup_deadlines_are_rejected_without_signalling(self) -> None:
        for runner in (soak_bot, soak_test):
            with self.subTest(runner=runner.__name__):
                with mock.patch.object(soak_bot, "_signal_owned_process_group") as signal_group:
                    self.assertFalse(runner.stop_process(object(), timeout=float("inf")))
                    signal_group.assert_not_called()

    def _assert_spawn_interrupt_is_owned(
        self, runner, argv: list[str], signum: int, *, fail_bot_cleanup: bool = False
    ) -> None:
        spawned: list[subprocess.Popen] = []
        world_dirs: list[str] = []
        child_code = "import time; time.sleep(60)"

        def spawn_then_interrupt(_command, **kwargs):
            world_arg = next(
                (arg for arg in _command if arg.startswith("--world-dir=")), None
            )
            if world_arg is not None:
                world_dir = world_arg.partition("=")[2]
                world_dirs.append(world_dir)
                self.addCleanup(shutil.rmtree, world_dir, ignore_errors=True)
            child = REAL_POPEN(
                [sys.executable, "-c", child_code],
                **kwargs,
            )
            spawned.append(child)
            self.addCleanup(soak_bot.stop_process, child, 0.1, 1.0)
            os.kill(os.getpid(), signum)
            return child

        cleanup_error = RuntimeError("injected bot cleanup failure")
        bot_cleanup_patch = (
            mock.patch.object(runner, "stop_bots", side_effect=cleanup_error)
            if fail_bot_cleanup
            else contextlib.nullcontext()
        )
        previous_handlers = {
            sig: signal.getsignal(sig) for sig in (signal.SIGINT, signal.SIGTERM)
        }
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(
            io.StringIO()
        ), mock.patch.object(runner.sys, "argv", argv), mock.patch.object(
            runner.subprocess, "Popen", side_effect=spawn_then_interrupt
        ), bot_cleanup_patch:
            with self.assertRaises(SystemExit) as raised:
                runner.main()

        self.assertEqual(raised.exception.code, 128 + signum)
        self.assertEqual(
            {sig: signal.getsignal(sig) for sig in previous_handlers}, previous_handlers
        )
        self.assertEqual(len(spawned), 1)
        self.assertIsNotNone(spawned[0].poll(), "spawned server must be reaped")
        self.assertEqual(len(world_dirs), 1)
        if fail_bot_cleanup:
            try:
                self.assertTrue(os.path.isdir(world_dirs[0]))
            finally:
                shutil.rmtree(world_dirs[0], ignore_errors=True)
        else:
            self.assertFalse(os.path.exists(world_dirs[0]))

    @unittest.skipUnless(os.name == "posix", "signal delivery injection is POSIX-only")
    def test_sigint_and_sigterm_during_spawn_are_deferred_until_registration(self) -> None:
        runners = (
            (soak_bot, ["soak_bot.py", "--binary", sys.executable, "--duration", "10"]),
            (
                soak_test,
                [
                    "soak_test.py",
                    "--binary",
                    sys.executable,
                    "--duration",
                    "10",
                    "--clients",
                    "1",
                    "--port",
                    "0",
                ],
            ),
        )
        for runner, argv in runners:
            for signum in (signal.SIGINT, signal.SIGTERM):
                with self.subTest(runner=runner.__name__, signal=signum.name):
                    self._assert_spawn_interrupt_is_owned(runner, argv, signum)

    @unittest.skipUnless(os.name == "posix", "signal delivery injection is POSIX-only")
    def test_interrupt_during_world_creation_is_deferred_until_cleanup_owns_it(self) -> None:
        runners = (
            (soak_bot, ["soak_bot.py", "--binary", sys.executable, "--duration", "10"]),
            (
                soak_test,
                [
                    "soak_test.py",
                    "--binary",
                    sys.executable,
                    "--duration",
                    "10",
                    "--clients",
                    "1",
                    "--port",
                    "0",
                ],
            ),
        )
        for runner, argv in runners:
            with self.subTest(runner=runner.__name__):
                world_dirs: list[str] = []
                real_mkdtemp = runner.tempfile.mkdtemp

                def create_world_then_interrupt(*args, **kwargs):
                    world_dir = real_mkdtemp(*args, **kwargs)
                    world_dirs.append(world_dir)
                    self.addCleanup(shutil.rmtree, world_dir, ignore_errors=True)
                    os.kill(os.getpid(), signal.SIGINT)
                    return world_dir

                with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(
                    io.StringIO()
                ), mock.patch.object(runner.sys, "argv", argv), mock.patch.object(
                    runner.tempfile, "mkdtemp", side_effect=create_world_then_interrupt
                ), mock.patch.object(runner.subprocess, "Popen") as popen:
                    with self.assertRaises(SystemExit) as raised:
                        runner.main()

                self.assertEqual(raised.exception.code, 128 + signal.SIGINT)
                popen.assert_not_called()
                self.assertEqual(len(world_dirs), 1)
                self.assertFalse(os.path.exists(world_dirs[0]))

    @unittest.skipUnless(os.name == "posix", "signal delivery injection is POSIX-only")
    def test_interrupt_plus_spawn_error_keeps_interrupt_exit_status(self) -> None:
        runners = (
            (soak_bot, ["soak_bot.py", "--binary", sys.executable, "--duration", "10"]),
            (
                soak_test,
                [
                    "soak_test.py",
                    "--binary",
                    sys.executable,
                    "--duration",
                    "10",
                    "--clients",
                    "1",
                    "--port",
                    "0",
                ],
            ),
        )
        for runner, argv in runners:
            with self.subTest(runner=runner.__name__):
                world_dirs: list[str] = []

                def interrupt_then_fail_spawn(_command, **_kwargs):
                    os.kill(os.getpid(), signal.SIGINT)
                    raise OSError("injected spawn failure")

                real_mkdtemp = runner.tempfile.mkdtemp

                def record_world_dir(*args, **kwargs):
                    world_dir = real_mkdtemp(*args, **kwargs)
                    world_dirs.append(world_dir)
                    self.addCleanup(shutil.rmtree, world_dir, ignore_errors=True)
                    return world_dir

                with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(
                    io.StringIO()
                ), mock.patch.object(runner.sys, "argv", argv), mock.patch.object(
                    runner.tempfile, "mkdtemp", side_effect=record_world_dir
                ), mock.patch.object(
                    runner.subprocess, "Popen", side_effect=interrupt_then_fail_spawn
                ) as popen:
                    with self.assertRaises(SystemExit) as raised:
                        runner.main()

                self.assertEqual(raised.exception.code, 128 + signal.SIGINT)
                popen.assert_called_once()
                self.assertEqual(len(world_dirs), 1)
                self.assertFalse(os.path.exists(world_dirs[0]))

    @unittest.skipUnless(os.name == "posix", "signal delivery injection is POSIX-only")
    def test_bot_cleanup_exception_does_not_skip_server_reaping(self) -> None:
        self._assert_spawn_interrupt_is_owned(
            soak_test,
            [
                "soak_test.py",
                "--binary",
                sys.executable,
                "--duration",
                "10",
                "--clients",
                "1",
                "--port",
                "0",
            ],
            signal.SIGINT,
            fail_bot_cleanup=True,
        )

    def test_connection_close_exception_does_not_skip_owned_server_cleanup(self) -> None:
        spawned: list[subprocess.Popen] = []
        world_dirs: list[str] = []

        def spawn_server(_command, **kwargs):
            world_arg = next(
                arg for arg in _command if arg.startswith("--world-dir=")
            )
            world_dir = world_arg.partition("=")[2]
            world_dirs.append(world_dir)
            self.addCleanup(shutil.rmtree, world_dir, ignore_errors=True)
            child = REAL_POPEN(
                [sys.executable, "-c", "import time; time.sleep(60)"],
                **kwargs,
            )
            spawned.append(child)
            self.addCleanup(soak_bot.stop_process, child, 0.1, 1.0)
            return child

        connection = mock.Mock()
        connection.close.side_effect = OSError("injected socket close failure")
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(
            io.StringIO()
        ), mock.patch.object(
            soak_bot.sys,
            "argv",
            ["soak_bot.py", "--binary", sys.executable, "--duration", "0"],
        ), mock.patch.object(
            soak_bot.subprocess, "Popen", side_effect=spawn_server
        ), mock.patch.object(soak_bot, "wait_for_server"), mock.patch.object(
            soak_bot, "Conn", return_value=connection
        ):
            self.assertEqual(soak_bot.main(), 1)

        self.assertEqual(len(spawned), 1)
        self.assertIsNotNone(spawned[0].poll(), "cleanup must reap after Conn.close fails")
        self.assertEqual(len(world_dirs), 1)
        self.assertFalse(os.path.exists(world_dirs[0]))


if __name__ == "__main__":
    unittest.main()
