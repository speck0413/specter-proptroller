# fusion_check

Builds `fusion/SpecterEnclosure/SpecterEnclosure.py` without Fusion and checks it against the OpenSCAD model.

- `adsk/`: a stand-in for the part of Fusion's API the script calls. Each call becomes the SolidCraft command that does
  the same (a sketch on the same plane, a dimension driven by the same expression, an extrude with the same extent), run
  live through `solidcraft-cli mcp`. A call the script makes that isn't here raises AttributeError, as a misspelt call
  would in Fusion. Units follow Fusion's: centimetres and radians inside the API.
- `run_fusion_script.py`: runs the script under the stand-in and saves the SolidCraft design, a STEP and an STL per body.
- `reference_parts.scad`: the OpenSCAD model's parts as assembled.
- `meshcmp.py`: compares two closed meshes by ray casting on a grid: volumes, the volume where they differ, and where.
- `check.sh <out_dir>`: all of it. `cad` (in `enclosure/`, ignored by git) is where the hand-out files go:

      SOLIDCRAFT_CLI=/path/to/solidcraft-cli PYTHON=/path/to/python-with-numba tools/fusion_check/check.sh cad

Needs SolidCraft 0.15, OpenSCAD and a Python with numpy, numba and scipy for the comparison. The whole build takes about
17 minutes: SolidCraft recomputes the design as it grows.

Where SolidCraft differs from Fusion, the stand-in works round it: it can't mirror a mirror or a pattern (the script
uses a half-turn pattern for the fourth latch), it sketches on a face's plane where it can't keep the face's history
(the lid's top, after the mirrors), and display names, timeline groups and occurrence placements are written into the
saved design at the end, since SolidCraft recomputes everything on each rename or move.
