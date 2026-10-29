"""Move the first latch occurrence's STL (left wall, front) back into the latch's own frame: x along the wall, y out."""
import json, os, struct, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stlbox import load

P = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'params.json')))['P']
L = (P['ix1'] - P['ix0']) / 2 + P['wall']                       # the end wall's outside face, from the centre
Y = abs(P['latches'][0][1] - (P['iyf'] + P['iyb']) / 2)         # the latch's centre, either side of the centreline
t = load(sys.argv[1])
w = np.stack([t[:, :, 1] + Y, -L - t[:, :, 0], t[:, :, 2]], axis=2)
with open(sys.argv[2], 'wb') as f:
    f.write(b'\0' * 80 + struct.pack('<I', len(w)))
    for tri in w:
        f.write(struct.pack('<12fH', 0, 0, 0, *tri.reshape(-1), 0))
