# MSX Desk

ZX Desk, Damian Cooper's desktop for the Spectrum, ported to the MSX1
and renamed for the machine it runs on. The port lives under `msx/`. The ZX tree above it is the source it
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
| keys | A, SHIFT+1, SPACE → 3 EV_KEY, no button events, status row echoes `A! ` |
| repeat | C held 30 frames → 5 EV_KEY (1 + 1 at 20 + 3 more every 3), `CCCCC` on the status row |


Phase 2, the portable layers, run as a TEST=1 build (`msxtest.rom`)
whose subjects execute at boot and leave records in RAM, the way the
ZX TEST build did, with openMSX in place of the Python Z80:

| subject | what is checked |
|---|---|
| heap | three blocks split to size at the expected addresses; a scribbled payload freed returns exactly its bytes; free by owner coalesces into one piece (expected addresses and statistics are calculated from the runtime heap bounds) |
| calendar | all 1,200 months 1980-2079 agree with Python's calendar on weekday of the 1st and length; August 2026 grid rows; a step back from the 1st lands on 31 July; ENTER sets today; midnight on the 31st rolls the month |
| storage | 64 bytes written, closed, reopened and read back identical through the RAM backend; four files listed; the fifth open fails with STERR_FULL; delete removes the entry |
| app model | AppAt finds the calendar's descriptor; AppSave then AppLoad through a heap state block restores (46, 7, 30) |
| hit test | bar, desktop, status band, off the edge and an open menu drop give (4, 5, 0, 0, 6) |

In the normal build a CTRL press at (130, 75) records CTL_DESKTOP.

Menus (`menus.inc`, the ZX pull downs on the cell grid, one row per
item, save-under out of the shadow):

| subject | what is checked |
|---|---|
| menu-open | a press on FILE opens menu 2: the name table equals the oracle built from the MenuDefs read out of the ROM, title inverted, six item rows |
| menu-open-1/4 | ZX DESK at the left edge and HELP, whose drop is nudged in from the right edge |
| menu-pick | FILE then a press on row 3 picks SAVE (item 2 of menu 2), the menu closes |
| menu-restore | after the pick the name table is byte for byte the boot one again |
| menu-away | VIEW then a press on the desktop: no pick, closed, restored |

Windows (`windows.inc`, phase 3: the ZX window model on the cell grid,
buffers of W x H name table codes from the heap, a Python compositor
as the oracle):

| subject | what is checked |
|---|---|
| win-cal | VIEW > CALENDAR opens a calendar at (4, 4): the name table equals the compositor's desktop + frame + January 1980 from Python's `calendar`, crc32 656f02da |
| win-two | MSX DESK > ABOUT in front of it: two windows, z order (1, 0), the front title inverted |
| win-drag | a press on the calendar's title raises it; a mouse move of (+32, +16) with the button held drags it to (6, 5); z (0, 1) |
| drag-frames | across the whole scenario (two opens, a raise, a drag) not one frame is dropped, on both machines |
| win-close | the close box closes the front window; the name table is the about window alone |
| close-heap | the heap holds the about window's buffer (90 bytes, owner $11) and nothing else |
| win-keys | SHIFT+RIGHT moves the selection to the 2nd, shown inverted; ENTER makes it today (0, 0, 2) |

Applications (phase 4 so far: `note.inc`, `clock.inc`):

| subject | what is checked |
|---|---|
| note-type | FILE > NEW, then H, I, ENTER, X, backspace: the document reads `HI` on row 0, cursor at (0, 1), the window shows the seven rows and the inverted cursor |
| note-file | H SPACE I, FILE > SAVE, XX, FILE > OPEN: the RAM backend holds `NOTE`, 256 bytes, and the document is `H I` again |
| note-space | H SPACE I with the pointer over the note body gives `H I`, cursor 3, name-table crc32 85d6c290 on C-BIOS; holding SPACE repeats text without button events |
| clock-face | VIEW > CLOCK, SHIFT+UP, 3100 frames: 13:01:01 on the 50 Hz machine, 13:00:51 on the 60 Hz one, the face as composed |
| clock-rate | the ROM's second counting run in Python for an hour of true PAL (180,572) or NTSC (215,722) interrupts: 3599 s and 3600 s |
| clock-count | the seconds the ROM counted since the set equal the Python model for the same number of interrupts |

