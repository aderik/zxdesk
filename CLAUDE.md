# MSX Desk

ZX Desk (Damian Cooper, MIT) ported to the MSX1. The ZX tree (`src/`,
`zxtest.py`, `build.sh`) is the source it is ported from and stays
untouched; all port work is under `msx/`. Read `msx/README.md` first:
it holds the toolchain findings, the harness, the screen model and the
measured numbers.

## How work is verified

- `msx/test.sh` assembles, boots the ROM headlessly in openMSX (Docker)
  and asserts on memory dumps: checksums and values, never screenshots.
  `msx/test-all.sh` runs every machine configuration; the real ones need
  the BIOS dumps in `msx/roms/` (gitignored) and are skipped without.
- Assembly that has not been run and asserted does not count as done.
  Every piece of work gets a subject in `msx/msxtest.py` (or, for pure
  routines, in the TEST build `msx/src/test.inc`) that asserts on a
  checksum or a memory value.
- Every claim about timing or correctness is a measurement, not an
  estimate: breakpoints and `machine_info time` for cycles, the
  `Dropped` counter for frames, crc32 of the name table for the screen.
  Put the numbers in the commit message.
- Run the 60 Hz machine (`MSX_MACHINE=C-BIOS_MSX1_JP`) as well as the
  50 Hz one before committing; it has caught races the other never
  showed. With ROMs present, all five configurations.
- The memory budget is printed by every build (ROM bytes, RAM bytes,
  heap). RAM is handed out by the `var` macro in `msx/src/msxdesk.asm`;
  the heap ends at HIMEM minus $380 at run time. Say in the commit how
  much RAM a change adds.

## Conventions

- Work in cells, not pixels: the desktop is a tile grid, windows are
  buffers of name table codes, painting goes into the shadow name table
  and `NtFlush` copies dirty rows to VRAM. Keep 29 cycles between VRAM
  OUTs during the display.
- VDP address writes go through `SetWrt` and register writes through
  `WrtVdp` (both under DI); BIOS hooks must be inter-slot calls.
- One commit per verified piece of work, with the acceptance numbers.
