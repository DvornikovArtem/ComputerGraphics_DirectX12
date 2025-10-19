#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Pack Marching Cubes triTable[256][16] into int4 triTable4[256][4].
Usage:
    python pack_tri_table.py MarchingCubesTables.hlsl > triTable_packed.hlsl
"""

import re
import sys
from pathlib import Path
from textwrap import indent

def remove_comments(src: str) -> str:
    # remove /* ... */ and // ...
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    src = re.sub(r"//.*?$", "", src, flags=re.M)
    return src

def extract_tritable(src: str):
    # find triTable block
    m = re.search(
        r"\bint\s+triTable\s*\[\s*256\s*\]\s*\[\s*16\s*\]\s*=\s*\{",
        src
    )
    if not m:
        sys.exit("ERROR: triTable[256][16] definition not found.")

    i = m.end()  # index after opening {
    depth = 1
    content = []
    while i < len(src) and depth > 0:
        ch = src[i]
        if ch == "{":
            depth += 1
            content.append(ch)
        elif ch == "}":
            depth -= 1
            if depth == 0:
                break
            content.append(ch)
        else:
            content.append(ch)
        i += 1

    if depth != 0:
        sys.exit("ERROR: unmatched braces while reading triTable.")

    block = "".join(content)

    # parse rows: each row is { ... }
    rows = []
    j = 0
    while True:
        mrow = re.search(r"\{([^{}]*)\}", block[j:])
        if not mrow:
            break
        row_txt = mrow.group(1)
        j += mrow.end()
        # collect integers (including negatives, hex not expected here)
        nums = [int(x) for x in re.findall(r"-?\d+", row_txt)]
        rows.append(nums)

    # Filter out any accidental short/empty matches
    rows = [r for r in rows if len(r) > 0]

    if len(rows) != 256:
        sys.exit(f"ERROR: expected 256 rows, got {len(rows)}.")

    for idx, r in enumerate(rows):
        if len(r) != 16:
            sys.exit(f"ERROR: row {idx} has {len(r)} entries (expected 16).")

    return rows

def format_int4_row(row16):
    assert len(row16) == 16
    chunks = [row16[i:i+4] for i in range(0, 16, 4)]
    parts = [f"int4({c[0]},{c[1]},{c[2]},{c[3]})" for c in chunks]
    return "{ " + ", ".join(parts) + " }"

def generate_hlsl(rows):
    header = (
        "// AUTO-GENERATED: packed triTable -> int4 triTable4[256][4]\n"
        "// Each 16-entry row packed into four int4 to reduce cbuffer size.\n\n"
        "static const int4 triTable4[256][4] = {\n"
    )
    body_lines = []
    for r in rows:
        body_lines.append("    " + format_int4_row(r))
    body = ",\n".join(body_lines) + "\n};\n\n"

    helper = (
        "int GetTri(uint cube, uint k) {\n"
        "    // k in [0..15]\n"
        "    int4 v = triTable4[cube][k >> 2];\n"
        "    uint c = k & 3u;\n"
        "    return (c==0u) ? v.x : (c==1u) ? v.y : (c==2u) ? v.z : v.w;\n"
        "}\n"
    )

    return header + body + helper

def main():
    if len(sys.argv) < 2:
        print("Usage: python pack_tri_table.py MarchingCubesTables.hlsl > triTable_packed.hlsl")
        sys.exit(1)

    src = Path(sys.argv[1]).read_text(encoding="utf-8", errors="ignore")
    src_nc = remove_comments(src)
    rows = extract_tritable(src_nc)
    out = generate_hlsl(rows)
    sys.stdout.write(out)

if __name__ == "__main__":
    main()