The clock's rate comes from bit 7 of the BIOS's $002B: 50 interrupts a
second and every so often 51 (50.16 Hz), or 59 and every so often 60
(59.92 Hz), the ZX hundredths accumulator with MSX numbers. SPACE types
a space in notepad, including while the pointer is over a window. CTRL is the keyboard equivalent of the left mouse button; cursor
keys move the pointer, and SHIFT+cursor keys go to the application.

Both C-BIOS_MSX1_EU (50 Hz) and C-BIOS_MSX1_JP (60 Hz) pass.

## Arranging windows

**VIEW > CASCADE** places windows back to front starting at cell (1, 2),
stepping two columns and one row, clamped to the desktop. Sizes and z order
are preserved. **VIEW > TILE** divides rows 1–22 into a whole desktop,
left/right halves, a full-height left cell plus two right cells, or four
quarters, for one through four windows. Cells are assigned front to back.
Application text is clipped to the resized interior; each window keeps its
own state. If a replacement buffer cannot be allocated, that window keeps
its previous buffer and geometry.

`msx/test.sh --arrange` checks zero through four windows, CASCADE then TILE,
repeated arrangements, allocator failure, compositor bytes and complete
heap recovery after closing. It also runs the window and notepad/clock
regressions. Arrangement scratch adds **3 bytes** of static RAM. A tile
temporarily holds one replacement buffer before freeing the old one (up to
704 payload bytes plus a four-byte heap header).

## Resizing and scrolling

Drag the bottom-right grip and release to resize in cells, with a minimum
of 6 columns by 4 rows and the desktop as the outer limit. The pointer
hotspot reaches column 31 and row 22 so every grip remains accessible.
The old buffer is freed through `WndAllocBuf`; allocation failure restores
the old dimensions and buffer size. Buffer composition and desktop repaint
use successive frames, keeping large resizes within the tested frame budget.

Notepad's right frame column contains up/down arrows, a track and a position
marker. Arrows move the view by one line; clicking above/below the marker
moves a page. `APP_SCROLL` reports total/visible/first units and `APP_SCROLLTO`
sets the view. The 16-line document initially shows seven lines. Scrolling
leaves the caret in place; SHIFT+DOWN past the viewport scrolls to follow it.
A caret outside the visible interior is not painted.

`msx/test.sh --resize-scroll` asserts real mouse growth, the 6x4 minimum,
24x17 screen-edge growth, a grip press at that edge, allocation failure,
heap recovery on close, `HeapStat`, compositor bytes, arrow/page scrolling,
clamping and SHIFT+DOWN. On C-BIOS EU and JP, growth to 18x10 has name-table
CRC32 **739b7584**, minimum size **fb3bbc3a**, and screen-edge size
**6bb44a63**, with **Dropped = 0** in the resize subjects. The same focused subjects also
pass on Roms_MSX1, Roms_MSX1 with Roms_Disk, and Roms_MSX2.

Static RAM increases by **4 bytes**, to **3,518 bytes**. The normal build
leaves **3,385 bytes** for the heap with the disk ROM (8,770 without it);
the TEST build leaves **670 bytes** with the disk ROM. Resize uses one
window buffer, with no extra temporary heap allocation.

Resizing or TILE also clamps the notepad's viewport to the last valid
first line, without moving the caret. A clock dragged to row 20 moves up
to row 19 when resized to the 6x4 minimum; a failed allocation preserves
its original 8x3 geometry. The resize/scroll subjects cover these cases
with memory and full name-table assertions. These fixes add **0 bytes**
of static RAM.

## Settings

