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
EXT = os.environ.get("MSX_EXT", "")        # e.g. Roms_Disk: a disk interface in slot 1
DISK = os.path.join(BUILD, "disk.dsk")       # the 720K image the disk backend writes

ROMBASE = 0x4000
WORK = 0xC000
RAMTOP = 0xF380         # HIMEM on a bare machine; a disk ROM lowers it
STACK = 0x0380          # STACKRES: the stack, under HIMEM; the heap ends below it
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
        rom_end, ram_end = sy["RomEnd"], sy["RamEnd"]
        heap_end = RAMTOP - STACK               # on a bare machine; a disk ROM takes more
        print(f"{name}: code ${ROMBASE:04X}-${rom_end:04X}, {rom_end - ROMBASE} bytes, "
              f"{0xC000 - rom_end} free; RAM ${WORK:04X}-${ram_end:04X}, {ram_end - WORK} bytes, "
              f"heap {heap_end - ram_end} to ${heap_end:04X} without a disk ROM, {0xDE77 - STACK - ram_end} with one")
        if ram_end > 0xDE77 - STACK:
            sys.exit(f"BUILD FAILED: RAM ends at ${ram_end:04X}, past a disk machine's heap start")
    return syms, tsyms


ROMS = os.path.join(ROOT, "roms")


def ensure_roms():
    """Real BIOS ROMs, if any, from msx/roms (gitignored: not ours to
    distribute) into the place openMSX looks: it matches them by sha1,
    the file names do not matter. C-BIOS has no cassette, so the tape
    backend needs a real machine: the configs in harness/machines
    (Roms_MSX1, Roms_MSX2) are built on the dumps in that directory."""
    home = os.environ.get("HOME", "/tmp")
    share = os.path.join(home, ".openMSX", "share")
    os.makedirs(share, exist_ok=True)
    for name, src in (("systemroms", ROMS), ("machines", os.path.join(ROOT, "harness", "machines")),
                      ("extensions", os.path.join(ROOT, "harness", "extensions"))):
        link = os.path.join(share, name)
        if os.path.isdir(src) and not os.path.exists(link):
            os.symlink(src, link)


def ensure_display():
    """Xvfb, if nothing answers on DISPLAY. openMSX's SDL init asserts
    under the dummy video driver, so a real X server it is."""
    ensure_roms()
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


# ---- a 720K MSX-DOS floppy image, built and read here rather than
# through openMSX's diskmanipulator, so the oracle owns the format
SECTOR, ROOT_ENTRIES, FAT_SECTORS = 512, 112, 3
ROOT_SECTOR = 1 + 2 * FAT_SECTORS           # boot, two FATs
DATA_SECTOR = ROOT_SECTOR + ROOT_ENTRIES * 32 // SECTOR


def make_disk_image(path, files=None):
    """Blank, formatted: a BPB for 720K (2 sides, 80 tracks, 9 sectors,
    2 sectors a cluster, media $F9), two FATs, an empty root directory."""
    img = bytearray(1440 * SECTOR)
    boot = bytearray(SECTOR)
    boot[0:3] = b"\xEB\xFE\x90"
    boot[3:11] = b"MSXDESK "
    import struct
    struct.pack_into("<HBHBHHBHHHHI", boot, 11, SECTOR, 2, 1, 2, ROOT_ENTRIES, 1440, 0xF9, FAT_SECTORS, 9, 2, 0, 0)
    # The disk ROM loads sector 0 to $C000 and calls offset $1E; a RET
    # there says there is no DOS to boot and Disk BASIC carries on.
    # Without it the zeros ran as code and the ROM asked "Drive name?".
    boot[0x1E] = 0xC9
    boot[510:512] = b"\x55\xAA"
    img[0:SECTOR] = boot
    for f in range(2):
        img[(1 + f * FAT_SECTORS) * SECTOR:(1 + f * FAT_SECTORS) * SECTOR + 3] = b"\xF9\xFF\xFF"
    # Seed real FAT12 files for UI tests; the emulator still does every operation.
    cluster = 2
    for index, (filename, data) in enumerate((files or {}).items()):
        assert index < ROOT_ENTRIES
        stem, _, ext = filename.partition(".")
        assert 0 < len(stem) <= 8 and len(ext) <= 3
        entry = ROOT_SECTOR * SECTOR + index * 32
        img[entry:entry + 11] = stem.encode().ljust(8) + ext.encode().ljust(3)
        img[entry + 11] = 0x20
        struct.pack_into("<HI", img, entry + 26, cluster if data else 0, len(data))
        chunks = (len(data) + 1023) // 1024
        for part in range(chunks):
            n = cluster + part
            value = n + 1 if part + 1 < chunks else 0xFFF
            for fat_index in range(2):
                pos = (1 + fat_index * FAT_SECTORS) * SECTOR + n * 3 // 2
                old = int.from_bytes(img[pos:pos + 2], "little")
                value16 = (old & 0x000F) | (value << 4) if n & 1 else (old & 0xF000) | value
                img[pos:pos + 2] = value16.to_bytes(2, "little")
            pos = (DATA_SECTOR + (n - 2) * 2) * SECTOR
            payload = data[part * 1024:(part + 1) * 1024]
            img[pos:pos + len(payload)] = payload
        cluster += chunks
    with open(path, "wb") as fh:
        fh.write(img)


def read_disk_image(path):
    """The root directory as {name: bytes}, following FAT12 chains."""
    img = open(path, "rb").read()
    fat = img[SECTOR:SECTOR + FAT_SECTORS * SECTOR]

    def next_cluster(n):
        i = n * 3 // 2
        v = fat[i] | (fat[i + 1] << 8)
        return (v >> 4) if n & 1 else (v & 0xFFF)

    files = {}
    for e in range(ROOT_ENTRIES):
        ent = img[ROOT_SECTOR * SECTOR + e * 32:ROOT_SECTOR * SECTOR + e * 32 + 32]
        if ent[0] in (0x00, 0xE5) or ent[11] & 0x18:
            continue
        name = ent[0:8].rstrip(b" ").decode()
        ext = ent[8:11].rstrip(b" ").decode()
        if ext:
            name += "." + ext
        size = int.from_bytes(ent[28:32], "little")
        cluster, data = int.from_bytes(ent[26:28], "little"), b""
        while 2 <= cluster < 0xFF0 and len(data) < size:
            s = DATA_SECTOR + (cluster - 2) * 2
            data += img[s * SECTOR:(s + 2) * SECTOR]
            cluster = next_cluster(cluster)
        files[name] = data[:size]
    return files


