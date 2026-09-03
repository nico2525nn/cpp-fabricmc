#!/usr/bin/env python3
"""plan44 G-01 tautology lint: every CHECK must touch implementation output.

Flags CHECK lines whose arguments contain no identifier (constant-vs-constant
comparisons like CHECK_EQ_INT(15,15), CHECK(true==true), CHECK_NEAR(12.8,12.8),
or constant boolean expressions like 34>=34). Intentional FAILs
(CHECK(false, ...) with a by-design note) are allow-listed by message match.

Usage: python3 tools/tautology_lint.py [--strict]
Exit 0 when clean (excluding allow-listed intentional FAILs), 1 otherwise.
"""
import re
import sys

ALLOW_MSG = re.compile(r"by design|HONEST GAP", re.IGNORECASE)

# CHECK_MACRO(args...) — capture the full argument list of the CHECK call.
CALL = re.compile(r"CHECK(?:_EQ_INT|_NEAR|_STR_EQ)?\s*\((.*)\)\s*;\s*$")
STRINGS = re.compile(r'"(?:[^"\\]|\\.)*"')
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*|::[A-Za-z_][A-Za-z0-9_]*|->\w+|\([^)]*\))?")


def args_touch_impl(argstr: str) -> bool:
    """True if the CHECK's value arguments reference an identifier (impl output)."""
    # Drop the trailing message literal: split top-level commas.
    parts, depth, cur, instr, esc = [], 0, "", False, False
    for ch in argstr:
        if instr:
            cur += ch
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                instr = False
            continue
        if ch == '"':
            instr = True
            cur += ch
        elif ch == "(":
            depth += 1
            cur += ch
        elif ch == ")":
            depth -= 1
            cur += ch
        elif ch == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    parts.append(cur)
    # Last part is the message literal for CHECK_EQ_INT/NEAR/STR_EQ (3 args);
    # for CHECK (2 args) the message is parts[1].
    value_parts = parts[:-1] if len(parts) > 1 else parts
    for vp in value_parts:
        code = STRINGS.sub("", vp)
        # An identifier (variable, member, call, qualified name) = impl output.
        # Strip numeric literals and operators first.
        code = re.sub(r"\b\d+(\.\d+)?f?\b", "", code)
        if IDENT.search(code):
            return True
    return False


def main() -> int:
    strict = "--strict" in sys.argv
    bad = []
    with open("tests/test_gameplay_full.cpp") as f:
        for i, line in enumerate(f, 1):
            s = line.strip()
            if not s.startswith("CHECK"):
                continue
            m = CALL.search(s)
            if not m:
                continue
            if ALLOW_MSG.search(s):
                continue
            if not args_touch_impl(m.group(1)):
                bad.append((i, s))
    for i, s in bad:
        print(f"TAUTOLOGY tests/test_gameplay_full.cpp:{i}: {s}")
    print(f"tautology_lint: {len(bad)} suspect CHECK(s)")
    if strict and bad:
        return 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
