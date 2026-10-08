#!/bin/zsh
# Recompile default.xex -> generated/ C++. Extra args go to `rexglue codegen`
# (e.g. --ignore-stamp). Unresolved calls are expected early on; add -f to emit anyway:
#   tools/regen.sh            # strict
#   FORCE=1 tools/regen.sh    # generate despite validation errors
set -euo pipefail
HERE="${0:A:h:h}"
cd "$HERE"
mkdir -p logs
rm -f logs/codegen.log  # --log-file appends
sdk/bin/rexglue ${FORCE:+--force} --log-file logs/codegen.log codegen svr2009_manifest.toml "$@"
