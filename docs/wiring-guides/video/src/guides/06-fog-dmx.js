// Guide 6: fog on cue, a fog machine with a DMX IN socket, through the DMX add-on (replaces the old "fog remote on
// RLY1" guide: many fog machines' wired remotes carry mains, so the project only drives fog machines over DMX).
// Facts: firmware (v0.1.0-rc.1 or later) setting dmx_port = off|s4 (Settings > Expansion > "DMX port"), script `dmx<N> = on/off/N%`,
// example firmware/show-script-examples/05_fog_over_dmx.txt; SHOW-SCRIPT §7.4: a MAX485 TTL module (a 5 V part) on
// expansion S4 (ESP32 GPIO15 / Pico GP3), DE and RE joined to +5V, A -> XLR pin 3, B -> XLR pin 2, GND -> pin 1.
// The expansion V row is 3V3 by default (board.ts SJ_V), so the module takes 5 V and GND from RAILS. The expansion
// header and RAILS are optional parts in the build. Unproven on hardware (GPIO15 is a strapping pin; A/B order untested).
import { view } from '../scene/view.js';
import { intro, need, before, tug, trouble, next } from './template.js';

const FW = 'Needs firmware v0.1.0-rc.1 or later (it has the "DMX port" setting). The DMX add-on is not yet tried on a real board or fog machine: test it before a show.';
const XLR_PINS = { type: 'xlr' };

