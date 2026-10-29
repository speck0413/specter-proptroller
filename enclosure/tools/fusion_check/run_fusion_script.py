"""Run a Fusion script under the SolidCraft-backed adsk stand-in (adsk/ here).

    python3 run_fusion_script.py ../../fusion/SpecterEnclosure/SpecterEnclosure.py <out_dir>
Writes out_dir/specter-enclosure.solidcraft and .step, design.json and one STL per body. ONLY=tub,lid,plate,latch builds a part
or two while working on them. About 17 minutes for the whole enclosure.
"""
import importlib.util, json, os, sys, time, traceback
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import adsk, adsk.fusion

script, out = sys.argv[1], os.path.abspath(sys.argv[2])
os.makedirs(out, exist_ok=True)
spec = importlib.util.spec_from_file_location('fusion_script', script)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
t0 = time.time()
mod.app.userInterface.messageBox = lambda t, *a: (_ for _ in ()).throw(RuntimeError(t))
only = os.environ.get('ONLY')
if only:   # build only some parts while iterating: ONLY=tub,lid
    keep = set(only.split(','))
    for part, fn in (('tub', 'build_tub'), ('lid', 'build_lid'), ('plate', 'build_plate'), ('latch', 'build_latch')):
        if part not in keep:
            setattr(mod, fn, lambda *a, **k: None)
try:
    mod.run(None)
finally:
    print(f'run: {time.time() - t0:.1f} s, {len(adsk.fusion.S.timeline) if adsk.fusion.S else 0} timeline items', flush=True)
adsk.fusion.S.design_name = 'Specter Proptroller enclosure'
adsk.fusion.finalize(os.path.join(out, 'design.json'))
sc = adsk.fusion.S.sc
sc.x('file.save', path=os.path.join(out, 'specter-enclosure.solidcraft'))
sc.x('file.export', path=os.path.join(out, 'specter-enclosure.step'))
m = sc.tool('measure', {})
for b in m['bodies']:
    sc.x('file.export', path=os.path.join(out, b['name'].replace(' ', '_') + '.stl'), bodies=[b['name']])
json.dump(m, open(os.path.join(out, 'measure.json'), 'w'), indent=1)
print('sketches on fixed face planes:', adsk.fusion.S.fixed_faces)
print('bodies:', [(b['name'], round(b['volume_mm3'], 1)) for b in m['bodies']])
sc.close()
