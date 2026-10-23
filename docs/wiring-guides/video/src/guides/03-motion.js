// Guide 3: a motion sensor on TRIG2 (TRIG1 in the case: the lid button shares TRIG2). The mini 3-pin PIR runs from the
// jack's own 3.3 V ring. TRIG facts (hardware/README.md, board.ts): tip = input with a 4.7k pull-up to 3.3 V; ring =
// 3V3 through 220 ohm (up to 15 mA); sleeve = GND. sensor_high mode (setting "TRIG2 input type", firmware v0.1.0-rc.1 or later) needs a
// sensor output that drives low at idle (sinking about 0.7 mA against the pull-up): AM312-type sensors are expected to
// but this is to be confirmed on the first build. PIR sensors settle for 30-60 s after power-on.
import { view } from '../scene/view.js';
import { intro, need, before, tug, trouble, next } from './template.js';
import { chips, setting } from '../player/insets.js';

const TRS = { type: 'trs', title: 'Three wires to the plug', wires: { T: { color: 'yellow', label: 'OUT' }, R: { color: 'red', label: 'VCC' }, S: { color: 'black', label: 'GND' } } };
const FW = 'Needs firmware v0.1.0-rc.1 or later (it has the "TRIG2 input type" setting). If your Settings tab has no "TRIG2 input type", update the firmware first (Firmware tab).';

