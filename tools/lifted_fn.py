"""Print the lifted C function that contains each given main-module offset.

The log lines of the native Options hooks give return addresses as main+0xOFFSET. A return address is inside a
function, not at its entry, so this finds the function f_70xxxxxxxx whose entry is the closest one at or below the
address and prints its whole body. Reads generated/v143/main/*.c (ignored, game-derived); writes to stdout only,
so redirect to a file under build/ if you want to send it somewhere.

Usage: lifted_fn.py [--dir generated/v143/main] OFFSET [OFFSET...]    (hex, with or without 0x)
"""
import argparse
import re
import sys
from pathlib import Path

BASE = 0x7000000000
DEF = re.compile(r'^void f_([0-9a-f]+)\(Cpu \*c\) \{')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default='generated/v143/main')
    ap.add_argument('offsets', nargs='+')
    a = ap.parse_args()
    root = Path(a.dir)
    if not root.is_dir():
        sys.exit(f'{root}: no such folder (run from the repo root after the generator ran)')
    funcs = []   # (entry offset, file, line index)
    cache = {}
    for p in sorted(root.glob('*.c')):
        lines = p.read_text(encoding='utf-8', errors='replace').split('\n')
        cache[p] = lines
        for i, ln in enumerate(lines):
            m = DEF.match(ln)
            if m:
                funcs.append((int(m.group(1), 16) - BASE, p, i))
    funcs.sort(key=lambda t: t[0])
    for s in a.offsets:
        off = int(s, 16)
        best = None
        for f in funcs:
            if f[0] <= off:
                best = f
            else:
                break
        if best is None:
            print(f'// main+0x{off:x}: no function at or below it')
            continue
        entry, p, i = best
        lines = cache[p]
        j = i
        while j < len(lines) and lines[j] != '}':
            j += 1
        print(f'// main+0x{off:x} is inside f_{BASE + entry:x} (entry main+0x{entry:x}, +0x{off - entry:x}) in {p.name}')
        print('\n'.join(lines[i:j + 1]))
        print()


if __name__ == '__main__':
    main()
