// Remotion settings for the wiring-guide videos: the narration WAVs are the public folder, the case model loads as a file.
import { Config } from '@remotion/cli/config';

Config.setPublicDir('.cache');
Config.setVideoImageFormat('jpeg');
Config.setChromiumOpenGlRenderer('swangle');
Config.overrideWebpackConfig((c) => ({
  ...c, module: { ...c.module, rules: [...(c.module?.rules ?? []), { test: /\.glb$/, type: 'asset/resource' }] },
}));
