// Spectre Proptroller: 3D-printed enclosure (Bambu Lab X1C, ASA). OpenSCAD 2021.01.
//
// The wooden box's layout in one printed tub: the same board on 10 mm standoffs, the same connector rows, a flat floor on
// the bed with a boss in each corner for a screw-on rubber foot. Each connector wall carries a separate faceplate printed
// face down: its holes come out round, its labels sit on the bottom layers (AMS colour); the panel-mount jacks go through
// it. How it is held is `mount` (design.toml [plates]): "inset" (the default) in a recess in the wall's outside face over
// one large opening (break-out ribs in it while it prints), six M3s; "screws" / "glue" flat on the wall over a window per
// jack, located by guide pins; "inline" (a try-out) no plate: the jacks go through the wall itself. The lid seals on foam
// tape (or the TPU gasket) in a groove round the rim, pressed by a tongue, and is screwed down through bosses on its edge
// into ears on the tub's outside walls (lid_mount "screws", the default); lid_mount "hinge" keeps the printed hinge and four
// cam latches (not built).
//
// Pick what to render with `part` (or -D part="tub" on the command line). Every part comes out in its print pose,
// ready for the slicer: lowest z = 0, no supports.
//   assembly        everything in place (preview only)
//   tub             the box (floor down)
//   lid             top face down          lid_text           its inlaid text, for a second AMS colour
//   latch           one cam latch lever (print 4), lying on its side (lid_mount "hinge" only)
//   zones_tub       the tub's screw zones (zones_lid the lid's): a slicer modifier round each insert and screw, not printed
//   plate_front     front faceplate        plate_front_text   its labels
//   plate_back      back faceplate         plate_back_text    its labels
//   gasket_front    screwed plates: the printed TPU 95A gasket for the front plate's groove, lip up (gasket_back the back's)
//   gasket_lid      the printed TPU 95A gasket for the rim's groove under the lid, lip up (the default seal is foam tape)
//   cover           AC case: the mains-bay cover, top face down     cover_text   its inlaid warning
//   coupon_seal     fit test: a piece of the rim with its groove and the left wall's lid-screw ear, and that piece of the
//                   lid with its boss (with the hinge: the latch ears and the lid's keeper lugs)
//   coupon_mount    fit test: the front wall's right-hand end (inset: the recess, the opening's end and its last break-out
//                   rib; screws / glue: a pin hole and window; the nut bosses) and that corner of the plate
//
// The AC case (design.toml [case] ac, or params from print_params.py --variant ac: README "AC case (advanced)", rev B
// boards only) is the same model with `ac` true: a deeper, taller tub with a barrier round the mains bay and a gland hole
// in the left wall, a duplex receptacle on the front plate, and lid ribs over the barrier.
//
// Every size comes from design.toml, through params.scad (tools/print_params.py writes it; tools/print_build.sh runs it).

include <params.scad>

part = "assembly";
hinge = "pin";              // "pin": printed knuckles on an M3 x 20 or #4 x 3/4 in pan head screw (or a 1.75 mm filament pin: set pin = 1.9)
                            // "everbilt": pads and pilot holes for two Everbilt 1 in narrow utility hinges
// mount ("glue" or "screws") comes from design.toml [plates] through params.scad; -D mount="screws" overrides it.
// Every size lives in design.toml (hole sizes, part measurements, screw counts...): edit it and run tools/print_build.sh.
font = "Liberation Sans:style=Bold";

$fn = $preview ? 64 : 48;   // smoother curves on screen
e = 0.01;


// ---------------------------------------------------------------- frame
xo0 = ix0 - wall; xo1 = ix1 + wall; yo_f = iyf - wall; yo_b = iyb + wall;
xc = (ix0 + ix1) / 2; yc = (iyf + iyb) / 2;
r_in = max(0.5, corner_r - wall);
land_in = land_w - wall;                     // how far the rim land reaches in past the wall
z_lu = z_top + seal_gap;                     // lid underside
z_lt = z_lu + lid_t;                         // lid top
y_ax = yo_b + knuckle_d / 2 + knuckle_off;   // hinge axis
z_ax = z_lt - knuckle_d / 2;
LS = lid_mount == "screws";                  // the screwed lid: no hinge, no latches
lid_back = LS ? yo_b + 20 : hinge == "pin" ? yo_b - 0.5 : yo_b + hinge_leaf_w + 2;
PLATE = mount != "inline";                   // a separate faceplate on each long wall
SCREWED = mount == "screws" || mount == "inset";   // its six M3s into bosses behind the wall
assert(!(ac && mount == "inline"), "the inline try-out is for the standard case only");
acs = mount == "inset" ? ac_seat_inset : ac_seat;   // the receptacle's seat in the wall (the inset plate sits inset_d deeper)

module rr(x0, y0, x1, y1, r) { translate([x0 + r, y0 + r]) offset(r = r) square([x1 - x0 - 2 * r, y1 - y0 - 2 * r]); }
module outer2d() { rr(xo0, yo_f, xo1, yo_b, corner_r); }
module inner2d() { rr(ix0, iyf, ix1, iyb, r_in); }
module ring2d(a, b) { difference() { offset(r = a) outer2d(); offset(r = b) outer2d(); } }   // band between two offsets of the outside
module groove2d(w) { ring2d(-(land_w / 2 - w / 2), -(land_w / 2 + w / 2)); }

// plate frame: x along the wall as seen from outside, y up from the connector row, z out of the wall (back face z = 0)
module at_plate(face) {
  if (face == "front") translate([0, yo_f, z_row]) rotate([90, 0, 0]) children();
  else translate([0, yo_b, z_row]) rotate([90, 0, 180]) children();
}
// wall frame: x along the wall, y outward from the outside face, z up
module at_wall(face, s) {
  if (face == "front") translate([s, yo_f, 0]) rotate([0, 0, 180]) children();
  else if (face == "back") translate([s, yo_b, 0]) children();
  else if (face == "left") translate([xo0, s, 0]) rotate([0, 0, 90]) children();
  else translate([xo1, s, 0]) rotate([0, 0, -90]) children();
}
// per wall, from params: 0 x0, 1 x1, 2 windows, 3 labels, 4 connectors, 5 screws, 6 the glue plate's [bottom, top],
// 7 the screwed plate's, 8 seal ring [x0, x1, y0, y1], 9 guide pins, 10 notches in the screwed plate's top (back: hinges),
// 11 the screwed plate's seal: [rect [x0, x1, y0, y1], discs [x, y, r] round the screws, cuts [x0, x1, y] under the notches],
// 12 each screw's insert boss length
function wl(face, k) = face == "front" ? [front_x0, front_x1, front_windows, front_labels, front_conns, front_screws, front_pf, front_pf_s, front_ring, front_pins, front_notches, front_seal, front_boss_lens, front_nuts, front_opening, front_breaks][k]
                                       : [back_x0, back_x1, back_windows, back_labels, back_conns, back_screws, back_pf, back_pf_s, back_ring, back_pins, back_notches, back_seal, back_boss_lens, back_nuts, back_opening, back_breaks][k];
function pfy(face) = SCREWED ? wl(face, 7) : wl(face, 6);   // the plate's bottom and top edges
pin_h = pin_d / 2 + pin_clr;                 // the pin holes' radius
// a hole through a wall that prints upright: round, with a 45-degree roof cut off flat at the circle's top (a short bridge)
module teardrop2d(r) { intersection() { union() { circle(r = r); polygon([[-r / sqrt(2), r / sqrt(2)], [r / sqrt(2), r / sqrt(2)], [0, r * sqrt(2)]]); }
  translate([-2 * r, -2 * r]) square([4 * r, 3 * r]); } }

