#!/bin/sh
# Build the Fusion script through the SolidCraft stand-in and compare every part with the OpenSCAD model.
#   tools/fusion_check/check.sh <out_dir>
# Needs SolidCraft 0.15 (solidcraft-cli on PATH, or SOLIDCRAFT_CLI), OpenSCAD, and a Python with numpy, numba and scipy
# (PYTHON, default python3) for the comparison. Writes <out_dir>/specter-enclosure.solidcraft, specter-enclosure.step and compare.txt.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mkdir -p "$1" && cd "$1" && pwd)
PY=${PYTHON:-python3}
mkdir -p "$OUT/ref"
for p in tub lid lid_text plate_front plate_front_text plate_back plate_back_text latch; do
  openscad -q -o "$OUT/ref/$p.stl" -D "sel=\"$p\"" "$HERE/reference_parts.scad" &
done
wait
python3 "$HERE/run_fusion_script.py" "$HERE/../../fusion/SpecterEnclosure/SpecterEnclosure.py" "$OUT"
# the Fusion design's origin is the box centre; the OpenSCAD model's is the board's corner
SHIFT=$(python3 -c "import json; P=json.load(open('$HERE/../../params.json'))['P']; print((P['ix0']+P['ix1'])/2, (P['iyf']+P['iyb'])/2)")
X=${SHIFT% *}; Y=${SHIFT#* }
: > "$OUT/compare.txt"
for pair in "tub Tub 0.2" "lid Lid 0.2" "lid_text Lid_text 0.05" "plate_front Front_plate 0.1" "plate_front_text Front_plate_text 0.05" \
            "plate_back Back_plate 0.1" "plate_back_text Back_plate_text 0.05"; do
  set -- $pair
  echo "=== $1" >> "$OUT/compare.txt"
  "$PY" "$HERE/meshcmp.py" "$OUT/ref/$1.stl" "$OUT/$2.stl" --h "$3" --shift "$X" "$Y" 0 --top 4 --ignore 2 >> "$OUT/compare.txt"
done
echo "=== latch (in its own frame)" >> "$OUT/compare.txt"
python3 "$HERE/latch_local.py" "$OUT/Latch.stl" "$OUT/latch_local.stl"
"$PY" "$HERE/meshcmp.py" "$OUT/ref/latch.stl" "$OUT/latch_local.stl" --h 0.05 --axis x --top 4 --ignore 0.3 >> "$OUT/compare.txt"
cat "$OUT/compare.txt"
