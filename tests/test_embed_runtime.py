#!/usr/bin/env python3
"""Tests that the runtime packer fails closed on missing required assets."""

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from embed_runtime import collect, required_asset_paths, verify_required_assets


class RequiredEmbeddedAssetTests(unittest.TestCase):
    def test_manifest_comments_and_required_files(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "required.txt"
            manifest.write_text(
                "# runtime contract\nregistry/tags.bin\n\nentities/pig.json\n",
                encoding="utf-8",
            )
            required = required_asset_paths(manifest)
            self.assertEqual(required, {"registry/tags.bin", "entities/pig.json"})
            verify_required_assets(
                [("assets/registry/tags.bin", b"tags"),
                 ("assets/entities/pig.json", b"{}")],
                required,
            )

    def test_missing_required_path_fails_closed(self) -> None:
        with self.assertRaisesRegex(ValueError, "not collected"):
            verify_required_assets([], {"registry/tags.bin"})

    def test_empty_manifest_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "required.txt"
            manifest.write_text("# intentionally empty\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "manifest is empty"):
                required_asset_paths(manifest)

    def test_unsafe_manifest_path_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "required.txt"
            manifest.write_text("../outside.bin\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unsafe required asset path"):
                required_asset_paths(manifest)

    def test_symlinked_file_and_directory_are_not_silently_omitted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            assets = root / "assets"
            assets.mkdir()
            external = root / "outside.bin"
            external.write_bytes(b"not embedded")
            try:
                (assets / "file.bin").symlink_to(external)
                (assets / "linked-dir").symlink_to(root, target_is_directory=True)
            except (NotImplementedError, OSError) as error:
                self.skipTest(f"symlinks unavailable: {error}")

            with self.assertRaisesRegex(ValueError, "symlink"):
                collect(assets, "assets", [])
            (assets / "file.bin").unlink()
            with self.assertRaisesRegex(ValueError, "symlink"):
                collect(assets, "assets", [])


if __name__ == "__main__":
    unittest.main()
