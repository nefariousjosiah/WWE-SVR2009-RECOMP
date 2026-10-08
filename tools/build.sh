#!/bin/zsh
# Configure + build the native game. PRESET defaults to Apple Silicon release;
# on Windows use win-amd64-release (from a VS 2022 developer prompt with clang-cl).
set -euo pipefail
HERE="${0:A:h:h}"
cd "$HERE"
PRESET="${PRESET:-mac-arm64-release}"
cmake --preset "$PRESET" -DREXSDK="$HERE/sdk"
cmake --build "out/build/$PRESET" "$@"
