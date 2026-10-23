#!/usr/bin/env python3
"""Quick STL check: triangles, bounding box, closed and manifold (every edge shared by exactly two triangles)."""
import struct, sys
from collections import Counter
def load(p):
    b = open(p, 'rb').read()
    if b[:5] == b'solid' and b'facet' in b[:300]:
        v = [tuple(map(float, l.split()[1:4])) for l in b.decode().splitlines() if l.strip().startswith('vertex')]
        return [v[i:i + 3] for i in range(0, len(v), 3)]
    n = struct.unpack('<I', b[80:84])[0]
    return [[struct.unpack('<3f', b[84 + 50 * i + 12 + 12 * k:84 + 50 * i + 24 + 12 * k]) for k in range(3)] for i in range(n)]
for p in sys.argv[1:]:
    T = load(p); E = Counter()
    key = lambda v: tuple(round(c, 4) for c in v)
    for t in T:
        a, b, c = map(key, t)
        for x, y in ((a, b), (b, c), (c, a)): E[(min(x, y), max(x, y))] += 1
    bad = sum(1 for k, n in E.items() if n != 2)
    xs = [v[0] for t in T for v in t]; ys = [v[1] for t in T for v in t]; zs = [v[2] for t in T for v in t]
    print(f'{p}: {len(T)} tris, bbox x {min(xs):.2f}..{max(xs):.2f} y {min(ys):.2f}..{max(ys):.2f} z {min(zs):.2f}..{max(zs):.2f}, '
          f'size {max(xs)-min(xs):.1f} x {max(ys)-min(ys):.1f} x {max(zs)-min(zs):.1f}, non-manifold edges {bad}')
