// Guide 4: a speaker and sound files. DFPlayer facts (hardware/README.md): SPK 3 W into 4 ohm, bridge-tied (never
// ground a speaker wire), LINE OUT for an amplifier; sound N is the card's mp3/NNNN-….mp3, numbered in write order; Studio's SD card
// tool fills the card (SHOW-SCRIPT SS-C4, docs/SD-CARD.md). The case's SPK jack has no defined wiring yet (enclosure, owner): the guide stays safe.
import { view } from '../scene/view.js';
import { intro, need, before, tug, trouble, next } from './template.js';

export default {
  id: '04', slug: 'speaker-and-sounds', title: 'A speaker and sound files',
  posterStep: 4, time: '15 minutes', level: 'screwdriver',
  blurb: 'Wire a speaker to SPK and put your sounds on the memory card so a show can play them.',
  need: [
    'A passive speaker (no power cord or battery), 4 Ω or more, rated 3 W or more',
    'A microSD card, 32 GB or smaller, formatted FAT32',
    'Two wires (any colour), a small flat screwdriver and wire strippers',
    'Your sounds as MP3 files',
  ],
  build(k) {
    k.add('speaker', k.props.speaker(), { pos: [150, 26, -75], label: 'speaker, 4 Ω or more', labelAt: [0, 34, 0] });
    k.wire('a', k.at('SPK', '2'), k.at('speaker', 'a'), { color: 'white', label: 'one wire to each SPK screw', leadA: 10 });
    k.wire('b', k.at('SPK', '1'), k.at('speaker', 'b'), { color: 'grey', leadA: 10 });
    k.add('card', k.props.sdCard(), { pos: [139, 29.2, 28], from: [40, 0, 0], label: 'microSD card', labelAt: [0, 8, 0] });
    k.note('lineout', 'LINE OUT: for an amplifier or powered speaker', [134, 20, 44], 'wg-note');
    k.add('caseplug', k.props.trsPlug(), { pos: k.plateJack('back', 101.6).add({ x: 0, y: 0, z: -2 }).toArray(), from: [0, 0, -60], label: 'SPK jack, back plate', labelAt: [0, 12, -10] });
  },
  steps: [
    intro(view([110, 15, -10], 280, 150, 40), 'In this guide you will wire a speaker to the board and put your sound files on a memory card, so a show can play them. It takes about fifteen minutes.', { title: 'Sound', focus: ['SPK', 'DFPLAYER'] }),
    need(view([150, 15, -60], 250, 160, 30), 'You need a plain passive speaker, four ohms or more, rated three watts or more, and a micro S D card of thirty two gigabytes or less, formatted FAT 32.', { show: ['speaker'] }),
    before(view([130, 15, -20], 300, 150, 40), ['No speaker wire to ground or any other screw', 'Never SPK into an amplifier, headphones or an aux input', 'No card bigger than 32 GB'],
      { extra: 'Neither speaker wire may touch ground. For an amplifier, use LINE OUT.' }),
    { kind: 'find', only: 'bare', title: 'Find SPK', say: 'SPK is the two screw terminal at the back right corner.',
      cam: view([119, 15, 0], 150, 160, 35), focus: ['SPK'] },
    { kind: 'find', only: 'case', title: 'In the case', say: 'In the case, the speaker plugs into the SPK jack on the back plate. Only plug in a speaker lead made for that jack. Never an amplifier, headphones or an aux cable. If the plate does not say how the jack is wired, open the lid and use the SPK screws instead.',
      cam: view([100, 25, -50], 260, 170, 20), case: 'closed', show: ['caseplug'],
      warn: 'The case’s SPK jack looks like a headphone jack but is not one. Speaker only.' },
    { kind: 'wire', title: 'One wire to each screw', say: 'Put one speaker wire under each SPK screw. Either way round works.',
      cam: view([135, 10, -40], 220, 140, 40), draw: ['a', 'b'], focus: ['SPK'] },
    tug(view([125, 10, -20], 150, 150, 35)),
    { kind: 'wire', title: 'Amplifiers: LINE OUT', say: 'Both SPK wires carry sound, so joining one to an amplifier’s ground can damage the sound module. For an amplifier or a powered speaker, use the LINE OUT jack on the right edge instead, with an ordinary audio cable.',
      cam: view([130, 15, 20], 260, 110, 40), show: ['lineout'], focus: ['LINE OUT'] },
    { kind: 'tell', title: 'Sounds on the card: with Studio', say: 'The easy way: in Specter Studio, open the SD card tool, add your sound files and put them in order. It erases the card, names the files and copies them in the right order. The first file is sound one, the second sound two.',
      text: 'The easy way: in Specter Studio, open the SD card tool, add your sound files and put them in order. It erases the card, names the files and copies them in the right order: the first is sound 1, the second sound 2.',
      cam: view([128, 25, 28], 160, 70, 35), hide: ['lineout'], focus: ['DFPLAYER'] },
    { kind: 'tell', title: 'Without Studio: the mp3 folder', say: 'Without Studio, make a folder called M P 3 on an empty card. Copy your sounds into it one at a time, in order, named zero zero zero one dash and a name, then zero zero zero two dash and a name. The player counts the files in the order they were copied.',
      text: 'Without Studio, make a folder called mp3 on an empty card. Copy your sounds into it one at a time, in order, named 0001-name.mp3, 0002-name.mp3. The player counts the files in the order they were copied (docs/SD-CARD.md).',
      cam: view([128, 25, 28], 160, 70, 35), inset: { type: 'folders' } },
    { kind: 'wire', title: 'Card in', say: 'With the power off, slide the card into the slot on the sound module, at the right hand edge of the board, contacts first.',
      cam: view([128, 25, 28], 140, 70, 35), show: ['card'], focus: ['DFPLAYER'] },
    { kind: 'test', title: 'Test it', say: 'Power the board and open its page. On the Live tab, in the Sound card, set Sound to one and press Play. Your first sound plays. The Volume slider sets how loud.',
      cam: view([130, 15, -20], 300, 150, 40), live: { speaker: 1 },
      inset: { type: 'phone', tab: 'Live', cardTitle: 'Sound', body: '<div class="wg-ph-set"><label>Sound</label><span class="wg-ph-sel">1</span></div><div class="wg-ph-out"><span class="pressed">Play</span><span>Stop</span><b style="text-align:right">Volume ━━●━</b></div>', note: 'Sound 1, <b>Play</b>.' } },
    { kind: 'show', title: 'Use it in a show', text: 'In a show script, `audio_single = 1` plays sound 1 once, `audio_loop = 2` loops sound 2, and `audio_volume = 60%` sets how loud the script plays them. The board\'s own sounds: Settings > Sound > "Trigger sound volume" and "Ambient volume", then Save settings.',
      say: 'In a show script, audio single equals one plays sound one once. How loud it plays is set under Settings, Sound: trigger sound volume and ambient volume.',
      cam: view([130, 15, -20], 300, 150, 40) },
    trouble(view([130, 15, -20], 300, 150, 40)),
    next(view([110, 15, -10], 280, 150, 40)),
  ],
  trouble: [
    'No sound: is the card in all the way, contacts first? Without Studio: is there a folder named mp3 with files named 0001-….mp3, 0002-….mp3?',
    'The wrong sound plays: the files were copied out of order. Studio\'s SD card tool (Check a card, then Fix) copies them again in order. Some computers add hidden files that begin with a dot; the tool removes those.',
    'Hum or buzz through an amplifier on LINE OUT: use a 3.5 mm ground-loop isolator on the LINE OUT lead.',
    'Quiet: raise the Volume slider on the Live tab, the volumes under Settings > Sound, or use LINE OUT with an amplifier.',
  ],
  next: ['kit', '03', '05'],
};
