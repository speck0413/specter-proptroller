// Guide 8: powering the board safely. Facts (hardware/README.md): 9-24 V DC, ratings for 12-24 V; barrel <= 5 A,
// terminal <= 10 A board total; SMAJ24A TVS only (conducts from about 26.7 V): no fuse, no reverse-polarity protection,
// so a backwards supply can kill the board. Over-current protection in a supply trips well above its rating: a backstop.
// In the case, PWR and OUT1-4 are the same 5.5 x 2.1 mm jack: a supply in an OUT jack back-feeds the board (enclosure
// labelling is the owner's item), so the guide says "PWR only".
import { view } from '../scene/view.js';
import { intro, need, before, trouble, next } from './template.js';

export default {
  id: '08', slug: 'powering-the-board', title: 'Powering the board safely', label: 'Start here',
  posterStep: 7, time: '10 minutes', level: 'none',
  blurb: 'Pick the supply, check its plus and minus with a meter, then plug it into the barrel jack (up to 5 A) or wire the screw terminal (up to 10 A).',
  need: [
    'A listed 12 V DC supply (UL, ETL or CSA mark on it) with over-current protection: a plug-in one with a 5.5 x 2.1 mm centre-positive plug (up to 5 A), or an enclosed one installed by a qualified person (up to 10 A)',
    'A multimeter (any cheap one) to check plus and minus',
    'For the screw terminal: red and black wire, 16 AWG (about 1.5 mm²), and a small flat screwdriver',
  ],
  build(k) {
    const bj = k.at('BARREL');
    k.add('plug', k.props.barrelPlug(), { pos: bj.pos.toArray(), from: [0, 0, -50] });
    k.add('brick', k.props.powerBrick(), { pos: [140, 15, -200], label: 'plug-in supply, 12 V, up to 5 A', labelAt: [0, 24, 0] });
    k.wire('cord', k.at('brick', 'cord'), k.at('plug', 'cable'), { color: 'black', r: 1.8, lead: 20 });
    k.add('box', k.props.enclosedSupply(), { pos: [80, 0, -190], pins: { plus: 'V+', minus: 'V−' }, label: 'enclosed supply, 12 V, up to 10 A', labelAt: [0, 50, 0] });
    k.wire('vp', k.at('box', 'plus'), k.at('POWER', '1'), { color: 'red', r: 1.2, label: 'V+ to + (thick wire)', leadB: 12 });
    k.wire('vm', k.at('box', 'minus'), k.at('POWER', '2'), { color: 'black', r: 1.2, label: 'V− to −', leadB: 12, labelT: 0.35 });
    k.note('mains', 'mains side: covered, qualified installer', [80, 30, -150], 'wg-warn');
    k.add('caseplug', k.props.barrelPlug(), { pos: k.plateJack('back', 83.83).add({ x: 0, y: 0, z: -2 }).toArray(), rot: [0, 0, 0], from: [0, 0, -60], label: 'PWR jack, back plate', labelAt: [0, 12, -8] });
  },
  steps: [
    intro(view([100, 10, -20], 290, 160, 40), 'The board runs on direct current between nine and twenty four volts. Twelve volts is the usual choice. This guide shows how to pick the supply, check it, and plug it in. Do this before any other guide.', { title: 'Power', focus: ['BARREL', 'POWER'] }),
    need(view([140, 15, -160], 300, 160, 35), 'You need a listed twelve volt supply with over current protection, and a cheap multimeter to check its plus and minus.', { show: ['brick'] }),
    before(view([100, 15, -20], 300, 160, 40), ['Never two supplies at once', 'Never above 24 V (do not turn an adjustable supply up)', 'No guitar-pedal adapter: most are centre-negative'],
      { extra: 'The board has no fuse and no protection against a backwards supply. Check plus and minus every time.' }),
    { kind: 'check', title: 'Match your loads', say: 'Your lamps, strips and valves run on the supply voltage. So if they are twelve volt parts, use a twelve volt supply.',
      cam: view([70, 10, 40], 290, 20, 45) },
    { kind: 'check', title: 'How many amps', say: 'Add up the amps of everything on the outputs, and add one and a half amps for the board itself. Pick a supply bigger than that, and never more than ten amps. Through the barrel jack, five amps at most.',
      cam: view([70, 10, 40], 290, 20, 45) },
    { kind: 'check', title: 'No fuse on the board', say: 'The board has no fuse. A listed supply switches itself off on a short, but often only well above its rating. So staying inside the amps is your job.',
      cam: view([100, 15, -20], 300, 160, 40) },
    { kind: 'check', title: 'Check plus and minus', say: 'Before the first plug in, check the supply with a meter. Set the meter to D C volts. Plug the supply into the wall, with its plug not in the board. Touch the red probe inside the plug, and the black one on the outside. Twelve means it is right. Minus twelve means it is swapped: do not use it.',
      text: 'Before the first plug-in, check the supply with a meter. Set the meter to DC V (a V with a straight line). Plug the supply into the wall, its plug not in the board. Red probe inside the plug (the centre), black probe on the outside. 12 means centre positive, right. −12 means swapped: do not use it. Most supplies also show the symbol (−)–C–(+) for centre positive.',
      cam: view([140, 15, -160], 220, 160, 35), show: ['brick'], inset: { type: 'meter', what: 'plug' } },
    { kind: 'wire', only: 'bare', title: 'Up to 5 A: the barrel jack', say: 'For up to five amps, push the five point five by two point one millimetre plug into the round jack on the back edge.',
      text: 'For up to 5 A, push the 5.5 x 2.1 mm plug into the round jack on the back edge.',
      cam: view([110, 10, -60], 300, 160, 35), show: ['plug', 'brick'], draw: ['cord'], focus: ['BARREL'] },
    { kind: 'wire', only: 'bare', title: 'Up to 10 A: the terminal', say: 'For more, use the screw terminal marked minus, nine to twenty four volts in, plus. Use thick wire, and check plus and minus with the meter before you connect. The enclosed supply’s wall side must be covered and installed by someone qualified: ask the person who does your show’s electrics.',
      cam: view([95, 10, -70], 300, 200, 40), hide: ['plug', 'brick', 'cord'], show: ['box', 'mains'], draw: ['vp', 'vm'], focus: ['POWER'], inset: { type: 'meter', what: 'terminal' } },
    { kind: 'wire', only: 'case', title: 'In the case: PWR only', say: 'In the case, the supply plugs into the jack marked PWR on the back plate, and nowhere else. The OUT jacks on the front take the same plug, but a supply there feeds the board backwards through an output that then cannot switch off. Through the PWR jack, five amps at most.',
      cam: view([85, 25, -45], 250, 170, 20), case: 'closed', show: ['caseplug'], warn: 'Supply into PWR only, never into an OUT jack.' },
    { kind: 'check', title: 'One supply only', say: 'The barrel jack and the terminal are joined inside. Use one of them, never both at once.',
      cam: view([100, 15, -10], 200, 170, 40), focus: ['BARREL', 'POWER'] },
    { kind: 'test', title: 'First power-on', say: 'Switch on with nothing connected to the outputs. The controller module’s light comes on, and the board’s Wi-Fi network appears on your phone. Then power off and add your loads.',
      cam: view([70, 10, 40], 290, 20, 45) },
    trouble(view([100, 15, -20], 300, 160, 40)),
    next(view([100, 10, -20], 290, 160, 40), { say: 'Next, open the board’s page on your phone, then try the starter kit.' }),
  ],
  trouble: [
    'Nothing lights: is the supply switched on and its plug all the way in? Measure it with the meter.',
    'The supply clicks off or blinks: it is overloaded. Add up the loads again, or use a bigger supply (up to 10 A on the terminal).',
    'Outputs get hot below 12 V: below 12 V the outputs run hotter. Use 12 V or more.',
    'The barrel plug is loose: the board takes 5.5 x 2.1 mm plugs. A 5.5 x 2.5 mm plug fits but wobbles.',
  ],
  next: ['00', 'kit', '02'],
};
