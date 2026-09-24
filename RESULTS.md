<!-- SPDX-License-Identifier: MIT -->
# Squatch Tuner: measured results

Measured on 2026-09-23 on an Apple M1 Max (clang 21, `-O3`, no fast-math) with the code in this
commit. `make results` in the tuner's directory reproduces every number. The raw output is in `results/`. Evidence tags:
**(M)** measured here, **(E)** estimate.

## Setup

- **Methods compared.** Four methods share one front end: decimation to 12 kHz, a 64-sample
  (5.3 ms) hop, and the same −70 dBFS gate.
  - **YIN** and **MPM**, each with a median-of-5 smoother, as most tuners use. They also run
    at the full 48 kHz as a stronger baseline.
  - **MPM + phase vocoder:** MPM finds the note. Each partial's phase advance between two
    Hann-windowed frames one period apart gives its frequency, and the same stiffness fit
    as ours combines them.
  - **Squatch strobe:** this repository. `hz` follows the string; `steadyHz` is the window
    average.
- **Synthetic truth.** Plucked, stiff strings from E1 to E6:
  - 12 notes × 8 trials, each detuned at random by up to ±50 c;
  - stiffness B from 3×10⁻⁵ to 3×10⁻⁴;
  - pluck and pickup combs, decay, pick noise;
  - a tension-modulation glide of +g·envelope², so the pluck starts sharp.
  - Accuracy is scored against the true pitch at each moment (glide included). Lock time is
    scored against the settled pitch: the first reading within tolerance after which no
    reading leaves it.