**MSX DESK > SETTINGS** edits pointer ramp (SLOW/MED/FAST), mouse Y
inversion (OFF/ON), application storage (RAM/DISK), SOUND (OFF/ON), and
KEY PTR (OFF/ON). KEY PTR defaults to ON: cursor keys move the pointer
unless SHIFT is held. OFF disables cursor pointer movement and clears its
acceleration counters; the mouse and CTRL button remain active. DISK is offered
only when `DskPresent` detects it. Click a bracketed value to cycle it;
TAB or SHIFT+UP/DOWN selects a row, SHIFT+LEFT/RIGHT decrements/increments,
and ENTER or SPACE activates the selected button. Changes apply immediately.
SAVE writes the SETTINGS file; DONE or ESC closes without writing. A failed
SAVE opens the existing error dialogue. Each window keeps its own row focus.

`SetSave` writes `SETTINGS` through the storage layer; boot calls
`SetLoad` then `SetApply`. The seven bytes are `4D 03 rr yy bb ss kk`: MSX
magic, format version, ramp, Y inversion, storage id (1 RAM, 2 tape with
`TAPE=1`, 5 disk), sound and key pointer (both 0 OFF, 1 ON).
The MSX magic is distinct from ZX settings. Defaults are ramp 1,
inversion 0, sound 1, key pointer 1 and the detected boot backend. Missing
files, I/O failures, incorrect magic/version and incorrect lengths give defaults. Version 1
five-byte records migrate with sound and key pointer ON; version 2 six-byte
records retain sound and migrate with key pointer ON; version 3 requires
seven bytes.
`SetApply` clamps invalid fields and rejects disk selection without BDOS.
Preferences stay on the detected boot device even if the application
backend is changed to RAM, so the next boot can still find them.

Run `msx/test.sh --settings` for the focused subjects. The TEST ROM
asserts save/clear/load, invalid headers, all three ramps, both mouse Y
signs, field validation and backend restoration. The normal ROM asserts
the menu contents and actual inverted mouse movement; on disk it also
boots seeded valid, invalid and truncated/oversized files. The TEST image
contains `SETTINGS` bytes `4D 03 02 00 01 01 01` after saving with RAM selected.
The persistence implementation added 12 static RAM bytes; TEST records
reuse commander scratch space.
In lf-1210, the settings window used 177 heap bytes: a 168-byte cell buffer,
one byte of row focus, and two four-byte allocation headers (+73 bytes).
The old two-byte digit scratch is replaced by one focus byte, reducing
static RAM by one byte to 3,524. The normal ROM uses 13,013 bytes
(TEST: 14,180). The heap budget is 8,764 bytes without
a disk ROM and 3,379 with one (TEST: 6,049 and 664).

The lf-1210 SETTINGS UI subjects asserted a composed name-table CRC32 and all five
record bytes after each mouse click and keyboard change, including ramp
wrap (CRC32 `049f8dfa`), SAVE, DONE without writing, and clearing/reloading
the saved record (`4D 01 02 01 01`, screen CRC32 `0574f7ad` on C-BIOS).
They also check the active ramp pointer/backend and unavailable-disk fallback.
The full `test-all.sh` suite remains the pipeline's responsibility for lf-1210,
as required by the implementation-run instructions.

Focused `msx/test.sh --settings` results for lf-1210:

| Configuration | Assertions |
|---|---|
| C-BIOS_MSX1_EU (50 Hz) | 50 passed |
| C-BIOS_MSX1_JP (60 Hz) | 50 passed |
| Roms_MSX1 | 50 passed |
| Roms_MSX1 + Roms_Disk | 56 passed |
| Roms_MSX2 | 50 passed |

Changes to shared settings now mark every open SETTINGS buffer and its
screen rows for repaint, retaining each window's button focus. The
`settings-two-*` subjects cover keyboard and mouse changes to all three
values, both cached buffers, ESC exposing the remaining window, and SAVE
from that window. For lf-1213, both ROM builds assemble: **+53 ROM bytes**,
**0 added static RAM bytes**, and no additional heap allocation. The normal
ROM uses **13,066 bytes**; RAM and heap budgets are unchanged. Python syntax
and diff checks pass. Emulator assertions have not been run in this
implementation environment (openMSX and xdotool are unavailable); the
focused subjects and full machine matrix remain to be run by the pipeline.

## PSG sound

SOUND defaults to ON. Menu selections and dialogue answers click; opening a
dialogue beeps. Channel A uses a single falling AY envelope: seven register
writes, with no delay loop or later frame callback. The mixer retains its
I/O direction bits, register 15 is untouched, and the address latch returns
to register 14. Tape owns the PSG from the start of a BIOS transfer until
its success or error return; sound requests in that interval are ignored.

