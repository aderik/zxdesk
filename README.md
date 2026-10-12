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
| menu-open-1/5 | MSX DESK at the left edge and HELP, whose drop is nudged in from the right edge |
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
window that says which icons are on the desktop. E, Y and P are CUT,
COPY and PASTE (see "Clipboard and the EDIT menu"), U is UNDO (see "Undo").
HELP > KEYS opens the list, with the calendar's keys and the notepad's
HOME (the start of the screen row with WRAP), as a window of two
columns, 28 by 16 at (2, 4): nineteen GRAPH letters no longer fit the
desktop's 22 rows in one.
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
KEY PTR (OFF/ON), LATTICE (NONE/DOTS/GRID) and WRAP (OFF/ON, the
notepad's word wrap, see "Word wrap"). LATTICE defaults to
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
`SetLoad` then `SetApply`. The record is 27 bytes, `4D 06 rr yy bb ss kk ll`
followed by the six desktop icons at three bytes each and the WRAP byte: MSX
magic, format version, ramp, Y inversion, storage id (1 RAM, 2 tape with
`TAPE=1`, 5 disk), sound and key pointer (both 0 OFF, 1 ON), lattice (0 NONE, 1 DOTS,
2 GRID), then for each icon present (0/1), cell column and cell row
(see "Desktop shortcuts"), then word wrap (0 OFF, 1 ON).
The MSX magic is distinct from ZX settings. Defaults are ramp 1,
inversion 0, sound 1, key pointer 1, lattice 1, the detected boot backend
and the six icons down the left. Missing
files, I/O failures, incorrect magic/version and incorrect lengths give defaults. Version 1
five-byte records migrate with sound and key pointer ON; version 2 six-byte
records retain sound and migrate with key pointer ON; version 3 seven-byte
records retain both fields and migrate with lattice
DOTS. Versions 1 and 2 also default lattice to DOTS. Version 4 is the
eight-byte record without the icons; versions 1 to 4 all load with the
default icons. Version 5 is the 26-byte record without the WRAP byte
and loads with WRAP on, its icons kept; a present byte other than 0
becomes 1 and a position is clamped so the whole slot is on the desktop.
Version 6 requires 27 bytes and clamps a WRAP byte other than 0 to 1;
versions 1 to 5 all load with WRAP on.
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

## Word wrap

SETTINGS > WRAP, on by default (lf-1529, finished in lf-1541). The notepad
lays a logical line out as screen rows of at most W = WinW-2 cells, broken
at the last space that fits (the space is not shown) or hard at W when a
word is longer; a hard line end is still a new row. It is display only:
the document stays sixteen chunks of fourteen characters with the
continuation bytes, and a resize or TILE lays the same text out again.
UP and DOWN move a screen row, keeping the column where the row is long
enough; HOME goes to the start of the screen row (without WRAP, of the
logical line). The scroll bar counts screen rows. Trailing spaces are not
laid out, except that the caret's line is laid out as far as the caret, so
a caret typed past a space has a row to be shown in. With WRAP off the
display is the horizontal scroll of lf-1219, unchanged; the `--note-width`
subjects run with WRAP off.

The layout is a table of a byte a screen row (length, a bit for the space
consumed after it, a bit for the end of the logical line), 128 entries at
most: 224 characters at three a row plus sixteen line ends. The table and
the job's scratch live in the tape buffer, which is only in use between
`TapeOpen` and the close of the same synchronous transfer; `TapeOpen`
invalidates the table. A job fills the table, resumable with a budget a
frame (`NWBUDGET`, in units of about 45 cycles: a character scanned, 12 a
row, 20 a line); an edit within a line lays out that line alone, the rows
after it parked at the end of the table meanwhile, and the table does not
depend on the caret, so a cursor key costs no layout. `NoteLayService`,
first thing in `WinRedraw`, runs the job for one notepad a frame, composes
that window when the job is done and lets the repaint go ahead only in a
frame that did little else, the way `WndOpenLater` splits an open: a key
in a full line is key and layout in one frame, composition in the next,
desktop and blit in the third. The painter and the caret walk the table
with the row's document address and chunk offset in registers.

The first version laid out and painted a full document in four columns in
34 ms a draw. Measured on C-BIOS EU with breakpoints on the loop (the
figures are the whole loop iteration, the interrupt handler on top of
them):

| frame | ms |
|---|---|
| notepad open with WRAP (FILE > NEW): the job's first 300 units | 10.9 |
| a key in a 224-character line: the insert and half a job step | 13.3 |
| the composition after it, 17 rows, and its blit | 13.0 |
| a cursor key in the full document at 6x4, 57 rows: compose and blit | 14.5 |
| SETTINGS opened over the notepad | 13.7 |
| WRAP toggled in SETTINGS over a notepad: the notepad's layout | 13.5 |

`NoteDrawWrap` is 5.0 ms for 57 rows at 6x4 after a resize (the walk to
the top row, 3.2 ms, is kept for the next draw of the same table) and
3.0 ms for 17 rows of fourteen (seven in view). The 60 Hz machine drops
a frame above about 15 ms. `./test.sh --wrap` asserts the document, the
name table against the compositor fed by `wrap_layout`, an independent
Python layout, the caret, the row counts, `NoteTop` and `Dropped = 0` in
every subject, on both C-BIOS machines: wrap-basic (thirty characters in
fourteen columns, crc32 f41353a5), wrap-long (a word of twenty, hard at
fourteen, 409dd39c), wrap-resize (16x9 to 10x9, 0cbfd0ce, and back to the
same f41353a5 with the 256 document bytes unchanged), up, down and HOME
over the broken line, WRAP off (8d4e5216, the lf-1219 display) and the
toggle both ways with the notepad open, the saved record, and the full
document in four columns (57 rows, 64bf9e50).

Two things the toggle brought out. Without WRAP, `NoteDraw` at the home
size paints only the chunks that reach into the view, and a chunk wholly
left of it, or the cells past the last chunk, kept whatever the buffer
held (lf-1542): it blanks the interior first now. And a repaint of two
windows selected every window for its blit, two state copies of 1.7 ms
for a notepad behind SETTINGS, so a TAB in SETTINGS measured 18.9 ms;
`WinRedraw` now blits a window that is not recomposed from its record
(`WndBlitSlot`) and flips a title row that changes hands in place
(`WndTouchCap`) instead of loading its state to compose the row again.
SETTINGS itself recomposed its whole window for every TAB and every
value (sixteen prints, 4.5 ms), which with a notepad behind it made a
14.9 ms frame that the 60 Hz machine dropped: a focus move now flips the
two buttons' cells in place and a value change prints its button again
where it is (`SetDrawRow`), the application returning 2 to `HdlKey` for
"rows to blit, nothing to recompose"; other SETTINGS windows are still
marked. The WRAP toggle skips `SetApply`, which has nothing to apply.

With the selection (lf-1526) merged in: a selection shows inverted in
the wrapped rows too, both parts when it crosses a break point
(`wrap-select`, SELECT at the end of the thirty characters and two rows
up), the caret is not shown over one, and a mouse drag maps the pointer
through the row table (`NoteMouseWrap`), the row below the view scrolling
a screen row a frame. The subjects that select over a line scrolled
sideways (`sel-chunks`, `sel-lines`) run with WRAP off. A notepad with a
selection that loses or gains the front is recomposed (only the front
instance shows it); one without is flipped like any other window. An
edit key leaves the repaint to the next frame: ENTER into a full line is
5 ms before any layout.

With the clipboard (lf-1545) merged in: CUT and PASTE change the text
outside NoteKey, so `NoteRefresh` invalidates the table with WRAP on and
leaves the layout and the composition to the service, a frame at a
time, instead of composing in the frame of the paste.

The SETTINGS menu pick over a notepad, which composes the window in
the frame of the open (`WndOpenLater`) after the 2 ms state save of the
notepad that loses the live slot, measured 14.9 ms with the EDIT menu
in: SetDraw's seven labels are one block in ROM copied into the rows
(2.2 ms of prints to 0.6), and the hit test flushes the records alone
(`WndFlushRec`), for 14.3 ms.

Budget: the normal ROM is 23,347 bytes, static RAM 4,014 bytes (+8
against main: the six wrap flags, SetWrap and RowsDesk), the heap 8,274
bytes without a disk ROM and 2,889 with one. The TEST build leaves 157
bytes behind the disk ROM, 21 of them after the heap subject's 48, 40
and 32 byte blocks. The table takes no heap and no RAM of its own.

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

## Frame budget at 60 Hz (lf-1583)

The frame observations of lf-1548, lf-1535 (point 2), lf-1556, lf-1562,
lf-1567, lf-1581 and lf-1582, measured on main `7038703` and on this
change. A loop iteration is timed with breakpoints at `FrameWatch` (the
wake after HALT) and at the next `MainLoop` (`machine_info time`); the
BIOS interrupt handler comes on top, so on C-BIOS JP an iteration of
15.2 to 15.9 ms drops a frame or not depending on where it starts, and
14.8 ms never did. Each route is booted fresh; "max" is the longest
iteration from the scenario's first key or press on (the windows it
starts from opened before that), `Dropped` counts from boot.

| scenario (source) | main EU max / Dropped | main JP | now EU | now JP |
|---|---|---|---|---|
| HELLO WORLD typed over the icons and the drag, `sel-scroll` (lf-1548) | 13.2 ms / 0 | 13.2 / 0 | 13.2 / 0 | 13.2 / 0 |
| HELLO WORLD typed with a second notepad open (lf-1548) | 11.2 / 0 | 11.2 / 0 | 11.2 / 0 | 11.2 / 0 |
| GRAPH+X twice between notepads of 24x17 and 22x16 (lf-1548) | 22.4 / 2 | 20.6 / 2 | 11.0 / 0 | 11.0 / 0 |
| GRAPH+X twice, a 24x17 notepad and the clock (GRAPH+K) (lf-1535) | 16.2 / 0 | 16.4 / 2 | 11.3 / 0 | 11.3 / 0 |
| GRAPH+X in the release frame of a grow to 24x17, the clock in front (lf-1535) | 15.1 / 0 | 15.2 / 1 | 12.9 / 0 | 12.9 / 0 |
| SAVE AS on RAM: GRAPH+S, LETTER, ENTER (lf-1556) | 17.3 / 0 | 19.3 / 1 | 10.7 / 0 | 10.7 / 0 |
| `undo-open-save` on RAM: FILE > OPEN BETA, GRAPH+S, GRAPH+U (lf-1562) | 30.4 / 2 | 30.4 / 3 | 30.4 / 2 | 30.4 / 3 |
| ENTER after a line of 13, WRAP on (lf-1567) | 17.1 / 0 | 17.3 / 1 | 11.9 / 0 | 11.9 / 0 |
| ENTER after a line of 14, WRAP on (`wrap-full-enter`) | 14.8 / 0 | 14.8 / 0 | 11.4 / 0 | 11.4 / 0 |
| AB, ENTER, CDE, ENTER with WRAP off | 18.0 / 0 | 18.2 / 2 | 9.9 / 0 | 9.9 / 0 |
| eight lines of A + ENTER with WRAP off, the grip to 6x4 and to 22x17 (lf-1581) | 17.5 / 0 | 19.1 / 11 to 12 | 11.6 / 0 | 11.6 / 0 |
| BS at the start of a line of 19 after one of 19, WRAP on (lf-1582) | 18.5 / 0 | 18.7 to 20.5 / 1 | 11.6 / 0 | 11.6 / 0 |
| DEL at the end of the first of those lines (lf-1582) | 18.9 / 0 | 19.1 / 1 | 12.0 / 0 | 12.0 / 0 |
| selection extended at 24x17: HELLO WORLD, SELECT, 5 SHIFT+LEFT, 2 SHIFT+DOWN, 2 SHIFT+UP, WRAP on (`fb-select`, lf-1591, main `3393724`) | 17.6 / 0 | 19.4 / 10 | 7.7 / 0 | 7.7 / 0 |
| the same with WRAP off, typed at the home size (`fb-select-wrap-off`, lf-1591) | 26.9 / 10 | 26.8 / 10 | 9.3 / 0 | 9.3 / 0 |

The grip of lf-1581 itself was 15.1 to 15.3 ms at 60 Hz (dropped one in
two runs); the eleven drops on main are its typed preamble. The join of
lf-1582 had never been measured: `NbHard` took **10.3 ms** of that
frame on main and takes **3.4 ms** now; `NoteEnter` after 13 characters
**8.0 → 2.8 ms**; `NoteUndoSaved` in the save **4.6 → 0.6 ms**.

What changed, each measured on its own route:

- **One pass for a delete of N characters** (`NoteDeleteN`): ENTER
  removed the characters before the caret from the new line and a join
  removed the padding of the joined chunk with one `NoteDeleteAt` a
  character, each a pass over the rest of the line.
- **Row moves by one LDDR/LDIR** (`NoteOpenRow`, `NoteCloseRow`): a row
  at a time with two `NoteRowAddr` calls was 2 ms of an ENTER near the
  top.
- **WRAP off: an edit's frame paints nothing, the next paints it all**,
  as WRAP on already split it (`NoteLayService`, `NwKeyed` 2). A typed
  key was 15.3 to 15.9 ms, ENTER 16.7 to 19.2.
- **The chunk painter** (`NotePaintChunk`, both WRAP off): WinDraw
  leaves the interior blank, so a blank chunk is not painted and the
  others are copied by LDIR instead of `WinPrint`'s 60 T a cell, unless
  a selection is shown.
- **A focus change repaints no desktop and waits a frame**
  (`WndFocusTo`): nothing moves, so the rows of the two windows are
  blitted again back to front, without the desktop and its icons (4.2
  ms at 24x17), in the frame after the select (two state copies, 4.1
  ms). `RepaintDue` 2 is now checked at the top of `WinRedraw`, before
  the resize compose and the wrap layout, so "paint next frame" holds
  for all of them; `DgOpen` still settles a pending change at once.
- **The resize compose puts the front back live** (`WrdCompose`): the
  repaint's select of it was 2.2 ms of a 15.1 ms frame after GRAPH+X in
  the release frame.
- **The save**: the snapshot's compare stops at the first difference,
  and the compose and repaint after the write are the next frame's
  (`RepaintLater`).

`./test.sh --frames` (in the default suite), each from boot with
`Dropped` asserted next to the result:

| subject | asserts |
|---|---|
| fb-focus-24, fb-focus-24-back | two notepads grown to 24x17 and 22x16, GRAPH+X once and twice: z order, the live window, the screen against the compositor, Dropped 0 |
| fb-focus-clock | a 24x17 notepad, GRAPH+K, GRAPH+X: the notepad in front over the clock as composed, Dropped 0 |
| fb-edit-menu | a 24x17 notepad, EDIT opened by the mouse (lf-1549): the drop with its four items grey over the notepad as composed, Dropped 0 |
| fb-save | SAVE AS on RAM: the file LETTER is HI byte for byte, the name, unmodified, Dropped 0; with the disk Dropped is 0 up to the ENTER and after the write |
| fb-join-bs, fb-join-del | the BS and DEL joins of two lines of 19 with WRAP on: the 256 bytes, the caret (5, 1), the screen against `wrap_win`, Dropped 0 |
| fb-open | FILE > OPEN BETA, GRAPH+S, GRAPH+U (lf-1562): HI under NOTE again; on RAM Dropped 0 over the whole route, the picker included (lf-1590); on the disk 0 before the OPEN and from the frame after the save |
| fb-select, fb-select-wrap-off | a 24x17 notepad, HELLO WORLD, SELECT, five SHIFT+LEFT, two SHIFT+DOWN and two SHIFT+UP (lf-1591): the screen against the compositor with the selection inverted (with WRAP on the end goes down the empty lines to (0, 0): the whole line; with it off it keeps column 6 and comes back to WORLD), the document, anchor and end, Dropped 0 from boot, the longest frame from SELECT on in the detail |
| fb-type-24 | a 24x17 notepad with WRAP on, eight lines of A and ENTER, then A (lf-1596): the screen against `wrap_win`, the document, Dropped 0 from boot; the eighth line's frames are 14.3 and 14.6 ms at 60 Hz, the painter's limit |
| fb-select-past-end, fb-select-past-end-up | HELLO WORLD and FOO BAR (and BAZ) typed at the home size, the grip to 24x17, SELECT, SHIFT+RIGHT past the line's end and SHIFT+DOWN (SHIFT+UP) (lf-1596): the screen with the selection inverted, the document, anchor and end, Dropped 0 from boot, the longest frame from SELECT on |
| fb-minute-note, fb-minute-clock, fb-minute-both | a minute's roll (ClkS set to 59, 90 frames) with a 24x17 notepad open, with a clock alone (GRAPH+K) and with the clock behind the notepad (lf-1601): Dropped before and after the roll both 0, 12:01 counted, every clock window recomposed with the new minute and the notepad as it was, the longest frame from the roll on in the detail |
| fb-caret-24, fb-type-24-16, fb-type-24-full, fb-esc-24, fb-sel-letter-24 | at 24x17 with WRAP on (lf-1597): two SHIFT+UP and eleven SHIFT+LEFT without marking in the full document; sixteen lines of A and ENTER; eight lines of 27 characters typed in the grown window; SELECT, three SHIFT+UP, ESC, HOME and X in the full document; WORLD selected after seven full lines, X and Y: the screen against `wrap_win`, the document, the caret, the top, no selection, Dropped read before the keys and at the end both 0, the longest frame from the mark in the detail |

