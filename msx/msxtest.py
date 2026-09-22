#!/usr/bin/env python3
"""Assemble the MSX ROM, boot it headlessly in openMSX and assert on memory.

    ./msxtest.py            build, run every subject, assert

Checksums and memory values are the assertion; build/out/<subject>/shot.png
is for people. The model is zxtest.py in the ZX tree, with openMSX
standing in for the Python Z80 because the subjects here are VRAM, the
PPI and the PSG, none of which a bare CPU model has.

Runs inside the msx/docker image (pasmo 0.5.5, openMSX, Xvfb, xdotool on
PATH); msx/test.sh wraps the docker invocation.
"""
import os
import subprocess
import sys
import time
import zlib

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(ROOT, "src", "msxdesk.asm")
BUILD = os.path.join(ROOT, "build")
ROM = os.path.join(BUILD, "msxdesk.rom")
SYM = os.path.join(BUILD, "msxdesk.sym")
TROM = os.path.join(BUILD, "msxtest.rom")   # the TEST=1 build: the subjects run at boot
TSYM = os.path.join(BUILD, "msxtest.sym")
OUT = os.path.join(BUILD, "out")
MACHINE = os.environ.get("MSX_MACHINE", "C-BIOS_MSX1_EU")

ROMBASE = 0x4000
WORK = 0xC000
RAMTOP = 0xF380         # the BIOS work area starts here; the stack sits below it
STACK = 0x0380          # what is left for the stack: $F000-$F380
NT, SAT, PGT = 0x1800, 0x1B00, 0x0000
COLS, ROWS = 32, 24
ACCEL = (1, 2, 3, 5, 7)


def load_symbols(path):
    syms = {}
    for line in open(path):
        parts = line.split()
        if len(parts) >= 3 and parts[1].upper() == "EQU":
            try:
                syms[parts[0]] = int(parts[2].rstrip("Hh"), 16)
            except ValueError:
                pass
    return syms


def assemble(rom, sym, equ=()):
    args = ["pasmo", "-I", os.path.join(ROOT, "src")]
    for e in equ:
        args += ["--equ", e]
    r = subprocess.run(args + ["--bin", SRC, rom, sym], capture_output=True, text=True)
    for line in r.stderr.splitlines():
        if not line.startswith("WARNING: Var") and "3 pass" not in line:
            print(line)
    if r.returncode != 0:
        sys.exit("assembly failed")
    if os.path.getsize(rom) != 0x8000:
        sys.exit(f"{rom} is {os.path.getsize(rom)} bytes, expected 32768")
    return load_symbols(sym)


def build():
    """Assemble both builds, and refuse if the budgets are blown: a ROM
    past $C000 or RAM into the stack does not fail loudly at run time."""
    os.makedirs(BUILD, exist_ok=True)
    syms = assemble(ROM, SYM)
    tsyms = assemble(TROM, TSYM, ["TEST=1"])
    for name, sy in (("msxdesk.rom", syms), ("msxtest.rom", tsyms)):
        rom_end, ram_end, heap_end = sy["RomEnd"], sy["RamEnd"], sy["HEAPEND"]
        print(f"{name}: code ${ROMBASE:04X}-${rom_end:04X}, {rom_end - ROMBASE} bytes, "
              f"{0xC000 - rom_end} free; RAM ${WORK:04X}-${ram_end:04X}, {ram_end - WORK} bytes, "
              f"heap {heap_end - ram_end} to ${heap_end:04X}, {RAMTOP - heap_end} reserve")
        if ram_end > heap_end or heap_end > RAMTOP - STACK:
            sys.exit(f"BUILD FAILED: RAM ends at ${ram_end:04X}, heap at ${heap_end:04X}, "
                     f"the stack reserve starts at ${RAMTOP - STACK:04X}")
    return syms, tsyms