// ---------------------------------------------------------------- tub
module tub() {
  difference() {
    union() {
      difference() {
        linear_extrude(z_top) outer2d();
        translate([0, 0, z_floor]) linear_extrude(z_top) inner2d();
      }
      feet_bosses();
      rim_land();
      floor_fillet();
      wall_ribs();
      for (h = board_holes) translate([h[0], h[1], z_floor - e]) standoff();
      if (SCREWED) for (f = ["front", "back"]) for (i = [0 : len(wl(f, 5)) - 1]) let(sc = wl(f, 5)[i]) at_plate(f) translate([sc[0], sc[1], 0]) screw_boss(sc[1], wl(f, 12)[i]);
      if (LS) for (q = lid_ears) at_wall(q[0], q[1]) lid_ear();
      else {
        for (l = latches) if (latch_style == "draw") at_wall(l[0], l[1]) dl_ears(); else at_latch(l) latch_ears();
        if (hinge == "pin") hinge_tub();
        else everbilt_tub();
      }
      if (ac) { ac_barrier_tub(); ac_ties(); ac_cover_bosses(); ac_shelf(); ac_stops(); if (acs < 0) at_plate("front") ac_pads(); }
    }
    if (ac) {
      for (q = ac_cover_bosses) translate([each ac_cover_screw(q), ac_cover_z - insert_depth]) cylinder(d = insert_d, h = insert_depth + 1);   // the cover's inserts
      ac_tie_slots();
      translate([ac_earth[0], ac_earth[3] + 1, ac_earth[1]]) rotate([90, 0, 0]) linear_extrude(ac_barrier_t + 2) teardrop2d(ac_earth[2] / 2);   // the earth wire, to H3 (the board's plated earth point, on GND)
      if (acs > 0) at_plate("front") ac_recess();
      translate([ac_earth2[0], ac_earth2[1], ac_earth2[2]]) rotate([90, 0, 90]) linear_extrude(ac_barrier_t + 2) teardrop2d(ac_earth2[3] / 2);   // the second earth lead, to H2
    }
    translate([0, 0, z_top - groove_d]) linear_extrude(groove_d + 1) groove2d(groove_w);
    feet_pilots();
    for (f = ["front", "back"]) at_plate(f) {
      if (mount == "inline") inline_cuts(f);
      else for (w = wl(f, 2)) if (mount != "inset" || w[4] != 0) translate([0, 0, -wall - 1 - max(0, -acs)]) linear_extrude(wall + 2 + max(0, -acs))   // a window, or a teardrop hole for a screw (through any receptacle pads too)
        if (w[4] == 1) translate([(w[0] + w[1]) / 2, (w[2] + w[3]) / 2]) teardrop2d((w[1] - w[0]) / 2); else rr(w[0], w[2], w[1], w[3], 2);
      if (mount == "inset") { inset_recess(f); for (o = wl(f, 14)) translate([0, 0, -wall - 1]) linear_extrude(wall + 2) square_at(o); }   // the recess, and the one opening behind it
      if (mount == "screws" || mount == "glue") for (p = wl(f, 9)) translate([p[0], p[1], -wall - 1]) linear_extrude(wall + 2) teardrop2d(pin_h);   // guide pin holes, inside the seal ring
      if (SCREWED) for (i = [0 : len(wl(f, 5)) - 1]) let(sc = wl(f, 5)[i], L = wl(f, 12)[i]) translate([sc[0], sc[1], 0])
        if (wl(f, 13)[i] == 1) { translate([0, 0, -wall - L - 1]) linear_extrude(wall + L + 2) teardrop2d(plate_pilot / 2);   // through the wall and the boss, undersized: the screw taps it, with or without the nut
          translate([0, 0, -wall - L - 1]) linear_extrude(nut_t + 0.4 + 1) circle(r = (nut_af + 2 * nut_clr) / sqrt(3), $fn = 6); }   // the nut's pocket, a flat up (a 3.4 mm bridge for its roof)
        else translate([0, 0, -insert_depth]) cylinder(d = insert_d, h = insert_depth + 1);   // a blind insert (the AC mains bay): the boss closes it behind
    }
    for (h = board_holes) translate([h[0], h[1], z_floor + standoff_h - standoff_depth]) cylinder(d = standoff_hole, h = standoff_depth + 1);
    if (LS) for (q = lid_ears) at_wall(q[0], q[1]) translate([0, lid_screw_d, z_lu - drip - lid_screw_gap - lid_screw_depth]) cylinder(d = lid_screw_pilot, h = lid_screw_depth + 1);   // the lid screws' pilots, blind
    else if (hinge == "pin") hinge_bore();
    else everbilt_tub_holes();
    if (ac) translate([xo0 - 1, ac_gland[0], ac_gland[1]]) rotate([90, 0, 90]) linear_extrude(wall + 2) teardrop2d(ac_gland[2] / 2);   // the cord's gland: a round hole would leave its last layers on air; the flat top's corners are 11.1 mm out (README: seal them)
  }
}
module square_at(o) { translate([o[0], o[2]]) square([o[1] - o[0], o[3] - o[2]]); }
// mount = "inset": the recess the plate sits in, inset_d deep in the wall's outside face, inset_clr round the plate, its
// top edge at 45 degrees (a ledge of inset_d would print on air otherwise). Plate frame: z out of the wall.
module inset_recess(face) {
  hull() {
    translate([0, 0, -inset_d]) linear_extrude(e) offset(r = inset_clr) plate2d(face);
    translate([0, inset_d, -e]) linear_extrude(1 + e) offset(r = inset_clr) plate2d(face);
    translate([0, 0, -e]) linear_extrude(1 + e) offset(r = inset_clr) plate2d(face);
  }
}
// the opening's break-out ribs (snap out after printing): break_t thick, one line (0.4) for break_neck where they meet the
// wall above and below; in the wall behind the recess only
module break_ribs(face) {
  for (x = wl(face, 15)) let(os = [for (o = wl(face, 14)) if (o[0] - 0.01 <= x && x <= o[1] + 0.01) o], y0 = min([for (o = os) o[2]]), y1 = max([for (o = os) o[3]]))
    translate([x, 0, -wall - e]) {
      translate([-break_t / 2, y0 + break_neck, 0]) cube([break_t, y1 - y0 - 2 * break_neck, wall - inset_d + 2 * e]);
      for (y = [y0 - e, y1 - break_neck]) translate([-0.2, y, 0]) cube([0.4, break_neck + e, wall - inset_d + 2 * e]);
    }
}
// mount = "inline" (a try-out, no plate): the jacks straight through the wall, teardrop holes as it prints upright; each
// 3.5 mm jack counterbored from inside, teardrop too, to leave j35_seat for its short thread; the labels sunk in the outside face
module inline_cuts(face) {
  for (c = wl(face, 4)) translate([c[2], c[3], -wall - 1]) {
    linear_extrude(wall + 2) teardrop2d(hole_for(c) / 2);
    if (c[1] == "j35p") linear_extrude(1 + wall - j35_seat) teardrop2d((j35p_behind_d + 2 * win_m) / 2);
  }
  translate([0, 0, -inlay]) linear_extrude(1) for (l = wl(face, 3)) label2d(l);
}
// the screwed lid: an ear on the tub's outside wall under each screw (wall frame: y out of the wall, z up), its top
// lid_screw_gap under the lid's boss, lid_ear_h solid, then a 45-degree underside down to the wall
module lid_ear() {
  zt = z_lu - drip - lid_screw_gap;
  hull() {
    translate([0, lid_screw_d, zt - lid_ear_h]) cylinder(r = lid_screw_r, h = lid_ear_h);
    translate([-lid_screw_r, -e, zt - lid_ear_h - (lid_screw_d + lid_screw_r)]) cube([2 * lid_screw_r, e, lid_ear_h + lid_screw_d + lid_screw_r]);
  }
}
module lid_boss() { translate([0, lid_screw_d, z_lu - drip]) cylinder(r = lid_screw_r, h = drip + lid_t); }   // on the lid, over each ear
module lid_boss_hole() {   // M3 clearance, countersunk in the lid's top for a flat head
  translate([0, lid_screw_d, z_lu - drip - 1]) cylinder(d = screw_d, h = drip + lid_t + 2);
  translate([0, lid_screw_d, z_lt - cs_h]) cylinder(d1 = screw_d, d2 = screw_head + 2 * e, h = cs_h + e);
}

