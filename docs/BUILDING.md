# Building Vitals

## Windows (the official way)

Needs Visual Studio 2022 (Desktop development with C++), XMake 3.0.0 or newer (`xmake.lua` asks for 3.0.0; CI and the Linux cross-build use 3.1.2), Git and Python 3.9+.

```bat
powershell -File tools\fetch_commonlib.ps1
xmake config --mode=release --yes
xmake build --yes
python tools\make_package.py build --dll build\windows\x64\release\Vitals.dll --out package
python -m unittest discover -s tests
```
`package\` is then the release folder: zip its contents for Vortex/MO2.

`fetch_commonlib.ps1` clones Dear-Modding-FO4/commonlibf4 at the commit in `lib/COMMONLIBF4_COMMIT` into
`lib/commonlibf4` (ignored by git).

## CI (`.github/workflows/build.yml`)

* `vitals` job on `windows-2022` (pinned: newer images moved to another toolset): fetch library, build, run
  `make_package.py` (fails on a broken FOMOD), run `f4se_dll_scan.py` on the DLL and fail if it has no
  `F4SEPlugin_Version` export or would not load on 1.11.240. Uploads `package/` and `symbols/` as an artifact.
* `analyzer` job: the Python unit tests.
* `release` job: on a `v*` tag, or a manual run with `release_version` set (the cloud session cannot push tags).
  Zips the package as `Vitals-<ver>-AI-Assisted.zip` (the main file) and the guides and changelog that are not in it as
  `Vitals-<ver>-Extra-Docs.zip` (an optional file on Nexus; `make_package.py` builds both), plus a symbols zip. Takes notes from `CHANGELOG.md`, adds the AI Assisted line.

## Linux cross-build (quick compile check)

Used from cloud sessions to check that the code compiles before pushing. CI stays the official build.
`tools/cloud_cross_setup.sh` installs clang 19, xwin (Windows SDK and CRT) and xmake 3.1.2. Then copy hxef's
`commonlibf4-linux-cross` into `contrib/linux-cross` of a scratch copy of the repo, add
`includes("contrib/linux-cross/xmake.lua")` at the top of that copy's `xmake.lua`, and:

```sh
XMAKE_ROOT=y XWIN_ROOT=$T/xwin PATH=$T/shim:$PATH xmake f -y -p windows -a x64 -m releasedbg --toolchain=xwin-clang-cl
XMAKE_ROOT=y XWIN_ROOT=$T/xwin PATH=$T/shim:$PATH xmake build -y
```
Warnings from inside the library (`VariantID.h` deprecated copy) are expected. Do not ship a cross-built DLL.
