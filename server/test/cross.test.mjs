// Differential check: server/scrub.js and include/Vitals/Scrub.h must agree on random input built from the pieces
// the scrubber looks for. Needs a C++20 compiler (g++ or clang++); skipped without one.
import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';
import { crashLog, text } from '../scrub.js';

const root = fileURLToPath(new URL('../../', import.meta.url));
const cxx = ['g++', 'clang++'].find((c) => spawnSync(c, ['--version']).status === 0);

test('C++ and JavaScript scrubbers agree', { skip: !cxx && 'no C++ compiler' }, () => {
  const exe = join(mkdtempSync(join(tmpdir(), 'scrub-')), 'driver');
  execFileSync(cxx, ['-std=c++20', '-O1', '-I', join(root, 'include'), join(root, 'tests/cpp/scrub_driver.cpp'), '-o', exe]);
  const ctx = { user: 'Robin Hale', pc: 'DESKTOP-4F7K2Q', domain: '', roots: [['D:\\Games\\Fallout 4', '<game>'], ['C:\\Users\\Robin Hale\\Documents', '<docs>']] };
  const pieces = ['C:\\', 'D:\\Games\\Fallout 4', '\\Users\\', 'Robin Hale', 'robin@x.com', '192.168.1.', '::', 'fe80', '00:1A:', '-', '"',
    'Name: "', 'apikey=', 'Bearer ', 'Zm9vYmFyQmF6UXV4MTIzNDU2Nzg5MEFC', ' ', '.dll ', '/', '\\\\', 'profiles\\', 'userdata\\', '7656119',
    'S-1-5-21-', '<user>', 'a', 'Z', '9', '.', '@', "'", ';', 'x.txt', 'version', 'DESKTOP-4F7K2Q', '12:34:56', '\t', '{', '}', '='];
  let seed = 42;
  const rnd = () => (seed = (seed * 1103515245 + 12345) >>> 0);
  const lines = [];
  for (let k = 0; k < 20000; k++) {
    let s = '';
    for (let i = rnd() % 14; i > 0; i--) s += pieces[rnd() % pieces.length];
    lines.push((rnd() % 3 === 0 ? 'C' : 'T') + s);
  }
  const out = execFileSync(exe, { input: lines.join('\n') + '\n', maxBuffer: 1 << 28 }).toString().split('\n');
  const bad = [];
  lines.forEach((l, i) => {
    const js = l[0] === 'C' ? crashLog(l.slice(1), ctx, {}) : text(l.slice(1), ctx, {});
    if (js !== out[i] && bad.length < 5) bad.push({ in: l, cpp: out[i], js });
  });
  assert.deepEqual(bad, []);
});
