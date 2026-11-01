"""Summarize PANEL_PROBE lines from an instrumented eink-qemu UC8179 run.

The probe is inserted immediately before the display-refresh command consumes
the two controller RAM planes. Hashes cover all 600 addressed rows; class
counts cover the visible 800x480 pixels. This checks refresh sequencing, not
physical ink density.
"""

import argparse
import re
from pathlib import Path


LINE = re.compile(
    r"PANEL_PROBE refresh=(\d+) psr=([0-9a-f]+) partial=(\d) "
    r"old=([0-9a-f]+) new=([0-9a-f]+) class=(\d+),(\d+),(\d+),(\d+),(\d+)"
)


def summarize(path: Path, last: int) -> None:
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = LINE.search(line)
        if match:
            n, psr, partial, old, new, *counts = match.groups()
            rows.append((int(n), psr, int(partial), old, new, tuple(map(int, counts))))

    pairs = []
    for i, row in enumerate(rows[:-1]):
        following = rows[i + 1]
        if row[1:3] == ("3f", 1) and following[1:3] == ("3f", 0):
            pairs.append((row, following))
    chosen = pairs[-last:]
    print(f"{path}: {len(rows)} refreshes, {len(pairs)} precondition+gray pairs")
    if not chosen:
        return
    for pre, gray in chosen:
        print(
            f"  {pre[0]}->{gray[0]} base={pre[4]} "
            f"pre_classes={pre[5][:4]} gray_classes={gray[5][:4]}"
        )
    linked = sum(a[0][4] == b[0][3] for a, b in zip(chosen, chosen[1:]))
    print(f"  retained base before next page: {linked}/{max(0, len(chosen)-1)}")
    mean = tuple(round(sum(pair[1][5][i] for pair in chosen) / len(chosen)) for i in range(4))
    print(f"  mean gray classes (00,01,10,11): {mean}")


parser = argparse.ArgumentParser()
parser.add_argument("logs", type=Path, nargs="+")
parser.add_argument("--last", type=int, default=4)
args = parser.parse_args()
for log in args.logs:
    summarize(log, args.last)