And elsewhere: `sel-scroll` and the `wrap-full-enter` subjects no longer
zero `Dropped`; `wrap-13-enter` (13 characters, ENTER, X) and
`wrap-off-regrow-max` (lf-1581: 22x17, the screen, Dropped 0) are new;
`wrap-off-shrink-regrow`, `resize-key-next`/`-close` and the `grow-off`
subjects count the whole route too.

**Known exceptions**, measured and not in one frame:

- **Disk I/O.** On Roms_MSX1 + Roms_Disk a save of a notepad is one loop
  iteration of **6.43 s** (Dropped 302 to 304) and the route of
  `undo-open-save` drops **549**: the picker's open 55 ms, a move 52 ms,
  the load **1.25 s**, the save **10.0 s**. The BDOS call does not
  return within a frame and nothing here can split it.
- FILE > OPEN on RAM (the picker's open **32.3 ms**, each SHIFT+DOWN
  in it **21.4 ms**, the load with ENTER **27.0 ms**: `Dropped` 2 at
  50 Hz and 3 at 60 Hz for the route) and the clock opened from the
  VIEW menu over a 24x17 notepad (**17.4 ms** at 60 Hz, one dropped)
  were exceptions here; lf-1590 below put them in the budget.

Budget against `7038703`: normal ROM **24,420 → 24,572 bytes (+152)**,
TEST ROM **26,177 → 26,329 (+152)**; static RAM **4,019 → 4,018 (-1)**,
TEST **6,751 → 6,750** (`NoteRowDst` is gone); heap **8,269 → 8,270**
without a disk ROM and **2,884 → 2,885** with one (TEST 5,537 → 5,538
and 152 → 153). No new allocation. Padded ROM crc32: normal
**b7a86a4b**, TEST **fedafa4d**. `./test.sh`: C-BIOS_MSX1_EU 721 ok,
C-BIOS_MSX1_JP 711 ok, all subjects pass.

## The selection extended with SHIFT+arrow at 24x17 (lf-1591)

dev3's report on lf-1549: FILE > NEW, the grip to 24x17, HELLO WORLD,
SELECT, five SHIFT+LEFT drops 5 to 6 frames at 60 Hz, 17.2 to 19.1 ms
an extension frame, on `d5bf146` and `3393724` alike. A key while
marking went the way of every cursor key: `NoteKey` returned 1, HdlKey
touched the window, and the frame composed and blitted all seventeen
rows although the inverse changes on the rows between the old end and
the new one only. With WRAP off it was worse: as soon as anything was
selected every chunk, blank ones too, was copied through
`NotePaintSelection` and `WinPrint` at 0.7 ms a chunk, 11 ms of the
compose.

Measured as in the table above (the loop iteration at `FrameWatch`
and `MainLoop`; "longest" from SELECT on, `Dropped` for the phase
unless the subject counts from boot), on `3393724` and on this change:

| scenario | main EU longest / Dropped | main JP | now EU | now JP |
|---|---|---|---|---|
| `fb-select`: HELLO WORLD at 24x17, SELECT, 5 SHIFT+LEFT, 2 SHIFT+DOWN, 2 SHIFT+UP, WRAP on (Dropped from boot) | 17.6 / 0 | 19.4 / 10 | 7.7 / 0 | 7.7 / 0 |
| `fb-select-wrap-off`: the same with WRAP off | 26.9 / 10 | 26.8 / 10 | 9.3 / 0 | 9.3 / 0 |
| sixteen lines of a letter at 24x17, SELECT, fourteen SHIFT+UP: the extension frames, WRAP on | 21.9 to 23.1 / 14 | 20.2 to 22.9 / 14 | 7.6 to 10.8 / 0 | 7.6 to 10.8 / 0 |
| the fifteenth SHIFT+UP, which scrolls the view: the compose frame, WRAP on | 19.7 / 0 | 19.9 / 1 | 13.1 / 0 | 13.1 / 0 |
| the fourteen extension frames with WRAP off | 27.4 to 29.1 / 14 | 25.7 to 29.0 / 14 | 9.3 to 11.1 / 0 | 9.3 to 11.1 / 0 |
| the scrolling SHIFT+UP with WRAP off: the compose frame | 26.6 / 1 | 24.8 / 1 | 20.5 / 1 | 20.5 / 1 |

What changed:

- **The row route** (`NoteEndRowSave`, `NoteEndMoved`, `NoteDrawRows`):
  before a key while marking, the end's row (the table's with WRAP, the
  line without); after the move, when the top (and with WRAP off the
  left column) stays, the rows of the view from the old end's to the
  new one's are blanked and painted into the buffer by the same
  painters, marked with `RowsAddText`, and `NoteKey` returns 2: no
  recompose, the blit and the flush take those rows only. The painters
  take a row range (`NdRowLo`, `NdRowHi`, `NdRowsOnly` in the tape
  buffer scratch): `NotePaintChunk` and `NotePaintAddr` skip the other
  rows, `NwPaint` steps over them without touching the buffer and stops
  after the last. The mouse drag (`NoteMouseFrame`) takes the same
  route; a drag past the edge scrolls and composes as before. A SELECT
  that changes nothing on the screen (marking off, or on from the caret
  with nothing selected) returns 2 too: its compose dropped one at JP.
- **The scroll case** (`NoteRecomposeSplit`, `NwKeyed` 3): the key's
  frame paints nothing, the next composes, the one after blits, with
  WRAP on and off alike (`NoteLayService` composes a WRAP-off window
  for it as the toggle does). With WRAP on that is 13.1 ms, 0 dropped.
- **The chunk painter with a selection** (`NotePaintChunk`,
  `NpcSelected`, `NpcInvert`): a chunk without selected cells is
  painted as if nothing were selected (blank ones skipped, the others
  by LDIR); a chunk with them is copied the same way and its selected
  cells are inverted in place. `NotePaintSelection` and its 15-byte
  `NotePaintBuf` are gone. The front-window rule (the instances behind
  show no selection) lives in `NpcSelected` now. `NwpSel`, the WRAP
  painter's inverse, keeps its bounds in registers.

**Known exceptions**, measured: the compose of a 24x17 window with
WRAP off over a selection of fifteen chunks is 18.5 ms (its painting
costs are the painter's, as on main: `WinCell` is a loop of the row,
and the inverse adds 14 cells a chunk), so the scrolling key drops
one at 50 and 60 Hz, as it did with the blit in the same frame. A
typed key in a 24x17 notepad with WRAP off drops frames at 60 Hz on
main too (the compose and blit frame is 16.0 ms; `fb-select-wrap-off`
types at the home size for that reason). Both are a cheaper WRAP-off
painter, a change of its own.

Budget against `3393724`: normal ROM **24,671 → 25,084 bytes (+413)**,
TEST ROM **26,428 → 26,841 (+413)**; static RAM **4,018 → 4,003
(-15)**, TEST **6,750 → 6,735**; heap **8,270 → 8,285** without a
disk ROM and **2,885 → 2,900** with one (TEST 5,538 → 5,553 and 153 →
168). Six bytes of the tape buffer scratch (`TapeBuf+194` to `+199`)
are used by the painters' row range. Padded ROM crc32: normal
**17c8b2e8**, TEST **a3705187**.

## The painter's per-row cost, and the caret past a line's end while marking (lf-1596)

dev3's two findings on lf-1591, measured as above (the loop iteration
at `FrameWatch` and `MainLoop`, breakpoints on the routines' entries
for the split), on `79843be` (before lf-1591), `5264508` (lf-1591) and
this change, C-BIOS JP; EU drops nothing in any of them.

1. **Every WRAP-on compose was 0.4 ms slower.** `NwPaint` is the whole
   cost of a compose with WRAP on and has no headroom at 24x17: a typed
   key's frame grows with the lines, 13.0 ms at the first and 15.0 at
   the thirteenth, where the drops begin. lf-1591 tested the row range
   once a row inside the painter's loop and counted the row (a test of
   `NdRowsOnly`, a call for the cell step, a counter in memory: about
   84 cycles a row, 0.36 ms over the fifteen rows), so the same frame
   was 0.4 ms longer and the drops began at line 6. The
   loop over every row is the one of `79843be` again, and the row
   route has a loop of its own (`NwpOnly`) that counts rows and steps
   over the ones not wanted.

   | fifteen lines of A and ENTER, then A, at 24x17, WRAP on | `79843be` | `5264508` | now |
   |---|---|---|---|
   | `NwPaint` with the caret, the last A | 3.90 ms | 4.27 | 3.92 |
   | the last A's frame | 15.61 | 16.03 | 15.68 |
   | Dropped over the sixteen lines (JP) | 5 | 12 | 5 |

   The 0.07 ms left against `79843be` is the row range's reset in
   `NoteDraw` and the follow and clamp as calls. `fb-type-24` (eight
   lines, Dropped 0 from boot) fails on `5264508` with Dropped 2.

2. **SHIFT+RIGHT past a line's end, then SHIFT+DOWN or SHIFT+UP.** The
   caret among the spaces after HELLO WORLD has its line laid out as
   far as it (`NwCaretCheck`, `NwEditLine`): a job under way, so the
   row route stood back and the key composed the window (10.8 ms, 0
   dropped). The SHIFT+DOWN then left the extended line, and
   `NwCaretCheck` invalidated the whole table: a full relayout over
   three frames with the compose and the blit in the last one, 16.1 to
   16.3 ms, one dropped at 60 Hz. Now:
   - a caret that leaves a line laid out as far as it has that line
     alone laid out again, to its text (`NwEditLineAt`; `NwLineBegin`
     drops the extension of a line without the caret), unless the
     caret's own line asks for a job, which is everything again as
     before;
   - while marking, a job over one line (not over all sixteen chunks)
     runs within the key's frame (`NemWrap`), and when the table keeps
     its row count, which is when nothing but the line's own trailing
     spaces changed, the rows the end moved over are painted as if
     there had been no job. A job that adds or takes a row goes the
     split way (`NoteRecomposeSplit`) as before.

   | key at 24x17, WRAP on (frame ms / Dropped, JP) | `79843be` | `5264508` | now |
   |---|---|---|---|
   | HELLO WORLD, FOO BAR, caret at (0, 0), SELECT, the 12th SHIFT+RIGHT (into the spaces) | 17.8 / 1 | 10.8 / 0 | 8.2 / 0 |
   | the SHIFT+DOWN after it | 6.4, 7.8, 15.7 / 1 | 5.1, 7.7, 16.1 / 1 | 9.8 / 0 |
   | HELLO WORLD, FOO BAR, BAZ, SELECT at the end, SHIFT+RIGHT | 20.0 / 1 | 11.1 / 0 | 8.6 / 0 |
   | the SHIFT+UP after it | 6.4, 7.7, 15.8 / 0 or 1 | 5.0, 7.7, 16.1 / 0 or 1 | 9.7 / 0 |

   The 9.8 ms key frame is the key (2.2 ms with the two derivations),
   the line's job (1.2 ms, the parked tail copied back included), two
   rows painted (2.4 ms) and blitted (1.6 ms). The screens, documents,
   anchors and ends are byte for byte those of `79843be` in all of
   these. A plain cursor key (no marking) that leaves an extended line
   composes and blits in its frame as every cursor key does, instead
   of the three frames of the relayout.

Budget against `5264508`: normal ROM code **25,742 → 25,831 bytes
(+89)** before the page alignment of `NsanTab` (the build line stays
25,856, 25 bytes of the pad left), TEST **27,496 → 27,585 (+89)**;
static RAM **3,997**, heap **8,291** without a disk ROM and **2,906**
with one, unchanged; no new scratch. Padded ROM crc32: normal
**1d8dd026**, TEST **92eca1c3**. `./test.sh`: C-BIOS_MSX1_EU 746 ok,
C-BIOS_MSX1_JP 736 ok, all subjects pass (the three subjects above are
new; against `5264508`'s source they fail at JP with Dropped 2, 1
and 1).

## The row route in a full document (lf-1599)

The review of lf-1596: its two subjects hold two short lines, and the
route it added (a one-line job within the key's frame, the rows the
end moved over painted alone) was measured on those. With the
document full the same keys cost more, and one of them drops. Eight
lines of ABCDEFGHIJKLMNOPQRSTU VWXYZ (two chunks, two rows each) at
24x17 with WRAP on, the view scrolled by one row, SELECT at the end
of the last line, SHIFT+RIGHT into the spaces after it and SHIFT+UP
off it, measured as in the sections above on C-BIOS JP:

