#!/usr/bin/env python3
"""Dependency-free timeout ownership tests, including real owned process trees."""

from __future__ import annotations

import contextlib
import ctypes
import io
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import run_with_timeout as runner


class RunnerTests(unittest.TestCase):
    def test_posix_popen_arguments_and_exit_code(self):
        process = mock.Mock()
        process.wait.return_value = 7
        with mock.patch.object(runner.os, "name", "posix"), \
                mock.patch.object(runner.subprocess, "Popen", return_value=process) as popen, \
                mock.patch.object(runner, "terminate") as terminate:
            self.assertEqual(runner.main(["--timeout", "3", "--", "command", "arg"]), 7)
        popen.assert_called_once_with(["command", "arg"], start_new_session=True)
        process.wait.assert_called_once_with(timeout=3)
        terminate.assert_called_once_with(process)

    def test_windows_main_uses_job_and_preserves_exit_code(self):
        job = mock.Mock()
        job.start.return_value.wait.return_value = 7
        job.wait_empty.return_value = True
        with mock.patch.object(runner.os, "name", "nt"), \
                mock.patch.object(runner, "_WindowsJob", return_value=job):
            self.assertEqual(runner.main(["--timeout", "3", "--", "command"]), 7)
        job.start.assert_called_once_with(["command"], 3)
        job.kill.assert_not_called()
        job.wait_empty.assert_called_once_with(0)
        job.close.assert_called_once_with()

    def test_windows_successful_root_exit_kills_remaining_job_children(self):
        job = mock.Mock()
        job.start.return_value.wait.return_value = 0
        job.wait_empty.side_effect = [False, True]
        with mock.patch.object(runner.os, "name", "nt"), \
                mock.patch.object(runner, "_WindowsJob", return_value=job):
            self.assertEqual(runner.main(["--timeout", "3", "--", "command"]), 0)
        self.assertEqual(job.wait_empty.call_args_list,
                         [mock.call(0), mock.call(runner.FORCE_SECONDS)])
        job.kill.assert_called_once_with()
        job.close.assert_called_once_with()

    def test_windows_popen_is_gated_and_has_only_windows_arguments(self):
        job = runner._WindowsJob.__new__(runner._WindowsJob)
        job.handle = 99
        events = []

        def checked(name, *args):
            events.append(name)
            return {"CreateEventW": 10, "OpenProcess": 20}.get(name, 1)

        job.api = mock.Mock()
        job.api.checked.side_effect = checked
        process = mock.Mock(pid=123)

        def popen(*args, **kwargs):
            events.append("Popen")
            return process

        startup = mock.Mock()
        with mock.patch.object(runner, "_standard_streams", return_value={"stdin": 0, "stdout": 1, "stderr": 2}), \
                mock.patch.object(runner.os, "set_handle_inheritable", create=True) as inherit, \
                mock.patch.object(runner.subprocess, "STARTUPINFO", create=True,
                                  return_value=startup), \
                mock.patch.object(runner.subprocess, "CREATE_NEW_PROCESS_GROUP", 512,
                                  create=True), \
                mock.patch.object(runner.subprocess, "Popen", side_effect=popen) as spawn:
            self.assertIs(job.start(["command", "a b", "雪"], 3), process)
        args, kwargs = spawn.call_args
        self.assertNotIn("start_new_session", kwargs)
        self.assertEqual(kwargs, {"creationflags": 512, "startupinfo": startup,
                                  "close_fds": True, "stdin": 0, "stdout": 1, "stderr": 2})
        self.assertEqual(startup.lpAttributeList, {"handle_list": [10]})
        self.assertEqual(args[0][-3:], ["command", "a b", "雪"])
        self.assertEqual(args[0][0], sys.executable)
        self.assertEqual(inherit.call_args_list, [mock.call(10, True), mock.call(10, False)])
        self.assertLess(events.index("Popen"), events.index("AssignProcessToJobObject"))
        self.assertLess(events.index("AssignProcessToJobObject"), events.index("SetEvent"))

    def test_windows_assignment_failure_never_opens_gate(self):
        for operation in ("OpenProcess", "AssignProcessToJobObject", "SetEvent"):
            with self.subTest(operation=operation):
                self._windows_start_failure(operation)

    def _windows_start_failure(self, operation):
        job = runner._WindowsJob.__new__(runner._WindowsJob)
        job.handle = 99
        calls = []

        def checked(name, *args):
            calls.append(name)
            if name == operation:
                raise OSError("ownership setup denied")
            return 10

        job.api = mock.Mock()
        job.api.checked.side_effect = checked
        process = mock.Mock(pid=123)
        with mock.patch.object(runner.os, "set_handle_inheritable", create=True), \
                mock.patch.object(runner.subprocess, "STARTUPINFO", create=True), \
                mock.patch.object(runner.subprocess, "CREATE_NEW_PROCESS_GROUP", 512,
                                  create=True), \
                mock.patch.object(runner.subprocess, "Popen", return_value=process):
            with self.assertRaisesRegex(OSError, "ownership setup denied"):
                job.start(["command"], 3)
        if operation != "SetEvent":
            self.assertNotIn("SetEvent", calls)
        self.assertEqual(calls[-1], "CloseHandle")
        process.kill.assert_called_once_with()
        process.wait.assert_called_once_with(timeout=runner.FORCE_SECONDS)

    def test_windows_launcher_never_spawns_before_gate_opens(self):
        api = mock.Mock()
        api.dll.WaitForSingleObject.return_value = 258  # WAIT_TIMEOUT
        with mock.patch.object(runner, "_standard_streams", return_value={"stdin": 0, "stdout": 1, "stderr": 2}), \
                mock.patch.object(runner, "_WindowsAPI", return_value=api), \
                mock.patch.object(runner.subprocess, "Popen") as popen:
            with self.assertRaisesRegex(runner.CleanupError, "ownership gate"):
                runner._windows_child(10, 3, ["command"])
        api.dll.WaitForSingleObject.assert_called_once_with(10, 30000)
        api.checked.assert_called_once_with("CloseHandle", 10)
        popen.assert_not_called()

    def test_windows_launcher_inherits_job_group_and_stdio(self):
        api = mock.Mock()
        api.dll.WaitForSingleObject.return_value = 0
        process = mock.Mock()
        process.wait.return_value = 7
        with mock.patch.object(runner, "_standard_streams", return_value={"stdin": 0, "stdout": 1, "stderr": 2}), \
                mock.patch.object(runner, "_WindowsAPI", return_value=api), \
                mock.patch.object(runner.signal, "SIGBREAK", 21, create=True), \
                mock.patch.object(runner.signal, "signal") as set_handler, \
                mock.patch.object(runner.subprocess, "Popen", return_value=process) as popen:
            self.assertEqual(runner._windows_child(10, 3, ["command", "arg"]), 7)
        popen.assert_called_once_with(["command", "arg"], stdin=0, stdout=1, stderr=2)
        set_handler.assert_called_once()
        process.wait.assert_called_once_with(timeout=17)

    def test_windows_job_sets_kill_on_close_without_breakaway(self):
        api = mock.Mock()
        api.checked.return_value = 99
        flags = []

        def capture(name, *args):
            if name == "SetInformationJobObject":
                limits = ctypes.cast(args[2], ctypes.POINTER(runner._ExtendedLimits)).contents
                flags.append(limits.BasicLimitInformation.LimitFlags)
            return 99

        api.checked.side_effect = capture
        with mock.patch.object(runner, "_WindowsAPI", return_value=api):
            job = runner._WindowsJob()
            job.close()
            job.close()
        self.assertEqual(flags, [0x2000])
        self.assertEqual(api.checked.call_args_list[-1], mock.call("CloseHandle", 99))

    def test_windows_job_configuration_failure_closes_native_handle(self):
        api = mock.Mock()
        api.checked.side_effect = [99, OSError("cannot set job limits"), 1]
        with mock.patch.object(runner, "_WindowsAPI", return_value=api):
            with self.assertRaisesRegex(OSError, "cannot set job limits"):
                runner._WindowsJob()
        self.assertEqual(api.checked.call_args_list[-1], mock.call("CloseHandle", 99))

    def test_standard_streams_use_devnull_for_closed_descriptors(self):
        with mock.patch.object(runner.os, "fstat", side_effect=[OSError("closed"), mock.Mock(), mock.Mock()]):
            self.assertEqual(runner._standard_streams(),
                             {"stdin": subprocess.DEVNULL, "stdout": 1, "stderr": 2})

    def test_long_command_waits_never_pass_win32_infinite_or_overflow(self):
        process = mock.Mock(args=["command"])
        process.wait.side_effect = subprocess.TimeoutExpired("command", 30)
        with mock.patch.object(runner.time, "monotonic", side_effect=[0, 30, 1e20]):
            with self.assertRaises(subprocess.TimeoutExpired) as error:
                runner._wait_for_exit(process, 1e20)
        self.assertEqual(error.exception.timeout, 1e20)
        self.assertEqual(process.wait.call_args_list,
                         [mock.call(timeout=30), mock.call(timeout=30)])

    def test_windows_tree_cleanup_after_root_exit(self):
        process = mock.Mock()
        process.poll.return_value = 0
        job = mock.Mock()
        job.wait_empty.side_effect = [False, True]
        runner.terminate(process, job)
        process.send_signal.assert_not_called()
        process.kill.assert_not_called()
        job.kill.assert_called_once_with()
        self.assertEqual(job.wait_empty.call_args_list,
                         [mock.call(runner.GRACE_SECONDS), mock.call(runner.FORCE_SECONDS)])
        process.wait.assert_called_once_with(timeout=runner.FORCE_SECONDS)

    def test_windows_graceful_tree_exit_does_not_force_kill(self):
        process = mock.Mock()
        process.poll.return_value = None
        job = mock.Mock()
        job.wait_empty.return_value = True
        with mock.patch.object(runner.signal, "CTRL_BREAK_EVENT", 1, create=True):
            runner.terminate(process, job)
        process.send_signal.assert_called_once_with(1)
        job.kill.assert_not_called()
        process.wait.assert_called_once_with(timeout=runner.FORCE_SECONDS)

    def test_windows_control_event_failure_still_terminates_job(self):
        process = mock.Mock()
        process.poll.return_value = None
        process.send_signal.side_effect = OSError("no console")
        job = mock.Mock()
        job.wait_empty.side_effect = [False, True]
        with mock.patch.object(runner.signal, "CTRL_BREAK_EVENT", 1, create=True):
            runner.terminate(process, job)
        job.kill.assert_called_once_with()
        process.kill.assert_not_called()

    def test_windows_job_cleanup_failure_is_not_silent(self):
        process = mock.Mock()
        process.poll.return_value = 0
        job = mock.Mock()
        job.wait_empty.return_value = False
        with self.assertRaisesRegex(runner.CleanupError, "still has active processes"):
            runner.terminate(process, job)

    def test_job_empty_wait_is_bounded_and_checks_descendants(self):
        job = runner._WindowsJob.__new__(runner._WindowsJob)
        job.handle = 99
        job.api = mock.Mock()

        def query(name, *args):
            self.assertEqual(name, "QueryInformationJobObject")
            ctypes.cast(args[2], ctypes.POINTER(runner._Accounting)).contents.ActiveProcesses = 1
            return 1

        job.api.checked.side_effect = query
        with mock.patch.object(runner.time, "monotonic", side_effect=[0, 0, 2]), \
                mock.patch.object(runner.time, "sleep") as sleep:
            self.assertFalse(job.wait_empty(2))
        sleep.assert_called_once_with(0.05)
        self.assertEqual(job.api.checked.call_count, 2)

    def test_posix_force_kills_group_when_root_exits_first(self):
        process = mock.Mock(pid=123)
        process.wait.return_value = 0
        with mock.patch.object(runner.signal, "SIGKILL", 9, create=True), \
                mock.patch.object(runner.os, "killpg", create=True) as killpg, \
                mock.patch.object(runner, "_wait_for_posix_group_exit", return_value=False), \
                mock.patch.object(runner.time, "monotonic", return_value=0):
            runner.terminate(process)
        self.assertEqual(killpg.call_args_list,
                         [mock.call(123, signal.SIGTERM), mock.call(123, 9)])
        self.assertEqual(process.wait.call_args_list,
                         [mock.call(timeout=runner.GRACE_SECONDS),
                          mock.call(timeout=runner.FORCE_SECONDS)])

    def test_posix_stubborn_root_has_bounded_waits(self):
        process = mock.Mock(pid=123)
        process.wait.side_effect = [subprocess.TimeoutExpired("command", 2), -9]
        with mock.patch.object(runner.signal, "SIGKILL", 9, create=True), \
                mock.patch.object(runner.os, "killpg", create=True) as killpg, \
                mock.patch.object(runner, "_wait_for_posix_group_exit", return_value=False):
            runner.terminate(process)
        self.assertEqual(killpg.call_args_list,
                         [mock.call(123, signal.SIGTERM), mock.call(123, 9)])
        self.assertEqual(process.wait.call_args_list[-1], mock.call(timeout=runner.FORCE_SECONDS))

    def test_timeout_keeps_124_and_reports_cleanup_failure(self):
        process = mock.Mock()
        process.wait.side_effect = subprocess.TimeoutExpired("command", 3)
        output = io.StringIO()
        with mock.patch.object(runner.os, "name", "posix"), \
                mock.patch.object(runner.subprocess, "Popen", return_value=process), \
                mock.patch.object(runner, "_wait_for_exit", side_effect=subprocess.TimeoutExpired("command", 3)), \
                mock.patch.object(runner, "terminate", side_effect=runner.CleanupError("still alive")), \
                contextlib.redirect_stderr(output):
            self.assertEqual(runner.main(["--timeout", "3", "command"]), 124)
        self.assertIn("command timed out after 3s", output.getvalue())
        self.assertIn("timeout cleanup failed: still alive", output.getvalue())

    def test_windows_timeout_failure_still_closes_job_and_returns_124(self):
        job = mock.Mock()
        job.start.return_value.wait.side_effect = subprocess.TimeoutExpired("command", 3)
        output = io.StringIO()
        with mock.patch.object(runner.os, "name", "nt"), \
                mock.patch.object(runner, "_WindowsJob", return_value=job), \
                mock.patch.object(runner, "_wait_for_exit", side_effect=subprocess.TimeoutExpired("command", 3)), \
                mock.patch.object(runner, "terminate", side_effect=OSError("job query failed")), \
                contextlib.redirect_stderr(output):
            self.assertEqual(runner.main(["--timeout", "3", "command"]), 124)
        job.close.assert_called_once_with()
        self.assertIn("timeout cleanup failed: job query failed", output.getvalue())

    def test_windows_diagnostics_are_written_after_job_is_closed(self):
        job = mock.Mock()
        job.start.return_value.wait.return_value = 7
        job.wait_empty.side_effect = OSError("cannot query job")

        class Output(io.StringIO):
            def write(self, text):
                job.close.assert_called_once_with()
                return super().write(text)

        with mock.patch.object(runner.os, "name", "nt"), \
                mock.patch.object(runner, "_WindowsJob", return_value=job), \
                contextlib.redirect_stderr(Output()):
            self.assertEqual(runner.main(["--timeout", "3", "command"]), 125)

    def test_invalid_timeouts_cannot_create_unbounded_waits(self):
        with mock.patch.object(runner.subprocess, "Popen") as popen:
            for timeout in ("nan", "inf", "-inf"):
                with self.subTest(timeout=timeout), contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit) as error:
                        runner.main(["--timeout", timeout, "command"])
                    self.assertEqual(error.exception.code, 2)
        popen.assert_not_called()

    def test_zero_timeout_keeps_immediate_timeout_semantics(self):
        process = mock.Mock()
        process.wait.side_effect = subprocess.TimeoutExpired("command", 0)
        with mock.patch.object(runner.os, "name", "posix"), \
                mock.patch.object(runner.subprocess, "Popen", return_value=process), \
                mock.patch.object(runner, "terminate") as terminate, \
                contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(runner.main(["--timeout", "0", "command"]), 124)
        terminate.assert_called_once_with(process, None)


