// The Script tab's views: "Script and blocks", "Script only", "Blocks only" (as in Specter Studio's effect editor),
// remembered on this phone. The blocks are drawn from blocks.js (the shared, Blockly-free text ⇄ blocks module)
// and the text stays the source: a block edit writes the text box, typing in the text box redraws the blocks.
// Blocks: change a value in place, move with ↑ ↓ (or drag by ⠿ with a mouse), remove with ✕, add a line to an
// effect or a new effect. An ES module loaded after app.js (uses $, el, store, pageHooks, goToLine).
import { textToBlocks, blocksToText, newBlock, KINDS, ADDABLE } from './blocks.js';

const VIEWS = ['both', 'text', 'blocks'];
const ta = $('script-text');
let model = textToBlocks(ta.value);
let drag = null;  // { list, i } of the block being dragged

// ------------------------------------------------------------------ the view choice

const view = () => document.querySelector('input[name=script-view]:checked')?.value || 'text';
function setView(v, remember = true) {
  if (!VIEWS.includes(v)) v = 'text';
  for (const r of document.querySelectorAll('input[name=script-view]')) r.checked = r.value === v;
  const p = $('script-panes');
  p.classList.remove(...VIEWS);
  p.classList.add(v);
  if (remember) store.set('scriptView', v);
  if (v !== 'text') { model = textToBlocks(ta.value); render(); }
}
for (const r of document.querySelectorAll('input[name=script-view]')) r.addEventListener('change', () => setView(r.value));
// the phone remembers; the first time, the script alone on a small screen and both side by side on a big one
setView(store.get('scriptView', '') || (window.innerWidth < 760 ? 'text' : 'both'), false);

// ------------------------------------------------------------------ text ⇄ blocks

// a block changed: the text box gets the new text (the Script tab's Save, Check and "not saved" read it there)
function commit(redraw) {
  ta.value = blocksToText(model);
  if (redraw) render();
}
let typing = null;
ta.addEventListener('input', () => {
  clearTimeout(typing);
  typing = setTimeout(() => { if (view() !== 'text') { model = textToBlocks(ta.value); render(); } }, 250);
});
pageHooks.script.push(() => { model = textToBlocks(ta.value); if (view() !== 'text') render(); });

// black or white words on a block colour, whichever reads better
function ink(hex) {
  const c = [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16) / 255).map((v) => (v <= 0.03928 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4));
  const l = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
  return (l + 0.05) / 0.05 >= 1.05 / (l + 0.05) ? '#111' : '#fff';
}
// what a block says, for button names ("sleep 1")
const said = (b) => KINDS[b.type].words.map((w) => (w in KINDS[b.type].fields ? b.f[w] : w)).join(' ').trim();

function field(b, k) {
  const i = el('input', { value: b.f[k], 'aria-label': `${k.toLowerCase()} of "${said(b)}"`, autocapitalize: 'off', autocomplete: 'off', spellcheck: 'false' });
  i.setAttribute('autocorrect', 'off');
  const size = () => { i.style.width = `${Math.max(4, i.value.length + 2)}ch`; };
  size();
  i.addEventListener('input', () => { b.f[k] = i.value; size(); commit(false); });
  return i;
}
function controls(list, i, b) {
  const move = (d) => {
    const j = i + d;
    if (j < 0 || j >= list.length) return;
    [list[i], list[j]] = [list[j], list[i]];
    commit(true);
    // the moved block keeps the focus on the same arrow
    const btn = document.querySelector(`#script-blocks [data-key="${keyOf(list, j)}"] .ctl button[data-d="${d}"]`);
    btn?.focus();
  };
  return el('span', { class: 'ctl' },
    el('button', { type: 'button', class: 'small', 'data-d': -1, 'aria-label': `Move up: ${said(b)}`, title: 'Move up', disabled: i === 0, onclick: () => move(-1) }, '↑'),
    el('button', { type: 'button', class: 'small', 'data-d': 1, 'aria-label': `Move down: ${said(b)}`, title: 'Move down', disabled: i === list.length - 1, onclick: () => move(1) }, '↓'),
    el('button', { type: 'button', class: 'small', 'aria-label': `Remove: ${said(b)}`, title: 'Remove', onclick: () => { list.splice(i, 1); commit(true); } }, '✕'));
}
const keys = new WeakMap();
let nextKey = 1;
const keyOf = (list, i) => { const b = list[i]; if (!keys.has(b)) keys.set(b, nextKey++); return keys.get(b); };

// drag by the grip (a mouse): drop on another block of the same kind of list to put it there
function draggable(node, list, i) {
  const grip = el('span', { class: 'grip', title: 'Drag to move', 'aria-hidden': 'true' }, '⠿');
  grip.addEventListener('mousedown', () => { node.draggable = true; });
  node.addEventListener('dragstart', (e) => { drag = { list, i }; e.dataTransfer.effectAllowed = 'move'; e.stopPropagation(); });
  node.addEventListener('dragend', () => { node.draggable = false; drag = null; });
  node.addEventListener('dragover', (e) => {
    if (!drag || (drag.list === model.blocks) !== (list === model.blocks)) return;
    e.preventDefault(); e.stopPropagation(); node.classList.add('dragover');
  });
  node.addEventListener('dragleave', () => node.classList.remove('dragover'));
  node.addEventListener('drop', (e) => {
    node.classList.remove('dragover');
    if (!drag || (drag.list === model.blocks) !== (list === model.blocks)) return;
    e.preventDefault(); e.stopPropagation();
    if (drag.list === list && drag.i === i) { drag = null; return; }
    const target = list[i];
    const [b] = drag.list.splice(drag.i, 1);
    let at = list.indexOf(target);
    if (drag.list === list && drag.i < i) at += 1;  // moved down: it goes after the block it was dropped on
    list.splice(at, 0, b);
    drag = null;
    commit(true);
  });
  return grip;
}