| key | `12e61ad` (lf-1596) | now |
|---|---|---|
| SHIFT+RIGHT into the spaces (the line's job, one row painted) | 12.7 ms / 0 dropped | 11.4 / 0 |
| SHIFT+UP off the line (the line's job, two rows painted) | **15.1 / 1** | 13.7 / 0 |
| the same keys on five lines of 39 characters (three chunks, the caret on chunks 12 to 14) | 14.5 / 0 | 13.6 / 0 |
| `fb-select-past-end`'s SHIFT+DOWN (two short lines) | 9.8 / 0 | 9.7 / 0 |

Where the 15.1 ms went, by breakpoints on the routines' entries: the
row route's painter 2.3 ms before the first row it painted, the
line-of walk 0.4 ms three times a key (`NwCaretCheck`, `NwEditLineAt`
and `NwDerive` after the job), the row-at walk of SHIFT+UP 1.0 ms,
the line's job 1.1 ms, the two rows painted 1.7 ms and blitted 1.4
ms. What changed:

- **The row route's painter** (`NwpOnly`): the rows of the view
  above `NdRowLo` are walked in registers (the walk advanced, the
  table's rows counted down) and the buffer cell is moved past them
  in one step; the every-row loop (`NwpEvery`) then paints `NdRowHi -
  NdRowLo + 1` rows and stops. The loop lf-1596 gave the route tested
  the range against both bounds, counted the row in memory and
  stepped the cell through a call for every row of the view, 150 us a
  row and 2.3 ms for the thirteen rows above the caret at the bottom
  of the view; the same rows are 0.6 ms now. `NwPRow`
  (`TapeBuf+196`) is no longer used.
- **The line-of walk** (`NwLineOf`): the stride is added to L with a
  carry into H instead of a 16-bit add around a saved DE, 0.33 ms
  instead of 0.42 for the sixteenth chunk.

The every-row loop is untouched: `fb-type-24` is 14.5 ms at JP as
before. Screens, documents, anchors and ends are those of `12e61ad`
in every scenario measured (the oracle `wrap_win` with the ROM's own
anchor and end): two-chunk lines with the caret past their end and
SHIFT+DOWN or SHIFT+UP after it, a row added past the end (the 23rd
column of a 19-character line, two chunks) and taken away again, HOME
and ESC from the spaces, sixteen one-letter lines with the view
scrolled, a plain cursor key off the extended line and a typed key
after it, a mouse click after it, and the four documents of the
table.

`fb-select-past-end-full` (`./test.sh --frames`) types the eight
lines at the home size, grips to 24x17, SELECT, SHIFT+RIGHT, SHIFT+UP,
SHIFT+DOWN, SHIFT+UP: the screen against the compositor with the
selection inverted (anchor (13, 15), end (5, 14), top 1), the
document, Dropped 0 from boot, the longest frame from SELECT on in the
detail. Against `12e61ad`'s source it fails at JP with Dropped 1.

Budget against `12e61ad`: the code before the page pad of `NsanTab`
(the last byte used, `build/msxdesk.rom`) **26,150 → 26,116 bytes
(-34)**, TEST **27,904 → 27,870 (-34)**; the build line stays
25,856 / 28,160 (page-aligned); static RAM **3,997**, heap **8,291**
without a disk ROM and **2,906** with one, unchanged; one scratch byte
freed. Padded ROM crc32: normal **922d06d3**, TEST **e7b21f14**.
`./test.sh`: C-BIOS_MSX1_EU 766 ok, C-BIOS_MSX1_JP 756 ok, all
subjects pass.

## The minute's roll with a window open (lf-1601)

Found by dev3 testing lf-1599, older than that ticket: every minute
with a window open the main loop stalled, 357 ms with a notepad and
488 with a clock alone, Dropped +18 at 50 Hz and +22 to +24 at 60,
nothing without a window. `ClkService` read `WndCount`, called
`WndFlush` and then took A as the slot count; `WndFlush` returns with
A = `WndCur`, so with the live window in slot 0 the loop ran 256
slots, `WndRecAt` on each, past `WndZ` and `WndTab` (and `WndTouch`
and `RowsAddSlot` on whatever record there happened to hold
`AppClock`). The count is read again after the flush; `WndFlush` is
as it was. A minute's roll measured by the frame probe, the longest
frame from the roll on:

| window | `3b7d9cd` | now |
|---|---|---|
| a 24x17 notepad | 337 ms / 16 dropped at EU, 344 / 20 at JP | 3.7 / 0 |
| a clock alone | 466 / 23 at EU, 467 / 27 at JP | 5.4 / 0 |
| the clock behind the notepad | 349 / 17 at EU, 352 / 21 at JP | 12.9 / 0 |

`fb-minute-note`, `fb-minute-clock` and `fb-minute-both` (`./test.sh
--frames`): the wait for the minute is bridged by writing 59 to
`ClkS`, so the next second rolls it within 60 frames; 90 frames
later Dropped is read again. Asserted: Dropped before and after the
roll both 0, the clock at 12:01, the screen against the compositor
with every clock window showing the new minute (behind the notepad
too, slot 1, not the live window) and the notepad as it was. Against
`3b7d9cd`'s source the three fail on both machines with the Dropped
in the table.

Budget against `3b7d9cd`: the code before the page pad of `NsanTab`
**26,115 → 26,118 bytes (+3)**, TEST **27,869 → 27,872 (+3)**; the
build line stays 26,624 / 28,160 (page-aligned); static RAM
**3,997**, heap **8,291** without a disk ROM and **2,906** with one,
unchanged. Padded ROM crc32: normal **891c3b63**, TEST **070c48f7**.

## The caret, the typed key and ESC at 24x17 with WRAP on (lf-1597)

dev3's observations on lf-1591 and lf-1599, the scope lf-1591 and
lf-1596 put off: those tickets gave SHIFT+arrow while marking the row
route (the rows the end moved over painted and blitted alone); every
other key in a 24x17 notepad with WRAP on still composed and blitted
the whole window in its frame. Measured as in the sections above
(C-BIOS JP, the loop iteration from `FrameWatch` to `MainLoop`,
breakpoints on the routines' entries for the split), on `0850c2d` and
on this change; EU drops nothing in these on `0850c2d`, its frames
being under 20 ms (the full document typed, measured again for
lf-1608: 17.5 ms at most, Dropped 0).

| key at 24x17, WRAP on (frame ms / Dropped, JP) | `0850c2d` | now |
|---|---|---|
| 1. two SHIFT+UP and eleven SHIFT+LEFT, no marking, after three short lines | 15.2 to 16.0 a key / 9 | 6.0 to 7.2 / 0 |
| the same in the full document (eight lines of 27, the caret on the last chunks, the view scrolled by one) | 18.6 to 21.5 / 13 | 7.1 to 9.7 / 0 |
| 2. sixteen lines of A and ENTER: the typed key | 13.1 at the first line to 15.6 at the sixteenth | 9.4 to 11.6 |
| the ENTER | 14.2 to 14.9 (and 20 ms when the rows below were painted in the frame, see below) | 12.0 to 12.5 |
| Dropped over the sixteen lines | 3 to 7 | 0 |
| eight lines of 27 characters (two chunks each): the fifteenth character going on into a new chunk | the table laid out again from the first line over three frames, the last with the compose and the blit, 13.1 to 16.3; Dropped 38 over the eight lines | the line's job in the key's frame, 8.0 to 11.7 / 0 |
| 3. ESC over a selection of three lines | 15.0 / 0 or 1 | 7.4 |
| HOME after it | 15.5 / 1 | 6.0 |
| X typed over WORLD selected (HELLO WORLD alone / after seven full lines) | 15.3 / 1 and 16 to 17 / 1 to 2 | 9.9 and 12.2 / 0 |
| ESC, HOME, X after SELECT and three SHIFT+UP in the full document | 19.4 / 3 | 13.0 / 0 |

What changed (`src/note.inc`; WRAP off keeps every route it had):

- **A caret move without marking** (`NkCaret`): `NoteEndRowSave` keeps
  the caret's row before every key with WRAP on, and the move goes
  the marking move's way with the end left alone (`NoteCaretMoved`,
  the entry of `NoteEndMoved` after its store of the end): the row
  the caret left and the one it reached are painted and marked when
  the top stays, else the window is composed the split way
  (`NoteRecomposeSplit`: nothing in the key's frame, the compose the
  next, the blit the one after) instead of composed and blitted in
  the key's frame.
- **An edit** (`NkEditSpan`, `NwEditSpan`, `NoteEditRows`): the
  table is no longer laid out again from the first line. The job over
  one line lf-1596 gave the same-links edit (`NwEditLineAt`) now
  covers a span: D lines there were from the caret's line (one more
  for every line `NbHard` joined into it, counted in `NwJoined`), E
  lines there are now (two after ENTER split it, `NwSplit`), and the
  blank lines that came or went at the document's end (an inserted
  chunk pushes the last one out, a reclaimed one comes back blank:
  the links taken away less the lines the span lost) are taken from
  the parked tail's end or added behind it, one `NW_END` entry each.
  The job runs within the key's frame; when the top stays, the
  scroll bar neither comes nor goes and the rows after the span are
  empty or as many as before, the span's rows (the old ones and the
  new, whichever reach lower) and the caret's old row are painted and
  marked and the bar's marker is moved along; a join, a shift of rows
  with text below, a scroll or the bar's coming or going compose the
  window the split way. `NwLinksSame`, for WRAP off, counts a join
  as a change instead of `NbHard` spoiling the saved links.
