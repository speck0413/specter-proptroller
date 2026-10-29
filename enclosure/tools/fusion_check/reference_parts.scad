// The OpenSCAD model's parts as assembled, for comparing with the Fusion script's: openscad -D sel=\"tub\" -o tub.stl reference_parts.scad
include <../../proptroller-print.scad>
part = "none";
sel = "tub";
if (sel == "tub") tub_done();
if (sel == "lid") lid();
if (sel == "lid_text") translate([0, 0, z_lt - inlay]) linear_extrude(inlay) lid_text2d();
if (sel == "plate_front") at_plate("front") plate("front");
if (sel == "plate_back") at_plate("back") plate("back");
if (sel == "plate_front_text") at_plate("front") plate_text("front");
if (sel == "plate_back_text") at_plate("back") plate_text("back");
if (sel == "latch") rotate([90, 0, 90]) translate([0, 0, -latch_w / 2]) linear_extrude(latch_w) latch2d();