// AC case: the barrier round the mains bay (boxes from params: the edge wall, the TRIG1 pocket, the sill, the right-hand
// wall) and the curtain over the relay window, its underside rising at 45 degrees from each end to a short flat bridge
module ac_barrier_tub() {
  for (b = ac_barrier) translate([b[0], b[1], b[4]]) cube([b[2] - b[0], b[3] - b[1], b[5] - b[4]]);
  c = ac_curtain;   // [x0, x1, y0, y1, z at its ends, top, haunch]
  translate([0, c[3], 0]) rotate([90, 0, 0]) linear_extrude(c[3] - c[2])
    polygon([[c[0], c[5]], [c[0], c[4]], [c[0] + c[6], c[4] + c[6]], [c[1] - c[6], c[4] + c[6]], [c[1], c[4]], [c[1], c[5]]]);
}
// the lid's ribs beside the barrier walls that reach up to it (not the sill): one each side, ac_rib_gap off it and
// overlapping its top, stopping at the rim land
module ac_lid_ribs() {
  difference() {
    intersection() {
      translate([0, 0, z_lu - lid_rib_h]) linear_extrude(lid_rib_h + e) offset(r = -(land_w + 0.5)) outer2d();
      union() for (r = ac_ribs) translate([r[0], r[1], 0]) cube([r[2] - r[0], r[3] - r[1], 200]);
    }
    ac_barrier_clear();
  }
}
module ac_barrier_clear() {   // room round the barrier walls' tops, cut from anything on the lid that crosses them
  for (b = ac_barrier) if (b[5] > z_board) translate([b[0] - ac_rib_gap, b[1] - ac_rib_gap, -1]) cube([b[2] - b[0] + 2 * ac_rib_gap, b[3] - b[1] + 2 * ac_rib_gap, b[5] + ac_rib_gap + 1]);
}
// tie points: a block on the floor in front of each relay terminal, a cable-tie slot across it under a 2 mm roof
module ac_ties() { for (t = ac_ties) translate([t[0] - ac_tie[0] / 2, t[1] - ac_tie[1] / 2, z_floor - e]) cube([ac_tie[0], ac_tie[1], ac_tie[2] + e]); }
module ac_tie_slots() { for (t = ac_ties) translate([t[0] - ac_tie[0] / 2 - 1, t[1] - ac_tie[3] / 2, z_floor + ac_tie[2] - 2 - ac_tie[4]]) cube([ac_tie[0] + 2, ac_tie[3], ac_tie[4]]); }
// the cover's bosses: 8 x 8 on a wall, their tops at the cover's underside, a 45-degree underside down to the wall
module ac_cover_bosses() {   // each [x, y, normal, reach]: from its wall out to reach, the insert reach - 4 off the wall
  for (q = ac_cover_bosses) translate([q[0], q[1], 0]) rotate([0, 0, atan2(q[3], q[2])]) hull() {
    translate([-e, -4, ac_cover_z - 8]) cube([q[4] + e, 8, 8]);
    translate([-e, -4, ac_cover_z - 8 - q[4]]) cube([e, 8, e]);
  }
}
module ac_stops() { for (q = ac_stops) translate([q[0], q[1], q[2]]) cube([q[3] - q[0], q[4] - q[1], q[5] - q[2]]); }   // behind the strap's ends
function ac_cover_screw(q) = [q[0] + (q[4] - 4) * q[2], q[1] + (q[4] - 4) * q[3]];
// the cover's labyrinth at the left and front walls: a shelf on each wall under the rim land (its top ac_shelf[1], 45-degree
// underside), from the bay's front-left corner along the left wall to the edge wall and along the front wall to the barrier
module ac_shelf() {
  d = ac_shelf[0]; zt = ac_shelf[1]; yb = ac_barrier[0][1];
  for (s = [[ix0, iyf, d, yb - iyf], [ix0, iyf, ac_cover[0][2] + 0.5 - ix0, d]]) hull() {
    translate([s[0] - e, s[1] - e, zt - 1]) cube([s[2] + e, s[3] + e, 1]);
    translate([s[0] - e, s[1] - e, zt - 1 - d]) cube([s[2] == d ? e : s[2] + e, s[3] == d ? e : s[3] + e, e]);
  }
}
// the receptacle's seat (ac_seat from design.toml: its faces face_set proud of the plate): a recess in the wall's inside
// face, its top edge at 45 degrees; or pads on it at its three screws, 45-degree undersides. Plate frame, front wall.
module ac_recess() {
  o = [for (c = front_conns) if (c[1] == "outlet") c][0];
  hull() {
    translate([o[2] - outlet_strap_l / 2 - 0.5, o[3] - outlet_strap_w / 2 - 0.5, -wall - e]) cube([outlet_strap_l + 1, outlet_strap_w + 1 + acs, e]);
    translate([o[2] - outlet_strap_l / 2 - 0.5, o[3] - outlet_strap_w / 2 - 0.5, -wall + acs - e]) cube([outlet_strap_l + 1, outlet_strap_w + 1, e]);
  }
}
module ac_pads() {
  o = [for (c = front_conns) if (c[1] == "outlet") c][0]; h = -acs;
  for (d = [-outlet_screw_pitch / 2, 0, outlet_screw_pitch / 2]) { w = d == 0 ? 7 : 10; hull() {
    translate([o[2] + d - w / 2, o[3] - 6, -wall - h]) cube([w, 12, h + e]);
    translate([o[2] + d - w / 2, o[3] - 6 - h, -wall]) cube([w, 12 + h, e]);
  } }
}
// the mains-bay cover: a 2.4 mm plate over the bay and the relay terminals, a chimney under it round J_R1 / J_R2, five
// countersunk M3s into the bosses, the warning inlaid in its top
ac_cover_t = 2.4;
module ac_cover2d() {
  difference() {
    union() for (r = ac_cover) translate([r[0], r[1]]) square([r[2] - r[0], r[3] - r[1]]);
    translate([ac_pocket_out[0], ac_pocket_out[1]]) square([ac_pocket_out[2] - ac_pocket_out[0], ac_pocket_out[3] - ac_pocket_out[1]]);
    translate([ac_niche_out[0], ac_niche_out[1]]) square([ac_niche_out[2] - ac_niche_out[0], ac_niche_out[3] - ac_niche_out[1]]);   // the earth wire's niche
    for (i = [0 : len(front_screws) - 1]) let(sc = front_screws[i]) if (z_row + sc[1] + boss_h / 2 > ac_cover_z - 3)
      translate([sc[0] - boss_w / 2 - 0.5, iyf - 1]) square([boss_w + 1, front_boss_lens[i] + 1.5]);   // the screwed front plate's top bosses
  }
}
module ac_cover_text2d() {
  translate([22, -131]) text("UNPLUG CORD", size = 6, font = font, halign = "center", valign = "center");
  translate([22, -143]) text("BEFORE OPENING", size = 6, font = font, halign = "center", valign = "center");
}
module ac_cover() {
  difference() {
    union() {
      translate([0, 0, ac_cover_z]) linear_extrude(ac_cover_t) ac_cover2d();
      for (q = ac_chimney) translate([q[0], q[1], q[4]]) cube([q[2] - q[0], q[3] - q[1], q[5] - q[4]]);
      // the labyrinth's lip under the left and front edges, cut round the bosses and the screwed front plate's top bosses
      translate([0, 0, ac_cover_z - ac_lip[2]]) linear_extrude(ac_lip[2] + e) difference() {
        intersection() { ac_cover2d(); union() {
          translate([ix0 + ac_lip[0], iyf + ac_lip[0]]) square([ac_lip[1], ac_barrier[0][1] - iyf]);
          translate([ix0 + ac_lip[0], iyf + ac_lip[0]]) square([ac_cover[0][2] - ix0, ac_lip[1]]); } }
        for (q = ac_cover_bosses) translate(ac_cover_screw(q)) square([20, 9], center = true);
      }
    }
    for (q = ac_cover_bosses) translate([each ac_cover_screw(q), ac_cover_z]) {
      translate([0, 0, -1]) cylinder(d = screw_d, h = ac_cover_t + 2);
      translate([0, 0, ac_cover_t - cs_h]) cylinder(d1 = screw_d, d2 = screw_head + 2 * e, h = cs_h + e);
    }
    translate([0, 0, ac_cover_z + ac_cover_t - inlay]) linear_extrude(1) ac_cover_text2d();
  }
}
if (ac && !LS) {   // the gland's cap clears the latch ears on the left wall (they come down to latch_eb at the wall)
  latch_eb = latch_pz - 3.6 - latch_ky - 1.5;
  for (l = latches) if (l[0] == "left") assert(abs(l[1] - ac_gland[0]) >= ac_gland[4] / 2 + pin_span / 2 + 0.5 || ac_gland[1] + ac_gland[4] / 2 <= latch_eb - 0.5,
    "the gland's cap runs into a latch's ears");
}
// rubber feet: a boss inside each corner of the floor, a blind pilot hole from underneath
function feet_xy() = [for (x = [ix0 + feet_in, ix1 - feet_in]) for (y = [iyf + feet_in, iyb - feet_in]) [x, y]];
module feet_bosses() { for (p = feet_xy()) translate([p[0], p[1], z_floor - e]) cylinder(d = feet_boss_d, h = feet_boss_h + e); }
module feet_pilots() { for (p = feet_xy()) translate([p[0], p[1], -1]) cylinder(d = feet_pilot, h = feet_depth + 1); }
// the land under the lid: the rim thickened inwards to land_w, on a 45-degree underside (stepped 0.5 mm)
module rim_land() {   // 45-degree underside in layer-height steps
  translate([0, 0, z_top - land_h]) linear_extrude(land_h) ring2d(0, -land_w);
  n = ceil(land_in / 0.2);   // one step per 0.2 mm layer: a smooth 45
  for (i = [0 : n - 1]) translate([0, 0, z_top - land_h - (i + 1) * land_in / n])
    linear_extrude(land_in / n + e) ring2d(0, -(wall + land_in * (n - i) / n));
}
module floor_fillet() {
  n = 4;
  for (i = [0 : n - 1]) translate([0, 0, z_floor + i * 0.5]) linear_extrude(0.5 + e) ring2d(-wall + e, -(wall + 2 * (n - i) / n));
}
// vertical ribs inside the end walls (the long walls are stiffened by their plates)
module wall_ribs() {
  zt = z_top - land_h - land_in;
  for (y = [for (r = ribs) yc + r]) {
    translate([ix0 - e, y - 0.8, z_floor]) cube([3, 1.6, zt - z_floor]);
    translate([ix1 - 3 + e, y - 0.8, z_floor]) cube([3, 1.6, zt - z_floor]);
  }
}
module standoff() {
  cylinder(d = 8, h = standoff_h);
  for (a = [0 : 90 : 270]) rotate([0, 0, a + 45])
    rotate([90, 0, 0]) linear_extrude(1.2, center = true) polygon([[3, 0], [8, 0], [3, 5]]);
}
// a boss behind the wall for each plate screw (mount = "screws"): the screw through it into a hex nut (plate_nut) in a pocket on its
// inside end, put in from inside the box; in the AC mains bay a blind M3 heat-set insert instead, pressed in from outside with a
// soldering iron. A 45-degree underside so it prints off the vertical wall. The plate's gasket seals round each screw (its discs)
module screw_boss(y, L = boss_len) {   // y: the screw's height in the plate frame; L: how far it reaches in; a low boss stands on the floor instead
  drop = z_row + y - boss_h / 2 - z_floor;     // room below it, down to the floor
  b = drop < L + 1 ? -boss_h / 2 - drop - 0.5 : -boss_h / 2;   // 0.5 into the floor
  translate([0, 0, -wall + e]) rotate([0, 90, 0])
    linear_extrude(boss_w, center = true) polygon([[0, b - (drop < L + 1 ? 0 : L)], [0, boss_h / 2], [L, boss_h / 2], [L, b]]);
}

