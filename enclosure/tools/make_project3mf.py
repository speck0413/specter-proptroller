#!/usr/bin/env python3
"""Bambu Studio project files, one per version of the case, each part on its build plate, all on ASA:

  specter-case-inset.3mf (the default) / -inline / -screws / -glue    the standard case: the faceplates' mount
  specter-case-ac-inset.3mf / -ac-screws / -ac-glue                   the AC case (README "AC case (advanced)", rev B only)

  standard case: two plates (fewer plate changes)
    plate 1  Tub + back plate   (the back plate's labels = filament 2; inline: the tub alone)
    plate 2  Lid + front plate  (the lid's title and the plate's labels = filament 2; inline: the lid alone)
  AC case: four plates (its tub is too big to share one)
    plate 1  Faceplates     (labels = filament 2)
    plate 2  Tub
    plate 3  Lid            (title text = filament 2)
    plate 4  the mains-bay cover (its warning = filament 2)

The layout and the process settings are the owner's, as saved from Bambu Studio on 2026-10-21 17:54 (bambu/project_settings.config:
X1C 0.4, 0.20 mm Standard, textured PEI, 2 walls, 15 % infill, the owner's project filament "Generic ASA - 105c plate" (Generic
ASA, bed 105 C, nozzle 260 C: bambu/filament_settings_1.config) on both ASA slots, body and text, TPU 95A on the third, the
exhaust fan 10 % while printing, no brim (owner 2026-10-22), a 2-minute heat soak, the prime tower on with flushing into infill; prices zeroed). Each part's
footprint is centred where the owner put it, at the owner's turn (LAYOUT: x, y[, degrees]), and each plate's prime tower
where the owner put it (TOWER), or in the first free spot when the owner placed none. Built from the STLs in stl/
(tools/print_build.sh makes them). Parts and their text are one object with two parts, the text on filament 2, so the AMS
colours it. The fit coupons (stl/coupon_*.stl) are not in the projects, nor the screwed plates' TPU gaskets: those are
stl/gaskets.3mf and stl/gaskets_ac.3mf, on TPU 95A alone (bambu/tpu95a.json: Bambu TPU 95A HF @BBL X1C, from the
external spool, not the AMS). Each part with text also gets its own project,
stl/<part>.3mf, centred on one plate.

The second filament is a copy of the first (ASA) in TEXT_COLOUR, since a part set to a filament the project lacks falls back
to filament 1 and the text would print in the body's colour. Pick the real second filament in Studio (or sync the AMS)
before slicing. What a project can't set: close the printer's door and top lid for ASA (the X1C has no chamber heater).
"""
import json, math, os, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make3mf import tris, CT, RELS

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STL = os.path.join(ROOT, 'stl')
BED, STRIDE = 256.0, 256.0 * 1.2       # Bambu Studio lays plates out on a grid 1.2 bed widths apart

SETTINGS = os.path.join(ROOT, 'bambu', 'project_settings.config')
# footprint centres on each plate, from the owner's saved arrangement (plate_1.json / plate_2.json of that save)
LAYOUT = {'Tub + back plate': [(103.4, 26.0), (128.0, 150.0)],
          'Lid + front plate': [(108.3, 26.0), (128.0, 150.0)],
          'Faceplates AC': [(128.0, 60.0), (128.0, 132.0)],
          'Gaskets (TPU 95A)': [(128.0, 128.0), (128.0, 100.0), (128.0, 160.0)],
          'Gaskets (TPU 95A) AC': [(128.0, 128.0), (128.0, 95.0), (128.0, 165.0)],
          'Tub': [(135.2, 123.1)], 'Lid': [(128.0, 128.0)], 'Cover': [(128.0, 128.0)]}
