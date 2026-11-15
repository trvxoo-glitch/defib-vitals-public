// Stored-only zip reader and writer for share packs (the format include/Vitals/Zip.h writes).
// The reader refuses anything else (compression, encryption, zip64, data descriptors) instead of guessing.

const TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

export function crc32(bytes) {
  let c = 0xffffffff;
  for (let i = 0; i < bytes.length; i++) c = TABLE[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

// Returns [{name, data: Uint8Array}] or throws Error with a message fit to show the player.
export function readZip(buf, { maxEntries = 64 } = {}) {
  const b = buf instanceof Uint8Array ? buf : new Uint8Array(buf);
  const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
  if (b.length < 22) throw new Error('not a zip file');
  let eocd = -1;
  for (let p = b.length - 22; p >= Math.max(0, b.length - 22 - 65535); p--) {
    if (v.getUint32(p, true) === 0x06054b50) {
      eocd = p;
      break;
    }
  }
  if (eocd < 0) throw new Error('not a zip file');
  const count = v.getUint16(eocd + 10, true);
  const dirSize = v.getUint32(eocd + 12, true);
  const dirStart = v.getUint32(eocd + 16, true);
  if (count > maxEntries) throw new Error(`too many files in the zip (${count}, at most ${maxEntries})`);
  if (dirStart + dirSize > eocd) throw new Error('damaged zip (directory)');
  const dec = new TextDecoder('utf-8', { fatal: true });
  const out = [];
  let p = dirStart;
  for (let k = 0; k < count; k++) {
    if (p + 46 > b.length || v.getUint32(p, true) !== 0x02014b50) throw new Error('damaged zip (entry)');
    const flags = v.getUint16(p + 8, true);
    const method = v.getUint16(p + 10, true);
    const crc = v.getUint32(p + 16, true);
    const csize = v.getUint32(p + 20, true);
    const size = v.getUint32(p + 24, true);
    const nlen = v.getUint16(p + 28, true);
    const xlen = v.getUint16(p + 30, true);
    const clen = v.getUint16(p + 32, true);
    const off = v.getUint32(p + 42, true);
    const name = dec.decode(b.subarray(p + 46, p + 46 + nlen));
    p += 46 + nlen + xlen + clen;
    if (method !== 0 || csize !== size) throw new Error(`${name}: only uncompressed zips made by Vitals are accepted`);
    if (flags & 0x1) throw new Error(`${name}: encrypted`);
    if (off + 30 > b.length || v.getUint32(off, true) !== 0x04034b50) throw new Error(`${name}: damaged zip`);
    const start = off + 30 + v.getUint16(off + 26, true) + v.getUint16(off + 28, true);
    if (start + size > b.length) throw new Error(`${name}: damaged zip`);
    const data = b.subarray(start, start + size);
    if (crc32(data) !== crc) throw new Error(`${name}: damaged zip (checksum)`);
    out.push({ name, data });
  }
  return out;
}

export function writeZip(entries, when = new Date()) {
  const enc = new TextEncoder();
  const time = ((when.getUTCHours() & 0x1f) << 11) | ((when.getUTCMinutes() & 0x3f) << 5) | ((when.getUTCSeconds() / 2) & 0x1f);
  const date = (((when.getUTCFullYear() - 1980) & 0x7f) << 9) | (((when.getUTCMonth() + 1) & 0xf) << 5) | (when.getUTCDate() & 0x1f);
  const parts = [];
  const dir = [];
  let offset = 0;
  for (const { name, data } of entries) {
    const n = enc.encode(name);
    const d = typeof data === 'string' ? enc.encode(data) : data;
    const crc = crc32(d);
    const h = new DataView(new ArrayBuffer(30));
    h.setUint32(0, 0x04034b50, true);
    h.setUint16(4, 20, true);
    h.setUint16(6, 0x0800, true);
    h.setUint16(8, 0, true);
    h.setUint16(10, time, true);
    h.setUint16(12, date, true);
    h.setUint32(14, crc, true);
    h.setUint32(18, d.length, true);
    h.setUint32(22, d.length, true);
    h.setUint16(26, n.length, true);
    h.setUint16(28, 0, true);
    parts.push(new Uint8Array(h.buffer), n, d);
    const c = new DataView(new ArrayBuffer(46));
    c.setUint32(0, 0x02014b50, true);
    c.setUint16(4, 20, true);
    c.setUint16(6, 20, true);
    c.setUint16(8, 0x0800, true);
    c.setUint16(10, 0, true);
    c.setUint16(12, time, true);
    c.setUint16(14, date, true);
    c.setUint32(16, crc, true);
    c.setUint32(20, d.length, true);
    c.setUint32(24, d.length, true);
    c.setUint16(28, n.length, true);
    c.setUint32(42, offset, true);
    dir.push(new Uint8Array(c.buffer), n);
    offset += 30 + n.length + d.length;
  }
  const dirSize = dir.reduce((s, x) => s + x.length, 0);
  const e = new DataView(new ArrayBuffer(22));
  e.setUint32(0, 0x06054b50, true);
  e.setUint16(8, entries.length, true);
  e.setUint16(10, entries.length, true);
  e.setUint32(12, dirSize, true);
  e.setUint32(16, offset, true);
  const all = [...parts, ...dir, new Uint8Array(e.buffer)];
  const out = new Uint8Array(all.reduce((s, x) => s + x.length, 0));
  let p = 0;
  for (const x of all) {
    out.set(x, p);
    p += x.length;
  }
  return out;
}