`msx/test.sh --sound` checks PSG readback through Z80 IN instructions,
both sound tables, OFF and tape suppression, pointer movement after a beep,
UI call sites with SETTINGS open, frame drops, and the SETTINGS UI and
persistence subjects. Breakpoints measure entry through return using
`machine_info time`; the subject prints cycles and requires fewer than 1000.
The SETTINGS format is now version 2; its sixth byte is SOUND.

This change adds **2 static RAM bytes** (SetSound and TapeBusy), for
**3,526 bytes** total. The normal ROM uses **13,312 bytes**, leaving
**8,762 heap bytes** without disk and **3,377** with disk. SETTINGS grows
by one 24-cell row: **201 heap bytes** per window, up **24 bytes**.
Sound itself allocates no heap memory.

Measured on C-BIOS EU/JP and the real MSX1 BIOS: CLICK/BEEP take **850
cycles**, OFF takes **140**, and tape suppression takes **167**, from
SndPlay entry through return (breakpoints + machine_info time). The sound
subjects report **Dropped = 0**. SOUND OFF save/reload has name-table CRC32
**24174bd9** on C-BIOS; five-byte v1 migration has **fe9a6441**.
The full test-all.sh matrix remains for the pipeline, as required by this
implementation run.

## Commander

**FILE > OPEN** opens a single-pane file picker on the active storage
backend. It lists names and five-digit byte lengths from `StDir`.
**SHIFT+UP/DOWN** selects a file; **ENTER** closes the picker and loads it
into the foremost notepad, creating one if none is open. **DELETE** opens
a modal CANCEL/DELETE confirmation, initially on CANCEL; TAB or
SHIFT+arrows changes the answer, ENTER chooses it, and ESC cancels.
**R** refreshes the directory. Tape has no directory and retains its
sequential FILE > OPEN load operation.

`msx/test.sh --browse` checks the single pane against the Python compositor,
selection, empty and variable-length listings, loading into the foremost
of two notepads, creating a notepad, a storage-open error, confirmation,
cancellation, deletion and heap recovery. On disk, `read_disk_image`
checks that deletion removes only the selected file and preserves every
other payload. `--apps` runs the notepad/clock subjects, including the
updated save/edit/picker/ENTER round trip; `--commander` includes both
picker and existing two-pane subjects.

The picker reuses Commander's buffer and cache. Static RAM grows by
**7 bytes** (one mode byte and six decimal-formatting bytes); each
Commander state grows by **1 byte**, to 203. A Commander window now uses
**571 heap bytes** including its two allocation headers. The modal
confirmation reuses the existing save-under and adds no heap allocation.
The normal build uses **12,557 ROM bytes** and **3,525 static RAM bytes**,
leaving **8,763 heap bytes** without a disk ROM and **3,378** with it.
The TEST build uses 13,724 ROM bytes, 6,240 static RAM bytes and leaves
663 heap bytes on the disk machine.

Measured picker name-table CRC32: RAM listing **6151d015**, disk listing
**c49b1ad1**; after confirmed deletion **47497a3a** (RAM) and **e283b0fe**
(disk). Loading BETA into the foremost notepad gives document CRC32
**5a0ad6a7** and composed screen CRC32 **c1fdf0cf** on both backends.

Open **VIEW > COMMANDER**. The two panes show DISK and RAM when a disk
interface is present; on C-BIOS both panes show the same RAM store.
The active pane and selected name are inverted.

- **TAB** or **SHIFT+LEFT/RIGHT**: choose a pane.
- **SHIFT+UP/DOWN**: select a file; the six-row listing scrolls.
- **ENTER**: open a 256-byte MSX Desk document in a new notepad.
- **C**: copy to the other device, up to 256 bytes. Existing targets are
  refused, not overwritten. RAM holds four files.
- **D**, then **Y**: delete the selected file. **N** or **ESC** cancels.
- **R**: refresh both listings and reset the selection.

