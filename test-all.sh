#!/bin/bash
# MSX Desk: the harness on every machine configuration there is. The
# C-BIOS machines always; the real ones when roms holds the BIOS
# dumps (see README.md, "Real BIOS ROMs"). Stops at the first red.
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
run() { echo "=== ${1}${2:+ + $2}"; MSX_MACHINE="$1" MSX_EXT="${2:-}" "$ROOT/test.sh"; }
run C-BIOS_MSX1_EU
run C-BIOS_MSX1_JP
if [[ -e "$ROOT/roms/MSX.ROM" ]]; then
  run Roms_MSX1
  [[ -e "$ROOT/roms/DISK.ROM" ]] && run Roms_MSX1 Roms_Disk
  [[ -e "$ROOT/roms/MSX2.ROM" ]] && run Roms_MSX2
  # the bank backend's mapper source on an MSX1 with a 512K mapper
  # cartridge (harness/extensions/Mapper512.xml): the bank subjects only
  echo "=== Roms_MSX1 + Mapper512, --bank"
  MSX_MACHINE=Roms_MSX1 MSX_EXT=Mapper512 "$ROOT/test.sh" --bank
else
  echo "no roms/MSX.ROM: the real machines were skipped"
fi
