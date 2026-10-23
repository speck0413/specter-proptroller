// Shows tab at phone width (390 px): add an example, link outputs to an input (on while held, on for a time),
// a new show, replace an existing show (Save as), delete with the in-page question (never window.confirm).
import { chromium } from 'playwright-core';
const S = process.argv[2] || '.', URL = process.argv[3] || 'http://127.0.0.1:18080/';
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM || 'google-chrome', args: ['--no-sandbox'] });
const page = await browser.newPage({ viewport: { width: 390, height: 900 }, deviceScaleFactor: 1, colorScheme: 'dark' });
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
// a window.confirm / alert would block a phone user's flow: the page must ask in the page
page.on('dialog', async (d) => { errors.push('dialog: ' + d.message()); await d.dismiss(); });
const ok = (c, msg) => { if (!c) { console.log('FAIL: ' + msg); process.exitCode = 1; } else console.log('ok: ' + msg); };
const rpc = (req) => page.evaluate(async (r) => (await (await fetch('/api/v1/rpc', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ v: 1, ...r }) })).json()), req);
const names = () => page.$$eval('#show-list .name', (n) => n.map((x) => x.textContent));
const level = (ch) => page.evaluate(async (c) => (await (await fetch('/api/v1/state')).json()).outputs.find((o) => o.channel === c).level, ch);
const noHScroll = async (what) => ok(await page.evaluate(() => document.documentElement.scrollWidth <= 390), what + ': no sideways scrolling at 390 px');

await page.goto(URL);
await page.waitForSelector('#conn.ok', { timeout: 5000 });
await page.click('#tabs button[data-tab="shows"]');
await page.waitForFunction(() => document.querySelector('#show-list').textContent.startsWith('No shows yet'), null, { timeout: 3000 });
ok(true, 'a fresh board lists no shows');

// add an example
await page.click('#add-example');
await page.waitForFunction(() => document.querySelectorAll('#show-list .item').length === 1, null, { timeout: 3000 });
ok((await names())[0] === 'Example show: door knock', 'Add an example show: ' + (await names()).join(', '));

// link: TRIG2 -> OUT2 + RLY1, on while held (at most 5 s)
await page.click('#link-open');
await page.waitForSelector('#link:not(.hidden)');
await page.selectOption('#link-input', 'TRIG2');
await page.click('#link-outputs .chip[data-ch="OUT2"]');
await page.click('#link-outputs .chip[data-ch="RLY1"]');
ok((await page.inputValue('#link-secs')) === '60', 'on while held: at most 60 s by default');
await page.fill('#link-secs', '5');
await noHScroll('Link card');
await page.screenshot({ path: S + '/shows-link.png', fullPage: true });
await page.click('#link-save');
await page.waitForFunction(() => document.querySelector('#link-msg').classList.contains('ok'), null, { timeout: 3000 });
ok(true, 'link saved: ' + (await page.textContent('#link-msg')));
let r = await rpc({ op: 'get_show', show: 2 });
const t = r.result.data;
ok(t.triggers[0].input === 'TRIG2' && t.triggers[0].until_release === true && t.length_ms === 5000 && t.tracks.map((x) => x.target).join() === 'OUT2,RLY1',
  'the link is a normal show with an until_release trigger: ' + JSON.stringify(t.triggers) + ' ' + t.length_ms);
// hold TRIG2 on the simulator: on while held, off at the release
await rpc({ op: 'sim_input', input: 'TRIG2', active: true });
await page.waitForTimeout(500);
ok((await level('OUT2')) === 1000 && (await level('RLY1')) === 1000, 'TRIG2 held: OUT2 and RLY1 on');
await rpc({ op: 'sim_input', input: 'TRIG2', active: false });
await page.waitForTimeout(300);
ok((await level('OUT2')) === 0 && (await level('RLY1')) === 0, 'TRIG2 let go: both off');