# the prime tower's corner (wipe_tower_x/y) per plate, as the owner placed it; TOWER_BOX: its footprint from that corner,
# brim included, as Bambu Studio sliced it (plate_1.json of the save: 31 x 30.5 mm, starting 3.2 mm before the corner)
TOWER = {'Tub + back plate': (215.8, 18.0), 'Lid + front plate': (217.0, 18.0)}
TOWER_BOX = (-3.2, -3.2, 27.8, 27.3)
TEXT_COLOUR = '#FFFFFF'
TPU_COLOUR = '#202020'
RING = {'gasket_lid': 2.8}   # a ring's width (groove_w less 2 gasket_clr): parts may sit inside its hole, 2 mm off it
# the screw zones (stl/zones/<part>.stl, from the model's zones_tub / zones_lid): a modifier in the tub and the lid round
# every insert and screw, printed with these settings whatever the project's walls and infill are; and slower over the
# hinge gussets' and latch ears' 45-degree undersides (owner 2026-10-21: rough in ASA), which at 0.2 mm layers overhang
# about half a line width: the 25-50 % band, 50 mm/s in the profile
ZONE = {'wall_loops': '6', 'sparse_infill_density': '100%', 'enable_overhang_speed': '1', 'overhang_2_4_speed': '20', 'overhang_3_4_speed': '15'}
# painted brim ears (owner 2026-10-21: the tub lifted at its corners): radius and inset, one ear on each corner of what
# touches the bed (the footprint's point nearest each corner of its box, moved the inset towards the middle: the lid's
# box takes in its latch lugs, so its corners are 10 mm in), instead of a brim. Bambu Studio keeps them in Metadata/brim_ear_points.txt, per object (its place in
# the build, from 1) in the frame of the object's mesh centred on its box, as Bambu Studio saves it: so these objects
# are written that way (the mesh centred, the centre as the component's offset)
EARS = {}   # none (owner 2026-10-22: no brims now the recipe is good); it was {'tub': (10.0, 2.0), 'lid': (10.0, 2.0)}
def ears(main): return next((v for k, v in EARS.items() if main.startswith(k)), None)
# per-filament settings Bambu Studio keeps as lists: one entry per filament, or one per filament per extruder variant
# (filament_self_index says which filament each entry belongs to). Doubled for the second filament.
FIL_EXTRA = {'pressure_advance', 'enable_pressure_advance', 'default_filament_colour', 'enable_overhang_bridge_fan',
             'ironing_fan_speed', 'first_x_layer_part_fan_speed', 'overhang_threshold_participating_cooling',
             'activate_air_filtration', 'additional_cooling_fan_speed', 'chamber_temperatures', 'close_fan_the_first_x_layers',
             'complete_print_exhaust_fan_speed', 'cool_plate_temp', 'cool_plate_temp_initial_layer', 'during_print_exhaust_fan_speed',
             'eng_plate_temp', 'eng_plate_temp_initial_layer', 'fan_cooling_layer_time', 'fan_max_speed', 'fan_min_speed',
             'full_fan_speed_layer', 'hot_plate_temp', 'hot_plate_temp_initial_layer', 'nozzle_temperature',
             'nozzle_temperature_initial_layer', 'nozzle_temperature_range_high', 'nozzle_temperature_range_low',
             'overhang_fan_speed', 'overhang_fan_threshold', 'reduce_fan_stop_start_freq', 'required_nozzle_HRC',
             'slow_down_for_layer_cooling', 'slow_down_layer_time', 'slow_down_min_speed', 'supertack_plate_temp',
             'supertack_plate_temp_initial_layer', 'temperature_vitrification', 'textured_plate_temp',
             'textured_plate_temp_initial_layer', 'additional_fan_full_speed_layer', 'close_additional_fan_first_x_layers',
             'cooling_perimeter_transition_distance', 'cooling_slowdown_logic', 'pre_start_fan_time', 'impact_strength_z',
             'no_slow_down_for_cooling_on_outwalls', 'first_x_layer_fan_speed', 'volumetric_speed_coefficients',
             'long_retractions_when_ec', 'retraction_distances_when_ec', 'override_process_overhang_speed', 'flush_volumes_vector',
             'circle_compensation_speed', 'counter_coef_1', 'counter_coef_2', 'counter_coef_3', 'counter_limit_max',
             'counter_limit_min', 'diameter_limit', 'hole_coef_1', 'hole_coef_2', 'hole_coef_3', 'hole_limit_max', 'hole_limit_min'}

