# ZX Desk for MSX1

The MSX1 port lives under `msx/`. The ZX tree above it is the source it
is ported from and stays untouched. Plan and phases: `msx/PLAN.md`.

    msx/test.sh             build the toolchain image once, assemble, run every subject, assert
    msx/test.sh --shell     a shell inside the image
    REBUILD=1 msx/test.sh   rebuild the image after a Dockerfile change

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
  the movement).

Phase 0 subjects, all asserted:

| subject | what is checked |
|---|---|
| boot | PGT = `(n & $FF) ^ (n >> 8)`, NT = `n & $FF`, CT = `$F1`, recomputed in Python, crc32 b6187c7e / b0c0df2a / 10062994; marker `ZXMSX` at $C000; frame counter running; all 11 key rows $FF |
| keys | `0` and SPACE held: rows 0 and 8 read $FE, VRAM unchanged |
| mouse | host move (+100, -60) arrives as dx -50, dy +30 |

Regional test: `MSX_MACHINE=C-BIOS_MSX1_JP msx/test.sh` for 60 Hz.
