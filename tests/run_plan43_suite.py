#!/usr/bin/env python3
"""Standalone driver for suite_plan43_b1b2 (fast re-verification without the
full 194-check server_full run)."""
import shutil
import sys, tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_server_full import (find_free_port, launch_server, kill_server,
                              suite_plan43_b1b2, summary_and_exit)

binary = sys.argv[1] if len(sys.argv) > 1 else "./build/cppfm"
binary_path = Path(binary).resolve()
port = find_free_port()
world_dir = tempfile.mkdtemp(prefix="p43suite_")
assets = (Path.cwd() / "assets" / "registry").resolve()
server_args = ["--max-players=200", "--view-distance=4"]
# A release binary outside the checkout must exercise its embedded assets;
# inject the development registry only when this driver is run from a source
# tree that actually has one.
if binary_path.parent == (Path.cwd() / "build").resolve() and assets.is_dir():
    server_args.insert(0, f"--assets={assets}")
proc = launch_server(binary, port, world_dir, extra_args=server_args)
try:
    suite_plan43_b1b2("127.0.0.1", port)
finally:
    kill_server(proc)
    shutil.rmtree(world_dir, ignore_errors=True)
summary_and_exit()