ASA = json.load(open(os.path.join(ROOT, 'bambu', 'asa.json')))['settings']
SAVED = json.load(open(SETTINGS))
SAVED_ASA = all(t == 'ASA' for t in SAVED['filament_type'] if t != 'TPU')   # the owner's save is on ASA already: taken as it is
SAVED_TPU = 'TPU' in SAVED['filament_type']   # and holds the TPU slot already
# the owner's project filament (2026-10-21: "Generic ASA - 110c plate", Generic ASA with a 110 C bed; 105 C from 2026-10-22:
# 110 made the bed too sticky and left white marks on the black ASA), saved in the project:
# Bambu Studio keeps it in Metadata/filament_settings_1.config, only what differs from the preset it inherits; without that
# file Studio swaps the slots back to a system preset on open
PROJECT_FIL = open(os.path.join(ROOT, 'bambu', 'filament_settings_1.config')).read()
BRIM_W = 0.0   # no brim on any part (owner 2026-10-22: the bed at 105 C, the soak and the exhaust low hold them)
LAYOUT_BAD = []   # every part on its plate, brim included, on the bed, out of the exclusion zone, clear of the others
PROCESS = {'brim_type': 'no_brim',   # no brim (owner 2026-10-22)
           'enable_prime_tower': '0'}   # the second colour is only in the bottom layers: purge in the chute, no tower to overlap a brim
ASA_PLUS = {'textured_plate_temp': ['105'], 'textured_plate_temp_initial_layer': ['105'], 'hot_plate_temp': ['105'],
            'hot_plate_temp_initial_layer': ['105'], 'eng_plate_temp': ['105'], 'eng_plate_temp_initial_layer': ['105'],
            'during_print_exhaust_fan_speed': ['10']}   # a hotter bed and the exhaust fan low keep the chamber warm (ASA warps)
BED_EXCLUDE = (18.0, 28.0)   # the X1C's front-left corner nothing may print in (bed_exclude_area 0..18 x 0..28)

def asa(d):   # the saved settings' filament switched to ASA: each list as long as the saved one (its extruder variants)
    if SAVED_ASA: return d
    for k, v in list(ASA.items()) + list(ASA_PLUS.items()):
        n = len(d[k]) if isinstance(d.get(k), list) else len(v)
        d[k] = (v + [v[-1]] * n)[:n]
    d.update(PROCESS)
    return d

TPU = json.load(open(os.path.join(ROOT, 'bambu', 'tpu95a.json')))['settings']
def tpu_only(cfg, towers, no_tower=(), tpu=False):   # the gasket projects: the saved process on TPU 95A, one filament; Arachne walls (the lip is one
    d = json.load(open(cfg))   # 0.45 mm line, which the classic wall generator may drop) and no brim (a TPU brim tears the ring)
    for k, v in TPU.items():
        n = len(d[k]) if isinstance(d.get(k), list) else len(v)
        d[k] = (v + [v[-1]] * n)[:n]
    d.update({'wall_generator': 'arachne', 'brim_type': 'no_brim'})
    if 'inherits_group' in d: d['inherits_group'] = [d['inherits_group'][0]] + [''] * (len(d['inherits_group']) - 2) + [d['inherits_group'][-1]]   # the TPU is a system preset: no project filament
    if 'different_settings_to_system' in d: v = d['different_settings_to_system']; d['different_settings_to_system'] = [v[0]] + [''] * (len(v) - 2) + [v[-1]]
    d['filament_colour'] = ['#202020'] * len(d['filament_colour'])
    return json.dumps(d, indent=4)

# the heat soak (owner 2026-10-22: 2 minutes; 10 with a 110 C bed made it too sticky): the X1C can't wait for a chamber
# temperature (no heater; M191 does nothing on it), so once the bed is hot an ASA plate holds SOAK_MIN minutes with the
# exhaust fan off; the nozzle stays at the start G-code's 75 C meanwhile (no ooze). A TPU plate skips it.
SOAK_MIN = 2
BED_WAIT = 'M190 S[bed_temperature_initial_layer_single] ;wait for bed temp'
SOAK = ('\n{if filament_type[initial_no_support_extruder]=="ASA"}\n;===== heat soak: the bed warms the closed chamber (ASA) =====\n'
        'M106 P3 S0 ; exhaust fan off\nM400\n' + ''.join(f'G4 S60 ; soak {i + 1}/{SOAK_MIN} min\n' for i in range(SOAK_MIN)) + '{endif}')
def soak(d):
    g = d['machine_start_gcode']
    assert g.count(BED_WAIT) == 1, 'the start G-code\'s bed wait moved: place the heat soak by hand'
    if ';===== heat soak' not in g: d['machine_start_gcode'] = g.replace(BED_WAIT, BED_WAIT + SOAK)
    return d

def chunks(d, k, nf):   # a per-filament setting's entries for each filament (one per extruder variant)
    v = d[k]; m = len(v) // nf
    return m if m * nf == len(v) else None