// ---------------------------------------------------------------- latches
// A screwed cam latch, two on each end wall. The lever turns on a screw through two ears on the tub and hooks a second
// screw, the keeper, across two lugs on the lid. The hook's underside is cut by the keeper's own path as the lever
// closes: from first catch (latch_lead above a seated lid) it draws the keeper down by latch_bite, and over the last
// latch_dip degrees by latch_detent more, then lets it back up by that much, so the closed latch sits in a pocket and stays shut
// until it's pulled open. Every pivot and keeper is the hinge's screw (pin_span long), in from the corner's side, clear
// through the thick ear (ear_f) and threaded into the thin one nearer the wall's middle (ear_t, pin_pilot): no nuts. Frame: the wall frame (y out of the wall, z up), x along it.
lk_r = 1.5;                                     // the keeper screw's radius (M3; a #4 is 1.42)
latch_yl = lid_gap + skirt_t;                   // the lid's outside face
latch_kz = z_lu - 1.5;                          // the keeper: in the lid's lugs, just under the lid
latch_pz = z_top - latch_drop;                  // the pivot
latch_d = latch_kz - latch_pz;                  // pivot to keeper, closed
latch_rm = latch_d + latch_bite + latch_lead;   // pivot to keeper at first catch
latch_ct = 2.6;                                 // the hook's thickness over the keeper
latch_ro = latch_rm + lk_r + latch_ct;          // the hook's outside radius
latch_tip = latch_travel + asin(lk_r / latch_rm) / 2;   // the hook reaches past the keeper at first catch by half its width
latch_ky = latch_yl + 0.4 + (latch_rm + lk_r) * sin(latch_tip);   // pivot and keeper this far out: the hook clears the lid
latch_p = [latch_ky, latch_pz]; latch_k = [latch_ky, latch_kz];
latch_g = latch_w / 2 + 0.4;                    // ears and lugs: the lever's half width and its side play
ear_f = pin_span - ear_t - 2 * latch_g;         // the thick ear / lug, the screw's head side (clear)
assert(ear_f >= 4, "pin_span leaves the far latch ear too thin: make latch_w or ear_t smaller");
// the keeper's distance from the pivot, lever opened by a degrees (closed at 0)
function latch_rho(a) = a <= 0 ? latch_d : a < latch_dip ? latch_d - latch_detent * a / latch_dip
  : a < latch_travel ? latch_d - latch_detent + (latch_rm - latch_d + latch_detent) * (a - latch_dip) / (latch_travel - latch_dip) : latch_rm;
function latch_kat(a) = latch_p + latch_rho(a) * [-sin(a), cos(a)];   // where the keeper sits in the lever's frame
// a latch's wall frame, mirrored so its thin ear and lug (ear_t, threaded) face the wall's middle and the thick ones
// (ear_f, clear: the screws' head side) the corner (owner 2026-10-21: the thin ones less exposed; 2026-10-22: the screws
// thread into the holes facing the middle, in from the corner's side, where a screwdriver has room); the lever is symmetric
function latch_mid(l) = l[0] == "front" ? l[1] > xc : l[0] == "back" ? l[1] < xc : l[0] == "left" ? l[1] < yc : l[1] > yc;   // the middle is on +x
module at_latch(l) { at_wall(l[0], l[1]) mirror([latch_mid(l) ? 1 : 0, 0, 0]) children(); }
module latch_ear2d(r) {   // a tub ear: round the pivot, its underside steeper than 45 down to the wall
  hull() { translate(latch_p) circle(r = r); translate([0, latch_pz - r - latch_ky - 1.5]) square([e, latch_ky + r + 1.5 + r]); }
}
module latch_ears() {   // s < 0: the thin ear (ear_t), which the screw threads into; s > 0 the thick one (clear: the head side)
  for (s = [-1, 1]) {
    t = s < 0 ? ear_t : ear_f;
    translate([s < 0 ? -latch_g - t : latch_g, 0, 0]) rotate([90, 0, 90]) linear_extrude(t) difference() {
      latch_ear2d(3.6);
      translate(latch_p) circle(d = s < 0 ? pin_pilot : pin_d);
    }
  }
}
module latch_lugs() {   // on the lid: from its top face (on the bed as printed) down round the keeper
  for (s = [-1, 1]) {
    t = s < 0 ? ear_t : ear_f;
    translate([s < 0 ? -latch_g - t : latch_g, 0, 0]) rotate([90, 0, 90]) linear_extrude(t) difference() {
      hull() { translate(latch_k) circle(r = 3.6); translate([lid_gap + 0.5, z_lu - drip + 0.5]) square([e, z_lt - z_lu + drip - 0.5]);
        translate([lid_gap + 0.5, z_lt - e]) square([latch_ky + 3.6 - lid_gap - 0.5, e]); }
      translate(latch_k) circle(d = s < 0 ? pin_pilot : pin_d);
    }
  }
}
module latch2d() {   // the lever's profile, closed, in the wall frame; it is latch_w wide
  y_in = latch_ky + lk_r + 0.6;                 // the spine's inner face, just outside the keeper
  st = 2.6;                                     // spine thickness
  intersection() {
    difference() {
      union() {
        hull() { translate(latch_p) circle(r = 3.6); translate([y_in, latch_pz]) square([st, 0.1]); }
        translate([y_in, latch_pz]) square([st, latch_ro]);
        // the hook: a sector round the pivot, from the spine over the keeper towards the wall
        polygon(concat([for (a = [-asin((lk_r + 0.6) / latch_d) : 1 : latch_tip + 6]) latch_p + latch_d * [-sin(a), cos(a)]],
                       [for (a = [latch_tip + 6 : -1 : -asin((lk_r + 0.6 + st) / latch_ro)]) latch_p + latch_ro * [-sin(a), cos(a)]]));
        hull() { translate([y_in, latch_pz + latch_ro - 3]) square([st, 3]); translate([y_in + st - 1.3, latch_pz + latch_ro + 3]) circle(r = 1.3); }   // thumb tab, straight up: pull it out to open
      }
      translate(latch_p) circle(d = pin_d + 0.3);
      // the keeper's path as the lever opens from closed, and on out of the hook's mouth
      for (a = [0 : 1 : latch_travel + 40]) hull() { translate(latch_kat(a)) circle(r = lk_r + 0.05); translate(latch_kat(a + 1)) circle(r = lk_r + 0.05); }
    }
    translate([latch_yl + 0.4, 0]) square([60, 200]);   // never inside the lid's outside face
  }
}
module latch_print() { linear_extrude(latch_w) latch2d(); }

