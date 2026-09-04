#pragma once
// Shared test harness: pass/fail counters + CHECK/SECTION macros (cleanup P5).
// Semantics are byte-identical to the per-file copies this replaces (same
// printf text, same counting): test names, log text and PASS totals unchanged.
// Files with their own section-tracking format (seed_parity, mining_full,
// wire_b6, gameplay_full, ...) keep their local variants by design (R6:
// assertion meaning must not change with harnessing).
#include <cstdio>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    bool c_ = static_cast<bool>(cond); \
    std::printf("  %s  %s\n", c_ ? " ok " : "FAIL", msg); \
    if (c_) ++g_pass; else ++g_fail; \
} while (0)
#define SECTION(name) std::printf("\n[%s]\n", name)
