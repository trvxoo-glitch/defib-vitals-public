// Share-pack scrubber, server copy: a line-by-line port of include/Vitals/Scrub.h (read that file for the reasons).
// The server runs it on every file of an uploaded pack as a second pass. tests/scrub_cases.txt feeds the same
// cases to both (server/test/scrub.test.mjs and tests/cpp/rules_test.cpp), so they cannot drift apart.
// Change both files together.

const isUpper = (c) => c >= 'A' && c <= 'Z';
const isLower = (c) => c >= 'a' && c <= 'z';
const isDigit = (c) => c >= '0' && c <= '9';
const isAlpha = (c) => isUpper(c) || isLower(c);
const isAlnum = (c) => isAlpha(c) || isDigit(c);
const isHex = (c) => isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
const isSep = (c) => c === '\\' || c === '/';
const isWordChar = (c) => isAlnum(c) || c === '_';
const low = (c) => (isUpper(c) ? c.toLowerCase() : c);

function eqI(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (low(a[i]) !== low(b[i])) return false;
  return true;
}
const startsI = (s, at, what) => at + what.length <= s.length && eqI(s.substr(at, what.length), what);
const at = (s, i) => (i >= 0 && i < s.length ? s[i] : '');

function nameEnd(c) {
  return isSep(c) || c === '"' || c === "'" || c === '\r' || c === '\n' || c === '\t' || c === ';' || c === ',' || c === ')' ||
    c === ']' || c === '<' || c === '>' || c === '|';
}

function bump(n, key) {
  n[key] = (n[key] || 0) + 1;
}

function rootVariants(root) {
  const v = [root];
  const slash = root.replaceAll('\\', '/');
  const json = root.replaceAll('\\', '\\\\');
  for (const s of [slash, json]) if (s !== root) v.push(s);
  return v;
}

// 1. Known folders.
export function roots(input, ctx, n) {
  const all = [];
  for (const [root, token] of ctx.roots || []) {
    let r = root;
    while (r.length > 3 && isSep(r[r.length - 1])) r = r.slice(0, -1);
    if (r.length < 4) continue;
    for (const v of rootVariants(r)) all.push([v, token]);
  }
  all.sort((a, b) => b[0].length - a[0].length); // stable in JS, like std::ranges::stable_sort
  let out = '';
  let i = 0;
  while (i < input.length) {
    let hit = false;
    for (const [root, token] of all) {
      if (!startsI(input, i, root)) continue;
      const after = i + root.length;
      if (after < input.length && (isWordChar(input[after]) || input[after] === '-' ||
          (input[after] === ' ' && after + 1 < input.length && isAlnum(input[after + 1])))) continue;
      out += token;
      i = after;
      bump(n, 'folder');
      hit = true;
      break;
    }
    if (!hit) out += input[i++];
  }
  return out;
}

// 2. User folder names.
export function userFolders(input, ctx, n) {
  const user = ctx.user || '';
  let out = '';
  let i = 0;
  while (i < input.length) {
    const c = input[i];
    if (!isSep(c)) {
      out += input[i++];
      continue;
    }
    let j = i;
    while (j < input.length && isSep(input[j])) j++;
    out += input.slice(i, j);
    if (user.length >= 2 && startsI(input, j, user)) {
      const after = j + user.length;
      if (after === input.length || nameEnd(input[after]) || input[after] === ' ') {
        out += '<user>';
        bump(n, 'user');
        i = after;
        continue;
      }
    }
    let k = 0;
    if (startsI(input, j, 'Users')) k = j + 5;
    else if (startsI(input, j, 'home')) k = j + 4;
    if (k && k < input.length && isSep(input[k])) {
      let s = k;
      while (s < input.length && isSep(input[s])) s++;
      let e = s;
      while (e < input.length && !nameEnd(input[e])) e++;
      const name = input.slice(s, e);
      const keep = name === '' || name[0] === '<' || name[0] === '%' || eqI(name, 'Public') || eqI(name, 'Default') ||
        eqI(name, 'Default User') || eqI(name, 'All Users');
      out += input.slice(j, s);
      if (keep) out += name;
      else {
        out += '<user>';
        bump(n, 'user');
      }
      i = e;
      continue;
    }
    i = j;
  }
  return out;
}

