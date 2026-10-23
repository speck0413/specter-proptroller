// Guide 7: a DC 12 V air valve (solenoid valve) on OUT1, the usual jump-scare part, with a diode across its coil.
// OUT facts (hardware/README.md, board.ts): switched +VIN, 5 A continuous at 12-24 V; each OUT already has an SS56
// freewheel diode on the board, so the diode at the valve is for long leads; backwards it shorts the output and the
// board has no fuse. Valve coils come as DC12V, DC24V, AC110V, AC220V: only DC 12 V (or DC 24 V on a 24 V supply).
// Watchdog: Settings > Watchdogs > "OUT1 max on" (ms, 0 = no limit). The case's OUT jacks have no defined polarity yet.
import { view } from '../scene/view.js';
import { intro, need, before, tug, trouble, next } from './template.js';
import { outRow } from '../player/insets.js';

export default {
  id: '07', slug: 'air-valve-on-out', title: 'A 12 V air valve on OUT1',
  posterStep: 7, time: '15 minutes', level: 'screwdriver+solder',
  blurb: 'Wire a DC 12 V air valve (or a push solenoid) to OUT1 for a jump scare, with a diode across its coil.',
  need: [
    'An air valve (solenoid valve) whose coil says DC 12V (or DC 24V on a 24 V supply), drawing under 5 A. Never a coil marked AC',
    'A diode across the coil. Easiest: a valve or coil plug sold with the diode already fitted ("with suppression diode" or "with LED and diode")',
    'If yours has no diode: a 1N4007 (or 1N5819, SS54), a soldering iron and solder, and heat-shrink tube',
    'Red and black wire, 18 AWG, a small flat screwdriver and wire strippers',
    'A multimeter (to check the case’s OUT jack, or unmarked leads)',
  ],
  build(k) {
    k.add('sol', k.props.airValve(), { pos: [100, 0, 175], pins: { plus: '+', minus: '−' }, label: 'DC 12 V air valve', labelAt: [0, 48, 0] });
    k.note('stripe', 'diode stripe on the + side', [80, 45, 172]);
    k.wire('plus', k.at('OUT1', '1'), k.at('sol', 'plus'), { color: 'red', label: 'red: + to OUT1 +', leadA: 12 });
    k.wire('minus', k.at('OUT1', '2'), k.at('sol', 'minus'), { color: 'black', label: 'black: − to OUT1 −', leadA: 12, labelT: 0.35 });
    k.add('caseplug', k.props.barrelPlug(), { pos: k.plateJack('front', 67.11).add({ x: 0, y: 0, z: 2 }).toArray(), rot: [0, Math.PI, 0], from: [0, 0, 60], label: 'barrel plug lead in OUT1', labelAt: [0, 12, -8] });
  },
  steps: [
    intro(view([90, 10, 110], 280, 15, 40), 'In this guide you will wire a twelve volt air valve to OUT1, the usual part behind a jump scare. A push solenoid is wired the same way. It takes about fifteen minutes.', { title: 'Things that move', focus: ['OUT1'] }),
    need(view([100, 25, 175], 200, 20, 30), 'You need an air valve whose coil says D C twelve volts, and a diode across the coil. Easiest is a valve or coil plug sold with the diode already fitted.', { show: ['sol'] }),
    before(view([90, 10, 130], 300, 20, 40), ['Never a coil marked AC', 'No diode backwards: it shorts the output', 'Never across another device’s button'],
      { extra: 'The coil must say D C twelve volts. Check the diode’s stripe before power on: backwards, it shorts the output, and the board has no fuse.' }),
    { kind: 'check', title: 'Check the coil', say: 'Read the coil’s label. It must say DC 12V, like your supply, and draw less than five amps. Coils marked AC 110 or AC 220 are for wall power: never use them here.',
      text: 'Read the coil’s label. It must say DC 12V (or DC 24V on a 24 V supply) and draw less than 5 A. Coils marked AC110V or AC220V are for wall power: never use them here.',
      cam: view([95, 30, 175], 130, 20, 30), show: ['sol'] },
    { kind: 'wire', title: 'The diode', say: 'A coil kicks back a spike when it switches off. The board has a diode on each output, and a second one right at the coil protects long wires. The stripe on the diode goes to the plus side, the wire that goes to OUT1 plus.',
      text: 'A coil kicks back a spike when it switches off. The board has a diode on each output already; a second one right at the coil protects long wires. Fitted already? Check its stripe is on the + side (the wire that goes to OUT1 +). Not fitted? Solder a 1N4007 across the coil’s two terminals, stripe on +, and cover the joints with heat-shrink (about 10 minutes).',
      cam: view([86, 35, 172], 80, -55, 35), show: ['stripe'], inset: { type: 'diode' } },
    { kind: 'find', only: 'bare', title: 'Find OUT1', say: 'Find OUT1 on the front edge. The silkscreen reads plus, OUT1, minus. Plus is on the left.',
      cam: view([83, 15, 80], 150, 10, 35), focus: ['OUT1'] },
    { kind: 'find', only: 'case', title: 'Find OUT1', say: 'In the case, OUT1 is the first round power jack on the front plate. With a diode fitted, a lead the wrong way round shorts the output. So unless the plate is marked, check which pin is plus with a meter first, or open the lid and use the OUT1 screws.',
      text: 'In the case, OUT1 is the first round power jack on the front plate. With a diode fitted, a lead the wrong way round shorts the output. Unless the plate is marked, check it first: Live > Outputs > OUT1 > On, red probe in the centre, black outside; 12 means the centre is +; then Auto. Or open the lid and use the OUT1 screws.',
      cam: view([70, 25, 125], 260, 20, 20), case: 'closed', hide: ['sol'], show: ['caseplug'], inset: { type: 'meter', what: 'plug', title: 'Which pin is +?' } },
    { kind: 'wire', title: 'Wire it', say: 'The valve’s plus wire, the one on the diode’s stripe side, goes to the plus screw of OUT1. The minus wire goes to the minus screw.',
      cam: view([88, 10, 125], 220, 30, 40), draw: ['plus', 'minus'], hide: ['stripe'], focus: ['OUT1'] },
    tug(view([83, 15, 85], 130, -30, 30)),
    { kind: 'test', title: 'Test it', say: 'Power the board. On the Live tab, in the Outputs card, press On next to OUT1. The valve clicks, and the light by OUT1 comes on. Then press Auto.',
      cam: view([90, 10, 130], 300, 20, 40), live: { OUT1: 1, sol: 1 },
      inset: { type: 'phone', tab: 'Live', cardTitle: 'Outputs', body: outRow('OUT1', 'On'), note: 'On: the valve clicks. Then press <b>Auto</b>.' } },
    { kind: 'tell', title: 'A safety limit', say: 'Air valves may stay on. A push solenoid is made for short pulses and gets hot. For a solenoid, open Settings, Watchdogs, and set OUT1 max on to two thousand milliseconds, then Save settings. The board then switches it off after two seconds, whatever the show says.',
      text: 'Air valves may stay on. A push solenoid is made for short pulses and gets hot. For a solenoid: Settings > Watchdogs > "OUT1 max on" = 2000 (ms) > Save settings. The board then switches it off after 2 s, whatever the show says.',
      cam: view([90, 10, 130], 300, 20, 40) },
    { kind: 'show', title: 'Use it in a show', text: 'In a show script: `out1 = on`, `sleep 0.5`, `out1 = off` gives a half-second blast.',
      say: 'In a show script, out 1 equals on, sleep half a second, out 1 equals off, gives a half second blast.',
      cam: view([90, 10, 130], 300, 20, 40), live: { OUT1: 1, sol: 1 } },
    trouble(view([90, 10, 130], 300, 20, 40)),
    next(view([90, 10, 110], 280, 15, 40)),
  ],
  trouble: [
    'Nothing moves but the OUT1 light comes on: check the wires, and that the valve has air pressure.',
    'The supply shuts off the moment OUT1 switches on: the diode is backwards (or the case lead is reversed). Power off and turn it round.',
    'It buzzes or moves weakly: the supply is too small, or the wires are too thin.',
    'The board restarts when it fires: use a bigger supply, and check the diode is fitted.',
  ],
  next: ['10', '02', '08'],
};