def ensure_display():
    """Xvfb, if nothing answers on DISPLAY. openMSX's SDL init asserts
    under the dummy video driver, so a real X server it is."""
    disp = os.environ.setdefault("DISPLAY", ":99")
    if subprocess.run(["xdpyinfo", "-display", disp], capture_output=True).returncode == 0:
        return
    subprocess.Popen(["Xvfb", disp, "-screen", "0", "800x600x24"],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(50):
        if subprocess.run(["xdpyinfo", "-display", disp], capture_output=True).returncode == 0:
            return
        time.sleep(0.1)
    sys.exit("Xvfb did not come up")


class Run:
    """One boot of the ROM through a step list: (frames, tcl) pairs, the
    frames counted on the ROM's own counter. Holds the dumps."""

    def __init__(self, syms, steps, name, rom=ROM):
        self.syms = syms
        out = os.path.join(OUT, name)
        os.makedirs(out, exist_ok=True)
        for f in ("vram.bin", "ram.bin", "bios.bin", "vdp.bin", "log.txt", "shot.png"):
            try:
                os.remove(os.path.join(out, f))
            except FileNotFoundError:
                pass
        steps_path = os.path.join(out, "steps.tcl")
        with open(steps_path, "w") as f:
            f.write("set steps {\n")
            for frames, cmd in steps:
                f.write(f"  {{{frames} {{{cmd}}}}}\n")
            f.write("}\n")
        env = dict(os.environ, MSXTEST_OUT=out, MSXTEST_STEPS=steps_path,
                   MSXTEST_FRAMES=str(syms["Frames"]),
                   SDL_AUDIODRIVER="dummy", HOME=os.environ.get("HOME", "/tmp"))
        cmd = ["openmsx", "-machine", MACHINE, "-cart", rom, "-romtype", "page12",
               "-script", os.path.join(ROOT, "harness", "run.tcl")]
        r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=180)
        logp = os.path.join(out, "log.txt")
        self.log = open(logp).read() if os.path.exists(logp) else ""
        if r.returncode != 0 or "shot:" not in self.log:
            print(r.stdout, r.stderr, self.log)
            sys.exit(f"openMSX run '{name}' failed (rc {r.returncode})")
        self.vram = open(os.path.join(out, "vram.bin"), "rb").read()
        self.ram = open(os.path.join(out, "ram.bin"), "rb").read()
        self.bios = open(os.path.join(out, "bios.bin"), "rb").read()
        self.vdp = open(os.path.join(out, "vdp.bin"), "rb").read()
        assert len(self.vram) == 16384 and len(self.ram) == 16384

    def peek(self, name, n=1):
        a = self.syms[name] - WORK
        return int.from_bytes(self.ram[a:a + n], "little")

    def peek16_at(self, addr):
        return int.from_bytes(self.ram[addr - WORK:addr - WORK + 2], "little")

    def peeks(self, name):
        v = self.peek(name)
        return v - 256 if v > 127 else v

    def bytes(self, name, n):
        a = self.syms[name] - WORK
        return self.ram[a:a + n]

    def nt(self):
        return self.vram[NT:NT + COLS * ROWS]

    def sat(self, i):
        return tuple(self.vram[SAT + i * 4:SAT + i * 4 + 4])

    def ptr(self):
        return self.peek("PtrX"), self.peek("PtrY")

    def counts(self):
        return tuple(self.bytes("EvCounts", 4))    # ptrmove, btndown, btnup, key


def check(fails, name, ok, detail):
    print(f"  {name:14s} {detail} {'ok' if ok else 'FAIL'}")
    if not ok:
        fails.append(name)


def expected_nt():
    """The screen the ROM paints, built here from the same layout rules."""
    bar = bytearray(b" " * COLS)
    bar[1:1 + 28] = b"MSX DESK  FILE   VIEW   HELP"
    rows = [bytes(bar), bytes([0x81]) * COLS] + [bytes([0x80]) * COLS] * 21 + [bytes([0xA0]) * COLS]
    return b"".join(rows)


def ramp(frames):
    """Pixels the pointer keys move over `frames` frames held, as ReadInput
    computes it: the hold count climbs to 32 and indexes the ramp by eighths."""
    hold, total = 0, 0
    for _ in range(frames):
        hold = min(hold + 1, 32)
        total += ACCEL[hold >> 3]
    return total


