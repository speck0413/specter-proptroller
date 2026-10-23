import { registerRoot, Composition } from 'remotion';
import { GUIDES } from '../guides/all.js';
import { GuideVideo } from './GuideVideo.jsx';
import { timeline, FPS } from './timeline.js';

const Root = () => (
  <>
    {GUIDES.map((g) => (
      <Composition key={g.id} id={`g-${g.id}`} component={GuideVideo} defaultProps={{ id: g.id }}
        durationInFrames={Math.round(timeline(g).total * FPS)} fps={FPS} width={1920} height={1080} />
    ))}
  </>
);
registerRoot(Root);
