#!/bin/zsh
# Re-derive config/pointer_funcs.toml: pointer-referenced code the analyzer missed.
#  1. Ghidra lists candidate entries (logs/pointer_targets.txt).
#  2. Codegen without the sweep entries to learn what analysis finds on its own.
#  3. Add the rest; prune any that split a function (config/pointer_exclude.txt) until clean.
set -euo pipefail
HERE="${0:A:h:h}"
cd "$HERE"
export JAVA_HOME="${JAVA_HOME:-/opt/homebrew/opt/openjdk@21}"
/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless "$HERE/ghidra" SVR2009 -process default.xex \
  -noanalysis -readOnly -scriptPath "$HERE/ghidra_scripts" \
  -postScript ExportPointerTargets.java "$HERE/logs/pointer_targets.txt" > logs/sweep_ghidra.log 2>&1
: > config/pointer_funcs.toml
tools/regen.sh > /dev/null 2>&1 || true
tools/pointer_funcs.py logs/pointer_targets.txt > config/pointer_funcs.toml.new
mv config/pointer_funcs.toml.new config/pointer_funcs.toml
for pass in 1 2 3 4 5 6; do
  # --ignore-stamp: an unchanged config would otherwise skip codegen and leave an empty log.
  if tools/regen.sh --ignore-stamp > /dev/null 2>&1 && ! grep -q "Unresolved" logs/codegen.log; then
    echo "sweep: clean after pass $pass ($(grep -c '^0x' config/pointer_funcs.toml) entries)"
    exit 0
  fi
  tools/pointer_funcs.py logs/pointer_targets.txt --prune logs/codegen.log > config/pointer_funcs.toml.new
  mv config/pointer_funcs.toml.new config/pointer_funcs.toml
done
echo "sweep: still failing after 6 passes; see logs/codegen.log"
exit 1
