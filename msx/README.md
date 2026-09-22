# ZX Desk for MSX1

The MSX1 port lives under `msx/`. The ZX tree above it is the source it
is ported from and stays untouched.

    msx/test.sh             build the toolchain image once, assemble, run every subject, assert
    msx/test.sh --shell     a shell inside the image
    REBUILD=1 msx/test.sh   rebuild the image after a Dockerfile change
    msx/run.sh              build and run it in a visible openMSX: the host's
                            (dnf install openmsx cbios) or the image on the
                            host display with the GPU passed in

Everything runs in `msx/docker`: Debian trixie with openMSX 20.0,
C-BIOS 0.29a, pasmo 0.5.5 built from source, Xvfb and xdotool. Nothing
needs to be installed on the host but Docker.

## Toolchain notes, measured

- **pasmo 0.5.3 (Debian) does not assemble the ZX source**: it has no
  `IFDEF` and fails on line 88 of zxdesk.asm. 0.5.5 from
  pasmo.speccy.org assembles it (22,961 byte TEST build), so the image
  builds 0.5.5 from the tarball, checked against its sha256.
- **openMSX 19.1 is packaged nowhere current**; trixie ships 20.0,
  bookworm 18.0. 20.0 it is.
- The Debian `cbios` package links only some of its ROMs into openMSX's
  systemroms; `C-BIOS_MSX1_EU` (the 50 Hz machine) is missing its main
  ROM until the Dockerfile links the rest.
- openMSX's SDL init asserts under `SDL_VIDEODRIVER=dummy`, so it runs
  under Xvfb. It also segfaults when `getpwuid()` finds no entry for the
  calling uid, which is why `test.sh --shell` mounts `/etc/passwd` read-only. The test run itself streams the tree in and `msx/build` out with tar, so it also works from a container that only has the docker socket.
- `after boot` in openMSX fires at power-on, not when C-BIOS has finished
  its logo (about 3.1 s emulated). The driver script waits for the ROM's
  marker in work RAM instead of a fixed delay.
- Tcl `puts` inside openMSX goes to its own console, not stdout, and an
  error inside an `after` callback is swallowed and the emulator runs
  forever. The driver writes a log file and has a realtime guard.
- With the throttle off the renderer skips frames and a screenshot is
  black. The driver switches the throttle on for ten frames before the
  capture; inside a proc that must be `set ::throttle`, a bare `set`
  makes a local variable.
- The VDP address latch is reset by a status-register read, and the
  BIOS interrupt handler does one every frame. An interrupt between the
  two control writes of `SetWrt` sends the data elsewhere: on the 60 Hz
  machine that hit desktop row 6 on every boot (name table crc32
  d4ebe497 instead of b905ed57). `SetWrt` holds DI across the pair.
- C-BIOS's CHGMOD 2 leaves the pattern table clear where the real BIOS
  loads its font, so the ROM copies glyphs 32-127 from the BIOS's
  CGTABL pointer itself and the harness asserts that copy against the
  BIOS ROM it read back.

## The harness

`msx/msxtest.py` (the model is `zxtest.py` in the ZX tree) assembles
`msx/src/msxdesk.asm` into a padded 32K `page12` ROM, boots it in
`C-BIOS_MSX1_EU` with the throttle off, runs a step list through
`msx/harness/run.tcl`, and reads VRAM (16K) and work RAM ($C000-$FFFF)
back through `debug read_block`. Checksums are the assertion; the
screenshot in `msx/build/out/<subject>/shot.png` is for people.

Input injection:

- keys go through `keymatrixdown <row> <mask>`, the PPI matrix;
- the mouse is `plug joyporta mouse` plus `xdotool` moving the Xvfb
  pointer, which openMSX turns into the joystick-port strobe protocol.
  Measured on openMSX 20.0: a host move of n pixels arrives as -n/2
  (openMSX halves host motion; an MSX mouse reports the negative of
  the movement), so the ROM subtracts the deltas. openMSX resets the
  strobe phase after 1.5 ms without a strobe, which is what makes a
  once-a-frame read see fresh deltas. The buttons ride on bits 4 and 5
  of the same byte, and ApplyDelta clobbers E: read them first.

Steps are timed on the ROM's own frame counter (`Frames` at $C008),
polled every emulated frame. That was meant to make a held key's frame
count exact; measured, injection still lands a frame either way (the
60 Hz machine saw 21 frames for a 20 frame hold once the frame had
more work in it). So the ROM counts the frames it saw each key held
(`HeldX`, `HeldY`, `KbdHeld`) and the harness recomputes the ramp and
the repeat count for that number: the arithmetic is asserted exactly,
the hold length is read.