class Run:
    """One boot of the ROM through a step list: (frames, tcl) pairs, the
    frames counted on the ROM's own counter. Holds the dumps."""

    def __init__(self, syms, steps, name, rom=ROM, files=None):
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
        # The cartridge goes in slot 2 so a disk interface extension takes
        # slot 1 and the BIOS runs its init first: it must lower HIMEM and
        # plant the BDOS jump before this ROM's Init reads them.
        cmd = ["openmsx", "-machine", MACHINE, "-cartb", rom, "-romtype", "page12",
               "-script", os.path.join(ROOT, "harness", "run.tcl")]
        if EXT:
            make_disk_image(DISK, files)
            cmd += ["-ext", EXT, "-diska", DISK]
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
    himem = int.from_bytes(r.ram[0xFC4A - WORK:0xFC4C - WORK], "little")
    heap_end = r.peek("HeapEnd", 2)
    check(fails, "stack", himem - STACK < sp <= himem and heap_end == himem - STACK and heap_end > syms["RamEnd"],
          f"HIMEM ${himem:04X}, SP ${sp:04X} at the dump, heap ends ${heap_end:04X}, "
          f"{heap_end - syms['RamEnd']} bytes of heap")
    check(fails, "backend", r.peek("StBackend") == (5 if EXT else 1),
          f"storage backend {r.peek('StBackend')}: {'disk (5)' if EXT else 'RAM (1)'}, BDOS entry ${r.ram[0xF37D - WORK]:02X}")
    shape = rom[syms["PtrShape"] - ROMBASE:syms["PtrShape"] - ROMBASE + 32]
    check(fails, "sprite-shape", r.vram[0x3800:0x3820] == shape, "32 bytes at $3800")
    # bit 7 of R1 is the TMS9918's 4K/16K select; a V9938 has no such
    # bit and reads it back clear
    check(fails, "vdp-regs", (r.vdp[1] & 0x7F) == 0x62 and r.vdp[7] == 0x0F,
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
                   (5, "key_down 6 0x02"), (5, "key_up 6 0x02"), (10, "")], "mouse")
    check(fails, "ptr-moved", r.ptr() == (130, 75), f"pointer {r.ptr()}, expected (130, 75)")
    # a fast move: 100 host pixels in one frame is 50 for the ROM, and
    # a clamp of 16 (the first version's) threw 34 of them away
    r2 = Run(syms, [(10, "plug joyporta mouse"), (10, "exec xdotool mousemove 300 200"), (10, reset),
                    (5, "mouse_move 100 0"), (5, "mouse_move 0 60"), (10, "")], "mouse-fast")
    check(fails, "ptr-fast", r2.ptr() == (170, 120), f"pointer {r2.ptr()}, expected (170, 120): nothing clamped away")
    check(fails, "hit-desktop", r.peek("LastHit") == 5, f"press at (130,75) hit {r.peek('LastHit')}, CTL_DESKTOP is 5")
    check(fails, "sprite-follows", r.sat(0)[:2] == (74, 130), f"sprite 0 {r.sat(0)}")
    c = r.counts()
    check(fails, "ev-ptrmove", c == (2, 1, 1, 0), f"events {c}, expected (2, 1, 1, 0): CTRL clicks without typing")
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
    # for 3 frames, SPACE (row 8 bit 0) as text for 5 frames.
    r = Run(syms, [(5, "key_down 2 0x40"), (3, "key_up 2 0x40"),
                   (5, "key_down 6 0x01; key_down 0 0x02"), (3, "key_up 0 0x02; key_up 6 0x01"),
                   (5, "key_down 8 0x01"), (5, "key_up 8 0x01"), (5, "")], "keys")
    c = r.counts()
    check(fails, "key-events", c[3] == 3 and c[1] == 0 and c[2] == 0,
          f"events {c}: 3 keys, no button events")
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
    tape_counts = b"".join(bytes((carry,)) + count.to_bytes(2, "little")
                           for carry, count in ((0, 256), (0, 0), (1, 0),
                                                (0, 64), (0, 192), (0, 0), (1, 0)))
    check(fails, "tape-buffer", r.bytes("TtResults", 21) == tape_counts
          and r.bytes("NoteBuf", 256) == bytes((-i) % 256 for i in range(256)),
          "full/short writes, partial reads, EOF and invalid/closed handles; 256 pattern bytes")
    backend = 5 if EXT else 1
    saved = bytes((0x4D, 1, 2, 0, 1))
    default = bytes((0x4D, 1, 1, 0, backend))
    check(fails, "settings-roundtrip", r.bytes("TsetResults", 12) == (b"\0" + saved) * 2,
          "SetSave, clear record, SetLoad: " + r.bytes("TsetResults", 12).hex())
    check(fails, "settings-headers", r.bytes("TsetResults", 24)[12:] == (b"\1" + default) * 2,
          "wrong magic and version both return carry and defaults")
    check(fails, "settings-apply", r.peek("AccelPtr", 2) == tsyms["AccelTabs"] + 10,
          "fast ramp selected")
    applied = bytes((1, 1, 2, 2, 3, 1, 2, 3, 5, 7, 2, 3, 5, 7, 11, 13, 243)) + default
    check(fails, "settings-input", r.bytes("TsetApplied", 22) == applied,
          "three ramps, Y negation on/off, invalid payload clamped: " + r.bytes("TsetApplied", 22).hex())
    check(fails, "settings-backend", r.peek("StBackend") == 1,
          "save restores selected RAM backend, including on disk")
    parsed = r.bytes("CmdBuf", 120)
    expected_fcb = (b"NOTE       ", b"NOTE    TXT", b"ABCDEFGHXYZ")
    check(fails, "disk-83-names", all(parsed[i * 12] == 0 and parsed[i * 12 + 1:i * 12 + 12] == expected
          for i, expected in enumerate(expected_fcb)) and all(parsed[i * 12] == 1 for i in range(3, 10)),
          "3 valid 8.3 names uppercased/padded; 7 empty, overlong, wildcard or path names rejected")


    # heap: three blocks split off exactly, a free returns the block
    # untouched, freeing by owner coalesces everything into one piece
    base, end = tsyms["HeapBase"], r.peek("HeapEnd", 2)
    ptrs = [r.peek16_at(tsyms["ThPtr"] + i * 2) for i in range(3)]
    stats = [(r.peek16_at(tsyms["ThStat"] + i * 4), r.peek16_at(tsyms["ThStat"] + i * 4 + 2)) for i in range(3)]
    blocks = heap_walk(r, base, end)
    A, B, C = 256, 200, 128
    check(fails, "heap-split", ptrs == [base + 4, base + 4 + A + 4, base + 4 + A + 4 + B + 4],
          f"blocks at {[hex(p) for p in ptrs]}")
    # The first block, owner $10, is never freed; the free by owner takes
    # $12 and coalesces its neighbours into one piece. The walk happens
    # after TestApp, which took three bytes for the calendar's state.
    total = end - base - 4
    check(fails, "heap-stat", stats[0][0] == total - A - B - C - 12 and stats[1][0] == stats[0][0] + B
          and stats[2] == (total - A - 4, total - A - 4),
          f"free/biggest after alloc {stats[0]}, after free {stats[1]}, after free by owner {stats[2]}; heap {total}")
    check(fails, "heap-coalesce", len(blocks) == 3 and blocks[0][1:] == (A, 1, 0x10) and blocks[1][1:] == (3, 1, 0x10)
          and blocks[2] == (base + A + 4 + 7, total - A - 4 - 7, 0, 0),
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
    # four files in RAM and the fifth refused; on a disk the fifth is
    # created too, so after the delete a fourth entry is still there
    disk = r.peek("SetDevice") == 5
    want_dir = (4, 0, 0, 0) if disk else (4, 8, 0, 1)
    got_dir = (st["TsDirN"], st["TsFull"], st["TsDel"], st["TsDirAfter"])
    check(fails, "store-dir", got_dir == want_dir,
          f"{'disk' if disk else 'RAM'}: {st['TsDirN']} files, fifth open err {st['TsFull']}, delete {st['TsDel']}, "
          f"fourth entry after delete {'gone' if st['TsDirAfter'] else 'present'}; expected {want_dir}")
    if disk:
        files = read_disk_image(DISK)
        names = sorted(files)
        check(fails, "store-disk", names == ["NOTE1", "NOTE3", "NOTE4", "SETTINGS"] and files["SETTINGS"] == bytes((0x4D, 1, 2, 0, 1)),
              f"on the image: {[(n, len(files[n])) for n in names]}")

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
                (5, "key_down 6 0x02"), (5, "key_up 6 0x02")]

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
    for i, ch in enumerate(title[:w - 1]):
        buf[1 + i] = ch | (0x80 if front else 0)
    for _ in range(h - 2):
        buf += bytes([T_LEFT]) + b" " * (w - 2) + bytes([T_RIGHT])
    buf += bytes([T_BL]) + bytes([T_BOTTOM]) * (w - 2) + bytes([T_BR])
    for row, col, text, inv in lines:
        if not 0 < row < h - 1 or not 0 <= col < w - 1:
            continue
        for i, ch in enumerate(text[:w - 1 - col]):
            buf[row * w + col + i] = ch | (0x80 if inv else 0)
    if title == b"NOTEPAD" and h - 2 < 16:
        top = next((col for row, col, _, _ in lines if row == -1), 0)
        thumb = 2 + top * max(0, h - 5) // (16 - (h - 2))
        for row in range(1, h - 1):
            buf[row * w + w - 1] = (ord('^') if row == 1 else ord('v') if row == h - 2
                                     else 0xA0 if row == thumb else ord('|'))
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
                (5, "key_down 6 0x02"), (hold, "key_up 6 0x02")]

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
    # and one down with the mouse, button held through CTRL
    drag = [(10, "plug joyporta mouse"), (10, "exec xdotool mousemove 300 200"),
            (5, f"debug write memory {syms['PtrX']} 50; debug write memory {syms['PtrY']} 35; "
                f"debug write memory {syms['EvLastX']} 50; debug write memory {syms['EvLastY']} 35"),
            (5, "key_down 6 0x02"), (5, "mouse_move 32 16"), (10, "key_up 6 0x02"), (10, "")]
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
    used = [b for b in heap_walk(r, syms["HeapBase"], r.peek("HeapEnd", 2)) if b[2]]
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


# ---- notepad and clock
NOTE_W, NOTE_H, CLK_W, CLK_H = 16, 3 + 6, 8, 3