Names use **8.3**, including the dot in the displayed name: `NOTES.TXT`
and `NOTES.BAK` are distinct. The disk parser uppercases names and
rejects overlong names, wildcards and paths instead of truncating them.
Folders and volume labels are excluded: this version browses the root
of drive A, without subdirectory navigation. Opening expects MSX Desk's
fixed 256-byte document layout, even when a file has a `.TXT` extension.

The notepad remembers the device it was opened from. Saving a document
opened from RAM writes back to RAM, even when the desktop defaults to disk.
Commander restores the global storage selection after each operation.
Directory pages are cached per window; repainting performs no disk I/O.
Before an operation, Commander resolves the displayed filename again so
a stale index cannot select a different file after another window deletes one.
Listings can be refreshed with R after changes made in another window.
Disk operations are synchronous; their latency is not covered by the
zero-dropped-frame assertion for the existing calendar drag scenario.

The harness seeds real FAT12 images and asserts the name table against
its Python compositor. It checks empty/populated panes, selection,
scrolling, separate window state, window-limit errors, close/heap recovery,
copying in both directions, full RAM/disk stores, close-error injection,
cleanup of failed new copies, no overwrite, delete/cancel,
notepad device ownership and file bytes after save. 257-byte and 64KB
copies are refused; full 8.3 names survive listing/copy/open/save. The
TEST ROM also exercises valid and invalid FCB names without a disk ROM.

## Frame budget, measured

`WinRedraw` repaints by row range: every change says which rows it
touched and which windows need recomposing (a fresh window, a key the
application acted on, a focus change flips two title rows only), and
one repaint per frame paints the desktop for those rows, recomposes the
dirty buffers and blits every window's rows within the range, back to
front. A drag recomposes nothing. Measured with breakpoints on the
loop (`machine_info time` at wake and at the next HALT), the calendar
open through the menu is the longest iteration:

| version | open frame | note |
|---|---|---|
| full recompose, 22 rows | ~30 ms | dropped two frames per open |
| row range, dirty slots | 19.3 ms | dropped one at 50 Hz |
| + MarkRow table, grid by running day, cap-only focus | 17.3 ms | |
| + PUSH desktop fill, rows marked once, incremental blit | 16.0 ms | |
| + APPF_FULL, no blank under an app that paints it all | 14.9 ms | 0 dropped at 50 and 60 Hz |

The flush of the resulting rows lands in the next frame: OUTI, NOP,
JP NZ at 30 cycles a cell, 16 rows about 5.6 ms. `drag-frames` asserts
zero dropped frames on both machines, so this is a guarded number.

## Real BIOS ROMs

C-BIOS has no cassette and no BASIC, so the tape backend and anything
that wants a real machine run on real BIOS ROMs, which are not ours to
distribute. Put them in `msx/roms/` (gitignored, any file names:
openMSX matches ROMs by sha1) and name the machine. Two configs in
`msx/harness/machines/` are built on the dumps of a common ROM set:

| machine | ROMs | what it is |
|---|---|---|
| `Roms_MSX1` | MSX.ROM (sha1 409e82ad...) | a 50 Hz 64K MSX1 on the generic BIOS, the VG 8020 config with the ROM swapped |
| `Roms_MSX2` | MSX2.ROM, MSX2EXT.ROM (the NMS 8245/8250/8255 dumps) | a Philips NMS 8250 without its drive: V9938, 128K mapper, RTC |

    MSX_MACHINE=Roms_MSX1 msx/test.sh
    MSX_MACHINE=Roms_MSX2 msx/run.sh

Both pass every subject; on the V9938 register 1 reads back without
the TMS9918's 4K/16K bit, which the assertion masks.

The DISK.ROM of the same set (Disk BASIC 2.2, a dump openMSX has no
config for) works as a WD2793 in the Philips connection style:
`msx/harness/extensions/Roms_Disk.xml`.

    MSX_MACHINE=Roms_MSX1 MSX_EXT=Roms_Disk msx/test.sh

## The disk backend

`disk.inc` is a storage backend on MSX-DOS 1's BDOS ($F37D): open,
create, delete, close, random block read and write with a record size
of one byte, search first/next for the directory. Names use uppercase 8.3 syntax. `StInit` selects it when the BDOS
jump is there, explicitly enabled tape next, and the RAM backend otherwise, so the notepad's SAVE
lands on a real MSX-DOS file that a PC can read.

