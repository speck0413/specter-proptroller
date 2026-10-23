// Write the one-page written version of each guide (docs/wiring-guides/NN-slug.md) from the same step data the
// interactive pages and the videos use, in the same order (src/guides/template.js). The starter kit has its own
// hand-written page (starter-kit/README.md). Steps are picked by `kind`, never by title.
import { writeFileSync } from 'node:fs';
import { GUIDES } from '../src/guides/all.js';
import { LEVELS, SAFETY } from '../src/guides/template.js';
import { termsIn } from '../src/guides/glossary.js';

const md = (g) => (g.id === 'kit' ? 'starter-kit/README.md' : `${g.id}-${g.slug}.md`);
const clean = (t) => t.replace(/`/g, '`').replace(/ /g, ' ');
for (const g of GUIDES.filter((x) => x.id !== 'kit')) {
  const body = g.steps.filter((s) => !['intro', 'need', 'before', 'trouble', 'next'].includes(s.kind));
  const before = g.steps.find((s) => s.kind === 'before');
  const words = termsIn(g.steps.map((s) => s.text ?? s.say).join(' '));
  const lines = [
    `# ${g.title}`, '',
    `${g.blurb}`, '',
    `**Time:** ${g.time} · **Tools:** ${LEVELS[g.level] ?? g.level}`, '',
    ...(g.firmware ? [`> **Firmware:** ${g.firmware}`, ''] : []),
    `[![${g.title}](html/posters/${g.id}-${g.slug}.webp)](html/${g.id}-${g.slug}.html)`, '',
    `**Open the interactive version:** [html/${g.id}-${g.slug}.html](html/${g.id}-${g.slug}.html) (turn the 3D scene, step through, hear each step).`, '',
    '## You need', '', ...g.need.map((p) => `- [ ] ${p}`), '',
    '## Before you start', '', ...SAFETY.map((t) => `- ${t}.`), ...(before?.card?.dont ?? []).map((t) => `- **Don’t:** ${t}.`), '',
    ...(before?.warn ? [`> **Warning:** ${before.warn}`, ''] : []),
    '## Steps', '',
    ...body.flatMap((s, i) => {
      const pre = s.only === 'case' ? '*In the case:* ' : s.only === 'bare' ? '*Bare board:* ' : '';
      const out = [`${i + 1}. ${pre}**${s.title}.** ${clean(s.text ?? s.say)}`];
      if (s.warn) out.push(`   > **Warning:** ${s.warn}`);
      if (s.kind === 'test' && g.id !== '00') out.push('   (How to open the board’s page: [Meet your board](00-meet-your-board.md), step "Connect your phone".)');
      return out;
    }), '',
    '## Not working?', '', ...g.trouble.map((t) => `- ${t}`), '',
    ...(g.next?.length ? ['## Next', '', ...g.next.map((n) => GUIDES.find((x) => x.id === n)).filter(Boolean).map((x) => `- [${x.title}](${md(x)})`), ''] : []),
    ...(words.length ? ['## Words in this guide', '', ...words.map((w) => `- **${w.name}**: ${w.def}`), ''] : []),
  ];
  writeFileSync(`../${g.id}-${g.slug}.md`, lines.join('\n'));
  console.log('wrote', `${g.id}-${g.slug}.md`, body.length, 'steps');
}