// the draw latch (latch_style = "draw"; print_params.py places dl_p1, dl_p2, dl_k and checks it goes over centre and lets
// go): in the wall frame, profiles in y-z, closed. Across the latch (x): the lever in the middle (dl_lever_t), a tub ear
// each side of it (dl_ear_t), a hook plate outside each ear (dl_plate_t), dl_gap between each. The lever's pin goes through
// the ears and the lever; the hook's through the plates and the lever, past the ears (it swings round outside them).
dl_xl = dl_lever_t / 2 + dl_gap;                 // the ears' inner faces
dl_xp = dl_xl + dl_ear_t + dl_gap;               // the hook plates' inner faces
dl_glue = dl_pin + 0.15;                         // a pin's hole where it is glued (the ears, the hook plates)
dl_turn = dl_pin + 0.45;                         // where it turns (the lever)
dl_knee = [lid_gap + skirt_t + 0.6 + 2.4, dl_k[1] + dl_tip_t / 2];   // the hook's bend over the skirt's top edge
module dl_ear2d() {   // a tub ear round the lever's pin, its underside steeper than 45 down to the wall
  difference() {
    hull() { translate(dl_p1) circle(r = dl_ear_r); translate([0, dl_p1[1] - dl_ear_r - dl_p1[0] - 1.5]) square([e, dl_p1[0] + 2 * dl_ear_r + 1.5]); }
    translate(dl_p1) teardrop2d(dl_glue / 2);
  }
}
module dl_ears() { for (s = [-1, 1]) translate([s < 0 ? -dl_xl - dl_ear_t : dl_xl, 0, 0]) rotate([90, 0, 90]) linear_extrude(dl_ear_t) dl_ear2d(); }
dl_foot = [dl_p2[1] - dl_boss_r - 9, dl_p2[1] - dl_boss_r - 3];   // the lever's foot, [bottom, top]: its inside face on the wall closes it
module dl_lever2d() {
  difference() {
    union() {
      hull() { translate(dl_p1) circle(r = dl_boss_r); translate(dl_p2) circle(r = dl_boss_r); }
      hull() { translate(dl_p2) circle(r = dl_boss_r); translate([0, dl_foot[0]]) square([dl_p2[0] + 2, dl_foot[1] - dl_foot[0]]); }
      translate([dl_p2[0] + 2 - 1.2, dl_foot[0] - 1.6]) square([2.4, 1.6 + e]);   // a lip under the foot's outside: pull here to open
    }
    translate(dl_p1) circle(d = dl_turn); translate(dl_p2) circle(d = dl_turn);
  }
}
module dl_hook2d() {   // one hook plate: from the hook's pin up outside the lid's skirt, its tip over the lid's top edge
  difference() {
    union() {
      hull() { translate(dl_p2) circle(r = dl_boss_r); translate(dl_knee) circle(r = 2.4); }
      translate([dl_k[0] - dl_reach / 2, dl_k[1]]) square([dl_knee[0] + 2.4 - dl_k[0] + dl_reach / 2, dl_tip_t]);
      translate([dl_knee[0], dl_k[1]]) square([2.4, dl_tip_t + 1.5]);   // a tab above the bend: lift here to unhook
    }
    translate(dl_p2) circle(d = dl_glue);
  }
}
assert(norm([lid_gap + skirt_t, dl_k[1]] - dl_knee) >= 2.4 + 0.4, "the hook's bend runs into the lid's skirt");
// the hook plates' inside edge stays 0.4 off the skirt's outside face, from the skirt's foot up to the tip
for (z = [z_lu - drip : 0.5 : dl_k[1]]) let(d = dl_knee - dl_p2, t = max(0, min(1, (z - dl_p2[1]) / d[1])), c = dl_p2 + t * d)
  assert(c[0] - (dl_boss_r + (2.4 - dl_boss_r) * t) >= lid_gap + skirt_t + 0.4, str("the hook runs into the lid's skirt at z ", z));
module dl_parts_closed() {   // the lever and hook in place, closed (the assembled view and renders)
  rotate([90, 0, 90]) translate([0, 0, -dl_lever_t / 2]) linear_extrude(dl_lever_t) dl_lever2d();
  for (s = [-1, 1]) translate([s < 0 ? -dl_xp - dl_plate_t : dl_xp, 0, 0]) rotate([90, 0, 90]) linear_extrude(dl_plate_t) dl_hook2d();
  for (q = [[dl_p1, dl_xl + dl_ear_t], [dl_p2, dl_xp + dl_plate_t]]) translate([-q[1], q[0][0], q[0][1]]) rotate([0, 90, 0]) cylinder(d = dl_pin, h = 2 * q[1]);
}
// the printed pins, lying (a flat 0.3 mm deep along the bottom: on the bed), each end chamfered
module dl_pin_print(L) {
  intersection() {
    translate([0, 0, dl_pin / 2 - 0.3]) rotate([0, 90, 0]) hull() { cylinder(d = dl_pin - 0.8, h = L); translate([0, 0, 0.4]) cylinder(d = dl_pin, h = L - 0.8); }
    translate([-1, -dl_pin, 0]) cube([L + 2, 2 * dl_pin, dl_pin]);
  }
}

// ---------------------------------------------------------------- hinge
// hinge_xs: the hinges' centres along the back edge (print_params.py, from [hinge] hinge_at)
function seg_x(hx, i) = hx - 1.5 * knuckle_l - knuckle_gap + i * (knuckle_l + knuckle_gap);
module hinge_tub() {   // outer knuckles of each hinge (the lid's sits between), each on its own 45-degree gusset from the wall
  for (hx = hinge_xs) for (i = [0, 2]) translate([seg_x(hx, i), 0, 0]) rotate([90, 0, 90]) linear_extrude(knuckle_l)
    hull() {   // underneath, 45 degrees (or steeper) from the knuckle's outer underside down to the wall's outside face
      translate([y_ax, z_ax]) circle(d = knuckle_d);
      translate([yo_b - e, k_zw]) square([e, z_ax - k_zw]);
    }
}
// where the gusset meets the wall: the 45-degree tangent off the knuckle's outer underside (pushed 0.15 out for the
// circle's facets). The back plate stands below it: its top edge, chamfered on the back, must clear the gusset's face.
k_t = knuckle_d / 2 / sqrt(2) + 0.15;
k_zw = (z_ax - k_t) - (y_ax + k_t - yo_b);
k_ptop = z_row + back_pf[1];                 // the back plate's top edge under the hinges (the screwed plate is notched there)
k_clr = (k_zw + plate_chamfer - k_ptop) / sqrt(2);   // square to the gusset's face, from the plate's chamfer
assert(LS || hinge != "pin" || k_clr >= 0.3, str("the hinge gussets reach down onto the back plate (", k_clr, " mm): lower the labels or chamfer the plate more"));
// the lid's knuckle: everything below the lid's underside stays within knuckle_d / 2 + 0.6 of the axis, so it swings past
// the rim's back edge (knuckle_off + knuckle_d / 2 from the axis) with room to spare; the web to the lid stays above
// the underside. Flanks steeper than 45 down to the bed (the lid prints top down).
module hinge_lid() {
  r_web = knuckle_d / 2 + 0.6;
  for (hx = hinge_xs) translate([seg_x(hx, 1), 0, 0]) rotate([90, 0, 90]) linear_extrude(knuckle_l) {
    hull() { translate([y_ax, z_ax]) circle(d = knuckle_d);
      translate([y_ax - sqrt(r_web * r_web - pow(z_lu - z_ax, 2)), z_lu]) square([2, lid_t]);
      translate([y_ax - 2.0, z_lt - e]) square([4.0, e]); }
    translate([yo_b - 6, z_lu + e]) square([y_ax - yo_b + 6 - 2, lid_t - e]);   // into the lid's back edge, above its underside
  }
}
// the screw goes in from the hinge's outer end (towards the nearer side wall, where a screwdriver has room): clear through
// the first two knuckles, threads into the one nearer the middle (pin_pilot)
module hinge_bore() {
  for (hx = hinge_xs) translate([hx, 0, 0]) mirror([hx > xc ? 1 : 0, 0, 0]) translate([-hx, y_ax, z_ax]) rotate([0, 90, 0]) {
    translate([0, 0, seg_x(hx, 0) - 1]) cylinder(d = pin_d, h = seg_x(hx, 2) - seg_x(hx, 0) + 1);
    translate([0, 0, seg_x(hx, 2) - e]) cylinder(d = pin_pilot, h = knuckle_l + 1);
  }
}
// clearance cut round the lid knuckle in the tub (and round the tub knuckles in the lid)
module knuckle_gap_cut(seg) {
  for (hx = hinge_xs) for (i = seg) translate([seg_x(hx, i) - knuckle_gap, y_ax, z_ax]) rotate([0, 90, 0]) cylinder(d = knuckle_d + 2 * knuckle_gap, h = knuckle_l + 2 * knuckle_gap);
}
// Everbilt: the wall leaf on the back wall's outside, the lid leaf under the lid's back overhang, knuckle at the corner
module everbilt_tub() {   // bosses inside; outside a pad flush with the plate's show face, for the leaf to lie flat on
  for (hx = hinge_xs) for (dx = hinge_holes) hull() {   // a 45-degree underside down to the inside face: no cantilever
    translate([hx + dx, iyb - 6 + e, z_top - hinge_hole_inset]) rotate([-90, 0, 0]) cylinder(d = 7, h = 6);
    translate([hx + dx - 3.5, iyb - e, z_top - hinge_hole_inset - 3.5 - 6]) cube([7, e, e]);
  }
  for (hx = hinge_xs) translate([hx - hinge_leaf_l / 2 - 1, 0, 0]) rotate([90, 0, 90]) linear_extrude(hinge_leaf_l + 2)
    polygon([[yo_b - e, e_zp], [yo_b + plate_t, e_zp + plate_t + e], [yo_b + plate_t, z_top], [yo_b - e, z_top]]);   // 45-degree underside, above the plate
}
e_zp = k_ptop + 0.4 - plate_chamfer;         // where the pad's 45-degree underside meets the wall: over the plate's chamfered top edge
module everbilt_tub_holes() { for (hx = hinge_xs) for (dx = hinge_holes) translate([hx + dx, yo_b + plate_t + 1, z_top - hinge_hole_inset]) rotate([90, 0, 0]) cylinder(d = hinge_pilot_d, h = plate_t + wall + 7); }
module everbilt_lid() { for (hx = hinge_xs) for (dx = hinge_holes) translate([hx + dx, yo_b + hinge_hole_inset, z_lu - 4]) cylinder(d = 7, h = 4 + e); }
module everbilt_lid_holes() { for (hx = hinge_xs) for (dx = hinge_holes) translate([hx + dx, yo_b + hinge_hole_inset, z_lu - 5]) cylinder(d = hinge_pilot_d, h = 5 + lid_t - 0.8); }

