// Guide 5: a pixel strip on PIXEL, and when it needs its own 5 V supply. PIXEL facts (hardware/README.md, board.ts):
// 5 V, <= 1 A from the board (about 15 WS2812 at full white); the data line already has a 330 ohm series resistor
// (R_PXS), so no resistor goes at the strip; NO bulk capacitor on the board: 470-1000 uF, >= 6.3 V across 5V / GND at
// the strip input. Plug or unplug only with power off. The case has no opening for PIXEL.
import { view } from '../scene/view.js';
import { intro, need, before, tug, trouble, next } from './template.js';

export default {
  id: '05', slug: 'pixel-strip', title: 'A pixel strip on PIXEL',
  posterStep: 7, time: '20 minutes', level: 'screwdriver+solder',
  blurb: 'Wire a 5 V WS2812 (addressable RGB) pixel strip to PIXEL, and give a longer strip its own 5 V supply.',
  need: [
    'A 5 V WS2812 (addressable RGB) strip. Easiest: one with a 3-pin plug lead already fitted at its input end, plus a matching lead with bare wire ends',
    'A capacitor of 470-1000 µF, 6.3 V or more',
    'A small 3-way screw terminal block (or three lever connectors) to join the leads at the strip, with the capacitor’s legs',
    'Red, green and black wire, a small flat screwdriver and wire strippers',
    'Only for a strip with bare pads (no plug lead): a soldering iron and solder',
    'For more than about 15 lights: a listed 5 V supply big enough for the strip (about 60 mA per light at full white) and a barrel-to-screw adapter',
  ],
  build(k) {
    k.add('pixels', k.props.pixelStrip({ count: 15 }), { pos: [-30, 1, -25], rot: [0, Math.PI, 0], pins: { v5: '5V', din: 'DIN', gnd: 'GND' }, label: 'pixel strip, 5 V (WS2812)', labelAt: [130, 14, 0] });
    k.wire('c1', k.at('pixels', 'capP'), k.at('pixels', 'v5'), { color: 'grey', lead: 2, sag: 0, r: 0.4 });
    k.wire('c2', k.at('pixels', 'capN'), k.at('pixels', 'gnd'), { color: 'grey', lead: 2, sag: 0, r: 0.4, label: 'capacitor: stripe to GND' });
    k.wire('v5', k.at('PIXEL', '1'), k.at('pixels', 'v5'), { color: 'red', label: '5V to 5V', leadA: 10, sag: 2 });
    k.wire('data', k.at('PIXEL', '2'), k.at('pixels', 'din'), { color: 'green', label: 'DATA to DIN', leadA: 10, sag: 2 });
    k.wire('gnd', k.at('PIXEL', '3'), k.at('pixels', 'gnd'), { color: 'black', label: 'GND to GND', leadA: 10, sag: 2, labelT: 0.3 });
    // a longer strip: its own 5 V supply
    k.add('psu5', k.props.powerBrick(), { pos: [-120, 15, -170], label: '5 V supply for the strip', labelAt: [0, 24, 0] });
    k.add('jack5', k.props.barrelAdapter(), { pos: [-110, 5, -100], pins: { plus: '+', minus: '−' } });
    k.wire('cord5', k.at('psu5', 'cord'), k.at('jack5', 'mouth'), { color: 'black', r: 1.8, lead: 10 });
    k.wire('p5', k.at('jack5', 'plus'), k.at('pixels', 'v5'), { color: 'red', label: 'supply + to strip 5V', leadA: 10 });
    k.wire('m5', k.at('jack5', 'minus'), k.at('pixels', 'gnd'), { color: 'black', label: 'supply − to strip GND', leadA: 10, labelT: 0.6 });
    k.note('nokeep', 'board 5V wire removed', [6, 10, -18], 'wg-warn');
  },
  steps: [
    intro(view([30, 5, -10], 290, 220, 45), 'In this guide you will wire a strip of colour pixels to the PIXEL terminal. Each light on the strip can show its own colour. It takes about twenty minutes. With a strip that has a plug lead, you need no soldering.', { title: 'Pixels', focus: ['PIXEL'] }),
    need(view([-80, 5, -25], 230, 230, 40), 'You need a five volt WS2812 strip, sometimes called an addressable R G B strip. Easiest is one with a plug lead on its input end, and a matching lead with bare ends. You also need a capacitor of about one thousand microfarads, and a small screw terminal block.', { show: ['pixels'] }),
    before(view([-60, 5, -60], 290, 220, 45), ['Never a 12 V strip on PIXEL', 'Never join the board’s 5V to another supply', 'Do not drop the GND wire'],
      { extra: 'Plug and unplug the strip only with the power off. Never a twelve volt strip on PIXEL.' }),
    { kind: 'find', only: 'case', title: 'In the case', say: 'The case has no opening for PIXEL yet. For pixels, use the board out of its case for now, or run the lead out with the lid open.',
      cam: view([50, 20, 30], 330, 200, 40), case: 'nolid', focus: ['PIXEL'] },
    { kind: 'find', title: 'Find PIXEL', say: 'PIXEL is the three screw terminal at the back left corner. The silkscreen reads GND, DATA, 5V.',
      cam: view([16, 15, 0], 140, 200, 35), focus: ['PIXEL'] },
    { kind: 'wire', title: 'The capacitor', say: 'At the strip’s input end, the end its arrows point away from, join the strip’s lead to your wires in the screw block. Put the capacitor’s legs in with them: the long leg with five volts, the striped side with ground.',
      text: 'At the strip’s input end (the end its arrows point away from), join the strip’s lead to your three wires in the screw block. Put the capacitor’s legs in with them: the long leg (+) with 5V, the striped side (−) with GND. Strip with bare pads and no lead? Solder the capacitor across its 5V and GND pads instead, stripe to GND, and solder the three wires to the pads (about 10 minutes with an iron).',
      cam: view([-40, 5, -25], 160, 230, 45), draw: ['c1', 'c2'], inset: { type: 'cap' } },
    { kind: 'wire', title: 'Three wires to PIXEL', say: 'Now three wires to PIXEL: the strip’s five volts to 5V, its data to DATA, and its ground to GND.',
      cam: view([-10, 5, -15], 200, 225, 45), draw: ['v5', 'data', 'gnd'], focus: ['PIXEL'] },
    tug(view([16, 10, -10], 140, 210, 35)),
    { kind: 'tell', title: 'Tell the board', say: 'On the phone page, open Settings. In the Pixels card, set Pixels to the number of lights on your strip, and Pixel brightness to forty to start. Then press Save settings.',
      text: 'On the phone page: Settings > Pixels > "Pixels" = the number of lights on your strip, and "Pixel brightness" = 40 to start. Then Save settings.',
      cam: view([30, 5, -10], 290, 220, 45) },
    { kind: 'test', title: 'Test it', say: 'Power the board and open the Live tab. In the Pixels card, pick a colour and the effect fill, and press Show. The strip lights up. Press Release when you are done.',
      cam: view([-60, 5, -25], 300, 210, 40), live: { pixels: 'glow' },
      inset: { type: 'phone', tab: 'Live', cardTitle: 'Pixels', body: '<div class="wg-ph-out"><span style="background:#ff6000;width:28px">&nbsp;</span><span class="wg-ph-sel">fill ▾</span><span class="pressed">Show</span><span>Release</span></div>', note: 'Colour, <b>fill</b>, <b>Show</b>. Then <b>Release</b>.' } },
    { kind: 'more', title: 'A longer strip', say: 'The board gives the strip about one amp, enough for about fifteen lights at full white. For more, give the strip its own five volt supply. Take the board’s five volt wire off.',
      cam: view([-60, 5, -60], 330, 230, 45), unwire: ['v5'], show: ['nokeep'] },
    { kind: 'more', title: 'Its own supply', say: 'The supply’s plus goes to the strip’s five volts, and its minus to the strip’s ground. Keep the DATA and GND wires to the board: the grounds must be joined. Plug both supplies into one power strip, so they switch on and off together.',
      cam: view([-80, 5, -80], 280, 230, 45), show: ['psu5', 'jack5'], draw: ['cord5', 'p5', 'm5'], live: { pixels: 'chase' } },
    { kind: 'show', title: 'Use it in a show', say: 'In a show, record a pixel track on the Record tab, or use the starter kit’s pixel shows. A show then plays it when its input fires.',
      cam: view([-60, 5, -25], 300, 210, 40), live: { pixels: 'chase' } },
    trouble(view([-60, 5, -60], 290, 220, 45)),
    next(view([30, 5, -10], 290, 220, 45)),
  ],
  trouble: [
    'Nothing lights: the data wire must go to the strip’s input end (DIN), not the output end. The arrows on the strip point away from the input.',
    'Only the first light works, or colours are wrong: check "Pixels" in Settings and the DATA wire.',
    'Lights flicker or go random: the GND wire between the board and the strip is missing or loose.',
    'The board resets when the strip lights up: too many lights on the board’s 5 V. Lower the brightness or give the strip its own supply.',
  ],
  next: ['kit', '02', '04'],
};
