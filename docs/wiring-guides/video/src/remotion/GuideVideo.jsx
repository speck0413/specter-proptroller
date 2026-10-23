// One guide as a video: the shared scene, driven frame by frame from the timeline, with the narration, the step
// title and its words as captions (it works with the sound off), and the do / don't card.
import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { AbsoluteFill, Audio, Sequence, continueRender, delayRender, staticFile, useCurrentFrame, interpolate } from 'remotion';
import { createScene, lerpCam } from '../scene/index.js';
import { GUIDES } from '../guides/all.js';
import { timeline, FPS, INTRO } from './timeline.js';
import caseUrl from '../../assets/case.glb';
import boardTexture from '../../assets/board-top.webp';
import '../player/style.css';
import { insetHTML } from '../player/insets.js';
import { LEVELS } from '../guides/template.js';

const big = { fontFamily: 'system-ui, sans-serif', color: '#fff' };

export const GuideVideo = ({ id }) => {
  const g = GUIDES.find((x) => x.id === id);
  const tl = timeline(g);
  const frame = useCurrentFrame(), sec = frame / FPS;
  const canvas = useRef(null), host = useRef(null);
  const [S, setS] = useState(null);
  const [handle] = useState(() => delayRender('scene'));
  useEffect(() => {
    let alive = true;
    (async () => {
      const glb = await (await fetch(caseUrl)).arrayBuffer();
      const s = await createScene({ canvas: canvas.current, labelHost: host.current, caseGlb: glb, boardTexture, preserve: true });
      s.load(g); s.resize(1920, 1080); s.renderer.setPixelRatio(1); s.resize(1920, 1080);
      if (alive) { setS(s); continueRender(handle); }
    })();
    return () => { alive = false; };
  }, []);
  const cur = tl.steps.findLast((s) => sec >= s.from) || tl.steps[0];
  const local = Math.max(0, sec - cur.from);
  const step = g.steps[cur.i];
  useLayoutEffect(() => {
    if (!S) return;
    const prev = cur.i > 0 ? g.steps[cur.i - 1].cam : step.cam;
    const intro = sec < INTRO;
    // during the title card, a slow turn round the first view
    const cam = intro ? [...step.cam] : lerpCam(prev, step.cam, Math.min(1, local / 1.4));
    if (intro) { const a = (sec - INTRO) * 0.08, dx = cam[0] - cam[3], dz = cam[2] - cam[5];
      cam[0] = cam[3] + dx * Math.cos(a) - dz * Math.sin(a); cam[2] = cam[5] + dx * Math.sin(a) + dz * Math.cos(a); }
    S.setCamera(cam);
    const anim = Math.max(1.8, Math.min(4, cur.voice * 0.6));
    S.show(intro ? 0 : cur.i, intro ? 0 : Math.min(1, local / anim), sec);
    S.render();
  });
  const end = sec > tl.total - 4;
  const fade = (from, len = 0.4) => interpolate(sec, [from, from + len], [0, 1], { extrapolateLeft: 'clamp', extrapolateRight: 'clamp' });
  return (
    <AbsoluteFill style={{ background: '#20252b' }}>
      <canvas ref={canvas} width={1920} height={1080} style={{ width: 1920, height: 1080 }} />
      <div ref={host} style={{ position: 'absolute', inset: 0, fontSize: 22 }} className="wg-video-labels" />
      {tl.steps.map((s) => (
        <Sequence key={s.i} from={Math.round((s.from + 0.25) * FPS)} durationInFrames={Math.round(s.len * FPS)}>
          <Audio src={staticFile(`narration/${g.id}-${String(s.i + 1).padStart(2, '0')}.wav`)} />
        </Sequence>
      ))}
      {sec < INTRO && (
        <AbsoluteFill style={{ background: 'rgba(15,18,22,.72)', justifyContent: 'center', padding: 140, opacity: 1 - fade(INTRO - 0.5, 0.5) }}>
          <div style={{ ...big, fontSize: 40, color: '#ffcc33', fontWeight: 700 }}>{g.id === 'kit' ? 'PROPTROLLER STARTER KIT' : `PROPTROLLER WIRING GUIDE ${Number(g.id)}`}</div>
          <div style={{ ...big, fontSize: 92, fontWeight: 800, lineHeight: 1.05, marginTop: 16 }}>{g.title}</div>
          <div style={{ ...big, fontSize: 38, color: '#cfd6dd', marginTop: 24, maxWidth: 1400 }}>{g.blurb}</div>
          <div style={{ ...big, fontSize: 32, color: '#ffcc33', marginTop: 24 }}>{g.time} · {LEVELS[g.level]}</div>
        </AbsoluteFill>
      )}
      {sec >= INTRO && !end && (
        <>
          <div style={{ position: 'absolute', left: 60, top: 48, opacity: fade(cur.from) }}>
            <div style={{ ...big, fontSize: 28, color: '#ffcc33', fontWeight: 700 }}>STEP {cur.i + 1} OF {g.steps.length}</div>
            <div style={{ ...big, fontSize: 64, fontWeight: 800, textShadow: '0 3px 12px rgba(0,0,0,.7)' }}>{step.only === 'case' && !/^In the case/.test(step.title) ? 'In the case: ' : step.only === 'bare' ? 'Bare board: ' : ''}{step.title}</div>
          </div>
          <div style={{ position: 'absolute', left: 160, right: 160, bottom: 50, padding: '20px 32px', borderRadius: 16, background: 'rgba(12,14,18,.82)', opacity: fade(cur.from) }}>
            <div style={{ ...big, fontSize: 40, lineHeight: 1.3, textAlign: 'center' }}>{(step.text ?? step.say).replace(/`/g, '')}</div>
          </div>
          {(step.inset || ['need', 'trouble'].includes(step.kind) || step.warn) && (
            <div className="wg-video-side" style={{ position: 'absolute', right: 60, top: step.card ? 560 : 200, width: 560, opacity: fade(cur.from + 0.3) }}>
              {step.warn && <div className="wg-warnbox">⚠ {step.warn}</div>}
              {step.kind === 'need' && <ul className="wg-check">{g.need.map((t) => <li key={t}>☐ {t}</li>)}</ul>}
              {step.kind === 'trouble' && <ul className="wg-trouble">{g.trouble.map((t) => <li key={t}>{t}</li>)}</ul>}
              {step.inset && <div className="wg-insetbox" dangerouslySetInnerHTML={{ __html: insetHTML(step.inset) }} />}
            </div>
          )}
          {step.card && (
            <div style={{ position: 'absolute', right: 60, top: 200, width: 900, display: 'flex', gap: 18, opacity: fade(cur.from + 0.3), fontSize: 26 }}>
              <div className="wg-do" style={{ ...big, fontSize: 26, padding: 22, borderRadius: 14 }}><b style={{ fontSize: 34 }}>{step.kind === 'before' ? 'ALWAYS' : 'DO'}</b>{step.card.do.map((t) => <div key={t}>{'✔ ' + t}</div>)}</div>
              <div className="wg-dont" style={{ ...big, fontSize: 26, padding: 22, borderRadius: 14 }}><b style={{ fontSize: 34 }}>DON'T</b>{step.card.dont.map((t) => <div key={t}>{'✖ ' + t}</div>)}</div>
            </div>
          )}
        </>
      )}
      {end && (
        <AbsoluteFill style={{ background: 'rgba(15,18,22,.8)', justifyContent: 'center', padding: 140, opacity: fade(tl.total - 4, 0.6) }}>
          <div style={{ ...big, fontSize: 80, fontWeight: 800 }}>{g.id === 'kit' ? 'Now make it bigger.' : 'Done.'}</div>
          <div style={{ ...big, fontSize: 40, color: '#cfd6dd', marginTop: 20 }}>Turn it around yourself: the interactive guide, in Specter Studio under Help &gt; Wiring guides.</div>
          <div style={{ ...big, fontSize: 32, color: '#ffcc33', marginTop: 30 }}>Low voltage only. Power off while you wire.</div>
        </AbsoluteFill>
      )}
    </AbsoluteFill>
  );
};
