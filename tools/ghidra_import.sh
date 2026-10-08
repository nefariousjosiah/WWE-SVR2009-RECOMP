#!/bin/zsh
# One-time: import + auto-analyse default.xex into ghidra/SVR2009.gpr (XEXLoaderWV, PowerPC 64/32 BE Xenon).
# Takes a while on a full game; open the project afterwards with `ghidraRun`.
set -euo pipefail
HERE="${0:A:h:h}"
export JAVA_HOME="${JAVA_HOME:-/opt/homebrew/opt/openjdk@21}"
mkdir -p "$HERE/ghidra" "$HERE/logs"
/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless "$HERE/ghidra" SVR2009 \
  -import "$HERE/assets/default.xex" -loader XEXLoaderWVLoader \
  -scriptPath "$HERE/ghidra_scripts" 2>&1 | tee "$HERE/logs/ghidra_import.log"
