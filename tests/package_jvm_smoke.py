#!/usr/bin/env python3
"""Validate the exact release ZIP's embedded default-on JVM boundary.

This is deliberately smaller than ``jvm_runtime_smoke.py``.  The latter uses
the build-tree classes and fixture mod directory to exercise the broader JVM
compatibility surface.  This gate validates the release boundary itself:
after the CPack ZIP has been built by the ``package`` target, verify that it
contains one executable, extract it into a clean directory, and start that
executable without a classes or assets override.  ``--jvm-strict=true`` turns
an unavailable embedded JVM into a real package-gate failure while leaving
normal native fallback behavior unchanged for regular server launches.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path, PurePosixPath
import signal
import stat
import subprocess
import sys
import tempfile
import time
from typing import TextIO
import zipfile


START_TIMEOUT = 45.0
STOP_TIMEOUT = 15.0


class GateError(RuntimeError):
    """A package-gate precondition or runtime assertion failed."""


def _terminate(process: subprocess.Popen[object], timeout: float = STOP_TIMEOUT) -> None:
    """Terminate only the process group owned by *process*."""

    if process.poll() is not None:
        return
    if os.name == "nt":
        process.terminate()
    else:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            return
    try:
        process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        if os.name == "nt":
            process.kill()
        else:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        process.wait(timeout=5.0)


def _read_package_member(archive: Path, executable_name: str) -> zipfile.ZipInfo:
    if not archive.is_file():
        raise GateError(f"package archive does not exist: {archive}")
    try:
        package = zipfile.ZipFile(archive)
    except (OSError, zipfile.BadZipFile) as exc:
        raise GateError(f"package archive is not a readable ZIP: {archive}: {exc}") from exc

    with package:
        files = [info for info in package.infolist() if not info.is_dir()]
        directories = [info.filename for info in package.infolist() if info.is_dir()]
        if directories:
            raise GateError(f"package contains directory entries: {directories!r}")
        if [info.filename for info in files] != [executable_name]:
            names = [info.filename for info in files]
            raise GateError(
                f"package must contain exactly {executable_name!r}; found {names!r}"
            )
        member = files[0]
        path = PurePosixPath(member.filename)
        if path.is_absolute() or ".." in path.parts:
            raise GateError(f"package member has an unsafe path: {member.filename!r}")
        if os.name != "nt":
            mode = (member.external_attr >> 16) & 0o777
            if not mode & (stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH):
                raise GateError(
                    f"package executable lacks an executable mode: {member.filename!r}"
                )
        return member


def _extract_package(archive: Path, destination: Path, executable_name: str) -> Path:
    member = _read_package_member(archive, executable_name)
    with zipfile.ZipFile(archive) as package:
        package.extractall(destination)
    executable = destination / executable_name
    if not executable.is_file():
        raise GateError(f"extracted package executable is missing: {executable}")
    if os.name != "nt":
        mode = (member.external_attr >> 16) & 0o777
        # Python's zipfile extractor does not restore POSIX mode bits.  Check
        # the archive bits above, then apply those verified bits for the clean
        # extracted execution below.
        executable.chmod(mode)
    return executable


def _read_log(log_file: TextIO) -> list[str]:
    log_file.flush()
    log_file.seek(0)
    return log_file.read().splitlines()


def _run_embedded_jvm(executable: Path, server_root: Path) -> list[str]:
    environment = os.environ.copy()
    environment["CPPFM_SERVER_DIR"] = str(server_root)
    # Do not let a caller's checkout/build-tree override turn this into a
    # test of external classes.  The command itself also intentionally omits
    # --jvm=true: the default-on behavior is what this gate is checking.
    environment.pop("CPPFM_JVM_CLASSES", None)
    command = [str(executable), "--jvm-strict=true", "--port=0"]
    expected_classes = str(server_root / ".cppfm" / "jvm" / "classes")
    marker = "embedded HotSpot started"
    process: subprocess.Popen[str]
    with tempfile.TemporaryFile(mode="w+", encoding="utf-8") as log_file:
        try:
            process = subprocess.Popen(
                command,
                cwd=str(executable.parent),
                env=environment,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                text=True,
                start_new_session=True,
            )
        except OSError as exc:
            raise GateError(
                f"could not start extracted package executable {executable}: {exc}"
            ) from exc
        try:
            deadline = time.monotonic() + START_TIMEOUT
            output: list[str] = []
            while time.monotonic() < deadline and process.poll() is None:
                output = _read_log(log_file)
                if any(marker in line for line in output):
                    break
                time.sleep(0.1)
            output = _read_log(log_file)
            if not any(marker in line for line in output):
                raise GateError(
                    f"package JVM startup marker was not observed within {START_TIMEOUT:g}s:\n"
                    + "\n".join(output[-80:])
                )
            if not any(
                marker in line and f"classes={expected_classes}" in line
                for line in output
            ):
                raise GateError(
                    "package JVM started without using the extracted embedded classes "
                    f"directory {expected_classes}:\n" + "\n".join(output[-80:])
                )
        finally:
            _terminate(process)
            output = _read_log(log_file)
        if process.returncode != 0:
            raise GateError(
                f"package JVM process exited with {process.returncode}:\n"
                + "\n".join(output[-80:])
            )
        return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--executable-name", default="cppfm")
    args = parser.parse_args()

    archive = args.package.resolve()

    with tempfile.TemporaryDirectory(prefix="cppfm-package-jvm-") as temporary:
        root = Path(temporary)
        extracted = root / "extracted"
        server = root / "server"
        extracted.mkdir()
        executable = _extract_package(archive, extracted, args.executable_name)
        output = _run_embedded_jvm(executable, server)
        class_files = list((server / ".cppfm" / "jvm" / "classes").rglob("*.class"))
        if not class_files:
            raise GateError("package JVM gate found no extracted embedded .class files")
        if not (server / "assets" / "registry").is_dir():
            raise GateError("package JVM gate found no extracted registry assets")
        print("package_jvm_smoke: PASS")
        print("  CPack archive: one executable")
        print("  JVM: embedded default-on startup with strict availability")
        print(f"  extracted classes: {len(class_files)} files")
        for line in output:
            if "embedded HotSpot started" in line:
                print(f"  evidence: {line}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except GateError as exc:
        print(f"package_jvm_smoke: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
