// Guide 2: a single-colour 12 V lamp or LED strip on OUT1. OUT facts (hardware/README.md): high-side switched +VIN,
// 5 A each continuous at 12-24 V, sum <= 10 A (barrel <= 5 A); PWM <= 2 A at 12 V, <= 1 A at 24 V. A full 5 m reel of
// 60 LED/m 5050 strip draws about 6 A at 12 V, more than one OUT. The case's OUT barrel jacks have no defined polarity
// yet (enclosure, owner): the guide measures it or uses the bare terminal.
import { view } from '../scene/view.js';
import { intro, need, before, tug, trouble, next } from './template.js';
import { outRow } from '../player/insets.js';

export default {
  id: '02', slug: 'lamp-on-out1', title: 'A 12 V lamp or LED strip on OUT1',
  posterStep: 7, time: '10 minutes', level: 'screwdriver',
  blurb: 'Wire a single-colour 12 V LED strip or lamp to OUT1 so the board can switch it on, off and dim it.',
  need: [
    'A single-colour 12 V LED strip with two wires (+ and −), or a 12 V lamp: it must match your supply voltage',
    'Red and black wire, 18 AWG (about 1 mm²) for up to 5 A',
    'A small flat screwdriver and wire strippers',
    'In the case: a lead with a 5.5 x 2.1 mm barrel plug, and a multimeter to check which pin is +',
  ],
  build(k) {
    k.add('strip', k.props.ledStrip({ length: 180 }), { pos: [55, 1, 165], pins: { plus: '+12V', minus: '−' }, label: '12 V LED strip', labelAt: [90, 12, 0] });
    k.wire('plus', k.at('OUT1', '1'), k.at('strip', 'plus'), { color: 'red', label: 'red: strip + to OUT1 +', leadA: 12 });
    k.wire('minus', k.at('OUT1', '2'), k.at('strip', 'minus'), { color: 'black', label: 'black: strip − to OUT1 −', leadA: 12, labelT: 0.35 });
    k.add('caseplug', k.props.barrelPlug(), { pos: k.plateJack('front', 67.11).add({ x: 0, y: 0, z: 2 }).toArray(), rot: [0, Math.PI, 0], from: [0, 0, 60], label: 'barrel plug lead in OUT1', labelAt: [0, 12, -8] });
  },
  steps: [
    intro(view([90, 10, 100], 290, 15, 40), 'In this guide you will wire a twelve volt LED strip to the output called OUT1, so the board can switch it on and off and dim it. A twelve volt lamp is wired the same way. It takes about ten minutes.', { title: 'A lamp on OUT1', focus: ['OUT1'] }),
    need(view([110, 5, 160], 300, 10, 40), 'You need a single colour twelve volt LED strip with two wires, or a twelve volt lamp, some red and black wire, and a small screwdriver.', { show: ['strip'] }),
    before(view([100, 10, 120], 260, 15, 40), ['No lamp that plugs into the wall', 'Never across another device’s button or remote', 'No colour-changing (4-wire) strip on one OUT'],
      { extra: 'Outputs give power, not a button press. Never wire an output across another device’s button, and never to anything that plugs into the wall.' }),
    { kind: 'check', title: 'Check your strip', say: 'The strip must run on the same voltage as your supply, usually twelve volts. One output can carry up to five amps: about four metres of a typical strip. A whole five metre reel is too much for one output, so split it over two. To dim it, keep it under two amps, about one and a half metres.',
      text: 'The strip must run on the same voltage as your supply, usually 12 V. One output can carry up to 5 A: about 4 m of a typical 60 LED/m strip (check its label: a full 5 m reel draws about 6 A, so split it over two outputs). To dim it, keep it under 2 A at 12 V (about 1.5 m), or under 1 A on a 24 V supply.',
      cam: view([110, 5, 160], 300, 10, 40), show: ['strip'] },
    { kind: 'find', only: 'bare', title: 'Find OUT1', say: 'Find OUT1 on the front edge. It has two screws. The silkscreen reads plus, OUT1, minus. Plus is on the left.',
      cam: view([83, 15, 80], 150, 10, 35), focus: ['OUT1'] },
    { kind: 'find', only: 'case', title: 'Find OUT1', say: 'In the case, OUT1 is the first round power jack on the front plate. Which pin is plus depends on how your case was wired. Unless the plate is marked, check it with a meter: switch OUT1 on from the page, red probe in the centre, black on the outside. Twelve means the centre is plus. Then press Auto. No meter? Open the lid and use the screws instead.',
      text: 'In the case, OUT1 is the first round power jack on the front plate (5.5 x 2.1 mm). Which pin is + depends on how your case was wired. Unless the plate is marked, check it with a meter: on the page, Live > Outputs > OUT1 > On; red probe in the centre, black on the outside; 12 means the centre is +. Then press Auto. No meter? Open the lid and use the OUT1 screws instead.',
      cam: view([70, 25, 125], 260, 20, 20), case: 'closed', hide: ['strip'], show: ['caseplug'], inset: { type: 'meter', what: 'plug', title: 'Which pin is +?' } },
    { kind: 'wire', title: 'Red to plus', say: 'Strip about seven millimetres off each wire and twist the strands. Put the strip’s plus wire, usually red, under the plus screw, and tighten it.',
      cam: view([80, 10, 115], 210, 30, 40), draw: ['plus'], focus: ['OUT1'] },
    { kind: 'wire', title: 'Black to minus', say: 'Put the minus wire, usually black, under the minus screw. Plus to plus, minus to minus.',
      cam: view([80, 10, 115], 210, 30, 40), draw: ['minus'], focus: ['OUT1'] },
    tug(view([83, 15, 85], 130, -30, 30)),
    { kind: 'test', title: 'Test it', say: 'Power the board and open its page on your phone. On the Live tab, in the Outputs card, press On next to OUT1. The small green light by OUT1 comes on, and so does the strip. Then press Auto, so your shows can use it again.',
      cam: view([100, 10, 120], 260, 15, 40), live: { OUT1: 1, strip: 1 },
      inset: { type: 'phone', tab: 'Live', cardTitle: 'Outputs', body: outRow('OUT1', 'On'), note: 'On lights the strip. Then press <b>Auto</b>: On holds OUT1 until Auto or STOP.' } },
    { kind: 'show', title: 'Use it in a show', text: 'In a show script, `out1 = on` turns it on, `out1 = off` turns it off, `out1 = 50%` dims it to half, and `flicker out1 for 5` makes a failing-bulb flicker.',
      say: 'In a show script, out 1 equals on turns it on, out 1 equals off turns it off, and out 1 equals fifty percent dims it to half.',
      cam: view([100, 10, 120], 260, 15, 40), live: { OUT1: 0.5, strip: 0.5 } },
    trouble(view([100, 10, 120], 260, 15, 40)),
    next(view([90, 10, 100], 290, 15, 40)),
  ],
  trouble: [
    'The green light by OUT1 comes on but the strip stays dark: plus and minus are swapped, or a screw grips insulation instead of copper.',
    'Nothing lights at all: did you press On for OUT1 (not OUT2)? Does the board have power?',
    'The strip is dim at the far end or flickers: a long strip needs thicker wire, or a feed at both ends.',
    'The supply shuts off, especially with an old-style bulb (halogen): a cold bulb draws many times its rating for a moment. Keep each output under 5 A and the whole board under 10 A.',
  ],
  next: ['07', '05', '08'],
};
