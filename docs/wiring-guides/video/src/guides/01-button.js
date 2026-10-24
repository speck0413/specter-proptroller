// Guide 1: a push button (or a step mat) on TRIG1. Buttons have two wires: either wire on either screw (owner).
// TRIG facts (hardware/README.md): tip = input, pulled up, active low; ring = 3.3 V through 220 ohm; sleeve = GND.
import { view } from '../scene/view.js';
import { intro, need, before, tug, trouble, next } from './template.js';
import { chips } from '../player/insets.js';

const TRS = (t, s) => ({ type: 'trs', wires: { ...(t ? { T: { color: 'yellow', label: 'button' } } : {}), ...(s ? { S: { color: 'black', label: 'button' } } : {}) } });

export default {
  id: '01', slug: 'button-on-trig1', title: 'A push button on TRIG1',
  posterStep: 7, time: '5 minutes', level: 'screwdriver',
  blurb: 'Wire a big push button (or a step mat) to the TRIG1 input and see the board notice the press.',
  need: [
    'A push button with two wires (an arcade button, a doorbell button) or a step mat',
    'A 3.5 mm stereo (TRS) plug with screw terminals, sold as a "3.5 mm male to 3 screw terminal adapter" (its screws are often marked L, R, G)',
    'Easiest of all: a button sold for props with a 3.5 mm stereo plug already on its cable. Then you skip the screws: plug it in and go to "Test it"',
    'A small flat screwdriver and wire strippers',
  ],
  build(k) {
    const m = k.at('TRIG1');                       // the jack's mouth at the board's front edge
    const plugAt = m.pos.clone().addScaledVector(m.dir, 1);
    k.add('adapter', k.props.trsAdapter(), { pos: plugAt.toArray(), rot: [0, Math.PI, 0], from: [0, 0, 70], pins: { T: 'T', R: 'R', S: 'S' }, label: '3.5 mm plug with screws', labelAt: [0, 14, -22] });
    k.add('button', k.props.arcadeButton(), { pos: [40, 0, 175], label: 'push button', labelAt: [0, 50, 0] });
    k.wire('tip', k.at('button', 'a'), k.at('adapter', 'T'), { color: 'yellow', label: 'button wire to T (tip)', via: [[36, 4, 150], [20, 4, 125]], leadA: 4, leadB: 8 });
    k.wire('sleeve', k.at('button', 'b'), k.at('adapter', 'S'), { color: 'black', label: 'other wire to S (sleeve)', via: [[44, 4, 152], [28, 4, 127]], leadA: 4, leadB: 8, labelT: 0.35 });
    k.add('caseplug', k.props.trsPlug(), { pos: k.plateJack('front', 5.29).add({ x: 0, y: 0, z: 1 }).toArray(), rot: [0, Math.PI, 0], from: [0, 0, 60], label: 'TRIG1 jack, front plate', labelAt: [0, 12, -10] });
  },
  steps: [
    intro(view([60, 10, 70], 290, 20, 40), 'In this guide you will wire a big push button to the input called TRIG1, and see the board notice the press. It takes about five minutes, and you only need a small screwdriver.', { title: 'A button on TRIG1', focus: ['TRIG1'] }),
    need(view([35, 10, 130], 260, 15, 35), 'You need a push button with two wires, and a three and a half millimetre stereo plug with screw terminals. A step mat, or any switch with two wires, is wired exactly the same way. Easiest of all is a prop button that comes with the plug already on its cable.', { show: ['button'] }),
    before(view([35, 15, 120], 260, 20, 40), ['No battery or power supply on TRIG1', 'Nothing on the R screw for a button', 'Never wall power near the jacks'],
      { extra: 'TRIG1 needs a switch that closes, nothing more. Never connect a battery, a power supply or an output to it.' }),
    { kind: 'find', only: 'bare', title: 'Find TRIG1', say: 'Find TRIG1. It is the round jack at the front left corner of the board.',
      cam: view([15, 14, 80], 170, 25, 35), focus: ['TRIG1'] },
    { kind: 'find', only: 'case', title: 'Find TRIG1', say: 'Your board is in the case, so use the jack marked TRIG1 on the front plate. The plug and the wiring are the same as on the bare board.',
      cam: view([30, 25, 120], 280, -25, 20), case: 'closed', show: ['caseplug'] },
    { kind: 'wire', title: 'Plug in', say: 'Push the stereo plug all the way into TRIG1, until it stops.',
      cam: view([15, 14, 95], 150, 25, 40), show: ['adapter'], hide: ['button'], focus: ['adapter'], inset: TRS(false, false) },
    { kind: 'wire', title: 'One wire to T', say: 'Strip about seven millimetres off each button wire and twist the strands. Put one wire under the screw marked T, and tighten it. On many plugs that screw is marked L instead.',
      text: 'Strip about 7 mm off each button wire and twist the strands. Put one wire under the screw marked T (tip), and tighten it. On many plugs that screw is marked L instead.',
      cam: view([25, 10, 135], 230, 50, 35), show: ['button'], draw: ['tip'], inset: TRS(true, false) },
    { kind: 'wire', title: 'Other wire to S', say: 'Put the other wire under the screw marked S, or G. Leave the R screw empty. A button has no plus or minus, so either wire can go on either screw.',
      text: 'Put the other wire under the screw marked S (sleeve), or G on many plugs. Leave the R screw empty. A button has no plus or minus, so either wire can go on either screw.',
      cam: view([25, 10, 135], 230, 50, 35), draw: ['sleeve'], inset: TRS(true, true) },
    tug(view([15, 10, 110], 180, -40, 30)),
    { kind: 'test', title: 'Test it', say: 'Power the board and open its page on your phone. Stay on the Live tab. Press the button. In the Inputs card, the TRIG1 box lights up orange while you hold it, and goes dark when you let go.',
      cam: view([35, 15, 120], 260, 20, 40), live: { button: 'press' },
      inset: { type: 'phone', tab: 'Live', cardTitle: 'Inputs', body: chips(['TRIG1', 'TRIG2', 'USER1', 'USER2', 'USER3'], 'TRIG1'), note: 'TRIG1 lights orange while you hold the button.' } },
    { kind: 'show', title: 'Use it in a show', text: 'In a show script, `wait for trig1` waits for your button. For example: `effect hello:`, then `wait for trig1`, `out1 = on`, `wait 2`, `out1 = off`. Or, on the Shows tab, pick TRIG1 under "Starts on".',
      say: 'In a show script, the line wait for trig 1 waits for your button. Or, on the Shows tab, pick TRIG1 under Starts on.',
      cam: view([35, 15, 120], 260, 20, 40) },
    trouble(view([35, 15, 120], 260, 20, 40)),
    next(view([60, 10, 70], 290, 20, 40)),
  ],
  trouble: [
    'Nothing lights on the page: push the plug in harder. It must go all the way in.',
    'Check the two wires are on T and S (L and G), not on R.',
    'Touch a short bare wire across the T and S screws: if TRIG1 lights, the button or its wires are the problem.',
    'TRIG1 is lit all the time: a stray strand joins T and S, or the button is the kind that is closed until pressed. If your switch has three tabs, use the ones marked C (or COM) and NO.',
  ],
  next: ['03', '02', 'kit'],
};
