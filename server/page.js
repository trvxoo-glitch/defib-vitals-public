// The upload page. Plain HTML and JavaScript, no outside scripts, fonts or trackers. The zip is read in the browser
// first, so the player sees every file and can untick any before anything is sent.
export default `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Vitals share pack</title>
<style>
  :root { --bg: #f6f8fc; --fg: #101828; --muted: #475467; --card: #ffffff; --line: #d0d7e2; --accent: #2f6fe0; --ok: #127a3a; --bad: #b42318; }
  @media (prefers-color-scheme: dark) { :root { --bg: #000a1f; --fg: #e8eefc; --muted: #9db4d9; --card: #071633; --line: #1f3460; --accent: #4f8cff; --ok: #5ad08a; --bad: #ff8a80; } }
  * { box-sizing: border-box; }
  body { margin: 0; background: var(--bg); color: var(--fg); font: 16px/1.5 Helvetica, Arial, sans-serif; }
  main { max-width: 760px; margin: 0 auto; padding: 32px 16px 64px; }
  h1 { font-size: 28px; margin: 0 0 4px; } h2 { font-size: 18px; margin: 28px 0 8px; }
  p { color: var(--muted); margin: 8px 0; }
  .card { background: var(--card); border: 1px solid var(--line); border-radius: 10px; padding: 16px; margin-top: 16px; }
  #drop { border: 2px dashed var(--line); border-radius: 10px; padding: 32px 16px; text-align: center; cursor: pointer; }
  #drop.over { border-color: var(--accent); }
  ul { list-style: none; padding: 0; margin: 8px 0; } li { padding: 4px 0; border-bottom: 1px solid var(--line); display: flex; gap: 8px; align-items: center; }
  li span.size { margin-left: auto; color: var(--muted); font-size: 14px; }
  button { background: var(--accent); color: #fff; border: 0; border-radius: 8px; padding: 10px 18px; font-size: 16px; cursor: pointer; }
  button:disabled { opacity: .5; cursor: default; }
  input[type=text] { padding: 9px; border-radius: 8px; border: 1px solid var(--line); background: var(--bg); color: var(--fg); font-size: 16px; width: 14em; max-width: 100%; }
  .code { font: 700 26px/1.2 ui-monospace, Consolas, monospace; letter-spacing: 1px; }
  .ok { color: var(--ok); } .bad { color: var(--bad); }
  pre { white-space: pre-wrap; font-size: 13px; color: var(--muted); }
  a { color: var(--accent); }
</style>
</head>
<body>
<main>
  <h1>Vitals share pack</h1>
  <p>Send the Vitals-Share.zip that Vitals made for you, so we can see which F4SE plugins fail and why. Thank you.</p>
  <p>It is in <b>Documents\\My Games\\Fallout4\\F4SE\\Vitals\\Vitals-Share.zip</b>. Vitals already removed your user name, PC name, emails, IP addresses, keys and private folder paths; this server checks every file again. Nothing about you is stored: no IP address, no account, no cookie. Packs are deleted after 90 days, or now with your code. <a href="https://github.com/trvxoo-glitch/defib-vitals-public/blob/main/docs/PRIVACY_AND_SAFETY.md">Privacy details</a>.</p>

  <div class="card">
    <div id="drop" tabindex="0">Drop Vitals-Share.zip here, or click to choose it<input id="file" type="file" accept=".zip" hidden></div>
    <div id="review" hidden>
      <h2>What will be sent</h2>
      <p>Untick anything you would rather keep. Click a name to read it first.</p>
      <ul id="list"></ul>
      <pre id="preview" hidden></pre>
      <button id="send">Send</button>
    </div>
    <div id="result"></div>
  </div>

  <div class="card">
    <h2 style="margin-top:0">Delete a pack</h2>
    <p>Enter the code you got when you sent it.</p>
    <input id="delcode" type="text" placeholder="VTL-XXXX-XXXX" autocomplete="off"> <button id="del">Delete</button>
    <div id="delresult"></div>
  </div>
</main>
<script>
(function () {
  var TABLE = (function () { var t = []; for (var n = 0; n < 256; n++) { var c = n; for (var k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
  function crc32(b) { var c = 0xffffffff; for (var i = 0; i < b.length; i++) c = TABLE[(c ^ b[i]) & 0xff] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }
  function readZip(b) {
    var v = new DataView(b.buffer), e = -1;
    for (var p = b.length - 22; p >= 0 && p >= b.length - 65557; p--) if (v.getUint32(p, true) === 0x06054b50) { e = p; break; }
    if (e < 0) throw new Error('This is not a zip file.');
    var n = v.getUint16(e + 10, true), q = v.getUint32(e + 16, true), out = [], dec = new TextDecoder();
    for (var k = 0; k < n; k++) {
      if (v.getUint32(q, true) !== 0x02014b50) throw new Error('The zip is damaged.');
      var method = v.getUint16(q + 10, true), size = v.getUint32(q + 24, true), nl = v.getUint16(q + 28, true), xl = v.getUint16(q + 30, true), cl = v.getUint16(q + 32, true), off = v.getUint32(q + 42, true);
      var name = dec.decode(b.subarray(q + 46, q + 46 + nl));
      if (method !== 0) throw new Error('Please send the zip exactly as Vitals made it (it was re-packed).');
      var start = off + 30 + v.getUint16(off + 26, true) + v.getUint16(off + 28, true);
      out.push({ name: name, data: b.subarray(start, start + size) });
      q += 46 + nl + xl + cl;
    }
    return out;
  }
  function writeZip(entries) {
    var enc = new TextEncoder(), parts = [], dir = [], off = 0;
    entries.forEach(function (x) {
      var n = enc.encode(x.name), d = x.data, crc = crc32(d), h = new DataView(new ArrayBuffer(30)), c = new DataView(new ArrayBuffer(46));
      h.setUint32(0, 0x04034b50, true); h.setUint16(4, 20, true); h.setUint16(6, 0x0800, true); h.setUint16(12, 0x5b61, true);
      h.setUint32(14, crc, true); h.setUint32(18, d.length, true); h.setUint32(22, d.length, true); h.setUint16(26, n.length, true);
      c.setUint32(0, 0x02014b50, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true); c.setUint16(8, 0x0800, true); c.setUint16(14, 0x5b61, true);
      c.setUint32(16, crc, true); c.setUint32(20, d.length, true); c.setUint32(24, d.length, true); c.setUint16(28, n.length, true); c.setUint32(42, off, true);
      parts.push(new Uint8Array(h.buffer), n, d); dir.push(new Uint8Array(c.buffer), n); off += 30 + n.length + d.length;
    });
    var ds = 0; dir.forEach(function (x) { ds += x.length; });
    var e = new DataView(new ArrayBuffer(22));
    e.setUint32(0, 0x06054b50, true); e.setUint16(8, entries.length, true); e.setUint16(10, entries.length, true); e.setUint32(12, ds, true); e.setUint32(16, off, true);
    var all = parts.concat(dir, [new Uint8Array(e.buffer)]), len = 0; all.forEach(function (x) { len += x.length; });
    var out = new Uint8Array(len), p = 0; all.forEach(function (x) { out.set(x, p); p += x.length; });
    return out;
  }
  function kb(n) { return n < 1024 ? n + ' B' : (n / 1024).toFixed(n < 10240 ? 1 : 0) + ' KB'; }
  var drop = document.getElementById('drop'), input = document.getElementById('file'), list = document.getElementById('list');
  var review = document.getElementById('review'), result = document.getElementById('result'), preview = document.getElementById('preview');
  var entries = [];
  function show(msg, cls) { result.className = cls || ''; result.textContent = msg; }
  function load(file) {
    show('');
    file.arrayBuffer().then(function (buf) {
      try { entries = readZip(new Uint8Array(buf)); } catch (e) { review.hidden = true; show(e.message, 'bad'); return; }
      if (!entries.some(function (x) { return x.name === 'manifest.json'; })) { review.hidden = true; show('This zip was not made by Vitals (no manifest.json).', 'bad'); return; }
      list.textContent = '';
      entries.forEach(function (x, i) {
        var li = document.createElement('li'), cb = document.createElement('input'), a = document.createElement('a'), s = document.createElement('span');
        cb.type = 'checkbox'; cb.checked = true; cb.dataset.i = i; cb.disabled = x.name === 'manifest.json';
        a.href = '#'; a.textContent = x.name; a.onclick = function (ev) { ev.preventDefault(); preview.hidden = false; preview.textContent = new TextDecoder().decode(x.data.subarray(0, 20000)) + (x.data.length > 20000 ? '\\n[...]' : ''); };
        s.className = 'size'; s.textContent = kb(x.data.length);
        li.appendChild(cb); li.appendChild(a); li.appendChild(s); list.appendChild(li);
      });
      review.hidden = false;
    });
  }
  drop.onclick = function () { input.click(); };
  drop.onkeydown = function (e) { if (e.key === 'Enter' || e.key === ' ') input.click(); };
  input.onchange = function () { if (input.files[0]) load(input.files[0]); };
  drop.ondragover = function (e) { e.preventDefault(); drop.classList.add('over'); };
  drop.ondragleave = function () { drop.classList.remove('over'); };
  drop.ondrop = function (e) { e.preventDefault(); drop.classList.remove('over'); if (e.dataTransfer.files[0]) load(e.dataTransfer.files[0]); };
  document.getElementById('send').onclick = function () {
    var btn = this, keep = [];
    list.querySelectorAll('input[type=checkbox]').forEach(function (cb) { if (cb.checked) keep.push(entries[+cb.dataset.i]); });
    btn.disabled = true; show('Sending...');
    fetch('/v1/pack', { method: 'POST', headers: { 'content-type': 'application/zip' }, body: writeZip(keep) })
      .then(function (r) { return r.json(); })
      .then(function (j) {
        btn.disabled = false;
        if (j.error) { show(j.error, 'bad'); return; }
        review.hidden = true;
        result.className = 'ok';
        result.innerHTML = '<p>Sent. Your code:</p><p class="code"></p><p>Quote it in your bug report or post so we can find your pack. Keep it to delete the pack later. It is deleted after 90 days anyway.</p>';
        result.querySelector('.code').textContent = j.code;
      })
      .catch(function () { btn.disabled = false; show('Could not reach the server. Nothing was sent.', 'bad'); });
  };
  document.getElementById('del').onclick = function () {
    var code = document.getElementById('delcode').value.trim().toUpperCase(), out = document.getElementById('delresult');
    fetch('/v1/pack/' + encodeURIComponent(code), { method: 'DELETE' }).then(function (r) { return r.json(); }).then(function (j) {
      out.className = j.error ? 'bad' : 'ok'; out.textContent = j.error || ('Deleted ' + j.deleted + '.');
    }).catch(function () { out.className = 'bad'; out.textContent = 'Could not reach the server.'; });
  };
})();
</script>
</body>
</html>
`;
