#!/usr/bin/env python3
"""Checks the project's own C++ against the Crust subset (see CPPRUST.md).

This is a text scan, not a parser -- the same position cpprust.py is in. It
cannot prove a file lowers; it catches the constructs that are known to be
refused, so they are found while writing rather than at lowering time.

Only our own sources are checked. Vendored litehtml and quickjs are outside
our control and are a separate problem.

Usage:
    ./check_crust.py            check the default directories
    ./check_crust.py headless   check specific paths
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DEFAULT_PATHS = ["headless", "cairo", "terminal", "gtk", "mininode"]

# (regex, message). Each corresponds to something CPPRUST.md refuses or
# does not supply.
RULES = [
    (r"\b(?:std::)?(?:ostringstream|istringstream|stringstream)\b",
     "stringstream: stream operators are not in the subset; append to a std::string"),
    (r"\b(?:std::)?(?:ifstream|ofstream|fstream)\b",
     "fstream: not in the subset; use fopen/fread from <stdio.h>"),
    (r"\b(?:std::)?(?:cout|cerr|cin|endl)\b",
     "iostreams: not in the subset; use printf/fprintf"),
    (r"\b(?:cout|cerr|clog|ss|oss|os)\s*<<",
     "stream operator: operator<< is refused; append to a std::string"),
    (r"\benum\s+class\b",
     "enum class: no scoped enumeration; use a plain enum"),
    (r"\bconstexpr\b",
     "constexpr: not in the subset; use const or a plain enum"),
    (r"\bemplace_back\b",
     "emplace_back: supplied vector has push_back only"),
    (r"\b(?:std::)?(?:sort|stable_sort|transform|find_if|accumulate|copy_if)\s*\(",
     "<algorithm>: not supplied; write the loop out"),
    (r"\bstd::(?:max|min)\s*\(",
     "std::max/std::min: not supplied; use an if"),
    (r"\bstd::(?:map|set|list|deque|array|pair|tuple|function|optional)\b",
     "container/utility outside the supplied set (string, vector, ownvector, map, unique_ptr, shared_ptr)"),
    (r"\bvector\s*<\s*bool\s*>",
     "vector<bool>: the proxy reference has no lowering"),
    (r"\bthrow\b|\btry\s*\{|\bcatch\s*\(",
     "exceptions are not in the subset"),
    (r"\bdynamic_cast\b|\btypeid\b",
     "dynamic_cast / typeid need RTTI and are refused"),
    (r"\bgoto\b",
     "goto is refused while a destructor is pending"),
    (r"\bnew\s+\w+\s*\[",
     "array new: refused, the element count is not recorded"),
    (r"\bdelete\s*\[\s*\]",
     "delete[]: refused, see array new"),
    (r"^\s*(?:template\s*<[^>]*>\s*)?[\w:<>*&\s]+&\s*\w+\s*\([^)]*\)\s*(?:const\s*)?[{;]",
     "reference return: refused except for operator[] / operator*"),
    (r"^[\w:<>*&\s]+\s+\w+\s*\([^)]*\w+\s*(?<![=!<>+\-*/%&|^])=(?!=)\s*[^),]+\)\s*(?:const\s*)?[{;]",
     "default function argument: refused"),
    (r"for\s*\(\s*(?:const\s+)?auto\s*&?\s*\w+\s*:\s*[^)]*\(\s*\)",
     "range-for over a call result: the range must be a nameable chain"),
    (r"\bnamespace\s*\{",
     "anonymous namespace: use static instead"),
]

COMPILED = [(re.compile(p), m) for p, m in RULES]

# A line ending in one of these is a comment or string and is skipped for the
# operator rules, which are the only ones prone to false positives.
def strip_noise(line: str) -> str:
    line = re.sub(r"//.*$", "", line)
    line = re.sub(r'"(?:[^"\\]|\\.)*"', '""', line)
    line = re.sub(r"'(?:[^'\\]|\\.)*'", "''", line)
    return line


def check_file(path: Path):
    findings = []
    in_block_comment = False

    for n, raw in enumerate(path.read_text(errors="replace").splitlines(), 1):
        line = raw

        # Crude block comment tracking, enough for our own sources.
        if in_block_comment:
            if "*/" in line:
                line = line.split("*/", 1)[1]
                in_block_comment = False
            else:
                continue
        if "/*" in line:
            before, _, after = line.partition("/*")
            if "*/" in after:
                line = before + after.split("*/", 1)[1]
            else:
                line = before
                in_block_comment = True

        line = strip_noise(line)

        if not line.strip():
            continue

        for rx, msg in COMPILED:
            if rx.search(line):
                findings.append((n, msg, raw.strip()))

    return findings


def main():
    args = sys.argv[1:]
    paths = [Path(a) for a in args] if args else [ROOT / p for p in DEFAULT_PATHS]

    files = []
    for p in paths:
        p = p if p.is_absolute() else ROOT / p
        if p.is_dir():
            files.extend(sorted(p.rglob("*.cpp")))
            files.extend(sorted(p.rglob("*.h")))
        elif p.exists():
            files.append(p)

    # crust_compat.h exists precisely to hold the one non-subset construct,
    # behind an #ifndef the lowering evaluates away.
    files = [f for f in files if f.name != "crust_compat.h"]

    total = 0
    for f in sorted(set(files)):
        findings = check_file(f)
        if findings:
            rel = f.relative_to(ROOT) if f.is_relative_to(ROOT) else f
            for n, msg, src in findings:
                print(f"{rel}:{n}: {msg}")
                print(f"    {src}")
                total += 1

    if total:
        print(f"\n{total} issue(s) outside the C++ subset")
        return 1

    print(f"{len(files)} file(s) checked, all inside the C++ subset")
    return 0


if __name__ == "__main__":
    sys.exit(main())