def add_tpu(d):   # a third filament, TPU 95A (bambu/tpu95a.json), after the two ASA slots: each per-filament list gets its entries
    nf = len(d['filament_settings_id'])
    for k, v in list(d.items()):
        if not (isinstance(v, list) and (k.startswith('filament_') or k in FIL_EXTRA)) or k in ('filament_self_index',): continue
        m = chunks(d, k, nf)
        if not m: continue
        t = TPU.get(k, v[:m]); d[k] = v + (t + [t[-1]] * m)[:m]
    m = chunks(d, 'filament_self_index', nf) or 1
    d['filament_self_index'] = d['filament_self_index'] + [str(nf + 1)] * len(d['filament_self_index'][:m])
    d['filament_colour'][-1] = TPU_COLOUR; d['filament_multi_colour'][-1] = TPU_COLOUR
    if 'different_settings_to_system' in d:   # [process, each filament, printer]
        v = d['different_settings_to_system']; d['different_settings_to_system'] = v[:-1] + [''] + v[-1:]
    f = d['flush_volumes_matrix']; n0 = int(len(f) ** 0.5); vol = max(int(x) for x in f)
    d['flush_volumes_matrix'] = [str(0 if i == j else vol) for i in range(n0 + 1) for j in range(n0 + 1)]
    return d