// 3. Mod Organizer 2 profile names.
export function profiles(input, n) {
  let out = '';
  let i = 0;
  while (i < input.length) {
    if (isSep(input[i]) && startsI(input, i + 1, 'profiles') && i + 9 < input.length && isSep(input[i + 9])) {
      let s = i + 9;
      while (s < input.length && isSep(input[s])) s++;
      let e = s;
      while (e < input.length && !isSep(input[e]) && !'"\r\n\t|<>'.includes(input[e])) e++;
      out += input.slice(i, s);
      if (e > s && e < input.length && isSep(input[e]) && input[s] !== '<') {
        out += '<profile>';
        bump(n, 'profile');
      } else out += input.slice(s, e);
      i = e;
      continue;
    }
    out += input[i++];
  }
  return out;
}

// 4. Steam account ids.
export function steamIds(input, n) {
  let out = '';
  let i = 0;
  while (i < input.length) {
    if (startsI(input, i, 'userdata') && i + 8 < input.length && isSep(input[i + 8])) {
      let s = i + 8;
      while (s < input.length && isSep(input[s])) s++;
      let e = s;
      while (e < input.length && isDigit(input[e])) e++;
      if (e > s && (e === input.length || !isAlnum(input[e]))) {
        out += input.slice(i, s) + '<account>';
        bump(n, 'account');
        i = e;
        continue;
      }
    }
    if (input[i] === '7' && (i === 0 || !isAlnum(input[i - 1])) && input.substr(i, 7) === '7656119') {
      let e = i;
      while (e < input.length && isDigit(input[e])) e++;
      if (e - i === 17 && (e === input.length || !isAlnum(input[e]))) {
        out += '<account>';
        bump(n, 'account');
        i = e;
        continue;
      }
    }
    out += input[i++];
  }
  return out;
}

// 5. SIDs and GUIDs.
export function sidsAndGuids(input, n) {
  let out = '';
  let i = 0;
  const hexRun = (p, len) => {
    if (p + len > input.length) return false;
    for (let k = 0; k < len; k++) if (!isHex(input[p + k])) return false;
    return true;
  };
  while (i < input.length) {
    const boundary = i === 0 || !isAlnum(input[i - 1]);
    if (boundary && input.substr(i, 9) === 'S-1-5-21-') {
      let e = i + 9;
      let parts = 0;
      while (e < input.length && isDigit(input[e])) {
        while (e < input.length && isDigit(input[e])) e++;
        parts++;
        if (e + 1 < input.length && input[e] === '-' && isDigit(input[e + 1])) e++;
        else break;
      }
      if (parts >= 3) {
        out += '<sid>';
        bump(n, 'sid');
        i = e;
        continue;
      }
    }
    if (boundary && hexRun(i, 8) && i + 36 <= input.length && input[i + 8] === '-' && hexRun(i + 9, 4) && input[i + 13] === '-' &&
        hexRun(i + 14, 4) && input[i + 18] === '-' && hexRun(i + 19, 4) && input[i + 23] === '-' && hexRun(i + 24, 12) &&
        (i + 36 === input.length || !isAlnum(input[i + 36]))) {
      out += '<guid>';
      bump(n, 'guid');
      i += 36;
      continue;
    }
    out += input[i++];
  }
  return out;
}

