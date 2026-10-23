#!/usr/bin/env python3
"""Join OpenSCAD STL parts into one glTF binary for the wiring guides' scene.

Usage: stl2glb.py out.glb name=file.stl[:#rrggbb] ...
Axes: OpenSCAD case frame (x right, y back, z up) -> three.js (x, z, -y), millimetres, so a board point
(x mm from the left edge, y mm from the back edge) sits at three.js (x, height, y).
Positions only (unindexed); gltf-transform welds, simplifies and compresses afterwards.
"""
import json, struct, sys
import numpy as np

def read_stl(path):
    data = open(path, 'rb').read()
    if data[:5] == b'solid' and b'facet' in data[:300]:
        v = [list(map(float, l.split()[1:4])) for l in data.decode().splitlines() if l.strip().startswith('vertex')]
        return np.array(v, dtype=np.float32)
    n = struct.unpack('<I', data[80:84])[0]
    a = np.frombuffer(data[84:84 + n * 50], dtype=np.dtype([('n', '<f4', 3), ('v', '<f4', (3, 3)), ('a', '<u2')]))
    return a['v'].reshape(-1, 3).astype(np.float32)

out, specs = sys.argv[1], sys.argv[2:]
gl = {"asset": {"version": "2.0", "generator": "stl2glb.py"}, "scene": 0, "scenes": [{"nodes": []}], "nodes": [],
      "meshes": [], "materials": [], "accessors": [], "bufferViews": [], "buffers": []}
blob = b''
for i, s in enumerate(specs):
    name, rest = s.split('=', 1)
    path, _, col = rest.partition(':')
    v = read_stl(path)
    v = np.stack([v[:, 0], v[:, 2], -v[:, 1]], axis=1).astype(np.float32)
    raw = v.tobytes()
    gl["bufferViews"].append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(raw), "target": 34962})
    blob += raw + b'\0' * (-len(raw) % 4)
    gl["accessors"].append({"bufferView": i, "componentType": 5126, "count": len(v), "type": "VEC3",
                            "min": v.min(0).tolist(), "max": v.max(0).tolist()})
    c = col.lstrip('#') or 'cccccc'
    rgb = [int(c[k:k + 2], 16) / 255 for k in (0, 2, 4)]
    gl["materials"].append({"name": name, "pbrMetallicRoughness": {"baseColorFactor": rgb + [1], "metallicFactor": 0, "roughnessFactor": 0.8}})
    gl["meshes"].append({"name": name, "primitives": [{"attributes": {"POSITION": i}, "material": i}]})
    gl["nodes"].append({"name": name, "mesh": i})
    gl["scenes"][0]["nodes"].append(i)
gl["buffers"].append({"byteLength": len(blob)})
js = json.dumps(gl, separators=(',', ':')).encode()
js += b' ' * (-len(js) % 4)
with open(out, 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(blob)))
    f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js)
    f.write(struct.pack('<II', len(blob), 0x004E4942)); f.write(blob)
print(out, sum(a["count"] for a in gl["accessors"]) // 3, 'triangles')
