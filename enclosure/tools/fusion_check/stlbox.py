import sys, struct, numpy as np
def load(path):
    b = open(path, 'rb').read()
    if b[:5] == b'solid' and b'facet' in b[:400]:
        v = [[float(s[1]), float(s[2]), float(s[3])] for s in (l.split() for l in b.decode().splitlines()) if s and s[0] == 'vertex']
        return np.array(v).reshape(-1, 3, 3)
    n = struct.unpack('<I', b[80:84])[0]
    a = np.frombuffer(b[84:84 + n * 50], dtype=np.dtype([('n', '<f4', 3), ('v', '<f4', (3, 3)), ('a', '<u2')]))
    return a['v'].astype(float)
if __name__ == '__main__':
    for f in sys.argv[1:]:
        t = load(f); v = np.einsum('ij,ij->i', t[:, 0], np.cross(t[:, 1], t[:, 2])).sum() / 6
        print(f, 'min', t.reshape(-1, 3).min(0).round(3), 'max', t.reshape(-1, 3).max(0).round(3), 'vol', round(abs(v), 2), 'tris', len(t))