Getting there took three measurements:

- A disk ROM initialises in two halves. Its INIT, which runs first
  because the extension sits in slot 1 and the cartridge in slot 2,
  takes a driver work area (HIMEM $F195) and hooks H.RUNC with an
  inter-slot call; the DOS kernel, the BDOS jump and the rest of the
  work area only arrive when BASIC's cold start calls that hook, and
  that handler does not return, it carries on into BASIC. So with a
  disk ROM present the cartridge's INIT hooks H.MAIN, the top of
  BASIC's main loop, returns to the BIOS, and the desktop starts from
  there with HIMEM at $DE77 and 3,717 bytes of heap. Without one
  (C-BIOS, a bare machine) INIT starts the desktop directly.
- BASIC calls hooks with page 1 on its own ROM, so the hook must be an
  inter-slot call (RST $30, the cartridge's slot id from the slot
  register, EXPTBL and SLTTBL): a plain JP was measured to land in
  BASIC at the same address.
- The disk ROM loads sector 0 of the disk to $C000 and calls offset
  $1E. The harness builds its own 720K FAT12 image (`make_disk_image`),
  and with zeros there the ROM ran them as code and asked "Drive
  name?"; a RET says there is no DOS to boot. The date prompt on a
  machine with no clock is answered by a return planted in the
  keyboard buffer at INIT.

The stack and the heap's end come from HIMEM at run time, STACKRES
($380) apart, not from an equate. The TEST build's records leave
986 usable bytes of heap under a disk ROM, so its heap subject allocates
256, 200 and 128 bytes (reduced for the tape buffer).

Subjects on the disk machine: `note-file` finds NOTE, 256 bytes, on
the image with the document's bytes; `store-disk` finds SETTINGS (five
bytes, `4D 01 02 00 01`, after the settings subject), NOTE1, NOTE3 and NOTE4 after NOTE2 was
deleted; `store-dir` expects the fifth file to be created rather than
refused. All five configurations pass: C-BIOS EU and JP, Roms_MSX1,
Roms_MSX1 with Roms_Disk, Roms_MSX2. `msx/test.sh`
links the ROM directory and the configs in as openMSX's user share
inside the image; `run.sh` does the same for the host's openMSX. Any
other machine from `/usr/share/openmsx/machines/` works the same way
once its ROMs are present; a missing one is reported by name and sha1
when the machine starts.

## The tape backend

`ST_TAPE` uses the main BIOS cassette entries: TAPOON/TAPOUT/TAPOOF
for writing and TAPION/TAPIN/TAPIOF for reading. Enable it explicitly
with pasmo `--equ TAPE=1` (default `TAPE=0`), for example:

    pasmo -I msx/src --equ TAPE=1 --bin msx/src/msxdesk.asm msx/build/msxtape.rom msx/build/msxtape.sym

Boot still prefers BDOS when present. Otherwise the opt-in selects tape,
and without it boot selects RAM. C-BIOS has no cassette implementation;
use Roms_MSX1 or Roms_MSX2 for transfers. SETTINGS displays TAPE and
uses defaults at boot on that device, without waiting for a cassette.

Like the ZX backend, writes are buffered until close and reads load a
file at open. One handle holds at most 256 bytes; short writes/reads
report the actual count, EOF returns zero, and directory/delete are
unsupported. The MSX-specific format consists of two BIOS blocks:

- Long leader, `MSXT`, a 13-byte zero-padded name including terminator,
  and a two-byte little-endian length (19 header bytes).
- Short leader and exactly that many payload bytes (0–256).

This is a Desk format, not BASIC's cassette format or a ZX tape image.
Position the cassette at the desired file: an unexpected name, magic or
length fails the open. BIOS carry failures propagate through storage;
a save failure leaves the document marked as unsaved.

