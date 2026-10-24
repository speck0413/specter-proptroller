// 2D inset cards: one flat, big drawing where the 3D scene is too small to read (the screw adapter's three screws, a
// diode's stripe, a meter reading, what the phone page should show). Each returns an HTML string so the interactive
// page and the Remotion video draw the same card. Step data says `inset: { type: 'trs', ... }`.
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const WIRE = { red: '#d62828', black: '#1c1c1c', yellow: '#e0b000', blue: '#2b6cd6', white: '#f0f0f0', grey: '#8a8f94', green: '#2a9d3a', orange: '#f08a24' };
const card = (title, svg, caption, cls = '') => `<figure class="wg-inset ${cls}"><figcaption class="wg-inset-t">${esc(title)}</figcaption>${svg}${caption ? `<div class="wg-inset-c">${caption}</div>` : ''}</figure>`;

// A 3.5 mm plug with three screws, big: which screw is which, and what goes on it.
function trs({ wires = {}, title = 'The plug with screws' }) {
  const rows = [['T', 'L', 'tip'], ['R', 'R', 'ring'], ['S', 'G or −', 'sleeve']];
  const y = [46, 96, 146];
  let s = `<svg viewBox="0 0 372 190" role="img" aria-label="3.5 mm plug: T is the tip, R the ring, S the sleeve">
  <rect x="8" y="88" width="34" height="16" rx="3" fill="#c9ced3"/><rect x="42" y="88" width="16" height="16" fill="#e2b13c"/>
  <rect x="58" y="88" width="16" height="16" fill="#c9ced3"/><rect x="74" y="88" width="16" height="16" fill="#e2b13c"/>
  <rect x="90" y="88" width="18" height="16" fill="#c9ced3"/><rect x="108" y="70" width="60" height="52" rx="6" fill="#2b2f35"/>
  <text x="25" y="80" class="s" text-anchor="middle">sleeve</text><text x="66" y="128" class="s" text-anchor="middle">ring</text><text x="99" y="80" class="s" text-anchor="middle">tip</text>`;
  rows.forEach(([k, alt, name], i) => {
    s += `<circle cx="196" cy="${y[i]}" r="15" fill="#d9dde1" stroke="#555"/><line x1="186" y1="${y[i]}" x2="206" y2="${y[i]}" stroke="#555" stroke-width="3"/>
    <text x="220" y="${y[i] - 2}" class="b">${k}</text><text x="220" y="${y[i] + 16}" class="s">${alt === k ? '' : 'or ' + esc(alt) + ' · '}${name}</text>`;
    const w = wires[k];
    if (w) s += `<line x1="290" y1="${y[i]}" x2="334" y2="${y[i]}" stroke="${WIRE[w.color] || w.color}" stroke-width="7" stroke-linecap="round"/><text x="300" y="${y[i] - 9}" class="s">${esc(w.label)}</text>`;
    else s += `<text x="300" y="${y[i] + 5}" class="s dim">empty</text>`;
    s += `<line x1="168" y1="96" x2="181" y2="${y[i]}" stroke="#777" stroke-width="2"/>`;
  });
  return card(title, s + '</svg>', 'Adapters are often marked <b>L</b>, <b>R</b>, <b>G</b> (or <b>−</b>) instead of T, R, S.');
}

// A diode across a coil, stripe to +.
function diode({ title = 'Diode: the stripe goes to +' }) {
  const s = `<svg viewBox="0 0 340 170" role="img" aria-label="Diode across the coil, its stripe on the plus terminal">
  <rect x="110" y="20" width="120" height="70" rx="10" fill="#555c66"/><text x="170" y="62" class="b" text-anchor="middle" style="fill:#fff">coil</text>
  <circle cx="135" cy="100" r="9" fill="#d62828"/><circle cx="205" cy="100" r="9" fill="#1c1c1c"/>
  <text x="135" y="130" class="b" text-anchor="middle">+</text><text x="205" y="130" class="b" text-anchor="middle">−</text>
  <line x1="135" y1="100" x2="135" y2="150" stroke="#999" stroke-width="3"/><line x1="205" y1="100" x2="205" y2="150" stroke="#999" stroke-width="3"/>
  <line x1="135" y1="150" x2="150" y2="150" stroke="#999" stroke-width="3"/><line x1="190" y1="150" x2="205" y2="150" stroke="#999" stroke-width="3"/>
  <rect x="150" y="140" width="40" height="20" rx="4" fill="#1c1c1c"/><rect x="152" y="140" width="8" height="20" fill="#d9dde1"/>
  <text x="30" y="160" class="s">stripe → +</text><text x="250" y="160" class="s">to OUT −</text></svg>`;
  return card(title, s, 'Stripe = cathode, on the + side (the wire to OUT1 +). Backwards it shorts the output, and the board has no fuse: check it before power-on.');
}

