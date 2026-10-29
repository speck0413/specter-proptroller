# The enclosure in Fusion (and SolidCraft, and STEP)

`SpecterEnclosure/` is a Fusion script that builds the enclosure as a parametric design: user parameters, dimensioned
sketches, extrudes, mirrors, patterns and holes in a grouped timeline. It is the standard case: screwed faceplates, the
printed-pin hinge and cam latches. Run it once, save the design, and work in Fusion from then on.

The same model, built from the same script, also comes as:
- `../cad/specter-enclosure.solidcraft`, a SolidCraft design with the same parameters, sketches and timeline
  (SolidCraft is free, for Windows, macOS and Linux);
- `../cad/specter-enclosure.step`, a solid (B-rep) STEP assembly for any CAD program.

Those two are generated, not kept in git: `../tools/fusion_check/check.sh ../cad` writes them (see below).

## Running it in Fusion

1. **Utilities > Add-Ins > Scripts and Add-Ins**, the **+** next to *My Scripts*, **Script or add-in from device**, and
   pick the `SpecterEnclosure` folder.
2. Select **SpecterEnclosure** and **Run**. It opens a new design and builds the model.
3. Save it. From then on it is an ordinary Fusion design.

The model is Z up. If Fusion is set to Y up (Preferences > General > Default modeling orientation), the box shows on
its back: set the preference to Z up before running, or reorient the ViewCube.

## What it builds

Origin at the centre of the box on the underside of the floor; X left to right seen from the front, Y front to back.

| Component | Bodies | Timeline groups |
|---|---|---|
| Tub | Tub | Box, Rim groove, Ribs and feet, Standoffs, Faceplate mounting, Hinge, Latch ears |
| Lid | Lid, Lid text | Lid and skirt, Ribs and button, Hinge, Keeper lugs, Lid text |
| Front plate | Front plate, Front plate text | Front plate |
| Back plate | Back plate, Back plate text | Back plate |
| Latch, 4 occurrences | Latch | Latch lever |

The `... text` bodies are the inlaid labels, cut into their part and kept as separate bodies for the second filament
colour. Print every part in ASA; print orientation, screws and assembly are in `../README.md`.

## Changing it

**In Fusion (Modify > Change Parameters)**, these follow through the model: the jack, button and faceplate screw holes
(`hole_j35`, `hole_dc`, `hole_button`, `screw_d`, `screw_head`), the guide pins (`guide_pin_d`, `guide_pin_l`), the
faceplates' `plate_t`, `plate_chamfer` and `plate_r`, `corner_r`, the rim groove and lid tongue (`groove_w`,
`groove_d`, `tongue_w`, `tongue_h`), the faceplate seal groove's depth `sg_d`, the feet bosses and pilots, `standoff_h`.

**Everything else, rerun the script** with the new values: edit `PARAMETERS` near the top of
`SpecterEnclosure.py` (or `../design.toml`, see below), run it again, and it builds a new design. The box's size, wall
and height, the hinge, the latches, the screw bosses, the wall windows and the faceplate layout are drawn from the
parameter values when the script runs and stay where they were drawn. Changing `inner_l` in the finished design, for
instance, moves the end walls away from their ribs and latch ears.

To move a jack by hand in Fusion: its dimension in the plate's **jack holes** sketch, and its window in the tub's
**wall windows** sketch.

## Where the numbers come from

`../design.toml` holds every size of the OpenSCAD model the enclosure is printed from. `../tools/fusion_layout.py`
copies the sizes and the faceplate layout into the script (the block between the `layout` markers), so the script
matches the OpenSCAD model. After changing `design.toml`: `../tools/print_build.sh`, then
`python3 ../tools/fusion_layout.py`, then run the script (and `check.sh` for the SolidCraft and STEP files).

## How it was checked

Fusion doesn't run on Linux, where this was made. `../tools/fusion_check/` holds a stand-in for the part of Fusion's
API the script uses, which builds each feature in SolidCraft instead; that run is the SolidCraft design and the
STEP. Every part was then compared with the OpenSCAD model's, as assembled. The comparison ray-casts both on a 0.05-0.2 mm
grid and measures the volume where they differ:

| Part | Volume | Differs | Why |
|---|---|---|---|
| Tub | 184 680 mm³ | 0.39 % | the OpenSCAD model steps its 45° floor fillet and rim underside in 0.5 and 0.2 mm layers; here they are true 45° faces (630 mm³ of the 725) |
| Lid | 97 519 mm³ | 0.18 % | the inlaid title's letter spacing |
| Front plate | 14 640 mm³ | 0.5 % | the back edge's 45° chamfer, stepped in 0.2 mm layers there, true here; labels |
| Back plate | 13 340 mm³ | 0.5 % | the same |
| Latch | 1 027 mm³ | 0.09 % | the cam path, a fitted spline here |
| Text bodies | same volume | | letters up to about 0.5 mm along their line from OpenSCAD's (different text layout engines) |

In SolidCraft, changing `wall`, `inner_h`, `plate_t` and `corner_r` rebuilt with no failed feature or sketch. Changing
`inner_l` did not, for the reason above.

Not checked: the script has not run in Fusion itself. The calls it makes were checked against Autodesk's API reference,
and it is written to come out the same whichever way Fusion orients a sketch on a plane; but the first run in Fusion is
the first real test. Two things to look at then:
- **Text height.** The script gives each label its capital height (3.8 mm for the jack labels, 6.7 mm for the title).
  If Fusion measures text height differently, the labels come out a different size: edit the text height.
- **Font.** Arial Bold, which has the same letter widths as the OpenSCAD model's Liberation Sans Bold.