// link: USER1 -> OUT4 for 1.5 s
await page.click('#link-open');
await page.selectOption('#link-input', 'USER1');
await page.click('#link-outputs .chip[data-ch="OUT2"]');  // picked last time: unpick
await page.click('#link-outputs .chip[data-ch="RLY1"]');
await page.click('#link-outputs .chip[data-ch="OUT4"]');
await page.check('#link-mode input[value="timed"]');
ok((await page.inputValue('#link-secs')) === '2', 'on for a time: 2 s by default');
await page.fill('#link-secs', '1.5');
await page.click('#link-save');
await page.waitForFunction(() => document.querySelector('#link-msg').textContent.includes('#3'), null, { timeout: 3000 });
r = await rpc({ op: 'get_show', show: 3 });
ok(!r.result.data.triggers[0].until_release && r.result.data.tracks[0].cues[0].do === 'pulse' && r.result.data.tracks[0].cues[0].ms === 1500, 'timed link: a 1.5 s pulse');
await page.click('#link-cancel');

// + New, saved as a new show
await page.click('#new-show');
await page.waitForSelector('#editor:not(.hidden)');
ok((await page.inputValue('#ed-save-to')) === 'new', 'a new show saves as a new show');
await page.fill('#ed-name', 'Lamp');
await page.click('#ed-save');
await page.waitForFunction(() => document.querySelector('#ed-msg').textContent.startsWith('saved as show #4'), null, { timeout: 3000 });
ok((await names()).includes('Lamp'), 'new show saved: ' + (await names()).join(', '));
// plain words in the editor: triggers and actions, levels in %
ok((await page.$$eval('#ed-triggers select >> nth=0', (x) => x[0].selectedOptions[0].textContent)) === 'an input is pressed', 'trigger reads "an input is pressed" (not "input")');
await page.selectOption('.track .cue select >> nth=0', 'level');
const lv = await page.$eval('.track .cue .params label', (l) => [l.firstChild.textContent, l.querySelector('input').value]);
ok(lv[0] === 'level (%)' && lv[1] === '50', 'a level in %: ' + lv.join(' '));
ok(JSON.parse(await page.inputValue('#ed-json')).tracks[0].cues[0].level === 500, 'stored as 500 (permille)');
// a JSON that is not a show is refused in plain words, nothing breaks
await page.click('#editor details summary');
await page.fill('#ed-json', '{"id":3}');
await page.click('#ed-json-apply');
ok(/a show needs "name"/.test(await page.textContent('#ed-msg')), 'JSON {"id":3}: ' + await page.textContent('#ed-msg'));
await page.click('#editor details summary');
// the "until released" box on an input trigger
await page.check('#ed-triggers .until-release');
ok(JSON.parse(await page.inputValue('#ed-json')).triggers[0].until_release === true, 'editor: until released sets until_release');
await page.uncheck('#ed-triggers .until-release');

// replace: save this show over the timed link (#3), asked in the page
await page.selectOption('#ed-save-to', '3');
ok((await page.textContent('#ed-save')).startsWith('Replace'), 'Save as an existing show: the button says Replace');
await page.click('#ed-save');
await page.waitForSelector('#editor .ask .ask-yes');
await noHScroll('Replace question');
await page.screenshot({ path: S + '/shows-replace-ask.png', fullPage: true });
await page.click('#editor .ask .ask-yes');
await page.waitForFunction(() => document.querySelector('#ed-msg').textContent.startsWith('saved over show #3'), null, { timeout: 3000 });
r = await rpc({ op: 'get_show', show: 3 });
ok(r.result.data.name === 'Lamp', 'show #3 replaced by the edited show');
// the list has followed the save (a redraw would drop a question asked in it)
await page.waitForFunction(() => document.querySelector('#show-list .item[data-id="3"] .name')?.textContent === 'Lamp', null, { timeout: 3000 });