# Every descendant writes its identity before the root advertises readiness.
# Children ignore graceful termination; the root accepts it and exits first.
TREE_SCRIPT = r'''
import os, signal, subprocess, sys, time
from pathlib import Path
source, directory, role, mode = sys.argv[1:]
folder = Path(directory)
if os.name == "nt":
    signal.signal(signal.SIGBREAK, (lambda *_: sys.exit(0)) if role == "root" else signal.SIG_IGN)
else:
    signal.signal(signal.SIGTERM, (lambda *_: sys.exit(0)) if role == "root" and mode != "stubborn" else signal.SIG_IGN)
print(role + " stdout", flush=True)
print(role + " stderr", file=sys.stderr, flush=True)
identity = folder / (role + ".pid.tmp")
identity.write_text(str(os.getpid()))
identity.replace(folder / (role + ".pid"))
if role != "grandchild":
    next_role = "child" if role == "root" else "grandchild"
    options = {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == "nt" else {}
    child = subprocess.Popen([sys.executable, "-c", source, source, directory, next_role, mode], **options)
    deadline = time.monotonic() + 20
    while not (folder / "grandchild.pid").exists():
        if time.monotonic() >= deadline:
            sys.exit(90)
        time.sleep(0.01)
    if role == "root":
        (folder / "ready").write_text("ready")
deadline = time.monotonic() + 30
while time.monotonic() < deadline:
    if role == "root" and mode == "exit" and (folder / "exit-root").exists():
        sys.exit(7)
    time.sleep(0.01)
'''


