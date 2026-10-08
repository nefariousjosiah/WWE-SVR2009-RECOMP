#!/bin/zsh
# Unpack the SvR 2008 (Xbox 360) disc into iso/ and assets/.
#   tools/import_disc.sh [path/to/archive.7z | path/to/image.iso]
# Default: the first non-empty "(USA, Europe)" 7z in ~/Downloads.
set -euo pipefail
HERE="${0:A:h:h}"
SRC="${1:-}"
if [[ -z "$SRC" ]]; then
  for f in "$HOME"/Downloads/WWE\ SmackDown\ vs.\ Raw\ 2008\ \(USA,\ Europe\)*.7z(N); do
    [[ -s "$f" ]] && { SRC="$f"; break; }
  done
  [[ -n "$SRC" ]] || { echo "error: no finished SvR 2008 .7z in ~/Downloads yet"; exit 1; }
fi

if [[ "$SRC" == *.7z ]]; then
  [[ -s "$SRC" ]] || { echo "error: $SRC is missing or empty (download still in progress?)"; exit 1; }
  mkdir -p "$HERE/iso"
  7zz x -y -o"$HERE/iso" "$SRC"
  ISO="$(ls "$HERE"/iso/*.iso | head -1)"
else
  ISO="$SRC"
fi

echo "image: $ISO"
python3 "$HERE/tools/xiso_extract.py" "$ISO" "$HERE/assets"
ls -la "$HERE/assets/default.xex"
shasum -a 1 "$HERE/assets/default.xex" | tee "$HERE/iso/default.xex.sha1"