// 6. Email addresses.
export function emails(input, n) {
  const isLocal = (c) => isAlnum(c) || c === '.' || c === '_' || c === '%' || c === '+' || c === '-';
  const isDomain = (c) => isAlnum(c) || c === '.' || c === '-';
  let out = '';
  let done = 0;
  for (let p = 0; p < input.length; p++) {
    if (input[p] !== '@' || p < done) continue;
    let s = p;
    while (s > done && isLocal(input[s - 1])) s--;
    let e = p + 1;
    while (e < input.length && isDomain(input[e])) e++;
    while (e > p + 1 && (input[e - 1] === '.' || input[e - 1] === '-')) e--;
    const domain = input.slice(p + 1, e);
    const dot = domain.lastIndexOf('.');
    let ok = s < p && dot > 0 && domain.length - dot - 1 >= 2;
    if (ok) for (let k = dot + 1; k < domain.length; k++) ok = ok && isAlpha(domain[k]);
    if (!ok) continue;
    out += input.slice(done, s) + '<email>';
    bump(n, 'email');
    done = e;
    p = e - 1;
  }
  return out + input.slice(done);
}

// 7. key=value secrets and Bearer tokens.
const KEYS = ['apikey', 'api_key', 'api-key', 'access_token', 'refresh_token', 'token', 'password', 'passwd', 'secret',
  'client_secret', 'authorization', 'bearer'];
export function keyValueSecrets(input, n) {
  const valueEnd = (c) => ' "\'&,;\r\n\t})'.includes(c);
  let out = '';
  let i = 0;
  while (i < input.length) {
    let hit = false;
    if (i === 0 || !isWordChar(input[i - 1])) {
      for (const key of KEYS) {
        if (!startsI(input, i, key)) continue;
        let p = i + key.length;
        if (p < input.length && (isWordChar(input[p]) || input[p] === '-')) continue;
        const bearer = key === 'bearer';
        while (p < input.length && (input[p] === '"' || input[p] === "'")) p++;
        while (p < input.length && input[p] === ' ') p++;
        if (!bearer) {
          if (p >= input.length || (input[p] !== '=' && input[p] !== ':')) continue;
          p++;
          while (p < input.length && (input[p] === ' ' || input[p] === '"' || input[p] === "'")) p++;
          if (startsI(input, p, 'bearer ')) continue;
        } else if (p === i + key.length) continue;
        let e = p;
        while (e < input.length && !valueEnd(input[e])) e++;
        if (e - p < 4 || input[p] === '<') continue;
        out += input.slice(i, p) + '<secret>';
        bump(n, 'secret');
        i = e;
        hit = true;
        break;
      }
    }
    if (!hit) out += input[i++];
  }
  return out;
}

// 8. Long random-looking tokens.
export function looksRandom(tok) {
  if (tok.length < 32) return false;
  let up = 0, lo = 0, dg = 0, hex = 0;
  const freq = new Map();
  for (const c of tok) {
    up += isUpper(c);
    lo += isLower(c);
    dg += isDigit(c);
    hex += isHex(c) || c === '-';
    freq.set(c, (freq.get(c) || 0) + 1);
  }
  if (hex === tok.length || up < 4 || lo < 4 || dg < 4) return false;
  let h = 0;
  for (const f of freq.values()) {
    const p = f / tok.length;
    h -= p * Math.log2(p);
  }
  return h >= 4.0;
}

export function longTokens(input, n) {
  const isTok = (c) => isAlnum(c) || c === '+' || c === '/' || c === '=' || c === '_' || c === '-';
  let out = '';
  let i = 0;
  while (i < input.length) {
    if (!isTok(input[i])) {
      out += input[i++];
      continue;
    }
    let e = i;
    while (e < input.length && isTok(input[e])) e++;
    const tok = input.slice(i, e);
    if (looksRandom(tok)) {
      out += '<secret>';
      bump(n, 'secret');
    } else out += tok;
    i = e;
  }
  return out;
}

