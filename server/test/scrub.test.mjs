// The server scrubber must give exactly the C++ scrubber's results: same cases file (tests/scrub_cases.txt).
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import { crashLog, looksRandom, text } from '../scrub.js';

const CASES = new URL('../../tests/scrub_cases.txt', import.meta.url);

test('scrub_cases.txt', () => {
  let ctx = { roots: [] };
  let input = null, crash = false, n = 0;
  const failed = [];
  for (let line of readFileSync(CASES, 'utf8').split('\n')) {
    if (line.endsWith('\r')) line = line.slice(0, -1);
    if (line.startsWith('ctx ')) {
      const kv = line.slice(4);
      if (kv === 'reset') ctx = { roots: [] };
      else if (kv.startsWith('user=')) ctx.user = kv.slice(5);
      else if (kv.startsWith('pc=')) ctx.pc = kv.slice(3);
      else if (kv.startsWith('domain=')) ctx.domain = kv.slice(7);
      else if (kv.startsWith('root=')) {
        const bar = kv.indexOf('|');
        ctx.roots.push([kv.slice(5, bar), kv.slice(bar + 1)]);
      }
    } else if (line.startsWith('text: ') || line.startsWith('crash: ')) {
      crash = line.startsWith('crash: ');
      input = line.slice(crash ? 7 : 6);
    } else if (line.startsWith('want: ')) {
      const got = crash ? crashLog(input, ctx, {}) : text(input, ctx, {});
      n++;
      if (got !== line.slice(6)) failed.push(`in:   ${input}\nwant: ${line.slice(6)}\ngot:  ${got}`);
    }
  }
  assert.ok(n >= 40, `only ${n} cases`);
  assert.deepEqual(failed, []);
});

test('second pass changes nothing on scrubbed text', () => {
  const ctx = { user: 'Robin Hale', pc: 'DESKTOP-4F7K2Q', roots: [['D:\\Games\\Fallout 4', '<game>']] };
  const once = text('C:\\Users\\Robin Hale\\x\\y.txt robin@example.com 192.168.0.4 D:\\Games\\Fallout 4\\Data', ctx, {});
  const hits = {};
  assert.equal(text(once, {}, hits), once);
  assert.deepEqual(hits, {});
});

test('long tokens', () => {
  assert.ok(looksRandom('Zm9vYmFyQmF6UXV4MTIzNDU2Nzg5MEFCQ0RFRkdISUpLTE1O'));
  assert.ok(!looksRandom('9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08'));
});

test('random input never throws', () => {
  const chars = '\\/:.-@"\' aZ09fF<>=_\n\t';
  let seed = 7;
  const rnd = () => (seed = (seed * 1103515245 + 12345) >>> 0);
  for (let k = 0; k < 2000; k++) {
    let s = '';
    for (let i = rnd() % 200; i > 0; i--) s += chars[rnd() % chars.length];
    crashLog(s, { user: 'Robin Hale', roots: [['D:\\Games\\Fallout 4', '<game>']] }, {});
  }
});
