# MSX Desk

ZX Desk, Damian Cooper's desktop for the Spectrum, ported to the MSX1
and renamed for the machine it runs on. The original ZX source is no longer in this tree; it lives
upstream (github.com/mindbox77/zxdesk) and in this repository's first commit.

    ./test.sh             build the toolchain image once, assemble, run every subject, assert
    ./test.sh --shell     a shell inside the image
    REBUILD=1 ./test.sh   rebuild the image after a Dockerfile change
    ./run.sh              build and run it in a visible openMSX: the host's
                            (dnf install openmsx cbios) or the image on the
                            host display with the GPU passed in

Everything runs in `docker`: Debian trixie with openMSX 20.0,
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
  calling uid, which is why `test.sh --shell` mounts `/etc/passwd` read-only. The test run itself streams the tree in and `build` out with tar, so it also works from a container that only has the docker socket.
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

`msxtest.py` (the model was `zxtest.py` in the original ZX Desk) assembles
`src/msxdesk.asm` into a padded 32K `page12` ROM, boots it in
`C-BIOS_MSX1_EU` with the throttle off, runs a step list through
`harness/run.tcl`, and reads VRAM (16K) and work RAM ($C000-$FFFF)
back through `debug read_block`. Checksums are the assertion; the
screenshot in `build/out/<subject>/shot.png` is for people.

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
| calendar | all 1,200 months 1980-2079 agree with Python's calendar on weekday of the 1st and length; August 2026 grid rows; a step back from the 1st lands on 31 July; ENTER sets today; midnight on the 31st rolls the month; a month on from 31 Jan 1980 is 29 Feb, a year on 28 Feb 1981, two months back December 1980, and both ends of the range stop |
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
| win-step | `.` a month on, SHIFT+`.` (`>`) a year on: February 1981 on the grid, crc32 775c19ea |

Not in the ZX original: the calendar steps a month with `,` and `.` and a
year with `<` and `>`, because today starts at 1 January 1980 and setting
it a week at a time took thousands of presses. The day is cut to the
month's last where needed. 115 ROM bytes, no RAM.