def two_filaments(cfg, towers, no_tower=(), tpu=False):   # the saved settings on ASA, filament 1 copied as filament 2, in TEXT_COLOUR (as saved, if they hold two already)
    d = soak(asa(json.load(open(cfg))))
    d['brim_type'] = 'no_brim'
    d['wipe_tower_x'] = [f'{t[0]:g}' for t in towers]; d['wipe_tower_y'] = [f'{t[1]:g}' for t in towers]   # one per plate
    if no_tower: d['enable_prime_tower'] = '0'   # a plate too full for the tower: the nozzle purges in the chute instead
    if len(d['filament_settings_id']) > 1: return json.dumps(add_tpu(d) if tpu and not SAVED_TPU else d, indent=4)
    for k, v in d.items():
        if isinstance(v, list) and (k.startswith('filament_') or k in FIL_EXTRA): d[k] = v + v
    d['filament_self_index'] = [str(int(i) + (j >= len(d['filament_self_index']) // 2)) for j, i in enumerate(d['filament_self_index'])]
    d['filament_colour'] = d['filament_colour'][:1] + [TEXT_COLOUR]
    d['filament_multi_colour'] = d['filament_multi_colour'][:1] + [TEXT_COLOUR]
    f = d['flush_volumes_matrix'][0]; n = str(sum(int(x) for x in d['flush_volumes_vector'][:2]))
    d['flush_volumes_matrix'] = [f, n, n, f]
    return json.dumps(add_tpu(d) if tpu else d, indent=4)

GASKET_META = '<metadata key="brim_type" value="no_brim"/><metadata key="wall_generator" value="arachne"/>'   # a TPU brim tears the ring; the lip is one 0.45 mm line, which the classic wall generator may drop
OBJ_META = {}
BRIM = {}

GASKET_BACK = {False: 'gasket_back', True: 'gasket_back_ac' if 'gasket_back_ac' in json.load(open(os.path.join(STL, 'gaskets.json'))) else 'gasket_back'}
def variant(name):   # name: [ac-]inset|inline|screws|glue
    ac, m = name.startswith('ac'), name.split('-')[-1]
    k = '_ac' if ac else ''
    # the TPU gaskets' plate (filament 3, TPU 95A): the lid's ring with the screwed plates' gaskets inside it
    tpu = ('Gaskets (TPU 95A)' + (' AC' if ac else ''), [(f'gasket_lid{k}', [])] + ([(f'gasket_front{k}', []), (GASKET_BACK[ac], [])] if m in ('screws', 'inset') else []))
    if not ac and m == 'inline': return [('Tub', [(f'tub_{m}', [])]), ('Lid', [('lid', ['lid_text'])]), tpu]
    if not ac: return [
        ('Tub + back plate', [(f'plate_back_{m}', [f'plate_back_text_{m}']), (f'tub_{m}', [])]),
        ('Lid + front plate', [(f'plate_front_{m}', [f'plate_front_text_{m}']), ('lid', ['lid_text'])]),
        tpu,
    ]
    out = [
        ('Faceplates', [(f'plate_front{k}_{m}', [f'plate_front_text{k}_{m}']), (f'plate_back{k}_{m}', [f'plate_back_text{k}_{m}'])]),
        ('Tub', [(f'tub{k}_{m}', [])]),
        ('Lid', [(f'lid{k}', [f'lid_text{k}'])]),
    ]
    if ac: out.append(('Cover', [('cover_ac', ['cover_text_ac'])]))
    out.append(tpu)
    return out
VARIANTS = [f'{c}{m}' for c in ('', 'ac-') for m in ('inset', 'inline', 'screws', 'glue') if not (c and m == 'inline')]

def footprint(T):   # x/y box of what touches the bed (the slicer's plate layout centres on this)
    z0 = min(v[2] for t in T for v in t)
    vs = [v for t in T for v in t if v[2] < z0 + 0.3]
    return min(v[0] for v in vs), min(v[1] for v in vs), max(v[0] for v in vs), max(v[1] for v in vs)

def bbox(T):
    xs = [v[0] for t in T for v in t]; ys = [v[1] for t in T for v in t]; zs = [v[2] for t in T for v in t]
    return min(xs), min(ys), min(zs), max(xs), max(ys), max(zs)

def bbox_below(T, z):   # x/y box of the part's triangles that reach below height z
    vs = [v for t in T if min(w[2] for w in t) < z for v in t]
    return min(v[0] for v in vs), min(v[1] for v in vs), max(v[0] for v in vs), max(v[1] for v in vs)

def mesh_xml(T):
    idx, vs, ts = {}, [], []
    for t in T:
        ids = []
        for v in t:
            k = tuple(round(c, 4) for c in v)
            if k not in idx: idx[k] = len(vs); vs.append(k)
            ids.append(idx[k])
        if len(set(ids)) == 3: ts.append(ids)
    return ('<mesh><vertices>' + ''.join(f'<vertex x="{x}" y="{y}" z="{z}"/>' for x, y, z in vs) + '</vertices><triangles>'
            + ''.join(f'<triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in ts) + '</triangles></mesh>')

def build(name, plates=None, layout=LAYOUT, out=None, settings=None):
    plates = plates or variant(name)
    cols = math.ceil(math.sqrt(len(plates)))
    objs, items, cfg_objs, cfg_plates, ear_lines = [], [], [], [], []
    has_tpu = not settings and any(m_.startswith('gasket') for _, ps in plates for m_, _ in ps)
    nid = 1; cache = {}
    def load(p, rot=0):   # the part's triangles, turned rot degrees about z (baked into the mesh)
        if (p, rot) not in cache:
            f = os.path.join(STL, p + '.stl')
            if not os.path.exists(f): sys.exit(f'missing {f}: run tools/print_build.sh')
            c, s_ = round(math.cos(math.radians(rot)), 12), round(math.sin(math.radians(rot)), 12)
            cache[(p, rot)] = [[(x * c - y * s_, x * s_ + y * c, z) for x, y, z in t] for t in tris(f)] if rot else tris(f)
        return cache[(p, rot)]
    towers, no_tower = [], []   # no_tower: plates with a second colour and no room for the tower
    for pi, (pname, parts) in enumerate(plates):
        cx, cy = (pi % cols) * STRIDE + BED / 2, -(pi // cols) * STRIDE + BED / 2
        rots = [(u[2] if len(u) > 2 else 0) for u in layout[pname]]
        boxes = []
        for (main, extra), rot in zip(parts, rots):
            T = load(main, rot); b = bbox(T)
            for e in extra: eb = bbox(load(e, rot)); b = (min(b[0], eb[0]), min(b[1], eb[1]), min(b[2], eb[2]), max(b[3], eb[3]), max(b[4], eb[4]), max(b[5], eb[5]))
            boxes.append(b)
        inst = []; placed = []
        for (main, extra), b, u, rot in zip(parts, boxes, layout[pname], rots):
            ux, uy = u[0], u[1]
            f = footprint(load(main, rot))
            comp_ids = []
            zone = os.path.join('zones', main)
            ear = ears(main)
            mb = bbox(load(main, rot)); c0 = ((mb[0] + mb[3]) / 2, (mb[1] + mb[4]) / 2, (mb[2] + mb[5]) / 2) if ear else (0.0, 0.0, 0.0)
            for p in [main] + extra + ([zone] if os.path.exists(os.path.join(STL, zone + '.stl')) else []):
                T = [[(v[0] - c0[0], v[1] - c0[1], v[2] - c0[2]) for v in t] for t in load(p, rot)] if ear else load(p, rot)
                objs.append(f'<object id="{nid}" name="{p}" type="model">{mesh_xml(T)}</object>'); comp_ids.append((nid, p)); nid += 1
            oid = nid; nid += 1
            off = f' transform="1 0 0 0 1 0 0 0 1 {c0[0]:.4f} {c0[1]:.4f} {c0[2]:.4f}"' if ear else ''
            objs.append(f'<object id="{oid}" name="{main}" type="model"><components>' + ''.join(f'<component objectid="{c}"{off}/>' for c, _ in comp_ids) + '</components></object>')
            if ear:   # an ear on each corner of the footprint, inset onto the part (the corners are rounded)
                r, i = ear; z = mb[2] - c0[2]
                fv = [v for t in load(main, rot) for v in t if v[2] < mb[2] + 0.01]; mx, my = (f[0] + f[2]) / 2, (f[1] + f[3]) / 2
                pts = []
                for kx, ky in ((f[0], f[1]), (f[2], f[1]), (f[2], f[3]), (f[0], f[3])):
                    v = min(fv, key=lambda v: (v[0] - kx) ** 2 + (v[1] - ky) ** 2); d = math.hypot(mx - v[0], my - v[1])
                    pts.append((v[0] + i * (mx - v[0]) / d, v[1] + i * (my - v[1]) / d))
                ear_lines.append(f'object_id={len(items) + 1}|' + ''.join(f'{x - c0[0]:f} {y - c0[1]:f} {z:f} {r:f} 0 ' for x, y in pts))
            tx, ty, tz = cx - BED / 2 + ux - (f[0] + f[2]) / 2, cy - BED / 2 + uy - (f[1] + f[3]) / 2, -b[2]
            gk = main.startswith('gasket')
            br = 0.0 if settings or ear or gk else BRIM.get(main, BRIM_W)   # the ASA brim round what touches the bed (the TPU gaskets have none)
            ox, oy = tx - (cx - BED / 2), ty - (cy - BED / 2)
            fb = (f[0] + ox - br, f[1] + oy - br, f[2] + ox + br, f[3] + oy + br)
            if ear: fb = (min([fb[0]] + [x - ear[0] + ox for x, _ in pts]), min([fb[1]] + [y - ear[0] + oy for _, y in pts]),
                          max([fb[2]] + [x + ear[0] + ox for x, _ in pts]), max([fb[3]] + [y + ear[0] + oy for _, y in pts]))   # the ears' discs
            bb = (b[0] + ox, b[1] + oy, b[3] + ox, b[4] + oy)
            if not (0 <= fb[0] and 0 <= fb[1] and fb[2] <= BED and fb[3] <= BED and 0 <= bb[0] and 0 <= bb[1] and bb[2] <= BED and bb[3] <= BED):
                LAYOUT_BAD.append(f'{name or main} plate {pi + 1}: {main} (with its brim) off the bed: {[round(v, 1) for v in fb]}')
            if any(r[0] < BED_EXCLUDE[0] and r[1] < BED_EXCLUDE[1] for r in (fb, bb)):
                LAYOUT_BAD.append(f'{name or main} plate {pi + 1}: {main} in the front-left exclusion zone: {[round(v, 1) for v in fb]}')
            def nested(a_, an, c_, cn):   # c_ inside ring a_'s hole, 2 mm off it
                w = RING.get(next((k for k in RING if an.startswith(k)), None))
                return w is not None and a_[0] + w + 2 <= c_[0] and a_[1] + w + 2 <= c_[1] and c_[2] <= a_[2] - w - 2 and c_[3] <= a_[3] - w - 2
            for q, qb, _, qT, qo in placed:
                if nested(qb, q, bb, main) or nested(bb, main, qb, q): continue   # the parts themselves 2 mm apart or more where they share a height (brims may meet: the slicer merges them)
                zc = min(b[5] - b[2], max(w[2] for t in qT for w in t) - min(w[2] for t in qT for w in t)) + 1
                a = bbox_below([[(w[0], w[1], w[2] - b[2]) for w in t] for t in load(main, rot)], zc); c = bbox_below(qT, zc + min(w[2] for t in qT for w in t))
                a = (a[0] + ox, a[1] + oy, a[2] + ox, a[3] + oy); c = (c[0] + qo[0], c[1] + qo[1], c[2] + qo[0], c[3] + qo[1])
                if a[0] - 2 < c[2] and c[0] - 2 < a[2] and a[1] - 2 < c[3] and c[1] - 2 < a[3]:
                    LAYOUT_BAD.append(f'{name or main} plate {pi + 1}: {main} and {q} closer than 2 mm')
            placed.append((main, bb, fb, load(main, rot), (ox, oy)))
            items.append(f'<item objectid="{oid}" transform="1 0 0 0 1 0 0 0 1 {tx:.4f} {ty:.4f} {tz:.4f}" printable="1"/>')
            cfg_objs.append(f'<object id="{oid}"><metadata key="name" value="{main}"/>{OBJ_META.get(main, "")}{'<metadata key="brim_type" value="brim_ears"/>' if ear else ''}{GASKET_META if gk and not settings else ''}<metadata key="extruder" value="{3 if gk and not settings else 1}"/>'
                            + ''.join(f'<part id="{c}" subtype="modifier_part"><metadata key="name" value="screw zones"/>'
                                      + ''.join(f'<metadata key="{k}" value="{v}"/>' for k, v in ZONE.items()) + '</part>'
                                      if p.startswith('zones') else
                                      f'<part id="{c}" subtype="normal_part"><metadata key="name" value="{p}"/>'
                                      f'<metadata key="extruder" value="{2 if "_text" in p else 3 if gk and not settings else 1}"/></part>' for c, p in comp_ids)
                            + '</object>')
            inst.append(oid)
        two = any(extra for _, extra in parts)   # a second colour on this plate: the prime tower prints
        def clear(t):   # the tower's box on the bed, out of the exclusion zone, 1 mm off every part's brim
            tb = (t[0] + TOWER_BOX[0], t[1] + TOWER_BOX[1], t[0] + TOWER_BOX[2], t[1] + TOWER_BOX[3])
            return (0 <= tb[0] and 0 <= tb[1] and tb[2] <= BED and tb[3] <= BED and not (tb[0] < BED_EXCLUDE[0] and tb[1] < BED_EXCLUDE[1])
                    and all(tb[2] + 1 <= r[0] or r[2] + 1 <= tb[0] or tb[3] + 1 <= r[1] or r[3] + 1 <= tb[1] for _, _, r, _, _ in placed))
        t = layout.get(pname + ' tower', TOWER.get(pname))   # a layout may move a plate's tower
        if t is None:   # the first free spot, front right first (1 mm steps)
            t = next((c for c in ((x - TOWER_BOX[0], y - TOWER_BOX[1]) for y in range(0, 230) for x in range(225, 0, -1)) if clear(c)), None)
            if t is None and two: no_tower.append(pi + 1)
            t = t or (165.0, 216.97)
        elif two and not clear(t): LAYOUT_BAD.append(f'{name} plate {pi + 1}: the prime tower at {t} on a part or off the bed')
        towers.append(t)
        cfg_plates.append(f'<plate><metadata key="plater_id" value="{pi + 1}"/><metadata key="plater_name" value="{pname}"/><metadata key="locked" value="false"/>'
                          + ''.join(f'<model_instance><metadata key="object_id" value="{o}"/><metadata key="instance_id" value="0"/></model_instance>' for o in inst)
                          + '</plate>')
    model = ('<?xml version="1.0" encoding="UTF-8"?><model unit="millimeter" xml:lang="en-US" '
             'xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">'
             '<metadata name="Application">BambuStudio-02.08.02.61</metadata><metadata name="BambuStudio:3mfVersion">1</metadata>'
             f'<metadata name="Title">specter-case{"-" + name if name else ""}</metadata>'
             '<resources>' + ''.join(objs) + '</resources><build>' + ''.join(items) + '</build></model>')
    cfg = '<?xml version="1.0" encoding="UTF-8"?><config>' + ''.join(cfg_objs) + ''.join(cfg_plates) + '</config>'
    out = out or os.path.join(ROOT, 'specter-case' + (f'-{name}' if name else '') + '.3mf')
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('[Content_Types].xml', CT); z.writestr('_rels/.rels', RELS)
        z.writestr('3D/3dmodel.model', model); z.writestr('Metadata/model_settings.config', cfg)
        if ear_lines: z.writestr('Metadata/brim_ear_points.txt', 'brim_points_format_version=1\n' + ''.join(l + '\n' for l in ear_lines))
        z.writestr('Metadata/project_settings.config', (settings or two_filaments)(SETTINGS, towers, no_tower, has_tpu))
        if not settings: z.writestr('Metadata/filament_settings_1.config', PROJECT_FIL)   # the ASA slots' project filament
    print(out, f'{len(plates)} plates' + (' (gaskets on filament 3, TPU 95A)' if has_tpu else '') + (f' (no room for the prime tower on plate {no_tower}: tower off, purged in the chute)' if no_tower and not settings else ''))

if __name__ == '__main__':
    # --usb _usb (or _nousb): also stl/plate_back_usb.3mf, the back plate with the USB panel port the other way round
    u = sys.argv[sys.argv.index('--usb') + 1] if '--usb' in sys.argv else None
    for v in VARIANTS: build(v, layout=dict(LAYOUT, Tub=[(128.0, 141.5)], Lid=[(128.0, 141.9)], Faceplates=LAYOUT['Faceplates AC']) if v.startswith('ac') else dict(LAYOUT, Tub=[(128.0, 128.0)], Lid=[(128.0, 128.0)]))   # the AC tub: its front-left corner clear of the exclusion zone
    # stl/<part>.3mf: each part with its text alone, as a project on the same settings (the text already on filament 2)
    SINGLE = {f'plate_{f}{k}_{m}': [f'plate_{f}_text{k}_{m}'] for f in ('front', 'back') for k in ('', '_ac') for m in ('inset', 'screws', 'glue')}
    SINGLE.update({f'lid{k}': [f'lid_text{k}'] for k in ('', '_ac')})
    SINGLE['cover_ac'] = ['cover_text_ac']
    if u: SINGLE[f'plate_back{u}'] = [f'plate_back_text{u}']
    for main, extra in SINGLE.items():
        build(main, [(main, [(main, extra)])], {main: [(BED / 2, 141.9 if main.startswith('lid_ac') else BED / 2)]}, os.path.join(STL, main + '.3mf'))
    # the screwed plates' printed gaskets, TPU 95A on their own (not in the ASA projects): stl/gaskets.3mf for the standard
    # case, stl/gaskets_ac.3mf for the AC case (the build lists in stl/gaskets.json which gaskets differ between cases)
    GK = json.load(open(os.path.join(STL, 'gaskets.json')))
    for out, ring, front, back, pl in (('gaskets', 'gasket_lid', 'gasket_front', 'gasket_back', 'Gaskets (TPU 95A)'), ('gaskets_ac', 'gasket_lid_ac', 'gasket_front_ac', GASKET_BACK[True], 'Gaskets (TPU 95A) AC')):
        build(out, [(pl, [(ring, []), (front, []), (back, [])])], {pl: LAYOUT[pl]},
              os.path.join(STL, out + '.3mf'), settings=tpu_only)
    for g in GK:   # each gasket alone too
        build(g, [('Gasket (TPU 95A)', [(g, [])])], {'Gasket (TPU 95A)': [(BED / 2, BED / 2)]}, os.path.join(STL, g + '.3mf'), settings=tpu_only)
    # the fit coupons, on ASA: print them first
    build('coupons', [('Coupons', [('coupon_seal', []), ('coupon_mount_inset', []), ('coupon_mount_screws', []), ('coupon_mount_glue', [])])],
          {'Coupons': [(70.0, 80.0), (180.0, 80.0), (70.0, 185.0), (180.0, 185.0)]}, os.path.join(STL, 'coupons.3mf'))
    # the draw latch's fit test (owner 2026-10-22): a piece of the left wall and that end of the lid, a lever, its two hook
    # plates and its two pins
    build('coupon_latch', [('Draw latch coupon', [('coupon_latch_wall', []), ('coupon_latch_lid', []), ('dl_lever', []), ('dl_hook', []), ('dl_hook', []), ('dl_pin1', []), ('dl_pin2', [])])],
          {'Draw latch coupon': [(60.0, 80.0), (130.0, 80.0), (200.0, 80.0), (60.0, 170.0), (100.0, 170.0), (150.0, 160.0), (150.0, 185.0)]}, os.path.join(STL, 'coupon_latch.3mf'))
    if LAYOUT_BAD:
        for l in LAYOUT_BAD: print('layout:', l)
        sys.exit(1)
    print('layouts: every part (and brim) on the bed, out of the exclusion zone, clear of the others')
