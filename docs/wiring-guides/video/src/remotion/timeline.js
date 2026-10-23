// The video timeline of a guide: a title card, then each step for its narration plus a pause, then an end card.
import narration from '../generated/narration.json';
export const FPS = 30, INTRO = 3, OUTRO = 4, PAUSE = 1.1;
export function timeline(g) {
  const d = narration[g.id] || [];
  let at = INTRO;
  const steps = g.steps.map((s, i) => { const len = Math.max(3, (d[i] || 3) + PAUSE); const r = { i, from: at, len, voice: d[i] || 0 }; at += len; return r; });
  return { steps, total: at + OUTRO };
}