// ---------------------------------------------------------------- lid
module lid2d() {
  intersection() { offset(r = lid_gap + skirt_t) outer2d(); translate([xo0 - 10, yo_f - 10]) square([xo1 - xo0 + 20, lid_back - yo_f + 10]); }
  if (!LS && hinge != "pin") translate([xo0 - lid_gap - skirt_t + corner_r, yo_b - 5]) square([xo1 - xo0 + 2 * (lid_gap + skirt_t) - 2 * corner_r, lid_back - yo_b + 5]);
}
module lid_text2d() {
  translate([xc, yc]) text(title, size = 7, font = font, halign = "center", valign = "center");
  translate([button_xy[0], button_xy[1] - hole_button / 2 - 3]) text("TRIG2", size = 4, font = font, halign = "center", valign = "top");
  if (ac) translate([xc, yc - 12]) text("AC CASE: REV B BOARD ONLY", size = 4, font = font, halign = "center", valign = "center");
}
module lid() {
  difference() {
    union() {
      translate([0, 0, z_lu]) linear_extrude(lid_t) lid2d();
      // drip skirt: all round for the screwed lid, on the front and both ends with the hinge
      intersection() {
        translate([0, 0, z_lu - drip]) linear_extrude(drip + e) ring2d(lid_gap + skirt_t, lid_gap);
        translate([xo0 - 10, yo_f - 10, 0]) cube([xo1 - xo0 + 20, yo_b - yo_f + 10 - (LS ? -20 : corner_r), 200]);
      }
      translate([0, 0, z_lu - tongue_h]) linear_extrude(tongue_h + e) groove2d(tongue_w);
      lid_ribs();
      if (ac) ac_lid_ribs();
      if (LS) for (q = lid_ears) at_wall(q[0], q[1]) lid_boss();
      else {
        if (hinge == "pin") hinge_lid(); else everbilt_lid();
        if (latch_style == "cam") for (l = latches) at_latch(l) latch_lugs();
      }
    }
    translate([button_xy[0], button_xy[1], z_lu - 10]) cylinder(d = hole_button, h = 20);
    translate([0, 0, z_lt - inlay]) linear_extrude(1) lid_text2d();
    if (LS) for (q = lid_ears) at_wall(q[0], q[1]) lid_boss_hole();
    else if (hinge == "pin") { hinge_bore(); knuckle_gap_cut([0, 2]); } else everbilt_lid_holes();
  }
}
module lid_ribs() {
  difference() {
    intersection() {
      translate([0, 0, z_lu - lid_rib_h]) linear_extrude(lid_rib_h + e) offset(r = -(land_w + 1)) outer2d();
      union() {
        for (x = [xc - 2 * lid_rib_pitch : lid_rib_pitch : xc + 2 * lid_rib_pitch]) translate([x - 0.8, yo_f, 0]) cube([1.6, yo_b - yo_f, 200]);
        for (y = [yc - lid_rib_pitch : lid_rib_pitch : yc + lid_rib_pitch]) translate([xo0, y - 0.8, 0]) cube([xo1 - xo0, 1.6, 200]);
      }
    }
    if (ac) ac_barrier_clear();   // TRIG2 sits in a rib cell (print_params.py asserts its nut clear of every rib)
  }
}

