// The starter kit: the bare board on a desk with a big button, a motion sensor, a lamp strip, a
// fan, a pixel strip and a speaker, all wired at once; then the show script and the first big press.
// Script: docs/wiring-guides/starter-kit/starter-kit.txt (checked by the firmware's own checker).
const SCRIPT_DIR = 'docs/wiring-guides/starter-kit';
import { view } from '../scene/view.js';
import { intro, need, before, trouble, next } from './template.js';
import { setting } from '../player/insets.js';

export default {
  id: 'kit', slug: 'starter-kit', title: 'The starter kit: your first hour', label: 'Start here',
  posterStep: 14, time: 'about an hour', level: 'screwdriver',
  firmware: 'The motion sensor needs firmware v0.1.0-rc.1 or later (it has the "TRIG2 input type" setting).',
  blurb: 'One button, one motion sensor, a lamp, a fan, a pixel strip and a speaker on your desk. One press makes it all go.',
  need: [
    'The board, bare on the desk (the case has no opening for PIXEL yet), and a listed 12 V supply with a 5.5 x 2.1 mm plug, 3 A is plenty',
    'A big push button with two wires, and a mini 3-pin PIR motion sensor (AM312 kind)',
    'Two 3.5 mm stereo plugs with screw terminals (screws often marked L, R, G)',
    'A 12 V LED strip (30-50 cm, single colour) and a small 12 V fan',
    'A 5 V WS2812 pixel strip with 15 lights, best with a plug lead fitted, a 1000 µF capacitor (10 V or more) and a small screw terminal block',
    'A passive speaker (4-8 Ω, rated 3 W or more) and a microSD card (32 GB or smaller, FAT32)',
    'Hook-up wire, a small flat screwdriver, wire strippers, and a multimeter',
  ],
  build(k) {
    const P = k.props;
    // inputs
    const t1 = k.at('TRIG1'), t2 = k.at('TRIG2');
    k.add('adapter1', P.trsAdapter(), { pos: t1.pos.clone().addScaledVector(t1.dir, 1).toArray(), rot: [0, Math.PI, 0], from: [0, 0, 60], pins: { T: 'T', R: 'R', S: 'S' }, label: 'plug in TRIG1', labelAt: [0, 14, -22] });
    k.add('button', P.arcadeButton(), { pos: [25, 0, 185], label: 'big button', labelAt: [0, 50, 0] });
    k.wire('b1', k.at('button', 'a'), k.at('adapter1', 'T'), { color: 'yellow', via: [[21, 4, 160], [8, 4, 125]], leadA: 4, leadB: 8, label: 'to T' });
    k.wire('b2', k.at('button', 'b'), k.at('adapter1', 'S'), { color: 'black', via: [[29, 4, 162], [17, 4, 127]], leadA: 4, leadB: 8, label: 'to S', labelT: 0.3 });
    k.add('adapter2', P.trsAdapter(), { pos: t2.pos.clone().addScaledVector(t2.dir, 1).toArray(), from: [0, 0, -60], pins: { T: 'T', R: 'R', S: 'S' }, label: 'plug in TRIG2', labelAt: [0, 14, -22] });
    k.add('pir', P.miniPir(), { pos: [31, 0, -95], pins: { vcc: 'VCC', out: 'OUT', gnd: 'GND' }, label: 'mini motion sensor (3 pins)', labelAt: [0, 20, 0] });
    k.wire('p1', k.at('pir', 'vcc'), k.at('adapter2', 'R'), { color: 'red', leadB: 8, label: 'VCC to R', labelT: 0.3 });
    k.wire('p2', k.at('pir', 'out'), k.at('adapter2', 'T'), { color: 'yellow', leadB: 8, label: 'OUT to T', labelT: 0.55 });
    k.wire('p3', k.at('pir', 'gnd'), k.at('adapter2', 'S'), { color: 'black', leadB: 8, label: 'GND to S', labelT: 0.8 });
    // loads on the outputs
    k.add('strip', P.ledStrip({ length: 150 }), { pos: [45, 1, 150], pins: { plus: '+', minus: '−' }, label: '12 V LED strip', labelAt: [75, 12, 0] });
    k.wire('o1p', k.at('OUT1', '1'), k.at('strip', 'plus'), { color: 'red', label: 'OUT1 +', leadA: 10 });
    k.wire('o1m', k.at('OUT1', '2'), k.at('strip', 'minus'), { color: 'black', label: 'OUT1 −', leadA: 10, labelT: 0.3 });
    k.add('fan', P.fan(), { pos: [215, 20, 140], rot: [0, -0.5, 0], label: '12 V fan', labelAt: [0, 30, 0] });
    k.wire('o2p', k.at('OUT2', '1'), k.at('fan', 'plus'), { color: 'red', label: 'OUT2 +', leadA: 10 });
    k.wire('o2m', k.at('OUT2', '2'), k.at('fan', 'minus'), { color: 'black', label: 'OUT2 −', leadA: 10, labelT: 0.35 });
    // the pixel strip behind the board, its input end at the left
    k.add('pixels', P.pixelStrip({ count: 15 }), { pos: [-30, 1, -25], rot: [0, Math.PI, 0], pins: { v5: '5V', din: 'DIN', gnd: 'GND' }, label: 'pixel strip (15 LEDs)', labelAt: [130, 14, 0] });
    k.wire('x5', k.at('PIXEL', '1'), k.at('pixels', 'v5'), { color: 'red', label: '5V', leadA: 10, sag: 2 });
    k.wire('xd', k.at('PIXEL', '2'), k.at('pixels', 'din'), { color: 'green', label: 'DATA', leadA: 10, sag: 2 });
    k.wire('xg', k.at('PIXEL', '3'), k.at('pixels', 'gnd'), { color: 'black', label: 'GND', leadA: 10, sag: 2, labelT: 0.3 });
    k.wire('xc1', k.at('pixels', 'capP'), k.at('pixels', 'v5'), { color: 'grey', lead: 2, sag: 0, r: 0.4 });
    k.wire('xc2', k.at('pixels', 'capN'), k.at('pixels', 'gnd'), { color: 'grey', lead: 2, sag: 0, r: 0.4 });
    // the speaker
    k.add('speaker', P.speaker(), { pos: [175, 26, -70], label: 'speaker (4 Ω or more)', labelAt: [0, 34, 0] });
    k.wire('s1', k.at('SPK', '2'), k.at('speaker', 'a'), { color: 'white', label: 'SPK', leadA: 10 });
    k.wire('s2', k.at('SPK', '1'), k.at('speaker', 'b'), { color: 'grey', label: 'SPK', leadA: 10, labelT: 0.3 });
    // power
    const bj = k.at('BARREL');
    k.add('plug', P.barrelPlug(), { pos: bj.pos.toArray(), from: [0, 0, -50] });
    k.add('brick', P.powerBrick(), { pos: [140, 15, -200], label: '12 V supply (listed, with overload protection)', labelAt: [0, 24, 0] });
    k.wire('cord', k.at('brick', 'cord'), k.at('plug', 'cable'), { color: 'black', r: 1.8, lead: 20 });
  },
  steps: [
    intro(view([67, 10, 40], 300, 15, 40), 'This is the starter kit. By the end, one press of a big button will flash a lamp, puff a fan, chase a strip of lights and play a sound. Walk past, and it reacts on its own. Plan on about an hour.', { title: 'Your first hour' }),
    need(view([67, 10, 40], 380, 15, 40), 'Here is everything on the list. Tick each one off. A multimeter is on it too, to check the power supply.', { show: ['button', 'pir', 'strip', 'fan', 'pixels', 'speaker'], labels: true }),
    before(view([67, 10, 40], 300, 15, 40), ['No wall power (mains) wiring', 'No bare strands touching', 'No power until the last step'],
      { focus: ['POWER', 'BARREL'], extra: 'Everything in this kit runs on twelve volts or less. Keep the power supply unplugged until the very last step.' }),
    { kind: 'wire', title: 'Big button on TRIG1', say: 'Start with the big button. Push a stereo plug into TRIG1 at the front left. Put one button wire under the T screw and the other under the S screw. Either way round.',
      text: 'Start with the big button. Push a stereo plug into TRIG1 at the front left. Put one button wire under the T screw (L on many plugs) and the other under the S screw (G). Either way round.',
      cam: view([25, 10, 120], 300, 35, 35), show: ['adapter1', 'button'], draw: ['b1', 'b2'], focus: ['TRIG1'],
      inset: { type: 'trs', title: 'Button plug', wires: { T: { color: 'yellow', label: 'button' }, S: { color: 'black', label: 'button' } } } },
    { kind: 'wire', title: 'Motion sensor on TRIG2', say: 'Next, the mini motion sensor. Push the second plug into TRIG2 at the back. The sensor’s VCC goes to R, which gives it 3.3 volts. Its OUT goes to T, and its GND to S.',
      cam: view([31, 5, -60], 200, 160, 40), show: ['adapter2', 'pir'], draw: ['p1', 'p2', 'p3'], focus: ['TRIG2'],
      inset: { type: 'trs', title: 'Sensor plug', wires: { T: { color: 'yellow', label: 'OUT' }, R: { color: 'red', label: 'VCC' }, S: { color: 'black', label: 'GND' } } } },
    { kind: 'wire', title: 'LED strip on OUT1', say: 'Now the lamp. This is a twelve volt LED strip. Its plus wire goes to the plus screw of OUT1, and its minus wire to the minus screw. Plus is on the left.',
      cam: view([90, 10, 115], 280, 15, 35), show: ['strip'], draw: ['o1p', 'o1m'], focus: ['OUT1'] },
    { kind: 'wire', title: 'Fan on OUT2', say: 'The fan goes on OUT2 the same way: red to plus, black to minus.',
      cam: view([140, 10, 115], 290, 25, 35), show: ['fan'], draw: ['o2p', 'o2m'], focus: ['OUT2'] },
    { kind: 'wire', title: 'Pixel strip on PIXEL', say: 'The pixel strip goes on the PIXEL terminal at the back left: its five volts to 5V, data to DATA, ground to GND. At the strip’s input end, put the capacitor in the screw block with the wires: long leg with five volts, striped side with ground.',
      text: 'The pixel strip goes on the PIXEL terminal at the back left: its 5V to 5V, data to DATA, ground to GND. At the strip’s input end, put the capacitor in the screw block with the wires: long leg (+) with 5V, striped side (−) with GND. (Strip with bare pads? Solder the capacitor and wires to the pads: see guide 5.)',
      cam: view([-60, 5, -10], 300, 250, 45), show: ['pixels'], draw: ['x5', 'xd', 'xg', 'xc1', 'xc2'], focus: ['PIXEL'], inset: { type: 'cap' } },
    { kind: 'wire', title: 'Speaker on SPK', say: 'The speaker’s two wires go on the two SPK screws, either way round. Never join a speaker wire to ground, and never plug SPK into an amplifier. Use LINE OUT for that.',
      cam: view([145, 10, -35], 290, 150, 35), show: ['speaker'], draw: ['s1', 's2'], focus: ['SPK'] },
    { kind: 'wire', title: 'Sounds on the card', text: 'On the microSD card, make a folder called 01 with the big sound inside, named 001.mp3. Make a folder called 02 with the spooky sound, also named 001.mp3. Slide the card into the sound module, contacts first. (Specter Studio can copy and number the sounds for you.)',
      say: 'On the memory card, make a folder called zero one with the big sound inside, named zero zero one dot M P 3. Make a folder called zero two with the spooky sound, also named zero zero one. Slide the card into the sound module.',
      cam: view([122, 25, 28], 170, 60, 35), focus: ['DFPLAYER'], inset: { type: 'folders' } },
    { kind: 'check', title: 'Check the supply', say: 'Check every screw one more time. Then check the supply with the meter, on D C volts: red probe inside its plug, black outside. It must read plus twelve.',
      cam: view([140, 15, -160], 220, 160, 35), show: ['brick'], inset: { type: 'meter', what: 'plug' } },
    { kind: 'wire', title: 'Power last', say: 'Now plug the twelve volt supply into the round power jack at the back.',
      cam: view([115, 10, -70], 330, 160, 35), show: ['plug', 'brick'], draw: ['cord'], focus: ['BARREL'] },
    { kind: 'tell', title: 'Load the show', connect: true, say: 'Open the board’s page. On the Script tab, paste the starter kit script, press Check, then Save. On the Shows tab, add the three pixel shows. On the Settings tab, set Pixels to fifteen, and TRIG2 input type to sensor high. Press Save settings, then Restart. This is easier from a computer than from a phone.',
      text: 'Open the board’s page. Script tab: paste the whole of starter-kit.txt, press Check, then Save. Shows tab: + New, open JSON, paste show-1-ready-glow.json, Apply JSON, Save; the same for show 2 and show 3. Settings tab: Pixels = 15, Pixel brightness = 40, Inputs > TRIG2 input type = sensor_high; press Save settings, then Restart. This is easier from a computer than from a phone.',
      cam: view([75, 0, 40], 470, 10, 45), inset: { type: 'phone', tab: 'Settings', cardTitle: 'Inputs', body: setting('TRIG2 input type', 'sensor_high'), note: 'Then Pixels = 15 and <b>Save settings</b>, <b>Restart</b>.' } },
    { kind: 'test', title: 'Ready', say: 'After the restart, the pixel strip breathes a calm orange glow. That means it is ready. Give the motion sensor a minute to settle.',
      cam: view([60, 5, -50], 360, 190, 35), live: { pixels: 'glow' } },
    { kind: 'test', title: 'Press the button!', say: 'Now press the big button. The sound plays, the lamp flashes, the fan puffs and the pixels chase. That is your first show.',
      cam: view([75, 0, 40], 450, 10, 45), live: { button: 'press', OUT1: 1, OUT2: 1, strip: 1, fan: 1, pixels: 'chase', speaker: 1 } },
    { kind: 'test', title: 'Walk past', say: 'Walk in front of the motion sensor. A spooky sound plays and the pixels flicker red. It waits twenty seconds before it reacts again.',
      cam: view([60, 5, -50], 380, 200, 40), live: { pir: 1, pixels: 'flicker', speaker: 1 } },
    trouble(view([75, 0, 40], 470, 10, 45)),
    next(view([75, 0, 40], 470, 10, 45), { say: 'That is the whole pattern: an input starts an effect, and the effect turns outputs on and off. Next, try the single guides: fog on cue, an air valve, or a longer pixel strip.', live: { pixels: 'glow' } }),
  ],
  trouble: [
    'Nothing happens at all: is the supply plugged in, and does the controller module’s light come on? Does the phone page open (guide "Meet your board")?',
    'The page says the script has an error: paste the whole file again, from the first line to the last.',
    'The button does nothing: plug fully in, wires on T and S (guide 1).',
    'The motion sensor fires all the time: wait a minute after power-on; check TRIG2 input type = sensor_high (guide 3).',
    'The strip or fan does nothing but the OUT light comes on: plus and minus, and screws on copper, not insulation.',
    'No sound: folders 01, 02 and files 001.mp3, card FAT32. The pixels stay dark: the DATA wire, the three shows, Pixels = 15.',
  ],
  next: ['06', '07', '05'],
  scriptDir: SCRIPT_DIR,
};