// delete from the list: Cancel keeps it, Delete removes it
const before = (await names()).length;
await page.click('#show-list .item[data-id="4"] button:has-text("Delete")');
await page.waitForSelector('#show-list .ask');
await page.screenshot({ path: S + '/shows-delete-ask.png', fullPage: true });
await page.click('#show-list .ask .ask-no');
ok((await names()).length === before, 'Cancel keeps the show');
await page.click('#show-list .item[data-id="4"] button:has-text("Delete")');
await page.click('#show-list .ask .ask-yes');
await page.waitForFunction((n) => document.querySelectorAll('#show-list .item').length === n - 1, before, { timeout: 3000 });
ok((await rpc({ op: 'get_show', show: 4 })).ok === false, 'deleted from the list (asked in the page)');
// delete from the editor
await page.click('#show-list .item[data-id="3"] button:has-text("Edit")');
await page.waitForSelector('#editor:not(.hidden)');
await page.click('#ed-delete');
await page.click('#editor .ask .ask-yes');
await page.waitForFunction(() => document.querySelector('#editor').classList.contains('hidden'), null, { timeout: 3000 });
ok((await rpc({ op: 'get_show', show: 3 })).ok === false, 'deleted from the editor');
await noHScroll('Shows tab');
await page.screenshot({ path: S + '/shows-list.png', fullPage: true });

// two phones at once (S5): new shows get different numbers from the board; saving over a show another phone
// changed meanwhile asks first; Close with unsaved edits asks
const ctx2 = await browser.newContext({ viewport: { width: 390, height: 900 } });
const p2 = await ctx2.newPage();
p2.on('pageerror', (e) => errors.push('phone 2: ' + String(e)));
await p2.goto(URL);
await p2.waitForSelector('#conn.ok', { timeout: 5000 });
for (const p of [page, p2]) { await p.click('#tabs button[data-tab="shows"]'); await p.click('#new-show'); }
await page.fill('#ed-name', 'From phone 1');
await p2.fill('#ed-name', 'From phone 2');
ok((await page.textContent('#ed-title')).includes('not saved yet'), 'a new show has no number before it is saved');
await Promise.all([page.click('#ed-save'), p2.click('#ed-save')]);
for (const p of [page, p2]) await p.waitForFunction(() => document.querySelector('#ed-msg').textContent.startsWith('saved as show #'), null, { timeout: 3000 });
const list = (await rpc({ op: 'list_shows' })).result.shows;
ok(list.some((x) => x.name === 'From phone 1') && list.some((x) => x.name === 'From phone 2'), 'both new shows kept (the board gave them different numbers): ' + list.map((x) => `#${x.id} ${x.name}`).join(', '));
// phone 2 renames phone 1's show while phone 1 has it open
const id1 = list.find((x) => x.name === 'From phone 1').id;
await p2.click(`#show-list .item[data-id="${id1}"] button:has-text("Edit")`);
await p2.waitForFunction((id) => document.querySelector('#ed-title').textContent === `From phone 1 (#${id})`, id1, { timeout: 3000 });
await p2.fill('#ed-name', 'Renamed on phone 2');
await p2.click('#ed-save');
await page.waitForFunction(async (id) => (await (await fetch('/api/v1/rpc', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ v: 1, op: 'get_show', show: id }) })).json()).result.data.name === 'Renamed on phone 2', id1, { timeout: 3000 });
await page.fill('#ed-name', 'Phone 1 edit');
await page.click('#ed-save');
await page.waitForFunction(() => /changed on another phone/.test(document.querySelector('#ed-msg').textContent), null, { timeout: 3000 });
ok((await rpc({ op: 'get_show', show: id1 })).result.data.name === 'Renamed on phone 2', 'the other phone\'s rename is not overwritten silently: ' + await page.textContent('#ed-msg'));
await page.screenshot({ path: S + '/shows-conflict.png', fullPage: false });
await page.click('#ed-msg button:has-text("Save mine over it")');
await page.waitForFunction(() => document.querySelector('#ed-msg').textContent.startsWith('saved as show #'), null, { timeout: 3000 });
ok((await rpc({ op: 'get_show', show: id1 })).result.data.name === 'Phone 1 edit', 'Save mine over it: saved');
// Close with unsaved edits asks in the page
await page.fill('#ed-name', 'not saved');
await page.click('#ed-cancel');
await page.waitForSelector('#editor .ask', { timeout: 2000 });
ok(/Close without saving/.test(await page.textContent('#editor .ask')), 'Close with unsaved edits asks first');
await page.click('#editor .ask .ask-yes');
ok(await page.isHidden('#editor'), 'closed after Close');
await ctx2.close();

ok(errors.length === 0, 'no page errors or dialogs ' + JSON.stringify(errors));
await browser.close();
