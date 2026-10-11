"""Small same-directory atomic text replacement for generated source files."""

from __future__ import annotations

import os
import stat
import tempfile
from pathlib import Path


def write_text_atomic(path: Path, contents: str) -> None:
    """Replace *path* only after the complete UTF-8 payload is on disk."""
    path = Path(path)
    if path.is_symlink():
        raise OSError(f"refusing to replace symlink output: {path}")
    mode = stat.S_IMODE(path.stat().st_mode) if path.exists() else 0o644
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", newline="\n", delete=False,
            dir=path.parent, prefix=f".{path.name}.", suffix=".tmp",
        ) as output:
            temporary = Path(output.name)
            output.write(contents)
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary, mode)
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            try:
                temporary.unlink()
            except FileNotFoundError:
                pass