def boot_checks(syms, fails):
    print("boot:")
    r = Run(syms, [(60, "")], "boot")
    check(fails, "marker", r.bytes("Marker", 6) == b"ZXMSX\0", repr(r.bytes("Marker", 6)))
    nt = r.nt()
    check(fails, "nametable", nt == expected_nt(),
          f"crc32 {zlib.crc32(nt):08x}, expected {zlib.crc32(expected_nt()):08x}")
    rom = open(ROM, "rb").read()
    tiles = rom[syms["Tiles"] - ROMBASE:syms["TILESEND"] - ROMBASE]
    for third in range(3):
        base = PGT + third * 0x800 + 0x80 * 8
        check(fails, f"tiles-{third}", r.vram[base:base + len(tiles)] == tiles,
              f"{len(tiles)} bytes at ${base:04X}")
    # The font is the BIOS's own, from its CGTABL pointer, so this holds
    # on C-BIOS and on a real BIOS alike.
    cgtabl = int.from_bytes(r.bios[4:6], "little")
    font = r.bios[cgtabl + 32 * 8:cgtabl + 128 * 8]
    for third in range(3):
        base = PGT + third * 0x800 + 32 * 8
        inv = base + 0x80 * 8
        check(fails, f"font-{third}", r.vram[base:base + len(font)] == font and r.vram[inv:inv + len(font)] == font,
              f"glyphs 32-127 from BIOS ${cgtabl:04X}, crc32 {zlib.crc32(font):08x}, and again at $A0")
    check(fails, "colours",
          all(r.vram[0x2000 + t * 0x800 + 32 * 8:0x2000 + t * 0x800 + 128 * 8] == b"\x1f" * 768
              and r.vram[0x2000 + t * 0x800 + 0xA0 * 8:0x2000 + t * 0x800 + 0x100 * 8] == b"\xf1" * 768
              for t in range(3)),
          "font $1F and inverted bank $F1 in all three thirds")
    import re
    sp = int(re.search(r"sp (\d+)", r.log).group(1))
    check(fails, "stack", 0xF000 < sp <= 0xF380, f"SP ${sp:04X} at the dump, reserve $F000-$F380")
    shape = rom[syms["PtrShape"] - ROMBASE:syms["PtrShape"] - ROMBASE + 32]
    check(fails, "sprite-shape", r.vram[0x3800:0x3820] == shape, "32 bytes at $3800")
    check(fails, "vdp-regs", r.vdp[1] == 0xE2 and r.vdp[7] == 0x0F,
          f"R1 ${r.vdp[1]:02X} (16x16 sprites), R7 ${r.vdp[7]:02X} (white border)")
    check(fails, "sprite-attr", r.sat(0) == (89, 120, 0, 1) and r.sat(1)[0] == 0xD0,
          f"sprite 0 {r.sat(0)}, sprite 1 y {r.sat(1)[0]}")
    frames, irqs, dropped = r.peek("Frames", 2), r.peek("IrqCnt", 2), r.peek("Dropped", 2)
    check(fails, "h.timi", irqs == frames and frames >= 60 and dropped == 0,
          f"frames {frames}, interrupts {irqs}, dropped {dropped}")
    check(fails, "ptr-idle", r.ptr() == (120, 90) and r.counts() == (0, 0, 0, 0),
          f"pointer {r.ptr()}, events {r.counts()}")
    print(f"  whole VRAM crc32 {zlib.crc32(r.vram):08x}")
    return r.vram


def mouse_checks(syms, fails, vram0):
    # Plug the mouse, park the Xvfb pointer inside the window, then move.
    # Measured on openMSX 20.0: a host move of n pixels arrives as -n/2,
    # and the ROM subtracts, so the pointer moves with the host.
    print("mouse:")
    # Parking is itself a move, so the pointer and the counters are put
    # back before the measured moves.
    reset = (f"debug write memory {syms['PtrX']} 120; debug write memory {syms['PtrY']} 90; "
             f"debug write memory {syms['EvLastX']} 120; debug write memory {syms['EvLastY']} 90; "
             f"debug write memory {syms['EvCounts']} 0")
    r = Run(syms, [(10, "plug joyporta mouse"),
                   (10, "exec xdotool mousemove 300 200"), (10, reset),
                   (10, "mouse_move 20 0"), (10, "mouse_move 0 -30"),
                   (5, "key_down 8 0x01"), (5, "key_up 8 0x01"), (10, "")], "mouse")
    check(fails, "ptr-moved", r.ptr() == (130, 75), f"pointer {r.ptr()}, expected (130, 75)")
    check(fails, "hit-desktop", r.peek("LastHit") == 5, f"press at (130,75) hit {r.peek('LastHit')}, CTL_DESKTOP is 5")
    check(fails, "sprite-follows", r.sat(0)[:2] == (74, 130), f"sprite 0 {r.sat(0)}")
    c = r.counts()
    check(fails, "ev-ptrmove", c == (2, 1, 1, 1), f"events {c}, expected (2, 1, 1, 1): SPACE is a key as well as the button")
    check(fails, "nt-untouched", r.nt() == vram0[NT:NT + COLS * ROWS], "name table unchanged")


