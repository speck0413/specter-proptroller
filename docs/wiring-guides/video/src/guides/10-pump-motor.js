// Guide 10: a 12 V DC pump, motor or pop-up on OUT1 (split from the old solenoid guide). OUT facts (hardware/README.md):
// 5 A continuous at 12-24 V, 10 A only as a start-up surge under about 1 s, sum <= 10 A; SS56 freewheel diode per OUT on
// the board. Settings > Outputs > "OUT1 soft start" (ms) ramps a motor up. A brushed motor reverses when its two wires
// swap; one OUT only turns it one way (reversing needs the two relays and the "RLY1 / RLY2 never together" setting).
import { view } from '../scene/view.js';
import { intro, need, before, tug, trouble, next } from './template.js';
import { outRow } from '../player/insets.js';

export default {
  id: '10', slug: 'pump-or-motor-on-out', title: 'A 12 V pump or motor on OUT1',
  posterStep: 6, time: '10 minutes', level: 'screwdriver',
  blurb: 'Wire a 12 V DC pump, motor or store-bought 12 V pop-up to OUT1, with a soft start for the start-up surge.',
  need: [
    'A 12 V DC pump or motor (or a pop-up prop that runs from a 12 V adapter) drawing under 5 A while running',
    'Red and black wire, 18 AWG (16 AWG for long runs), a small flat screwdriver and wire strippers',
    'For a brushed motor on a long lead (over about 1 m): a 1N4007 or 1N5819 diode across it, stripe on + (needs soldering)',
  ],
  build(k) {
    k.add('motor', k.props.dcMotor(), { pos: [105, 0, 175], pins: { plus: '+', minus: '−' }, label: '12 V DC motor or pump', labelAt: [0, 40, 0] });
    k.wire('plus', k.at('OUT1', '1'), k.at('motor', 'plus'), { color: 'red', label: 'red: + to OUT1 +', leadA: 12 });
    k.wire('minus', k.at('OUT1', '2'), k.at('motor', 'minus'), { color: 'black', label: 'black: − to OUT1 −', leadA: 12, labelT: 0.35 });
  },
  steps: [
    intro(view([90, 10, 110], 280, 15, 40), 'In this guide you will wire a twelve volt pump or motor to OUT1, for a spinning, pumping or popping prop. It takes about ten minutes.', { title: 'Pumps and motors', focus: ['OUT1'] }),
    need(view([105, 20, 175], 180, 20, 30), 'You need a twelve volt D C pump or motor that draws under five amps while it runs, some red and black wire, and a small screwdriver.', { show: ['motor'] }),
    before(view([95, 10, 130], 300, 20, 40), ['No motor that plugs into the wall', 'No motor over 5 A running', 'Never across another device’s button'],
      { extra: 'Only twelve volt D C motors, under five amps while running.' }),
    { kind: 'check', title: 'Check the label', say: 'Read the label: twelve volts D C, and the running current under five amps. A motor draws much more for a moment as it starts. The board allows up to ten amps for under a second, and the soft start below helps.',
      cam: view([105, 20, 175], 140, 20, 30), show: ['motor'] },
    { kind: 'find', only: 'case', title: 'In the case', say: 'In the case, OUT1 is the first round power jack on the front plate. Which pin is plus depends on how your case was wired: check with a meter, or open the lid and use the OUT1 screws.',
      cam: view([70, 25, 125], 260, 20, 20), case: 'closed', hide: ['motor'], inset: { type: 'meter', what: 'plug', title: 'Which pin is +?' } },
    { kind: 'wire', title: 'Wire it', say: 'Unplug the power. The motor’s plus wire, usually red or marked with a dot, goes to the plus screw of OUT1. The minus wire goes to the minus screw.',
      cam: view([88, 10, 125], 220, 30, 40), draw: ['plus', 'minus'], focus: ['OUT1'] },
    tug(view([83, 15, 85], 130, -30, 30)),
    { kind: 'test', title: 'Test it', say: 'Power the board. On the Live tab, in the Outputs card, press On next to OUT1. The motor runs. If it turns the wrong way, power off and swap its two wires. Then press Auto.',
      cam: view([95, 10, 135], 300, 20, 40), live: { OUT1: 1, motor: 1 },
      inset: { type: 'phone', tab: 'Live', cardTitle: 'Outputs', body: outRow('OUT1', 'On'), note: 'On: the motor runs. Then press <b>Auto</b>.' } },
    { kind: 'tell', title: 'Soft start', say: 'To ease the start up surge, open Settings, Outputs, and set OUT1 soft start to five hundred milliseconds. Then Save settings. The motor now speeds up over half a second.',
      text: 'To ease the start-up surge: Settings > Outputs > "OUT1 soft start" = 500 (ms) > Save settings. The motor now speeds up over half a second.',
      cam: view([95, 10, 135], 300, 20, 40) },
    { kind: 'show', title: 'Use it in a show', text: 'In a show script: `out1 = on`, `wait 3`, `out1 = off`. `out1 = 60%` runs it slower (keep a dimmed motor under 2 A at 12 V). A pop-up that must stop at an end needs its own end switches: follow its maker’s wiring.',
      say: 'In a show script, out 1 equals on, wait three, out 1 equals off. Out 1 equals sixty percent runs it slower.',
      cam: view([95, 10, 135], 300, 20, 40), live: { OUT1: 0.6, motor: 1 } },
    trouble(view([95, 10, 135], 300, 20, 40)),
    next(view([90, 10, 110], 280, 15, 40)),
  ],
  trouble: [
    'It turns the wrong way: power off and swap the two wires. One OUT turns a motor one way only.',
    'The supply shuts off as the motor starts: the start-up surge is too big. Set a soft start, or use a bigger supply.',
    'It hums but does not turn: it is stuck, or the supply or wires are too small.',
    'The board restarts when it starts: use a bigger supply, set a soft start, and fit a diode across a brushed motor on a long lead.',
  ],
  next: ['07', '02', '08'],
};