Keyboard shortcuts (`shortcut.inc`, after the ZX's): GRAPH and a letter.
The ZX used both shifts (EXTEND MODE); an MSX has one SHIFT bit and CTRL
is the pointer's button, so GRAPH, which nothing else reads, is the
modifier. GRAPH+letter decodes to a code from $81 up that no text table
produces, and HdlKey dispatches it before any application sees a key;
an open menu or dialogue swallows it. Shortcuts do not auto-repeat.
N NEW NOTE, O OPEN, S SAVE, W CLOSE, X NEXT WINDOW, C CASCADE, T TILE,
K CLOCK, L CALENDAR, F COMMANDER, G SETTINGS, I ABOUT. V is SAVE AS, R is PRINT and D is DESKTOP, the
window that says which icons are on the desktop.
HELP > KEYS opens the list, with the calendar's keys, as a window;
at 21 rows it opens at row 2, the lowest it fits.
333 ROM bytes, no RAM.

| subject | asserts |
|---|---|
| sc-next | GRAPH+L, GRAPH+K, GRAPH+X: two windows, the calendar back in front, z (0, 1) |
| sc-close | then GRAPH+W: the clock alone, nothing echoed on the status row |
| sc-repeat | GRAPH+N held 60 frames opens one notepad (4 without the repeat guard) |
| keys-win | HELP > KEYS: the name table equals the compositor's list window at (16, 2) |

Applications (phase 4 so far: `note.inc`, `clock.inc`):

| subject | what is checked |
|---|---|
| note-type | FILE > NEW, then H, I, ENTER, X, backspace: the document reads `HI` on row 0, cursor at (0, 1), the window shows the seven rows and the inverted cursor |
| note-file | H SPACE I, FILE > SAVE, NOTE ENTER, XX, FILE > OPEN: the RAM backend holds `NOTE`, 256 bytes, and the document is `H I` again |
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

`./test.sh --arrange` checks zero through four windows, CASCADE then TILE,
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
the old dimensions and buffer size. Allocation, buffer composition and
desktop repaint use three successive frames, keeping large resizes within
the tested frame budget: with lf-1527 the release frame of the 24x17
resize measured 15.9 ms on the 60 Hz machine (allocation 1.1 ms, the
compose 13.0 ms) and dropped a frame once the main loop grew by a few
dozen microseconds, so the compose moved to the frame after the release.

Notepad's right frame column contains up/down arrows, a track and a position
marker. Arrows move the view by one line; clicking above/below the marker
moves a page. `APP_SCROLL` reports total/visible/first units and `APP_SCROLLTO`
sets the view. The 16-line document initially shows seven lines. Scrolling
leaves the caret in place; SHIFT+DOWN past the viewport scrolls to follow it.
A caret outside the visible interior is not painted.

`./test.sh --resize-scroll` asserts real mouse growth, the 6x4 minimum,
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
inversion (OFF/ON), application storage (RAM/DISK), SOUND (OFF/ON),
KEY PTR (OFF/ON), and LATTICE (NONE/DOTS/GRID). LATTICE defaults to
DOTS; it updates the desktop and menu rule patterns in all three screen
banks without changing name-table cells. KEY PTR defaults to ON: cursor
keys move the pointer
unless SHIFT is held. OFF disables cursor pointer movement and clears its
acceleration counters; the mouse and CTRL button remain active. DISK is offered
only when `DskPresent` detects it. Click a bracketed value to cycle it;
TAB or SHIFT+UP/DOWN selects a row, SHIFT+LEFT/RIGHT decrements/increments,
and ENTER or SPACE activates the selected button. Changes apply immediately.
SAVE writes the SETTINGS file; DONE or ESC closes without writing. A failed
SAVE opens the existing error dialogue. Each window keeps its own row focus.

`SetSave` writes `SETTINGS` through the storage layer; boot calls
`SetLoad` then `SetApply`. The record is 26 bytes, `4D 05 rr yy bb ss kk ll`
followed by the six desktop icons at three bytes each: MSX
magic, format version, ramp, Y inversion, storage id (1 RAM, 2 tape with
`TAPE=1`, 5 disk), sound and key pointer (both 0 OFF, 1 ON), lattice (0 NONE, 1 DOTS,
2 GRID), then for each icon present (0/1), cell column and cell row
(see "Desktop shortcuts").
The MSX magic is distinct from ZX settings. Defaults are ramp 1,
inversion 0, sound 1, key pointer 1, lattice 1, the detected boot backend
and the six icons down the left. Missing
files, I/O failures, incorrect magic/version and incorrect lengths give defaults. Version 1
five-byte records migrate with sound and key pointer ON; version 2 six-byte
records retain sound and migrate with key pointer ON; version 3 seven-byte
records retain both fields and migrate with lattice
DOTS. Versions 1 and 2 also default lattice to DOTS. Version 4 is the
eight-byte record without the icons; versions 1 to 4 all load with the
default icons. Version 5 requires 26 bytes; a present byte other than 0
becomes 1 and a position is clamped so the whole slot is on the desktop.
`SetApply` clamps invalid fields and rejects disk selection without BDOS.
Preferences stay on the detected boot device even if the application
backend is changed to RAM, so the next boot can still find them.

Run `./test.sh --settings` for the focused subjects. The TEST ROM
asserts save/clear/load, invalid headers, all three ramps, both mouse Y
signs, field validation and backend restoration. The normal ROM asserts
the menu contents and actual inverted mouse movement; on disk it also
boots seeded valid, invalid and truncated/oversized files. The TEST image
contains `SETTINGS` bytes `4D 04 02 00 01 01 01 01` after saving with RAM selected.
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

Focused `./test.sh --settings` results for lf-1210:

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

`./test.sh --sound` checks PSG readback through Z80 IN instructions,
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
**SHIFT+UP/DOWN** or a single click on a file row selects it (inverted).
**ENTER** or a single click on **[OPEN]** closes the picker and loads it
into the foremost notepad, creating one if none is open. **DELETE** opens
a modal CANCEL/DELETE confirmation, initially on CANCEL; TAB or
SHIFT+arrows changes the answer, ENTER chooses it, and ESC cancels.
**R** refreshes the directory. Tape has no directory and retains its
sequential FILE > OPEN load operation.

`./test.sh --browse` checks the single pane against the Python compositor,
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

With the desktop icons (lf-1527) the same scenario was measured again
on the 60 Hz machine, breakpoints at the loop's HALT and the
instruction after it: the calendar open had grown to 15.9 ms before
the icons and went to 18.2 ms with the first icon painter, one frame
dropped. The painter was rewritten (see "Desktop shortcuts") and the
simple openers now go through `WndOpenLater`, the split SETTINGS
already used: the buffer is composed in the frame of the open and the
desktop repaint and blit happen in the next. Longest iterations now:
the repaint after ABOUT opens over the calendar 13.4 ms, the raise
13.1 ms, the calendar open 11.7 ms and its repaint 11.6 ms; 0 dropped.

## Real BIOS ROMs

C-BIOS has no cassette and no BASIC, so the tape backend and anything
that wants a real machine run on real BIOS ROMs, which are not ours to
distribute. Put them in `roms/` (gitignored, any file names:
openMSX matches ROMs by sha1) and name the machine. Two configs in
`harness/machines/` are built on the dumps of a common ROM set:

| machine | ROMs | what it is |
|---|---|---|
| `Roms_MSX1` | MSX.ROM (sha1 409e82ad...) | a 50 Hz 64K MSX1 on the generic BIOS, the VG 8020 config with the ROM swapped |
| `Roms_MSX2` | MSX2.ROM, MSX2EXT.ROM (the NMS 8245/8250/8255 dumps) | a Philips NMS 8250 without its drive: V9938, 128K mapper, RTC |

    MSX_MACHINE=Roms_MSX1 ./test.sh
    MSX_MACHINE=Roms_MSX2 ./run.sh

Both pass every subject; on the V9938 register 1 reads back without
the TMS9918's 4K/16K bit, which the assertion masks.

The DISK.ROM of the same set (Disk BASIC 2.2, a dump openMSX has no
config for) works as a WD2793 in the Philips connection style:
`harness/extensions/Roms_Disk.xml`.

    MSX_MACHINE=Roms_MSX1 MSX_EXT=Roms_Disk ./test.sh

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
593 bytes of heap under a disk ROM, so its heap subject allocates
256, 160 and 128 bytes (reduced first for the tape buffer, then for the
desktop icons).

Subjects on the disk machine: `note-file` finds NOTE, 256 bytes, on
the image with the document's bytes; `store-disk` finds SETTINGS (five
bytes, `4D 01 02 00 01`, after the settings subject), NOTE1, NOTE3 and NOTE4 after NOTE2 was
deleted; `store-dir` expects the fifth file to be created rather than
refused. All five configurations pass: C-BIOS EU and JP, Roms_MSX1,
Roms_MSX1 with Roms_Disk, Roms_MSX2. `./test.sh`
links the ROM directory and the configs in as openMSX's user share
inside the image; `run.sh` does the same for the host's openMSX. Any
other machine from `/usr/share/openmsx/machines/` works the same way
once its ROMs are present; a missing one is reported by name and sha1
when the machine starts.

## The tape backend

`ST_TAPE` uses the main BIOS cassette entries: TAPOON/TAPOUT/TAPOOF
for writing and TAPION/TAPIN/TAPIOF for reading. Enable it explicitly
with pasmo `--equ TAPE=1` (default `TAPE=0`), for example:

    pasmo -I src --equ TAPE=1 --bin src/msxdesk.asm build/msxtape.rom build/msxtape.sym

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

`MSX_MACHINE=Roms_MSX1 ./test.sh --tape` records a real WAV using
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
row too. Codes $80-$87 are the desktop and frame tiles: lattice, rule,
the window's left and right edges, bottom, both bottom corners and the
close box. The desktop icons take $88-$97, four tiles a shape for the
four shapes (document, clock, calendar, drawer), copied by LoadTiles
right after the frame tiles and given the text colours by the colour
table's fill; they are the first free codes after the chrome. $98-$9F
and $00-$1F are still free (counted: nothing paints a code below $20,
and the inverted bank starts at $A0). Sprites are 16x16 (VDP R1 $E2; CHGMOD leaves 8x8).

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

`./test.sh --dialogs` checks the panel and focus against a cell oracle,
modal input, unchanged document bytes on cancel/error, heap recovery on
discard and all 256 saved bytes in RAM or on the disk image. It injects
storage open/write/close errors, including a write error with a full byte
count. The arrangement close subjects now explicitly discard their edited
notepad; `./test.sh --arrange-only` runs that group alone.

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

`./test.sh --note-load` injects open, short-read, read and close failures
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
The full `./test-all.sh` run is left to the pipeline, as required by the
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

Focused lf-1216 verification (`./test.sh --settings`):

