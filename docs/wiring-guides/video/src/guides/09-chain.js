// Guide 9: chaining two boards, CONTACT1 (CC1 on the phone page) of one into TRIG1 of the next. CONTACT facts
// (hardware/README.md): isolated opto closure on 3.5 mm, tip +, sleeve −, ring NC; <= 30 V DC, count on about 2 mA;
// TRIG1 sinks about 0.7 mA. cc1_role defaults to trigger_repeat (a pulse for each accepted trigger, made for chaining);
// "shows" lets a script drive it. A press that only a script waits for does not pulse a role (device.cpp), so the guide
// sets cc1_role = shows. Board two's TRIG1 input type must be "switch". Back plate: CC2 sits left of CC1.
import { view } from '../scene/view.js';
import { intro, need, before, trouble, next } from './template.js';
import { chips, setting } from '../player/insets.js';

export default {
  id: '09', slug: 'chaining-two-boards', title: 'Chaining two boards',
  posterStep: 5, time: '5 minutes', level: 'none',
  blurb: 'One board starts the next: a plain 3.5 mm stereo cable from CONTACT1 to TRIG1.',
  need: ['A second Proptroller (or another controller with a trigger input)', 'A 3.5 mm stereo (TRS) male-to-male cable, as long as you need (not a mono cable)'],
  build(k) {
    const b2 = k.board2([30, 0, 230], 0);
    const c1 = k.at('CONTACT1');
    k.add('plugA', k.props.trsPlug(), { pos: c1.pos.clone().addScaledVector(c1.dir, 1).toArray(), from: [0, 0, -50], label: 'board 1: CONTACT1', labelAt: [0, 12, -10] });
    const t1 = b2.at('TRIG1');
    k.add('plugB', k.props.trsPlug(), { pos: t1.pos.clone().addScaledVector(t1.dir, 1).toArray(), rot: [0, Math.PI, 0], from: [0, 0, 50], label: 'board 2: TRIG1', labelAt: [0, 12, -10] });
    k.wire('cable', k.at('plugA', 'cable'), k.at('plugB', 'cable'), { color: 'black', r: 1.6, lead: 20, via: [[42, 6, -60], [-40, 6, -40], [-40, 6, 330], [42, 6, 340]], label: '3.5 mm stereo cable' });
    k.note('b1', 'BOARD 1', [67, 40, 40]); k.note('b2', 'BOARD 2', [97, 40, 270]);
  },
  always: ['board2', 'b1', 'b2'],
  steps: [
    intro(view([80, 10, 150], 560, 30, 45), 'In this guide, one board starts another, for example a prop in the next room. Board one’s CONTACT1 output presses board two’s TRIG1 input. It takes five minutes and no tools.', { title: 'Board to board' }),
    need(view([43, 14, -20], 170, 190, 30), 'You need a second board, and an ordinary three and a half millimetre stereo cable with a plug at each end.', { show: ['plugA'] }),
    before(view([80, 10, 150], 560, 30, 45), ['No lamp or motor on CONTACT', 'Never more than 30 V DC', 'Not a mono cable'],
      { extra: 'CONTACT1 is for small signals only: thirty volts D C and a few milliamps at most. Never a lamp, never a motor.' }),
    { kind: 'find', only: 'bare', title: 'What CONTACT1 is', say: 'CONTACT1 is the round jack on the back edge, next to TRIG2. It is an isolated switch: tip is plus, sleeve is minus. On the phone page it is called C C 1.',
      text: 'CONTACT1 is the round jack on the back edge, next to TRIG2. It is an isolated switch: tip is +, sleeve is −. On the phone page it is called CC1.',
      cam: view([43, 14, 0], 160, 200, 35), focus: ['CONTACT1'] },
    { kind: 'find', only: 'case', title: 'In the case', say: 'In the case, CONTACT1 is the jack marked CC1 on the back plate. Looking at the back, CC2 sits to its left. Board two’s TRIG1 is the jack on its front plate.',
      text: 'In the case, CONTACT1 is the jack marked CC1 on the back plate (looking at the back, CC2 sits to its left). Board two’s TRIG1 is the jack on its front plate.',
      cam: view([45, 25, -45], 250, 190, 20), case: 'closed' },
    { kind: 'wire', title: 'Plug in both ends', say: 'With both boards unplugged, push one end of the stereo cable into CONTACT1 on board one, and the other end into TRIG1 on board two. A straight stereo cable joins tip to tip and sleeve to sleeve, which is just right.',
      cam: view([60, 10, 150], 520, 300, 45), show: ['plugA', 'plugB'], draw: ['cable'] },
    { kind: 'tell', title: 'Board two: a switch input', say: 'On board two’s page, open Settings. In the Inputs card, check that TRIG1 input type is switch. Save settings if you changed it.',
      text: 'On board two’s page: Settings > Inputs > TRIG1 input type must be "switch" (sensor_high would fire the wrong way round). Save settings if you changed it.',
      cam: view([80, 10, 150], 560, 30, 45), inset: { type: 'phone', tab: 'Settings', cardTitle: 'Inputs', body: setting('TRIG1 input type', 'switch'), note: 'Board two: <b>switch</b>.' } },
    { kind: 'tell', title: 'Board one: CC1 for shows', say: 'On board one’s page, open Settings. In the Output roles card, set C C 1 role to shows, and press Save settings. Now board one’s shows and script can switch it.',
      text: 'On board one’s page: Settings > Output roles > "CC1 role" = shows > Save settings. Now board one’s shows and script can switch it.',
      cam: view([80, 10, 150], 560, 30, 45), inset: { type: 'phone', tab: 'Settings', cardTitle: 'Output roles', body: setting('CC1 role', 'shows'), note: 'Board one: <b>shows</b>.' } },
    { kind: 'test', title: 'Test it', say: 'Power both boards. On board one’s Live tab, in the Outputs card, press On next to C C 1. On board two’s Live tab, the TRIG1 box lights up. Then press Auto on board one.',
      text: 'Power both boards. On board one’s Live tab, in the Outputs card, press On next to CC1: on board two’s Live tab, the TRIG1 box lights up. Then press Auto on board one.',
      cam: view([80, 10, 150], 560, 30, 45), inset: { type: 'phone', tab: 'Live', cardTitle: 'Inputs (board two)', body: chips(['TRIG1', 'TRIG2', 'USER1', 'USER2', 'USER3'], 'TRIG1'), note: 'Board one: CC1 <b>On</b>. Board two: TRIG1 lights. Then <b>Auto</b> on board one.' } },
    { kind: 'show', title: 'Use it in a show', say: 'In board one’s script, cc 1 equals on, sleep half a second, cc 1 equals off, presses board two’s TRIG1. Board two waits for trig 1 as usual.',
      text: 'In board one’s script, `cc1 = on`, `sleep 0.5`, `cc1 = off` presses board two’s TRIG1. Board two waits with `wait for trig1` as usual.',
      cam: view([80, 10, 150], 560, 30, 45) },
    trouble(view([80, 10, 150], 560, 30, 45)),
    next(view([80, 10, 150], 560, 30, 45)),
  ],
  trouble: [
    'Board two’s TRIG1 never lights: is the cable stereo and pushed in at both ends? Is board one’s CC1 role set to shows (out of the box it is trigger_repeat, which pulses only when a show, not a script, starts)?',
    'TRIG1 on board two is lit all the time: its TRIG1 input type is sensor_high. Set it to switch.',
    'Swapped plugs: CONTACT1 has polarity. A straight stereo cable keeps tip to tip; a cable that swaps them will not work.',
    'Another brand of controller: its trigger input must want a contact closure; tip of CONTACT1 is +, so match its + and −.',
  ],
  next: ['01', '03', '00'],
};