// ---------------------------------------------------------------- faceplates
module hole_cut(d, thickness) { translate([0, 0, -1]) cylinder(d = d, h = thickness + 2, $fn = 64); }   // through the plate
module print_pose(thickness) { translate([0, 0, thickness]) rotate([180, 0, 0]) children(); }   // flipped onto its show face
function hole_for(c) = c[1] == "usb" ? hole_usb : c[1] == "j35p" ? hole_j35p : hole_dcp;   // c = [id, kind, x, y]
module label2d(l) {   // [text, x, row, size, baseline y]: row 1 above the connectors, row 2 below
  translate([l[1], l[4]]) text(l[0], size = l[3], font = font, halign = "center");
}
// AC case: a duplex receptacle on its side behind the plate. An opening per face, and three #6-32 flat heads countersunk
// in the show face: the faceplate screw midway, tapped into the strap, and one through each of the strap's mounting slots
// (a nut behind). The strap lies on the wall's inside face.
ocs_h = (outlet_screw_head - outlet_screw_d) / 2 / tan(41);
module outlet_cut(c) {
  fw = outlet_face_h / 2 + outlet_open_m; fh = outlet_face_w / 2 + outlet_open_m;
  for (s = [-1, 1]) translate([c[2] + s * outlet_face_pitch / 2, c[3], -1]) linear_extrude(plate_t + 2) rr(-fw, -fh, fw, fh, 3);
  for (d = [-outlet_screw_pitch / 2, 0, outlet_screw_pitch / 2]) translate([c[2] + d, c[3], 0]) {
    hole_cut(outlet_screw_d, plate_t);
    translate([0, 0, plate_t - ocs_h]) cylinder(d1 = outlet_screw_d, d2 = outlet_screw_head + 2 * e, h = ocs_h + e);
  }
}
// the plate's outline in its frame; the screwed one is taller (its screw rows) and notched under the back wall's hinges
module plate2d(face) {
  x0 = wl(face, 0); x1 = wl(face, 1); y = pfy(face);
  difference() {
    rr(x0, y[0], x1, y[1], plate_r);
    if (SCREWED) for (n = wl(face, 10)) translate([n[0], wl(face, 6)[1]]) offset(r = 1) offset(delta = -1) square([n[1] - n[0], 50]);
    if (mount == "inset") translate([x1, y[1]]) polygon([[-inset_key, 1], [-inset_key - 1, 0], [0, -inset_key - 1], [1, -inset_key]]);   // the keyed corner (cut at 45 degrees)
  }
}
module plate_body(face) {   // flat, with a 45-degree chamfer round the back edge (in 0.2 mm steps: the top layers as printed)
  n = round(plate_chamfer / 0.2);
  translate([0, 0, plate_chamfer]) linear_extrude(plate_t - plate_chamfer) plate2d(face);
  for (i = [0 : n - 1]) translate([0, 0, i * plate_chamfer / n]) linear_extrude(plate_chamfer / n + e) offset(delta = -(n - i - 0.5) * plate_chamfer / n) plate2d(face);
}
module plate(face) {
  g = wl(face, 8);   // the seal ring's outside [x0, x1, y0, y1]
  difference() {
    plate_body(face);
    for (c = wl(face, 4)) if (c[1] == "outlet") outlet_cut(c); else translate([c[2], c[3], 0]) hole_cut(hole_for(c), plate_t);
    // the 3.5 mm jacks: a counterbore from the back, as wide as the wall's window, leaving j35_seat where the jack seats
    for (c = wl(face, 4)) if (c[1] == "j35p") translate([c[2], c[3], -1]) cylinder(d = j35p_behind_d + 2 * win_m, h = 1 + plate_t - j35_seat, $fn = 64);
    translate([0, 0, plate_t - inlay]) linear_extrude(1) for (l = wl(face, 3)) label2d(l);
    if (SCREWED) for (sc = wl(face, 5)) translate([sc[0], sc[1], 0]) screw_cut();
    // the seal groove round the back, round the windows and the pin holes: glued, the sealing adhesive's bead; screwed, the
    // wider loop along the screw rows and round each screw (seal_groove2d) for butyl, silicone, tubing or the TPU gasket
    if (SCREWED) translate([0, 0, -1]) linear_extrude(1 + sg_d) seal_groove2d(face);
    else translate([0, 0, -1]) linear_extrude(1 + pg_d) difference() { rr(g[0], g[2], g[1], g[3], 2); rr(g[0] + pg_w, g[2] + pg_w, g[1] - pg_w, g[3] - pg_w, 1); }
  }
  if (mount == "screws" || mount == "glue") for (p = wl(face, 9)) translate([p[0], p[1], 0]) mirror([0, 0, 1]) guide_pin();   // into the wall, pointing up as printed
}
module guide_pin() { cylinder(d = pin_d, h = pin_l - 0.6 + e); translate([0, 0, pin_l - 0.6]) cylinder(d1 = pin_d, d2 = pin_d - 1.2, h = 0.6); }
// M3 clearance through the plate, countersunk at the show face so a flat head sits flush (the cone is the bottom layers
// as printed: its 45 degree overhang needs no support); for pan heads (screw_pan) a plain hole, the head on the show face
cs_h = (screw_head - screw_d) / 2 / tan(screw_head_angle / 2);
assert(cs_h < plate_t - 0.4, "the countersink is deeper than the plate: make plate_t thicker or screw_head smaller");
module screw_cut() {
  hole_cut(screw_d, plate_t);
  if (!screw_pan) translate([0, 0, plate_t - cs_h]) cylinder(d1 = screw_d, d2 = screw_head + 2 * e, h = cs_h + e);
}
// the screwed plate's seal: E is the region the groove runs round (print_params.py: the rectangle through the screw rows
// less a disc round each screw and the cuts under the back plate's hinge notches), its corners rounded by sg_round; the
// groove is the band sg_w wide centred on E's edge
module seal_E(face) {
  sl = wl(face, 11); r = sl[0];
  offset(r = -sg_round) offset(r = 2 * sg_round) offset(r = -sg_round) difference() {
    translate([r[0], r[2]]) square([r[1] - r[0], r[3] - r[2]]);
    for (d = sl[1]) translate([d[0], d[1]]) circle(r = d[2], $fn = 90);
    for (c = sl[2]) translate([c[0], c[2]]) square([c[1] - c[0], 100]);
  }
}
// the printed TPU gasket for the lid (stl/gasket_lid.stl), in its print pose, which is also how it lies in the tub's rim
// groove (lip up): the plate gasket's section in the 3 mm groove: a base strip gasket_clr narrower each side and one lip
// from its inner edge (towards the box's inside) leaning out to gasket_tip from its outer edge; the latched lid's tongue
// stands groove_d + seal_gap - tongue_h (1.7 mm) over the groove's floor and bends it over (README "Seal (the lid)")
module rim_band(a, b) { ring2d(-land_w / 2 + b, -land_w / 2 + a); }   // a < b: across the groove from its centre line, + outward
module gasket_lid() {
  h = groove_w / 2 - gasket_clr; n = round((gasket_h - gasket_base) / 0.2);
  linear_extrude(gasket_base) rim_band(-h, h);
  for (i = [0 : n - 1]) {
    a = -h + 0.02 + (2 * h - 0.02 - gasket_lip - gasket_tip_lid) * i / (n - 1);
    translate([0, 0, gasket_base + i * 0.2 - e]) linear_extrude(0.2 + (i < n - 1 ? 2 : 1) * e)
      intersection() { rim_band(-h, h); rim_band(a, a + gasket_lip); }
  }
}
module seal_band(face, a, b) { difference() { offset(r = b) seal_E(face); offset(r = a) seal_E(face); } }   // a < b, from E's edge
module seal_groove2d(face) { seal_band(face, -sg_w / 2, sg_w / 2); }
// the printed TPU gasket for a screwed plate, in its print pose (stl/gasket_*.stl, TPU 95A on its own: README "Sealing a
// screwed plate"): lay it in the groove of the plate as that printed (show face down), the way it printed (lip up). A base
// strip in the groove, gasket_clr narrower each side, and one lip from the base's inner edge (towards the windows) leaning
// out to gasket_tip from its outer edge (towards the plate's edge and the screws), in 0.2 mm layers, each within 0.25 mm of
// the one below: the wall bends it over as the plate goes down
module gasket(face) {
  h = sg_w / 2 - gasket_clr; n = round((gasket_h - gasket_base) / 0.2);
  mirror([0, 1, 0]) {   // as the plate's print pose (turned over about x)
    linear_extrude(gasket_base) seal_band(face, -h, h);
    for (i = [0 : n - 1]) {
      a = -h + 0.02 + (2 * h - 0.02 - gasket_lip - gasket_tip) * i / (n - 1);   // the lip's inner edge, from E's edge: the root at the base's inner edge
      translate([0, 0, gasket_base + i * 0.2 - e]) linear_extrude(0.2 + (i < n - 1 ? 2 : 1) * e)
        intersection() { seal_band(face, -h, h); seal_band(face, a, a + gasket_lip); }
    }
  }
}
module plate_text(face) { translate([0, 0, plate_t - inlay]) linear_extrude(inlay) for (l = wl(face, 3)) label2d(l); }

// ---------------------------------------------------------------- stand-ins for the assembled view
// the board (its parts as their keep-out boxes), each jack and the TRIG2 button, from the measured sizes
show_parts = true;          // the assembled view: show the board and the parts
C_TUB = "#2b2f33"; C_LID = "#cfd8dc"; C_PLATE = "#e6e6e6"; C_TEXT = "#ff9800"; C_LATCH = "#c62828";
C_COVER = "#8d6e63"; C_BOARD = "#1b5e20"; C_PART = "#546e7a"; C_JACK = "#1e1e1e"; C_METAL = "#b0bec5"; C_BUTTON = "#d32f2f";
module standins() {
  translate([0, -board_h, z_board]) color(C_BOARD) cube([board_w, board_h, board_t]);
  for (k = board_parts) color(C_PART) translate([k[0], k[1], z_board + k[2]]) cube([k[3] - k[0], k[4] - k[1], k[5] - k[2]]);
  for (f = ["front", "back"]) at_plate(f) for (c = wl(f, 4)) translate([c[2], c[3], mount == "inset" ? -inset_d : mount == "inline" ? -plate_t : 0]) jack(c);
  if (ac) translate([xo0, ac_gland[0], ac_gland[1]]) rotate([0, 90, 0]) {   // the gland: cap outside, locknut inside
    color(C_BUTTON) translate([0, 0, -14]) cylinder(d = ac_gland[4], h = 14, $fn = 6);
    color(C_BUTTON) translate([0, 0, wall]) cylinder(d = ac_gland[3], h = 5, $fn = 6);
    color(C_JACK) translate([0, 0, -60]) cylinder(d = 10, h = 120);   // the cord
  }
  translate([button_xy[0], button_xy[1], 0]) {
    color(C_BUTTON) translate([0, 0, z_lt]) cylinder(d = button_head_d, h = 2);
    color(C_METAL) translate([0, 0, z_lu - button_depth]) cylinder(d = button_body_d, h = button_depth);
    color(C_METAL) translate([0, 0, z_lu - 2]) cylinder(d = 14, h = 2, $fn = 6);
  }
}
module jack(c) {   // in the plate frame: z out of the wall
  if (c[1] == "outlet") outlet_standin(); else jack_(c);
}
module outlet_standin() {   // the receptacle: faces through the wall, strap on its inside face, body behind
  for (s = [-1, 1]) color("#f5f0e1") translate([s * outlet_face_pitch / 2 - outlet_face_h / 2, -outlet_face_w / 2, -wall - 1 + acs]) cube([outlet_face_h, outlet_face_w, outlet_face_proud + 1]);
  translate([0, 0, acs]) {
    color(C_METAL) translate([-outlet_strap_l / 2, -outlet_strap_w / 2, -wall - 1]) cube([outlet_strap_l, outlet_strap_w, 1]);
    color("#f5f0e1") translate([-outlet_body_l / 2, -outlet_body_w / 2, -wall - 1 - outlet_body_d]) cube([outlet_body_l, outlet_body_w, outlet_body_d]);
  }
}
module jack_(c) {
  d = c[1] == "usb" ? usb_hole_d : c[1] == "j35p" ? j35p_hole_d : dcp_hole_d;
  w = c[1] == "usb" ? 16 : c[1] == "j35p" ? j35p_behind_d + 2 : dcp_behind_d + 2;
  color(C_METAL) cylinder(d = d, h = plate_t + 3);
  color(C_JACK) translate([-w / 2, -w / 2, -22]) cube([w, w, 22]);
}

