#!/usr/bin/env python3
"""Standalone driver for suite_plan43_b1b2 (fast re-verification without the
full 194-check server_full run)."""
import subprocess, sys, tempfile, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_server_full import (find_free_port, launch_server, kill_server,
                              suite_plan43_b1b2, summary_and_exit)

binary = sys.argv[1] if len(sys.argv) > 1 else "./build/cppfm"
port = find_free_port()
world_dir = tempfile.mkdtemp(prefix="p43suite_")
assets = str((Path.cwd() / "assets").resolve())
proc = launch_server(binary, port, world_dir,
                     extra_args=[f"--assets={assets}/registry", "--max-players=200", "--view-distance=4"])
try:
    suite_plan43_b1b2("127.0.0.1", port)
finally:
    kill_server(proc)
    time.sleep(0.5)
    try: subprocess.run(["pkill", "-9", "-f", "cppfm --por[t]"], timeout=2,
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception: pass
summary_and_exit()
