# Fetch CommonLibF4 (Dear-Modding) at the exact commit in lib/COMMONLIBF4_COMMIT.
# The maintainers have force-pushed before, so we never build from a moving branch.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$sha = (Get-Content "$root/lib/COMMONLIBF4_COMMIT").Trim()
$dst = "$root/lib/commonlibf4"
if (-not (Test-Path "$dst/.git")) {
    git clone https://github.com/Dear-Modding-FO4/commonlibf4 $dst
}
git -C $dst fetch origin $sha
git -C $dst checkout --detach $sha
git -C $dst submodule update --init --recursive
Write-Host "CommonLibF4 pinned at $sha"
