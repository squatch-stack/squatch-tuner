// SPDX-License-Identifier: MIT
// The browser demo end to end: headless Chrome with a test tone as its microphone, the page's
// Start button pressed, and the cents THE PAGE SHOWS read back and scored against the truth.
//
//   node hosts/wasm/test/browser.test.mjs [--dir build/web-demo] [--url URL] [--shots DIR] [--only a2,...]
//
// --dir serves the built demo under /squatch-tuner/ on 127.0.0.1 (a secure context), as GitHub
// Pages would serve it; --url tests a page that is already up (the files host, say) instead.
// --shots writes screenshots, desktop and phone, light and dark, of A2 in tune and A2 +7 c.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { parseArgs } from 'node:util';
import { launch, openPage, sleep, waitFor } from './cdp.mjs';
import { serve } from './serve.mjs';
import { TONES, writeWav } from './tones.mjs';

const TOL = 0.1;            // cents, on the number the page shows (it shows tenths)
const SETTLE_MS = 2500;     // after the first valid reading
const SAMPLES = 40, EVERY_MS = 100;
const SCREENS = {
  desktop: { width: 1280, height: 900, scale: 2, mobile: false },
  phone: { width: 390, height: 844, scale: 2, mobile: true },
};

const READ = `(() => {
  const $ = (id) => document.getElementById(id), r = window.squatchTuner.reading();
  return { shown: $('cents-num').textContent, note: $('note-name').textContent + $('note-acc').textContent,
    oct: $('note-oct').textContent, view: $('stage').dataset.view, raw: r && r.valid ? r.cents : null,
    s05: $('seal-05').classList.contains('on'), s01: $('seal-01').classList.contains('on') };
})()`;

async function start(page) {
  await waitFor(() => page.eval('Boolean(window.squatchTuner)'), 5000, 'demo.js');
  await page.eval(`document.getElementById('start').click()`);
  await waitFor(() => page.eval(`(() => { const r = window.squatchTuner.reading(); return r && r.valid; })()`), 10000, 'a valid reading');
  await sleep(SETTLE_MS);
}

async function sample(page) {
  const out = [];
  for (let i = 0; i < SAMPLES; i++) {
    out.push(await page.eval(READ));
    await sleep(EVERY_MS);
  }
  return out;
}

function expectedSeals(cents) {
  const abs = Math.abs(cents);
  return { s05: abs <= 0.5, s01: abs <= 0.1 };
}

function judge(tone, samples) {
  const seals = expectedSeals(tone.cents);
  const shown = samples.map((s) => Number(s.shown.replace('−', '-')));
  const raw = samples.map((s) => s.raw).filter((v) => v != null);
  const bad = samples.filter((s, i) => s.view !== 'note' || s.note !== tone.note || s.oct !== String(tone.oct) ||
    !(Math.abs(shown[i] - tone.cents) <= TOL + 1e-9) || s.s05 !== seals.s05 || s.s01 !== seals.s01);
  return { ok: bad.length === 0 && raw.length === samples.length, bad, shown, raw };
}

function report(tone, j) {
  const uniq = [...new Set(j.shown.map((v) => (v > 0 ? '+' : '') + v.toFixed(1)))].join(' ');
  const raw = j.raw.length ? `${Math.min(...j.raw).toFixed(3)} … ${Math.max(...j.raw).toFixed(3)}` : 'none';
  const sign = tone.cents > 0 ? '+' : '';
  console.log(`${j.ok ? 'ok  ' : 'FAIL'} ${tone.label.padEnd(11)} truth ${sign}${tone.cents.toFixed(3)} c  shown ${uniq} ` +
    `(${j.shown.length - j.bad.length}/${j.shown.length} within ±${TOL})  reading ${raw}`);
  if (!j.ok) console.log('     first bad sample:', JSON.stringify(j.bad[0]));
}

// Chrome's fake microphone is called "Fake Default Audio Input"; the screenshots show what a
// player's picker would, a neutral interface name. Only the option's text changes.
const NEUTRAL_LABEL = `document.querySelectorAll('#input-select option').forEach((o) => { o.text = 'Audio interface input 1'; })`;

async function shots(page, tone, dir) {
  const tag = tone.id === 'a2' ? 'a2-in-tune' : tone.id;
  await page.eval(NEUTRAL_LABEL);
  for (const [screen, metrics] of Object.entries(SCREENS)) {
    for (const scheme of ['light', 'dark']) {
      await page.screen({ ...metrics, scheme });
      await sleep(900);
      const file = path.join(dir, `demo-${tag}-${screen}-${scheme}.jpg`);
      await page.shot(file);
      console.log(`     screenshot ${file}`);
    }
  }
}

async function idleShot(url, wav, dir) {
  if (!fs.existsSync(wav)) writeWav(wav, TONES[0].hz, 30);
  const browser = await launch(wav);
  try {
    const page = await openPage(browser, url);
    await page.screen({ ...SCREENS.desktop, scheme: 'light' });
    await sleep(1500);
    const file = path.join(dir, 'demo-idle-desktop-light.jpg');
    await page.shot(file);
    console.log(`     screenshot ${file}`);
  } finally {
    await browser.close();
  }
}

async function runTone(tone, url, tmp, shotDir) {
  const wav = path.join(tmp, `${tone.id}.wav`);
  writeWav(wav, tone.hz, 30);
  const browser = await launch(wav);
  try {
    const page = await openPage(browser, url);
    await page.screen({ ...SCREENS.desktop, scheme: 'light' });
    await start(page);
    const j = judge(tone, await sample(page));
    report(tone, j);
    if (shotDir && (tone.id === 'a2' || tone.id === 'a2-plus7')) await shots(page, tone, shotDir);
    return j.ok;
  } finally {
    await browser.close();
  }
}

function options() {
  const { values } = parseArgs({ options: { dir: { type: 'string', default: 'build/web-demo' }, url: { type: 'string' },
    shots: { type: 'string' }, only: { type: 'string' } } });
  const only = values.only ? new Set(values.only.split(',')) : null;
  return { ...values, tones: TONES.filter((t) => !only || only.has(t.id)) };
}

async function runAll(o, url, tmp) {
  let failed = 0;
  for (const tone of o.tones) {
    if (!(await runTone(tone, url, tmp, o.shots))) failed++;
  }
  if (o.shots) await idleShot(url, path.join(tmp, `${TONES[0].id}.wav`), o.shots);
  return failed;
}

async function main() {
  const o = options();
  const server = o.url ? null : await serve(o.dir);
  const url = o.url ?? server.url;
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'squatch-tuner-tones-'));
  if (o.shots) fs.mkdirSync(o.shots, { recursive: true });
  console.log(`browser.test: ${url}`);
  let failed;
  try {
    failed = await runAll(o, url, tmp);
  } finally {
    server?.close();
    fs.rmSync(tmp, { recursive: true, force: true });
  }
  console.log(failed ? `browser.test: ${failed} FAILED` : `browser.test: every tone shown within ±${TOL} c`);
  process.exit(failed ? 1 : 0);
}

main().catch((e) => {
  console.error(`browser.test: ${e.message}`);
  process.exit(2);
});