function lineBlock(list, i) {
  const b = list[i];
  const k = KINDS[b.type];
  const node = el('div', { class: 'blk', 'data-key': keyOf(list, i), 'data-type': b.type, title: k.tip });
  node.style.background = k.colour;
  node.style.color = ink(k.colour);
  node.append(draggable(node, list, i));
  for (const w of k.words) node.append(w in k.fields ? field(b, w) : el('span', { class: 'w', text: w }));
  node.append(controls(list, i, b));
  return node;
}
function effectBlock(i) {
  const b = model.blocks[i];
  const k = KINDS.ps_effect;
  const node = el('div', { class: 'blk effect', 'data-key': keyOf(model.blocks, i), 'data-type': 'ps_effect', title: k.tip });
  node.style.background = k.colour;
  node.style.color = ink(k.colour);
  const head = el('div', { class: 'ehead' }, draggable(node, model.blocks, i), el('span', { class: 'w', text: 'effect' }), field(b, 'NAME'), el('span', { class: 'w', text: ':' }), controls(model.blocks, i, b));
  const lines = el('div', { class: 'lines' }, ...b.lines.map((_, j) => lineBlock(b.lines, j)));
  const sel = el('select', { 'aria-label': `A line to add to effect "${b.f.NAME}"` }, ...ADDABLE.map(([t, words]) => el('option', { value: t, text: words })));
  const add = el('div', { class: 'add' }, sel, el('button', { type: 'button', text: 'Add the line', onclick: () => {
    b.lines.push(newBlock(sel.value));
    commit(true);
    document.querySelector(`#script-blocks [data-key="${keyOf(b.lines, b.lines.length - 1)}"] input, #script-blocks [data-key="${keyOf(b.lines, b.lines.length - 1)}"] button`)?.focus();
  } }));
  // a line dragged onto the effect's own area goes at its end
  add.addEventListener('dragover', (e) => { if (drag && drag.list !== model.blocks) e.preventDefault(); });
  add.addEventListener('drop', (e) => {
    if (!drag || drag.list === model.blocks) return;
    e.preventDefault();
    const [x] = drag.list.splice(drag.i, 1);
    b.lines.push(x);
    drag = null;
    commit(true);
  });
  node.append(head, lines, add);
  return node;
}

function render() {
  const box = $('script-blocks');
  // a redraw keeps the focus on the same field (typing in the text box never moves it)
  const items = model.blocks.map((b, i) => (b.type === 'ps_effect' ? effectBlock(i) : lineBlock(model.blocks, i)));
  const addEffect = el('button', { type: 'button', text: '+ Add an effect', onclick: () => {
    const e = newBlock('ps_effect', { NAME: `effect ${model.blocks.filter((x) => x.type === 'ps_effect').length + 1}` });
    e.lines.push(newBlock('ps_wait'));
    model.blocks.push(e);
    commit(true);
    document.querySelector(`#script-blocks [data-key="${keyOf(model.blocks, model.blocks.length - 1)}"] input`)?.focus();
  } });
  box.replaceChildren(...(items.length ? items : [el('p', { class: 'hint', text: 'No blocks yet: add an effect, or record one on the Record effect tab.' })]), addEffect);
}

// ------------------------------------------------------------------ the block for a line (a tapped checker message)

// Marks and scrolls to the block that holds 1-based line n of the text (a blank line belongs to the block it rides
// with); false when the blocks are hidden or no block holds it.
window.blockForLine = (n) => {
  if (view() === 'text') return false;
  model = textToBlocks(ta.value);
  render();
  let at = 0, hit = null;
  const visit = (list, i) => {
    const d = list[i].d || { before: [], after: [] };
    const first = at + 1;
    at += d.before.length + 1 + d.after.length;
    if (!hit && n >= first && n <= at) hit = keyOf(list, i);
  };
  model.blocks.forEach((b, i) => { visit(model.blocks, i); (b.lines || []).forEach((_, j) => visit(b.lines, j)); });
  const node = hit != null && document.querySelector(`#script-blocks [data-key="${hit}"]`);
  if (!node) return false;
  for (const x of document.querySelectorAll('#script-blocks .goto')) x.classList.remove('goto');
  node.classList.add('goto', 'flash');
  node.scrollIntoView({ block: 'nearest' });
  setTimeout(() => node.classList.remove('flash'), 2000);
  return true;
};

// ------------------------------------------------------------------ open an effect (from Record effect, Keep)

document.addEventListener('open-effect', (e) => {
  document.querySelector('#tabs button[data-tab="script"]').click();
  model = textToBlocks(ta.value);
  if (view() !== 'text') render();
  const name = String(e.detail?.name || '').toLowerCase().replace(/\s+/g, ' ').trim();
  const i = model.blocks.findIndex((b) => b.type === 'ps_effect' && b.f.NAME.toLowerCase().replace(/\s+/g, ' ').trim() === name);
  if (i < 0) return;
  if (view() !== 'blocks') {
    // the effect's header line in the text box
    const lines = ta.value.split(/\r\n|\n|\r/);
    const n = lines.findIndex((l) => /^\s*effect\b/i.test(l) && l.replace(/^\s*effect\s*/i, '').replace(/:.*$/, '').toLowerCase().replace(/\s+/g, ' ').trim() === name);
    if (n >= 0 && view() === 'text') goToLine(n + 1);
  }
  if (view() !== 'text') {
    const node = document.querySelector(`#script-blocks [data-key="${keyOf(model.blocks, i)}"]`);
    node?.scrollIntoView({ block: 'start' });
    node?.classList.add('flash');
    setTimeout(() => node?.classList.remove('flash'), 2000);
  }
});
