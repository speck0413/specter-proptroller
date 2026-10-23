#!/usr/bin/env python3
"""3MF helpers for tools/make_project3mf.py: read an STL, write a mesh, the package's fixed parts."""
import struct
def tris(p):
    b = open(p, 'rb').read()
    if b[:5] == b'solid' and b'facet' in b[:300]:
        v = [tuple(map(float, l.split()[1:4])) for l in b.decode().splitlines() if l.strip().startswith('vertex')]
        return [v[i:i + 3] for i in range(0, len(v), 3)]
    n = struct.unpack('<I', b[80:84])[0]
    return [[struct.unpack('<3f', b[84 + 50 * i + 12 + 12 * k:84 + 50 * i + 24 + 12 * k]) for k in range(3)] for i in range(n)]
def mesh(T):
    idx, vs, ts = {}, [], []
    for t in T:
        ids = []
        for v in t:
            k = tuple(round(c, 5) for c in v)
            if k not in idx: idx[k] = len(vs); vs.append(k)
            ids.append(idx[k])
        ts.append(ids)
    return ('<mesh><vertices>' + ''.join(f'<vertex x="{x}" y="{y}" z="{z}"/>' for x, y, z in vs) + '</vertices><triangles>'
            + ''.join(f'<triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in ts) + '</triangles></mesh>')
CT = ('<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
      '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
      '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
RELS = ('<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        '<Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')

