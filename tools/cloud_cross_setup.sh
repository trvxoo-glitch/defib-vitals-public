#!/usr/bin/env bash
# Build Vitals for Windows from a Linux machine (scratch check only; CI on windows-2022 stays the official build).
# Needs network access to: crates.io, github.com, aka.ms, download.visualstudio.microsoft.com
set -euo pipefail
T=${T:-/tmp/tc}; mkdir -p "$T"
apt-get install -y clang-19 lld-19 llvm-19 cargo git build-essential
cargo install xwin
xwin --accept-license --cache-dir "$T/xwin-cache" splat --output "$T/xwin"
git clone --depth 1 --branch v3.1.2 --recursive https://github.com/xmake-io/xmake "$T/xmake-src"
(cd "$T/xmake-src" && ./configure && make -j4 && make install)
mkdir -p "$T/shim"; for t in clang lld-link llvm-lib llvm-rc llvm-ar; do
  ln -sf "$(command -v ${t}-19 || command -v $t)" "$T/shim/$t"; done
ln -sf "$(command -v clang-19)" "$T/shim/clang-cl"
cat <<MSG
Next: fetch the pinned library (tools/fetch_commonlib.sh), copy hxef's commonlibf4-linux-cross into contrib/linux-cross,
add 'if is_host("linux") then includes("contrib/linux-cross") end' to xmake.lua, then:
  XMAKE_ROOT=y XWIN_ROOT=$T/xwin PATH=$T/shim:\$PATH xmake f -y -p windows -a x64 -m releasedbg --toolchain=xwin-clang-cl && xmake build -y
MSG
