#!/usr/bin/env python3
"""Run one build/test command with the same timeout semantics on every OS."""

from __future__ import annotations

import argparse
import ctypes
import os
import signal
import subprocess
import sys
import time
from typing import Sequence


GRACE_SECONDS = 2.0
FORCE_SECONDS = 2.0


class CleanupError(RuntimeError):
    """The owned process tree could not be confirmed terminated."""


def _standard_streams() -> dict[str, int]:
    streams = {}
    for name, descriptor in (("stdin", 0), ("stdout", 1), ("stderr", 2)):
        try:
            os.fstat(descriptor)
        except OSError:
            streams[name] = subprocess.DEVNULL
        else:
            streams[name] = descriptor
    return streams


def _wait_for_exit(process: subprocess.Popen[bytes], timeout: float) -> int:
    # Win32's millisecond timeout is a DWORD, with 0xffffffff meaning INFINITE.
    # Slice long waits so a finite CLI timeout cannot overflow or become infinite.
    deadline = time.monotonic() + max(0, timeout)
    interval = min(max(0, timeout), 30.0)
    while True:
        try:
            return process.wait(timeout=interval)
        except subprocess.TimeoutExpired:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise subprocess.TimeoutExpired(process.args, timeout) from None
            interval = min(remaining, 30.0)


# Fixed-width Win32 types keep these layouts correct on 32- and 64-bit Windows.
class _BasicLimits(ctypes.Structure):
    _fields_ = [
        ("PerProcessUserTimeLimit", ctypes.c_int64),
        ("PerJobUserTimeLimit", ctypes.c_int64),
        ("LimitFlags", ctypes.c_uint32),
        ("MinimumWorkingSetSize", ctypes.c_size_t),
        ("MaximumWorkingSetSize", ctypes.c_size_t),
        ("ActiveProcessLimit", ctypes.c_uint32),
        ("Affinity", ctypes.c_size_t),
        ("PriorityClass", ctypes.c_uint32),
        ("SchedulingClass", ctypes.c_uint32),
    ]


class _IoCounters(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint64) for name in (
        "ReadOperationCount", "WriteOperationCount", "OtherOperationCount",
        "ReadTransferCount", "WriteTransferCount", "OtherTransferCount",
    )]


class _ExtendedLimits(ctypes.Structure):
    _fields_ = [
        ("BasicLimitInformation", _BasicLimits),
        ("IoInfo", _IoCounters),
        ("ProcessMemoryLimit", ctypes.c_size_t),
        ("JobMemoryLimit", ctypes.c_size_t),
        ("PeakProcessMemoryUsed", ctypes.c_size_t),
        ("PeakJobMemoryUsed", ctypes.c_size_t),
    ]


class _Accounting(ctypes.Structure):
    _fields_ = [(name, ctypes.c_int64) for name in (
        "TotalUserTime", "TotalKernelTime", "ThisPeriodTotalUserTime",
        "ThisPeriodTotalKernelTime",
    )] + [(name, ctypes.c_uint32) for name in (
        "TotalPageFaultCount", "TotalProcesses", "ActiveProcesses",
        "TotalTerminatedProcesses",
    )]


class _WindowsAPI:
    """Small, checked ctypes binding; no optional dependencies or shell tools."""

    def __init__(self) -> None:
        self.dll = ctypes.WinDLL("kernel32", use_last_error=True)
        handle, dword, boolean = ctypes.c_void_p, ctypes.c_uint32, ctypes.c_int32
        signatures = {
            "CreateJobObjectW": ([handle, ctypes.c_wchar_p], handle),
            "SetInformationJobObject": ([handle, dword, handle, dword], boolean),
            "QueryInformationJobObject": ([handle, dword, handle, dword, handle], boolean),
            "AssignProcessToJobObject": ([handle, handle], boolean),
            "TerminateJobObject": ([handle, dword], boolean),
            "OpenProcess": ([dword, boolean, dword], handle),
            "CreateEventW": ([handle, boolean, boolean, ctypes.c_wchar_p], handle),
            "SetEvent": ([handle], boolean),
            "WaitForSingleObject": ([handle, dword], dword),
            "CloseHandle": ([handle], boolean),
        }
        for name, (args, result) in signatures.items():
            function = getattr(self.dll, name)
            function.argtypes = args
            function.restype = result

    def checked(self, name: str, *args):
        result = getattr(self.dll, name)(*args)
        if not result:
            raise ctypes.WinError(ctypes.get_last_error())
        return result


def _windows_child(gate: int, timeout: float, command: Sequence[str]) -> int:
    """Do not execute user code until the parent has assigned us to its job."""
    api = _WindowsAPI()
    try:
        # Also bounded if the parent dies before assigning/signalling the job.
        if api.dll.WaitForSingleObject(gate, 30000) != 0:
            raise CleanupError("Windows launcher ownership gate failed or timed out")
    finally:
        api.checked("CloseHandle", gate)
    # Keep the launcher alive for graceful control events. The command inherits
    # our console group and job, and can install its own CTRL_BREAK handler.
    signal.signal(signal.SIGBREAK, lambda _signum, _frame: None)
    process = subprocess.Popen(command, **_standard_streams())
    try:
        return _wait_for_exit(process, max(0, timeout) + GRACE_SECONDS + FORCE_SECONDS + 10)
    except subprocess.TimeoutExpired as error:
        # The parent's job owns cleanup even if this launcher exits first.
        raise CleanupError("Windows launcher outlived the parent timeout") from error