def note_win(x, y, rows, cx, cy, top=0):
    lines = [(1 + i, 1, (rows[top + i] if top + i < len(rows) else b"").ljust(14), False) for i in range(7)]
    # Nonpainting row carries the viewport origin for the frame oracle.
    lines.append((-1, top, b"", False))
    lines.append((1 + cy - top, 1 + cx, bytes([rows[cy][cx] if cx < len(rows[cy]) else 0x20]), True))
    return (x, y, NOTE_W, NOTE_H, b"NOTEPAD", lines)


def clock_win(x, y, h, m, field=0):
    text = f" {h:02d}:{m:02d}".encode()
    lines = [(1, 1, text, False), (1, 2 + field * 3, text[1 + field * 3:3 + field * 3], True)]
    return (x, y, CLK_W, CLK_H, b"CLOCK", lines)


def clock_model(frames, hz50):
    """The ROM's second counting in Python: base frames a second plus a
    hundredths accumulator, from noon."""
    base, add = (50, 16) if hz50 else (59, 92)
    need, frac, count, secs = base, 0, 0, 0
    for _ in range(frames):
        count += 1
        if count == need:
            count = 0
            frac += add
            need = base + 1 if frac >= 100 else base
            frac %= 100
            secs += 1
    return secs


def app_checks(syms, fails, vram0):
    print("notepad and clock:")
    nt0 = vram0[NT:NT + COLS * ROWS]

    def press_at(x, y, hold=5):
        return [(5, f"debug write memory {syms['PtrX']} {x}; debug write memory {syms['PtrY']} {y}; "
                    f"debug write memory {syms['EvLastX']} {x}; debug write memory {syms['EvLastY']} {y}"),
                (5, "key_down 6 0x02"), (hold, "key_up 6 0x02")]

    def tap(row, mask, shift=False):
        down = f"key_down {row} {mask:#x}" + ("; key_down 6 0x01" if shift else "")
        up = f"key_up {row} {mask:#x}" + ("; key_up 6 0x01" if shift else "")
        return [(3, down), (3, up)]

    file_new = press_at(88, 3) + press_at(96, 11)       # FILE, NEW on row 1
    file_open = press_at(88, 3) + press_at(96, 19)      # FILE, OPEN on row 2
    file_save = press_at(88, 3) + press_at(96, 27)      # FILE, SAVE on row 3
    view_clock = press_at(140, 3) + press_at(148, 11)   # VIEW, CLOCK on row 1
    H, I, X, ENTER, BS = (3, 0x20), (3, 0x40), (5, 0x20), (7, 0x40), (7, 0x20)

    # type HI, ENTER, X, then backspace over the X
    r = Run(syms, file_new + tap(*H) + tap(*I) + tap(*ENTER) + tap(*X) + tap(*BS) + [(10, "")], "note-type")
    buf = r.bytes("NoteBuf", 256)
    rows = [buf[i * 16:i * 16 + 14] for i in range(16)]
    want = compose(nt0, [note_win(8, 6, rows, 0, 1)])
    check(fails, "note-type", rows[0] == b"HI".ljust(14) and rows[1] == b" " * 14
          and (r.peek("NoteCX"), r.peek("NoteCY")) == (0, 1) and r.nt() == want,
          f"rows {rows[0]!r} {rows[1]!r}, cursor ({r.peek('NoteCX')}, {r.peek('NoteCY')}), "
          f"crc32 {zlib.crc32(r.nt()):08x}, expected {zlib.crc32(want):08x}")

    # SPACE must type into the focused note even with the pointer over its body.
    park = (f"debug write memory {syms['PtrX']} 100; debug write memory {syms['PtrY']} 70; "
            f"debug write memory {syms['EvLastX']} 100; debug write memory {syms['EvLastY']} 70")
    r = Run(syms, file_new + [(5, park)] + tap(*H) + tap(8, 0x01) + tap(*I) + [(10, "")], "note-space")
    rows = [b"H I"] + [b""] * 15
    want = compose(nt0, [note_win(8, 6, rows, 3, 0)])
    check(fails, "note-space", r.bytes("NoteBuf", 14) == b"H I".ljust(14)
          and (r.peek("NoteCX"), r.peek("NoteCY")) == (3, 0) and r.nt() == want,
          f"row {r.bytes('NoteBuf', 14)!r}, cursor {r.peek('NoteCX')}, "
          f"crc32 {zlib.crc32(r.nt()):08x}, expected {zlib.crc32(want):08x}")

    # Holding SPACE repeats text without grabbing or closing the window.
    r = Run(syms, file_new + [(5, park)] + tap(*H)
            + [(3, "key_down 8 0x01"), (30, "key_up 8 0x01")]
            + tap(*I) + [(10, "")], "note-space-repeat")
    want = compose(nt0, [note_win(8, 6, [b"H     I"] + [b""] * 15, 7, 0)])
    check(fails, "note-space-repeat", r.bytes("NoteBuf", 14) == b"H     I".ljust(14)
          and r.peek("NoteCX") == 7 and r.counts()[1:3] == (2, 2) and r.nt() == want,
          f"row {r.bytes('NoteBuf', 14)!r}, events {r.counts()}, "
          f"crc32 {zlib.crc32(r.nt()):08x}, expected {zlib.crc32(want):08x}")

    # save, type more, open: the file comes back over the document
    r = Run(syms, file_new + tap(*H) + tap(8, 0x01) + tap(*I) + file_save + tap(*X) + tap(*X) + file_open + [(10, "")], "note-file")
    buf = r.bytes("NoteBuf", 256)
    rows = [buf[i * 16:i * 16 + 14] for i in range(16)]
    ramdir = r.bytes("RamDir", 16)
    want = compose(nt0, [note_win(8, 6, rows, 0, 0)])
    if EXT:
        files = read_disk_image(DISK)
        stored = "NOTE" in files and len(files["NOTE"]) == 256 and files["NOTE"][:16] == buf[:16]
        where = f"disk file NOTE {len(files.get('NOTE', b''))} bytes, {list(files)}"
    else:
        stored = ramdir[:5] == b"NOTE\0" and int.from_bytes(ramdir[13:15], "little") == 256 and ramdir[15] == 1
        where = f"RAM file {ramdir[:5]!r} {int.from_bytes(ramdir[13:15], 'little')} bytes"
    check(fails, "note-file", rows[0] == b"H I".ljust(14) and r.peek("NoteResult") == 0 and stored and r.nt() == want,
          f"row 0 {rows[0]!r} after save/type/open, result {r.peek('NoteResult')}, {where}")

    # the clock: opened, hours bumped, then run for 3100 frames
    r = Run(syms, view_clock + tap(8, 0x20, shift=True) + [(3100, "")], "clock")
    hz50 = bool(r.bios[0x2B] & 0x80)
    hms = (r.peek("ClkH"), r.peek("ClkM"), r.peek("ClkS"))
    want = compose(nt0, [clock_win(18, 3, hms[0], hms[1])])
    check(fails, "clock-face", hms[0] == 13 and r.nt() == want,
          f"{hms[0]:02d}:{hms[1]:02d}:{hms[2]:02d} on a {'50' if hz50 else '60'} Hz machine, "
          f"crc32 {zlib.crc32(r.nt()):08x}, expected {zlib.crc32(want):08x}")
    # drift of the arithmetic against the true rate, an hour of it
    true_hz = 3579545 / (313 * 228) if hz50 else 3579545 / (262 * 228)
    frames_hour = round(true_hz * 3600)
    model_secs = clock_model(frames_hour, hz50)
    check(fails, "clock-rate", abs(model_secs - 3600) <= 1,
          f"an hour of {'PAL' if hz50 else 'NTSC'} interrupts ({frames_hour}) counts {model_secs} s")
    # and the ROM agrees with the model over the run: seconds since the set
    since_set = r.peek("IrqCnt", 2) - r.peek("ClkSetAt", 2)
    check(fails, "clock-count", hms[1] * 60 + hms[2] == clock_model(since_set, hz50),
          f"{since_set} interrupts since the set, {hms[1] * 60 + hms[2]} s counted, model {clock_model(since_set, hz50)}")


