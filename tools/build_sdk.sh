#!/bin/zsh
# Rebuild/install the vendored ReXGlue SDK into ./sdk (only needed after changing third_party/rexglue-sdk).
# macOS deps: brew install cmake ninja vulkan-loader molten-vk
set -euo pipefail
HERE="${0:A:h:h}"
SDK_SRC="$HERE/third_party/rexglue-sdk"
PRESET="${PRESET:-mac-arm64}"
cmake -S "$SDK_SRC" --preset "$PRESET" -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DCMAKE_INSTALL_PREFIX="$HERE/sdk"
cmake --build "$SDK_SRC/out/build/$PRESET" --config Release --target install --parallel
