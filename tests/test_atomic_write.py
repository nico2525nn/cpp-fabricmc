#!/usr/bin/env python3
"""Regression tests for safe replacement of generated text files."""

from __future__ import annotations

from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from atomic_write import write_text_atomic


class AtomicWriteTests(unittest.TestCase):
    def test_replaces_complete_file_and_preserves_mode(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "generated.hpp"
            target.write_text("old\n", encoding="utf-8")
            target.chmod(0o640)

            write_text_atomic(target, "new\ncontent\n")

            self.assertEqual(target.read_text(encoding="utf-8"), "new\ncontent\n")
            self.assertEqual(target.stat().st_mode & 0o777, 0o640)
            self.assertEqual(list(Path(directory).glob("*.tmp")), [])

    def test_failed_replace_keeps_original_and_cleans_temporary(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "generated.hpp"
            target.write_text("original\n", encoding="utf-8")

            with patch("atomic_write.os.replace", side_effect=OSError("injected")):
                with self.assertRaisesRegex(OSError, "injected"):
                    write_text_atomic(target, "partial candidate\n")

            self.assertEqual(target.read_text(encoding="utf-8"), "original\n")
            self.assertEqual(list(Path(directory).iterdir()), [target])

    def test_refuses_symlink_output(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "target.txt"
            target.write_text("preserve\n", encoding="utf-8")
            link = root / "generated.hpp"
            try:
                link.symlink_to(target)
            except (NotImplementedError, OSError) as error:
                self.skipTest(f"symlinks unavailable: {error}")

            with self.assertRaisesRegex(OSError, "symlink"):
                write_text_atomic(link, "replace\n")
            self.assertEqual(target.read_text(encoding="utf-8"), "preserve\n")


if __name__ == "__main__":
    unittest.main()