`MSX_MACHINE=Roms_MSX1 msx/test.sh --tape` records a real WAV using
`cassetteplayer new`, saves the notepad, edits it, rewinds and plays the
cassette, and uses FILE > OPEN. All 256 restored bytes match the snapshot;
Python independently decodes the WAV's FSK pulses into a 19-byte header
and the identical 256-byte document, CRC32 **0b0601cb** on both real
machines. The subject also checks backend precedence, invalid headers,
and injected TAPOUT/TAPOOF failures. The TEST ROM checks buffering,
partial reads, full writes, EOF and invalid handles on C-BIOS too.
These subjects are included in the normal suite.

Tape traffic masks interrupts in the BIOS. The notepad round trip
measured **Dropped = 34** on both real machines; this operation is outside
the zero-drop UI budget. The clock retains its existing IrqCnt accounting.

Static RAM grows by **299 bytes**: a 256-byte buffer, two 19-byte headers,
two 2-byte counters and one mode byte. Normal static RAM is **3,514 bytes**,
with **3,389 bytes** left for the heap behind the disk ROM; the TEST build
leaves **674 bytes**. Tape adds no heap allocation. The TEST heap subject
now uses 256/200/128-byte allocations to fit this smaller budget.

## Memory budget

The build prints it and fails past the line:

    msxdesk.rom: code $4000-$6A23, 10787 bytes, 21981 free; RAM $C000-$CC81, 3201 bytes, heap 9087 to $F000 without a disk ROM, 3702 with one

Work RAM is handed out by the `var` macro in msxdesk.asm from $C000 up;
the heap takes everything from `RamEnd` to `HeapEnd`, which is HIMEM
minus $380 for the stack, read at Init: $F000 on a bare machine, $DAF7
behind this disk ROM. The RAM storage
backend's four 256 byte files and directory occupy 1,088 bytes;
the name table shadow occupies 768. Commander adds a 360-byte window
buffer and 203-byte state per instance, plus two four-byte heap headers
(571 bytes total). Its 256-byte transfer buffer is shared static RAM.

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
screen; the status band is inverted spaces and a front window's title
row too. Codes $80-$9F are the desktop and frame tiles. Sprites are 16x16 (VDP R1 $E2; CHGMOD leaves 8x8).

The stack is set from HIMEM at startup ($F380 without a disk ROM): the BIOS called the cartridge on
its own stack, and C-BIOS and a real BIOS need not agree where that
was. VDP register writes go through `WrtVdp`, under DI like `SetWrt`.

