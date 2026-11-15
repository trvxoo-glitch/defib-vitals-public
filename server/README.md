# Vitals share-pack server

The upload page and private storage for share packs: one Cloudflare Worker and one private R2 bucket. Vitals itself
sends opted-in players' `Vitals-Share.zip` here automatically (`src/Share.cpp`); the upload page is for sending one by hand.

| File | What it is |
| --- | --- |
| `worker.js` | The Worker: upload, delete, and maintainer download routes |
| `page.js` | The upload page. The zip is read in the browser first, so the player sees every file and can untick any |
| `scrub.js` | Second scrub pass, a line-by-line port of `include/Vitals/Scrub.h` |
| `zip.js` | Reads and writes the uncompressed zips Vitals makes |
| `wrangler.toml` | Cloudflare settings: bucket, rate-limit store, logs off |
| `test/` | `node --test test/*.test.mjs` (CI runs it): the shared scrub cases, a C++/JS cross check, the routes |

## What the server does with a pack

1. Refuses anything over 10 MB, more than 10 uploads an hour from one place, any file name Vitals would never write,
   compressed or encrypted zips, binary files, and zips without Vitals' `manifest.json`.
2. Scrubs every file again with the same rules as Vitals. Anything it finds means Vitals' own pass missed it: the
   count is stored with the pack (`second_pass_hits`) so it can be fixed in the next Vitals release.
3. Stores the scrubbed pack as `packs/VTL-XXXX-XXXX.zip` with only the Vitals version, game version, runtime, the
   random install id from the manifest and the upload date. No IP address, account or cookie.
4. Returns the code. The code deletes the pack (`DELETE /v1/pack/<code>`, or the box on the page).

Packs are deleted by the bucket's lifecycle rule after 90 days. Maintainers read them with `tools/share_inbox.py` and
the `READ_KEY`; every download is recorded under `audit/`.

The rate limit stores `SHA-256(SALT_KEY + today's date + IP address)` with a one-hour expiry. The address itself is never
stored, and the hash cannot be matched across days.

## Deploying (once)

You need a free Cloudflare account and Node.js. Run these in this folder.

1. `npx wrangler login`
2. `npx wrangler r2 bucket create vitals-share-packs`
3. `npx wrangler kv namespace create RATE`, then put the printed id into `wrangler.toml` (`REPLACE_WITH_KV_NAMESPACE_ID`).
4. Delete packs after 90 days: `npx wrangler r2 bucket lifecycle add vitals-share-packs expire-packs packs/ --expire-days 90`
   (and `audit/` after a year: `npx wrangler r2 bucket lifecycle add vitals-share-packs expire-audit audit/ --expire-days 365`).
5. Two secrets, each a long random string (for example from a password manager). Keep `READ_KEY`; it is how you download packs.
   `npx wrangler secret put READ_KEY` and `npx wrangler secret put SALT_KEY`
6. `npx wrangler deploy`. It prints the address (`https://vitals-share.<your-subdomain>.workers.dev`).

Then put the host name (`vitals-share.<your-subdomain>.workers.dev`) in `Share::kUploadHost` in
`include/Vitals/Share.h`, and release Vitals. Until then Vitals only writes the zip. Check the commands against Cloudflare's current documentation when you run them:
Wrangler's command names change between versions.

## Reading packs

```
set VITALS_SHARE_URL=https://vitals-share.<your-subdomain>.workers.dev
set VITALS_READ_KEY=<the READ_KEY>
python tools/share_inbox.py pull --out inbox
python tools/share_inbox.py summary inbox
```

Raw packs stay private: never commit, post or forward them. What goes public (knowledge rules, statistics) is counts
across five or more install ids, confirmed in game by a person first (docs/CONTRIBUTING.md).