| Configuration | Result |
|---|---|
| C-BIOS_MSX1_EU, 50 Hz | 107 assertions passed |
| C-BIOS_MSX1_JP, 60 Hz | 107 assertions passed |
| Roms_MSX1 | 107 assertions passed |
| Roms_MSX1 + Roms_Disk | 116 assertions passed |
| Roms_MSX2 | 107 assertions passed |

Disk KEY PTR OFF save/clear/load has name-table CRC32 **338c5e83**.

For lf-1217, LATTICE adds **226 ROM bytes** and **1 static RAM byte**.
The normal build uses **13,638 ROM bytes**, **3,528 static RAM bytes**,
and leaves **8,760 / 3,375 heap bytes** without/with disk. The TEST build
uses **14,814 ROM bytes**, **6,243 static RAM bytes**, and leaves
**6,045 / 660 heap bytes**. Each SETTINGS window uses **249 heap bytes**
including allocation headers, up **24** for the extra row. Pattern updates
allocate no heap memory.

`--settings` asserts all eight bytes of both tiles in all three pattern
banks for NONE, DOTS and GRID, plus invalid-value fallback. The isolated
pattern update preserves the full VRAM name table and RAM shadow byte for
byte: C-BIOS CRC32 **add5ac78** with no window, **240fbe30** with SETTINGS,
and **b8b1f0d6** with two SETTINGS windows, for every pattern. UI tests
assert mouse/keyboard wrap, both cached window buffers and their distinct
focus, closing the front window, SAVE/clear/SetLoad (GRID CRC32
**6d26cc98**), and carry-clear v3 migration with DOTS (**0cefb18a**).
The displayed LATTICE value itself changes during UI interaction; the
isolated update checks that changing the background does not repaint cells.

Focused verification: C-BIOS EU (50 Hz), **151 assertions** with
`--settings`; C-BIOS JP (60 Hz), **162 assertions** with `--sound`
(including SETTINGS). The JP two-SETTINGS sound/menu probe reports
**Dropped = 0**, maximum measured frame **12,276 us**. The complete
`test-all.sh` matrix remains for the pipeline, as required by this run.

Roms_MSX1 + Roms_Disk passes **162 assertions** with `--settings`,
including seeded v1/v2/v3 migration, v4 GRID at boot, invalid fields and
short/oversized records. Its unchanged name-table CRC32 values with one
and two SETTINGS windows are **fc6b58a5** and **ea011005**; GRID
SAVE/clear/load is **b5422a0d**. Standalone Roms_MSX1 and Roms_MSX2 are
left to the pipeline's full machine matrix.

## lf-1218: opening a saved document

Baseline: commit `c99eb9f`, normal ROM 13,448 bytes. On C-BIOS MSX1 EU,
`--open-ui` passed both Enter cases before the fix and failed both mouse
cases. Commander had no mouse handler: clicking a row only focused its
window. The previous keyboard/fixture PASS therefore did not cover the
missing mouse selection/open operation. The reported Enter failure is
**not reproduced**; the reporter's build and exact steps remain unknown.
Do not infer that the mouse defect explains that part of the report.

Reproduction (`./test.sh --open-ui`, RAM or disk, same running session):

1. Click FILE > NEW. Type `H I`, ENTER, `X` using the keyboard matrix.
2. Click FILE > SAVE (filename `NOTE`, 256 bytes). Type another `X`.
3. Either leave Notepad open, or click its close box and DISCARD.
4. Click FILE > OPEN. The picker shows `NOTE` inverted, size `00256`.
   Click its row once. Press ENTER in the keyboard case; click **[OPEN]**
   once in the mouse case. Double-click is not required or implemented.
5. Assert exactly one Notepad, no alert, cleared modified flag and all
   256 document bytes equal both the independent expected content and
   the saved RAM payload/disk file: CRC32 **1cc48393**.

Before the fix the mouse left the edited document at CRC32 **69f8980d**;
with Notepad still open there were two windows (Notepad and picker).
`--browse` additionally clicks BETA in a multi-file listing and asserts
both the inverted selection (RAM name-table CRC32 **e2755111**) and loaded
content (**5a0ad6a7**) with Enter and the OPEN button. Blank rows leave
selection unchanged. Mouse buttons use xdotool/openMSX joystick input;
pointer coordinates are placed deterministically, as in SETTINGS tests.
No load/save routine is directly invoked for these round trips.

`--note-load` also exercises the mouse OPEN button for open, short-read,
read and close errors: the alert is visible, document CRC32 **c21bbd31**
and metadata survive, and dismiss/retry succeeds (screen **82f03d3c**).

Tape still takes the sequential branch before Commander. With `TAPE=1`,
`--tape` records `NOTE`, edits it, rewinds and plays the cassette, then
uses FILE > OPEN. Its second round trip closes/DISCARDs the edited note,
uses FILE > NEW to supply the sequential loader's required target, then
rewinds/plays and opens. Both compare all 256 bytes and independently
decode the recorded WAV (19-byte header plus payload, CRC32 **0b0601cb**).
Tape FILE > OPEN without a target Notepad is unchanged; it is not a picker.
RAM persistence across restart is neither expected nor tested.

The fix adds **103 ROM bytes**, **0 static RAM bytes**, and no heap allocation.
Normal build: **13,551 ROM bytes**, **3,527 static RAM bytes**;
heap **8,761** bytes without disk / **3,376** with disk.
TEST build: **14,724 ROM bytes**, **6,242 static RAM bytes**;
heap **6,046 / 661** bytes. No timing improvement is claimed.

Focused verification for lf-1218 (all passed):

| Machine | Backend | Subjects run | Assertions |
|---|---|---|---|
| C-BIOS_MSX1_EU, 50 Hz | RAM | `--open-ui`, `--browse`, `--note-load` | 71 |
| C-BIOS_MSX1_JP, 60 Hz | RAM | `--open-ui`, `--browse`, `--note-load` | 71 |
| Roms_MSX1 + Roms_Disk | disk | `--open-ui`, `--browse` | 28 |
| Roms_MSX1 | RAM, tape | `--open-ui`, `--tape` | 15 |
| Roms_MSX2 | RAM, tape | `--open-ui`, `--tape` | 15 |

Normal ROM SHA256:
`d948c8c1b27cf89b46022a24c25172122755667df9d08d932a295490ff99e149`.
`git diff --check` and Python compilation also pass. The full
`./test-all.sh` was **not run here**, because this implementation run
explicitly permits only focused tests; the pipeline must run the full
50/60 Hz and real-ROM matrix. The new subjects are included in its default
suite. The local `roms` symlink is ignored and not committed.

## lf-1219: Notepad width and long lines

