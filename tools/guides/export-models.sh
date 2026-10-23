#!/usr/bin/env bash
# Build the wiring guides' 3D assets from the design files (no network):
#   docs/wiring-guides/video/assets/board-top.webp  the board's top side (copper, mask, silkscreen) from KiCad
#   docs/wiring-guides/video/assets/case.glb        the standard case (tub, lid, plates, latches) from OpenSCAD,
#                                                    welded, simplified and meshopt-compressed
#   docs/wiring-guides/video/src/scene/board-data.json  connector pads and part boxes
# Needs kicad-cli (KiCad 10, with its pcbnew Python module), openscad, google-chrome (to rasterise the layer SVGs),
# python3 with Pillow and numpy, and `npm ci` done in docs/wiring-guides/video (for gltf-transform).
set -euo pipefail
R="$(cd "$(dirname "$0")/../.." && pwd)"
V="$R/docs/wiring-guides/video"
T="$(mktemp -d)"; trap 'rm -r "$T"' EXIT
PCB="$R/hardware/kicad/proptroller.kicad_pcb"
mkdir -p "$V/assets" "$T/svg" "$T/case"

# board texture: one black-on-white SVG per layer (each with the outline), rasterised at 16 px/mm, then composed
for L in F.Cu F.Mask F.Silkscreen Edge.Cuts; do
  kicad-cli pcb export svg --mode-single --black-and-white --page-size-mode 2 --exclude-drawing-sheet \
    --drill-shape-opt 2 -l "$L,Edge.Cuts" -o "$T/svg/$L.svg" "$PCB" >/dev/null
  echo "<html><body style='margin:0;background:#fff'><img src='$L.svg' style='width:2144px;height:1280px;display:block'></body></html>" > "$T/svg/$L.html"
  google-chrome --headless=new --disable-gpu --user-data-dir="$T/chrome" --hide-scrollbars \
    --screenshot="$T/svg/$L.png" --window-size=2144,1280 "file://$T/svg/$L.html" 2>/dev/null
done
python3 "$R/tools/guides/board_texture.py" "$T/svg" "$V/assets/board-top.webp"
python3 "$R/tools/guides/board_data.py" "$PCB" "$R/enclosure/board-parts.json" "$V/src/scene/board-data.json"

# case: the assembled pose of each part (OpenSCAD case frame), one STL each
E="$R/enclosure"
part() { printf 'include <%s/proptroller-print.scad>\n%s\n' "$E" "$2" > "$T/case/$1.scad"; }
part tub 'tub_done();'
part lid 'lid();'
part lidtext 'translate([0, 0, z_lt - inlay]) linear_extrude(inlay) lid_text2d();'
part plates 'for (f = ["front", "back"]) at_plate(f) plate(f);'
part platetext 'for (f = ["front", "back"]) at_plate(f) plate_text(f);'
part latches 'for (l = latches) at_wall(l[0], l[1]) rotate([90, 0, 90]) translate([0, 0, -latch_w / 2]) linear_extrude(latch_w) latch2d(); if (front_latch) at_wall("front", fl_x) rotate([90, 0, 90]) translate([0, 0, -fl_w / 2]) linear_extrude(fl_w) fl_bolt2d();'
for p in tub lid lidtext plates platetext latches; do
  (cd "$E" && openscad -q -o "$T/case/$p.stl" -D 'part="none"' "$T/case/$p.scad") &
done
wait
python3 "$R/tools/guides/stl2glb.py" "$T/case/raw.glb" tub="$T/case/tub.stl:#2b2f33" lid="$T/case/lid.stl:#cfd8dc" \
  lidtext="$T/case/lidtext.stl:#ff9800" plates="$T/case/plates.stl:#e6e6e6" platetext="$T/case/platetext.stl:#ff9800" \
  latches="$T/case/latches.stl:#c62828"
GT="$V/node_modules/.bin/gltf-transform"
"$GT" weld "$T/case/raw.glb" "$T/case/w.glb"
"$GT" simplify "$T/case/w.glb" "$T/case/s.glb" --ratio 0.5 --error 0.0005
"$GT" meshopt "$T/case/s.glb" "$V/assets/case.glb" --level high
ls -l "$V/assets"