Subjects, all asserted (phase 1):

| subject | what is checked |
|---|---|
| boot | VDP R1 $E2 (16x16 sprites; CHGMOD leaves 8x8 and the arrow lost its tail), R7 white border; name table = the Python oracle (bar text, rule row, lattice, status band), crc32 b905ed57; the BIOS font (crc32 897a8dfc on C-BIOS) and the two tiles in all three thirds of the PGT; sprite shape at $3800 and attributes (89,120,0,1) + end marker; H.TIMI: interrupts == frames, 0 dropped; pointer idle, no events |
| mouse | host (+20, -30) → pointer (130, 75), sprite follows, exactly 2 EV_PTRMOVE and no button events |
| cursor | RIGHT held 20 frames, DOWN 10 → (158, 103), the ramp recomputed in Python; 30 moves, no key events |
| keys | A, SHIFT+1, SPACE → 3 EV_KEY, one EV_BTNDOWN, one EV_BTNUP, status row echoes `A! ` |
| repeat | C held 30 frames → 5 EV_KEY (1 + 1 at 20 + 3 more every 3), `CCCCC` on the status row |


Phase 2, the portable layers, run as a TEST=1 build (`msxtest.rom`)
whose subjects execute at boot and leave records in RAM, the way the
ZX TEST build did, with openMSX in place of the Python Z80:

| subject | what is checked |
|---|---|
| heap | three blocks split to size at the expected addresses; a scribbled payload freed returns exactly its bytes; free by owner coalesces into one piece (stats 6128 / 6804 / 7112 of 8268) |
| calendar | all 1,200 months 1980-2079 agree with Python's calendar on weekday of the 1st and length; August 2026 grid rows; a step back from the 1st lands on 31 July; ENTER sets today; midnight on the 31st rolls the month |
| storage | 64 bytes written, closed, reopened and read back identical through the RAM backend; four files listed; the fifth open fails with STERR_FULL; delete removes the entry |
| app model | AppAt finds the calendar's descriptor; AppSave then AppLoad through a heap state block restores (46, 7, 30) |
| hit test | bar, desktop, status band, off the edge and an open menu drop give (4, 5, 0, 0, 6) |

In the normal build a SPACE press at (130, 75) records CTL_DESKTOP.

Both C-BIOS_MSX1_EU (50 Hz) and C-BIOS_MSX1_JP (60 Hz) pass.

## Memory budget

The build prints it and fails past the line:

    msxdesk.rom: code $4000-$4E70, 3696 bytes, 29072 free; RAM $C000-$C81C, 2076 bytes, heap 10212 to $F000, 896 reserve

Work RAM is handed out by the `var` macro in msxdesk.asm from $C000 up;
the heap takes everything from `RamEnd` to `HEAPEND` ($F000), and
$F000-$F380 is the stack's, under the BIOS work area. The RAM storage
backend's four 256 byte files and directory are 1,088 of the 2,076,
the name table shadow 768.

The ZX storage layer dispatched through the operands of JP
instructions it patched at run time; a ROM cannot be patched, so the
six vectors are a table in RAM and each entry point jumps through IX
(HL carries the name or the buffer). The hit test table is copied to
RAM for the same reason.

## Screen model

Everything paints into a shadow of the name table in RAM (`ShadowNT`,
768 bytes) and marks the row dirty; `NtFlush` copies the dirty rows to
VRAM right after the interrupt, 32 OUTs a row at 41 cycles apart. A
save-under is an LDIR out of the shadow and a read-modify-write is a
byte, which is what the ZX code did to its screen and what VRAM behind
a port cannot do.

Screen 2's colour table is one colour byte per pattern row per third,
so the font is loaded twice: at $20-$7F black on white and at $A0-$FF
white on black, the same bytes with the colours swapped, in all three
thirds. Inverted text is a code with bit 7 set, anywhere on the
screen; the status band is inverted spaces. Codes $80-$9F are the
desktop tiles. Sprites are 16x16 (VDP R1 $E2; CHGMOD leaves 8x8).

The stack is set to $F380 in Init: the BIOS called the cartridge on
its own stack, and C-BIOS and a real BIOS need not agree where that
was. VDP register writes go through `WrtVdp`, under DI like `SetWrt`.