The UI reproduction on baseline `1193d35` types fourteen H characters,
drags the resize grip from 16x9 to 18x10, then types six I characters.
The baseline still puts the I characters on the next document row:
chunk caret `(6, 1)`, document CRC32 **e4be184b**. This establishes the
old automatic 14-character line break in this build; the reporter's
exact build remains unknown.

Notepad now uses the complete window interior, excluding both frame
columns and the right-hand scrollbar. Long lines scroll horizontally
with the caret; SHIFT+LEFT/RIGHT reaches hidden text with KEY PTR ON.
Widening reveals earlier columns again. Resizing changes neither the
text nor its hard line breaks. Up/down and the vertical scrollbar count
logical lines, including lines spanning several storage chunks.

The file remains **256 bytes**, with sixteen chunks of fourteen printable
characters, a zero terminator, and a continuation byte. A continuation
byte of **1** joins the following chunk to the same logical line; **0**
ends the line. Legacy files have zero continuation bytes and retain all
sixteen lines. Loading clears invalid continuation values and forbids a
link after the final chunk. Older Desk builds do not understand the new
continuation flag and display the chunks as separate lines.

The existing **224-character document budget** is unchanged. Long lines
share that budget with other lines, up to a single 224-character line.
Insertion carries text across chunks; ENTER splits and backspace joins
logical lines. Empty continuation tails are reclaimed. Insertion/splitting
that needs a chunk refuses when it would discard existing text. The
RAM/disk/tape backends, Commander copy limit and atomic load scratch
remain unchanged; no larger backend buffers are required.

`./test.sh --note-width` adds UI subjects for typing after a grip resize,
shrinking to four visible columns, reaching the hidden prefix, growing
again, insertion, split/join/backspace, logical up/down, 42-character and
full-capacity lines, retaining later lines, and ENTER on the final blank
chunk. Assertions compare all **256 document bytes**, the entire composed
name table, cursor, logical row count, viewport and window geometry.
Mouse drags run throttled so host motion arrives before button release.

The 20-character document retains CRC32 **9a0f1306** throughout resizing
and the RAM/disk/tape save-edit-or-close-reopen UI routes. Its grown
screen CRC32 is **21936a7a**, shrunk screen **31dd9b61**. The 224-character
capacity/refusal case has document CRC32 **22b4f361**. Tape's existing
round trips now use this wide document, including closing/DISCARD and
creating a target before sequential reopening; independent WAV decoding
asserts both the 19-byte header and the complete 256-byte payload.
On both real BIOS machines these tape cases measured **Dropped = 36/37**
(existing/closed target), outside the zero-drop UI budget.

Compared with the assembled baseline, this adds **420 ROM bytes** and
**7 static RAM bytes** (eight scratch bytes, one per-instance viewport
byte, minus two obsolete scratch bytes). Each Notepad state allocation
grows by **1 byte**, from 276 to 277; there are no additional allocations.
The normal build uses **14,161 ROM bytes**, **3,535 static RAM bytes**,
with **8,753 / 3,368** heap bytes without/with disk. The TEST build uses
**15,337 ROM bytes**, **6,250 static RAM bytes**, with **6,038 / 653** heap
bytes. Resize tests assert allocator statistics and complete heap recovery.

The full `./test-all.sh` is deliberately left to the pipeline: this
implementation run explicitly permits focused tests only. The new width
subjects and expanded tape round trips are included in the default suite.

Focused verification (all listed assertions passed):

| Machine | Subjects | Assertions |
|---|---|---|
| C-BIOS_MSX1_EU, 50 Hz | `--note-width`, `--resize-scroll`, `--apps` | 16 + 20 + 7 |
| C-BIOS_MSX1_JP, 60 Hz | `--note-width` | 16 |
| Roms_MSX1 | `--note-width`, `--tape` | 16 + 11 |
| Roms_MSX1 + Roms_Disk | `--note-width` (disk UI round trips) | 16 |
| Roms_MSX2 | `--note-width`, `--tape` | 16 + 11 |

The EU resize subjects measured **Dropped = 0**. Python compilation and
`git diff --check` pass. The local `roms` symlink is ignored and is
not part of the commit.

## lf-1522: FILE > DELETE

FILE > DELETE confirms deletion of the front saved Notepad document,
initially selecting CANCEL. DELETE calls the active storage backend,
keeps the document open and marks it modified and no longer saved.
A new document or another front window opens the existing picker.
Backends using `StNoDelete` show NOT SUPPORTED. Failed deletion retains
the document's saved/modified state. Full 8.3 names use the existing
two-line confirmation layout. No new key is introduced.

`./test.sh --delete` adds eleven memory/checksum assertions covering
confirmation, byte-identical cancellation, only the selected file being
removed, retained document contents/window, picker equivalence to OPEN,
unsupported backends, errors and full 8.3 names. All eleven pass on
C-BIOS_MSX1_EU (50 Hz), C-BIOS_MSX1_JP (60 Hz), Roms_MSX1,
Roms_MSX1 + Roms_Disk and Roms_MSX2. RAM directory CRC32 changes from
`730eaddc` to `45fc3d2f`; the document remains `c21bbd31`.
Confirmation name-table CRC32 is `92447dda`; picker CRC32 is `23a99124`
on RAM and `86635be0` on disk. Disk cancellation retains the complete
image byte for byte; `read_disk_image` confirms only NOTE is removed.

The existing `--open-ui` routes additionally assert the saved flag after
typing, saving, modifying/closing and reopening through both Enter and
mouse input: four assertions pass on EU, JP and disk, with all 256 document
bytes at CRC32 `1cc48393`. The tape subjects assert the same saved flag
after their complete cassette round trips and exercise DELETE against
the actual tape registry. All twelve `--tape` assertions pass on both
Roms_MSX1 and Roms_MSX2: document/WAV CRC32 `9a0f1306`, unsupported
alert name-table CRC32 `5d8b3f04`. Cassette round trips measured
35 dropped frames on each machine; no timing improvement is claimed.

Measured against assembled `cf19453`: **+187 ROM bytes**, **+1 static RAM
byte**, **+1 heap byte per Notepad state** (277 to 278); no extra allocations.
Normal ROM: **14,826 bytes**, **1,558 bytes free in page 1**; static RAM
**3,536 bytes**, heap **8,752 / 3,367 bytes** without/with disk.
TEST ROM: **16,103 bytes**, **281 bytes free in page 1**; static RAM
**6,268 bytes**, heap **6,020 / 635 bytes**. Confirmation text reuses
Commander scratch beyond the dialogue's 140-byte save-under.

