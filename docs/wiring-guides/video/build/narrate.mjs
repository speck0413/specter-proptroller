// Turn each step's `say` text into narration: Piper (offline neural TTS) -> WAV (for the videos, in .cache/) and
// low-bitrate Opus plus an AAC .m4a fallback (for the HTML pages, in ../html/audio/), plus src/generated/narration.json with every step's length
// in seconds, which both the pages and the videos time their steps by. Re-runs only the steps whose text changed.
// PIPER (the piper binary) and PIPER_VOICE (an .onnx voice) override the defaults under ~/.cache/proptroller-guides/.
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, readdirSync, unlinkSync, writeFileSync } from 'node:fs';
import { homedir } from 'node:os';
import { GUIDES } from '../src/guides/all.js';

const C = `${homedir()}/.cache/proptroller-guides`;
const PIPER = process.env.PIPER || `${C}/piper-dl/piper/piper`;
const VOICE = process.env.PIPER_VOICE || `${C}/voices/en_US-ljspeech-high.onnx`;
const WAV = '.cache/narration', OPUS = '../html/audio';
const PACE = ['--length_scale', '1.22', '--sentence_silence', '0.55'];   // about 135 words a minute, a pause after each sentence
mkdirSync(WAV, { recursive: true }); mkdirSync(OPUS, { recursive: true });
if (!existsSync(PIPER) || !existsSync(VOICE)) {
  console.error(`Piper or its voice is missing (${PIPER}, ${VOICE}). See ../README.md, "Building the guides".`); process.exit(1);
}
// How the words should sound (the text on screen keeps the board's names).
const spoken = (t) => t
  .replace(/\bWS2812B?\b/g, 'W S twenty-eight twelve').replace(/\bAM312\b/g, 'A M three twelve').replace(/\bHC[ -]?SR501\b/g, 'H C S R five oh one')
  .replace(/\b1N4007\b/g, 'one N four oh oh seven').replace(/\b1N5819\b/g, 'one N fifty-eight nineteen').replace(/\bMAX485\b/g, 'max four eighty-five')
  .replace(/\bRS-485\b/g, 'R S four eighty-five').replace(/\bXLR\b/g, 'X L R').replace(/\bDMX\b/g, 'D M X').replace(/\bJST\b/g, 'J S T')
  .replace(/\bto 5V\b/g, 'to the screw marked five V').replace(/\b5V\b/g, 'five V').replace(/\b3V3\b/g, 'three point three volts')
  .replace(/\bTRIG ?(\d)/g, 'trig $1').replace(/\bOUT ?(\d)/g, 'out $1').replace(/\bRLY ?(\d)/g, 'relay $1').replace(/\bS4\b/g, 'S four')
  .replace(/\bCC ?(\d)/g, 'C C $1').replace(/\bSPK\b/g, 'speaker').replace(/\bPIR\b/g, 'P I R').replace(/\bGND\b/g, 'ground')
  .replace(/(\d+(?:\.\d+)?) ?mm\b/g, '$1 millimetre').replace(/(\d+) ?V\b/g, '$1 volt').replace(/(\d+) ?A\b/g, '$1 amp')
  .replace(/\bNO\b/g, 'N O').replace(/\bNC\b/g, 'N C').replace(/\bVIN\b/g, 'V in').replace(/\bDIN\b/g, 'D in').replace(/\bCOM\b/g, 'C O M')
  .replace(/\bSD\b/g, 'S D').replace(/\bLED\b/g, 'L E D').replace(/\bLEDs\b/g, 'L E Ds').replace(/\bUI\b/g, 'U I').replace(/’/g, "'");
const manifest = existsSync('src/generated/narration.json') ? JSON.parse(readFileSync('src/generated/narration.json')) : {};
const hashes = existsSync(`${WAV}/hashes.json`) ? JSON.parse(readFileSync(`${WAV}/hashes.json`)) : {};
for (const g of GUIDES) {
  manifest[g.id] = [];
  g.steps.forEach((s, i) => {
    const n = `${g.id}-${String(i + 1).padStart(2, '0')}`, text = spoken(s.say);
    const hash = createHash('sha1').update(VOICE + PACE.join(' ') + text).digest('hex');
    if (hashes[n] !== hash || !existsSync(`${WAV}/${n}.wav`) || !existsSync(`${OPUS}/${n}.opus`) || !existsSync(`${OPUS}/${n}.m4a`)) {
      execFileSync(PIPER, ['-m', VOICE, ...PACE, '-f', `${WAV}/${n}.wav`], { input: text, stdio: ['pipe', 'ignore', 'ignore'] });
      execFileSync('ffmpeg', ['-y', '-loglevel', 'error', '-i', `${WAV}/${n}.wav`, '-ac', '1', '-c:a', 'libopus', '-b:a', '24k', '-application', 'voip', `${OPUS}/${n}.opus`]);
      // AAC fallback for browsers without Ogg Opus (macOS / iOS WebKit)
      execFileSync('ffmpeg', ['-y', '-loglevel', 'error', '-i', `${WAV}/${n}.wav`, '-ac', '1', '-c:a', 'aac', '-b:a', '24k', '-movflags', '+faststart', `${OPUS}/${n}.m4a`]);
      hashes[n] = hash; console.log('spoke', n);
    }
    const d = Number(execFileSync('ffprobe', ['-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', `${WAV}/${n}.wav`]).toString());
    manifest[g.id].push(Math.round(d * 100) / 100);
  });
}
// drop the audio and lengths of steps and guides that no longer exist (a guide that lost steps, a removed guide)
for (const k of Object.keys(manifest)) if (!GUIDES.some((g) => g.id === k)) delete manifest[k];
const keep = new Set(GUIDES.flatMap((g) => g.steps.map((s, i) => `${g.id}-${String(i + 1).padStart(2, '0')}`)));
for (const f of readdirSync(OPUS)) { const n = f.replace(/\.(opus|m4a)$/, ''); if (n !== f && !keep.has(n)) { unlinkSync(`${OPUS}/${f}`); console.log('removed', f); } }
writeFileSync(`${WAV}/hashes.json`, JSON.stringify(hashes));
writeFileSync('src/generated/narration.json', JSON.stringify(manifest, null, 1) + '\n');
const words = (g) => g.steps.reduce((a, s) => a + spoken(s.say).split(/\s+/).length, 0);
console.log('narration.json:', GUIDES.map((g) => { const t = manifest[g.id].reduce((a, b) => a + b, 0); return `${g.id} ${t.toFixed(0)} s (${(words(g) / t * 60).toFixed(0)} wpm)`; }).join(', '));