// 9. IP and MAC addresses.
export function addresses(input, n) {
  let out = '';
  let i = 0;
  const versionBefore = (p) => /version/i.test(input.slice(Math.max(0, p - 24), p));
  while (i < input.length) {
    const c = input[i];
    const prev = i ? input[i - 1] : ' ';
    if (isDigit(c) && !isAlnum(prev) && prev !== '.' && prev !== '_' && prev !== '-') {
      const oct = [];
      let p = i;
      let ok = true;
      while (oct.length < 4) {
        const s = p;
        let v = 0;
        while (p < input.length && isDigit(input[p]) && p - s < 4) v = v * 10 + (input.charCodeAt(p++) - 48);
        if (p === s || p - s > 3 || v > 255) {
          ok = false;
          break;
        }
        oct.push(v);
        if (oct.length < 4) {
          if (p + 1 < input.length && input[p] === '.' && isDigit(input[p + 1])) p++;
          else {
            ok = false;
            break;
          }
        }
      }
      const tailOk = p === input.length || (!isAlnum(input[p]) && !(input[p] === '.' && p + 1 < input.length && isDigit(input[p + 1])));
      if (ok && tailOk && oct[0] >= 10 && !versionBefore(i)) {
        out += '<ip>';
        bump(n, 'ip');
        i = p;
        continue;
      }
    }
    if (isHex(c) && !isWordChar(prev) && prev !== ':' && prev !== '.') {
      let e = i;
      while (e < input.length && (isHex(input[e]) || input[e] === ':')) e++;
      const endOk = e === input.length || (!isWordChar(input[e]) && !(input[e] === '.' && e + 1 < input.length && isHex(input[e + 1])));
      if (endOk) {
        const run = input.slice(i, e);
        const groups = run.split(':').filter((g) => g.length);
        const colons = run.split(':').length - 1;
        let groupsOk = groups.length > 0, letter = false, len2 = groups.length === 6, long3 = false;
        for (const g of groups) {
          groupsOk = groupsOk && g.length <= 4;
          len2 = len2 && g.length === 2;
          long3 = long3 || g.length >= 3;
          for (const h of g) letter = letter || isAlpha(h);
        }
        const mac = len2 && colons === 5;
        const v6 = groupsOk && colons >= 2 && groups.length >= 3 && letter && long3;
        if (mac || v6) {
          out += mac ? '<mac>' : '<ip>';
          bump(n, mac ? 'mac' : 'ip');
          i = e;
          continue;
        }
      }
      if (i + 17 <= input.length) {
        let m = true;
        for (let k = 0; k < 17 && m; k++) m = k % 3 === 2 ? input[i + k] === '-' : isHex(input[i + k]);
        if (m && (i + 17 === input.length || (!isAlnum(input[i + 17]) && input[i + 17] !== '-'))) {
          out += '<mac>';
          bump(n, 'mac');
          i += 17;
          continue;
        }
      }
    }
    out += input[i++];
  }
  return out;
}

// 10. User and PC names as whole words.
const COMMON = ['admin', 'administrator', 'user', 'owner', 'player', 'gamer', 'games', 'game', 'data', 'mods', 'steam', 'test', 'home',
  'default', 'public', 'guest', 'fallout', 'desktop', 'pc', 'windows'];
export const commonWord = (w) => COMMON.some((c) => eqI(w, c));

export function words(input, word, min, token, n, key) {
  word = word || '';
  if (word.length < min || commonWord(word)) return input;
  let out = '';
  let i = 0;
  while (i < input.length) {
    if ((i === 0 || !isAlnum(input[i - 1])) && startsI(input, i, word) && (i + word.length === input.length || !isAlnum(input[i + word.length]))) {
      out += token;
      bump(n, key);
      i += word.length;
      continue;
    }
    out += input[i++];
  }
  return out;
}