export default {
  id: '03', slug: 'motion-on-trig2', title: 'A motion sensor on TRIG2',
  posterStep: 7, time: '15 minutes', level: 'screwdriver', firmware: FW,
  blurb: 'Wire a mini 3-pin motion sensor to TRIG2, powered by the jack itself, so a prop starts when someone walks by.',
  need: [
    'A mini 3-pin PIR motion sensor, the AM312 kind (2.7-12 V, pins VCC, OUT, GND). Its output must drive low when idle: AM312 sensors are expected to (not yet tried on a real board)',
    'A 3.5 mm stereo (TRS) plug with screw terminals (screws often marked L, R, G)',
    'Three wires (red, yellow, black). Easiest: a sensor sold with wires already fitted',
    'A small flat screwdriver and wire strippers',
  ],
  build(k) {
    const m = k.at('TRIG2');
    k.add('adapter', k.props.trsAdapter(), { pos: m.pos.clone().addScaledVector(m.dir, 1).toArray(), from: [0, 0, -60], pins: { T: 'T', R: 'R', S: 'S' }, label: '3.5 mm plug with screws', labelAt: [0, 14, -22] });
    k.add('mini', k.props.miniPir(), { pos: [31, 0, -95], pins: { vcc: 'VCC', out: 'OUT', gnd: 'GND' }, label: 'mini motion sensor (3 pins)', labelAt: [0, 20, 0] });
    k.wire('vcc', k.at('mini', 'vcc'), k.at('adapter', 'R'), { color: 'red', leadB: 8, label: 'VCC to R (ring, 3.3 V)', labelT: 0.3 });
    k.wire('out', k.at('mini', 'out'), k.at('adapter', 'T'), { color: 'yellow', leadB: 8, label: 'OUT to T (tip)', labelT: 0.55 });
    k.wire('gnd', k.at('mini', 'gnd'), k.at('adapter', 'S'), { color: 'black', leadB: 8, label: 'GND to S (sleeve)', labelT: 0.8 });
    // the alternative: a 12 V sensor with a relay output, powered from RAILS
    k.add('pir', k.props.motionSensor(), { pos: [35, 2, -110], pins: { p12: '+12V', gnd: 'GND', com: 'COM', no: 'NO' }, label: '12 V sensor, relay output', labelAt: [0, 26, 0] });
    k.wire('no', k.at('pir', 'no'), k.at('adapter', 'T'), { color: 'blue', leadB: 8, label: 'NO to T' });
    k.wire('com', k.at('pir', 'com'), k.at('adapter', 'S'), { color: 'black', leadB: 8, label: 'COM to S', labelT: 0.35 });
    k.add('fuse', k.props.fuse(), { pos: [70, 3, -60], label: '0.5 A fuse', labelAt: [0, 9, 0] });
    k.wire('vin', k.at('RAILS', '1'), k.at('pir', 'p12'), { color: 'red', via: [[84, 6, -30], [70, 3, -60], [36, 3, -86]], leadB: 6, label: 'RAILS VIN to +12V' });
    k.wire('rgnd', k.at('RAILS', '4'), k.at('pir', 'gnd'), { color: 'black', via: [[69, 5, -28], [52, 3, -55], [40, 3, -88]], leadB: 6, label: 'RAILS GND to GND', labelT: 0.4 });
    k.add('caseplug', k.props.trsPlug(), { pos: k.plateJack('front', 5.29).add({ x: 0, y: 0, z: 1 }).toArray(), rot: [0, Math.PI, 0], from: [0, 0, 60], label: 'in the case: TRIG1, front plate', labelAt: [0, 12, -10] });
  },
  steps: [
    intro(view([50, 10, -20], 290, 160, 40), 'In this guide you will wire a mini motion sensor to the board, so your prop starts when someone walks by. It takes about fifteen minutes and a small screwdriver.', { title: 'Motion on TRIG2', focus: ['TRIG2'] }),
    need(view([31, 5, -95], 110, 160, 40), 'You need a mini motion sensor with three pins, marked VCC, OUT and GND, the AM312 kind. It runs on 2.7 to 12 volts, so the jack itself can power it. And a stereo plug with screw terminals.', { show: ['mini'] }),
    before(view([45, 10, -40], 280, 170, 40), ['Nothing safety-related on a powered sensor', 'No other supply on the R screw', 'No signal above 3.3 V on T'],
      { extra: 'With a powered sensor, an unplugged cable counts as motion. Use it for spooky reactions only.',
        warn: 'Unplugged = motion. In this mode a loose or unplugged cable reads as someone there. Never use it for anything safety-related: use a switch for that.' }),
    { kind: 'find', only: 'bare', title: 'Find TRIG2', say: 'TRIG2 is the round jack on the back edge, next to the PIXEL terminal. Push a stereo plug with screw terminals all the way in.',
      cam: view([31, 14, -20], 170, 150, 30), focus: ['TRIG2'], show: ['adapter'] },
    { kind: 'find', only: 'case', title: 'In the case: use TRIG1', say: 'In the case, the lid button is wired to TRIG2, and pressing it would short a powered sensor. So in the case, plug the sensor into TRIG1 on the front plate instead. The wiring is the same.',
      cam: view([30, 25, 125], 250, -15, 20), case: 'closed', show: ['caseplug'], focus: ['caseplug'] },
    { kind: 'wire', title: 'Three wires', say: 'Now three wires. The sensor’s VCC goes to R, the ring, which gives 3.3 volts. Its OUT goes to T, the tip. On many plugs that is L. Its GND goes to S, which may be marked G.',
      text: 'Now three wires. The sensor’s VCC goes to R (the ring, which gives 3.3 V). Its OUT goes to T (the tip; L on many plugs). Its GND goes to S (the sleeve; G or − on many plugs).',
      cam: view([31, 5, -60], 170, 140, 40), show: ['adapter'], draw: ['vcc', 'out', 'gnd'], inset: TRS },
    tug(view([31, 10, -40], 150, 160, 35)),
    { kind: 'tell', only: 'bare', title: 'Tell the board', say: 'This sensor’s signal goes high when it sees someone, the opposite of a button. On the phone page, open Settings. In the Inputs card, set TRIG2 input type to sensor high. Then press Save settings.',
      text: 'This sensor’s signal goes high when it sees someone, the opposite of a button. On the phone page: Settings > Inputs > TRIG2 input type > sensor_high > Save settings. (sensor_high means "powered sensor, its signal goes high".)',
      cam: view([50, 10, -20], 290, 160, 40), inset: { type: 'phone', tab: 'Settings', cardTitle: 'Inputs', body: setting('TRIG2 input type', 'sensor_high'), note: 'Pick <b>sensor_high</b>, then press <b>Save settings</b>.' } },
    { kind: 'tell', only: 'case', title: 'Tell the board', say: 'This sensor’s signal goes high when it sees someone. On the phone page, open Settings. In the Inputs card, set TRIG1 input type to sensor high. Then press Save settings.',
      text: 'This sensor’s signal goes high when it sees someone. On the phone page: Settings > Inputs > TRIG1 input type > sensor_high > Save settings.',
      cam: view([50, 10, -20], 290, 160, 40), inset: { type: 'phone', tab: 'Settings', cardTitle: 'Inputs', body: setting('TRIG1 input type', 'sensor_high'), note: 'Pick <b>sensor_high</b>, then press <b>Save settings</b>.' } },
    { kind: 'test', title: 'Test it', say: 'Power the board and wait a minute: motion sensors need about a minute to settle after power on. Open the Live tab. Wave in front of the sensor. In the Inputs card, the box of your input lights up orange for a couple of seconds.',
      text: 'Power the board and wait a minute: motion sensors need about a minute to settle after power-on, and may fire on their own until then. Open the Live tab. Wave in front of the sensor. In the Inputs card, TRIG2 (TRIG1 in the case) lights up orange for a couple of seconds.',
      cam: view([45, 10, -40], 280, 170, 40), live: { mini: 1 },
      inset: { type: 'phone', tab: 'Live', cardTitle: 'Inputs', body: chips(['TRIG1', 'TRIG2', 'USER1', 'USER2', 'USER3'], 'TRIG2'), note: 'Lights orange while it sees you. Lit all the time? See "Not working?".' } },
    { kind: 'show', title: 'Use it in a show', text: 'In a show script, `wait for trig2` (or `wait for trig1` in the case) waits for someone to walk by. End the effect with a rest, for example `sleep 20`, so it does not fire again straight away.',
      say: 'In a show script, wait for trig 2 waits for someone to walk by. End the effect with a rest, for example sleep twenty, so it does not fire again straight away.',
      cam: view([45, 10, -40], 280, 170, 40) },
    { kind: 'more', only: 'bare', title: 'Long cable: a 12 V sensor (optional)', say: 'For a long cable, use a twelve volt sensor with a relay output instead. NO goes to T and COM to S. Power it from RAILS: VIN through a half amp fuse to plus twelve volts, GND to GND. Set TRIG2 input type back to switch.',
      text: 'For a long cable, use a 12 V sensor with a relay output instead. NO goes to T and COM to S. Power it from RAILS: VIN through a 0.5 A fuse to +12V, GND to GND (RAILS gives the supply’s voltage, so match the sensor to it). Set TRIG2 input type back to "switch" and Save settings.',
      cam: view([60, 5, -50], 290, 150, 38), hide: ['mini', 'vcc', 'out', 'gnd'], show: ['adapter', 'pir', 'fuse'], draw: ['no', 'com', 'vin', 'rgnd'], focus: ['RAILS'] },
    { kind: 'more', only: 'bare', title: 'Bigger sensors (optional)', say: 'A sensor that needs five volts, like the HC SR501, can take its power from RAILS: five V to its VCC, GND to its GND. Its signal goes to T, and its ground also to S. Keep sensor high. Its signal must not go above 3.3 volts.',
      text: 'A sensor that needs 5 V, like the HC-SR501, can take its power from RAILS: 5V to its VCC, GND to its GND. Its signal goes to T and its GND also to S, and keep sensor_high. Its signal must not go above 3.3 V (the HC-SR501’s does not).',
      cam: view([50, 10, -40], 290, 170, 40), focus: ['RAILS'] },
    trouble(view([45, 10, -40], 280, 170, 40)),
    next(view([50, 10, -20], 290, 160, 40)),
  ],
  trouble: [
    'Lit all the time, and goes dark when someone moves: the input type is still "switch". Set it to sensor_high and press Save settings.',
    'Lit all the time even on sensor_high: your sensor’s output cannot pull the input low against the board’s pull-up (it needs to sink about 0.7 mA). Use a relay-output sensor (the optional step).',
    'Never lights: check the three wires (VCC on R, OUT on T, GND on S), that the plug is all the way in, and that you waited a minute after power-on.',
    'It fires with nobody there: keep the sensor away from heaters, fans and sunlight; it also fires while it settles after power-on.',
  ],
  next: ['01', '04', 'kit'],
};
