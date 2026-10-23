// Words a beginner may not know. The interactive page underlines the first use in each step (tap for the meaning);
// the written pages list the ones a guide uses under "Words in this guide".
export const GLOSSARY = [
  ['silkscreen', /\bsilkscreen\b/i, 'The white printing on the board that names each terminal and screw.'],
  ['terminal', /\bscrew terminals?\b/i, 'A block with screws on the board edge. Push a bare wire end in under the screw and tighten it.'],
  ['TRS', /\b(TRS|stereo plug|stereo cable)\b/, 'A 3.5 mm plug with three parts: tip, ring and sleeve (headphone plugs are this kind). A mono plug has only two.'],
  ['tip, ring, sleeve', /\b(tip|ring|sleeve)\b/, 'The three metal parts of a 3.5 mm stereo plug: tip at the end, ring in the middle, sleeve nearest the handle. Screw adapters often mark them L, R and G (or −).'],
  ['COM, NO, NC', /\b(COM|NO|NC)\b/, 'The three contacts of a switch or relay. COM (common) joins NO (normally open) only while it is pressed or on, and joins NC (normally closed) the rest of the time.'],
  ['polarity', /\bpolarity\b|\bplus and minus\b/i, 'Which wire is plus (+) and which is minus (−). Some parts work only one way round, and some are damaged backwards.'],
  ['centre positive', /\bcent(re|er)[ -]positive\b/i, 'On a round barrel plug, the inside (centre) is + and the outside is −. The supply shows it with a little symbol: (−)–C–(+).'],
  ['listed supply', /\blisted\b/i, 'A power supply tested by a safety lab: it carries a UL, ETL or CSA mark on the product itself.'],
  ['over-current protection', /\bover-?current protection\b|\boverload protection\b|\bshuts itself off on overload\b/i, 'The supply switches itself off when too much current flows. A backstop, not a fuse you can count on.'],
  ['flyback diode', /\bdiode\b/i, 'A small part that lets current flow one way only. Across a coil it soaks up the spike when the coil switches off. Its stripe end is the cathode and goes to +.'],
  ['capacitor', /\bcapacitor\b/i, 'A part that stores a little energy and smooths the supply. The big round kind has a − stripe down its side.'],
  ['µF', /µF/, 'Microfarad: the size of a capacitor.'],
  ['Ω', /Ω/, 'Ohm: the unit of resistance. A speaker’s ohms tell the board how hard it is to drive.'],
  ['AWG', /\bAWG\b/, 'American Wire Gauge, the thickness of a wire. A smaller number is a thicker wire.'],
  ['DIN', /\bDIN\b/, 'Data in: the end of a pixel strip where the data goes in. The little arrows on the strip point away from it.'],
  ['active low', /\bgoes high\b|\bpull(s)? low\b|\bdrives? low\b/i, 'Inputs read a voltage. A button pulls the input low (to ground). A powered sensor drives it high when it sees someone.'],
  ['dry contact', /\bdry contact\b|\bisolated switch\b/i, 'A switch with no voltage of its own, like a finger pressing a button.'],
  ['multimeter', /\b(multimeter|meter)\b/i, 'A tester that measures volts. Set it to DC V (a V with a straight line), red lead in V, black lead in COM.'],
  ['DMX', /\bDMX\b/, 'A standard way for show controllers to talk to lights and fog machines over a 3-pin (or 5-pin) XLR cable.'],
  ['hotspot', /\bhotspot\b/i, 'A Wi-Fi network the board makes by itself, so your phone can join it without your home Wi-Fi.'],
];

export function termsIn(text) {
  return GLOSSARY.filter(([, re]) => re.test(text)).map(([name, , def]) => ({ name, def }));
}
