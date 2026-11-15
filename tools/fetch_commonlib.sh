#!/usr/bin/env bash
# Same as fetch_commonlib.ps1, for Linux/macOS (cloud workspace, analysis only).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
sha="$(tr -d '[:space:]' < "$root/lib/COMMONLIBF4_COMMIT")"
dst="$root/lib/commonlibf4"
[ -d "$dst/.git" ] || git clone https://github.com/Dear-Modding-FO4/commonlibf4 "$dst"
git -C "$dst" fetch origin "$sha"
git -C "$dst" checkout --detach "$sha"
git -C "$dst" submodule update --init --recursive
echo "CommonLibF4 pinned at $sha"
