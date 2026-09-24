// SPDX-License-Identifier: MIT
// The test tones and their truth, and a 16-bit PCM WAV writer for Chrome's fake audio input.
// Each tone is an exact harmonic series (partials 1-5 at 1, 1/2, 1/3, 1/4, 1/5), so its
// fundamental is the truth to within float rounding.
import fs from 'node:fs';

const noteHz = (midi, a4 = 440) => a4 * 2 ** ((midi - 69) / 12);

export const TONES = [
  { id: 'a2', label: 'A2 exact', note: 'A', oct: 2, cents: 0, hz: noteHz(45) },
  { id: 'a2-plus7', label: 'A2 +7 c', note: 'A', oct: 2, cents: 7, hz: noteHz(45) * 2 ** (7 / 1200) },
  { id: 'e2-minus12.5', label: 'E2 −12.5 c', note: 'E', oct: 2, cents: -12.5, hz: noteHz(40) * 2 ** (-12.5 / 1200) },
  { id: 'e4-plus0.3', label: 'E4 +0.3 c', note: 'E', oct: 4, cents: 0.3, hz: noteHz(64) * 2 ** (0.3 / 1200) },
];

/** One sample of the tone at time t (s): peak about 0.4 (−8 dBFS). */
export function toneAt(hz, t) {
  let y = 0;
  for (let k = 1; k <= 5; k++) y += Math.sin(2 * Math.PI * k * hz * t) / k;
  return 0.18 * y;
}

/** Float samples of the tone, with a 20 ms fade-in so the start does not click. */
export function render(hz, seconds, rate) {
  const n = Math.round(seconds * rate), fade = Math.round(0.02 * rate);
  const x = new Float32Array(n);
  for (let i = 0; i < n; i++) x[i] = toneAt(hz, i / rate) * Math.min(1, i / fade);
  return x;
}

/** A mono 16-bit PCM WAV of the tone. */
export function writeWav(path, hz, seconds, rate = 48000) {
  const x = render(hz, seconds, rate);
  const buf = Buffer.alloc(44 + 2 * x.length);
  buf.write('RIFF', 0);
  buf.writeUInt32LE(36 + 2 * x.length, 4);
  buf.write('WAVEfmt ', 8);
  buf.writeUInt32LE(16, 16);
  buf.writeUInt16LE(1, 20);            // PCM
  buf.writeUInt16LE(1, 22);            // mono
  buf.writeUInt32LE(rate, 24);
  buf.writeUInt32LE(rate * 2, 28);
  buf.writeUInt16LE(2, 32);
  buf.writeUInt16LE(16, 34);
  buf.write('data', 36);
  buf.writeUInt32LE(2 * x.length, 40);
  for (let i = 0; i < x.length; i++) buf.writeInt16LE(Math.round(Math.max(-1, Math.min(1, x[i])) * 32767), 44 + 2 * i);
  fs.writeFileSync(path, buf);
}
