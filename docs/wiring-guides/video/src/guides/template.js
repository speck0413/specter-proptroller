// The shared shape of every guide (same order in the interactive page, the video and the written page):
//   intro (outcome, time, difficulty, case or bare) -> need (checklist) -> before (the same safety box everywhere plus
//   the guide's own don'ts) -> find -> wire (one connection per step) -> tug -> tell (exact UI path) -> test (exact
//   tab / card / control and what you should see) -> show (one script line) -> trouble -> next.
// Each step carries a `kind`; the player, the video and markdown.mjs read it (never the step title).
// A step with `only: 'case'` or `only: 'bare'` is shown only when the reader picked that build on the first step.

export const LEVELS = {
  none: 'No tools',
  screwdriver: 'Small screwdriver',
  solder: 'Soldering needed',
  'screwdriver+solder': 'Screwdriver; soldering only on one path',
};

// The safety box every guide shows before any wiring.
export const SAFETY = [
  'Unplug the power supply while you wire',
  'Low voltage only: nothing here touches wall power',
  'Strip about 7 mm, twist, tighten each screw on bare copper, tug each wire',
];

export const intro = (cam, say, o = {}) => ({ kind: 'intro', title: o.title ?? 'What you will build', say, cam, ...o });
export const need = (cam, say, o = {}) => ({ kind: 'need', title: 'You need', say, cam, ...o });
export const before = (cam, dont, o = {}) => ({
  kind: 'before', title: 'Before you start', cam,
  say: o.say ?? `Before you start: unplug the power while you wire, and keep to low voltage. ${o.extra ?? ''}`.trim(),
  card: { do: SAFETY, dont }, ...o,
});
export const tug = (cam, o = {}) => ({
  kind: 'tug', title: 'Tug test', cam,
  say: 'Give each wire a gentle tug. If one pulls out, open the screw and do it again. Check that no stray strand touches the next screw.', ...o,
});
export const trouble = (cam, o = {}) => ({ kind: 'trouble', title: 'Not working?', cam, say: o.say ?? 'Not working? Check these, starting at the top of the list.', ...o });
export const next = (cam, o = {}) => ({ kind: 'next', title: 'Next', cam, say: o.say ?? 'That is it. Next, try one of the guides on the list.', ...o });

// The phone page path used in Test steps; the player links "How do I open the board's page?" to guide 00.
export const CONNECT = { guide: '00', step: 'connect' };
