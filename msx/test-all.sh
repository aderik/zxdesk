#!/bin/bash
# MSX Desk: the harness on every machine configuration there is. The
# C-BIOS machines always; the real ones when msx/roms holds the BIOS
# dumps (see msx/README.md, "Real BIOS ROMs"). Stops at the first red.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
run() { echo "=== ${1}${2:+ + $2}"; MSX_MACHINE="$1" MSX_EXT="${2:-}" "$ROOT/msx/test.sh"; }
run C-BIOS_MSX1_EU
run C-BIOS_MSX1_JP
if [[ -e "$ROOT/msx/roms/MSX.ROM" ]]; then
  run Roms_MSX1
  [[ -e "$ROOT/msx/roms/DISK.ROM" ]] && run Roms_MSX1 Roms_Disk
  [[ -e "$ROOT/msx/roms/MSX2.ROM" ]] && run Roms_MSX2
else
  echo "no msx/roms/MSX.ROM: the real machines were skipped"
fi
