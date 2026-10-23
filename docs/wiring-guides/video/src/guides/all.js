// Every guide. GUIDES is the build list; ORDER is how the index page groups them (start here first: meet and connect
// to the board, power it, then the starter kit).
import meet from './00-meet.js';
import kit from './00-starter-kit.js';
import g01 from './01-button.js';
import g02 from './02-lamp.js';
import g03 from './03-motion.js';
import g04 from './04-speaker.js';
import g05 from './05-pixels.js';
import g06 from './06-fog-dmx.js';
import g07 from './07-air-valve.js';
import g08 from './08-power.js';
import g09 from './09-chain.js';
import g10 from './10-pump-motor.js';
export const GUIDES = [meet, kit, g01, g02, g03, g04, g05, g06, g07, g08, g09, g10].filter(Boolean);
export const ORDER = [
  { title: 'Start here', ids: ['00', '08', 'kit'] },
  { title: 'Inputs: start a show', ids: ['01', '03', '09'] },
  { title: 'Outputs: make things happen', ids: ['02', '07', '10', '05', '04', '06'] },
];