def wait_ready(directory: Path) -> dict[str, int]:
    deadline = time.monotonic() + 10
    while not (directory / "ready").exists():
        if time.monotonic() >= deadline:
            raise AssertionError("process-tree fixture did not become ready")
        time.sleep(0.01)
    return {role: int((directory / (role + ".pid")).read_text())
            for role in ("root", "child", "grandchild")}


def posix_running(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    status = Path(f"/proc/{pid}/stat")
    try:
        # Linux orphan zombies are dead, even if container init has not reaped.
        return status.read_text().rsplit(")", 1)[1].split()[0] != "Z"
    except (FileNotFoundError, ProcessLookupError):
        return False if sys.platform.startswith("linux") else True


class ProcessTreeTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "posix", "POSIX process groups")
    def test_posix_timeout_kills_children_after_root_accepts_sigterm(self):
        self._posix_tree("wait")

    @unittest.skipUnless(os.name == "posix", "POSIX process groups")
    def test_posix_timeout_force_kills_stubborn_root_and_descendants(self):
        self._posix_tree("stubborn")

    @unittest.skipUnless(os.name == "posix", "POSIX process groups")
    def test_posix_group_cleanup_after_root_exit(self):
        self._posix_tree("exit")

    def _posix_tree(self, mode):
        with tempfile.TemporaryDirectory(prefix="timeout-tree-") as temporary:
            directory = Path(temporary)
            command = [sys.executable, "-c", TREE_SCRIPT, TREE_SCRIPT, temporary, "root", mode]
            timeout = "15" if mode == "exit" else "2"
            launch = [sys.executable, runner.__file__, "--timeout", timeout,
                      "--", *command]
            process = subprocess.Popen(
                launch,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                start_new_session=True,
            )
            pids = {}
            try:
                pids = wait_ready(directory)
                if mode == "exit":
                    (directory / "exit-root").write_text("exit")
                stdout, stderr = process.communicate(timeout=10)
                self.assertEqual(process.returncode, 7 if mode == "exit" else 124, stderr)
                self.assertNotIn("cleanup failed", stderr)
                for role in pids:
                    self.assertIn(role + " stdout", stdout)
                    self.assertIn(role + " stderr", stderr)
                deadline = time.monotonic() + 2
                while any(posix_running(pid) for pid in pids.values()) and time.monotonic() < deadline:
                    time.sleep(0.01)
                self.assertFalse(any(posix_running(pid) for pid in pids.values()), pids)
            finally:
                # Only the processes created by this fixture, never broad pkill.
                for role in ("root", "child", "grandchild"):
                    marker = directory / (role + ".pid")
                    if marker.exists():
                        pid = int(marker.read_text())
                        if posix_running(pid):
                            os.kill(pid, signal.SIGKILL)
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=5)

    def test_cli_preserves_stdin_and_large_stdout_stderr(self):
        program = (
            "import sys; data=sys.stdin.buffer.read(); "
            "sys.stdout.buffer.write(b'x'*131072+data); "
            "sys.stderr.buffer.write(b'y'*131072); sys.exit(7)"
        )
        result = subprocess.run(
            [sys.executable, runner.__file__, "--timeout", "10", "--",
             sys.executable, "-c", program],
            input=b"stdin payload\n", capture_output=True, timeout=20,
        )
        self.assertEqual(result.returncode, 7, result.stderr[-100:])
        self.assertEqual(result.stdout, b"x" * 131072 + b"stdin payload\n")
        self.assertEqual(result.stderr, b"y" * 131072)

    @unittest.skipUnless(os.name == "nt", "real Windows Job Object APIs")
    def test_windows_timeout_kills_separate_group_children_and_grandchildren(self):
        self._windows_tree(root_exits_first=False)

    @unittest.skipUnless(os.name == "nt", "real Windows Job Object APIs")
    def test_windows_job_cleanup_after_root_and_launcher_exit(self):
        self._windows_tree(root_exits_first=True)

    def _windows_tree(self, root_exits_first: bool):
        api = runner._WindowsAPI()
        # Retain native process handles so assertions cannot confuse PID reuse
        # or mere root exit with actual termination of the owned descendants.
        api.dll.TerminateProcess.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        api.dll.TerminateProcess.restype = ctypes.c_int32
        handles = []
        job = None
        process = None
        with tempfile.TemporaryDirectory(prefix="timeout-tree-") as temporary:
            directory = Path(temporary)
            mode = "exit" if root_exits_first else "wait"
            command = [sys.executable, "-c", TREE_SCRIPT, TREE_SCRIPT, temporary, "root", mode]
            try:
                if root_exits_first:
                    job = runner._WindowsJob()
                    process = job.start(command, 15)
                else:
                    process = subprocess.Popen(
                        [sys.executable, runner.__file__, "--timeout", "10", "--", *command],
                        creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                    )
                pids = wait_ready(directory)
                for pid in pids.values():
                    handles.append(api.checked("OpenProcess", 0x100001, False, pid))
                if root_exits_first:
                    (directory / "exit-root").write_text("exit")
                    self.assertEqual(process.wait(timeout=5), 7)
                    # The launcher is gone but both descendants are still alive.
                    for handle in handles[1:]:
                        self.assertEqual(api.dll.WaitForSingleObject(handle, 0), 258)
                    runner.terminate(process, job)
                    self.assertTrue(job.wait_empty(1))
                else:
                    stdout, stderr = process.communicate(timeout=20)
                    self.assertEqual(process.returncode, 124, stderr)
                    self.assertNotIn("cleanup failed", stderr)
                    for role in pids:
                        self.assertIn(role + " stdout", stdout)
                        self.assertIn(role + " stderr", stderr)
                for handle in handles:
                    self.assertEqual(api.dll.WaitForSingleObject(handle, 2000), 0)
            finally:
                if job is not None:
                    job.close()
                for handle in handles:
                    if api.dll.WaitForSingleObject(handle, 0) == 258:
                        api.checked("TerminateProcess", handle, 1)
                    api.checked("CloseHandle", handle)
                if process is not None:
                    if process.poll() is None:
                        process.kill()
                    if process.stdout is not None:
                        process.communicate(timeout=5)
                    else:
                        process.wait(timeout=5)


if __name__ == "__main__":
    unittest.main()