class _WindowsJob:
    """Own descendants independently of the lifetime of the root process."""

    def __init__(self) -> None:
        self.api = _WindowsAPI()
        self.handle = self.api.checked("CreateJobObjectW", None, None)
        try:
            limits = _ExtendedLimits()
            # No breakaway flags: descendants stay owned, including nested jobs.
            limits.BasicLimitInformation.LimitFlags = 0x2000  # KILL_ON_JOB_CLOSE
            self.api.checked("SetInformationJobObject", self.handle, 9,
                             ctypes.byref(limits), ctypes.sizeof(limits))
        except BaseException:
            self.close()
            raise

    def start(self, command: Sequence[str], timeout: float) -> subprocess.Popen[bytes]:
        gate = self.api.checked("CreateEventW", None, True, False, None)
        process = None
        try:
            os.set_handle_inheritable(gate, True)
            startup = subprocess.STARTUPINFO()
            startup.lpAttributeList = {"handle_list": [gate]}
            bootstrap = (
                "import runpy,sys; "
                "sys.exit(runpy.run_path(sys.argv[1])['_windows_child']("
                "int(sys.argv[2]),float(sys.argv[3]),sys.argv[4:]))"
            )
            try:
                process = subprocess.Popen(
                    [sys.executable, "-c", bootstrap, os.path.abspath(__file__),
                     str(gate), str(timeout), *command],
                    creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
                    startupinfo=startup,
                    close_fds=True,
                    # CPython adds these duplicated handles to handle_list. With
                    # all three left as None, redirected output can be excluded.
                    **_standard_streams(),
                )
            finally:
                os.set_handle_inheritable(gate, False)
            # The launcher cannot spawn the command before this assignment.
            process_handle = self.api.checked("OpenProcess", 0x0101, False, process.pid)
            try:
                self.api.checked("AssignProcessToJobObject", self.handle, process_handle)
            finally:
                self.api.checked("CloseHandle", process_handle)
            self.api.checked("SetEvent", gate)
            return process
        except BaseException:
            if process is not None:
                # Before the gate opens there are no command descendants.
                process.kill()
                process.wait(timeout=FORCE_SECONDS)
            raise
        finally:
            self.api.checked("CloseHandle", gate)

    def wait_empty(self, timeout: float) -> bool:
        deadline = time.monotonic() + timeout
        while True:
            accounting = _Accounting()
            self.api.checked("QueryInformationJobObject", self.handle, 1,
                             ctypes.byref(accounting), ctypes.sizeof(accounting), None)
            if accounting.ActiveProcesses == 0:
                return True
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return False
            time.sleep(min(0.05, remaining))

    def kill(self) -> None:
        self.api.checked("TerminateJobObject", self.handle, 124)

    def close(self) -> None:
        if self.handle is not None:
            self.api.checked("CloseHandle", self.handle)
            self.handle = None


def terminate(process: subprocess.Popen[bytes], job: _WindowsJob | None = None) -> None:
    if job is not None:
        if process.poll() is None:
            try:
                process.send_signal(signal.CTRL_BREAK_EVENT)
            except OSError:
                # No console (e.g. a CI service) or a racing root exit. The job
                # remains authoritative and forceful cleanup is still required.
                pass
        if not job.wait_empty(GRACE_SECONDS):
            job.kill()
            if not job.wait_empty(FORCE_SECONDS):
                raise CleanupError("Windows job still has active processes after termination")
        process.wait(timeout=FORCE_SECONDS)
        return

    # Never use the root's poll() as evidence that its process group is gone.
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        process.wait(timeout=FORCE_SECONDS)
        return
    try:
        process.wait(timeout=GRACE_SECONDS)
    except subprocess.TimeoutExpired:
        pass
    # Also kill surviving descendants when the root accepted SIGTERM first.
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait(timeout=FORCE_SECONDS)


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--timeout", type=float, required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    command = list(args.command)
    if command and command[0] == "--":
        command.pop(0)
    if not command:
        parser.error("a command is required after --timeout")
    if not (-float("inf") < args.timeout < float("inf")):
        parser.error("--timeout must be finite")

    job = None
    result = 125
    diagnostics = []
    try:
        if os.name == "nt":
            job = _WindowsJob()
            process = job.start(command, args.timeout)
        else:
            process = subprocess.Popen(command, start_new_session=True)
        try:
            result = _wait_for_exit(process, args.timeout)
        except subprocess.TimeoutExpired:
            result = 124
            diagnostics.append(f"command timed out after {args.timeout:g}s: {' '.join(command)}")
            try:
                terminate(process, job)
            except (OSError, CleanupError, subprocess.TimeoutExpired) as error:
                diagnostics.append(f"timeout cleanup failed: {error}")
        else:
            if job is not None:
                # A successful root exit must not release live descendants.
                job.kill()
                if not job.wait_empty(FORCE_SECONDS):
                    raise CleanupError("Windows job still has active processes after root exit")
    except (OSError, CleanupError, subprocess.TimeoutExpired) as error:
        diagnostics.append(f"command ownership/cleanup failed: {error}")
        result = 125
    finally:
        if job is not None:
            try:
                job.close()
            except OSError as error:
                diagnostics.append(f"job cleanup failed: {error}")
                if result != 124:
                    result = 125
    # A blocked diagnostic pipe must not hold the job open or delay tree cleanup.
    for message in diagnostics:
        print(message, file=sys.stderr)
    return result


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