The full `test-all.sh` was not run here: this implementation run permits
only focused tests. The new subjects are included in the default suite
for the pipeline. The local ignored `roms` symlink is not committed.


## SAVE AS (lf-1523)

FILE > SAVE AS and GRAPH+V open a modal filename field. Type a name,
use BS and LEFT/RIGHT (SHIFT is optional), then ENTER to save or ESC to
cancel. SAVE also asks for a name for a new document. RAM and cassette
names have at most 12 characters plus the terminator; disk names use
8.3 components. The field accepts uppercase letters, digits, underscore,
hyphen and a separating dot. Invalid input, including `*`, is refused.
Existing RAM/disk names require OVERWRITE, with CANCEL initially selected.
Successful saves and loads display the document name in its window title.
The document keeps its own storage backend, as with ordinary SAVE.

`DlgInput` in `src/dialog.inc` takes a caption, answer callback and
candidate-validator callback. It owns a twelve-character insertion field,
cursor and rollback buffer in the unused part of `CmdBuf`; rendering goes
through `ShadowNT`. No VRAM output routine or output spacing changes.
SAVE AS commits `NoteName` only after the write and close succeed.

Focused validation (the implementation-run instruction reserves the full
`test-all.sh` suite for the pipeline):

| Configuration | `--saveas` assertions |
|---|---:|
| C-BIOS_MSX1_EU, 50 Hz | 21 |
| C-BIOS_MSX1_JP, 60 Hz | 21 |
| Roms_MSX1 | 21 |
| Roms_MSX1 + Roms_Disk | 21 |
| Roms_MSX2 | 21 |

These subjects exercise menu and GRAPH input, insertion/backspace/cursor
movement, backend length limits, cancellation, overwrite decisions,
failed writes, separate documents, renamed copies, HELP > KEYS and
save/edit/reopen. Disk checks also compare both untouched fixture files.
Additional modified test groups passed: C-BIOS EU `--apps` (7),
`--dialogs` (30), `--note-load` (44), `--delete` (11) and `--open-ui` (4);
MSX1 with disk `--commander` (55) and `--open-ui` (4); MSX1 `--tape` (13).
The modified tape subjects use real BIOS recording and playback: LETTER
is saved, edited and reopened; a second route closes and reopens NOTE.
All 256 bytes are compared, and Python independently decodes the recorded
WAV header and payload. The cassette filename limit is asserted as well.

Measured document CRC32 is `9fbabb8f` for LETTER, `708c69bf` after the edit
and overwrite; its saved title screen is `eaf22d9c`. The tape document
CRC32 is `9a0f1306`. HELP > KEYS has nametable CRC32 `7d64da57`.

Assembling checkout base `ed02ada` gives 14,826 normal ROM bytes, rather
than the earlier 14,639 quoted in the ticket. This change adds **649 ROM
bytes**, giving **15,475**, with **909 bytes** left in page 1. The TEST
ROM grows by the same amount to **16,752 bytes** (page 2 is mapped).
Static RAM grows **7 bytes**, from 3,536 to **3,543**. Available normal
heap shrinks by 7 to **8,745 bytes**, or **3,360** with the disk ROM.
The dialog adds **0 heap allocations** and reuses 26 scratch bytes.
The additional HELP > KEYS row increases that window's heap buffer by
**15 bytes** while it is open; document state size is unchanged.

Padded ROM CRC32: normal `ebda8e84`, TEST `d964f796`.

## Printer (lf-1524)

FILE > PRINT and GRAPH+R print the front notepad through the write-only
`ST_PRINT` (8) backend. In COMMANDER, **P** toggles the opposite pane
between PRN and RAM; **C** copies the selected file there. A 256-byte
notepad is printed as logical lines, joining continuation chunks and
omitting empty trailing lines; each line ends in CR LF and the job ends
in FF. Other files within the commander's existing 256-byte copy limit
are sent as raw bytes followed by FF. Printing never changes the source.
PRN has only `STCAP_WRITE`, no directory, and is excluded from FILE > OPEN
and SETTINGS > BACKEND. HELP > KEYS includes R PRINT.

The backend uses inter-slot CALSLT calls to BIOS LPTSTT ($00A8) and
LPTOUT ($00A5). It checks readiness before opening and before each output
byte, and reports `PRINTER NOT READY` for busy status or output carry.
[C-BIOS implements both calls](https://github.com/cbios/cbios/blob/master/src/main.asm);
its LPTOUT implementation includes a busy loop, hence the explicit status
check. The logger subjects run on C-BIOS as well as the real BIOS machines;
no alternate printer port driver is needed.

`./test.sh --print` uses openMSX `plug printerport logger` and
`set printerlogfilename` to assert complete output bytes. Subjects cover
the menu, shortcut, front document, empty document, continuation lines,
internal empty lines, commander copy from a UI-saved document, raw streams,
unplugged printer, injected LPTOUT carry after three bytes, capabilities,
settings cycling and the KEYS name table. No screenshot is an assertion.

Measured against repository base `666af80`: normal ROM **15,483 → 15,870
bytes (+387)**, leaving **514 bytes** in page 1. TEST ROM **16,760 → 17,147
bytes (+387)**. Static RAM remains **3,543 bytes (+0)** (TEST **6,275**),
and the normal heap budget remains **8,745 bytes** without disk or **3,360**
with disk. Printing allocates **0 heap bytes**. The expanded KEYS window
uses **15 additional heap bytes** while open.

The print-note and print-cmd byte stream `HI\r\nTHERE\r\n\f` has CRC32
**6cea7f8e**; the unchanged document CRC32 is **b1bdd139**. The long-line
stream has CRC32 **bddae9d6**, and HELP > KEYS **f3f4bf42**.
The focused machine results are recorded in the commit message. The full
suite is left to the pipeline as required by this implementation run.

## Desktop shortcuts (lf-1527)

`desktop.inc` and `dsksetup.inc`, the ZX's icons on the cell grid. An
icon is a block of two by two tiles with its label on the row below,
one bit, no bevel and no shadow. The label is centred under the icon
and may be wider than it (COMMANDER is nine cells against the icon's
two), so the slot, icon and label together, is what the hit test and
the repaint go by. Six icons: NOTEPAD, CLOCK, CALENDAR, COMMANDER and
SETTINGS down the left at column 1, rows 2, 6, 10, 14 and 18, and
ABOUT beside SETTINGS at (10, 18), as SETUP sat beside ABOUT on the ZX.
The four shapes are the ZX's bitmaps (document, clock, calendar,
drawer) as tiles $88-$97; SETTINGS and ABOUT share the document, as
they did there.

