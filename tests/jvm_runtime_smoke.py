#!/usr/bin/env python3
"""Process-level plan51 gate: HotSpot, entrypoint, tick, Mixin, clean stop."""

from __future__ import annotations

import argparse
import os
import signal
import subprocess
import sys
import tempfile
import time


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--classes", required=True)
    parser.add_argument("--mods", required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="cppfm-jvm-smoke-") as world:
        command = [
            os.path.abspath(args.binary),
            "--jvm=true",
            "--jvm-strict=true",
            "--jvm-classes=" + os.path.abspath(args.classes),
            "--jvm-mods=" + os.path.abspath(args.mods),
            "--world-dir=" + world,
            "--port=0",
        ]
        with tempfile.TemporaryFile(mode="w+", encoding="utf-8") as log_file:
            proc = subprocess.Popen(
                command,
                cwd=os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                stdout=log_file,
                stderr=subprocess.STDOUT,
                text=True,
            )
            output: list[str] = []

            def refresh() -> None:
                log_file.flush()
                log_file.seek(0)
                output[:] = log_file.read().splitlines()

            deadline = time.monotonic() + 30.0
            required = (
                "embedded HotSpot started",
                "fixture entrypoint initialized",
                "fixture SERVER_STARTED",
                "fixture END_SERVER_TICK",
                "fixture COMMAND_EXECUTED 8",
                "fixture MIXIN_HEAD",
                "fixture MIXIN_TAIL",
                "fixture MIXIN_RETURN",
                "fixture MIXIN_OVERWRITE tick=1",
                "fixture COMMAND_REGISTERED",
                "fixture COMMAND_EXECUTED 7",
                "fixture WORLD_API",
            )
            try:
                while time.monotonic() < deadline and proc.poll() is None:
                    refresh()
                    if all(any(token in item for item in output) for token in required):
                        break
                    time.sleep(0.1)
                refresh()
                if not all(any(token in item for item in output) for token in required):
                    print("JVM smoke missing evidence:", file=sys.stderr)
                    print("\n".join(output[-80:]), file=sys.stderr)
                    return 1
            finally:
                if proc.poll() is None:
                    proc.send_signal(signal.SIGTERM)
                try:
                    proc.wait(timeout=15.0)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5.0)
                refresh()
            if proc.returncode != 0:
                print("JVM smoke server exited with", proc.returncode, file=sys.stderr)
                print("\n".join(output[-80:]), file=sys.stderr)
                return 1
            print("JVM smoke: PASS")
            for item in output:
                if any(token in item for token in required):
                    print(item)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
