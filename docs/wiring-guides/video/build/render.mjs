// Render the guide videos (1080p MP4) into out/: `npm run render` for all, `npm run render -- 01 kit` for some.
// Encoding is tuned for slide-like scenes (mostly still frames, slow camera moves, flat colours): x264 'slow' at CRF 28
// and 64 kbit/s AAC for the voice: the 3.4-minute starter kit is 14.5 MB (72 KB per second; it was 37.8 MB at CRF 20
// with 317 kbit/s audio), labels and captions still crisp, fine silkscreen a little softer during camera moves.
// CRF=, X264_PRESET= and AUDIO_BITRATE= override them.
// Needs the narration (npm run narrate) first. Uses Remotion's renderer with headless Chrome and SwiftShader WebGL.
import { execFileSync } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import { GUIDES } from '../src/guides/all.js';

const want = process.argv.slice(2);
mkdirSync('out', { recursive: true });
mkdirSync('.cache/tmp', { recursive: true });   // frames go to disk here, not to a RAM-backed /tmp
for (const g of GUIDES.filter((x) => !want.length || want.includes(x.id))) {
  const out = `out/${g.id}-${g.slug}.mp4`;
  const args = ['remotion', 'render', 'src/remotion/index.jsx', `g-${g.id}`, out, '--gl=swangle', `--concurrency=${process.env.RENDER_CONCURRENCY || 4}`, `--crf=${process.env.CRF || 28}`,
    `--x264-preset=${process.env.X264_PRESET || 'slow'}`, `--audio-bitrate=${process.env.AUDIO_BITRATE || '64k'}`, '--log=warn'];
  if (process.env.REMOTION_BROWSER) args.push(`--browser-executable=${process.env.REMOTION_BROWSER}`);
  execFileSync('npx', args, { stdio: 'inherit', env: { ...process.env, TMPDIR: `${process.cwd()}/.cache/tmp` } });
  console.log('rendered', out);
}
