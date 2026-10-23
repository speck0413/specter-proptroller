#!/bin/sh
# Render every printed-enclosure part from proptroller-print.scad into stl/ (each in its print pose), check each STL
# (closed, manifold, lowest z = 0, and no layer printed on air: tools/overhangs.py, which fails the build), and pack the
# parts with their AMS text into Bambu Studio projects (tools/make_project3mf.py), all on ASA; the screwed plates' printed
# gaskets (stl/gasket_*.stl) go into their own TPU 95A projects, stl/gaskets.3mf and stl/gaskets_ac.3mf.
#
# Every combination is built, with its name spelled out:
#   case   standard (DC), or _ac (README "AC case (advanced)", rev B only)
#   hinge  printed pin, or _everbilt
#   mount  _screws (six M3s per plate into heat-set inserts: the default) or _glue
# e.g. stl/tub_screws.stl, stl/tub_ac_everbilt_glue.stl, stl/plate_front_ac_screws.3mf. The lids, latches and the AC
# cover follow case and hinge only. The USB panel port the other way round from design.toml ([connectors] USB enabled)
# gets stl/tub_usb.stl and stl/plate_back_usb.* (or *_nousb), with design.toml's mount and the pin hinge.
# Each case renders from its own params in stl/v/<dc|ac|usb>, next to a copy of the model, so no two renders share a
# params file; params.scad here follows design.toml, for opening the model in OpenSCAD.
set -e
cd "$(dirname "$0")/.."
python3 tools/print_params.py
MOUNT=$(python3 -c "import tomllib; print(tomllib.load(open('design.toml', 'rb'))['plates']['mount'])")
case "$MOUNT" in glue|screws) ;; *) echo "design.toml [plates] mount: \"glue\" or \"screws\", not \"$MOUNT\""; exit 1 ;; esac
USB=$(python3 -c "import tomllib; print('_nousb' if tomllib.load(open('design.toml', 'rb'))['connectors']['USB'].get('enabled', True) else '_usb')")
mkdir -p stl
rm -rf stl/zones; mkdir -p stl/zones   # the screw zones: slicer modifiers, not printed parts (not checked as prints)
rm -f stl/*.stl stl/*.3mf specter-case.3mf specter-case-everbilt.3mf specter-case-ac.3mf   # stale names from earlier builds
v() { mkdir -p "stl/v/$1"; python3 tools/print_params.py --variant "$1" --out "stl/v/$1" > /dev/null; cp proptroller-print.scad "stl/v/$1/"; }
v dc; v ac; v usb
# the renders, in parallel (a few less than the cores): name, part, case, then -D settings
python3 - "$USB" "$MOUNT" <<'EOF'
import subprocess, sys, os
from concurrent.futures import ThreadPoolExecutor
usb, mount = sys.argv[1], sys.argv[2]
J = []
def r(name, part, case, **d): J.append((name, part, case, d))
for c in ('dc', 'ac'):
    k = '_ac' if c == 'ac' else ''
    for h in ('pin', 'everbilt'):
        e = '_everbilt' if h == 'everbilt' else ''
        for m in ('screws', 'glue'): r(f'tub{k}{e}_{m}', 'tub', c, mount=m, hinge=h)
        r(f'lid{k}{e}', 'lid', c, hinge=h)
        for m in ('screws', 'glue'): r(f'zones/tub{k}{e}_{m}', 'zones_tub', c, mount=m, hinge=h)
        r(f'zones/lid{k}{e}', 'zones_lid', c, hinge=h)
    r(f'lid_text{k}', 'lid_text', c)
    for m in ('screws', 'glue'):
        for f in ('front', 'back'):
            r(f'plate_{f}{k}_{m}', f'plate_{f}', c, mount=m); r(f'plate_{f}_text{k}_{m}', f'plate_{f}_text', c, mount=m)
r('latch', 'latch', 'dc'); r('coupon_seal', 'coupon_seal', 'dc')
for p in ('coupon_latch_wall', 'coupon_latch_lid', 'dl_lever', 'dl_hook', 'dl_pin1', 'dl_pin2'): r(p, p, 'dc', latch_style='draw')   # the draw latch's fit test
for m in ('screws', 'glue'): r(f'coupon_mount_{m}', 'coupon_mount', 'dc', mount=m)
r('cover_ac', 'cover', 'ac'); r('cover_text_ac', 'cover_text', 'ac')
# the screwed plates' TPU gaskets: one per distinct seal (the same shape, shifted, is the same gasket)
import json
def seal(c, f):   # the seal's shape with its x measured from its left end
    w = json.load(open(f'stl/v/{c}/params.json'))['walls'][f]['seal']; x0 = w['rect'][0]
    r_ = w['rect']
    return json.dumps([[round(r_[0] - x0, 2), round(r_[1] - x0, 2), r_[2], r_[3]], [[round(d[0] - x0, 2), d[1], d[2]] for d in w['discs']],
                       [[round(q[0] - x0, 2), round(q[1] - x0, 2), q[2]] for q in w['cuts']]])
G = {'gasket_front': ('gasket_front', 'dc'), 'gasket_back': ('gasket_back', 'dc'), 'gasket_front_ac': ('gasket_front', 'ac'),
     'gasket_lid': ('gasket_lid', 'dc'), 'gasket_lid_ac': ('gasket_lid', 'ac')}
assert seal('dc', 'front') == seal('usb', 'front'), 'the standard front seal differs with the USB port: build a gasket for it'
if seal('ac', 'back') != seal('dc', 'back'): G['gasket_back_ac'] = ('gasket_back', 'ac')
if seal('usb', 'back') != seal('dc', 'back'): G[f'gasket_back{usb}'] = ('gasket_back', 'usb')
for n, (pt, c) in G.items(): r(n, pt, c)
open('stl/gaskets.json', 'w').write(json.dumps(sorted(G)))
for p in ('tub', 'plate_back', 'plate_back_text'): r(f'{p}{usb}', p, 'usb', mount=mount)
def run(j):
    name, part, case, d = j
    cmd = ['openscad', '-q', '-o', f'stl/{name}.stl', '-D', f'part="{part}"'] + [a for k, v in d.items() for a in ('-D', f'{k}="{v}"')]
    return name, subprocess.run(cmd + [f'stl/v/{case}/proptroller-print.scad'], capture_output=True, text=True)
with ThreadPoolExecutor(max(2, os.cpu_count() - 2)) as ex:
    bad = [(n, p.stderr[-500:]) for n, p in ex.map(run, J) if p.returncode or not os.path.exists(f'stl/{n}.stl')]
if bad:
    for n, err in bad: print('render failed:', n, err)
    sys.exit(1)
print(len(J), 'parts rendered')
EOF
python3 tools/stlcheck.py stl/*.stl
python3 tools/overhangs.py stl/*.stl
python3 tools/make_project3mf.py --usb "$USB"