- **Real DI.**
  - Source: the Guitar-TECHS single-note recordings, both players (P1, P2). Pedroza, Abreu,
    Corey and Roman, ICASSP 2025, doi:10.1109/ICASSP49660.2025.10887996, data at
    [Zenodo record 14963133](https://zenodo.org/records/14963133), **CC BY 4.0**.
  - Nothing from the dataset is copied into this repository. The bench reads the files from a
    local copy (`GUITAR_TECHS` in the Makefile).
  - The data: 275 notes that ring at least 1.5 s. Every method runs continuously over each
    whole 9-minute file.
  - No other audio is used: everything else is synthesised by the bench.
  - Real notes have no ground truth, so each file is also resampled up by exactly 7.0 cents
    (band-limited) and each method's measured shift is compared with 7.0.

## Synthetic plucks (M)

RMS error in cents over 0.5–1.5 s after the pluck (0.25–0.5 s for the dead note), against the
true pitch at that moment. Percentages are readings more than 50 c out (octave slips, hum).

| Scenario | YIN | MPM | YIN @48k | MPM @48k | MPM + PV | Squatch `hz` | Squatch `steadyHz` |
|---|---|---|---|---|---|---|---|
| Clean (60 dB SNR) | 2.75 | 1.23 | 0.128 | 0.032 | 0.029 | **0.001** | 0.001 |
| Guitar pluck | 3.97 | 1.90 | 1.31 | 1.23 | 0.036 | **0.036** | 0.220 |
| Fundamental −20 dB | 316 (8.2%) | 204 (1.5%) | 315 (8.1%) | 65 (0.5%) | 204 (1.5%) | **0.037 (0%)** | 0.216 |
| 30 dB SNR + hum | 190 (5.0%) | 176 (5.1%) | 117 (5.7%) | 56 (1.3%) | 177 (4.9%) | **0.055 (0%)** | 0.231 |
| Hard pluck (15 c glide) | 3.90 | 2.21 | 1.47 | 1.38 | **0.171** | 0.178 | 1.08 |
| Dead note (T60 0.6 s) | 3.17 | 1.39 | 3.05 | 1.08 | 0.165 | 0.021 | **0.020** |

Lock times (median / p90, ms):

| | YIN | MPM | MPM @48k | MPM + PV | Squatch `hz` | Squatch `steadyHz` |
|---|---|---|---|---|---|---|
| ±1 c, clean | 55 / 65 | 44 / 65 | 39 / 65 | 44 / 108 | **39 / 87** | 39 / 92 |
| ±0.1 c, clean | 1484 / 1500 | 471 / 1487 | 65 / 1479 | 1457 / 1500 | **44 / 145** | 44 / 148 |
| ±1 c, guitar pluck | 873 / 1283 | 839 / 1306 | 892 / 1341 | 500 / 657 | **465 / 604** | 639 / 839 |
| ±1 c, dead note | 204 / 455 | 151 / 435 | 156 / 441 | 97 / 439 | **87 / 103** | 92 / 108 |

On the guitar pluck, ±1 c of the settled pitch takes about half a second whatever the method,
because the string is still gliding. Ablations, also in `results/synth.md`:

- **Without the stiffness fit**, the guitar-pluck error rises to 1.29 c. That is the
  stiff-string bias.
- **With only the line fit** (`steadyHz`), the error is the window lag during a glide.

Per-note tables are in `results/synth.md`.

## Real guitar DI (M)

| Method | Re-pitch error, median / abs p50 / abs p95 (c) | Jitter p50 (c) | Stable ±1 c p50 / p90 (ms) | Stable ±0.1 c p50 (ms) |
|---|---|---|---|---|
| YIN | 0.027 / 0.130 / 0.505 | 0.40 | 776 / 1462 | 1493 |
| MPM | 0.003 / 0.041 / 0.250 | 0.37 | 704 / 1141 | 1483 |
| MPM + PV | −0.001 / 0.058 / 0.293 | 0.53 | 1216 / 1493 | 1493 |
| Squatch `hz` | 0.000 / 0.008 / 0.176 | 0.51 | 864 / 1262 | 1435 |
| **Squatch `steadyHz`** | 0.001 / **0.005 / 0.131** | 0.38 | 715 / 1182 | **1168** |

Notes on this table:

- **Jitter** is the spread 0.5–1.0 s after the pluck. The guitar's own pitch still wanders
  there, so no method can reach zero. **Stable** means the reading stays within the band
  around the note's own settled value (median over 1.0–1.5 s).
- **Coverage.** 270 notes had a Squatch reading in the settled window, against 272 for the
  others.
- **One note is an octave dispute.** A fretted G4 had the open G string ringing in sympathy,
  in tune. Squatch reads G3 and the others G4; the cents agree within 1.3 c. Which side the
  "slip" is counted on depends on which methods form the majority (`results/real.md` against
  `results/real-variants.md`).
- **Stiffness on real strings.** On real strings MPM reads 2 to 6 c sharp of the partial-1
  frequency the strobe measures. Measured partials of a fretted low-E note gave B ≈ 1×10⁻³.

### The pluck glide on real strings (M)

Pitch after the pluck, relative to the settled pitch (1.0–1.5 s), from the Squatch `hz` trace:

| after | 50 ms | 100 ms | 200 ms | 300 ms | 500 ms | 750 ms |
|---|---|---|---|---|---|---|
| p10 | −0.76 | −2.82 | −3.62 | −3.21 | −2.36 | −1.77 |
| median | **+5.22** | +2.06 | +0.85 | +0.22 | −0.06 | −0.17 |
| p90 | +15.0 | +7.85 | +5.25 | +4.16 | +2.23 | +1.59 |

### Variants on the same notes (M, `results/real-variants.md`)

| `steadyHz` variant | abs p50 / p95 (c) | Jitter (c) | Stable ±1 c p50 (ms) | Stable ±0.1 c p50 (ms) |
|---|---|---|---|---|
| default (6 partials, 0.5 s) | 0.005 / 0.131 | 0.38 | 715 | 1168 |
| memory 0.35 s | 0.005 / 0.149 | 0.44 | 736 | 1264 |
| memory 0.7 s | 0.004 / 0.187 | 0.38 | 619 | 1128 |
| 3 partials | 0.004 / 0.105 | **0.32** | **579** | 1168 |
| no stiffness fit | 0.004 / 0.107 | 0.48 | 763 | 1301 |

Three partials look better on real notes. That is a candidate default, to be re-checked on the
synthetic suite, where fewer partials weaken the stiffness fit.

## Polyphonic strum check (M, `results/poly.md`)

RMS cents over 300 strums. "Real" is the poly reading minus the mono tuner's reading of the
same open-string pluck alone. The strums mix open strings from P1 with 0–40 ms spread and 0 to
−6 dB levels. "Synthetic" is the error against the true pitch, with every string detuned by
up to ±20 c.

| String | Real 0.5 s | Real 1 s | Synthetic 0.5 s | Synthetic 1 s |
|---|---|---|---|---|
| E2 | 0.73 | 1.86 | 0.86 | 0.58 |
| A2 | 0.77 | 0.98 | 0.99 | 0.70 |
| D3 | 0.37 | 0.32 | 1.08 | 0.78 |
| G3 | 1.05 | 1.12 | 0.74 | 0.46 |
| B3 | 5.90 | 2.84 | 6.40 | 6.72 |
| E4 | 6.04 | 4.83 | 8.40 | 6.89 |

B3 and E4 fail by frequency separation alone. Every partial of B3 is within 0.3 Hz of a
multiple of E2's 3rd, and E2's 4th, A2's 3rd and E4 coincide. The fix is joint estimation:
subtract the low strings' already-measured partials.

## CPU and memory (`results/perf.md`)

| | ns per 48 kHz sample, playing | Share of one M1 Max core | Quiet |
|---|---|---|---|
| Squatch Tuner (6 partials) | 158.9 | 0.76% | 0.03% |
| Squatch Tuner (3 partials) | 153.4 | 0.74% | 0.03% |
| MPM alone | 438.6 | 2.11% | 0.03% |
| YIN alone | 250.6 | 1.20% | 0.04% |
| MPM + phase vocoder | 490.5 | 2.35% | 0.04% |
| Poly strum check | 27.1 | 0.13% | 0.13% |

All of these are measured (M). Memory is all static (M): `Tuner` 128 kB, `PolyTuner` 234 kB.
Both are sized for the worst case (512-point fit windows, 8 partials, 8 strings). Trimming
them to what is used would bring the mono tuner to about 70 kB (E).

Most of the mono cost is the MPM coarse check. One check is 152k multiply-adds (a 636-sample
window × 318 lags), run every 4th hop while a note rings and every hop at a note's start. On
the pedals (E):

| | While a note rings | At a note's start, or loud unpitched input |
|---|---|---|
| **Daisy (Cortex-M7 480 MHz)** | about 16 M cycles/s = **3%** | about 48 M cycles/s = 10% |
| **ESP32-S3 (240 MHz), plain C** | about 60 M cycles/s = **25%** of one core | about 70% |
| **ESP32-S3 with ESP-DSP and a float fit** | about 21 M cycles/s = **9%** | about 24% |

How the estimates are built:

- **Daisy:** the M7 is scalar float at about 1.5 cycles per multiply-add, and its FPU does
  doubles in hardware.
- **ESP32-S3, plain C:** 5.2 cycles per tap and software doubles in the fits.
- **ESP32-S3, optimised:** ESP-DSP's dot product at about 1.7 cycles per tap, with the fit
  sums made float.
- **Cutting it further:** restrict the coarse check to lags near the current note while
  tracking.
- **Status:** none of these is measured on target yet.

## Live on the Pi 5 (M, 2026-09-24)

The core ran live on a Pi 5 test bench:

- **Hardware.** Raspberry Pi 5 with a UMC204HD, performance governor.
- **Audio.** jackd at 48 kHz, 64 × 3 frames.
- **Software.** `hosts/jack/tuner_live.cpp`, built with g++ 14 at `-O3`.
- **Signal path.** Test tones from `squatch-tone` left MAIN OUTS L at −20 dBFS, went over the
  bench's cable into Input 2, and arrived at −57.5 dBFS peak.
- **Truth.** The DAC and ADC share one clock, so the tone's frequency is the truth.
- **Scoring.** The steady reading 2 to 6 s after each tone started.

| Tone | Truth (c) | Shown | Steady median (c) | Range (c) | Correct note in the stream |
|---|---|---|---|---|---|
| 110.000000 Hz | 0.000 | A2 | 0.000 | −0.004 … +0.003 | 82–86 ms |
| 110 × 2^(7/1200) = 110.445670 Hz | +7.000 | A2 | +7.000 | +6.997 … +7.004 | 55–64 ms |
| the same, A4 = 442 Hz | −0.851 | A2 | −0.851 | −0.854 … −0.848 | 72 ms |
| 81.814031 Hz (E2 −12.5 c) | −12.500 | E2 | −12.500 | −12.504 … −12.494 | 68–93 ms |
| 196.000000 Hz (G3) | +0.020 | G3 | +0.020 | +0.018 … +0.022 | 43–70 ms |
| 329.684682 Hz (E4 +0.3 c) | +0.300 | E4 | +0.300 | +0.299 … +0.301 | 44–51 ms |

Repeat counts:

- A2 and A2 +7 c were each scored 15 times, including 4 cycles straight after a fresh start.
- The other tones were scored 6 times each.
- The note times are from the runs whose log kept them.

**One unexplained run.** In one run of the +7 c tone (1 of 53 tone runs), the steady reading
held at +6.463 c for the whole 4 s, with one excursion to +13.4 c. That run was right after a
restart.

- A constant −0.54 c is what a stiffness estimate of B ≈ 6×10⁻⁴ does to a single-partial
  input. The fit applies B to f0 but only re-learns it while three or more partials count.
- This is a hypothesis, not a finding: B was not logged then.
- It did not reproduce in the next 41 runs, 28 of them with B logged (all B = 0).
- It did not reproduce in 20 offline trials of the same sequence in white noise.
- B is now in the stream (`"B"`), so a recurrence will show its cause.
- Real strings have several partials, which pin B.

**Latency.**

| Stage | Time | How it was measured |
|---|---|---|
| Steady readings, input jack to a drawn frame | 17–20 ms | The page's own measurement (clock offset from the minimum round trip), a Mac on the same Wi-Fi through a local HTTPS proxy. It includes JACK's 1.33 ms capture latency and the 33 ms publish cadence (16.7 ms on average). It excludes the converter and USB delay and the display's scan-out. |
| A new note, tone onset to the correct note in the Pi's stream | 43–93 ms | The core's own lock time plus the output and input paths |

Adding the two, a new note reaches the screen in about 60 to 115 ms.

**CPU, over 30 s, with a tone playing and one browser attached (% of one Cortex-A76 core at
2.4 GHz).**

| Process | CPU |
|---|---|
| `squatch-tuner-live` | 1.0% |
| jackd | 0.8% |
| `serve.py` | 0.3% |
| **Total** | **about 2.2%**, 0.5% of the Pi |

The audio callback took 1.0% of each 1.33 ms period on average; the worst callback seen was
13% of a period. There were no xruns.

## In the browser (M, 2026-09-24)

The same core compiled to WebAssembly (`hosts/wasm`: Emscripten 6.0.10, `-O3`, 31 kB, no
imports) and run in an AudioWorklet in 128-frame blocks, with the page reading it 30 times a
second. `make wasm-test` reproduces both rows.

- **Module alone** (`hosts/wasm/test/core.test.mjs`, Node 22): exact harmonic test tones
  (partials 1–5) at 48 kHz, scored 2 to 4 s after the start.
- **Whole page** (`hosts/wasm/test/browser.test.mjs`): headless Chrome with the tone as its
  microphone (a 16-bit WAV on Chrome's fake capture device, which delivers 44.1 kHz into a
  48 kHz AudioContext, so the browser's resampler is in the path). The Start button is pressed
  and the number the page shows is read 40 times over 4 s, from 2.5 s after the first reading.

| Tone | Truth (c) | Module alone, steady (c) | Page shows | Page's reading (c) |
|---|---|---|---|---|
| A2, 110 Hz | 0.000 | 0.0000 … 0.0000 | 0.0, 40 of 40 | 0.000 … 0.000 |
| A2 +7 c | +7.000 | +6.9998 … +7.0000 | +7.0, 40 of 40 | +7.000 … +7.000 |
| E2 −12.5 c | −12.500 | −12.5000 … −12.4998 | −12.5, 40 of 40 | −12.500 … −12.500 |
| E4 +0.3 c | +0.300 | +0.3000 … +0.3000 | +0.3, 40 of 40 | +0.300 … +0.300 |

The in-tune seals matched in every sample: both on for A2, the half-cent seal only for E4
+0.3 c, neither for the others. These are clean electronic tones; a plucked string settles as
described above, in the browser as anywhere.

The page also times one second of audio through a second copy of the module on its main
thread: 1.0 to 1.3% of one M1 Max core in headless Chrome.

## Where it fails

- **B3 and E4 in a strum** (above).
- **Sympathetic strings.** A fretted note with an open string ringing in sympathy at nearly
  the same pitch leaves two sources that no single-note method can separate. On a fretted G3,
  the fundamental and 2nd partial disagreed by 17 c. Harmonic weights are smoothed, so the
  reading no longer flips between them, but it can sit between them.
- **The octave of an ambiguous note** (the G4/G3 case above).
- **Hum.** Strong hum at −30 dB with the note decaying into it: readings are withheld (88%
  coverage in that scenario), not wrong.
- **The first ~40 ms** go to the coarse stage naming the note.
- **Real pluck glide.** It limits every method: ±0.1 c of the settled pitch is not reachable
  in the first ~0.3 s of a hard pluck, however good the estimator.

## Reproduce

```sh
make test                      # unit tests under ASan + UBSan
make results                   # everything above, about 20 minutes
make results GUITAR_TECHS=/path/to/guitar-techs
```
