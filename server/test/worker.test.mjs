// The upload server end to end, with in-memory stand-ins for R2 and KV.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import worker, { checkPack, MAX_BYTES, UPLOADS_PER_HOUR } from '../worker.js';
import { readZip, writeZip } from '../zip.js';

function memoryBucket() {
  const m = new Map();
  return {
    m,
    async head(k) { return m.has(k) ? { key: k } : null; },
    async get(k) { const o = m.get(k); return o ? { body: o.data } : null; },
    async put(k, data, opts = {}) { m.set(k, { data: typeof data === 'string' ? new TextEncoder().encode(data) : data, ...opts }); },
    async delete(k) { m.delete(k); },
    async list({ prefix }) {
      const objects = [...m].filter(([k]) => k.startsWith(prefix)).map(([key, o]) => ({ key, size: o.data.length, customMetadata: o.customMetadata }));
      return { objects, truncated: false };
    },
  };
}
function memoryKv() {
  const m = new Map();
  return { async get(k) { return m.get(k) ?? null; }, async put(k, v) { m.set(k, v); } };
}
const env = () => ({ PACKS: memoryBucket(), RATE: memoryKv(), READ_KEY: 'k'.repeat(40), SALT_KEY: 's' });
const manifest = JSON.stringify({ format: 'vitals-share 1', vitals_version: '0.4.0', runtime: 'AE', game_version: '1.11.240', install_id: 'a'.repeat(32) });
const pack = (extra = []) => writeZip([{ name: 'manifest.json', data: manifest }, { name: 'f4se.log', data: 'plugin directory = C:\\Users\\Robin\\Games\\x.dll\n' }, ...extra]);
const post = (body, e, ip = '1.2.3.4') =>
  worker.fetch(new Request('https://share.test/v1/pack', { method: 'POST', body, headers: { 'cf-connecting-ip': ip } }), e);

test('upload, second scrub pass, admin download, delete', async () => {
  const e = env();
  const r = await post(pack([{ name: 'crash/crash-2026-11-15-10-42-13.log', data: '\tName: "Nate"\n' }]), e);
  const j = await r.json();
  assert.equal(r.status, 200, JSON.stringify(j));
  assert.match(j.code, /^VTL-[A-Z2-9]{4}-[A-Z2-9]{4}$/);
  assert.ok(j.second_pass_hits >= 2);
  const stored = e.PACKS.m.get(`packs/${j.code}.zip`);
  assert.equal(stored.customMetadata.runtime, 'AE');
  const files = Object.fromEntries(readZip(stored.data).map((f) => [f.name, new TextDecoder().decode(f.data)]));
  assert.ok(!files['f4se.log'].includes('Robin'));
  assert.equal(files['crash/crash-2026-11-15-10-42-13.log'], '\tName: <text>\n');
  // admin: no key, wrong key, right key
  const admin = (path, key) => worker.fetch(new Request(`https://share.test${path}`, { headers: key ? { authorization: `Bearer ${key}` } : {} }), e);
  assert.equal((await admin('/v1/admin/packs')).status, 401);
  assert.equal((await admin('/v1/admin/packs', 'wrong')).status, 401);
  const list = await (await admin('/v1/admin/packs', e.READ_KEY)).json();
  assert.equal(list.packs.length, 1);
  assert.equal((await admin(`/v1/admin/pack/${j.code}`, e.READ_KEY)).status, 200);
  assert.ok([...e.PACKS.m.keys()].some((k) => k.startsWith('audit/')));
  // delete with the code
  const d = await worker.fetch(new Request(`https://share.test/v1/pack/${j.code.toLowerCase()}`, { method: 'DELETE' }), e);
  assert.equal(d.status, 200);
  assert.ok(!e.PACKS.m.has(`packs/${j.code}.zip`));
  const again = await worker.fetch(new Request(`https://share.test/v1/pack/${j.code}`, { method: 'DELETE' }), e);
  assert.equal(again.status, 404);
});

test('refuses what Vitals would never send', () => {
  assert.throws(() => checkPack(writeZip([{ name: 'f4se.log', data: 'x' }])), /not a Vitals share pack/);
  assert.throws(() => checkPack(pack([{ name: 'saves/Save1.fos', data: 'x' }])), /not a file Vitals puts/);
  assert.throws(() => checkPack(pack([{ name: 'Vitals.dll', data: 'MZ' }])), /not a file Vitals puts/);
  assert.throws(() => checkPack(pack([{ name: 'status.json', data: new Uint8Array([1, 0, 2]) }])), /not a text file/);
  assert.throws(() => checkPack(new Uint8Array([1, 2, 3])), /not a zip/);
  const z = pack();
  z[z.length - 30] ^= 0xff; // damage the directory
  assert.throws(() => checkPack(z));
});

test('size and rate limits', async () => {
  const e = env();
  const big = await worker.fetch(new Request('https://share.test/v1/pack', { method: 'POST', body: new Uint8Array(MAX_BYTES + 1) }), e);
  assert.equal(big.status, 413);
  for (let i = 0; i < UPLOADS_PER_HOUR; i++) assert.equal((await post(pack(), e, '9.9.9.9')).status, 200);
  assert.equal((await post(pack(), e, '9.9.9.9')).status, 429);
  assert.equal((await post(pack(), e, '8.8.8.8')).status, 200);
  // the rate-limit keys never hold the address
  assert.ok(![...e.PACKS.m.values()].some((o) => new TextDecoder().decode(o.data).includes('9.9.9.9')));
});

test('upload page', async () => {
  const r = await worker.fetch(new Request('https://share.test/'), env());
  const html = await r.text();
  assert.equal(r.status, 200);
  assert.ok(html.includes('Vitals share pack'));
  assert.ok(!/<script[^>]+src=/.test(html), 'no outside scripts');
});
