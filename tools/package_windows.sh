#!/bin/zsh
# Zip the project for the Windows PC (sources, config, tools; no builds, game data or SDK).
# Windows rebuilds everything with tools\windows\setup.ps1. Copy the game .7z separately.
#   tools/package_windows.sh   -> ~/Desktop/WWE2008-windows.zip
set -euo pipefail
HERE="${0:A:h:h}"
OUT="${1:-$HOME/Desktop/WWE2008-windows.zip}"
cd "$HERE/.."
rm -f "$OUT"
zip -qr "$OUT" "${HERE:t}" \
  -x "${HERE:t}/assets/*" "${HERE:t}/iso/*" "${HERE:t}/out/*" "${HERE:t}/sdk/*" \
     "${HERE:t}/logs/*" "${HERE:t}/ghidra/*" "${HERE:t}/userdata/*" "${HERE:t}/third_party/*" \
     "${HERE:t}/generated/default/*" "*/.DS_Store"
ls -lh "$OUT"