// ---------------------------------------------------------------- output
module lid_pose() { translate([0, 0, z_lt]) rotate([180, 0, 0]) children(); }
module tub_done() {
  difference() { tub(); if (!LS && hinge == "pin") knuckle_gap_cut([1]); }
  if (mount == "inset") for (f = ["front", "back"]) at_plate(f) break_ribs(f);
}
// screw zones: the volumes round every insert and screw, for a slicer modifier (more walls, solid infill) so the print can
// keep few walls and little infill elsewhere. Overlapping solids, never printed: tools/make_project3mf.py adds them to the
// tub and the lid as modifiers. In each part's print pose.
module tub_zones() {
  if (SCREWED) for (f = ["front", "back"]) for (sc = wl(f, 5)) at_plate(f)
    translate([sc[0], sc[1], -insert_depth - 0.9]) cylinder(d = boss_w - 1, h = insert_depth + 0.9 + e);   // the plate screws' nuts or inserts, through the wall into the boss
  for (h = board_holes) translate([h[0], h[1], z_floor - e]) cylinder(d = 9, h = standoff_h + e);   // the board standoffs' inserts
  for (p = feet_xy()) translate([p[0], p[1], 0]) cylinder(d = feet_boss_d, h = z_floor + feet_boss_h);   // the feet's screws
  if (LS) for (q = lid_ears) at_wall(q[0], q[1]) lid_ear();   // the lid screws' ears
  else {
    for (l = latches) if (latch_style == "draw") at_wall(l[0], l[1]) dl_ears(); else at_latch(l) latch_ears();   // the latch pivots
    if (hinge == "pin") hinge_tub(); else everbilt_tub();
  }
  if (ac) ac_cover_bosses();
}
module lid_zones() {
  if (LS) for (q = lid_ears) at_wall(q[0], q[1]) lid_boss();   // the lid screws' bosses
  else {
    if (hinge == "pin") hinge_lid(); else everbilt_lid();
    if (latch_style == "cam") for (l = latches) at_latch(l) latch_lugs();   // the keepers
  }
}

// the assembled view's options: show_lid = false for a look inside; cut_x for a section (everything left of it cut away)
show_lid = true;
show_cover = true;          // AC case: the mains-bay cover
cut_x = -1e9;
module cutaway() {   // colour it from outside (color() cutaway() ...): the cut faces then keep the part's colour
  if (cut_x > -1e8) difference() { children(); translate([-60, -200, -10]) cube([60 + cut_x, 300, 100]); } else children(); }
if (part == "assembly") {
  color(C_TUB) cutaway() tub_done();
  if (show_lid) color(C_TEXT) cutaway() translate([0, 0, z_lt - inlay]) linear_extrude(inlay) lid_text2d();
  if (PLATE) for (f = ["front", "back"]) { color(C_PLATE) cutaway() at_plate(f) translate([0, 0, mount == "inset" ? -inset_d : 0]) plate(f); color(C_TEXT) cutaway() at_plate(f) translate([0, 0, mount == "inset" ? -inset_d : 0]) plate_text(f); }
  if (!LS) for (l = latches) color(C_LATCH) cutaway() at_wall(l[0], l[1]) if (latch_style == "draw") dl_parts_closed(); else rotate([90, 0, 90]) translate([0, 0, -latch_w / 2]) linear_extrude(latch_w) latch2d();
  if (show_parts) cutaway() standins();
  if (ac && show_cover) { color(C_COVER) cutaway() ac_cover(); color(C_TEXT) cutaway() translate([0, 0, ac_cover_z + ac_cover_t - inlay]) linear_extrude(inlay) ac_cover_text2d(); }
  if (show_lid) color(C_LID, 0.45) cutaway() lid();   // see-through: drawn last, or OpenSCAD hides what's behind it
} else if (part == "tub") tub_done();
else if (part == "lid") lid_pose() lid();
else if (part == "zones_tub") tub_zones();
else if (part == "zones_lid") lid_pose() lid_zones();
else if (part == "lid_text") lid_pose() translate([0, 0, z_lt - inlay]) linear_extrude(inlay) lid_text2d();
else if (part == "latch") latch_print();
else if (part == "dl_lever") linear_extrude(dl_lever_t) dl_lever2d();
else if (part == "dl_hook") linear_extrude(dl_plate_t) dl_hook2d();   // print two
else if (part == "dl_pin1") dl_pin_print(2 * (dl_xl + dl_ear_t));   // the lever's: through the ears and the lever
else if (part == "dl_pin2") dl_pin_print(2 * (dl_xp + dl_plate_t));   // the hook's: through the plates and the lever
else if (part == "coupon_latch_wall") {   // the draw latch's fit test (latch_style = "draw"): a piece of the left wall round the first
  l = latches[0]; bx = [xo0 - 24, l[1] - 16, 0]; bs = [wall + 36, 32, z_top + 1];   // latch, standing on a strip of the floor
  translate(-bx) intersection() { tub_done(); translate(bx) cube(bs); }
} else if (part == "coupon_latch_lid") {   // and that end of the lid
  l = latches[0]; bx = [xo0 - 24, l[1] - 16]; bs = [wall + 36, 32];
  translate([-bx[0], -bx[1], 0]) lid_pose() intersection() { lid(); translate([bx[0], bx[1], 0]) cube([bs[0], bs[1], 200]); }
}
else if (part == "cover") translate([0, 0, ac_cover_z + ac_cover_t]) rotate([180, 0, 0]) ac_cover();
else if (part == "cover_text") translate([0, 0, ac_cover_z + ac_cover_t]) rotate([180, 0, 0]) translate([0, 0, ac_cover_z + ac_cover_t - inlay]) linear_extrude(inlay) ac_cover_text2d();
else if (part == "plate_front") print_pose(plate_t) plate("front");
else if (part == "plate_front_text") print_pose(plate_t) plate_text("front");
else if (part == "plate_back") print_pose(plate_t) plate("back");
else if (part == "plate_back_text") print_pose(plate_t) plate_text("back");
else if (part == "gasket_front") gasket("front");
else if (part == "gasket_back") gasket("back");
else if (part == "gasket_lid") gasket_lid();
else if (part == "coupon_seal") {   // the left wall's middle lid screw (or the first latch): a piece of the wall and rim, and of the lid
  l = LS ? [for (q = lid_ears) if (q[0] == "left") q][0] : latches[0]; bx = [xo0 - 18, l[1] - 20, z_top - 28]; bs = [land_w + 22, 40, 40];
  translate(-bx) intersection() { tub_done(); translate(bx) cube(bs); }
  translate([bs[0] + 8 - bx[0], 0, 0]) lid_pose() intersection() { lid(); translate([bx[0], bx[1], 0]) cube([bs[0], bs[1], 200]); }
} else if (part == "coupon_mount") {   // the front wall's right-hand end, and that corner of the plate with its pin
  x1 = front_x1; y = pfy("front"); wn = front_windows[len(front_windows) - 1];
  xa = mount == "inset" ? front_breaks[len(front_breaks) - 1] : front_windows[len(front_windows) - 2][1];   // from the last break-out rib, or the rib before the last window (a cut through an opening would leave its top on air)
  bx = [xa, yo_f - 1, z_row + y[0] - 2]; bs = [x1 + 3 - xa, wall + boss_len + 1.5, y[1] - y[0] + 4];
  translate(-bx) intersection() { tub_done(); translate(bx) cube(bs); }
  translate([0, -12 - (y[1] - y[0]), 0]) translate([-xa, -y[0], 0]) print_pose(plate_t) intersection() { plate("front"); translate([xa, y[0] - 1, -pin_l - 1]) cube([x1 + 1 - xa, y[1] - y[0] + 2, pin_l + plate_t + 2]); }
}
