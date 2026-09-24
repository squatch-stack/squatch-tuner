// SPDX-License-Identifier: MIT
// The WebAssembly module on its own, in Node (same engine as Chrome): each test tone through
// tuner.wasm in 128-frame blocks, as the worklet feeds it, scored on the steady part.
//
//   node hosts/wasm/test/core.test.mjs build/web-demo/tuner.wasm
import fs from 'node:fs';
import { TONES, render } from './tones.mjs';

const RATE = 48000, FIELDS = 13, TOL = 0.01;   // cents: the core alone, on exact tones
const [, , wasmPath = 'build/web-demo/tuner.wasm'] = process.argv;

function instance() {
  const { exports: e } = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasmPath)), {});
  e._initialize();
  if (e.tuner_frame_size() !== FIELDS) throw new Error(`frame has ${e.tuner_frame_size()} fields, expected ${FIELDS}`);
  if (WebAssembly.Module.imports(new WebAssembly.Module(fs.readFileSync(wasmPath))).length) throw new Error('module has imports');
  return e;
}

function score(e, tone) {
  if (e.tuner_configure(RATE, 38) !== 0) throw new Error('configure failed');
  e.tuner_set_a4(440);
  const input = new Float32Array(e.memory.buffer, e.tuner_input(), 128);
  const frame = new Float32Array(e.memory.buffer, e.tuner_frame(), FIELDS);
  const x = render(tone.hz, 4, RATE), cents = [];
  let valid = true, midi = null;
  for (let i = 0; i + 128 <= x.length; i += 128) {
    input.set(x.subarray(i, i + 128));
    e.tuner_process(128);
    if (i < 2 * RATE || (i / 128) % 12) continue;   // steady part: 2 s on, 30 readings a second
    e.tuner_frame();
    valid &&= frame[0] > 0.5;
    midi ??= frame[1];
    cents.push(frame[3]);
  }
  return { valid, midi, min: Math.min(...cents), max: Math.max(...cents) };
}

const e = instance();
let failed = 0;
for (const tone of TONES) {
  const r = score(e, tone);
  const ok = r.valid && Math.abs(r.min - tone.cents) <= TOL && Math.abs(r.max - tone.cents) <= TOL;
  if (!ok) failed++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${tone.label.padEnd(11)} truth ${tone.cents.toFixed(3)}  steady ${r.min.toFixed(4)} … ${r.max.toFixed(4)}  midi ${r.midi}`);
}
console.log(failed ? `core.test: ${failed} FAILED` : 'core.test: all tones within ±0.01 c');
process.exit(failed ? 1 : 0);
