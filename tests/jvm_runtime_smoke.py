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
    parser.add_argument(
        "--startup-timeout",
        type=float,
        default=90.0,
        help="maximum seconds to wait for all JVM fixture evidence",
    )
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
        environment = os.environ.copy()
        environment["CPPFM_SERVER_DIR"] = world
        with tempfile.TemporaryFile(mode="w+", encoding="utf-8") as log_file:
            proc = subprocess.Popen(
                command,
                cwd=os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                env=environment,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                text=True,
                start_new_session=True,
            )
            output: list[str] = []

            def refresh() -> None:
                log_file.flush()
                log_file.seek(0)
                output[:] = log_file.read().splitlines()

            if args.startup_timeout <= 0:
                parser.error("--startup-timeout must be positive")
            deadline = time.monotonic() + args.startup_timeout
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
                "dynamic route JVM_TRANSFORMED owner=net/minecraft/server/MinecraftServer method=setTick descriptor=(J)V tick=1",
                "fixture COMMAND_REGISTERED",
                "fixture COMMAND_EXECUTED 7",
                "fixture DAMAGE_BRIDGE_SETUP true",
                "fixture DAMAGE_PROBE_CANCEL result=false health=4.0 callbacks=1",
                "fixture DAMAGE_BRIDGE_CANCEL true",
                "fixture DAMAGE_BRIDGE_NESTED_REJECT true",
                "fixture DAMAGE_BRIDGE_DEATH_VETO true",
                "fixture DAMAGE_BRIDGE_DEATH_ALLOW true",
                "fixture WORLD_API",
                "fixture RELOAD_SUCCESS resource=true",
                "fixture RELOAD_FAILURE_INVOKED",
                "fixture RELOAD_AFTER_FAILURE",
                "server resource reload listener cppfm_fixture:reload_failure failed:",
                "java.lang.IllegalStateException: fixture reload failure",
                "server resource reload complete success=false",
                "server /reload Java listeners success=false",
                "fixture RELOAD_COMMAND_RESULT true",
                "fixture POST_RELOAD_TICK",
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
                heads = [item for item in output if "fixture MIXIN_HEAD " in item]
                tails = [item for item in output if "fixture MIXIN_TAIL " in item]
                end_ticks = [item for item in output if "fixture END_SERVER_TICK" in item]
                if (len(heads) != 1 or "tick=1 nativeTick=1 count=1" not in heads[0]
                        or len(tails) != 1 or "tick=1 nativeTick=1 count=1" not in tails[0]
                        or len(end_ticks) != 1):
                    print("JVM smoke dynamic route duplicated or lost evidence:", file=sys.stderr)
                    print("\n".join(output[-80:]), file=sys.stderr)
                    return 1
            finally:
                if proc.poll() is None:
                    try:
                        os.killpg(proc.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                try:
                    proc.wait(timeout=15.0)
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(proc.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    proc.wait(timeout=5.0)
                refresh()
            if proc.returncode != 0:
                print("JVM smoke server exited with", proc.returncode, file=sys.stderr)
                print("\n".join(output[-80:]), file=sys.stderr)
                return 1
            stopping = [
                item for item in output
                if "fixture SHUTDOWN_EVENT " in item and
                "phase=server-stopping " in item
            ]
            world_unloads = [
                item for item in output
                if "fixture SHUTDOWN_EVENT " in item and
                "phase=world-unload " in item
            ]
            stopped = [
                item for item in output
                if "fixture SHUTDOWN_EVENT " in item and
                "phase=server-stopped " in item
            ]
            expected_dimensions = {
                "minecraft:overworld",
                "minecraft:the_nether",
                "minecraft:the_end",
            }
            actual_dimensions = {
                item.split("dimension=", 1)[1].split(" ", 1)[0]
                for item in world_unloads
                if "dimension=" in item
            }
            if (len(stopping) != 1 or "status=PASS" not in stopping[0] or
                    "count=1" not in stopping[0] or "nativeServer=0" in stopping[0] or
                    "worldHandle=0" in stopping[0] or "tick=0" in stopping[0] or
                    "nativeMutation=PASS" not in stopping[0] or
                    len(world_unloads) != 3 or
                    any("status=PASS" not in item or "nativeServer=0" in item or
                        "worldHandle=0" in item or "tick=0" in item
                        for item in world_unloads) or
                    actual_dimensions != expected_dimensions or
                    len(stopped) != 1 or "status=PASS" not in stopped[0] or
                    "count=1" not in stopped[0] or "nativeServer=0" in stopped[0] or
                    "worldHandle=0" not in stopped[0] or "tick=0" in stopped[0] or
                    "worldsClosed=PASS" not in stopped[0]):
                print(
                    "JVM shutdown lifecycle evidence invalid: expected "
                    "SERVER_STOPPING, one valid UNLOAD per world, then "
                    "SERVER_STOPPED after worlds close",
                    file=sys.stderr,
                )
                print("\n".join(output[-80:]), file=sys.stderr)
                return 1
            shutdown_markers = [stopping[0], *world_unloads, stopped[0]]
            if any(output.index(before) >= output.index(after)
                   for before, after in zip(shutdown_markers,
                                            shutdown_markers[1:])):
                print("JVM shutdown lifecycle events were out of order", file=sys.stderr)
                print("\n".join(output[-80:]), file=sys.stderr)
                return 1
            if any("fixture CALLBACK_THREAD_MISMATCH" in item for item in output):
                print("JVM shutdown callback left the authoritative server thread",
                      file=sys.stderr)
                print("\n".join(output[-80:]), file=sys.stderr)
                return 1
            print("JVM smoke: PASS")
            for item in output:
                if (any(token in item for token in required) or
                        "fixture SHUTDOWN_EVENT " in item):
                    print(item)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