A press and release without movement opens; a press and a move of
three pixels or more drags the icon, in whole cells, keeping the cell
of the press within the slot, clamped so the whole slot stays on the
desktop. The release after a drag opens nothing. An entry carries a
routine (`NoteMenuNew`, `ClockOpen`, `WndOpenCal`, `CommanderOpen`,
`SetOpen`, `AboutOpen`), not an application index. Icons lie under the
windows: `DrawDesktopRows` paints the lattice for the pending row range
and then every present icon's rows within that range, and the windows
are blitted on top as before. A row outside the range is not touched,
so a window dragged over an icon and away leaves the name table as it
was, which `dsk-under` asserts byte for byte.

**VIEW > DESKTOP** and **GRAPH+D** open the DESKTOP window: six rows of
label and [ON  ]/[OFF ], then [SAVE] and [DONE], with the SETTINGS
keys (TAB and SHIFT+arrows move the focus, ENTER, SPACE, LEFT and
RIGHT act, ESC closes) and a click on a button. A toggle repaints the
icon's rows under the window at once; SAVE writes the SETTINGS record.
HELP > KEYS lists D DESKTOP.

Which icons are present and where they sit are the three bytes an
entry that follow the eight settings bytes in the **version 5**
SETTINGS record, so they are saved and loaded with the rest and a
desktop a person has arranged comes back. Version 1 to 4 files load
with the default icons; a version 5 record is clamped on load (present
to 0 or 1, the position so the slot fits). The label, the action and
the tile are constants in ROM; the ZX's pack and unpack went with the
split.

`./test.sh --desktop` (in the default suite):

| subject | asserts |
|---|---|
| dsk-init | a fresh boot: name table = the compositor with six icons, crc32 169916c1 on C-BIOS; the table in RAM = the defaults; the 128 tile bytes at $0440 in all three thirds; 0 dropped |
| dsk-open, dsk-open-label | a press and release on CLOCK's tile, and on its label: the clock window in front, crc32 7212e959 |
| dsk-open-drag | pressed and dragged one cell: no window, CLOCK at (2, 6) |
| dsk-drag, dsk-drag-frames | dragged five cells right: table (1, 6, 6), name table = the compositor with the icon there and lattice in the old slot, crc32 448ef56f; 0 dropped |
| dsk-drag-clamp | dragged off the bottom left corner: (0, 20) |
| dsk-over, dsk-under, dsk-under-frames | ABOUT dragged to (0, 10) over CALENDAR and COMMANDER (crc32 36155273) and back to (8, 12): name table identical to before the drag, 0ca883d3; 0 dropped |
| dsk-panel, dsk-panel-key | VIEW > DESKTOP and GRAPH+D: the window at (10, 5), crc32 f832251e |
| dsk-toggle-mouse, dsk-toggle-key | CLOCK off by a click and by TAB, ENTER: the table and the screen without it, 6bb5dd27 |
| dsk-setup | then SAVE: the 26-byte record on the device, `4D 05 01 00 bb 01 01 01` and the icons, CLOCK's present byte 0 |
| dsk-reload | the table reset to the defaults and the header cleared, then the real SetLoad, SetApply and a repaint in place of the next key: CLOCK stays off, from the file |
| dsk-reload-v4 | the same with an eight-byte version 4 file: the six default icons again |
| dsk-boot-saved, dsk-boot-v4 | on the disk machine, real reboots on a seeded version 5 and a version 4 SETTINGS |

The settings subjects carry the icons too: every SAVE compares all 26
bytes, the seeded boots include version 5 records with icons moved,
off, all zero and all 255 (clamped to (1, 30, 20)), and short and long
ones, each with the name table it should give; the TEST build clamps
255 in every icon byte through `SetApply`. The window subjects compose
over the icon desktop, so every window test now also checks that the
icons stay under the windows.

Measured against `8118c5d`: the normal ROM goes from 15,870 to
**17,166 bytes (+1,296)**; page 1 was 514 bytes from full, so the code
now runs 782 bytes into page 2, which `Start` maps before anything
there is reached. The TEST ROM goes from 17,147 to **18,452 bytes**.
Static RAM grows **35 bytes** (18 for the table inside the SETTINGS
record, 17 of slot, drag and focus scratch), to **3,578 bytes**
(TEST **6,310**); the heap is **8,710 bytes** without a disk ROM and
**3,325** with one (TEST **5,978 / 593**). The TEST heap subject's
middle block went from 200 to 160 bytes to fit. The DESKTOP window
uses **199 heap bytes** while open (190 cells, 1 byte of focus, two
headers); HELP > KEYS grows by 15 for its extra row. Dragging an icon
allocates nothing.

The icon painter, measured with breakpoints at its entry and return
on the 60 Hz machine for the calendar open (NOTEPAD's label row, CLOCK
and CALENDAR in range): 2.2 ms as a straight port, 1.5 ms after the
rewrite that walks the tables with pointers, rejects an icon on two
compares, takes one shadow address a slot and copies the label with
LDIR. The lattice fill for the same ten rows is 1.0 ms. Both drag
subjects report **Dropped = 0** at 50 and at 60 Hz.

Focused verification (the full `test-all.sh` is the pipeline's):

| Configuration | Groups | Assertions |
|---|---|---|
| C-BIOS_MSX1_EU, 50 Hz | `--desktop`, `--arrange`, `--settings`, `--dialogs`, `--print`, `--saveas`, `--sound-only` | 17, 106, 153, 30, 18, 21, 11 |
| C-BIOS_MSX1_JP, 60 Hz | `--desktop`, `--arrange` | 17, 106 |

Roms_MSX1, Roms_MSX1 with Roms_Disk and Roms_MSX2 were not available
in this environment; the disk-only subjects (`dsk-boot-*`, the seeded
version 5 boots) run there in the pipeline.

## The bank backend (lf-1525)

`ST_BANK` (4, `bank.inc`) is a RAM disk in memory the desktop cannot
see, the way `bank.inc` on the ZX 128K used the free banks. An MSX has
two kinds of it, and `BkInit` measures both at `StInit` and keeps the
larger:

- **Source 1, hidden pages.** On a 64K MSX1 the BIOS sits in page 0 and
  this cartridge in pages 1 and 2, so 48K of the RAM is behind them.
  For each page the slot id of the first slot whose byte at a test
  address takes a write is recorded: page 3's slot first (the same
  chip), then the primary slots that are not expanded and, when page
  3's is, its secondary slots from `EXPTBL`/`SLTTBL`. An expanded slot
  other than page 3's is not probed: reaching its register means
  switching page 3 itself, which nothing in RAM can do. The first
  block of the first hidden page is left alone: a disk ROM keeps its
  RST and interrupt vectors in the first 256 bytes of page 0's RAM for
  the time it pages the BIOS out (measured on Roms_MSX1 + Roms_Disk:
  formatting that block corrupted the FAT buffer and lost files), so
  48K of hidden pages is 191 blocks, shown rounded up as 48 KB.
