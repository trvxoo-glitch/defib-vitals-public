// Vitals share-pack upload server: a Cloudflare Worker with a private R2 bucket. See server/README.md.
//
//   GET    /                      the upload page (page.js)
//   POST   /v1/pack               body: a share pack zip. Checks it, scrubs every file again, stores it, returns a code
//   DELETE /v1/pack/<code>        deletes that pack (the code is the only key)
//   GET    /v1/admin/packs        maintainers (Authorization: Bearer READ_KEY): list packs
//   GET    /v1/admin/pack/<code>  maintainers: download one pack (every download is recorded under audit/)
//
// Kept about the sender: nothing. No IP address, no account, no cookie; Workers logs are off (wrangler.toml).
// The rate limit keys on a SHA-256 of (a key that changes every day + the IP address), kept for one hour.

import PAGE from './page.js';
import { crashLog, text } from './scrub.js';
import { readZip, writeZip } from './zip.js';

export const MAX_BYTES = 10 * 1024 * 1024;
export const UPLOADS_PER_HOUR = 10;
const CODE_CHARS = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789'; // no 0/O, 1/I/L
const NAMES = new RegExp(
  '^(manifest\\.json|Vitals-Report\\.txt|Vitals-summary\\.txt|status\\.json|f4se\\.log|f4se_loader\\.log|plugins\\.txt|' +
    'Fallout4\\.ccc|ini-settings\\.txt|system\\.txt|dll-sources\\.txt|papyrus-summary\\.txt|breadcrumbs\\.txt|' +
    'popup_disabled\\.txt|Vitals\\.jsonl|Vitals\\.prev\\.jsonl|scrub-report\\.txt|' +
    'crash/crash-[A-Za-z0-9_.-]{1,80}\\.log|history/Vitals-[0-9-]{1,40}\\.jsonl)$',
);