// 11. Other full paths keep their last two names.
export function paths(input, n) {
  const drive = (p) => p + 2 < input.length && isAlpha(input[p]) && (p === 0 || !isAlnum(input[p - 1])) && input[p + 1] === ':' && isSep(input[p + 2]);
  const hard = (c) => '"\'<>|*?\r\n\t;,()[]{}'.includes(c);
  const token = (p) => {
    if (input[p] !== '<') return 0;
    let k = p + 1;
    while (k < input.length && isLower(input[k])) k++;
    return k > p + 1 && k < input.length && input[k] === '>' ? k + 1 - p : 0;
  };
  let out = '';
  let i = 0;
  while (i < input.length) {
    if (!drive(i)) {
      out += input[i++];
      continue;
    }
    let e = i + 2;
    let lastSep = i + 2;
    while (e < input.length) {
      const c = input[e];
      const t = token(e);
      if (t) {
        e += t;
        continue;
      }
      if (hard(c) || (e > i + 2 && drive(e)) || (c === ':' && at(input, e + 1) === '/' && at(input, e + 2) === '/')) break;
      if (isSep(c)) lastSep = e;
      if (c === ' ') {
        const name = input.slice(lastSep + 1, e);
        const dot = name.lastIndexOf('.');
        if (dot >= 0 && name.length - dot - 1 >= 1 && name.length - dot - 1 <= 4) {
          let ext = true;
          for (let k = dot + 1; k < name.length; k++) ext = ext && isAlnum(name[k]);
          if (ext) break;
        }
      }
      e++;
    }
    const runs = [];
    for (let k = i + 2; k < e;) {
      if (isSep(input[k])) {
        const s = k;
        while (k < e && isSep(input[k])) k++;
        runs.push([s, k]);
      } else k++;
    }
    if (runs.length && runs[runs.length - 1][1] === e && runs.length > 1) runs.pop();
    const keep = runs.length >= 2 ? runs.length - 2 : 0;
    out += '<path>' + input.slice(runs[keep][0], runs[keep][1]) + input.slice(runs[keep][1], e);
    bump(n, 'path');
    i = e;
  }
  return out;
}

// Crash logs: quoted text from game memory.
export function keepMemoryString(s) {
  if (s === '') return true;
  if (s.length > 160) return false;
  let marker = false, digit = false, innerCap = false;
  for (let k = 0; k < s.length; k++) {
    const c = s[k];
    if (!(isAlnum(c) || c === '_' || c === '.' || c === ':' || c === '\\' || c === '/' || c === '-')) return false;
    marker = marker || c === '.' || c === ':' || c === '\\' || c === '/' || c === '_';
    digit = digit || isDigit(c);
    innerCap = innerCap || (k > 0 && isUpper(c));
  }
  return marker || digit || innerCap;
}

export function crashMemoryStrings(input, n) {
  let out = '';
  let i = 0;
  while (i < input.length) {
    if (input[i] !== '"') {
      out += input[i++];
      continue;
    }
    let e = i + 1;
    while (e < input.length && input[e] !== '"' && input[e] !== '\n') e++;
    if (e >= input.length || input[e] !== '"') {
      out += input[i++];
      continue;
    }
    const s = input.slice(i + 1, e);
    if (keepMemoryString(s)) out += input.slice(i, e + 1);
    else {
      out += '<text>';
      bump(n, 'text');
    }
    i = e + 1;
  }
  return out;
}

export function text(input, ctx = {}, n = {}) {
  ctx = { user: '', pc: '', domain: '', roots: [], ...ctx };
  let s = roots(input, ctx, n);
  s = userFolders(s, ctx, n);
  s = profiles(s, n);
  s = steamIds(s, n);
  s = sidsAndGuids(s, n);
  s = emails(s, n);
  s = keyValueSecrets(s, n);
  s = longTokens(s, n);
  s = addresses(s, n);
  s = words(s, ctx.user, 4, '<user>', n, 'user');
  s = words(s, ctx.pc, 3, '<pc>', n, 'pc');
  if (!eqI(ctx.domain, ctx.pc)) s = words(s, ctx.domain, 3, '<pc>', n, 'pc');
  return paths(s, n);
}

export function crashLog(input, ctx = {}, n = {}) {
  return text(crashMemoryStrings(input, n), ctx, n);
}