- **Source 2, a memory mapper.** 16K segments selected per page through
  ports $FC-$FF, which on an expansion are write-only, so nothing is
  ever read back from them. A slot is a mapper when a marker written
  with segment 1 selected leaves the one written with segment 0 in
  place; its size comes from writing every segment number into its
  segment and reading what segment 0 ends up with (a register with
  fewer bits wraps). The segments the system keeps are found with
  markers too: segment number k into every segment through page 0,
  then what pages 1 and 2 show through their own windows and what page
  3 shows in the probe byte itself. Page 0's own segment is the one
  register that cannot be told without writing it first; it is taken
  as 3 when pages 1-3 hold 2, 1, 0 (the BIOS's layout) and as 0
  otherwise, which is what a mapper nobody initialised has
  (openMSX's `MSXMemoryMapperBase::reset`, and the MSX1 BIOS never
  writes them). All four are kept out; page 2's is written back after
  every transfer.

Every transfer goes through `BkCode`, 63 bytes copied to work RAM at
init, because it puts the page the ROM sits in on the RAM slot for one
LDIR: under DI it reads `PSLOT` and the secondary register at $FFFF,
selects the page (and the segment), copies up to 256 bytes between a
page-3 buffer and the window and writes both registers back exactly.
`ENASLT` cannot do page 0 and nothing in ROM can do its own page. The
routine is a template whose operands the ROM patches: the masks and
bits for both registers, the segment, the window and the buffer. On a
plain page-3 slot $FFFF is RAM and gets its own byte back; on an
expanded one it reads inverted and the register is written as read.
Measured with breakpoints on the routine's DI and EI (`machine_info
time`): a 64 byte directory chunk **476 us**, a 256 byte block
**1,709 us** on the 50 Hz machines and **1,721 us** on the MSX2,
where the mapper window has one more OUT. The VDP holds its interrupt
until the status register is read, so a window that short loses
nothing: `bank-irq` saves a document to the bank with GRAPH+S while the
clock runs and RIGHT is held and asserts **Dropped = 0** on both C-BIOS
machines, the clock's seconds equal to the interrupt model and the
pointer moved by the ramp for the frames held. The first save of a
document goes through the name dialogue, whose closing frame is
**19.5 ms** on RAM at 50 Hz (**21.4 ms** at 60 Hz) and 22.4 ms with the
bank's 3.7 ms of transfers in it, and opening the clock is 18.6 ms:
those frames drop with or without the bank at 60 Hz, so the subject
counts from the held key on. The Commander's 30 by 12 repaint is a
20 ms frame on its own (24.9 ms opening it), the same story.

The disk is 256 byte blocks: one summary block per 256 (a byte per
entry, the name's hash, 0 while free), then a directory block per
seventeen of the rest (sixteen byte entries in the RAM backend's
layout: name, size, used) and a file per block left, 256 bytes at most
like the RAM backend. Both are read in 64 byte chunks through one
cache and written through. A name is looked up, a free entry found and
the directory counted on the summary, 64 entries a transfer, and an
entry is only read when its hash matches: with 4 entries a transfer
filling the 512K bank's 1,859 files took over 650 s of emulated time
(the TEST run on 180 files alone took 11 s), which is also what a save
on a full bank would have cost in frames. A free-entry hint keeps
filling linear. Capabilities `WRITE|RANDOM|DIR`, not `PERSIST`.

| machine | source | bank |
|---|---|---|
| C-BIOS_MSX1_EU/JP, Roms_MSX1, + Roms_Disk | 1: pages 0-2 of slot 3, less the first block | 191 blocks (48 KB rounded up), 178 files |
| Roms_MSX2 | 2: slot 3-2, 128K, segments 3,2,1,0 kept out | 64 KB, 240 files |
| Roms_MSX1 + Mapper512 | 2: slot 1, 512K, segment 0 under every page | 496 KB, 1,859 files |

Found on the way, on Roms_MSX1 + Roms_Disk: the H.TIMI hook was a
`JP` into page 1 of this cartridge, but the BDOS pages the disk ROM
into page 1 for the length of a call and its driver enables
interrupts while it waits, so a frame interrupt in that window ran
the disk ROM's bytes at `IrqTick`'s address (`ld (hl),c`, HL on the
kernel's directory buffer: a file's first byte became 0 and it was
gone). Whether a frame fell in the window was a matter of when the
call started: the bank's 50 ms of detection moved it there, and a
40 ms delay before the TEST storage subject did the same on `main`
(`store-dir` 3 files, `del` lost). The handler now runs from 8 bytes
of work RAM (`IrqCode`), valid whatever sits in page 1.

`harness/extensions/Mapper512.xml` is openMSX's own 512K mapper
cartridge under a name the harness can read as a debuggable; on
Roms_MSX1 the BIOS puts page 3 on it (slot 1, `PSLOT` $68), so the 64K
in slot 3 is hidden entirely and source 2 wins. openMSX's `MapperIO`
debuggable shows random bits for registers never written, so the
harness finds page 3's segment by looking for the work RAM's marker in
the dumped device instead. `test-all.sh` adds `--bank` on that
configuration after the five machines.

**SETTINGS > BACKEND** goes round RAM, BANK and DISK, those that are
there, in both directions; BANK shows its size after the label:
`BACKEND 48 KB   [BANK]`. A 16K or 32K MSX1 has no hidden pages and
no BANK (a 32K machine's page 2 RAM is behind the cartridge too, 16 KB,
not measured here). In the Commander **B** takes the other pane round
the same ring (from PRN to RAM), so RAM, DISK and BANK copy both ways;
the help row reads `TAB PANE P PRN B BANK R LIST`. A document saved on
the bank keeps it as its backend, as with RAM.

`./test.sh --bank` runs, on every machine: `bank-detect` (source,
blocks, page and slot tables or the kept-out segments and the mapper's
slot, against the machine), `bank-format` (geometry, no entry used),
`bank-irq`, the Commander ring, copies RAM/DISK -> BANK -> RAM/DISK
with the bytes read out of the bank device and back in the source,
`bank-note` (SETTINGS to BANK, type, save, edit, close and discard,
FILE > OPEN: the document is the saved one, in the bank), and
`bank-picker`. The TEST build's `bank-sys` writes a hundred 256 byte
files with interrupts off throughout (the routine's EI is a NOP for the
duration) and the harness snapshots $F380-$FFFF and the whole ROM at
`TbsStart` and `TbsEnd`: identical, and the ROM equals the image.
`bank-rw` round trips 64 bytes, fills the bank until `STERR_FULL`
(**77** files after the 101 before it on 48 KB, 139 on 64 KB, 1,758
on 496 KB: 412 s of emulated time, the whole TEST run), counts the
directory (to 255, the index's width), deletes `F0002`, sees `NEW`
take entry 103 and reads a full file back; `bank-dir` parses the
dumped device itself: every entry, summary byte and data block. Every
bank file's bytes are read from the openMSX device (`Main RAM`,
`Mapper512`), not through the ROM.

Measured against `8118c5d`: normal ROM **15,870 -> 18,006 bytes
(+2,136)**, now into page 2 (`$4000-$8656`); TEST ROM **17,147 ->
19,731**. Static RAM **3,543 -> 3,716 bytes (+173)**: the 63 byte
routine, 64 bytes of chunk cache, 46 bytes of geometry and state;
heap **8,572** bytes without a disk ROM, **3,187** with one (TEST
**5,840 / 455**). The TEST build's heap subject allocates 160, 120 and
96 bytes now, what fits behind the disk ROM. The bank allocates no
heap. Commander and notepad buffers are unchanged.

## lf-1526: Notepad selection

SELECT toggles marking at the caret; holding it does not repeat the
toggle. While marking, SHIFT+arrows extend the selection with KEY PTR ON,
and plain arrows extend it with KEY PTR OFF. Mouse press places the caret,
drag selects whole cells, and dragging beyond the viewport scrolls it.
SELECT again stops extending while retaining the selection. ESC or a
click without dragging clears it. Typing replaces it; BS and DEL delete
it. HELP > KEYS now includes SELECT MARK, below D DESKTOP: 22 rows,
opened at row 1, the lowest row a list of that height fits at above the
status row.

Each Notepad owns four endpoint bytes: anchor X/Y and exclusive end X/Y
in document chunks. Anchor X bit 7 enables marking; anchor Y of $FF means
no selection. Continuation chunks remain part of the same logical line.
Selection uses the existing inverse bank, only in the front window.
Focus changes recompose Notepad content so the background loses inversion
without losing its selection. Mouse composition and desktop repaint use
successive frames; all output still goes through the shadow nametable.

`./test.sh --note-selection` adds 17 assertions, also included in the
pipeline's default suite. They compare memory and compositor bytes for
keyboard/mouse equivalence, both KEY PTR modes, SELECT toggling and repeat
suppression, replacement, BS/DEL, ESC/click clearing, per-instance focus,
reverse selection across chunks, three-line deletion and drag scrolling.
The three-line deletion compares all 256 document bytes.

Focused verification, rebased on `40b7e5a` (the icons of lf-1527 lie
under every window, so the screen checksums include them; the bank
backend of lf-1525 moved the RAM layout again):

| Configuration | Group | Assertions |
|---|---|---:|
| C-BIOS_MSX1_EU, 50 Hz | `--note-selection`, `--print` | 17, 18 |
| C-BIOS_MSX1_JP, 60 Hz | `--note-selection` | 17 |
| Roms_MSX1 + Roms_Disk | `--settings` (the TEST build's heap subject) | 170 |
| Roms_MSX1 + Mapper512 | `--bank` | 20 |

On the earlier base `0ed9256` the pipeline's full `test-all.sh` passed
on C-BIOS EU, C-BIOS JP and Roms_MSX1 and failed only `heap-stat` on
Roms_MSX1 + Roms_Disk, which the bank backend's smaller heap subject
(160, 120, 96 bytes) now covers: on this base the three blocks fill
the disk machine's 388 byte TEST heap exactly, with nothing to split
off, and the stats agree with the formula (after the allocations 0,
after the free 120, after the free by owner 224). The next ticket that
adds static RAM will have to shrink that subject again.

Keyboard and mouse selection both have nametable CRC32 **4f499791**.
Replacement with X gives document CRC32 **fb9f7a1f**; BS/DEL give
**3bc1feef**; deletion across three logical lines including a continuation
chunk gives **cb065de7**. Reverse chunk selection has screen CRC32
**96518af1** and, typed over, document CRC32 **27fb7f93**. HELP > KEYS
has CRC32 **6f18e610**.

The drag-scroll subject measures **Dropped = 0** on both frequencies for
the drag alone: the counter is zeroed after the typed preamble. Measured
with breakpoints at `FrameWatch` and `MainLoop` on the 60 Hz machine, a
typed key frame with the icons under the window is 15.7 to 15.9 ms on
`0ed9256` and 16.0 to 16.3 ms here (state write-back and row marking
2.7 ms, lattice 0.9 ms, icon rows 1.6 ms, the Notepad compose 6.5 ms,
the blit 1.6 ms), and both drop 11 of the preamble's 23 key frames at
60 Hz; that cost is main's, not this change's, and is noted for the
owner. The drag's own frames are at most 13.2 ms. This is not a claim
about other editing operations or window sizes.

The mouse capture byte is cleared at `Start`, with `DskDrag`: openMSX
fills RAM with a pattern, and on the rebased layout the byte came up set,
so the caret followed the idle pointer until the first release, which
reversed the typed text in the `--print` subjects.

Measured against `40b7e5a`: normal ROM **19,318 → 19,833 bytes (+515)**,
TEST **21,055 → 21,570 (+515)**; both run on into page 2, which `Start`
maps. Static RAM grows **20 bytes** (four live state bytes, one mouse
capture byte, fifteen paint scratch bytes), **3,759 → 3,779** normal and
**6,491 → 6,511** TEST. The heap falls by 20: **8,529 → 8,509** bytes
without a disk ROM, **3,144 → 3,124** with one (TEST **5,797 → 5,777 /
412 → 392**). Each Notepad's existing state allocation grows **4
bytes**, from **278 to 282**, with no new allocation. The KEYS window at
22 rows uses **15 more heap bytes** than main's 21 while it is open.
Padded ROM CRC32: normal **ae86c09a**, TEST **ed042bf7** (main: a3e64a62, 8ee3ab2d).

The full six-configuration `test-all.sh` is left to the pipeline, as
required by this implementation run's focused-tests-only instruction.
No real-ROM matrix results are claimed here.