The display is switched off (R1 bit 6) right after CHGMOD and on again
after the first flush: CHGMOD leaves the BIOS font table on the screen
and it showed for the fraction of a second the tiles, colours and
desktop took to load. The SETTINGS byte "mouse Y invert" is 0 for a pointer that
follows the hand (the MSX mouse's negative delta negated) and 1 for
upside down; the first version had 0 mean "no negation" and a file the
harness left on the disk image turned the axis over on a real screen.
A byte that is neither is a damaged file and reads as 0. The mouse's four nibbles are read under DI, one
strobe sequence that an interrupt handler touching PSG register 15
would shuffle, and the per-frame clamp is 64 as on the ZX: at 16, a
fast host move of 100 pixels lost 34 of its 50 counts (`ptr-fast`
asserts (170, 120) for +100, +60).

## Unsaved-work dialogues

Closing a modified notepad opens **UNSAVED WORK** with **CANCEL**, **DISCARD**
and **SAVE**. CANCEL is initially selected; ENTER chooses it, TAB or
SHIFT+arrows moves the focus, ESC cancels, and a click chooses an answer.
Clicks outside the panel and other typing are ignored. A failed open,
write, short write or close during saving keeps the document modified and
opens **COULD NOT SAVE**, dismissed with OK, ENTER or ESC. FILE > SAVE
also reports failures with this alert.

`msx/test.sh --dialogs` checks the panel and focus against a cell oracle,
modal input, unchanged document bytes on cancel/error, heap recovery on
discard and all 256 saved bytes in RAM or on the disk image. It injects
storage open/write/close errors, including a write error with a full byte
count. The arrangement close subjects now explicitly discard their edited
notepad; `msx/test.sh --arrange-only` runs that group alone.

The panel saves 140 shadow cells in the existing 256-byte Commander
transfer buffer: modal input prevents Commander operations until restoration.
Window redraws wait while the dialogue is open; pending updates repaint after
restoration. Static RAM grows by **14 bytes** (9 dialogue state bytes and
5 control-table bytes), with no extra heap allocation. The normal ROM uses
3,215 static RAM bytes and leaves 3,688 heap bytes with the disk ROM;
the TEST ROM leaves 973 heap bytes with it.

## Failed notepad loads

A failed load keeps the document, modified flag, caret, viewport, filename
and storage ownership. Reads use the existing 256-byte Commander scratch
buffer; the document and name are committed only after a complete read and
successful close. FILE > OPEN's sequential path and the Commander show
**COULD NOT LOAD**, and dismissing the alert leaves the document intact.

`msx/test.sh --note-load` injects open, short-read, read and close failures
through the sequential menu path, the file picker and the two-pane Commander.
It asserts the preserved document (CRC32 **c21bbd31**), metadata, carry and
close counts, the alert text, dismissal, and a successful subsequent load
(name-table CRC32 **82f03d3c**). The two-pane Commander opens a new notepad,
whose blank document is preserved on failure (CRC32 **20acf377**).

The normal build uses **12,613 ROM bytes**, **3,525 static RAM bytes** and
leaves **8,763 heap bytes** without a disk ROM, **3,378** with it. This fix
adds **56 ROM bytes**, **0 static RAM bytes**, and no heap allocations.

For lf-1212, all 32 focused subjects passed on C-BIOS_MSX1_EU (50 Hz),
C-BIOS_MSX1_JP (60 Hz), Roms_MSX1, Roms_MSX1 with Roms_Disk and Roms_MSX2.
The full `msx/test-all.sh` run is left to the pipeline, as required by the
ticket's implementation-run instructions.

Focused lf-1215 verification (the complete suite was not run here):

| Configuration | Result |
|---|---|
| C-BIOS MSX1 EU, 50 Hz | 11 sound assertions; 73 SETTINGS UI/persistence assertions passed |
| C-BIOS MSX1 JP, 60 Hz | 103 assertions with --sound (TEST ROM, sound, SETTINGS); final sound recheck passed |
| Roms_MSX1 | 11 assertions with --sound-only passed |
| Roms_MSX1 + Roms_Disk | 111 assertions with --sound passed |
| Roms_MSX2 | 11 assertions with --sound-only passed |

Disk SOUND OFF save/reload name-table CRC32 is **fc73ad4c**. The BIOS
transfer probes use stubbed BIOS returns to assert PSG ownership during
success and failure; actual cassette waveform round trips remain in the
pipeline's existing tape subjects.

For lf-1216, KEY PTR adds **100 ROM bytes** and **1 static RAM byte**.
The normal build uses **13,412 ROM bytes**, **3,527 static RAM bytes**,
and leaves **8,761 heap bytes** without disk or **3,376** with disk.
The TEST build uses **14,585 ROM bytes**, **6,242 static RAM bytes**,
and leaves **6,046 / 661 heap bytes** respectively. Each SETTINGS window
now uses **225 heap bytes**, up **24**, for its extra row.

The `--settings` subjects include KEY PTR ON/OFF with and without SHIFT,
the measured acceleration ramp, disabling during a held direction,
mouse movement and CTRL while OFF, mouse/keyboard cycling, OFF persistence,
and v1/v2 migration. On C-BIOS, OFF save/clear/load has name-table CRC32
**ebe8b816**; v2 migration preserving SOUND OFF has **43f72adc** and
returns carry clear. The complete `test-all.sh` suite is left to the
pipeline, as required by this implementation run.

Focused lf-1216 verification (`msx/test.sh --settings`):

| Configuration | Result |
|---|---|
| C-BIOS_MSX1_EU, 50 Hz | 107 assertions passed |
| C-BIOS_MSX1_JP, 60 Hz | 107 assertions passed |
| Roms_MSX1 | 107 assertions passed |
| Roms_MSX1 + Roms_Disk | 116 assertions passed |
| Roms_MSX2 | 107 assertions passed |

Disk KEY PTR OFF save/clear/load has name-table CRC32 **338c5e83**.