def key_checks(syms, fails):
    print("keys:")
    # RIGHT (row 8 bit 7) held 20 frames, then DOWN (bit 6) 10 frames.
    r = Run(syms, [(5, "key_down 8 0x80"), (20, "key_up 8 0x80"),
                   (5, "key_down 8 0x40"), (10, "key_up 8 0x40"), (5, "")], "cursor")
    # Injection lands within a frame either way of the step, so the ROM
    # counts the frames it saw each key held and the ramp is recomputed
    # for that count: the arithmetic is exact, the hold length is read.
    hx, hy = r.peek("HeldX"), r.peek("HeldY")
    want = (120 + ramp(hx), 90 + ramp(hy))
    check(fails, "cursor-ramp", r.ptr() == want and 19 <= hx <= 21 and 9 <= hy <= 11,
          f"pointer {r.ptr()}, expected {want} for {hx} and {hy} frames held")
    c = r.counts()
    check(fails, "cursor-events", c[3] == 0 and c[0] == hx + hy, f"events {c}: {hx + hy} moves, no keys")

    # A (row 2 bit 6) tapped for 3 frames, SHIFT+1 (row 6 bit 0, row 0 bit 1)
    # for 3 frames, SPACE (row 8 bit 0) as the button for 5 frames.
    r = Run(syms, [(5, "key_down 2 0x40"), (3, "key_up 2 0x40"),
                   (5, "key_down 6 0x01; key_down 0 0x02"), (3, "key_up 0 0x02; key_up 6 0x01"),
                   (5, "key_down 8 0x01"), (5, "key_up 8 0x01"), (5, "")], "keys")
    c = r.counts()
    check(fails, "key-events", c[3] == 3 and c[1] == 1 and c[2] == 1,
          f"events {c}: 3 keys, one press, one release")
    status = r.nt()[STATROW * COLS:STATROW * COLS + 4]
    check(fails, "status-echo", status == bytes(c | 0x80 for c in b"A!  "), f"status row {status!r}, inverted bank")
    check(fails, "last-key", r.peek("LastKey") == ord(" "), f"last key {r.peek('LastKey')}")

    # Held for 30 frames: one press, one repeat at 20, then every 3.
    r = Run(syms, [(5, "key_down 3 0x01"), (30, "key_up 3 0x01"), (5, "")], "repeat")
    c = r.counts()
    held = r.peek("KbdHeld")
    want = 1 + (0 if held < 20 else 1 + (held - 20) // 3)
    check(fails, "key-repeat", c[3] == want and 29 <= held <= 31, f"events {c}: expected {want} keys for {held} frames held")
    check(fails, "repeat-echo", r.nt()[STATROW * COLS:STATROW * COLS + 5] == bytes([ord("C") | 0x80] * 5),
          f"status row {r.nt()[STATROW * COLS:STATROW * COLS + 6]!r}")


STATROW = 23


def heap_walk(r, base, end):
    out, p = [], base
    while p < end:
        size = r.peek16_at(p)
        out.append((p, size, r.ram[p + 2 - WORK], r.ram[p + 3 - WORK]))
        p += 4 + size
    return out


def test_build_checks(tsyms, fails):
    """The correctness subjects run at boot in the TEST build and leave
    records in RAM; this reads them back. The model is zxtest.py's
    heap_checks, calendar_checks and BStoreTest."""
    import datetime
    print("test build:")
    r = Run(tsyms, [(60, "")], "subjects", rom=TROM)
    check(fails, "subjects-ran", r.peek("TestDone") == 1, "TestDone flag")

    # heap: three blocks split off exactly, a free returns the block
    # untouched, freeing by owner coalesces everything into one piece
    base, end = tsyms["HeapBase"], tsyms["HEAPEND"]
    ptrs = [r.peek16_at(tsyms["ThPtr"] + i * 2) for i in range(3)]
    stats = [(r.peek16_at(tsyms["ThStat"] + i * 4), r.peek16_at(tsyms["ThStat"] + i * 4 + 2)) for i in range(3)]
    blocks = heap_walk(r, base, end)
    check(fails, "heap-split", ptrs == [base + 4, base + 4 + 1152 + 4, base + 4 + 1152 + 4 + 676 + 4],
          f"blocks at {[hex(p) for p in ptrs]}")
    # The first block, owner $10, is never freed; the free by owner takes
    # $12 and coalesces its neighbours into one piece. The walk happens
    # after TestApp, which took three bytes for the calendar's state.
    total = end - base - 4
    check(fails, "heap-stat", stats[0][0] == total - 1152 - 676 - 300 - 12 and stats[1][0] == stats[0][0] + 676
          and stats[2] == (total - 1156, total - 1156),
          f"free/biggest after alloc {stats[0]}, after free {stats[1]}, after free by owner {stats[2]}; heap {total}")
    check(fails, "heap-coalesce", len(blocks) == 3 and blocks[0][1:] == (1152, 1, 0x10) and blocks[1][1:] == (3, 1, 0x10)
          and blocks[2] == (base + 1156 + 7, total - 1156 - 7, 0, 0),
          f"{len(blocks)} blocks: {blocks[:3]}")

    # calendar: 1,200 months against Python's calendar
    months = r.ram[tsyms["TcMonths"] - WORK:tsyms["TcMonths"] - WORK + 2400]
    wrong = []
    for year in range(1980, 2080):
        for month in range(12):
            i = ((year - 1980) * 12 + month) * 2
            first = datetime.date(year, month + 1, 1)
            nxt = datetime.date(year + 1, 1, 1) if month == 11 else datetime.date(year, month + 2, 1)
            if months[i] != first.weekday() or months[i + 1] != (nxt - first).days:
                wrong.append((year, month + 1, months[i], first.weekday(), months[i + 1], (nxt - first).days))
    check(fails, "cal-months", not wrong, f"1200 months, {len(wrong)} wrong {wrong[:3]}")
    grid = r.ram[tsyms["TcGrid"] - WORK:tsyms["TcGrid"] - WORK + 9 + 6 * 21]
    head = grid[:8]
    weeks = [grid[9 + w * 21:9 + w * 21 + 20] for w in range(6)]
    want = [b"                1  2", b" 3  4  5  6  7  8  9", b"10 11 12 13 14 15 16",
            b"17 18 19 20 21 22 23", b"24 25 26 27 28 29 30", b"31                  "]
    check(fails, "cal-grid", head == b"AUG 2026" and weeks == want, f"{head!r} {weeks[0]!r} .. {weeks[5]!r}")
    sel = r.ram[tsyms["TcSel"] - WORK:tsyms["TcSel"] - WORK + 9]
    check(fails, "cal-walk", tuple(sel) == (46, 6, 31, 46, 7, 1, 46, 8, 1),
          f"back from 1 Aug {tuple(sel[:3])}, ENTER {tuple(sel[3:6])}, midnight on the 31st {tuple(sel[6:])}")

    # storage: 64 bytes round trip, four files, a fifth refused, a delete
    st = {k: r.peek(k, n) for k, n in (("TsWrote", 2), ("TsRead", 2), ("TsBad", 1), ("TsErrCode", 1),
                                        ("TsDirN", 1), ("TsFull", 1), ("TsDel", 1), ("TsDirAfter", 1))}
    check(fails, "store-rw", st["TsWrote"] == 64 and st["TsRead"] == 64 and st["TsBad"] == 0 and st["TsErrCode"] == 0,
          f"wrote {st['TsWrote']}, read {st['TsRead']}, {st['TsBad']} wrong, err {st['TsErrCode']}")
    check(fails, "store-dir", st["TsDirN"] == 4 and st["TsFull"] == 8 and st["TsDel"] == 0 and st["TsDirAfter"] == 1,
          f"{st['TsDirN']} files, fifth open err {st['TsFull']} (STERR_FULL 8), delete {st['TsDel']}, "
          f"fourth entry after delete {'gone' if st['TsDirAfter'] else 'present'}")

    # app model: the calendar's state saved and loaded back
    check(fails, "app-desc", r.peek("TaDesc", 2) == tsyms["AppCal"], f"AppAt -> ${r.peek('TaDesc', 2):04X}")
    stt = r.ram[tsyms["TaState"] - WORK:tsyms["TaState"] - WORK + 6]
    check(fails, "app-swap", tuple(stt) == (46, 7, 30, 46, 7, 30), f"after init {tuple(stt[:3])}, after load {tuple(stt[3:])}")

    # hit testing on the cell grid
    hits = tuple(r.ram[tsyms["ThitRes"] - WORK:tsyms["ThitRes"] - WORK + 5])
    check(fails, "hit-table", hits == (4, 5, 0, 0, 6), f"bar, desktop, status, off edge, open drop: {hits}, expected (4, 5, 0, 0, 6)")


def menu_defs(syms):
    """The menu definitions read out of the ROM, so the oracle paints
    from the same data the ROM does."""
    rom = open(ROM, "rb").read()
    defs = []
    p = syms["MenuDefs"] - ROMBASE
    for _ in range(4):
        col, width, dropw, count = rom[p:p + 4]
        ip = int.from_bytes(rom[p + 4:p + 6], "little") - ROMBASE
        items = []
        for _ in range(count):
            e = rom.index(b"\0", ip)
            items.append(rom[ip:e])
            ip = e + 1
        defs.append((col, width, dropw, items))
        p += 6
    return defs


def menu_checks(syms, fails, vram0):
    """A pull down: opened from the bar, painted over the desktop with
    the title inverted, closed by a pick or a press elsewhere with the
    cells underneath put back exactly."""
    print("menus:")
    defs = menu_defs(syms)
    nt0 = vram0[NT:NT + COLS * ROWS]

    def press_at(x, y):
        return [(5, f"debug write memory {syms['PtrX']} {x}; debug write memory {syms['PtrY']} {y}; "
                    f"debug write memory {syms['EvLastX']} {x}; debug write memory {syms['EvLastY']} {y}"),
                (5, "key_down 8 0x01"), (5, "key_up 8 0x01")]

    def expected_open(menu):
        col, width, dropw, items = defs[menu - 1]
        nt = bytearray(nt0)
        for c in range(col, col + width):
            nt[c] ^= 0x80
        x = min(col, COLS - dropw)
        for i, item in enumerate(items):
            row = (1 + i) * COLS
            nt[row + x:row + x + dropw] = (b" " + item).ljust(dropw)
        return bytes(nt)

    # FILE, title at column 10: a press at x 88 is cell 11
    r = Run(syms, press_at(88, 3) + [(10, "")], "menu-open")
    check(fails, "menu-open", r.peek("MenuOpen") == 2 and r.nt() == expected_open(2),
          f"MenuOpen {r.peek('MenuOpen')}, name table crc32 {zlib.crc32(r.nt()):08x}, "
          f"expected {zlib.crc32(expected_open(2)):08x}")
    # ZX DESK, at the left edge, and HELP, whose drop is nudged in from the right
    r = Run(syms, press_at(8, 3) + [(10, "")], "menu-open-1")
    check(fails, "menu-open-1", r.peek("MenuOpen") == 1 and r.nt() == expected_open(1),
          f"MenuOpen {r.peek('MenuOpen')}, crc32 {zlib.crc32(r.nt()):08x}")
    r = Run(syms, press_at(200, 3) + [(10, "")], "menu-open-4")
    check(fails, "menu-open-4", r.peek("MenuOpen") == 4 and r.nt() == expected_open(4),
          f"MenuOpen {r.peek('MenuOpen')}, crc32 {zlib.crc32(r.nt()):08x}")
    # open FILE, then pick SAVE, the third item, on row 3
    r = Run(syms, press_at(88, 3) + press_at(96, 27) + [(10, "")], "menu-pick")
    check(fails, "menu-pick", (r.peek("MenuPick"), r.peek("MnLastMenu"), r.peek("MenuOpen")) == (2, 2, 0),
          f"pick {r.peek('MenuPick')} from menu {r.peek('MnLastMenu')}, open {r.peek('MenuOpen')}")
    check(fails, "menu-restore", r.nt() == nt0, f"name table crc32 {zlib.crc32(r.nt()):08x} after the pick, "
          f"boot {zlib.crc32(nt0):08x}")
    # open VIEW, press on the desktop away from it: no pick, put back
    r = Run(syms, press_at(140, 3) + press_at(40, 120) + [(10, "")], "menu-away")
    check(fails, "menu-away", (r.peek("MenuPick"), r.peek("MenuOpen"), r.peek("LastHit")) == (0xFF, 0, 4)
          and r.nt() == nt0,
          f"pick {r.peek('MenuPick')}, open {r.peek('MenuOpen')}, last hit {r.peek('LastHit')} (the bar press), "
          f"crc32 {zlib.crc32(r.nt()):08x}")


# ---- windows: a Python compositor as the oracle
T_LEFT, T_RIGHT, T_BOTTOM, T_BL, T_BR, T_CLOSE = 0x82, 0x83, 0x84, 0x85, 0x86, 0x87
CAL_W, CAL_H, ABOUT_W, ABOUT_H = 23, 10, 18, 5
ABOUT_TEXT = [b"MSX DESK", b"AFTER ZX DESK BY", b"DAMIAN COOPER"]


def window_buffer(w, h, title, front, lines):
    """The frame as WinDraw composes it, then the application's rows:
    a list of (row, col, bytes, inverted)."""
    fill = 0xA0 if front else 0x20
    buf = bytearray([T_CLOSE] + [fill] * (w - 1))
    for i, ch in enumerate(title):
        buf[1 + i] = ch | (0x80 if front else 0)
    for _ in range(h - 2):
        buf += bytes([T_LEFT]) + b" " * (w - 2) + bytes([T_RIGHT])
    buf += bytes([T_BL]) + bytes([T_BOTTOM]) * (w - 2) + bytes([T_BR])
    for row, col, text, inv in lines:
        for i, ch in enumerate(text[:w - col]):
            buf[row * w + col + i] = ch | (0x80 if inv else 0)
    return bytes(buf)


def calendar_lines(year, month, sel):
    import calendar
    lines = [(1, 1, f"{calendar.month_abbr[month].upper()} {year}".encode(), False),
             (2, 1, b"MO TU WE TH FR SA SU", False)]
    first, length = calendar.monthrange(year, month)
    cells = [b"  "] * first + [f"{d:2d}".encode() for d in range(1, length + 1)]
    cells += [b"  "] * (42 - len(cells))
    for w in range(6):
        lines.append((3 + w, 1, b" ".join(cells[w * 7:w * 7 + 7]), False))
    idx = first + sel - 1
    lines.append((3 + idx // 7, 1 + (idx % 7) * 3, f"{sel:2d}".encode(), True))
    return lines


def compose(nt0, windows):
    """windows back to front: (x, y, w, h, title, lines); the last is front."""
    nt = bytearray(nt0)
    for i, (x, y, w, h, title, lines) in enumerate(windows):
        buf = window_buffer(w, h, title, i == len(windows) - 1, lines)
        for r in range(h):
            nt[(y + r) * COLS + x:(y + r) * COLS + x + w] = buf[r * w:(r + 1) * w]
    return bytes(nt)


def cal_win(x, y, sel=1):
    return (x, y, CAL_W, CAL_H, b"CALENDAR", calendar_lines(1980, 1, sel))


def about_win(x, y):
    return (x, y, ABOUT_W, ABOUT_H, b"ABOUT", [(1 + i, 1, t, False) for i, t in enumerate(ABOUT_TEXT)])


def window_checks(syms, fails, vram0):
    print("windows:")
    nt0 = vram0[NT:NT + COLS * ROWS]

    def press_at(x, y, hold=5):
        return [(5, f"debug write memory {syms['PtrX']} {x}; debug write memory {syms['PtrY']} {y}; "
                    f"debug write memory {syms['EvLastX']} {x}; debug write memory {syms['EvLastY']} {y}"),
                (5, "key_down 8 0x01"), (hold, "key_up 8 0x01")]

    open_cal = press_at(140, 3) + press_at(148, 19)     # VIEW, then CALENDAR on row 2
    open_about = press_at(8, 3) + press_at(16, 11)      # MSX DESK, then ABOUT on row 1

    def rec(r, slot):
        base = syms["WndTab"] + slot * 15
        return tuple(r.ram[base - WORK:base - WORK + 4])

    r = Run(syms, open_cal + [(10, "")], "win-cal")
    want = compose(nt0, [cal_win(4, 4)])
    check(fails, "win-cal", r.peek("WndCount") == 1 and r.nt() == want,
          f"{r.peek('WndCount')} window, crc32 {zlib.crc32(r.nt()):08x}, expected {zlib.crc32(want):08x}")

    r = Run(syms, open_cal + open_about + [(10, "")], "win-two")
    want = compose(nt0, [cal_win(4, 4), about_win(10, 13)])
    z = tuple(r.ram[syms["WndZ"] - WORK:syms["WndZ"] - WORK + 2])
    check(fails, "win-two", r.peek("WndCount") == 2 and z == (1, 0) and r.nt() == want,
          f"{r.peek('WndCount')} windows, z {z}, crc32 {zlib.crc32(r.nt()):08x}, expected {zlib.crc32(want):08x}")

    # a press on the calendar's title raises it; dragged two cells right
    # and one down with the mouse, button held through SPACE
    drag = [(10, "plug joyporta mouse"), (10, "exec xdotool mousemove 300 200"),
            (5, f"debug write memory {syms['PtrX']} 50; debug write memory {syms['PtrY']} 35; "
                f"debug write memory {syms['EvLastX']} 50; debug write memory {syms['EvLastY']} 35"),
            (5, "key_down 8 0x01"), (5, "mouse_move 32 16"), (10, "key_up 8 0x01"), (10, "")]
    r = Run(syms, open_cal + open_about + drag, "win-drag")
    want = compose(nt0, [about_win(10, 13), cal_win(6, 5)])
    z = tuple(r.ram[syms["WndZ"] - WORK:syms["WndZ"] - WORK + 2])
    check(fails, "win-drag", z == (0, 1) and rec(r, 0)[:2] == (6, 5) and r.nt() == want,
          f"z {z}, calendar at {rec(r, 0)[:2]}, expected (6, 5), crc32 {zlib.crc32(r.nt()):08x}, "
          f"expected {zlib.crc32(want):08x}")
    check(fails, "drag-frames", r.peek("Dropped", 2) == 0 and r.peek("Frames", 2) > 60,
          f"{r.peek('Frames', 2)} frames, {r.peek('Dropped', 2)} dropped")

    # the close box of the front window
    r = Run(syms, open_cal + open_about + drag + press_at(50, 42) + [(10, "")], "win-close")
    want = compose(nt0, [about_win(10, 13)])
    used = [b for b in heap_walk(r, syms["HeapBase"], syms["HEAPEND"]) if b[2]]
    check(fails, "win-close", r.peek("WndCount") == 1 and r.nt() == want,
          f"{r.peek('WndCount')} window, crc32 {zlib.crc32(r.nt()):08x}, expected {zlib.crc32(want):08x}")
    check(fails, "close-heap", [b[3] for b in used] == [0x11] and used[0][1] == ABOUT_W * ABOUT_H,
          f"used blocks {[(b[1], hex(b[3])) for b in used]}: the about window's buffer only")

    # SHIFT+RIGHT moves the selection, ENTER makes it today
    keys = [(5, "key_down 6 0x01; key_down 8 0x80"), (3, "key_up 8 0x80; key_up 6 0x01"),
            (5, "key_down 7 0x40"), (3, "key_up 7 0x40"), (10, "")]
    r = Run(syms, open_cal + keys, "win-keys")
    want = compose(nt0, [cal_win(4, 4, sel=2)])
    today = tuple(r.ram[syms["TodayY"] - WORK:syms["TodayY"] - WORK + 3])
    check(fails, "win-keys", r.peek("CalSel") == 2 and today == (0, 0, 2) and r.nt() == want,
          f"selection {r.peek('CalSel')}, today {today}, crc32 {zlib.crc32(r.nt()):08x}, expected {zlib.crc32(want):08x}")


def main():
    syms, tsyms = build()
    ensure_display()
    fails = []
    vram0 = boot_checks(syms, fails)
    mouse_checks(syms, fails, vram0)
    key_checks(syms, fails)
    menu_checks(syms, fails, vram0)
    window_checks(syms, fails, vram0)
    test_build_checks(tsyms, fails)
    if fails:
        print("FAILED:", ", ".join(fails))
        return 1
    print("all subjects pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
