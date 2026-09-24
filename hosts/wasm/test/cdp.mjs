// SPDX-License-Identifier: MIT
// Just enough of the Chrome DevTools Protocol for the demo test: launch headless Chrome, open a
// page, evaluate, emulate a screen, take a screenshot. Node's built-in WebSocket; no packages.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const MAC_CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';

export function chromePath() {
  const found = [process.env.CHROME_PATH, MAC_CHROME, '/usr/bin/google-chrome', '/usr/bin/chromium'].find((p) => p && fs.existsSync(p));
  if (!found) throw new Error('Chrome not found: set CHROME_PATH');
  return found;
}

/** Headless Chrome whose microphone is `wav` (looped), permission granted without a prompt. */
export async function launch(wav) {
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'squatch-tuner-chrome-'));
  const args = ['--headless=new', '--remote-debugging-port=0', `--user-data-dir=${profile}`,
    '--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream', `--use-file-for-fake-audio-capture=${wav}`,
    '--autoplay-policy=no-user-gesture-required', '--no-first-run', '--no-default-browser-check',
    '--disable-background-timer-throttling', '--disable-renderer-backgrounding', '--mute-audio',
    // Headless Chrome on macOS captures silence from the fake device while the audio service runs
    // in its own sandboxed process; in-process, the file plays.
    '--disable-features=AudioServiceOutOfProcess,AudioServiceSandbox', 'about:blank'];
  const proc = spawn(chromePath(), args, { stdio: ['ignore', 'ignore', 'pipe'] });
  const wsUrl = await new Promise((resolve, reject) => {
    let err = '';
    proc.stderr.on('data', (d) => {
      err += d;
      const m = /DevTools listening on (ws:\/\/\S+)/.exec(err);
      if (m) resolve(m[1]);
    });
    proc.on('exit', (code) => reject(new Error(`Chrome exited (${code}): ${err.slice(-400)}`)));
  });
  const browser = await connect(wsUrl);
  const exited = new Promise((resolve) => proc.on('exit', resolve));
  browser.close = async () => {
    proc.kill();
    await exited;
    fs.rmSync(profile, { recursive: true, force: true, maxRetries: 5 });
  };
  return browser;
}

async function connect(url) {
  const ws = new WebSocket(url);
  await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject; });
  let id = 0;
  const pending = new Map();
  ws.onmessage = (m) => {
    const msg = JSON.parse(m.data);
    const p = pending.get(msg.id);
    if (!p) return;
    pending.delete(msg.id);
    if (msg.error) p.reject(new Error(`${p.method}: ${msg.error.message}`));
    else p.resolve(msg.result);
  };
  const send = (method, params = {}, sessionId) => new Promise((resolve, reject) => {
    const msg = { id: ++id, method, params, ...(sessionId ? { sessionId } : {}) };
    pending.set(msg.id, { resolve, reject, method });
    ws.send(JSON.stringify(msg));
  });
  return { send };
}

/** A new tab; returns helpers bound to its session. */
export async function openPage(browser, url) {
  const { targetId } = await browser.send('Target.createTarget', { url: 'about:blank' });
  const { sessionId } = await browser.send('Target.attachToTarget', { targetId, flatten: true });
  const send = (method, params) => browser.send(method, params, sessionId);
  await send('Page.enable');
  await send('Runtime.enable');
  const page = {
    send,
    async eval(expression) {
      const r = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true, userGesture: true });
      if (r.exceptionDetails) throw new Error(`page: ${r.exceptionDetails.exception?.description ?? r.exceptionDetails.text}`);
      return r.result.value;
    },
    async screen({ width, height, scale, mobile, scheme }) {
      await send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: scale, mobile });
      await send('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-color-scheme', value: scheme }] });
    },
    async shot(file) {
      const { data } = await send('Page.captureScreenshot', { format: 'jpeg', quality: 82, captureBeyondViewport: false });
      fs.writeFileSync(file, Buffer.from(data, 'base64'));
    },
  };
  await send('Page.navigate', { url });
  await waitFor(() => page.eval('document.readyState === "complete"'), 10000, 'page load');
  return page;
}

export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/** Poll `check` until it returns something truthy, or throw after `ms`. */
export async function waitFor(check, ms, what) {
  const until = Date.now() + ms;
  for (;;) {
    const v = await check().catch(() => null);
    if (v) return v;
    if (Date.now() > until) throw new Error(`timed out waiting for ${what}`);
    await sleep(100);
  }
}
