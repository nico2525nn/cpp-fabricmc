#!/usr/bin/env python3
"""Ensure an override cannot relabel another block order as protocol 769."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "tools" / "gen_block_registry_ids.py"
OUTPUT = ROOT / "src" / "generated" / "BlockRegistryIds.hpp"


class BlockRegistryGeneratorTests(unittest.TestCase):
    def test_custom_source_must_match_verified_1214_order(self) -> None:
        original_header = OUTPUT.read_bytes()
        blocks = [
            {"id": index, "name": f"synthetic_{index}"}
            for index in range(1095)
        ]
        blocks[0]["name"] = "air"
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "blocks.json"
            source.write_text(json.dumps(blocks), encoding="utf-8")
            environment = os.environ.copy()
            environment["BLOCKS_JSON"] = str(source)
            result = subprocess.run(
                [sys.executable, str(GENERATOR), "--check"],
                cwd=ROOT,
                env=environment,
                capture_output=True,
                text=True,
                check=False,
                timeout=10,
            )

        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("does not match the verified Minecraft 1.21.4", result.stderr)
        self.assertEqual(OUTPUT.read_bytes(), original_header)


if __name__ == "__main__":
    unittest.main()
