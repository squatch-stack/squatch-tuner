// SPDX-License-Identifier: MIT
// Squatch Tuner page on the Pi bench: readings from serve.py's stream (/events), drawn by
// tuner-view.js. This half adds what only the bench has: the reference pitch shared by every
// viewer (POST /a4), input-to-screen latency across the Pi's and this device's clocks, and the
// Pi's CPU and JACK figures in the footer.
import { $, setText, startView } from './tuner-view.js';

const clock = { offsetMs: null, latency: null, drawnSeq: 0 };

function connect(feed) {
  const es = new EventSource('events');
  es.onmessage = (e) => feed.push(JSON.parse(e.data));
  es.addEventListener('offline', () => feed.offline());
  es.onerror = () => feed.offline();  // EventSource reconnects by itself
}

// The Pi's clock and this device's clock differ. Estimate the offset from the round trip with
// the smallest delay (Cristian's method), so latency can be measured across the two.
async function syncClock() {
  let best = null;
  for (let i = 0; i < 6; i++) {
    try {
      const t0 = Date.now();
      const r = await fetch('clock', { cache: 'no-store' });
      const { t } = await r.json();
      const t1 = Date.now();
      if (!best || t1 - t0 < best.rtt) best = { rtt: t1 - t0, offset: t - (t0 + t1) / 2 };
    } catch { /* offline: try again later */ }
  }
  if (best) clock.offsetMs = best.offset;
}

// Input -> screen: the reading carries the wall-clock time its newest audio reached the input
// jack (Pi clock); this frame's time on the Pi's clock is ours plus the offset.
function measureLatency(ts, r, seq) {
  if (!r || clock.drawnSeq === seq || clock.offsetMs == null || !r.tin) return;
  clock.drawnSeq = seq;
  const ms = performance.timeOrigin + ts + clock.offsetMs - r.tin;
  if (ms < 0 || ms > 2000) return;
  clock.latency = clock.latency == null ? ms : clock.latency + 0.05 * (ms - clock.latency);
}

function renderFooter(r) {
  if (!r) return;
  const lat = clock.latency;
  setText('lat', lat == null ? 'latency –' : `latency ${Math.round(lat)} ms input→screen`);
  setText('cpu', `Pi CPU ${(r.cpu ?? 0).toFixed(1)}% tuner + ${(r.bridgeCpu ?? 0).toFixed(1)}% web`);
  setText('dsp', `DSP ${r.dsp.toFixed(1)}% of each ${((1000 * r.period) / r.rate).toFixed(2)} ms block`);
  setText('audio', `${(r.rate / 1000).toFixed(0)} kHz · ${r.period} frames · JACK ${r.jack.toFixed(0)}%`);
  setText('input-name', `Input ${r.input ?? 1}`);
}

function sendA4(hz) {
  const body = JSON.stringify({ a4: hz });
  fetch('a4', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body }).catch(() => {});
}

// The index lives at home.<domain> beside tuner.<domain>. Opened some other way (the Pi's own
// address), there is no index to go back to, so the link goes.
function wireHome() {
  const labels = location.hostname.split('.');
  if (labels.length >= 3 && !/^\d+$/.test(labels[labels.length - 1])) {
    $('home').href = `https://home.${labels.slice(1).join('.')}/`;
  } else {
    $('home').classList.add('gone');
  }
}

startView({
  status: { offline: 'tuner offline' },
  strobeMsg: { offline: 'tuner offline · start it with tuner-on' },
  start(feed) {
    wireHome();
    connect(feed);
    syncClock();
    setInterval(syncClock, 60000);
  },
  sendA4,
  renderFooter,
  onFrame: measureLatency,
});
