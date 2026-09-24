// SPDX-License-Identifier: MIT
// A static server for the built demo under a sub-path, as GitHub Pages serves a project site
// (https://<user>.github.io/squatch-tuner/). http://127.0.0.1 is a secure context, so the
// microphone works here without HTTPS.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';

const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.css': 'text/css',
  '.svg': 'image/svg+xml', '.wasm': 'application/wasm', '.jpg': 'image/jpeg', '.png': 'image/png' };

export function serve(dir, prefix = '/squatch-tuner/') {
  const root = path.resolve(dir);
  const server = http.createServer((req, res) => {
    const url = new URL(req.url, 'http://x');
    if (!url.pathname.startsWith(prefix)) return end(res, 404);
    const rel = decodeURIComponent(url.pathname.slice(prefix.length)) || 'index.html';
    const file = path.resolve(root, rel);
    if (!file.startsWith(root + path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) return end(res, 404);
    res.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] ?? 'application/octet-stream', 'Cache-Control': 'no-store' });
    fs.createReadStream(file).pipe(res);
  });
  return new Promise((resolve) => server.listen(0, '127.0.0.1', () => {
    resolve({ url: `http://127.0.0.1:${server.address().port}${prefix}`, close: () => server.close() });
  }));
}

function end(res, code) {
  res.writeHead(code, { 'Content-Type': 'text/plain' });
  res.end(String(code));
}
