#!/usr/bin/env python3
"""Static quality gates for the C++ regression tests and process harnesses.

The test suite may contain explicit negative assertions (``CHECK(false, ...)``)
when an input is supposed to be rejected.  What it must not contain are
unconditional successes or liveness-only fallbacks that turn an unobserved
feature into a pass.
"""

from __future__ import annotations

import argparse
import ast
import re
import sys
from pathlib import Path


def code_without_line_comments(line: str) -> str:
    """Remove the simple comments used by the C++ test sources.

    This is deliberately line-oriented: the audit is a guardrail for common
    test mistakes, not a C++ parser.  Block comments are not stripped because
    the forbidden patterns are only meaningful in assertion expressions.
    """

    return line.split("//", 1)[0]


def python_call_name(node: ast.Call) -> str | None:
    if isinstance(node.func, ast.Name):
        return node.func.id
    if isinstance(node.func, ast.Attribute):
        return node.func.attr
    return None


class PythonQualityVisitor(ast.NodeVisitor):
    """Find assertion patterns that can turn a missing observation into PASS."""

    def __init__(self, path: Path, failures: list[str]):
        self.path = path
        self.failures = failures

    def report(self, node: ast.AST, message: str) -> None:
        self.failures.append(f"{self.path}:{node.lineno}: {message}")

    def visit_Call(self, node: ast.Call) -> None:
        name = python_call_name(node)
        if name in {"check", "CHECK"} and node.args:
            first = node.args[0]
            if isinstance(first, ast.Constant) and first.value is True:
                self.report(node, "unconditional assertion")
        if (isinstance(node.func, ast.Attribute) and
                node.func.attr == "Popen"):
            start_new_session = next(
                (keyword.value for keyword in node.keywords
                 if keyword.arg == "start_new_session"), None)
            if not (isinstance(start_new_session, ast.Constant) and
                    start_new_session.value is True):
                self.report(node, "Popen must create an owned process group")
        self.generic_visit(node)

    def visit_BoolOp(self, node: ast.BoolOp) -> None:
        if isinstance(node.op, ast.Or) and any(
                isinstance(value, ast.Constant) and value.value is True
                for value in node.values):
            self.report(node, "unconditional OR fallback")
        self.generic_visit(node)

    def visit_Compare(self, node: ast.Compare) -> None:
        if (isinstance(node.left, ast.Call) and
                python_call_name(node.left) == "len" and
                len(node.ops) == 1 and len(node.comparators) == 1 and
                isinstance(node.comparators[0], ast.Constant) and
                node.comparators[0].value == 0 and
                isinstance(node.ops[0], ast.GtE)):
            self.report(node, "length comparison accepts an empty result")
        self.generic_visit(node)

    def visit_Assert(self, node: ast.Assert) -> None:
        if isinstance(node.test, ast.Constant) and node.test.value is True:
            self.report(node, "unconditional assert")
        self.generic_visit(node)

    def visit_ExceptHandler(self, node: ast.ExceptHandler) -> None:
        if node.type is None:
            self.report(node, "bare except hides unrelated failures")
        self.generic_visit(node)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True,
                        help="directory containing C++ regression tests")
    parser.add_argument("--source-root", type=Path,
                        help="optional production source directory")
    parser.add_argument("--python-root", type=Path, action="append", default=[],
                        help="directory of Python test/tool files to audit")
    args = parser.parse_args()
    test_root = args.root.resolve()
    failures: list[str] = []

    test_files = sorted(test_root.glob("*.cpp"))
    source_files = []
    if args.source_root:
        source_root = args.source_root.resolve()
        source_files = sorted(
            path for path in source_root.rglob("*")
            if path.suffix in {".cpp", ".hpp"})

    python_files = []
    for root in args.python_root:
        python_files.extend(sorted(root.resolve().rglob("*.py")))

    for path in test_files + source_files:
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except OSError as exc:
            failures.append(f"{path}: cannot read: {exc}")
            continue
        for number, line in enumerate(lines, 1):
            code = code_without_line_comments(line)
            if path in test_files and re.search(r"\b(?:CHECK|check)\s*\(\s*true\b", code):
                failures.append(f"{path}:{number}: unconditional assertion")
            if re.search(r"\|\|\s*true\b", code):
                failures.append(f"{path}:{number}: unconditional OR fallback")
            if path in test_files and re.search(
                    r"\|\|\s*[A-Za-z_]\w*\.alive\s*\(\s*\)", code):
                failures.append(f"{path}:{number}: liveness used as feature evidence")
            if path in test_files and re.search(r"==\s*true\s*\|\|.*==\s*false", code):
                failures.append(f"{path}:{number}: tautological boolean assertion")

        if path in test_files:
            for number, line in enumerate(lines, 1):
                code = code_without_line_comments(line)
                if re.search(r"\b(?:CHECK|check)\s*\([^\n]*\.alive\s*\(\s*\)", code):
                    if "[liveness]" not in line:
                        failures.append(f"{path}:{number}: liveness assertion needs [liveness] label")

    for path in python_files:
        try:
            tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
        except (OSError, SyntaxError) as exc:
            failures.append(f"{path}: cannot parse: {exc}")
            continue
        PythonQualityVisitor(path, failures).visit(tree)

    if failures:
        print("quality audit: FAIL")
        for failure in failures:
            print(f" - {failure}")
        return 1
    print(f"quality audit: PASS ({len(test_files)} C++ test files, "
          f"{len(source_files)} production files, {len(python_files)} Python files checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