def commander_checks(syms, fails, vram0):
    print("commander:")
    nt0 = vram0[NT:NT + COLS * ROWS]
    def press(x, y):
        return [(5, f"debug write memory {syms['PtrX']} {x}; debug write memory {syms['PtrY']} {y}; "
                    f"debug write memory {syms['EvLastX']} {x}; debug write memory {syms['EvLastY']} {y}"),
                (5, "key_down 6 0x02"), (5, "key_up 6 0x02")]
    def tap(row, mask, shift=False):
        return [(5, f"key_down {row} {mask}" + ("; key_down 6 1" if shift else "")),
                (3, f"key_up {row} {mask}" + ("; key_up 6 1" if shift else ""))]
    open_cmd = press(140, 3) + press(148, 51)
    enter, down, tab = tap(7, 64), tap(8, 64, True), tap(7, 8)
    copy, delete, yes, no = tap(3, 1), tap(3, 2), tap(5, 64), tap(4, 8)
    def doc(name):
        return name.encode().ljust(14) + b"\0\0" + (b" " * 14 + b"\0\0") * 15
    fixtures = {name: doc(name) for name in ("ALPHA", "BETA", "GAMMA", "DELTA")}
    def seed_ram(files):
        directory, data = bytearray(64), bytearray(1024)
        for i, (name, content) in enumerate(files.items()):
            assert i < 4 and len(content) <= 256
            directory[i * 16:i * 16 + len(name)] = name.encode()
            directory[i * 16 + 13:i * 16 + 15] = len(content).to_bytes(2, "little")
            directory[i * 16 + 15] = 1
            data[i * 256:i * 256 + len(content)] = content
        return [(5, f"debug write_block memory {syms['RamDir']} [binary format H* {directory.hex()}]; "
                    f"debug write_block memory {syms['RamHeap']} [binary format H* {data.hex()}]")]
    def run(steps, name, files=fixtures, ram=None):
        if ram is None:
            ram = {} if EXT else files
        return Run(syms, seed_ram(ram) + open_cmd + steps + [(15, "")], name,
                   files=files if EXT else None)
    def cmdwin(left, right, active=0, selection=0, status=b"READY", top=0, front=True):
        lines = [(1, 1, b"DISK" if EXT else b"RAM", active == 0),
                 (1, 16, b"RAM", active == 1), (8, 1, status, False),
                 (9, 1, b"TAB PANE SHIFT+ARROWS R LIST", False),
                 (10, 1, b"ENTER OPEN C COPY D DELETE", False)]
        for pane, names in enumerate((left, right)):
            start = top if pane == active else 0
            names = names[start:start + 6]
            for i, name in enumerate(names or ["(EMPTY)"]):
                lines.append((2 + i, 1 if pane == 0 else 16, name.encode(),
                              pane == active and i + start == selection))
        return (1, 3, 30, 12, b"COMMANDER", lines)
    def screen(r, name, windows):
        want = compose(nt0, windows)
        check(fails, name, r.nt() == want,
              f"crc32 {zlib.crc32(r.nt()):08x}, expected {zlib.crc32(want):08x}")
    def backend(r):
        return r.peek("StBackend") == (5 if EXT else 1)
    left = list(fixtures)
    right = [] if EXT else left
    r = run([], "cmd-list")
    check(fails, "cmd-open", r.peek("WndCount") == 1 and backend(r),
          f"windows {r.peek('WndCount')}, backend {r.peek('StBackend')}")
    screen(r, "cmd-list", [cmdwin(left, right)])
    r = run([], "cmd-empty", files={})
    screen(r, "cmd-empty", [cmdwin([], [])])
    r = run(down, "cmd-select")
    screen(r, "cmd-select", [cmdwin(left, right, selection=1)])
    r = run(tab, "cmd-pane")
    screen(r, "cmd-pane", [cmdwin(left, right, active=1)])
    r = run(delete, "cmd-confirm")
    screen(r, "cmd-confirm", [cmdwin(left, right, status=b"DELETE SELECTED FILE? Y/N")])
    r = run(delete + no, "cmd-cancel")
    screen(r, "cmd-cancel", [cmdwin(left, right)])
    r = run(delete + yes, "cmd-delete")
    remaining = left[1:]
    screen(r, "cmd-delete", [cmdwin(remaining, [] if EXT else remaining, status=b"DELETED")])
    if EXT:
        check(fails, "cmd-delete-file", read_disk_image(DISK) == {k: v for k, v in fixtures.items() if k != "ALPHA"},
              "ALPHA removed; all other disk payloads unchanged")
    r = run(down + enter, "cmd-open-note")
    check(fails, "cmd-open-note", r.peek("WndCount") == 2 and r.bytes("NoteBuf", 256) == fixtures["BETA"]
          and r.bytes("NoteName", 5) == b"BETA\0" and backend(r),
          f"windows {r.peek('WndCount')}, name {r.bytes('NoteName', 5)!r}, backend {r.peek('StBackend')}")
    rows = [b"BETA"] + [b""] * 15
    screen(r, "cmd-note-screen", [cmdwin(left, right, selection=1), note_win(10, 7, rows, 0, 0)])
    r = run(down + open_cmd + press(20, 35), "cmd-instance")
    # The second window closes; the first keeps its own selection/cache.
    check(fails, "cmd-instance", r.peek("WndCount") == 1 and r.peek("CmdSel0") == 1,
          f"windows {r.peek('WndCount')}, restored selection {r.peek('CmdSel0')}")
    r = run(open_cmd + delete + yes + press(20, 35) + delete + yes, "cmd-stale-list")
    if EXT:
        intact = read_disk_image(DISK) == {k: v for k, v in fixtures.items() if k != "ALPHA"}
    else:
        intact = r.bytes("RamDir", 64)[15::16] == bytes([0, 1, 1, 1])
    check(fails, "cmd-stale-list", r.peek("CmdStatus") == 6 and intact,
          f"status {r.peek('CmdStatus')}; stale ALPHA must not delete BETA")
    r = run(open_cmd * 3 + enter, "cmd-window-full")
    check(fails, "cmd-window-full", r.peek("WndCount") == 4 and r.peek("CmdStatus") == 7 and backend(r),
          f"windows {r.peek('WndCount')}, status {r.peek('CmdStatus')}, no existing window replaced")
    r = run(press(12, 27), "cmd-close")
    used = [b for b in heap_walk(r, syms["HeapBase"], r.peek("HeapEnd", 2)) if b[2]]
    check(fails, "cmd-close-heap", r.peek("WndCount") == 0 and not used,
          f"windows {r.peek('WndCount')}, allocated blocks {len(used)}")
    if not EXT:
        r = run(copy, "cmd-same-device")
        screen(r, "cmd-same-device", [cmdwin(left, right, status=b"SAME DEVICE")])
        return
    r = run(copy, "cmd-copy")
    screen(r, "cmd-copy", [cmdwin(left, ["ALPHA"], status=b"COPIED")])
    check(fails, "cmd-copy-bytes", r.bytes("RamHeap", 256) == fixtures["ALPHA"] and backend(r)
          and read_disk_image(DISK) == fixtures, "256 bytes copied to RAM, source disk intact, backend restored")
    other = doc("KEEP ME")
    r = run(copy, "cmd-no-overwrite", ram={"ALPHA": other})
    check(fails, "cmd-no-overwrite", r.peek("CmdStatus") == 3 and r.bytes("RamHeap", 256) == other,
          f"status {r.peek('CmdStatus')}, existing RAM file preserved")
    r = run(copy, "cmd-full", ram={f"RAM{i}": other for i in range(4)})
    check(fails, "cmd-full", r.peek("CmdStatus") == 5 and r.bytes("RamHeap", 1024) == other * 4,
          f"status {r.peek('CmdStatus')}, full RAM store unchanged")
    # A document opened from RAM retains that backend when saved from its new window.
    save = press(88, 3) + press(96, 27)
    r = run(copy + tab + enter + tap(5, 32) + save, "cmd-note-backend")
    check(fails, "cmd-note-backend", r.peek("NoteBackend") == 1 and backend(r)
          and r.bytes("RamHeap", 6) == b"XALPHA" and read_disk_image(DISK) == fixtures,
          "RAM note edited and saved to RAM; disk source and global backend unchanged")
    r = run(tab + copy, "cmd-copy-to-disk", files={}, ram={"ALPHA": fixtures["ALPHA"]})
    check(fails, "cmd-copy-to-disk", read_disk_image(DISK) == {"ALPHA": fixtures["ALPHA"]} and backend(r),
          "256 bytes copied from RAM to FAT12 disk")
    # Free directory entries, no free clusters: creation succeeds, writing fails.
    full_disk = {"FILLER": b"F" * (((1440 - DATA_SECTOR) // 2) * 1024)}
    r = run(tab + copy + copy, "cmd-disk-full", files=full_disk, ram={"ALPHA": fixtures["ALPHA"]})
    check(fails, "cmd-disk-full", r.peek("CmdStatus") == 5 and read_disk_image(DISK) == full_disk
          and r.bytes("RamHeap", 256) == fixtures["ALPHA"],
          "failed copy cleaned up; retry reports storage error, not target exists; RAM source intact")
    # Fault injection at BDOS CLOSE for the newly created target only. Return
    # the real BDOS error convention (A=FF, carry clear), not StClose's API.
    scratch = syms["RamHeap"] + 768
    fault = (f"debug write_block memory {scratch} [binary format H* 3effb7c9]; "
             f"debug set_bp 0xF37D {{[reg C] == 16 && [debug read memory {syms['DskMode']}] == 14}} "
             f"{{reg PC {scratch}}}")
    r = run([(5, fault)] + tab + copy, "cmd-close-error", files={}, ram={"ALPHA": fixtures["ALPHA"]})
    check(fails, "cmd-close-error", r.peek("CmdStatus") == 5 and read_disk_image(DISK) == {}
          and r.bytes("RamHeap", 256) == fixtures["ALPHA"],
          "BDOS close failure reported; failed target removed and RAM source intact")
    many = {f"FILE{i}": doc(f"FILE{i}") for i in range(8)}
    r = run(down * 6, "cmd-scroll", files=many)
    screen(r, "cmd-scroll", [cmdwin(list(many), [], selection=6, top=1)])
    for size in (257, 65536):
        content = b"Z" * size
        r = run(copy, f"cmd-large-{size}", files={"BIG": content})
        check(fails, f"cmd-large-{size}", r.peek("CmdStatus") == 4 and r.bytes("RamDir", 64) == bytes(64)
              and read_disk_image(DISK) == {"BIG": content}, "oversize copy refused without modifying either device")
    r = run([], "cmd-extensions", files={"ALPHA.TXT": other, "ALPHA": fixtures["ALPHA"]})
    screen(r, "cmd-extensions", [cmdwin(["ALPHA.TXT", "ALPHA"], [])])
    r = run(down + delete + yes, "cmd-delete-extension-safe", files={"ALPHA.TXT": other, "ALPHA": fixtures["ALPHA"]})
    check(fails, "cmd-extension-safe", read_disk_image(DISK) == {"ALPHA.TXT": other},
          "deleting ALPHA preserves ALPHA.TXT byte for byte")
    names83 = {"NOTES.TXT": doc("TEXT"), "NOTES.BAK": doc("BACKUP"), "ABCDEFGH.XYZ": doc("MAX NAME")}
    r = run(copy + tab + enter + tap(5, 32) + save, "cmd-83-roundtrip", files=names83)
    check(fails, "cmd-83-roundtrip", r.bytes("NoteName", 10) == b"NOTES.TXT\0"
          and r.bytes("RamHeap", 5) == b"XTEXT" and read_disk_image(DISK) == names83,
          "NOTES.TXT copied, opened and saved to RAM; NOTES.BAK and source unchanged")
    r = run(down * 2 + copy, "cmd-83-max", files=names83)
    check(fails, "cmd-83-max", r.bytes("RamDir", 13) == b"ABCDEFGH.XYZ\0"
          and r.bytes("RamHeap", 256) == names83["ABCDEFGH.XYZ"], "full 8.3 name and bytes preserved")
    r = run(enter, "cmd-invalid-note", files={"SHORT": b"short"})
    check(fails, "cmd-invalid-note", r.peek("WndCount") == 1 and r.peek("CmdStatus") == 10,
          "non-document refused without opening a notepad")



def settings_checks(syms, fails):
    backend = 5 if EXT else 1
    defaults = bytes((0x4D, 1, 1, 0, backend))
    def press(x, y):
        return [(5, f"debug write memory {syms['PtrX']} {x}; debug write memory {syms['PtrY']} {y}"),
                (5, "key_down 6 0x02"), (5, "key_up 6 0x02")]
    r = Run(syms, press(20, 4) + press(30, 20) + [(10, "")], "settings-menu")
    check(fails, "settings-menu", r.peek("WndCount") == 1 and r.peek("WinApp", 2) == syms["AppSettings"],
          "MSX DESK > SETTINGS opens the settings window")
    buf = r.peek("WinBufP", 2) - WORK
    rows = [r.ram[buf + i * 20:buf + (i + 1) * 20] for i in range(5)]
    check(fails, "settings-values", rows[1][1:13] == b"POINTER RAMP" and rows[1][16] == ord('1')
          and rows[2][1:15] == b"MOUSE Y INVERT" and rows[2][16] == ord('0')
          and rows[3][14:14 + (4 if EXT else 3)] == (b"DISK" if EXT else b"RAM"),
          f"window rows {rows[1:4]}")
    check(fails, "settings-missing", r.bytes("SetRec", 5) == defaults, "missing file gives defaults")
    # 0: the pointer follows the hand, host up is screen up; 1: upside down
    for inv, want in ((0, (120, 75)), (1, (120, 105))):
        reset = (f"debug write memory {syms['PtrX']} 120; debug write memory {syms['PtrY']} 90; "
                 f"debug write memory {syms['SetInvertY']} {inv}")
        r = Run(syms, [(10, "plug joyporta mouse"), (10, "exec xdotool mousemove 300 200"),
                       (10, reset), (10, "mouse_move 0 -30"), (10, "")], f"settings-mouse-{inv}")
        check(fails, f"settings-mouse-{inv}", r.ptr() == want,
              f"Y invert {inv}, host up 30: pointer {r.ptr()}, expected {want}")
    if EXT:
        for name, data, expected in (
                ("valid", bytes((0x4D, 1, 0, 0, 1)), bytes((0x4D, 1, 0, 0, 1))),
                ("magic", bytes((0, 1, 0, 0, 1)), defaults),
                ("version", bytes((0x4D, 99, 0, 0, 1)), defaults),
                ("short", b"M\1", defaults),
                ("long", defaults + b"x", defaults),
                ("fields", bytes((0x4D, 1, 255, 255, 255)), defaults)):
            r = Run(syms, [(10, "")], "settings-boot-" + name, files={"SETTINGS": data})
            check(fails, "settings-boot-" + name, r.bytes("SetRec", 5) == expected
                  and r.peek("StBackend") == expected[4]
                  and r.peek("AccelPtr", 2) == syms["AccelTabs"] + 5 * expected[2],
                  "record " + r.bytes("SetRec", 5).hex())


def arrange_checks(syms, fails):
    """Menu-driven arrangements against arithmetic and compositor oracles."""
    print("arrange:")

    def press(x, y):
        return [(5, f"debug write memory {syms['PtrX']} {x}; debug write memory {syms['PtrY']} {y}; "
                    f"debug write memory {syms['EvLastX']} {x}; debug write memory {syms['EvLastY']} {y}"),
                (5, "key_down 6 0x02"), (5, "key_up 6 0x02")]

    cascade = press(140, 3) + press(148, 27)
    tile = press(140, 3) + press(148, 35)
    opens = [press(140, 3) + press(148, 19),
             press(92, 3) + press(100, 11) +
             [(5, "key_down 2 0x40"), (5, "key_up 2 0x40")],  # A in the notepad
             press(8, 3) + press(16, 11),
             press(140, 3) + press(148, 19) +
             [(5, "key_down 6 0x01; key_down 8 0x80"),
              (5, "key_up 8 0x80; key_up 6 0x01")]]  # second calendar selects 2
    windows = [cal_win(4, 4), note_win(10, 7, [b"A"] + [b""] * 15, 1, 0),
               about_win(12, 14), cal_win(9, 7, sel=2)]

    def tiled(n):
        # Front-to-back cells, computed independently of the ROM table.
        height, half = 22, 11
        if n == 0:
            return []
        if n == 1:
            return [(0, 1, 32, height)]
        if n == 2:
            return [(x, 1, 16, height) for x in (0, 16)]
        if n == 3:
            return [(0, 1, 16, height), (16, 1, 16, half), (16, 1 + half, 16, height - half)]
        return [(x, y, 16, half) for x in (0, 16) for y in (1, 1 + half)]

    def verify(steps, name, geometry, n):
        r = Run(syms, steps + [(15, "")], name)
        actual = [tuple(r.ram[syms['WndTab'] - WORK + i * 15:syms['WndTab'] - WORK + i * 15 + 4])
                  for i in range(n)]
        want = compose(expected_nt(), [tuple(g) + windows[i][4:] for i, g in enumerate(geometry)])
        check(fails, name, actual == geometry and r.peek('WndCount') == n
              and r.bytes('WndZ', n) == bytes(reversed(range(n))) and r.nt() == want,
              f"geometry {actual}, crc32 {zlib.crc32(r.nt()):08x}, expected {zlib.crc32(want):08x}")
        for i in range(n):
            base = syms['WndTab'] + i * 15
            old = tuple(r.ram[base + 12 - WORK:base + 15 - WORK])
            check(fails, name + f"-record-{i}", old == (*geometry[i][:2], 0), f"old x/y/moved {old}")
        return r

    for n in range(5):
        steps = sum(opens[:n], [])
        geometry = [(min(1 + i * 2, 32 - w[2]), min(2 + i, 23 - w[3]), w[2], w[3])
                    for i, w in enumerate(windows[:n])]
        before = verify(steps + cascade, f"arrange-cascade-{n}", geometry, n)
        if n == 3:
            # Force the allocator's real failure return after the windows
            # exist. TILE must keep their buffers, state and geometry.
            refuse = [(5, f"debug set_bp {syms['HeapAlloc']} {{}} {{reg PC {syms['HpaNone']}}}")]
            r = verify(steps + cascade + refuse + tile, "arrange-no-room", geometry, n)
            check(fails, "arrange-no-room-heap",
                  r.bytes('WndTab', n * 15) == before.bytes('WndTab', n * 15)
                  and heap_walk(r, syms['HeapBase'], r.peek('HeapEnd', 2))
                  == heap_walk(before, syms['HeapBase'], before.peek('HeapEnd', 2)),
                  "records, buffer pointers and heap blocks unchanged")
        geometry = list(reversed(tiled(n)))
        steps += cascade + tile
        verify(steps, f"arrange-tile-{n}", geometry, n)
        if n:
            # Reallocation and edge clamping also work on already tiled sizes.
            clamped = [(min(1 + i * 2, 32 - g[2]), min(2 + i, 23 - g[3]), g[2], g[3])
                       for i, g in enumerate(geometry)]
            verify(steps + cascade, f"arrange-clamp-{n}", clamped, n)
            verify(steps + cascade + tile, f"arrange-repeat-{n}", geometry, n)
        for i, (x, y, _, _) in reversed(list(enumerate(geometry))):
            steps += press(x * 8 + 2, y * 8 + 2)
            if i == 1:  # the modified notepad now asks before closing
                steps += press(60, 98)  # DISCARD
        r = Run(syms, steps + [(15, "")], f"arrange-close-{n}")
        blocks = heap_walk(r, syms['HeapBase'], r.peek('HeapEnd', 2))
        want_heap = [(syms['HeapBase'], r.peek('HeapEnd', 2) - syms['HeapBase'] - 4, 0, 0)]
        check(fails, f"arrange-close-{n}", r.peek('WndCount') == 0 and blocks == want_heap
              and r.nt() == expected_nt(), f"heap {blocks}, crc32 {zlib.crc32(r.nt()):08x}")

    # A tall notepad scrolled to its last document row must blank the
    # extra space, without treating the following state bytes as text.
    last_row = [(5, f"debug write memory {syms['NoteCY']} 15; "
                    f"debug write memory {syms['NoteTop']} 15; "
                    f"debug write memory {syms['NoteBuf'] + 15 * 16} 90")]
    r = Run(syms, opens[1] + last_row + tile + [(15, "")], "arrange-note-bottom")
    want = compose(expected_nt(), [(0, 1, 32, 22, b"NOTEPAD",
                                   [(1, 1, b"Z", False), (1, 2, b" ", True)])])
    check(fails, "arrange-note-bottom", r.nt() == want and r.peek('NoteTop') == 15,
          f"top {r.peek('NoteTop')}, crc32 {zlib.crc32(r.nt()):08x}, expected {zlib.crc32(want):08x}")

    # Commander has a second pane starting beyond a tiled window's right
    # edge and a help row on its bottom border: neither may write there.
    cmd = press(140, 3) + press(148, 51)
    steps = opens[0] + cmd + opens[2] + opens[3] + tile
    lines = [(1, 1, b"DISK" if EXT else b"RAM", True), (1, 16, b"RAM", False),
             (2, 1, b"(EMPTY)", True), (2, 16, b"(EMPTY)", False),
             (8, 1, b"READY", False), (9, 1, b"TAB PANE SHIFT+ARROWS R LIST", False),
             (10, 1, b"ENTER OPEN C COPY D DELETE", False)]
    arranged = [windows[0], (0, 0, 30, 12, b"COMMANDER", lines), windows[2], windows[3]]
    want = compose(expected_nt(), [g + w[4:] for g, w in zip(reversed(tiled(4)), arranged)])
    r = Run(syms, steps + [(15, "")], "arrange-commander", files={})
    check(fails, "arrange-commander", r.nt() == want,
          f"crc32 {zlib.crc32(r.nt()):08x}, expected {zlib.crc32(want):08x}")
    for x, y, _, _ in tiled(4):
        steps += press(x * 8 + 2, y * 8 + 2)
    r = Run(syms, steps + [(15, "")], "arrange-commander-close", files={})
    blocks = heap_walk(r, syms['HeapBase'], r.peek('HeapEnd', 2))
    check(fails, "arrange-commander-close", r.peek('WndCount') == 0 and r.nt() == expected_nt()
          and blocks == [(syms['HeapBase'], r.peek('HeapEnd', 2) - syms['HeapBase'] - 4, 0, 0)],
          f"heap {blocks}")


def dialog_checks(syms, fails):
    print("dialogues:")
    def press(x, y):
        return [(5, f"debug write memory {syms['PtrX']} {x}; debug write memory {syms['PtrY']} {y}; "
                    f"debug write memory {syms['EvLastX']} {x}; debug write memory {syms['EvLastY']} {y}"),
                (5, "key_down 6 0x02"), (5, "key_up 6 0x02")]
    def tap(row, mask, shift=False):
        return [(3, f"key_down {row} {mask}" + ("; key_down 6 1" if shift else "")),
                (3, f"key_up {row} {mask}" + ("; key_up 6 1" if shift else ""))]
    new = press(88, 3) + press(96, 11)
    typed = new + tap(3, 32)
    close = press(66, 50)
    enter, tab, esc = tap(7, 64), tap(7, 8), tap(7, 4)
    base = compose(expected_nt(), [note_win(8, 6, [b"H"] + [b""] * 15, 1, 0)])
    def panel(focus=0, alert=False):
        nt = bytearray(base)
        for y in range(8, 15):
            nt[y*32+6:y*32+26] = bytes([160 if y == 11+focus else 32])*20
        lines = [(9, b"COULD NOT SAVE" if alert else b"NOTE"), (10, b"" if alert else b"UNSAVED WORK")]
        lines += [(11+i, t) for i, t in enumerate([b"OK"] if alert else [b"CANCEL", b"DISCARD", b"SAVE"])]
        for y, text in lines:
            nt[y*32+7:y*32+7+len(text)] = bytes(c | (128 if y == 11+focus else 0) for c in text)
        return bytes(nt)
    def run(steps, name, want, count=1, opened=0, **kwargs):
        r = Run(syms, steps + [(15, "")], name, **kwargs)
        check(fails, name, r.nt() == want and r.peek('WndCount') == count
              and r.peek('DgOpenFlag') == opened,
              f"windows {r.peek('WndCount')}, dialog {r.peek('DgOpenFlag')}, "
              f"crc32 {zlib.crc32(r.nt()):08x}, expected {zlib.crc32(want):08x}")
        return r
    run(new + close, 'dialog-clean-close', expected_nt(), 0)
    r = run(typed + close, 'dialog-open', panel(), opened=1)
    document = r.bytes('NoteBuf', 256)
    r = run(typed + close + press(88, 3) + press(30, 160) + tap(5, 32),
            'dialog-modal', panel(), opened=1)
    check(fails, 'dialog-modal-state', r.bytes('NoteBuf', 256) == document and r.peek('MenuOpen') == 0,
          'outside presses and typing leave document and menus unchanged')
    for suffix, answer in [('cancel', press(60, 90)), ('enter', enter), ('escape', esc)]:
        r = run(typed + close + answer, 'dialog-' + suffix, base)
        check(fails, 'dialog-' + suffix + '-state', r.bytes('NoteBuf', 256) == document
              and r.peek('NoteModified') == 1, '256 document bytes and modified flag retained')
    run(typed + close + tab, 'dialog-focus', panel(1), opened=1)
    run(typed + close + tap(8, 32, True), 'dialog-focus-wrap', panel(2), opened=1)
    for suffix, answer in [('mouse', press(60, 98)), ('key', tab + enter)]:
        r = run(typed + close + answer, 'dialog-discard-' + suffix, expected_nt(), 0, files={})
        check(fails, 'dialog-discard-' + suffix + '-heap',
              heap_walk(r, syms['HeapBase'], r.peek('HeapEnd', 2)) ==
              [(syms['HeapBase'], r.peek('HeapEnd', 2)-syms['HeapBase']-4, 0, 0)]
              and (read_disk_image(DISK) == {} if EXT else r.bytes('RamDir', 64)[15::16] == bytes(4)),
              'all window allocations recovered; no file written')
    for suffix, answer in [('mouse', press(60, 106)), ('key', tab + tab + enter)]:
        r = run(typed + close + answer, 'dialog-save-' + suffix, expected_nt(), 0, files={})
        stored = (read_disk_image(DISK).get('NOTE') == document if EXT else
                  r.bytes('RamDir', 16) == b'NOTE' + bytes(9) + bytes([0, 1, 1])
                  and r.bytes('RamHeap', 256) == document)
        check(fails, 'dialog-save-' + suffix + '-file', stored, 'NOTE contains all 256 document bytes')
    # Inject each storage API failure with its carry convention. WRITE
    # retains BC=256 deliberately: carry must not be hidden by a full count.
    for vector in ('StOpen', 'StWrite', 'StClose'):
        scratch = syms['RamHeap'] + 768
        fault = [(5, f"debug write_block memory {scratch} [binary format H* 37c9]; "
                     f"debug set_bp {syms[vector]} {{}} {{reg PC {scratch}}}")]
        steps = typed + fault + close + tab + tab + enter
        r = run(steps, 'dialog-error-' + vector, panel(alert=True), opened=1)
        check(fails, 'dialog-error-' + vector + '-state', r.bytes('NoteBuf', 256) == document
              and r.peek('NoteModified') == 1, 'failed save preserves modified document')
        run(steps + enter, 'dialog-alert-ok-' + vector, base)
    file_save = press(88, 3) + press(96, 27)
    run(typed + fault + file_save, 'dialog-menu-error', panel(alert=True), opened=1)


def read_tape_wav(path):
    """Decode BIOS 1200-baud FSK independently of the emulator: a zero
    is two long half-waves, a one four short ones; 8N2, LSB first.
    Silence separates blocks and the leader is a run of ones.
    """
    import wave
    with wave.open(path) as w:
        assert (w.getnchannels(), w.getsampwidth()) == (1, 1)
        rate = w.getframerate()
        samples = w.readframes(w.getnframes())
    edges = [i for i in range(1, len(samples))
             if (samples[i] >= 128) != (samples[i - 1] >= 128)]
    runs = [b - a for a, b in zip(edges, edges[1:])]
    split, silence = rate / 3200, rate / 1500
    blocks, block, i = [], bytearray(), 0
    while i < len(runs):
        if runs[i] > silence:
            if block:
                blocks.append(bytes(block))
                block.clear()
            i += 1
            continue
        if runs[i] < split:              # leader or trailing stop bits
            i += 1
            continue
        bits = []
        for _ in range(11):
            assert i < len(runs), "truncated cassette byte"
            bit = int(runs[i] < split)
            n = 4 if bit else 2
            assert len(runs[i:i+n]) == n and all(
                (v < split) == bool(bit) and v < silence for v in runs[i:i+n]), "bad FSK pulse"
            bits.append(bit)
            i += n
        assert bits[0] == 0 and bits[9:] == [1, 1], "bad cassette framing"
        block.append(sum(bits[b + 1] << b for b in range(8)))
    if block:
        blocks.append(bytes(block))
    return blocks


def tape_checks(syms, fails):
    # C-BIOS has no cassette. Still verify the opt-in boot selection there;
    # boot must not try to read SETTINGS from tape.
    normal = Run(syms, [(10, "")], "tape-default")
    check(fails, "tape-default", normal.peek("StBackend") == (5 if EXT else 1),
          "without opt-in: disk when BDOS is present, otherwise RAM")
    rom = os.path.join(BUILD, "msxtape.rom")
    ts = assemble(rom, os.path.join(BUILD, "msxtape.sym"), ["TAPE=1"])
    r = Run(ts, [(10, "")], "tape-select", rom=rom)
    expected = ts["ST_DISK"] if EXT else ts["ST_TAPE"]
    check(fails, "tape-select", r.peek("StBackend") == expected,
          f"backend {r.peek('StBackend')}, expected {expected}")
    if MACHINE.startswith("C-BIOS") or EXT:
        return

    def press(x, y):
        return [(5, f"debug write memory {ts['PtrX']} {x}; debug write memory {ts['PtrY']} {y}; "
                    f"debug write memory {ts['EvLastX']} {x}; debug write memory {ts['EvLastY']} {y}"),
                (5, "key_down 6 0x02"), (3, "key_up 6 0x02")]

    def tap(row, mask):
        return [(3, f"key_down {row} {mask}"), (3, f"key_up {row} {mask}")]

    new = press(88, 3) + press(96, 11)
    save = press(88, 3) + press(96, 27)
    reopen = press(88, 3) + press(96, 19)
    record = [(5, 'cassetteplayer new $::out/note.wav')]
    snapshot = [(5, f'set f [open $::out/document.bin wb]; puts -nonewline $f '
                 f'[debug read_block memory {ts["NoteBuf"]} 256]; close $f')]
    rewind = [(5, 'cassetteplayer rewind; cassetteplayer play')]
    r = Run(ts, record + new + tap(3, 32) + tap(8, 1) + tap(3, 64)
            + snapshot + save + tap(5, 32) + tap(5, 32) + rewind + reopen
            + [(10, 'cassetteplayer eject')], "note-tape", rom=rom)
    document = open(os.path.join(OUT, "note-tape", "document.bin"), "rb").read()
    check(fails, "note-tape", r.bytes("NoteBuf", 256) == document
          and r.peek("NoteResult") == 0 and r.peek("NoteModified") == 0,
          f"256 bytes crc32 {zlib.crc32(r.bytes('NoteBuf', 256)):08x}, "
          f"expected {zlib.crc32(document):08x}; Dropped {r.peek('Dropped', 2)}")
    blocks = read_tape_wav(os.path.join(OUT, "note-tape", "note.wav"))
    check(fails, "tape-wav", blocks == [b"MSXT" + b"NOTE".ljust(13, b"\0")
                                       + b"\0\1", document],
          f"Python decoded blocks {[len(b) for b in blocks]}, document crc32 {zlib.crc32(document):08x}")

    # Carry from byte output and final close must reach the existing
    # save-error dialog; edits must remain marked as unsaved.
    for entry in ("TAPOUT", "TAPOOF"):
        scratch = ts["CmdBuf"] + 250
        fault = [(5, f"debug write memory {scratch} 55; debug write memory {scratch+1} 201; "
                     f"debug set_bp {ts[entry]} {{}} {{reg PC {scratch}}}")]
        rr = Run(ts, record + new + tap(3, 32) + fault + save + [(10, "cassetteplayer eject")],
                 "tape-error-" + entry.lower(), rom=rom)
        check(fails, "tape-error-" + entry.lower(), rr.peek("NoteModified") == 1
              and rr.peek("NoteResult") != 0 and rr.peek("TapeMode") == 0,
              "BIOS carry propagated, document remains unsaved")

    # Real BIOS reads synthetic CAS headers: reject foreign format, a
    # different filename and oversized payload before accepting a payload.
    marker = bytes.fromhex("1fa6debacc137d74")
    header = blocks[0]
    for name, bad in (("magic", b"BAD!" + header[4:]),
                      ("name", header[:4] + b"OTHER".ljust(13, b"\0") + header[17:]),
                      ("length", header[:17] + b"\1\1")):
        path = os.path.join(BUILD, "tape-" + name + ".cas")
        with open(path, "wb") as f:
            f.write(marker + bad)
        rr = Run(ts, new + [(5, f"cassetteplayer insert {path}")]
                 + reopen + [(10, "")], "tape-bad-" + name, rom=rom)
        check(fails, "tape-bad-" + name, rr.peek("NoteResult") != 0
              and rr.peek("TapeLen", 2) == 0 and rr.peek("TapeMode") == 0,
              "bad header rejected, no payload accepted")


def resize_scroll_checks(syms, fails):
    def point(x, y):
        return [(5, f"debug write memory {syms['PtrX']} {x}; debug write memory {syms['PtrY']} {y}; "
                    f"debug write memory {syms['EvLastX']} {x}; debug write memory {syms['EvLastY']} {y}")]

    def press(x, y):
        return point(x, y) + [(5, "key_down 6 0x02"), (5, "key_up 6 0x02")]

    new = press(92, 3) + press(100, 11)
    # Call the real HeapStat at an idle loop boundary, using test scratch.
    scratch = syms['CmdBuf']
    probe = bytes([0xCD]) + syms['HeapStat'].to_bytes(2, 'little')
    probe += bytes([0xC3]) + syms['MainLoop'].to_bytes(2, 'little')
    heapstat = [(5, "; ".join(f"debug write memory {scratch+i} {v}" for i, v in enumerate(probe))
                 + f"; set ::heapbp [debug set_bp {syms['MainLoop']} {{}} "
                   f"{{debug remove_bp $::heapbp; reg PC {scratch}}}]"), (10, "")]
    # Real mouse motion while CTRL supplies the held left button.
    drag = [(10, "plug joyporta mouse"), (10, "exec xdotool mousemove 300 200")]
    drag += point(23 * 8 + 2, 14 * 8 + 2)
    drag += [(5, "key_down 6 0x02"), (5, "mouse_move 32 16"),
             (10, "key_up 6 0x02"), (10, "")]
    cases = [("resize-grow", drag, 18, 10)]
    minimum = point(25 * 8 + 2, 15 * 8 + 2) + [(5, "key_down 6 0x02")]
    minimum += point(0, 8) + [(5, "key_up 6 0x02"), (10, "")]
    cases.append(("resize-minimum", drag + minimum, 6, 4))
    maximum = point(25 * 8 + 2, 15 * 8 + 2) + [(5, "key_down 6 0x02")]
    maximum += point(255, 191) + [(5, "key_up 6 0x02"), (10, "")]
    cases.append(("resize-screen-edge", drag + maximum, 24, 17))
    edge_minimum = point(255, 183) + [(5, "key_down 6 0x02")]
    edge_minimum += point(0, 8) + [(5, "key_up 6 0x02"), (10, "")]
    cases.append(("resize-from-edge", drag + maximum + edge_minimum, 6, 4))
    refuse = [(5, f"set ::failbp [debug set_bp {syms['HeapAlloc']} {{}} "
                   f"{{debug remove_bp $::failbp; reg PC {syms['HpaNone']}}}]")]
    cases.append(("resize-no-room", refuse + drag, 16, 9))
    for name, steps, w, h in cases:
        r = Run(syms, new + steps + heapstat, name)
        win = note_win(8, 6, [b""] * 16, 0, 0)
        lines = [(i + 1, 1, b" " * 14, False) for i in range(h - 2)] + [(1, 1, b" ", True)]
        want = compose(expected_nt(), [(8, 6, w, h, win[4], lines)])
        blocks = heap_walk(r, syms['HeapBase'], r.peek('HeapEnd', 2))
        used = [b for b in blocks if b[2]]
        free = [b[1] for b in blocks if not b[2]]
        check(fails, name, r.peek('WinW') == w and r.peek('WinH') == h
              and r.peek('LastHit') == 8 and r.nt() == want
              and sorted(b[1] for b in used) == sorted([w * h, syms['NOTESTSZ']])
              and all(b[3] == 0x10 for b in used) and r.peek('Dropped', 2) == 0
              and r.peek('HpTotal', 2) == sum(free) and r.peek('HpBiggest', 2) == max(free),
              f"size {r.peek('WinW')}x{r.peek('WinH')}, heap {[b[1] for b in used]}, "
              f"HeapStat={r.peek('HpTotal', 2)}/{r.peek('HpBiggest', 2)}, "
              f"crc32 {zlib.crc32(r.nt()):08x}/{zlib.crc32(want):08x}, Dropped={r.peek('Dropped', 2)}")

    r = Run(syms, new + drag + minimum + press(8 * 8 + 2, 6 * 8 + 2) + heapstat,
            "resize-close")
    blocks = heap_walk(r, syms['HeapBase'], r.peek('HeapEnd', 2))
    check(fails, "resize-close", r.peek('WndCount') == 0 and r.nt() == expected_nt()
          and blocks == [(syms['HeapBase'], r.peek('HeapEnd', 2) - syms['HeapBase'] - 4, 0, 0)]
          and r.peek('HpTotal', 2) == blocks[0][1] and r.peek('HpBiggest', 2) == blocks[0][1],
          f"heap {blocks}, crc32 {zlib.crc32(r.nt()):08x}")

    for name, cx, cy, top in [("resize-caret-right", 13, 0, 0),
                               ("resize-caret-below", 0, 15, 9)]:
        caret = [(5, f"debug write memory {syms['NoteCX']} {cx}; "
                     f"debug write memory {syms['NoteCY']} {cy}; "
                     f"debug write memory {syms['NoteTop']} {top}")]
        r = Run(syms, new + caret + drag + minimum + heapstat, name)
        want = compose(expected_nt(), [(8, 6, 6, 4, b"NOTEPAD", [(-1, top, b"", False)])])
        check(fails, name, r.nt() == want and r.peek('NoteCX') == cx and r.peek('NoteCY') == cy
              and r.bytes('NoteBuf', 256) == (b" " * 14 + b"\0\0") * 16
              and r.peek('HpTotal', 2) == r.peek('HeapEnd', 2) - syms['HeapBase'] - 24 - syms['NOTESTSZ'] - 16,
              f"caret ({r.peek('NoteCX')}, {r.peek('NoteCY')}), crc32 {zlib.crc32(r.nt()):08x}")

    rows = [f"{i:02d}".encode() for i in range(16)]
    seed = [(5, "; ".join(f"debug write memory {syms['NoteBuf'] + i * 16 + j} {v}"
                         for i, row in enumerate(rows) for j, v in enumerate(row)))]
    down = press(23 * 8 + 2, 13 * 8 + 2)
    page = press(23 * 8 + 2, 10 * 8 + 2)
    up = press(23 * 8 + 2, 7 * 8 + 2)
    key = [(3, "key_down 6 0x01; key_down 8 0x40"),
           (3, "key_up 8 0x40; key_up 6 0x01")]
    for name, steps, top, cy in [("scroll-down", down, 1, 0),
                                  ("scroll-up", down + up, 0, 0),
                                  ("scroll-clamp", down * 12, 9, 0),
                                  ("scroll-page", page, 7, 0),
                                  ("scroll-page-up", page * 2, 0, 0),
                                  ("scroll-key", key * 7, 1, 7)]:
        r = Run(syms, new + seed + steps + [(10, "")], name)
        want = compose(expected_nt(), [note_win(8, 6, rows, 0, cy, top)])
        check(fails, name, r.peek('NoteTop') == top and r.peek('NoteCY') == cy and r.nt() == want,
              f"top {r.peek('NoteTop')}, caret {r.peek('NoteCY')}, "
              f"crc32 {zlib.crc32(r.nt()):08x}/{zlib.crc32(want):08x}")


def main():
    syms, tsyms = build()
    ensure_display()
    fails = []
    if sys.argv[1:] == ["--resize-scroll"]:
        resize_scroll_checks(syms, fails)
        return bool(fails)
    if sys.argv[1:] == ["--tape"]:
        tape_checks(syms, fails)
        return bool(fails)
    if sys.argv[1:] == ["--arrange-only"]:
        arrange_checks(syms, fails)
        return bool(fails)
    if sys.argv[1:] == ["--dialogs"]:
        dialog_checks(syms, fails)
        return bool(fails)
    if sys.argv[1:] == ["--settings"]:
        test_build_checks(tsyms, fails)
        settings_checks(syms, fails)
        return bool(fails)
    if sys.argv[1:] == ["--arrange"]:
        arrange_checks(syms, fails)
        vram0 = boot_checks(syms, fails)
        window_checks(syms, fails, vram0)
        app_checks(syms, fails, vram0)
        return bool(fails)
    resize_scroll_checks(syms, fails)
    tape_checks(syms, fails)
    dialog_checks(syms, fails)
    arrange_checks(syms, fails)
    settings_checks(syms, fails)
    vram0 = boot_checks(syms, fails)
    mouse_checks(syms, fails, vram0)
    key_checks(syms, fails)
    menu_checks(syms, fails, vram0)
    window_checks(syms, fails, vram0)
    app_checks(syms, fails, vram0)
    commander_checks(syms, fails, vram0)
    test_build_checks(tsyms, fails)
    if fails:
        print("FAILED:", ", ".join(fails))
        return 1
    print("all subjects pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