// A multimeter checking a barrel plug or terminal wires: +12 good, −12 swapped.
function meter({ title = 'Check + and − with a meter', what = 'plug' }) {
  const probe = what === 'plug'
    ? '<circle cx="250" cy="90" r="30" fill="#333"/><circle cx="250" cy="90" r="11" fill="#bbb"/><circle cx="250" cy="90" r="4" fill="#333"/><text x="250" y="150" class="s" text-anchor="middle">red inside, black outside</text>'
    : '<rect x="220" y="70" width="60" height="40" rx="5" fill="#2a2f35"/><text x="236" y="96" class="b" style="fill:#fff">+  −</text><text x="250" y="140" class="s" text-anchor="middle">red on +, black on −</text>';
  const s = `<svg viewBox="0 0 340 170" role="img" aria-label="Meter on DC volts: 12 means right way round, minus 12 means swapped">
  <rect x="14" y="10" width="120" height="150" rx="12" fill="#f2c200"/><rect x="26" y="22" width="96" height="40" rx="4" fill="#cfe0c8"/>
  <text x="74" y="52" class="b" text-anchor="middle" style="font-size:24px">12.0</text><circle cx="74" cy="104" r="26" fill="#333"/>
  <text x="74" y="110" class="s" text-anchor="middle" style="fill:#fff">DC V</text><circle cx="50" cy="146" r="6" fill="#1c1c1c"/><circle cx="98" cy="146" r="6" fill="#d62828"/>
  <path d="M98 146 C 160 160, 190 120, ${what === 'plug' ? 246 : 228} 90" stroke="#d62828" stroke-width="4" fill="none"/>
  <path d="M50 146 C 120 175, 220 150, ${what === 'plug' ? 268 : 268} ${what === 'plug' ? 110 : 90}" stroke="#1c1c1c" stroke-width="4" fill="none"/>${probe}</svg>`;
  return card(title, s, '<b>12</b> (or 12.0): right way round. <b>−12</b>: swapped. Do not connect until it reads plus.');
}

// What the board's phone page should show.
function phone({ tab = 'Live', cardTitle = 'Inputs', body = '', note = '' }) {
  const tabsRow = ['Live', 'Wiring', 'Shows', 'Script', 'Settings'].map((t) => `<span class="${t === tab ? 'on' : ''}">${t}</span>`).join('');
  const html = `<div class="wg-phone" role="img" aria-label="${esc(`Phone page, ${tab} tab, ${cardTitle}`)}"><div class="wg-ph-top"><b>Proptroller</b><span class="wg-ph-stop">STOP</span></div>
  <div class="wg-ph-tabs">${tabsRow}</div><div class="wg-ph-card"><div class="wg-ph-h">${esc(cardTitle)}</div>${body}</div></div>`;
  return card('On your phone', html, note);
}
export const chips = (names, lit) => `<div class="wg-ph-chips">${names.map((n) => `<span class="${n === lit ? 'lit' : ''}">${n}</span>`).join('')}</div>`;
export const outRow = (name, press) => `<div class="wg-ph-out"><b>${name}</b>${['On', 'Off', 'Auto'].map((b) => `<span class="${b === press ? 'pressed' : ''}">${b}</span>`).join('')}</div>`;
export const setting = (label, value, extra = '') => `<div class="wg-ph-set"><label>${esc(label)}</label><span class="wg-ph-sel">${esc(value)} ▾</span></div>${extra}<div class="wg-ph-save">Save settings</div>`;

// The SD card's mp3 folder, as a tree.
function folders({ title = 'On the card' }) {
  const s = `<div class="wg-tree"><div>📁 mp3</div><div class="in">🎵 0001-thunder.mp3 <i>sound 1</i></div><div class="in">🎵 0002-scream.mp3 <i>sound 2</i></div></div>`;
  return card(title, s, 'Copied in this order, one at a time (Studio\'s SD card tool does it). FAT32, 32 GB or smaller.');
}

