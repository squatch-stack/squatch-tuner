// SPDX-License-Identifier: MIT
// The strobe: three rows of ink bands that drift right when the string is sharp, left when it is
// flat, and stand still when it is in tune.
//
// Rows follow partials 1, 2 and 4 of the note (the fundamental and two octaves up), as the rows
// of a mechanical strobe do. Partial k drifts k times as fast as the fundamental, so the bottom
// row shows an error the top row barely shows. Speed: the fundamental row moves GAIN band periods
// per second per cent. Each band is painted once into a tile with a ragged, dry-brush edge; every
// frame only moves the tiles, with sub-pixel offsets, so motion stays smooth at 60 fps.

const ROWS = [{ partial: 1, seed: 11 }, { partial: 2, seed: 23 }, { partial: 4, seed: 37 }];
const GAIN = 0.5;          // band periods per second per cent, on the fundamental row
const MAX_SPEED = 12;      // periods per second: above this the pattern would alias at 60 fps
const PERIOD = 64;         // CSS px per band period
const GAP = 6;             // CSS px between rows

function rng(seed) {
  let s = seed >>> 0;
  return () => {
    s = (s + 0x6d2b79f5) >>> 0;
    let t = Math.imul(s ^ (s >>> 15), 1 | s);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// A smooth wobble along the band's length: a few sines with random phases.
function wobble(rand) {
  const parts = [1, 2.3, 5.1].map((f) => ({ f, p: rand() * Math.PI * 2, a: rand() * 0.6 + 0.4 }));
  return (y) => parts.reduce((sum, w) => sum + w.a * Math.sin(w.f * y + w.p), 0) / 1.6;
}

// One band period: an ink band half a period wide, ragged edges, dry-brush streaks inside.
function paintTile(w, h, ink, seed) {
  const tile = document.createElement('canvas');
  tile.width = w;
  tile.height = h;
  const g = tile.getContext('2d');
  const rand = rng(seed);
  const left = wobble(rand), right = wobble(rand);
  const edge = w * 0.03;
  g.fillStyle = ink;
  for (let y = 0; y < h; y++) {
    const t = (y / h) * Math.PI * 2;
    const x0 = w * 0.04 + edge * left(t) + (rand() - 0.5) * edge * 0.25;
    const x1 = w * 0.52 + edge * right(t) + (rand() - 0.5) * edge * 0.6;
    g.globalAlpha = 0.9 + rand() * 0.1;
    g.fillRect(x0, y, x1 - x0, 1);
  }
  paintStreaks(g, w, h, rand);
  return tile;
}

// Dry-brush: thin lighter streaks along the band, where the brush ran short of ink.
function paintStreaks(g, w, h, rand) {
  g.globalCompositeOperation = 'destination-out';
  for (let i = 0; i < 5; i++) {
    const x = w * (0.2 + rand() * 0.3);
    const y0 = rand() * h * 0.5, len = h * (0.3 + rand() * 0.6);
    g.globalAlpha = 0.12 + rand() * 0.2;
    g.fillRect(x, y0, Math.max(1, w * 0.01), len);
  }
  g.globalCompositeOperation = 'source-over';
  g.globalAlpha = 1;
}

export class Strobe {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.phase = ROWS.map(() => 0);
    this.activity = 0;
    this.colors = null;
    new ResizeObserver(() => this.resize()).observe(canvas);
  }

  /** Colours from the page's tokens; call again when the theme changes. */
  setColors(colors) {
    this.colors = colors;
    this.rebuild();
  }

  resize() {
    const dpr = Math.min(window.devicePixelRatio || 1, 3);
    const w = Math.round(this.canvas.clientWidth * dpr), h = Math.round(this.canvas.clientHeight * dpr);
    if (!w || !h || (w === this.canvas.width && h === this.canvas.height)) return;
    this.canvas.width = w;
    this.canvas.height = h;
    this.dpr = dpr;
    this.rebuild();
  }

  rebuild() {
    if (!this.colors || !this.dpr) return;
    const gap = GAP * this.dpr;
    this.rowH = Math.floor((this.canvas.height - gap * (ROWS.length - 1)) / ROWS.length);
    this.period = Math.round(PERIOD * this.dpr);
    this.patterns = ROWS.map((r) => this.ctx.createPattern(paintTile(this.period, this.rowH, this.colors.ink, r.seed), 'repeat'));
  }

  /**
   * Advance by dt seconds at `cents` (the fast reading, already smoothed) and draw.
   * `live` is true while a note is being read; otherwise the bands settle and fade.
   */
  draw(dt, cents, live) {
    if (!this.patterns) return;
    this.activity += ((live ? 1 : 0.22) - this.activity) * (1 - Math.exp(-dt / 0.25));
    const c = this.ctx, w = this.canvas.width, gap = GAP * this.dpr;
    c.clearRect(0, 0, w, this.canvas.height);
    ROWS.forEach((row, i) => {
      const speed = live ? Math.max(-MAX_SPEED, Math.min(MAX_SPEED, GAIN * cents * row.partial)) : 0;
      this.phase[i] = (this.phase[i] + speed * dt) % 1;
      this.drawRow(i, i * (this.rowH + gap), speed);
    });
    c.globalAlpha = 1;
  }

  drawRow(i, y, speed) {
    const c = this.ctx, w = this.canvas.width, offset = this.phase[i] * this.period;
    c.globalAlpha = 1;
    c.fillStyle = this.colors.row;
    c.fillRect(0, y, w, this.rowH);
    // Fast bands blur, as a real strobe's do; contrast falls with speed.
    c.globalAlpha = this.activity / (1 + Math.abs(speed) / 6);
    c.save();
    c.translate(offset, y);
    c.fillStyle = this.patterns[i];
    c.fillRect(-offset - this.period, 0, w + 2 * this.period, this.rowH);
    c.restore();
    this.drawLabel(ROWS[i].partial, y);
  }

  drawLabel(partial, y) {
    const c = this.ctx, d = this.dpr, s = `${partial}×`;
    c.globalAlpha = 0.95;
    c.font = `600 ${11 * d}px ui-monospace, Menlo, monospace`;
    const tw = c.measureText(s).width;
    c.fillStyle = this.colors.row;
    c.fillRect(0, y, tw + 12 * d, 18 * d);
    c.fillStyle = this.colors.label;
    c.fillText(s, 6 * d, y + 13 * d);
  }
}
