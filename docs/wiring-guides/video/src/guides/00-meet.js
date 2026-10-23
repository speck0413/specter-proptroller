// Start here: a tour of the board (which edge is the front, what each terminal is for) and how to open the board's
// page on a phone (its own Wi-Fi hotspot, then optionally your home Wi-Fi). Every Test step in the other guides links to
// the "connect" step. Facts: firmware/arduino net.h (hotspot Proptroller-XXXX, password "password", 192.168.4.1,
// name.local once on a network), firmware/web (tabs, STOP), settings.cpp (Wi-Fi network / password, Name).
import { view } from '../scene/view.js';
import { intro, need, trouble, next } from './template.js';
import { chips } from '../player/insets.js';

export default {
  id: '00', slug: 'meet-your-board', title: 'Meet your board and connect to it', label: 'Start here',
  posterStep: 1, time: '10 minutes', level: 'none',
  blurb: 'A quick tour of the board, then open its page on your phone. Every other guide tests with that page.',
  need: ['The board (bare or in its case)', 'A 12 V power supply (see "Powering the board safely")', 'A phone or laptop with Wi-Fi and a web browser'],
  build(k) {
    k.note('front', 'FRONT (this edge toward you)', [67, 2, 112], 'wg-note');
    k.note('back', 'BACK', [67, 2, -22], 'wg-note');
  },
  steps: [
    intro(view([67, 10, 40], 300, 15, 40), 'Meet your board. First a short tour, so you know what each part is for. Then you will open the board’s own page on your phone. Every other guide uses that page to test your wiring.', { title: 'Meet your board' }),
    need(view([67, 10, 40], 300, 15, 40), 'You need the board, a twelve volt power supply, and a phone or laptop with Wi-Fi.'),
    { kind: 'find', title: 'Front and back', say: 'In these guides, the front is the edge with the long row of screw terminals. Turn the board so that edge faces you. Left and right are then your left and right.',
      cam: view([67, 10, 40], 280, 0, 55), show: ['front', 'back'] },
    { kind: 'find', title: 'The front edge', say: 'Along the front, from the left: TRIG1, a round jack for a button. RLY1 and RLY2, two relays. And OUT1 to OUT4, four outputs that switch power to lamps, valves and motors.',
      text: 'Along the front, from the left: TRIG1, a round jack for a button. RLY1 and RLY2, two relays (on every board built so far, rev A, the relays are for low-voltage DC only, 30 V at most: the silkscreen says "RELAYS: <=30V DC ONLY"). And OUT1 to OUT4, four outputs that switch power to lamps, valves and motors.',
      cam: view([70, 10, 70], 220, 0, 40), focus: ['TRIG1', 'RLY1', 'RLY2', 'OUT1', 'OUT2', 'OUT3', 'OUT4'], show: ['front'] },
    { kind: 'find', title: 'The back edge', say: 'Along the back, from the left: PIXEL for a colour pixel strip. TRIG2 for a motion sensor. CONTACT1 and CONTACT2, to start another controller. RAILS, which gives power to sensors. The power input. And SPK, for a speaker.',
      cam: view([70, 10, 10], 230, 180, 40), focus: ['PIXEL', 'TRIG2', 'CONTACT1', 'CONTACT2', 'RAILS', 'POWER', 'SPK'], show: ['back'] },
    { kind: 'find', title: 'Sound and the brain', say: 'On the right is the sound module, with the slot for the memory card, and the LINE OUT jack for an amplifier. In the middle sits the controller module, the brain. Its light shows it has power.',
      cam: view([110, 10, 40], 200, 70, 45), focus: ['DFPLAYER', 'LINE OUT', 'MODULE'], hide: ['front', 'back'] },
    { kind: 'find', title: 'The white printing', say: 'The white printing on the board is called the silkscreen. It names every terminal and every screw. When a guide says the screw marked plus, look for the plus in the silkscreen next to it.',
      cam: view([90, 12, 76], 120, 0, 50), focus: ['OUT1'] },
    { kind: 'tell', title: 'Power it', say: 'Plug in the power supply. The guide called Powering the board safely shows how. The controller module’s light comes on. After about ten seconds the board is ready.',
      cam: view([110, 10, -40], 260, 160, 35), focus: ['BARREL'] },
    { kind: 'tell', id: 'connect', title: 'Connect your phone', say: 'On your phone, open the Wi-Fi settings. Join the network called Proptroller, dash, and four letters or numbers. The password is the word password, all lower case. Then open the web browser and type one nine two, dot, one six eight, dot, four, dot, one.',
      text: 'On your phone, open the Wi-Fi settings. Join the network called Proptroller-XXXX (the last four letters or numbers differ from board to board). The password is password, all lower case (a simple default: you can change it later in Settings, Wi-Fi). Then open the web browser and type 192.168.4.1.',
      cam: view([67, 10, 40], 300, 15, 40), inset: { type: 'wifi' } },
    { kind: 'test', title: 'What you should see', say: 'The board’s page opens on the Live tab. At the top is the board’s name and a red STOP button. Below are cards: Status, Shows, Inputs and Outputs. The Inputs card shows a box for each input. A box lights up orange while that input is pressed.',
      cam: view([67, 10, 40], 300, 15, 40),
      inset: { type: 'phone', tab: 'Live', cardTitle: 'Inputs', body: chips(['TRIG1', 'TRIG2', 'USER1', 'USER2', 'USER3'], 'TRIG1'), note: 'A box lights orange while its input is closed. Press USER1 on the board to try it.' } },
    { kind: 'tell', title: 'STOP and Auto', say: 'The red STOP button stops every show and lets go of every output you switched by hand. On the Outputs card, On and Off take over an output for a test. Auto gives it back to your shows. Always press Auto after a test.',
      cam: view([67, 10, 40], 300, 15, 40),
      inset: { type: 'phone', tab: 'Live', cardTitle: 'Outputs', body: '<div class="wg-ph-out"><b>OUT1</b><span>On</span><span>Off</span><span class="pressed">Auto</span></div>', note: 'On or Off holds the output until you press Auto or STOP.' } },
    { kind: 'tell', title: 'Your home Wi-Fi (optional)', say: 'To use your home Wi-Fi too: on the Settings tab, fill in Wi-Fi network and Wi-Fi password, press Save settings, then Restart. The board’s own network stays on. On your home Wi-Fi, open the board’s name followed by dot local. In Specter Studio, boards on your network show up by themselves.',
      text: 'To use your home Wi-Fi too: on the Settings tab, fill in "Wi-Fi network" and "Wi-Fi password", press Save settings, then Restart. The board’s own Proptroller-XXXX network stays on, and the top of its Live tab there shows its address on your home Wi-Fi. On your home Wi-Fi, open the board’s name followed by .local (the Name setting, for example http://proptroller.local). In Specter Studio, boards on your network show up by themselves.',
      cam: view([67, 10, 40], 300, 15, 40) },
    trouble(view([67, 10, 40], 300, 15, 40)),
    next(view([67, 10, 40], 300, 15, 40), { say: 'Next, power the board safely, then try the starter kit, or any single guide.' }),
  ],
  trouble: [
    'No Proptroller network: wait 30 seconds after power-on. Is the controller module’s light on? The board makes this network even when it is on your home Wi-Fi.',
    'The phone joins but the page does not open: turn mobile data off for a moment, and type the whole address: http://192.168.4.1',
    'The phone says "no internet" and leaves the network: choose "stay connected" or "use this network anyway".',
    'Name.local does not open (some Android phones): use the board’s address from your router, or go back to the Proptroller network.',
  ],
  next: ['08', 'kit', '01'],
};