export default {
  id: '06', slug: 'fog-over-dmx', title: 'Fog on cue: a fog machine with DMX IN',
  posterStep: 9, time: '30 minutes', level: 'screwdriver+solder', firmware: FW,
  blurb: 'Let a show puff fog on cue through the fog machine’s DMX IN socket and a small DMX add-on. Never its wired remote.',
  need: [
    'A fog machine with a socket labelled DMX IN (3-pin XLR). A machine with only a REMOTE socket or a wired remote is not covered',
    'A "MAX485 TTL to RS-485" module (5 V, often sold in packs)',
    'A 3-pin XLR female to bare-wire pigtail (or a DMX cable cut in half)',
    'Four jumper wires with a socket (female) end for the expansion pins, and short wires for RAILS',
    'The expansion pins (S / V / G rows) and the RAILS terminal fitted on your board. Both are optional in the build: if yours has no pins there, they need soldering first',
    'A button on TRIG1 to test with (guide 1)',
    'A small flat screwdriver and wire strippers',
  ],
  build(k) {
    const T = k.TOP;
    k.add('mod', k.props.max485(), { pos: [70, 0, -55], label: 'MAX485 module', labelAt: [0, 16, 0] });
    k.wire('vcc', k.at('mod', 'vcc'), k.at('RAILS', '2'), { color: 'red', label: 'VCC to RAILS 5V', leadA: 6 });
    k.wire('de', k.at('mod', 'de'), k.at('RAILS', '2'), { color: 'orange', label: 'DE + RE to 5V', leadA: 4, labelT: 0.35 });
    k.wire('gnd', k.at('mod', 'gnd'), k.at('RAILS', '4'), { color: 'black', label: 'GND to RAILS GND', leadA: 6, labelT: 0.65 });
    k.wire('di', k.at('mod', 'di'), k.point(69.64, T + 6, 20.61, [0, 1, 0]), { color: 'yellow', label: 'DI to S4', leadA: 6, via: [[40, 14, -40], [55, 18, 0]] });
    k.note('s4', 'S4 (expansion pin)', [69.64, T + 14, 22], 'wg-note');
    k.add('fog', k.props.fogMachine(), { pos: [60, 0, -250], rot: [0, Math.PI, 0], label: 'fog machine', labelAt: [0, 80, 0] });
    k.note('dmxin', 'DMX IN: use this', [30, 50, -208], 'wg-note');
    k.note('remote', 'REMOTE: never', [80, 50, -208], 'wg-warn');
    k.add('xlr', k.props.xlrPlug(), { pos: [30, 30, -190], rot: [0, Math.PI / 2, 0], from: [0, 0, 30], label: 'XLR plug', labelAt: [0, 16, 0] });
    k.wire('a', k.at('mod', 'a'), k.at('xlr', 'cable'), { color: 'green', label: 'A to XLR pin 3', leadA: 6 });
    k.wire('b', k.at('mod', 'b'), k.at('xlr', 'cable'), { color: 'blue', label: 'B to XLR pin 2', leadA: 6, labelT: 0.35 });
    k.wire('shield', k.at('RAILS', '5'), k.at('xlr', 'cable'), { color: 'black', label: 'GND to XLR pin 1', leadA: 8, labelT: 0.7 });
  },
  steps: [
    intro(view([60, 10, -90], 420, 160, 40), 'In this guide, a show puffs fog on cue. The board talks to the fog machine through its socket labelled DMX IN, using a small add-on module. It takes about thirty minutes. This needs the newest firmware, and it has not been tried on a real board yet.', { title: 'Fog on cue', show: ['fog'] }),
    need(view([60, 10, -150], 330, 160, 35), 'You need a fog machine with a socket labelled DMX IN, a MAX485 module, a three pin XLR pigtail, and a few jumper wires. Your board needs its expansion pins and the RAILS terminal fitted.', { show: ['fog', 'mod'] }),
    before(view([50, 35, -225], 190, 15, 22), ['Never the machine’s wired remote or its REMOTE socket', 'Never open the fog machine', 'Nothing safety-related on this add-on'],
      { show: ['fog', 'dmxin', 'remote'], extra: 'Use the socket labelled DMX IN, never one labelled REMOTE, even when they look alike. On many fog machines the remote line carries wall power.',
        warn: 'DMX IN, never REMOTE. Many fog machines’ wired remotes and remote sockets carry wall power (mains). These guides never connect to them.' }),
    { kind: 'check', title: 'Check the machine', say: 'Look at the back of the fog machine. You need a socket labelled DMX IN. It looks like a microphone socket with three pins. If your machine has only a remote socket, it is not covered by these guides.',
      cam: view([50, 35, -220], 150, 15, 22), show: ['fog', 'dmxin', 'remote'], inset: XLR_PINS },
    { kind: 'find', only: 'case', title: 'In the case', say: 'The case has no opening for the expansion pins yet. For now, use the board out of its case, or work with the lid open.',
      cam: view([70, 15, 30], 220, 180, 40), case: 'nolid' },
    { kind: 'find', title: 'Find RAILS and S4', say: 'Find RAILS on the back edge: five screws, VIN, 5V, 3V3, GND, GND. Then find the expansion pins: three rows of eleven pins, marked S, V and G. The fourth pin of the S row is S4.',
      text: 'Find RAILS on the back edge: five screws, VIN, 5V, 3V3, GND, GND. Then find the expansion pins: three rows of eleven pins, marked S, V and G. The fourth pin of the S row is S4. No pins there? They need soldering first (ask whoever built the board).',
      cam: view([75, 15, 15], 150, 180, 50), focus: ['RAILS'], show: ['s4'] },
    { kind: 'wire', title: 'Power the module', say: 'Wire the module’s VCC to the RAILS screw marked 5V, and its GND to a RAILS GND screw. Join the module’s DE and RE pins, and wire them to 5V too. Then it always sends.',
      cam: view([75, 10, -40], 200, 170, 40), show: ['mod'], draw: ['vcc', 'de', 'gnd'], focus: ['RAILS'] },
    { kind: 'wire', title: 'Data to S4', say: 'Wire the module’s DI pin to S4, the fourth pin of the expansion S row.',
      cam: view([70, 10, -10], 200, 160, 45), draw: ['di'], show: ['s4'] },
    { kind: 'wire', title: 'The XLR plug', say: 'Now the XLR pigtail. The module’s A goes to XLR pin three, its B to pin two, and a ground wire from RAILS GND to pin one. The pin numbers are printed tiny on the plug.',
      text: 'Now the XLR pigtail. The module’s A goes to XLR pin 3 (Data +), its B to pin 2 (Data −), and a wire from a RAILS GND screw to pin 1. The pin numbers are printed tiny on the plug.',
      cam: view([50, 10, -120], 260, 150, 40), show: ['xlr', 'fog'], draw: ['a', 'b', 'shield'], inset: XLR_PINS },
    tug(view([75, 10, -40], 200, 170, 40)),
    { kind: 'tell', title: 'Tell the board', say: 'On the phone page, open Settings. In the Expansion card, set DMX port to s four, and press Save settings.',
      text: 'On the phone page: Settings > Expansion > DMX port > s4 > Save settings.',
      cam: view([60, 10, -90], 420, 160, 40),
      inset: { type: 'phone', tab: 'Settings', cardTitle: 'Expansion', body: '<div class="wg-ph-set"><label>DMX port</label><span class="wg-ph-sel">s4 ▾</span></div><div class="wg-ph-save">Save settings</div>', note: 'Pick <b>s4</b>, then <b>Save settings</b>.' } },
    { kind: 'tell', title: 'Set up the machine', say: 'Set the fog machine to DMX mode, address one. Its manual shows how: often small switches or a menu. If it has a DMX OUT socket too, leave it empty, or fit a DMX terminator if the manual asks for one. Then plug the XLR into DMX IN.',
      cam: view([45, 35, -215], 160, 15, 22), show: ['fog', 'xlr', 'dmxin', 'remote'] },
    { kind: 'tell', title: 'Warm it up', say: 'A fog machine only fogs when its heater is ready. A cold machine does nothing, and the board cannot tell. Switch the machine on a few minutes before the show.',
      cam: view([70, 40, -240], 280, 30, 25), show: ['fog'] },
    { kind: 'test', title: 'Test it', text: 'On the Script tab, paste a test effect: `effect fog:`, `wait for trig1`, `dmx1 = 100%`, `sleep 4`, `dmx1 = off`, `sleep 45`. Press Check, then Save. Press your TRIG1 button: the machine puffs fog for 4 seconds.',
      say: 'On the Script tab, paste a short test effect: wait for trig 1, D M X 1 equals 100 percent, sleep 4, D M X 1 equals off. Press Check, then Save. Press your button on TRIG1. The machine puffs fog for four seconds.',
      cam: view([80, 40, -150], 380, 35, 30), live: { fog: 1 } },
    { kind: 'show', title: 'Use it in a show', text: 'In a show script, `dmx1 = 100%` starts fog, `dmx1 = off` stops it, and `dmx1 = 50%` asks for less if your machine supports it. End the effect with a long rest (`sleep 45`): a fog machine needs time to heat up again.',
      say: 'In a show script, D M X 1 equals 100 percent starts fog, and D M X 1 equals off stops it. End the effect with a long rest, so the machine can heat up again.',
      cam: view([80, 40, -150], 380, 35, 30), live: { fog: 1 } },
    trouble(view([60, 10, -90], 420, 160, 40)),
    next(view([60, 10, -90], 420, 160, 40)),
  ],
  trouble: [
    'The Check button says the DMX add-on is not turned on: set Settings > Expansion > DMX port to s4 and Save settings.',
    'No fog: is the machine warm (its ready light on)? Is it in DMX mode at address 1? Is the XLR in DMX IN?',
    'Still no fog: swap the A and B wires at the XLR (pins 2 and 3). The A/B order has not been tested on a real machine yet.',
    'The board does not start, or restarts, with the module fitted (ESP32 boards): power off, take the DI wire off S4, power up, then power off again and refit it. Report it: S4 is a pin the ESP32 reads at start-up.',
  ],
  next: ['01', '03', 'kit'],
};