- **ESC over a selection** (`NkEscape`, `NoteSelRows`, `NwRowsOf`):
  one walk over the table finds the rows the selection's first and
  last cells are on (the last row that starts at or before each, as
  the painter's inverse has them); those rows and the caret's are
  painted with the caret shown again. Without a selection ESC is a
  caret move that stayed where it was.
- **A selection within one line goes in one pass**
  (`NoteEraseSelection`): the caret at the lower bound and
  `NoteDeleteN` over the characters between the bounds, the tail
  reclaimed once; a BS a character was a pass over the rest of the
  line each, 0.4 ms a character (five of them 2 ms of the X above).
  Over a line end it is deleted backwards as before.
- **The row route's painter pads** (`NoteDrawRows`): with WRAP on
  the rows are no longer blanked before they are painted; the painter
  pads them to the edge as at the home size (`WdFull`), 0.23 ms a row
  less.

**Not built, a question for the PO.** ENTER (or a character that
wraps a word onto a new row) with text below the caret shifts the
rows below it. Painted and blitted in the key's frame they were
**20.1 ms** for ENTER at the top of the 24x17 view (3.2 ms blanking,
2.7 painting and 4.7 blitting the fifteen rows, on 9.5 of key, job
and the document's own shift), decreasing by 0.6 ms a row down to
12.8 ms with one row below, and a frame dropped at 60 Hz down to
about six rows below (and at 50 Hz at the top). Such an ENTER now
composes the window the split way: 9.0, 8.5, 7.2 and 9.5 ms
(key and job; compose; blit; flush), no frame dropped, one frame of
latency more than before. Painting the shifted rows within the key's
frame needs the buffer's rows moved instead of painted (about 0.17 ms
a row for the interior cells, the bar's column kept) and their blit
deferred to the next frame (a pending row range applied by
`NoteLayService`), estimated at 12 to 13 ms for the key's frame: a
second deferral mechanism next to `RepaintDue`, which this ticket
does not introduce without a decision. While typing a document from
the top the rows below are empty lines, which show nothing shifting,
and ENTER stays on the row route (the table above).

What stays per key with the lines above the caret: the line-of walk
(`NwLineOf`, three times a key, 0.02 ms a chunk), the caret's
derivation over the table's rows (`NwDerive`, twice), the walk to the
span's first entry and the row route's walk past the rows above the
first one it paints (0.06 ms a row): 0.15 ms a line, 9.4 ms for the
first A and 11.6 for the sixteenth (13.1 to 15.6 before); the table
is no longer laid out from the first line for any key.

Screens, documents, carets and tops are byte for byte those of
`0850c2d` in every scenario probed (the ROM's own name table against
the base build's, and against the `wrap_win` oracle in the subjects):
the three above with three short lines, with the full document and
with sixteen lines of A, ESC and HOME over a selection of one line
and of three, X over WORLD, five BS down to and over a line's start.

Tests (`msxtest.py`, `frame_checks`, `./test.sh --frames`):
`fb-caret-24`, `fb-type-24-16`, `fb-type-24-full`, `fb-esc-24` and
`fb-sel-letter-24` (the table in "Frame budget at 60 Hz"). Each reads
Dropped before its keys and at the end and asserts both 0, the
screen, the document, the caret, the top and no selection; where the
typing is not what is measured the document is typed at the home
size and the window grown before the first mark. Against `0850c2d`'s
source they fail at 60 Hz with Dropped 13, 3, 39, 3 and 2 and pass at
50 Hz, where its frames are under 20 ms. `fb-join-bs` and
`fb-join-del` take the split way now (13.3 ms at both machines, as
before); the join's span job, its rows painted and blitted in the
key's frame, was 16.0 ms at 60 Hz, one dropped.

Budget against `0850c2d`: the code before the page pad of `NsanTab`
**26,184 → 27,027 bytes (+843)**, TEST **27,938 → 28,781 (+843)**;
the build line 26,624 → 27,392 and 28,416 → 29,184 (`NsanTab` on the
next pages); static RAM **3,997**, heap **8,291** without a disk ROM
and **2,906** with one, unchanged; nineteen bytes of `TapeBuf`
scratch (+196, +200 to +214). Padded ROM crc32: normal **ff97d1db**,
TEST **25db1ce9** (base 9e3fa2c7 / be30eb22).

## A letter, BS or DEL over a selection that crosses a line end (lf-1608)

dev3's finding on lf-1597 (point 3): X typed over HEL|LO WORLD through
BAZ at 24x17 with WRAP on was still a **21.1 ms** frame at 60 Hz and
22.9 at 50, one dropped at both machines, BS and DEL the same, and
37 ms with two dropped (one at 50 Hz) over three SHIFT+UP in the full
document. lf-1597 gave only a selection within one line the one pass;
over a line end `NoteEraseSelection` still deleted backwards, a
`NoteBack` a character (`NoteDeleteAt`, `NoteTrimTail`,
`NoteMarkEdited`, `NoteUndoDue`: 0.5 ms each) with `NbHard` and a 2 ms
`NoteTrimTail` at each join, 12 ms before the span job and the split
compose. Measured as in the sections above (the loop iteration from
`FrameWatch` to `MainLoop`, breakpoints on the routines' entries for
the route), on main `9a0a185` and on this change; the longest frame
from SELECT on, `Dropped` from boot.

| at 24x17, WRAP on (frame ms / Dropped) | `9a0a185` JP | `9a0a185` EU | now, both |
|---|---|---|---|
| HELLO WORLD, FOO BAR, BAZ; SELECT, two SHIFT+UP, X | 21.1 / 1 | 22.9 / 1 | 12.3 / 0 |
| the same, BS | 20.7 / 1 | 22.5 / 1 | 11.9 / 0 |
| the same, DEL | 20.7 / 1 | 22.4 / 1 | 11.9 / 0 |
| the same selection made downwards (SELECT at HEL, two SHIFT+DOWN), X | 21.1 / 1 | 22.9 / 1 | 12.3 / 0 |
| HOME after the two SHIFT+UP (all three lines), BS | 22.1 / 1 | 23.9 / 1 | 12.0 / 0 |
| the full document (eight lines of 27): SELECT, three SHIFT+UP, X | 35.1 / 2 | 36.7 / 1 | 11.4 / 0 |
| the same, BS | 34.7 / 2 | 36.4 / 1 | 11.5 / 0 |
| three SHIFT+UP and three SHIFT+LEFT (a soft and a hard end between the bounds), DEL | 36.3 / 2 | 37.8 / 1 | 11.5 / 0 |
| A, CDEFGH, IJKL; two SHIFT+UP from IJKL's end (the lower bound among A's trailing spaces), X | 20.3 / 1 | 18.3 / 0 | 12.5 / 0 |
| fourteen characters, ENTER, OPQ; the caret at (0, 14), SELECT, SHIFT+DOWN, X | 13.1 / 0 | 13.1 / 0 | 11.7 / 0 |
| ENTER over the three lines | 24.3 / 1 | 26.1 / 1 | 15.5 / 0 |
| GRAPH+E (CUT) of the three lines | 27.5 / 1 | 27.4 / 1 | 17.1 / 1 (JP), 16.9 / 0 (EU) |
| GRAPH+P (PASTE) over the three lines | 27.7 / 1 | 27.6 / 1 | 20.7 / 1 (JP), 22.4 / 1 (EU) |

What changed (`NoteEraseSelection`, `src/note.inc`): every selection
goes in one pass. The caret goes to the lower bound and the links of
the chunks from its chunk to the one before the upper bound's are
walked once; a line that ends among them is joined the way `NbHard`
has it, the link set and `NwJoined` counted for the span job. When
the lower bound's own chunk ends its line and the bound lies among
its trailing spaces, the caret moves back to the text's end, where
the join left it before, and those spaces count as cells. Then one
`NoteDeleteN` over the cells between the bounds (the packed distance
less two a chunk end), one `NoteTrimTail`, one `NoteMarkEdited`; the
span job and the compose after it are lf-1597's (a join composes the
window the split way, as `fb-join-bs` has it). A lower bound of 14,
the end-of-chunk caret a chunk filled by typing leaves, begins the
cells at the next chunk's first and keeps the caret: lf-1597's one
pass began at that chunk's terminator and wrote the cells over it and
the link (fifteen characters, two SHIFT+LEFT, Z, SELECT, SHIFT+RIGHT
and X left `ABCDEFGHIJKLMZ`, a chunk `O` with a spoiled link and `X`
on a third, the caret at (15, 0)); it is `ABCDEFGHIJKLMZXO` now
(`sel-chunk-end-type`).

Documents, carets, tops and screens are byte for byte `9a0a185`'s in
every scenario of the table (the ROM's own name table against the
base build's) and in UNDO after the X (the three lines back), but the
end-of-chunk one.

**Measured and not built, for the PO.** ENTER over a selection over
lines is **15.5 ms** at 60 Hz: no frame dropped in the run, but in
the band (15.2 to 15.9) where it depends on where the frame starts;
the erase, `NoteEnter` and the span job over the joined and split
lines in one frame. CUT and PASTE over lines go through `NoteRefresh`
(the whole window composed and blitted in the frame): **17.1 ms** and
**20.7 to 22.4 ms**, a frame dropped (27.5 before). Neither is the
ticket's letter, BS or DEL.

Tests (`msxtest.py`): `fb-sel-lines-24`, `-bs`, `-del` (the three
lines, SELECT, two SHIFT+UP, X, BS or DEL) and `fb-sel-lines-24-full`
(the full document, SELECT, three SHIFT+UP, X) in `frame_checks`, each
Dropped before the keys and at the end both 0, the screen against
`wrap_win`, the document, the caret, the top and no selection; on
`9a0a185`'s source they fail at both machines with Dropped 1, 1, 1
and 1 at 50 Hz (22.9, 22.5, 22.4 and 36.7 ms) and 1, 1, 1 and 2 at
60 Hz (21.1, 20.7, 20.7 and 36.9 ms). `sel-chunk-end-type` in
`note_selection_checks` asserts the document above (on `9a0a185` crc32
1c159c07 for 1c614ff9). The comment on the lf-1597 subjects said
`0850c2d` dropped a frame in `fb-type-24-full` at 50 Hz; measured
again on `0850c2d`'s source it does not (Dropped 0, longest frame
17.5 ms), and the comment and the section above say so now.

Budget against `9a0a185`: the code before the page pad of `NsanTab`
**27,113 → 27,128 bytes (+15)**, TEST **28,867 → 28,882 (+15)**; the
build line 27,392 and 29,184 unchanged; static RAM **3,997**, heap
**8,291** without a disk ROM and **2,906** with one, unchanged, no
new scratch. Padded ROM crc32: normal **8d0434e3**, TEST **e7f28946**
(base bef87f9c / 52adccde).

## FILE > OPEN in the budget (lf-1590)

The two exceptions lf-1583 left, measured on main `3393724` and on
this change the same way (breakpoints at `FrameWatch` and `MainLoop`,
two files on RAM, a 16x9 notepad with HI typed). The iterations are
the same length on C-BIOS EU and JP; the frame is 20 ms at 50 Hz and
16.7 at 60, where 14.8 ms never dropped and 15.2 sometimes did.

| step | main | now, frame by frame |
|---|---|---|
| FILE > OPEN pressed in the menu | **30.4 ms**: the menu's close, the open with two pane reads, the desktop under 12 rows, the compose and the blit in the frame of the press | 9.0 (the menu's close, the open, one pane read), 10.8 (the flush of the menu's six rows and the compose), 8.2 (the blits), 7.2 (the flush of the picker's 12 rows) |
| SHIFT+DOWN in the picker | **21.1 ms**: both panes read again, a full recompose and blit | 8.2 (two rows painted and blitted), 3.1 (their flush) |
| ENTER: BETA loaded | **27.0 ms**: the picker closed, the notepad's state loaded, the read, the undo snapshot, the LDIR and the compare loop, then the first step of the wrap layout | 13.7 (the load), 7.9 and 5.5 (the wrap layout), 6.4 (the compose), 7.7 (the desktop under the picker's rows and the blit), 7.2 (the flush) |
| the route | Dropped 2 at 50 Hz, 3 at 60 Hz | Dropped 0 on both |
| the clock from the VIEW menu over a 24x17 notepad | **17.4 ms** at 60 Hz, one dropped: the flush of the menu's rows, then the desktop under 17 rows and the notepad blitted whole | 8.2 (the menu's close, the open and the compose), 7.8 (the menu's flush, the clock's rows and the notepad's title row blitted), 3.4 (their flush); Dropped 0 |

What changed:

- **A move paints two rows** (`CmdMoved`). The rows the selection
  left and took are blanked and painted into the window's buffer
  (the name, and the size in the picker) and marked by row with
  `RowsAddText`; `CmdKey` returns 2, so nothing is recomposed, and the
  directory is not read: the page's cache still describes the page.
  When the top moves (`CmdPaged`) the pane is read again from its new
  top and its six rows painted the same way; the other pane's cache
  is left as it was. Delete, copy, B, P and R still go through
  `CmdRefresh` and a full recompose, and `CmdPicked` resolves the
  name again as before. READY replaces another status on the status
  row alone.
- **The picker reads one pane.** `CmdInit` no longer reads the
  directories: `CommanderOpen` and `CmdBrowseOpen` do once the mode
  is set, and nothing is cleared first (`CmdReadPane` writes each
  name's terminator and a zero first byte for the rows past the end;
  the clear was 0.55 ms a pane, `CmdInit`'s 1.1 ms).
- **The open is spread over three frames** (`RepaintDue` 4). The
  frame of the press closes the menu and opens the window; the next
  flushes the menu's rows and composes the front window alone
  (`RepaintDue` 3, `WndComposeFront`, which `WndOpenLater` shares);
  the third blits. The picker shows four frames after the press
  instead of two, and no frame is dropped.
- **A window open repaints no desktop** (`WndOpen`). Nothing moved,
  so the new window's rows are blitted over the shadow as it is
  (`RowsLiveText`); the old front contributes its title row alone
  (`WndCapRows`), or all of its rows when the change of cap
  recomposes it (a notepad with a selection). `WndClose` and
  `WndFocusTo` do the same for the window that gains or loses the
  cap. This put the clock from the VIEW menu in the budget: it had
  the 24x17 notepad's 17 rows blitted over a fresh desktop.
- **The load's four copies.** `NoteSanitise` moves the document from
  `CmdBuf` into `NoteBuf` in one pass through a 256-byte table
  (`NsanTab`; POP reads the source, interrupts off meanwhile): 2.65
  ms for the 1.5 of the LDIR and the 7.4 of the compare loop.
  `CopyFast` (sixteen LDIs a turn, 18 T a byte against LDIR's 23)
  does the state load and save (`AppLoad`, `AppSave`), the RAM read
  and write and the undo snapshot. The repaint of the loaded notepad
  waits a frame (`RepaintLater`), as a save's does.
- **The compose of a framed window** (`WinDraw`): the body is blanked
  by PUSH, 6 T a cell, and the edges set after, 0.8 ms for 30x12
  against 2.75; the picker's sizes are written straight into the
  buffer (`CmdDrawSize`).
- **`NtFlush`** walks the dirty bits by shifting each byte once:
  rebuilding the byte and the bit from the row number was 1.5 ms of
  every flush before any row went out. Every frame that flushes
  anything gains that.

Subjects: `fb-open` asserts Dropped 0 over the whole RAM route, the
picker included (on the disk the bounded form stays); `fb-view-clock`
the clock from the VIEW menu over the 24x17 notepad as composed,
Dropped 0; `browse-move-rows` and `cmd-move-rows` one repaint pass per
SHIFT+DOWN whose range is rows 5 to 6, no slot dirty and no compose
(breakpoints on `MarkRows` and `WinDraw`, `Recomposes` before and
after); `browse-reload` R after a file vanished behind the picker's
back; `browse-scroll` and `browse-scroll-back` (the disk, where eight
files fit) the page turned down and back, the first file of the page
on the first row and the selection on its row; `cmd-scroll`,
`browse-delete`, `cmd-delete`, `cmd-copy` and the bank's `B` subjects
as they were. Disk I/O stays the exception above.

Budget against `3393724`: normal ROM **24,671 → 25,344 bytes
(+673)**, TEST ROM **26,428 → 27,136 (+708)**: `NsanTab` is 256 of
it, the padding that puts it on a page 15 (TEST 53), code the other
402 (TEST 399); static RAM **4,018 → 4,012 (-6)**, TEST **6,750 →
6,744** (`CmdNumber` is gone); heap **8,270 → 8,276** without a disk
ROM and **2,885 → 2,891** with one (TEST 5,538 → 5,544 and 153 →
159). No new allocation. Padded ROM crc32: normal **eb7ff6cb**, TEST
**bb02f548**. `./test.sh`: C-BIOS_MSX1_EU 741 ok, C-BIOS_MSX1_JP 731
ok, all subjects pass.

## The page against the directory (lf-1593)

The review of lf-1590: since a move within the page no longer reads
the directory, the page's cache goes stale when another window adds
or removes a file meanwhile. A notepad with HI, FILE > OPEN over
ALPHA and BETA, GRAPH+X to the notepad, GRAPH+S with the name G,
GRAPH+X back and two SHIFT+DOWN: the selection stood on index 2, the
row stayed blank, nothing was inverted and ENTER said NO FILE
SELECTED. R put it right.

Now a move checks the two rows it is about to paint against the
directory first (`CmdRowStale`: one `StDir` and a name compare a
row), and when either no longer says what the cache does (a blank
row where a file now is, a name where none or another one is) it
takes the page route of a page turn (`CmdPaged`: the pane read again
from its top, its six rows painted and marked). SHIFT+DOWN at the end
of the directory checks the selection's own row the same way, so a
file deleted from under the selection leaves no ghost: the page is
read again and the selection stands on its blank row. A move whose
two rows the directory confirms reads nothing, as before; the
Commander's other pane is checked on its own moves, as it is read on
its own page turns. The mouse's click on a blank row and the picker
brought to the front are as they were.

Measured as in the section above on C-BIOS JP (the same lengths on
EU), two files on RAM, a 16x9 notepad behind the picker:

| step | `3b7d9cd` | now |
|---|---|---|
| SHIFT+DOWN within a current page (ALPHA to BETA), two rows painted | 8.2 ms | 8.9 (the two rows checked first) |
| SHIFT+DOWN onto the row the cache has blank where the directory has G | 7.6: the row stays blank, ENTER says NO FILE SELECTED | 14.7 (the page read again, six rows painted and blitted), 4.8 (their flush): G on the row, inverted, ENTER loads it; Dropped 0 at 50 and 60 Hz |
| SHIFT+DOWN past the last file (nothing moves; `CmdDone`'s full recompose) | 15.3, one dropped at 60 Hz, as before lf-1590 | 15.7 (the selection's row checked), one dropped |

The last row was dropping before this change and still does: a key
that moves nothing recomposes the whole window. The page route's
14.7 ms is that of a page turn since lf-1590; it stays under the
14.8 that never dropped at 60 Hz, with no room to spare.

Subjects (`./test.sh --commander`, all configurations):
`browse-saved-behind` the route above, the screen against the
compositor (G on the third row, inverted, the notepad behind it
titled G), `CmdSel0` 2 and G stored as HI; `browse-saved-behind-open`
ENTER then loads G (the name, the 256 bytes, unmodified);
`cmd-saved-behind` and `cmd-saved-behind-right` the same route in the
two-pane Commander, G in the left pane after its two moves and in the
right one, on RAM, after TAB and its own two. On RAM, the directory
table written behind the picker's back: `browse-added-behind` a third
entry, found on the move onto its row; `browse-gone-behind` BETA
removed with the selection on ALPHA, the move lands on GAMMA;
`browse-gone-under` DELTA removed from under the selection, SHIFT+DOWN
reads the page again (the selection on its blank row), and
`browse-gone-under-up` SHIFT+UP from there takes GAMMA.
`browse-move-rows`, `cmd-move-rows` and `fb-open` as they were: one
repaint pass of rows 5 to 6, no compose, Dropped 0.

Budget against `3b7d9cd`: the code before the page pad of `NsanTab`
**26,115 → 26,181 bytes (+66)**, TEST **27,869 → 27,935 (+66)**; the
build line stays 26,624 for the normal ROM and goes 28,160 → 28,416
for TEST (the pad: `NsanTab` moved a page up); static RAM **3,997**
(TEST 6,729), heap **8,291** without a disk ROM and **2,906** with
one, unchanged. Padded ROM crc32: normal **bb5dd696**, TEST
**dc7ff500**. `./test.sh`: C-BIOS_MSX1_EU 777 ok, C-BIOS_MSX1_JP 767 ok, all
subjects pass.

## The ghost row and the click against the directory (lf-1604)

The mirror of lf-1593, from dev3's test report on it: there a file
saved from another window did not appear, here one deleted from
another window stayed. The picker over ALPHA, BETA and G, GRAPH+X to
the notepad, G deleted with FILE > DELETE, GRAPH+X back: G still on
the third row. SHIFT+DOWN from BETA found no third file and left the
page as it was (`CdnEnd` checked the selection's own row alone, not
the row `StDir` had just found nothing for); ENTER on the selected G
said NO FILE SELECTED with G still inverted; a click on the ghost row
selected it. And a click on the blank row where a freshly saved G now
stood did nothing (`CmdPress` saw a blank cached row), left so by
lf-1593.

- A move past the last file (`CdnEnd`) checks the row the move was
  for as well: when the cache names a file there, where `StDir` has
  just found none, it takes the page route (`CmdPaintPage`, the pane
  read again and its six rows painted and marked) with the selection
  where it stood. No second `StDir` for that row: the one the move
  made is the answer. The selection's own row is checked as before.
- The lookup under ENTER, C, D and DELETE (`CmdPicked`) ends the key
  itself when the directory runs out without the cached name
  (`CpkGone`): the page read again, its six rows and NO FILE SELECTED
  on the status row painted and marked, the selection on its blank
  row, 2 for the dispatcher. Every caller would have said the same
  through `CmdEmpty`, with a recompose of the whole window that
  dropped a frame at 60 Hz once the read came first; so the caller's
  return address goes and the key returns from there.
- A click on a row of the picker (`CmdPress`) checks that row against
  the directory first (`CmdRowStale`, one `StDir`). A row the
  directory confirms reads nothing, as the PO asked; a blank one that
  stays blank is no click, as before. When the row no longer says
  what the cache does the page is read again and the click selects
  what the row holds now, nothing when it is blank; the window is
  recomposed either way, as a click always was.

Measured with the frame probe on C-BIOS JP (`FrameWatch` to
`MainLoop`, the longest frame of the step), RAM, `9a0a185` → now:

| step | before | now |
|---|---|---|
| click on BETA, four files, the picker alone (a row the directory confirms) | 17.5 ms, one dropped (lf-1592 point 1) | 18.2, one dropped: the one `StDir`, nothing read |
| click on a blank row past the files | 2.8 | 3.5, nothing read |
| click on the ghost row (G deleted behind the picker, BETA selected) | 18.9, one dropped; G selected | 20.3, one dropped; the page read, G gone, BETA still selected |
| click on the blank row where a freshly saved G stands | 2.8; nothing happened | 22.8, one dropped; the page read, G selected |
| ENTER on the deleted G | 16.3, Dropped 0; NO FILE SELECTED with G inverted | 14.9, Dropped 0; the page read, G gone |
| SHIFT+DOWN from BETA with the ghost on the third row | 16.2, Dropped 0; G stayed | 13.3, Dropped 0; the page read, G gone, BETA selected |
| SHIFT+DOWN past the last file, four files, no ghost | 14.8, Dropped 0 | 15.0, Dropped 0: the cached row checked, nothing read |

The two clicks that drop were dropping before (a click recomposes the
whole picker: lf-1592 point 1); the click that found G costs the page
read on top. The two ghost routes that recomposed the whole window at
16 ms now paint six rows and come in under the budget.

Subjects (`./test.sh --commander`, the browse group, all
configurations; the first five fail on `9a0a185`):
`browse-gone-end` SHIFT+DOWN from BETA after G's deletion from the
notepad, the screen against the compositor (ALPHA and BETA, BETA
inverted, the notepad behind titled G), `CmdSel0` 1, G's entry
unused; `browse-gone-enter` ENTER on the selected G, NO FILE SELECTED
on the status row and no row inverted, `CmdSel0` 2; `browse-gone-click`
a click on the ghost row, G gone and BETA still selected;
`browse-saved-click` a click on the blank third row after GRAPH+S of G
from the notepad, G on the row and inverted, `CmdSel0` 2;
`browse-saved-click-open` ENTER then loads G (the name, the 256 bytes,
unmodified); `browse-mouse-reads` a click on BETA with a breakpoint on
`CmdReadPane`: no read. `browse-move-rows` and `cmd-move-rows` count
`CmdReadPane` too and still see none; `browse-saved-behind*`,
`cmd-saved-behind*`, `browse-gone-*`, `browse-added-behind` and
`fb-open` as they were.

Budget against `9a0a185`: the last byte before the page pad of
`NsanTab`, read from the ROM, **27,113 → 27,197 bytes (+84)**, TEST
**28,867 → 28,951 (+84)**; the build line goes 27,392 → 27,648 for
the normal ROM and 29,184 → 29,440 for TEST (the pad: `NsanTab` moved
a page up in both); static RAM **3,997** (TEST 6,729), heap **8,291**
without a disk ROM and **2,906** with one, unchanged; no new RAM.
Padded ROM crc32: normal **d7ae23ff**, TEST **c3ca26dd**.
`./test.sh --commander`: C-BIOS_MSX1_EU 58 ok, C-BIOS_MSX1_JP 58 ok.

## The click, TAB and the keys that recomposed the Commander (lf-1592)

dev3's findings on lf-1590, lf-1593 and lf-1604: the picker's and the
Commander's routes that still recomposed the whole 30x12 window in
their frame. Measured with the frame probe on C-BIOS JP (`FrameWatch`
to `MainLoop`, the longest frame of the step; the same lengths at 50
Hz, where the budget is 20 ms), a 16x9 notepad with HI behind the
picker (FILE > OPEN) or the Commander (VIEW), four files on RAM,
`ab93a19` → now:

| step | `ab93a19` | now |
|---|---|---|
| a click on a picker row the directory confirms (BETA, then GAMMA) | 20.3 ms, one dropped; 18.2 with the picker alone, one dropped; 22.0 at 50 Hz, one dropped | 8.9: the two rows painted, nothing composed |
| a click on the blank row where G stands, saved from the notepad since the page was read (lf-1604) | 21.2, one dropped; 22.8 at 50 Hz, one dropped | 15.0, Dropped 0 at both: the page read again and its six rows painted |
| a click on the ghost row, G deleted from the notepad since (lf-1604) | 20.3, one dropped; 21.9 at 50 Hz, one dropped | 14.1, Dropped 0 at both |
| SHIFT+DOWN on the last file, SHIFT+UP on the first: nothing moves | 17.4 and 16.4, one dropped each (15.3 with the picker alone, lf-1593) | 3.6 and 2.6: nothing painted |
| SHIFT+DOWN within the page | three `StDir`: the next file's existence, then each of the two rows checked | two: the lookup is the row's check |
| TAB in the Commander | 16.4, one dropped; 14.2 with the Commander alone | 11.9 and 9.8: the headers, the selections and the help row painted |
| ENTER in the Commander, the notepad open | 16.8, one dropped; 15.9 with the Commander alone | 10.5 and 9.1: the open in its frame, the load in the next |
| DELETE in the picker: the dialogue, TAB, ENTER | 21.1, one dropped; 22.9 at 50 Hz, one dropped | 11.7 |
| R in the picker | 16.1, Dropped 0 | 8.9 |
| D then Y in the Commander | 18.1, one dropped | 9.3 |
| R in the Commander | 18.5, one dropped | 9.1 |

What changed, all of it in `commander.inc` but for one byte and one
call:

- **A click paints what it changes** (`CmdPress`). A row the
  directory confirms: the row the selection left and the row it took,
  as a key's move paints them (`CmdPaintMove`, the tail of
  `CmdMoved`); a row that no longer says what the cache does: the
  page read again and its six rows painted (`CmdPaintRows`, the rows
  of `CmdPaintPage`), the selection on what the row holds now. A
  click on the selection or on a blank row paints nothing. It had the
  window recomposed over a fresh desktop (`WndTouch`, `RowsAddLive`).
  [OPEN] goes to `CmdKey` as before; its rows are the key's.
- **A key that moves nothing paints nothing** (`CmdStill`): SHIFT+UP
  on the first file, SHIFT+DOWN on the last (the selection's row
  checked first, as lf-1593 has it), P from a PRN pane, a key other
  than Y, N or ESC while the delete asks. They returned 1 and the
  dispatcher recomposed the window.
- **SHIFT+DOWN's lookup is the row's check** (`CmdRowNamed`): the
  name `StDir` finds for the next index is compared with the cached
  row it lands on, where `CmdRowStale` looked it up again. On the
  disk that was a BDOS search for every move within the page.
- **TAB paints the rows it changes** (`CmdPaneSet`): each pane's
  header and its selection's row (`CmdPaintPaneSel`), the help's
  second row (`CmdDrawHelp2`, the active pane's keys) and READY, the
  window not recomposed. SHIFT+LEFT or RIGHT onto the pane that is
  active already paints READY alone.
- **A fresh notepad loads in the frame after its open** (`CopNew`,
  `CmdLoadService`). The open was 6.6 ms (the old front's state saved,
  three heap walks, the blank document, the old front's cap flipped)
  and the load 7.1 (the read, the undo snapshot, `NoteSanitise`): one
  frame of 16.8 ms. Now `CopNew` opens the notepad, sets its name and
  device and `CmdLoadDue` (its slot plus one, 1 byte of RAM), and
  `RepaintLater` keeps the blank notepad from being composed; the main
  loop calls `CmdLoadService` before the frame's input, which loads
  the file into the front window when it is still that notepad, then
  `WndTouch`, `RowsLiveText` and `RepaintLater` as `CopOk` has them,
  or gives the name and the device back and says COULD NOT LOAD as
  `CopLoad` did. The picker's ENTER into an existing notepad
  (`CopExisting`) is as it was, 13.7 ms.
- **The keys that read the directories again compose next frame and
  blit the one after** (`CmdDone`, `RepaintDue` 4 as the open has it
  since lf-1590): the delete, R, B, P, a copy and the statuses mark
  the window's rows and return 2; the frame after composes the front
  window (`WndComposeFront`), the one after blits. The compose of
  30x12 is 7.3 ms and the blit 3.3; with the reads in the key's frame
  the routes were 16 to 23 ms. The dialogue's close before the delete
  restores its save-under into the shadow and leaves `RepaintDue`
  alone, so the split holds there too. With no window left (the
  picker closed for a notepad there was no room for) nothing is
  composed.

**The question for the PO.** The click on a stale row is 15.0 ms at 60
Hz: Dropped 0 in the subject on both machines, but at the limit (14.8
never dropped, 15.2 sometimes). The frame is the one `StDir` of the
check, the page's four in `CmdReadPane` (2.2 ms on RAM) and the six
rows painted (0.6 ms a blank row, 1.2 a named one: the blank, the
name, the size, the mark). `fb-click-stale` asserts Dropped 0 at 50
Hz and allows one at 60, where it reports the frame.

Subjects (`./test.sh --frames`, all configurations; each from boot
with the notepad and the picker or the Commander over it, Dropped read
before the keys and at the end and both 0 on RAM, the screen against
the compositor, the composes counted on a breakpoint at `WinDraw` and
the longest frame from the mark in the detail; on the disk the
lookups and the delete are the BDOS's, the known exception, so Dropped
is reported there and not asserted): `fb-click-row` the two clicks,
GAMMA selected, no compose; `fb-click-stale` the click on G's row
after GRAPH+S from the notepad, G selected, no compose;
`fb-click-gone` the click on the ghost row after G is read onto the
page and deleted from the notepad (FILE > DELETE), BETA still
selected, G gone from the directory, no compose, Dropped 0 at 50 and
at 60 Hz (14.1 ms, lf-1622);
`fb-move-end` SHIFT+DOWN on DELTA and SHIFT+UP on ALPHA around three
moves up, no compose; `fb-tab` the right pane active, no compose;
`fb-cmd-enter` ALPHA loaded into a third window, one compose;
`fb-delete-picker` BETA gone and DELETED, one compose;
`fb-reload-picker` the page again from ALPHA, one compose;
`fb-delete-cmd` ALPHA gone and DELETED, two composes (the question
and the answer); `fb-reload-cmd` one. On `ab93a19`'s sources with this
harness nine of them fail at 60 Hz and seven at 50 (`fb-cmd-enter`
and `fb-reload-picker` dropped at 60 Hz alone). `browse-move-rows`
and `cmd-move-rows` count the lookups of a move as well: two;
`browse-mouse-reads` one lookup, the two rows and no compose.
`browse-gone-*`, `browse-saved-click*`, `fb-open` and the rest of the
commander group as they were.

Budget against `ab93a19`: the last byte before the page pad of
`NsanTab`, read from the ROM, **27,212 → 27,453 bytes (+241)**, TEST
**28,966 → 29,207 (+241)**, TAPE=1 **27,230 → 27,471 (+241)**; the
build line goes 27,648 → 27,904 for the normal ROM and 29,440 → 29,696
for TEST (the pad: `NsanTab` moved a page up in both); static RAM
**3,997 → 3,998 (+1: `CmdLoadDue`)**, TEST **6,729 → 6,730**; heap
**8,291 → 8,290** without a disk ROM and **2,906 → 2,905** with one.
Padded ROM crc32: normal **64663f39**, TEST **da7b99ce**, TAPE=1
**a755d74a**.

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

### Review follow-up (lf-1539)

Only the text field takes plain arrows: LEFT/RIGHT move its caret and the
key pointer rests while it is open. Button dialogues (UNSAVED WORK,
OVERWRITE, DELETE, alerts) keep the convention above: with the key
pointer ON, plain arrows move the pointer and SHIFT+arrows or TAB move
the focus. `DgFieldOpen` in `src/dialog.inc` is the one test for both
`KbdDecode` and `ReadInput`. The text field no longer paints an inverted
focus bar on row 11, where it has no button.

`--saveas` adds `saveas-field-row` (row 11 is 20 plain spaces, CRC32
`349a0886`; base `d9a444aa`), `saveas-field-pointer` (caret 1, pointer
(120, 90)) and `saveas-button-arrows` (OVERWRITE dialogue: plain DOWN
leaves focus 0 and moves the pointer to (120, 93); base gave focus 1).
`saveas-exists-overwrite` now chooses OVERWRITE with SHIFT+DOWN.
ROM grows **18 bytes** (23,362 to 23,380 normal; 25,119 to 25,137 TEST);
static RAM and heap are unchanged (4,015 bytes; heap 8,273 / 2,888).

## Printer (lf-1524)

FILE > PRINT and GRAPH+R print the front notepad through the write-only
`ST_PRINT` (8) backend. In COMMANDER, **P** toggles the opposite pane
between PRN and its own device (see lf-1536 below); **C** copies the
selected file there. A 256-byte
notepad is printed as logical lines, joining continuation chunks and
omitting empty trailing lines; each line ends in CR LF and the job ends
in FF. Other files within the commander's existing 256-byte copy limit
are sent as raw bytes followed by FF. Printing never changes the source.
PRN has only `STCAP_WRITE`, no directory, and is excluded from FILE > OPEN
and SETTINGS > BACKEND. HELP > KEYS includes R PRINT.

The backend uses inter-slot CALSLT calls to BIOS LPTSTT ($00A8) and
LPTOUT ($00A5). It checks readiness before opening and before each output
byte, and reports `PRINTER NOT READY` for a printer that stays busy (see
lf-1536 below for the bounded wait) or for output carry.
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
`BACKEND 48 KB   [BANK]`. A 16K MSX1 has no hidden pages and no BANK.
A 32K MSX1 ($8000-$FFFF) does: its page 2 RAM is behind the cartridge,
a 16 KB bank of 63 blocks and 58 files, `BACKEND 16 KB   [BANK]`
(measured in the lf-1525 test on a Roms_MSX1 with 32K: source 1, 63
blocks, Commander B and the save/close/reopen route byte-identical;
`bank-mirror` measures the same size here). In the Commander **B** takes the other pane round
the same ring (from PRN back to the device the pane had before P), so RAM, DISK and BANK copy both ways;
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
files (half the bank's files when that is fewer, so `bank-rw` still
has room: 89 on 48 KB, 29 on 16 KB) with interrupts off throughout (the routine's EI is a NOP for the
duration) and the harness snapshots $F380-$FFFF and the whole ROM at
`TbsStart` and `TbsEnd`: identical, and the ROM equals the image.
`bank-rw` round trips 64 bytes, fills the bank until `STERR_FULL`
(**88** files after the 90 before it on 48 KB, 28 after 30 on 16 KB,
139 after 101 on 64 KB, 1,758 on 496 KB: 412 s of emulated time, the
whole TEST run), counts the directory (to 255, the index's width),
deletes `F0002`, sees `NEW` take its entry (103 when `bank-sys` wrote
a hundred, 92 on 48 KB) and reads a full file back; `bank-dir` parses the
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

## Review of the printer (lf-1536)

Two findings from the review of lf-1524, both fixed here.

**Busy handling.** LPTSTT answers busy or ready and nothing else, and the
backend failed the whole job on the first busy poll. A real printer holds
BUSY while it takes a character into its buffer and for as long as it
prints once the buffer is full, so on hardware the second byte or the
second line would have raised `PRINTER NOT READY`; the emulated logger is
never busy, which is why no subject saw it. `PrintReady` now polls LPTSTT
until ready or until `PRINTWAIT` (150) interrupts have passed on `IrqCnt`,
which keeps counting because `PrintBios` re-enables interrupts after
CALSLT. An unplugged port still ends in the alert, after 150 interrupts:
measured **2,990,748 µs** at 50 Hz and **2,503,478 µs** at 60 Hz, asserted
as 150 to 152 interrupts on the ROM's own counter rather than as a time.
The new `print-wait` subject starts the job with nothing plugged in and
plugs the logger one emulated second later: the output is the complete
`HI\r\nTHERE\r\n\f` (CRC32 6cea7f8e), no alert, and `Dropped` counts the
stalled frames, **51** at 50 Hz and **61** at 60 Hz.

**Commander P.** P turned the opposite pane into PRN and then into RAM, so
a DISK pane was gone until the commander was reopened. It now returns the
pane to what `CmdInit` gave it: the global backend on the left, RAM on the
right. Four subjects assert `CmdBk0`/`CmdBk1` and the PRN header after P
and P P on either pane; on Roms_MSX1 + Roms_Disk the left pane goes
5 → 8 → 5.

Measured against base `0ed9256`: normal ROM **17,166 → 17,201 bytes
(+35)**, TEST ROM **18,455 → 18,490 (+35)**. Static RAM **3,578 (+0)**,
TEST **6,310 (+0)**; heap **8,710** without disk and **3,325** with it,
unchanged. No new RAM: the wait uses `IrqCnt`, the toggle `CmdWasK`.
Padded ROM CRC32: normal `6704eb41`, TEST `8498aabb`.

`./test.sh --print`: C-BIOS_MSX1_EU **23**, C-BIOS_MSX1_JP **23**,
Roms_MSX1 + Roms_Disk **22** assertions, none failed (the disk run omits
the RAM-only raw-stream fixture, as before). The full suite is the
pipeline's.

## lf-1526: Notepad selection

SELECT toggles marking at the caret; holding it does not repeat the
toggle. While marking, SHIFT+arrows extend the selection with KEY PTR ON,
and plain arrows extend it with KEY PTR OFF. Mouse press places the caret,
drag selects whole cells, and dragging beyond the viewport scrolls it.
SELECT again stops extending while retaining the selection. ESC or a
click without dragging clears it. Typing replaces it; BS and DEL delete
it (without a selection BS deletes backwards and DEL forwards, since
lf-1572). HELP > KEYS now includes SELECT MARK.

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
| C-BIOS_MSX1_EU, 50 Hz | `--note-selection`, `--print`, `--resize-scroll` | 17, 23, 20 |
| C-BIOS_MSX1_JP, 60 Hz | `--note-selection`, `--resize-scroll` | 17, 20 |
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
the drag alone: the counter is zeroed after the typed preamble (since
lf-1583 it counts the whole route, see "Frame budget at 60 Hz"). Measured
with breakpoints at `FrameWatch` and `MainLoop` on the 60 Hz machine, a
typed key frame with the icons under the window is 15.7 to 15.9 ms on
`0ed9256` and 16.0 to 16.3 ms here (state write-back and row marking
2.7 ms, lattice 0.9 ms, icon rows 1.6 ms, the Notepad compose 6.5 ms,
the blit 1.6 ms), and both drop 11 of the preamble's 23 key frames at
60 Hz; that cost is main's, not this change's, and is noted for the
owner. The drag's own frames are at most 13.2 ms. This is not a claim
about other editing operations or window sizes.

The chunk painter leaves the selection code alone when the anchor row
is $FF, which is the usual case: one load, an INC and a jump, 27 T a
chunk, where the call into the painter and its two checks cost 240 T
for the same answer. Measured on the 60 Hz machine with breakpoints
on every chunk of the `resize-screen-edge` compose, a 24 by 17
Notepad: a chunk took 427 us on main (`8fefef5`), 495 us with the call
and 440 us with the early-out, so the compose frame went from 14,420
to 15,607 us and dropped one frame, and is 14,665 us now, none
dropped. A front window with a selection still pays the 68 us a chunk
while it is drawn.

The mouse capture byte is cleared at `Start`, with `DskDrag`: openMSX
fills RAM with a pattern, and on the rebased layout the byte came up set,
so the caret followed the idle pointer until the first release, which
reversed the typed text in the `--print` subjects.

Measured against `8fefef5`: normal ROM **19,353 → 19,875 bytes (+522)**,
TEST **21,090 → 21,612 (+522)**; both run on into page 2, which `Start`
maps. Static RAM grows **20 bytes** (four live state bytes, one mouse
capture byte, fifteen paint scratch bytes), **3,759 → 3,779** normal and
**6,491 → 6,511** TEST. The heap falls by 20: **8,529 → 8,509** bytes
without a disk ROM, **3,144 → 3,124** with one (TEST **5,797 → 5,777 /
412 → 392**). Each Notepad's existing state allocation grows **4
bytes**, from **278 to 282**, with no new allocation. The KEYS window at
22 rows uses **15 more heap bytes** than main's 21 while it is open.
Padded ROM CRC32: normal **673eee09**, TEST **ecc22aa8**.

The full six-configuration `test-all.sh` is left to the pipeline, as
required by this implementation run's focused-tests-only instruction.
No real-ROM matrix results are claimed here.

## Clipboard and the EDIT menu (lf-1528)

The bar is MSX DESK, FILE, EDIT, VIEW, HELP, two spaces apart from
column 0 (the titles sit at 0, 10, 16, 22 and 28; FILE's cell did not
move, VIEW's and HELP's did, and the subjects that press them moved
with them). EDIT holds CUT, COPY, PASTE and UNDO; UNDO has no action
until the undo ticket. GRAPH+E, GRAPH+Y and GRAPH+P are the keys, the
free letters once X C T W N O S K L F G I V R D were taken, and HELP >
KEYS lists them.

One clipboard for every notepad: `ClipBuf`, 224 bytes, the document's
text budget, and `ClipLen`. COPY takes the selection's cells from the
lower endpoint to the upper, a byte 13 for every hard line break it
crosses; a line-ending chunk loses its padding, a continued chunk is
text through to its 14th cell, so a cut and a paste give the document
back. It is built in `CmdBuf` (free: no dialogue is open while a menu
item or a shortcut runs) and moved when it fits; a selection of padding
alone leaves an empty clipboard, and the one selection that cannot fit,
all sixteen chunks full with fifteen breaks, leaves the clipboard as it
was and cuts nothing. CUT is COPY and the selection's erase. PASTE goes
in at the caret through `NoteInsert` and `NoteEnter`, byte by byte as
if typed, over a selection if there is one; the document and the
thirteen cursor, flag and selection bytes (`NOTECURSZ`) are saved in
`CmdBuf` and `CmdWork` first, and a byte that does not fit puts all of
it back and opens DOCUMENT FULL: no half pasted text. Nothing to act on
(no selection, an empty clipboard, no notepad in front) does nothing,
and since lf-1549 the menu shows it: such an item is painted grey (see
"EDIT items in grey").

After a cut or a paste the buffer is composed in the same frame and
the desktop repaint and blit happen in the next, the split mouse
selection uses. Measured with breakpoints on the 50 Hz machine: a
paste of HELLO into HELLO WORLD at the caret is 7.3 ms (1.8 ms the
snapshot, 3.9 ms the one insert that opens a continuation chunk, the
same cost a typed 15th character pays) and the compose 6.1 ms; with
the repaint on top that frame was 25.0 ms and dropped, with the split
it ends at 14.4 ms and the next at 10.5 ms, 0 dropped. A typed key is
16.0 ms and ENTER 20.7 ms in the same window (ENTER drops a frame
already; so does every key once a second notepad is open, which is
what `clip-paste2` and `clip-full` report as Dropped).

`./test.sh --clipboard` (in the default suite):

| subject | asserts |
|---|---|
| clip-bar, clip-menu | the boot name table with the five titles equals the compositor (crc32 bda22fd6 on C-BIOS), and EDIT open equals it painted from the ROM's MenuDefs, its four items grey since lf-1549 (no window in front) |
| clip-copy | HELLO selected, GRAPH+Y: ClipBuf HELLO, ClipLen 5, the 256 document bytes and the selection unchanged |
| clip-cut, clip-cut-menu | GRAPH+E and EDIT > CUT: the document ` WORLD` (3bc1feef), clipboard HELLO, caret (0, 0), no selection, the screen as composed |
| clip-paste2, clip-paste2-menu | copied in notepad 1, GRAPH+N, pasted in notepad 2 by key and by menu: notepad 2's 256 bytes are `HELLO` (3771117c), caret (5, 0), HELLO and the caret in its window, notepad 1's state block untouched |
| clip-lines, clip-lines-cut, clip-lines-back | `B`, a break, `C` out of AB / CD: clipboard `B\rC`; pasted into a new notepad two lines (51b8db0c); cut, `AD` (ac2ff1c8); pasted back, AB / CD again (e1909815) |
| clip-replace | pasted over WORLD: HELLO HELLO (53f0cc97) |
| clip-empty, clip-nosel | an empty clipboard pasted and cut without a selection change nothing; COPY without a selection keeps HELLO; EDIT > UNDO is picked and does nothing |
| clip-full, clip-full-after | HELLO pasted into 220 cells: DOCUMENT FULL, the 256 bytes identical (9845f0e9), caret (10, 15); OK and a key go on from there |
| clip-keys | HELP > KEYS equals the two-column compositor window (a0bf828e) |

Two things the merge with lf-1526 turned up, both fixed here:

- `NoteMouse` was never initialised; openMSX fills RAM with $FF, so
  until the first button release every frame moved the caret to the
  pointer's cell. Typing after GRAPH+N without a mouse (the saveas and
  print subjects) came out reversed at (6, 4). Init clears it.
- The TEST build died on Roms_MSX1 with Roms_Disk within a frame of
  its marker, before any subject: SP run down to $D029, PC in KEYINT,
  the disk ROM in page 1. H.TIMI was a JP to IrqTick in page 1, and a
  disk driver runs through an inter-slot call with the disk ROM mapped
  there; an interrupt then executed the disk ROM's bytes at that
  address. The baseline survived with IrqTick at $416E and died at
  $4174, by what those bytes are. An inter-slot hook (RST $30) fixed it
  but cost the 60 Hz resize release frame its last few dozen
  microseconds (`resize-screen-edge` Dropped 1, `h.timi` 61 against
  60), so IrqTick went to the end of the ROM, in page 2, which no
  inter-slot call switches. Main's lf-1525 met the same interrupt
  (files vanishing from a directory write) and runs the handler from
  work RAM (`IrqCode`); the merge with main `8fefef5` takes that and
  drops the page-2 copy and its build check. The TEST heap subject's
  blocks are 48, 40 and 32 for the 165 bytes left under the disk ROM
  once the bank backend's and the clipboard's RAM are both in.

- `sel-scroll`, lf-1526's drag scroll, reported 11 dropped frames on
  the 60 Hz C-BIOS machine, on main plus lf-1526 alone as well
  (measured in a scratch worktree without this change). Probed frame
  by frame, the drag itself was within budget: the eleven drops were
  the eleven letters of HELLO WORLD typed before it. A typed key's
  frame measured 16.1 ms there, over the 16.7 ms frame once the BIOS
  handler is counted, 2.0 ms of it the 282-byte AppSave in the
  repaint's WndFlush (icons 1.6 ms, lattice 0.9, frame 0.9, text 4.8,
  blit 1.6). Every reader of a slot's state without a select
  (ClkService, SetActivate, the hit test) flushes first and WndSelect
  flushes at a switch, so the repaint now writes the window record
  only (`WndFlushRec`): the typed key frame is 14.2 ms at 60 Hz and
  `sel-scroll` reports 0 dropped on both machines.
- On main `8fefef5` the 24x17 resize's compose frame measured 15.6 ms
  at 60 Hz on this branch against 14.4 ms on main alone, one dropped
  frame: NoteDraw asked NotePaintSelection per chunk whether the
  front window has a selection, 1.0 ms over sixteen chunks. PR #22's
  squash (`8257a17`) answers that per chunk with a 27 T test of the
  anchor row, which the merge takes in place of this branch's
  once-per-draw flag: the compose frame is 14.7 ms, Dropped 0.

Measured against main `8257a17` (PR #22's squash of lf-1526 on
`8fefef5`; 19,869 / 21,606 ROM bytes, 3,779 / 6,511 RAM), which this
branch merges: normal ROM **20,293 bytes (+424)**, TEST **22,030
(+424)**; static RAM **+226 bytes** (ClipBuf 224, ClipLen, ClipIx), to
**4,005** normal and **6,737** TEST; heap **8,283 / 2,898** bytes
without / with the disk ROM (TEST **5,551 / 166**). No heap
allocation; the KEYS window's buffer is 27 by 16 while open. Padded
ROM crc32: normal **1fcd9bae**, TEST **e46f87a8**.

Focused verification (the full `test-all.sh` is the pipeline's):

| Configuration | Groups | Assertions |
|---|---|---|
| C-BIOS_MSX1_EU, 50 Hz | `--note-selection`, `--clipboard`, `--arrange`, `--resize-scroll`, `--saveas`, `--print`, `--note-width`, `--settings`, `--dialogs`, `--desktop`, `--commander`, `--bank`, boot/mouse/keys/menus | 17, 16, 106, 20, 21, 23, 16, 156, 30, 17, 38, 20, 35 |
| C-BIOS_MSX1_JP, 60 Hz | `--resize-scroll`, `--note-selection`, `--clipboard`, `--arrange`, `--note-width` | 20, 17, 16, 106, 16 |
| Roms_MSX1 | `--tape`, `--arrange`; with Mapper512 `--bank` | 13, 106; 20 |
| Roms_MSX1 + Roms_Disk | `--settings`, `--clipboard`, `--commander`, `--bank` | 170, 16, 55, 20 |
| Roms_MSX2 | `--clipboard`, `--arrange` | 16, 106 |

## Printer follow-up (lf-1546)

Three remarks from the test of lf-1536.

**P gives back the pane's own device.** P restored what `CmdInit` gave a
pane, so a right pane that B had put on DISK came back as RAM. P now keeps
the device the pane showed in `CmdPrnWas` and returns to it: on
Roms_MSX1 + Roms_Disk, B B gives panes (5, 5) and B B P P (5, 5) again
(`print-cmd-b-back` since lf-1552).

**One PRN pane.** P from a pane that is already PRN does nothing, so TAB P
TAB P leaves (8, 1) rather than two write-only panes
(`print-cmd-one-prn`).

**ESC ends the wait.** While the printer stays busy, `PrintReady` reads
ESC (row 7, bit 2) between polls and gives up on it, waiting for the key's
release so the `PRINTER NOT READY` alert that follows does not take the
same ESC as its own. `print-esc` holds ESC from 0.5 s into the job and
releases it at 0.7 s: the alert opens after **35** interrupts (700,198 µs)
at 50 Hz and **42** (700,214 µs) at 60 Hz, instead of after `PRINTWAIT`
(150), and stays open with the document intact. No "PRINTING" status
line: the ESC abort is the way out of a long wait.

Measured against base `ca6f74d`: normal ROM **20,432 → 20,467 bytes
(+35)**, TEST ROM **22,169 → 22,204 (+35)**. Static RAM **4,005 → 4,006
(+1)**, TEST **6,737 → 6,738 (+1)**: `CmdPrnWas` sits in the commander
state, so each open commander's heap copy grows by the same byte; heap
**8,283 → 8,282** without disk, **2,898 → 2,897** with it. Padded ROM
CRC32: normal `a3cf85e2`, TEST `cbf4d59e`.

## Commander B and P on every machine (lf-1552)

Two remarks from the test of lf-1546.

**The subject proves the restore everywhere.** `print-cmd-bb-back` used
B B, which without a disk goes RAM → BANK → RAM, so the wrong restore to
RAM passed as well. `print-cmd-b-back` uses one B, which puts the right
pane on BANK on every machine in the matrix, and asserts B P P gives
BANK back: (1, 4) on C-BIOS EU and JP, (5, 4) on Roms_MSX1 + Roms_Disk.
With the lf-1546 restore swapped back to RAM it reads (1, 1) and fails.

**B from PRN goes back, like P.** B on a PRN pane went to RAM; it now
takes the device saved in `CmdPrnWas`, the one P would give back.
`print-cmd-b-prn-b` (B P B) expects (1, 4), (5, 4) with a disk; the old
B gives (1, 1) and fails it.

Measured against base `e68b6e5`: normal ROM **23,361 → 23,362 bytes
(+1)**, TEST ROM **25,118 → 25,119 (+1)**. Static RAM 4,015 and TEST
6,747 unchanged, heap 8,273 / 2,888 unchanged. Padded ROM CRC32: normal
`982a4b54`, TEST `f04a0839`.

## Small banks in the TEST subjects (lf-1540)

The lf-1525 test found two things on a 32K MSX1. The 16 KB bank there
works and is now documented above (whether it is wanted is for the
product owner; nothing in the ROM changed). And `bank-sys` wrote a
hundred files on any bank, so on 58 files it filled the bank and
`bank-rw` began with `STERR_FULL`: the rw route went untested.
`TestBankSys` now writes `min(100, BkFiles / 2)` files and the harness
works the expected entries out from that count. On C-BIOS_MSX1_EU,
`./test.sh --bank` (and the full run) also runs the TEST subjects on
`C-BIOS_MSX1_Mirror32`, a 63 block bank: `bank-sys-mirror` 29 files
under DI, work area and ROM identical; `bank-rw-mirror` 28 files to
`STERR_FULL`, `NEW` in entry 32; `bank-dir-mirror` 58 of 58 entries
parsed from the device. Normal ROM unchanged (20,467 bytes, crc32
`a3cf85e2`); TEST ROM 22,204 -> 22,221 bytes (+17); RAM and heap
unchanged.

## Segment 255 of a 4 MB mapper (lf-1545)

The lf-1525 review suspected that on a 256 segment mapper with nothing
kept out the bank's last 16K wrapped to segment 0. Measured, and worse:
three faults. `BkXferSkip` stepped over the `$FF` that ends `BkSkip` as
if it were segment 255 (index 255 became 0); with that fixed, `BkSeg`
used `$FF` for "no segment", so `BkRaw` skipped the OUT for segment 255
and the block went into whatever page 2 showed; and `BkCand` returned a
secondary of page 3's slot above page 3's own id with carry set, which
the callers read as "no more slots", so a mapper in 3-1 next to RAM in
3-0 was never probed. `BkXferSkip` now stops at the terminator, "no
segment" is a flag of its own (`BkSegOff`, set through `BkSegSel` and
`BkSegNone`) and `BkCand` clears carry.

`harness/machines/C-BIOS_MSX1_Mapper4M` is that machine: plain 64K in
3-0 under page 3, a 4096K mapper in 3-1. On C-BIOS_MSX1_EU `bank-4m`
asserts source 2, 16,384 blocks in slot `$87`, nothing kept out and the
last block at device offset `$3FFF00`; `bank-4m-note` saves a document
through the UI into the last entry (15,359) and finds it in the dumped
mapper at `$3FFF00` (crc32 `9fbabb8f`). Without the terminator check it
lands at `$3F00`, segment 0, inside the bank's own summary blocks; with
that check but `$FF` still the sentinel, at `$7F00`, segment 1.
Normal ROM 20,467 -> 20,481 bytes (+14, crc32 `a3cf85e2` -> `edbcf735`);
TEST ROM 22,221 -> 22,235 (+14); RAM 4,006 -> 4,007 bytes (+1,
`BkSegOff`), heap 8,282 -> 8,281 without disk, 2,897 -> 2,896 with it.

## Page 3 or 1/2 on segment 255 (lf-1554)

The same `$FF` stood in detection. `BkMapperUsed` starts `BkProbe` at
`$FF` and read a `$FF` there as "the marker never reached page 3", and
`BkSkipAdd` and the count stopped at `BkSkip`'s `$FF` terminator, so a
page on segment 255 of a 256 segment mapper was never kept out. Now
page 3 is on the mapper when the segment its marker names changes
through the window as `BkProbe` is written (inverted, then put back),
and `BkSkip` is four bytes counted by `BkSkipN`, with no terminator.

`harness/machines/C-BIOS_MSX1_Mapper4MRAM` has the 4096K mapper as the
only RAM, in slot 3; a breakpoint at `BkInit` copies page 3's segment to
255 and writes 255 to port `$FF` (`bank-4m-p3`) or writes 255 to port
`$FE` (`bank-4m-p2`). Asserted on C-BIOS_MSX1_EU: kept out `[0, 1, 2,
255]`, 16,128 blocks and `[0, 2, 255]`, 16,192 blocks, the last block
at `$3FBF00` (segment 254); `bank-4m-p3-note` saves through the UI into
the last entry (15,119) and finds the document (crc32 `9fbabb8f`) there.
On main the three FAIL: nothing kept out with page 3 on 255 and the
document lands in the work RAM at `$FF00`; `[0, 2]` with page 2 on 255.
Normal ROM 24,356 -> 24,383 bytes (+27); RAM unchanged (`BkSkip` 5 bytes
is now `BkSkipN` 1 + `BkSkip` 4), heap unchanged.

## Undo (lf-1530)

One level a notepad: **EDIT > UNDO** and **GRAPH+U** put the document
back as it was before the last edit group, and a second UNDO is the
redo. The snapshot is a heap block of the window's own (owner $10 plus
the slot, so closing frees it with the rest), allocated in `NoteInit`:
the 256 document bytes, the 13-byte name and the nine bytes from
`NoteCX` to `NoteBackend` (cursor, scroll, the saved and modified flags,
the backend), **278 bytes** (`UNDOSZ`). UNDO swaps those bytes with the
live ones. The ticket counted 262 (document, cursor, scroll); the name
and the flags are in it so that FILE > OPEN undone is the previous
document with its title, its device and its edited flag, not the old
text under the new file's name. When the allocation fails the notepad
opens and works as before, `NoteUndoP` is 0, UNDO does nothing and the
EDIT menu paints it grey: codes $01-$04 hold U, N, D and O from the
BIOS font in the lattice's grey (`LoadDim`, `T_DIM`), in all three
thirds. Since lf-1549 UNDO is grey as well while no snapshot has been
taken yet (a new notepad before its first edit), and CUT, COPY and
PASTE are grey on the same footing (see "EDIT items in grey").

A group starts at the first change after a cursor key, a click in the
text, ENTER, a cut, a paste, an UNDO, a load or a save, so a run of
typed characters and DELETEs is one group. Cut, paste, the erase of a
selection (DELETE, ENTER or a character typed over it) and a load are
each a group of their own. A paste takes its snapshot from the
rollback copy it already makes, after it has all gone in: a paste that
does not fit leaves the earlier snapshot alone. A save ends the group
and makes the snapshot an earlier version of the file just written:
its name and saved flag become the document's, and it is unmodified
only if its 256 bytes are the bytes written. So SAVE, typing, UNDO is
the saved text, unmodified, and closing asks nothing; the redo is
modified again. The commander sets `NoteBackend` before it loads into
a notepad; `NoteUndoBackend` puts the old device into the snapshot.

HELP > KEYS gains `U UNDO`; the calendar's three lines moved to the
second column under HOME ROW START to make room.

`./test.sh --undo` (in the default suite):

| subject | asserts |
|---|---|
| undo-type, -redo, -menu | HELLO, SHIFT+UP (on row 0, no move), ` WORLD`, GRAPH+U: `HELLO` (3771117c), caret (5, 0); again: `HELLO WORLD` (df37c4c1), caret (11, 0); the same through EDIT > UNDO |
| undo-first | HELLO, UNDO: the blank document, NoteModified 0 |
| undo-click, undo-enter | a click and ENTER end a group: only what follows is undone |
| undo-cut, -redo, undo-erase | CUT of HELLO, UNDO: the 256 bytes from before the cut, caret (5, 0), clipboard still HELLO; redo ` WORLD`; DELETE on a selection is a group of its own |
| undo-paste-before, undo-paste | `B\rC` pasted at AB / CD: AB / CB / CD (8cc09b01); UNDO: AB / CD byte for byte (e1909815), caret (1, 0) |
| undo-saved-text, undo-saved, -redo | HI, SAVE as NOTE, X, UNDO: `HI`, NoteModified 0, then GRAPH+W closes without a dialogue; UNDO UNDO then GRAPH+W asks |
| undo-past-save | HI, SAVE, UNDO: the blank document, modified, still named NOTE |
| undo-open-before, undo-open, -redo | HI, FILE > OPEN BETA, UNDO: `HI`, name NOTE, title NOTEPAD, modified 1; redo BETA (5a0ad6a7), unmodified, title BETA |
| undo-two | UNDO in a second notepad leaves the first's 256 bytes |
| undo-heap-block | the notepad's heap blocks are 144, `NOTESTSZ` and `UNDOSZ`, all owner $10; EDIT shows UNDO in the font |
| undo-heap, -after | the heap rewritten so that after the notepad's buffer and state 261 bytes are free: the notepad opens, `NoteUndoP` 0, GRAPH+U and EDIT > UNDO change nothing, no crash, the blocks are (144, 285, free 261); the UNDO cells are $01-$04 and their patterns and grey colours are in all three thirds; typing goes on |

The subject that picked UNDO as a no-op (`clip-nosel`) no longer picks it.

Measured with breakpoints (`machine_info time`), the same on C-BIOS EU
and JP: the snapshot is **1.82 ms**, paid by the first key of a group
(that key's loop iteration 11.0 ms against 9.1 ms for the next key);
the swap is **4.69 ms**, and the loop iteration with UNDO in it
**12.8 ms**, the redo the same; **Dropped 0** over typing, UNDO and the
redo on both machines. With the snapshot on top, the default suites
still pass: 630 ok lines on C-BIOS_MSX1_EU and 623 on C-BIOS_MSX1_JP.

Measured against base `cedf91c`: normal ROM **23,362 → 23,756 bytes
(+394)**, TEST ROM **25,119 → 25,513 (+394)**. Static RAM **4,015 →
4,018 (+3)**, TEST **6,747 → 6,750 (+3)**: `NoteUndoP` and `NoteUndoSt`
in the notepad's state, `NOTESTSZ` 282 → 285. Heap at boot **8,273 →
8,270** without a disk ROM, **2,888 → 2,885** with one (TEST 5,541 →
5,538 and 156 → 153). Each open notepad costs **285 heap bytes more**:
the 278-byte snapshot, its 4-byte header and the 3 state bytes.

## DEL deletes forward (lf-1572)

DEL has a key code of its own, `KEY_DEL` (7), in both keyboard tables;
BS stays `KEY_DELETE` (8). The dialogs' text field and the commander
treat `KEY_DEL` as `KEY_DELETE`, so DEL works there as before (back
over a character, delete the file).

In Notepad DEL now follows the MSX convention (`NoteForward`): the
character under the caret goes and the caret stays. At the end of a
line the next line joins this one, as BS at that line's start would;
from the padding after a line's text the caret lands at the text's end.
Where only blank lines follow the caret, the document has ended and DEL
changes nothing (no undo snapshot either). Over a selection DEL erases
it, as BS does, and a DEL is an edit for undo as a BS is: a run of
them after a cursor key is one group, the snapshot taken with the caret
where it was. A full document (the `tape-full` payload) takes a DEL at
0,0, and then a character typed there.

Joining an empty line (BS on it, or DEL at the end of the line above)
left the sixteen links as they were, so with WRAP on the window kept
the old layout although the lines below had moved up; `NbHard` now
spoils the saved links, and the whole document is laid out again.
Already so on 9046a6d for BS.

`./test.sh --del` (in the default suite), each `-on` and `-off` for
WRAP; the 256 document bytes against `note_width_document`, the caret,
NoteModified and, with WRAP on, the screen against `wrap_win`:

| subject | asserts |
|---|---|
| del-first, -mid, -twice | HELLO, SHIFT+LEFT x5 / x3, DEL: `ELLO` (2834a4f6) at (0, 0), `HELO` (04aa7282) at (2, 0); DEL DEL: `HEO` |
| del-chunk, -chunk-next | 18 letters, DEL at (13, 0) and (0, 1): N and O go across the chunk boundary, the caret stays |
| del-join, -join-empty | AB / CD, DEL at the end of AB: `ABCD` (c301e9ec), caret (2, 0); AB / blank / CD: AB / CD (e1909815) |
| bs-join-empty | BS on that blank line: AB / CD, the screen too |
| del-end, -end-blank | DEL after HELLO, and on the blank line after AB: nothing changes |
| del-selection, del-bs | DEL erases a selection; BS still deletes backwards (`HLLO`) |
| del-undo, -redo, -undo-join, -undo-noop | two DELs are one group: UNDO is HELLO at (0, 0), the redo `LLO`; a join undone; a DEL at the end takes no snapshot (UNDO is the blank document, unmodified) |
| del-dialog | the SAVE dialog's field: AB, DEL: `A` |
| tape-full-del (`--tape`) | the full tape document, DEL at 0,0: the first character gone (a3b9579d), then A typed: crc32 80e60bd4, caret (1, 0) |

`tape-full` no longer presses DEL among the refused keys. `undo-noop`
presses BS at 0,0 instead of DEL, its intent since lf-1557.

Control, these subjects on the sources of dafaf7d: every DEL subject
fails except del-selection, del-undo-join, del-undo-noop and del-dialog
(the old DEL as BS gives the same result there), and bs-join-empty-on
fails; tape-full-del fails (crc32 6b4ca544, nothing deleted).

Base 9046a6d → this change: normal ROM **24,231 → 24,327 bytes (+96)**,
TEST ROM **25,988 → 26,084 (+96)**; static RAM 4,018 and heap 8,270 /
2,885 unchanged (0 bytes). `./test.sh`: C-BIOS_MSX1_EU 701 ok, JP 694
ok, all subjects pass; `MSX_MACHINE=Roms_MSX1 ./test.sh --tape` 22 ok.

## EDIT items in grey (lf-1549)

UNDO was painted grey when the notepad had no snapshot block
(lf-1530); CUT, COPY and PASTE were always black, also when they had
nothing to act on. Now `MenuPaint`'s last word, `NoteEditDim`, paints
every EDIT item that would do nothing in grey:

- **CUT and COPY** when the front notepad has no selection
  (`NoteSelectionBounds` Z, the test `NoteClipCopy` makes itself);
- **PASTE** when the clipboard is empty (`ClipLen` 0, as `NoteMenuPaste`
  tests);
- **UNDO** when there is nothing to swap in: no snapshot block
  (`NoteUndoP` 0, as before) or none taken yet (`UNDO_HAVE` clear, a
  new notepad before its first edit). The ticket's first criterion
  wants UNDO grey on a new notepad and black after typing, which the
  block test alone did not give: the state bit is the one addition to
  UNDO's rule, its mechanism is untouched;
- **all four** when the front window is not a notepad (the clock, the
  commander) or there is no window: none of them has a notepad to act
  on. UNDO was painted black there before; grey is what the other three
  get and what "a grey item does nothing" promises, so UNDO follows.

The actions check the same things themselves, so a grey item picked
with the mouse, or its GRAPH key, does nothing and says nothing, as
before. An open menu owns the keyboard (`HdlKey`), so ENTER on an item
is not an input the menu has.

The grey glyphs: `DimLetters` in menus.inc is the eleven distinct
letters of the four items, `UNDOCTPYASE`, U N D O first so that they
keep $01-$04 (undo-heap asserts those). `LoadDim` copies each from the
BIOS font to `T_DIM` + its index, in all three thirds, with the
lattice's grey as its colour: **seven new tile codes**, $05-$0B, C T P
Y A S E. VRAM: 7 x 8 bytes x 3 thirds in the pattern table and the
same in the colour table, **336 bytes**, all of them in the otherwise
unused range below $20, so no glyph moved. `NoteDimItem` paints item B
by walking its text and replacing each letter by `T_DIM` plus its place
in `DimLetters`; a character not in there (none today) stays as it is.

`./test.sh --edit-dim` (in the default suite); the screen is the
compositor's notepad with `menu_open_nt` over it and the grey codes
from the ROM's `DimLetters` (`menu_dim_nt`):

| subject | asserts |
|---|---|
| dim-glyphs | `DimLetters` is `UNDOCTPYASE` at $01-$0B, the set of letters of the four items; each code's pattern is its letter's BIOS glyph and its colours $EF, in all three thirds |
| dim-new, dim-new-block | a new notepad, EDIT: CUT, COPY, PASTE and UNDO grey (05 01 06 / 05 04 07 08 / 07 09 0a 06 0b / 01 02 03 04) though `NoteUndoP` is set and `NoteUndoSt` 0 |
| dim-typed | HI typed: UNDO in the font, the rest grey |
| dim-select, dim-copied, dim-escape | HELLO selected by SHIFT+RIGHT: CUT and COPY in the font, PASTE grey, the selection on the screen; after COPY nothing grey; after ESC CUT and COPY grey, PASTE in the font |
| dim-drag | HELLO selected by a mouse drag: the same four rows, a selection in `NoteAnchor` |
| dim-clock, dim-commander | EDIT over the clock (GRAPH+K) and over VIEW > COMMANDER: all four grey, the clock's screen against the compositor |
| dim-saved | HELLO WORLD saved as LETTER, HELLO on the clipboard, no selection: CUT and COPY grey, PASTE and UNDO in the font |
| dim-pick-cut, dim-pick-copy | the grey CUT and COPY picked by the mouse there: the 256 bytes, clipboard HELLO, caret (5, 0), NoteModified 0, NoteSaved 1, the menu closed, the screen as composed, MenuPick 0 and 1 |
| dim-pick-paste, dim-pick-undo | the grey PASTE and UNDO picked on a new notepad: blank, ClipLen 0, NoteModified 0, no dialogue, the screen as composed |
| clip-menu (`--clipboard`) | EDIT with no window: all four grey |
| fb-edit-menu (`--frames`) | EDIT opened over a 24x17 notepad at 50 and 60 Hz: the four grey items over the notepad as composed, Dropped 0 |

Measured in fb-edit-menu (`machine_info time` between `FrameWatch` and
`MainLoop`, C-BIOS EU and JP the same): the press frame that opens
EDIT over the 24x17 notepad, the paint and the four grey items in it,
is **7.4 ms**, the blit frame after it 5.3 ms, against a budget of
16.7 ms at 60 Hz; the route's longest frame is the grip's, 11.6 ms;
**Dropped 0** on both machines.

Base d5bf146 → this change: normal ROM **24,572 → 24,671 bytes (+99)**,
TEST ROM **26,329 → 26,428 (+99)**; static RAM 4,018 and heap 8,270 /
2,885 unchanged (0 bytes). `./test.sh --edit-dim` 15 ok, `--frames` 8
ok, `--clipboard` 22 ok and `--undo` 44 ok on C-BIOS_MSX1_EU and JP.
crc32 of the padded ROM: normal 655ed564, TEST 879ef520.

## Commander: import from tape (lf-1575)

After upstream ZX Desk's eb98c06 "Commander: import files from tape"
(MIT). A tape has no directory, so on a machine with a cassette alone
a file could only be opened from tape through the notepad; in
Commander the tape was no source. With a tape pane active, **I** reads
the **next** file on the tape and writes it under its header's name to
the device of the **other** pane (RAM, DISK or BANK), and **A** reads
them **all**, stopping when the first name comes round again, the tape
ends (TAPION finds no leader, or CTRL+STOP) or ESC or SPACE is held
between two files (the printer's convention, lf-1546; within a block
the BIOS's CTRL+STOP is the way out). After that they are ordinary
files in the listing: copy, delete, open. Upstream's keys are R and A;
R is LIST here, so I and A.

- The tape pane's one row says `I NEXT A ALL`, its header `TAPE`, and
  with it active the second help row is `I IMPORT NEXT A IMPORT ALL`
  (28 columns): ENTER, C and D have no file to act on there. I and A
  on any other pane mean nothing.
- `TapeNext` reads header blocks into `TapeIn` until one carries the
  MSXT magic, passing over the others (another format's header, a
  payload left behind by a refused file) and blocks shorter than a
  header; carry with nothing read is the tape's end. FILE > OPEN keeps
  its own seek, which fails on a wrong block rather than wait for a
  leader that is not coming. `TapeBlock` reads a block with the PSG and
  the interrupt mask the BIOS's, as `TapeOpen` does.
- The file is read whole into `CmdBuf` and written under its header's
  name; a name SAVE AS on the **target** would refuse becomes `NOTE`
  (lf-1551: `NoteNameValidFor` takes the device, so twelve characters
  without a dot stay a name on RAM and are NOTE on the disk). A file of
  the name already there is replaced, as upstream does, so importing a
  tape again brings its versions.
- One that does not fit is refused whole with the alert `DOES NOT FIT`
  over its name and no half file left: a header past 256 bytes (left
  unread), `STERR_FULL` from the open (four files on RAM, a full
  directory), or a short write (a disk without free clusters), after
  which the entry is deleted. The files before it keep their
  `IMPORTED` status under the alert; A refused on its very first file
  reads `READY` under it, as I does, since the first name is kept only
  once a file went in (lf-1598).
- PRN in the other pane: `PRN IS NO TARGET`, nothing read. The empty
  tape: `NO FILE ON TAPE`. A stopped by a key: `IMPORT STOPPED`.
- In a TAPE=1 build the ring of B and SETTINGS > BACKEND is RAM, BANK,
  DISK, TAPE (`StRingStep`, `StAvail`), so a pane on a disk machine can
  be the tape; the normal build's ring is as it was.

The harness has a **tape trap** (`tape_trap` in `harness/run.tcl`, the
way emulators trap the ZX's LD_BYTES): a breakpoint on the ROM's
`TapeGetBlock` delivers the next block of a Tcl list in place of the
BIOS cassette routines, with carry and the unread count as the BIOS
leaves them, so the import runs headlessly on C-BIOS too.
`tape_import_checks` (`./test.sh --tape-import`, and in `--tape` and
the default suite, on the TAPE=1 ROM) asserts, each from boot:

| subject | asserts |
|---|---|
| imp-next, imp-next-screen | I with ALPHA and BETA on the tape: ALPHA in RAM byte for byte, status IMPORTED, two blocks left; the screen against the compositor with the tape pane's hint and help row |
| imp-open-ram | the UI round trip: I, TAB, ENTER opens the imported file in a notepad, the 256 bytes the tape held, name ALPHA, owned by RAM |
| imp-all, imp-all-end | A over ALPHA, BETA, ALPHA, BETA: both once, the repeated header and its payload read, two blocks left; A over ALPHA, BETA to the tape's end: both, none left |
| imp-none, imp-none-all | I and A on an empty tape: NO FILE ON TAPE, no alert |
| imp-skip | a block of 19 `$FF` and one of five bytes before ALPHA are passed over |
| imp-big | a header of 300 bytes: the alert, nothing written, the payload unread |
| imp-full, imp-full-all, imp-full-after | four files on RAM: I and A, the alert, the store as it was, status READY (A read ALPHA's payload, two blocks left); two files and a tape of three: ALPHA and BETA in, GAMMA's alert over status IMPORTED |
| imp-replace | ALPHA on RAM with other bytes: replaced by the tape's |
| imp-abort-esc, imp-abort-space | ESC or SPACE held while A runs: ALPHA imported, BETA not read, IMPORT STOPPED |
| imp-prn | P then I: PRN IS NO TARGET, nothing read, the PRN header |
| imp-name-space, imp-name-long | `MY FILE` arrives as NOTE, `ABCDEFGHIJKL` as itself on RAM |
| imp-note-twice, imp-same-tail | A over `A*B`, `C*D`, GAMMA: NOTE holds C*D's bytes, GAMMA follows, IMPORTED, none left; a third header `ALPHA` with other bytes behind its terminator and another payload is the first name again: ALPHA and BETA as first written, none left |
| imp-not-tape | I and A on the RAM pane: nothing |
| imp-open-disk, imp-disk-screen, imp-name-long-disk, imp-disk-full (Roms_MSX1 + Roms_Disk) | B B B TAB puts the right pane on TAPE; I writes ALPHA to the FAT12 image and TAB, ENTER opens it from the disk pane, owned by the disk; the screen; the twelve-character name is NOTE on the disk; a disk without free clusters: the alert, the image as it was |

Run on this change: `--tape-import` 19 ok on C-BIOS_MSX1_EU and JP, 23
ok on Roms_MSX1 + Roms_Disk; `--tape` 43 ok on Roms_MSX1 (the existing
cassette subjects on the real BIOS included); `--commander` 41,
`--print` 27 and `--bank` 30 ok on C-BIOS_MSX1_EU. The import reads
the tape with interrupts masked, as the notepad's tape load does, and
is outside the zero-drop frame budget.

Base `5264508` → this change (code before the `NsanTab` alignment):
normal ROM **25,486 → 26,061 bytes (+575)**, TEST ROM **27,240 →
27,815 (+575)**, TAPE=1 ROM **25,494 → 26,079 (+585)**; static RAM
**3,997 (+0)**, TEST 6,729; heap **8,291 / 2,906** unchanged. The
first name of an import lives in `TapeHdr`'s name field, free while
no tape file is open; no new allocation. crc32 of the padded ROMs:
normal **b4c269ad**, TEST **7d46248f**, TAPE=1 **a7a73d17**.

**Review of the change (lf-1598).** PR #60 went through the pipeline
without a test or a review (an OAuth failure in both steps), so this
pass checked it against lf-1575's points and ran the whole suite. On
main `12e61ad` as merged, `./test.sh` is 765 ok on C-BIOS_MSX1_EU and
755 ok on C-BIOS_MSX1_JP, nothing red. One deviation: `CmdImport` kept
the first name before the first file's write, so A on a full target
reported `IMPORTED` under the `DOES NOT FIT` alert with nothing
imported (`imp-full-all`, status 11 on 12e61ad). The name is now kept
once a file went in, which also drops the key test before the compare:
I never has a name there. ROM 26,150 → 26,149 bytes (-1), TEST
27,904 → 27,903, TAPE=1 26,168 → 26,167; RAM 3,997 and the heap
unchanged. Confirmed unchanged by inspection: FILE > OPEN on tape
(`TapeOpen` untouched, the `tape-*` subjects), and the normal build's
ring (`IF TAPE` around the fourth entry). Seen then and fixed in
lf-1603 below: a COPY towards a tape pane, which the TAPE=1 ring can
show on a disk machine, opened the tape for reading first. With
the fix `./test.sh` is 766 ok on C-BIOS_MSX1_EU and 756 ok on
C-BIOS_MSX1_JP (`--tape-import` 20); crc32 of the padded ROMs: normal
**c07b1605**, TEST **550efb04**, TAPE=1 **c7cde283**.

**The tape-came-round stop and the NOTE fallback (lf-1602).** Seen by
dev3 testing lf-1598: A over `A*B`, `C*D`, `GAMMA` imported one NOTE
and stopped, two blocks left. The first name was kept from `CmdWork`
after `CmdImportName`'s NOTE fallback and compared over all 13 bytes,
so the second refused name was `NOTE` too and passed for the first
name coming round; `MY FILE` and `OTHER ONE` only differed in the
header bytes left behind the terminator (`NOTE\0LE`). The name kept
and compared is now the header's own, `TapeIn+4` as the tape holds it,
and the compare ends at the terminator, so what a header carries
behind the name is not part of it. The "anything written yet" flag
moved from the name's first byte to `TapeHdr+17`, the length field,
free as the name field is while no tape file is open. `imp-note-twice`
and `imp-same-tail` assert both halves; on `0850c2d`'s source both
fail on EU and JP (one NOTE, two blocks left; ALPHA rewritten). ROM
+7 bytes in each build, RAM and the heap unchanged.

**A tape as the target (lf-1603).** Two observations from lf-1598.
dev3, on Roms_MSX1 + Roms_Disk with the right pane on TAPE: C on a
disk file hung the machine until CTRL+STOP and then said `COPIED`
with nothing recorded. `CmdCopy` checked whether the target existed
with an open for reading, which on the tape is `TapeOpen` waiting for
a leader. dev4, by reading: with a tape in both panes I and A made
the tape itself the target, so the open for writing took `TapeHdr`
(the magic and the name, and A's first name in it) and the close
recorded over the tape being read.

- A tape has no directory, so for `ST_TAPE` there is no existence
  check to make: `CmdCopy` goes from the source's read straight to the
  write (`CmdTapeCopy`, `CcpWrite`), which `TapeSave` records at the
  close. Before a byte goes out the status row says `RECORDING,
  CTRL+STOP ENDS`, on the screen now rather than at the frame's end
  (`CmdNotice`: the row into the window's buffer, `WinRedraw` as the
  frame's end would do it, `NtFlush` with the address pair under
  `SetWrt`'s DI), since the BIOS holds the frame for the recording as
  it does for a SAVE to tape. A recording that went through is
  `COPIED`; CTRL+STOP or a failed block is carry from the close,
  `STORAGE ERROR` and no `COPIED` (the delete of the half file is the
  tape's `StNoDelete`, nothing). PRN keeps its own route.
- I and A with the tape in the other pane: `TAPE IS NO TARGET` (status
  16, the PRN route with its own text) before a block is read.
  `TapeHdr` is not touched.

The harness has the tape trap's other half, a **write trap**
(`tape_put_trap` in `harness/run.tcl`): a breakpoint on `TapePutBlock`
keeps every block the ROM would record in `::taped` and returns in its
place, carry clear, or carry with `::tape_put_fail` set, the BIOS's
CTRL+STOP during the block; the name table as it stands at that moment
goes into the log with the block, the screen the user sees while the
tape runs. `tape_copy_checks` (`./test.sh --tape-copy`, and in
`--tape-import`, `--tape` and the default suite, on the TAPE=1 ROM)
asserts, each from boot, with BETA on the read trap's tape and ALPHA
on the source (RAM in the right pane on a bare machine, TAB; the disk
in the left pane on the disk machine, B B B for TAPE on the right,
dev3's route):

| subject | asserts |
|---|---|
| cp-tape, cp-tape-recording, cp-tape-screen | C: no read in the log and BETA still on the tape, the header on the long leader and the 256 bytes on the short one, byte for byte; status COPIED, the source as it was; the name table at the header's write is the Commander with `RECORDING, CTRL+STOP ENDS` on its status row, against the compositor; the screen after, `COPIED` |
| cp-tape-stop, cp-tape-stop-screen | carry on the header: no block kept, the payload not attempted, `STORAGE ERROR`, no alert, the source as it was |
| imp-both-tape, imp-both-tape-all, and -screen | B round to TAPE in the other pane (on the disk machine both panes round), a pattern in `TapeHdr`, then I or A: status 16, no read, no write, the pattern still there, RAM empty; the screen with `TAPE` over both panes and `TAPE IS NO TARGET` |
| cp-tape-wav (`--tape`, Roms_MSX1) | the real BIOS: ALPHA on RAM, `cassetteplayer new`, TAB, C; the WAV decoded in Python is the 19 byte header and the 256 bytes, status COPIED. No cassette plays, so a read before the write would wait for a leader and the run would time out |

On `ea13d77`'s source cp-tape fails on EU (one read, one block
left), cp-tape-recording fails (no notice), and imp-both-tape and
imp-both-tape-all fail (status IMPORTED, two and three reads, two
blocks recorded, `TapeHdr` rewritten). With the change: `--tape-copy`
9 ok on C-BIOS_MSX1_EU and JP; `--tape` 54 ok on Roms_MSX1 (cp-tape-wav
among them, the 256 bytes at crc32 7dcac7b5 in the WAV); `--tape-import`
35 ok on Roms_MSX1 + Roms_Disk; `--commander` 51 ok on EU and JP
(`CmdPaintReady` is now the nought of `CmdPaintStatus`).

Base `ea13d77` → this change (code before the `NsanTab` alignment):
normal ROM **26,191 → 26,270 bytes (+79)**, TEST ROM **27,945 →
28,024 (+79)**, TAPE=1 ROM **26,209 → 26,288 (+79)**; static RAM
**3,997 (+0)**, heap **8,291 / 2,906** unchanged. crc32 of the padded
ROMs: normal **c6efd7ca**, TEST **0c70c71e**, TAPE=1 **63e875b7**.

## The file in the tape buffer served again, and the band while the tape runs (lf-1576)

Upstream da60fed and 277c7ca, and dev4's observation on lf-1603
(lf-1610). A file the tape has just played (`TapeOpen` for reading
brings its header into `TapeIn` and its bytes into `TapeBuf`) asked
for again by name went back to the tape, which had played past it,
so the BIOS waited on silence with the desktop stopped. And the BIOS
holds the frame for every transfer: a SAVE or SAVE AS to tape from
the notepad recorded with the FILE menu or the SAVE AS dialogue still
on the screen (the shadow had them closed, nothing had flushed it),
and a read waited with nothing saying how to end it.

- **Served from the buffer.** `TapeOpen` for reading with a name that
  is the file the buffer still holds puts `TapePos` at 0 and succeeds
  without a BIOS call (`TapeHeld`). Held means: `TapeKeep` set, which
  only the read that brought the file in sets; the name equal to the
  header's in `TapeIn+4` through its terminator (what a header carries
  behind the name is not part of it, lf-1602); and the bytes summing
  to `TapeSum` as they did when they came in. An empty name is the
  next file on the tape, never the buffer; another name goes to the
  tape, and the read that follows replaces the buffer.
- **MSX-specific: the buffer is the notepad's.** The wrap table, the
  layout job's and the painters' scratch live in `TapeBuf` (note.inc,
  `TapeBuf+0` to `+214`), so the flag drops wherever the notepad takes
  the buffer: `NwStartJob` (the table), `NoteDraw` (the painters),
  `NoteKey` (the key's scratch), `NoteLayService` once it works
  (`NlsLive`); and where the header or the buffer go to other use: the
  open for writing (`TapeFresh`), `TapeNext` (Commander's import reads
  another header into `TapeIn`), `StInit`. The sum is the backstop for
  a path that writes the buffer without dropping the flag: the open
  goes to the tape, the doubt resolved the tape's way. `TapeSumBuf` is
  the sum so far rotated left a bit plus the byte, 256 bytes in
  **25,388 cycles, 7.1 ms** (openMSX breakpoints at `TapeSumBuf` and
  at `TsbLoop` with BC nought), at a tape open only.
- **The band says what runs.** Before the first block of a read
  (`TapeOpen`, and `TapeBlock` for Commander's import) the status band
  says `READING TAPE, CTRL+STOP ENDS`; before the header of a
  recording (`TapeSave`, so every writer: the notepad's SAVE and SAVE
  AS, Commander's C, which keeps its own `RECORDING` on its status row
  from lf-1603) it says `RECORDING, CTRL+STOP ENDS`, the one string
  `TxtCmdRecording`. `TapeNotice` clears the band (the key echoes with
  it, `StatCol` to 0), prints inverted as the band is, and `NtFlush`
  puts out every dirty row: the band and the rows a menu's close or a
  dialogue's answer left, so the user looks at the desktop while the
  tape runs. `TapeShown` is set, and the first frame after the
  transfer (`TapeBandIdle`, before the loop's `NtFlush`) gives the band
  back blank, whichever caller ran the tape: no exit to chase in the
  import's status, refuse and error paths. A SAVE ended by CTRL+STOP is
  the existing route: carry from the close, `COULD NOT SAVE`, the
  document modified and not saved.

No UI route on the MSX asks the tape by name for a read (FILE > OPEN
on the tape takes the next file; a note kept elsewhere loads from its
own device; Commander's panes list no tape file), so the buffer hit is
asserted in the TEST build with the read trap armed before boot
(`Run(..., setup=)`, `tape_trap_tcl`): `TestTapeHeld` records seven
pairs. The read trap now logs the name table at every block, as the
write trap does, the screen while the tape runs.

| subject | asserts |
|---|---|
| tape-held (TEST build, every machine) | ALPHA, BETA, ALPHA, ALPHA on the tape: ALPHA read (two blocks), ALPHA again from the buffer (no block), BETA from the tape, ALPHA from the tape (BETA was in the buffer), a byte of the buffer changed by a path that drops no flag sends ALPHA to the tape (the sum), `NwStartFull` drops the flag and the open after it finds the tape run out, carry and STERR_NOTFOUND; nine blocks asked for, the tape empty. It runs first of the subjects and every TEST build run arms its tape (the bank runs too: on Roms_MSX1 the real BIOS waited on TAPION for ever, the run timed out), with a breakpoint that removes itself at the ninth read, before the bank fill. The breakpoint fires only with this ROM's `ld ix,TAPION` at the address: `TapeGetBlock` is in page 1, where a disk ROM in slot 1 runs its own code at boot and in every BDOS call, and on Roms_MSX1 + Roms_Disk its boot took one block off the tape (pairs 01 05, 01 05, 01 05 before the fix) |
| imp-reading (`--tape-import`) | at both blocks of I the band reads `READING TAPE, CTRL+STOP ENDS` over the Commander with READY on its row, against the compositor; imp-next-screen has the band blank after |
| cp-tape-recording | now the Commander with `RECORDING` on its row and on the band |
| note-tape-save, note-tape-saveas-recording, note-tape-save-recording (`--tape-note`, TAPE=1, not the disk machine) | FILE > NEW, A, FILE > SAVE (SAVE AS: LETTER), A, FILE > SAVE: two recordings of the header and the document; at both headers the band says `RECORDING, CTRL+STOP ENDS` with the menu's rows and the dialogue's as on the screen after; saved, unmodified, the band blank after |
| note-tape-stop | carry on the header: nothing recorded, `COULD NOT SAVE`, modified 1, saved 0, the document kept |
| note-tape-read, note-tape-reading | FILE > OPEN on a new note of the tape's: LETTER in, the name set, the band saying `READING TAPE` at both blocks with the menu gone, blank after |

On `ab93a19`'s sources with this harness, note-tape-saveas-recording,
note-tape-save-recording and note-tape-reading fail on C-BIOS_MSX1_EU
(the band empty, the menu or the dialogue on the screen at the block);
the others pass there, the save and the load themselves being right.
With the change: `--tape-note` 6 ok on EU; `--tape-import` 38 ok on
EU and JP (imp-reading, cp-tape-recording among them); `--sound`
(TEST build, sound-transfer-TapeOpen through the stub, settings) on
EU and `--settings` on JP, tape-held ok; `--tape` 61 ok on Roms_MSX1
(the real BIOS: note-tape, tape-error, tape-name, cp-tape-wav with the
notices in the path); `--tape-import` 36 ok on Roms_MSX1 + Roms_Disk
(the notepad group skipped there: a new note is the disk's).

Base `ab93a19` → this change (the last byte before `NsanTab`'s $FF
pad, read from the ROM): normal ROM **27,212 → 27,443 bytes (+231)**,
TEST **28,966 → 29,297 (+331)**, TAPE=1 **27,230 → 27,461 (+231)**;
build line 27,648 → 27,904 and 29,440 → 29,696 (the pad: `NsanTab` a
page up); static RAM **3,997 → 4,001 (+4)**: `TapeKeep` 1, `TapeSum`
2, `TapeShown` 1; the TEST build 14 more for `TthResults` (`CmdBuf`
is taken: `TbResults`, the bank subject's record, sits at
`CmdBuf+221`, and the first cut of this record over it failed bank-rw
in the full suite), **6,729 → 6,747**; heap **8,291 → 8,287 / 2,906 →
2,902**, no heap allocation. Per frame: `TapeBandIdle` is a load, a
test and a return, 45 cycles. crc32 of the padded ROMs: normal
**6ad150f4**, TEST **24f3f3c2** (TestTapeHeld first in TestRun), TAPE=1 **f8ff14f1** (base dd9db94c /
b5928469 / 814e16cf, the same three from `git archive ab93a19`).