const json = (body, status = 200) =>
  new Response(JSON.stringify(body), { status, headers: { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store' } });

export function newCode() {
  const r = new Uint8Array(8);
  crypto.getRandomValues(r);
  let s = '';
  for (const x of r) s += CODE_CHARS[x % CODE_CHARS.length];
  return `VTL-${s.slice(0, 4)}-${s.slice(4)}`;
}

const CODE_RE = /^VTL-[A-Z2-9]{4}-[A-Z2-9]{4}$/;
const key = (code) => `packs/${code}.zip`;

async function sha256Hex(s) {
  const d = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(s));
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

async function rateLimited(request, env) {
  if (!env.RATE) return false;
  const ip = request.headers.get('cf-connecting-ip') || '';
  const day = new Date().toISOString().slice(0, 10);
  const h = await sha256Hex(`${env.SALT_KEY || ''}|${day}|${ip}`);
  const k = `rl:${h}`;
  const n = parseInt((await env.RATE.get(k)) || '0', 10);
  if (n >= UPLOADS_PER_HOUR) return true;
  await env.RATE.put(k, String(n + 1), { expirationTtl: 3600 });
  return false;
}

function decodeText(name, data) {
  if (data.includes(0)) throw new Error(`${name}: not a text file`);
  try {
    return new TextDecoder('utf-8', { fatal: true }).decode(data);
  } catch {
    // plugins.txt and old logs can be Windows-1252
    return new TextDecoder('windows-1252').decode(data);
  }
}

// Checks a pack and runs the second scrub pass. Returns {entries, manifest, hits} or throws with a player-facing message.
export function checkPack(bytes) {
  const files = readZip(bytes);
  const seen = new Set();
  const out = [];
  const hits = {};
  let manifest = null;
  for (const { name, data } of files) {
    if (!NAMES.test(name)) throw new Error(`${name}: not a file Vitals puts in a share pack`);
    if (seen.has(name)) throw new Error(`${name}: listed twice`);
    seen.add(name);
    const raw = decodeText(name, data);
    if (name === 'manifest.json') {
      try {
        manifest = JSON.parse(raw);
      } catch {
        throw new Error('manifest.json: not valid JSON');
      }
    }
    const clean = name.startsWith('crash/') ? crashLog(raw, {}, hits) : text(raw, {}, hits);
    out.push({ name, data: clean });
  }
  if (!manifest || manifest.format !== 'vitals-share 1') throw new Error('this is not a Vitals share pack (no manifest.json)');
  return { entries: out, manifest, hits };
}

function authorized(request, env) {
  const got = request.headers.get('authorization') || '';
  const want = `Bearer ${env.READ_KEY || ''}`;
  if (!env.READ_KEY || got.length !== want.length) return false;
  let diff = 0;
  for (let i = 0; i < want.length; i++) diff |= got.charCodeAt(i) ^ want.charCodeAt(i);
  return diff === 0;
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const path = url.pathname.replace(/\/+$/, '') || '/';
    try {
      if (request.method === 'GET' && path === '/')
        return new Response(PAGE, { headers: { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' } });

      if (request.method === 'POST' && path === '/v1/pack') {
        const len = parseInt(request.headers.get('content-length') || '0', 10);
        if (len > MAX_BYTES) return json({ error: 'The pack is larger than 10 MB.' }, 413);
        if (await rateLimited(request, env)) return json({ error: 'Too many uploads from here in the last hour. Try again later.' }, 429);
        const bytes = new Uint8Array(await request.arrayBuffer());
        if (bytes.length > MAX_BYTES) return json({ error: 'The pack is larger than 10 MB.' }, 413);
        let pack;
        try {
          pack = checkPack(bytes);
        } catch (e) {
          return json({ error: String(e.message || e) }, 400);
        }
        let code = newCode();
        for (let k = 0; k < 4 && (await env.PACKS.head(key(code))); k++) code = newCode();
        const secondPass = Object.values(pack.hits).reduce((a, b) => a + b, 0);
        await env.PACKS.put(key(code), writeZip(pack.entries), {
          httpMetadata: { contentType: 'application/zip' },
          customMetadata: {
            uploaded: new Date().toISOString().slice(0, 10),
            vitals_version: String(pack.manifest.vitals_version || '').slice(0, 20),
            runtime: String(pack.manifest.runtime || '').slice(0, 4),
            game_version: String(pack.manifest.game_version || '').slice(0, 20),
            install_id: /^[0-9a-f]{32}$/.test(pack.manifest.install_id || '') ? pack.manifest.install_id : '',
            second_pass_hits: JSON.stringify(pack.hits),
          },
        });
        return json({ code, files: pack.entries.length, second_pass_hits: secondPass, deleted_after_days: 90 });
      }

      const del = path.match(/^\/v1\/pack\/([^/]+)$/);
      if (request.method === 'DELETE' && del) {
        const code = decodeURIComponent(del[1]).toUpperCase();
        if (!CODE_RE.test(code)) return json({ error: 'That is not a share code (VTL-XXXX-XXXX).' }, 400);
        if (!(await env.PACKS.head(key(code)))) return json({ error: 'No pack with that code (it may already be deleted).' }, 404);
        await env.PACKS.delete(key(code));
        return json({ deleted: code });
      }

      if (path.startsWith('/v1/admin/')) {
        if (!authorized(request, env)) return json({ error: 'not allowed' }, 401);
        if (request.method === 'GET' && path === '/v1/admin/packs') {
          const list = await env.PACKS.list({ prefix: 'packs/', cursor: url.searchParams.get('cursor') || undefined, include: ['customMetadata'] });
          return json({
            packs: list.objects.map((o) => ({ code: o.key.slice(6, -4), bytes: o.size, ...(o.customMetadata || {}) })),
            cursor: list.truncated ? list.cursor : null,
          });
        }
        const get = path.match(/^\/v1\/admin\/pack\/(VTL-[A-Z2-9]{4}-[A-Z2-9]{4})$/);
        if (request.method === 'GET' && get) {
          const obj = await env.PACKS.get(key(get[1]));
          if (!obj) return json({ error: 'not found' }, 404);
          const now = new Date().toISOString();
          await env.PACKS.put(`audit/${now.slice(0, 10)}/${now}-${get[1]}.txt`, `downloaded ${get[1]} at ${now}\n`);
          return new Response(obj.body, { headers: { 'content-type': 'application/zip', 'cache-control': 'no-store' } });
        }
      }
      return json({ error: 'not found' }, 404);
    } catch (e) {
      return json({ error: 'Server error. Nothing was stored.' }, 500);
    }
  },
};