// The bulk capacitor at the strip's input, on a small screw block (no soldering).
function cap({ title = 'Capacitor at the strip input' }) {
  const s = `<svg viewBox="0 0 340 170" role="img" aria-label="Capacitor legs under the 5V and GND screws of the screw block at the strip input; stripe to GND">
  <rect x="20" y="40" width="130" height="60" rx="6" fill="#2a6f3a"/><text x="85" y="78" class="s" text-anchor="middle" style="fill:#fff">screw block</text>
  <g fill="#d9dde1" stroke="#555"><circle cx="45" cy="30" r="10"/><circle cx="85" cy="30" r="10"/><circle cx="125" cy="30" r="10"/></g>
  <text x="45" y="124" class="b" text-anchor="middle">5V</text><text x="85" y="124" class="b" text-anchor="middle">DIN</text><text x="125" y="124" class="b" text-anchor="middle">GND</text>
  <rect x="200" y="30" width="54" height="80" rx="8" fill="#1d3f8a"/><rect x="238" y="30" width="12" height="80" fill="#c9d3e8"/>
  <text x="244" y="76" class="b" text-anchor="middle" style="font-size:16px">−</text>
  <path d="M215 110 C 215 150, 45 150, 45 108" stroke="#aaa" stroke-width="3" fill="none"/><path d="M244 110 C 244 160, 125 160, 125 108" stroke="#aaa" stroke-width="3" fill="none"/>
  <text x="270" y="60" class="s">stripe (−)</text><text x="270" y="78" class="s">to GND</text></svg>`;
  return card(title, s, '470-1000 µF, 6.3 V or more. Long leg (+) to 5V, the striped side (−) to GND.');
}

// A 3-pin XLR socket and the DMX pins.
function xlr({ title = 'DMX IN, never REMOTE' }) {
  const s = `<svg viewBox="0 0 340 170" role="img" aria-label="XLR pin 1 ground, pin 2 data minus (B), pin 3 data plus (A)">
  <circle cx="90" cy="85" r="62" fill="#2b2f35"/><circle cx="90" cy="85" r="48" fill="#444b54"/>
  <g fill="#e2b13c"><circle cx="62" cy="70" r="8"/><circle cx="118" cy="70" r="8"/><circle cx="90" cy="112" r="8"/></g>
  <text x="62" y="58" class="s" text-anchor="middle" style="fill:#fff">2</text><text x="118" y="58" class="s" text-anchor="middle" style="fill:#fff">1</text><text x="90" y="140" class="s" text-anchor="middle" style="fill:#fff">3</text>
  <text x="175" y="50" class="s">1 = GND (shield)</text><text x="175" y="80" class="s">2 = Data − (module B)</text><text x="175" y="110" class="s">3 = Data + (module A)</text>
  <text x="175" y="146" class="b" style="font-size:15px">socket says DMX IN</text></svg>`;
  return card(title, s, 'Pin numbers are printed tiny on the plug. A socket labelled REMOTE can look the same: never use it, it may carry mains.');
}

// Joining the phone to the board's own Wi-Fi hotspot.
function wifi({ title = 'Join the board’s Wi-Fi' }) {
  const html = `<div class="wg-phone"><div class="wg-ph-top"><b>Wi-Fi</b></div><div class="wg-ph-card">
  <div class="wg-ph-net on">📶 Proptroller-XXXX <i>(the last four differ)</i></div><div class="wg-ph-net">📶 your home Wi-Fi</div>
  <div class="wg-ph-set"><label>Password</label><span class="wg-ph-sel">password</span></div></div>
  <div class="wg-ph-top" style="margin-top:8px"><b>Browser</b></div><div class="wg-ph-url">192.168.4.1</div></div>`;
  return card(title, html, 'No page? Turn mobile data off for a moment, and type the address in full: <b>http://192.168.4.1</b>');
}

const KINDS = { trs, diode, meter, phone, folders, cap, xlr, wifi };
export function insetHTML(inset) {
  if (!inset) return '';
  const o = typeof inset === 'string' ? { type: inset } : inset;
  return KINDS[o.type] ? KINDS[o.type](o) : '';
}
