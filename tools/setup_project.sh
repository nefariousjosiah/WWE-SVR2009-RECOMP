#!/bin/zsh
# One-time: scaffold the ReXGlue project around assets/default.xex.
# Writes CMakeLists.txt, CMakePresets.json, svr2009_manifest.toml, src/, generated/rexglue.cmake.
set -euo pipefail
HERE="${0:A:h:h}"
cd "$HERE"
[[ -f assets/default.xex ]] || { echo "error: assets/default.xex missing; run tools/import_disc.sh first"; exit 1; }
if [[ -f svr2009_manifest.toml ]]; then
  echo "already initialised (svr2009_manifest.toml exists)"
  exit 0
fi
sdk/bin/rexglue init --project-name svr2009 --xex-path assets/default.xex --game-root assets --project-root .
