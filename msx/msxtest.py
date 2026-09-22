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
OUT = os.path.join(BUILD, "out")
MACHINE = os.environ.get("MSX_MACHINE", "C-BIOS_MSX1_EU")

ROMBASE = 0x4000
WORK = 0xC000
RAMTOP = 0xF380         # the BIOS work area starts here; the stack sits below it
STACK = 0x0400          # what is left for the stack and the BIOS's own use
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


def build():
    """Assemble, and refuse the build if the budgets are blown: a ROM past
    $C000 or RAM into the stack does not fail loudly at run time."""
    os.makedirs(BUILD, exist_ok=True)
    r = subprocess.run(["pasmo", "-I", os.path.join(ROOT, "src"), "--bin", SRC, ROM, SYM],
                       capture_output=True, text=True)
    for line in r.stderr.splitlines():
        if not line.startswith("WARNING: Var") and "3 pass" not in line:
            print(line)
    if r.returncode != 0:
        sys.exit("assembly failed")
    syms = load_symbols(SYM)
    size = os.path.getsize(ROM)
    if size != 0x8000:
        sys.exit(f"ROM is {size} bytes, expected 32768")
    rom_end, ram_end = syms["RomEnd"], syms["RamEnd"]
    print(f"msxdesk.rom: code ${ROMBASE:04X}-${rom_end:04X}, {rom_end - ROMBASE} bytes, "
          f"{0xC000 - rom_end} free; RAM ${WORK:04X}-${ram_end:04X}, {ram_end - WORK} bytes, "
          f"{RAMTOP - STACK - ram_end} free before the stack")
    if ram_end > RAMTOP - STACK:
        sys.exit(f"BUILD FAILED: RAM ends at ${ram_end:04X}, past ${RAMTOP - STACK:04X}")
    return syms


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

    def __init__(self, syms, steps, name):
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
        cmd = ["openmsx", "-machine", MACHINE, "-cart", ROM, "-romtype", "page12",
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
    bar[1:1 + 28] = b"ZX DESK   FILE   VIEW   HELP"
    rows = [bytes(bar), bytes([0x81]) * COLS] + [bytes([0x80]) * COLS] * 21 + [b" " * COLS]
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
        check(fails, f"font-{third}", r.vram[base:base + len(font)] == font,
              f"glyphs 32-127 from BIOS ${cgtabl:04X}, crc32 {zlib.crc32(font):08x}")
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
                   (10, "mouse_move 20 0"), (10, "mouse_move 0 -30"), (10, "")], "mouse")
    check(fails, "ptr-moved", r.ptr() == (130, 75), f"pointer {r.ptr()}, expected (130, 75)")
    check(fails, "sprite-follows", r.sat(0)[:2] == (74, 130), f"sprite 0 {r.sat(0)}")
    c = r.counts()
    check(fails, "ev-ptrmove", c[0] == 2 and c[1:] == (0, 0, 0), f"events {c}, expected (2, 0, 0, 0)")
    check(fails, "nt-untouched", r.nt() == vram0[NT:NT + COLS * ROWS], "name table unchanged")


def key_checks(syms, fails):
    print("keys:")
    # RIGHT (row 8 bit 7) held 20 frames, then DOWN (bit 6) 10 frames.
    r = Run(syms, [(5, "key_down 8 0x80"), (20, "key_up 8 0x80"),
                   (5, "key_down 8 0x40"), (10, "key_up 8 0x40"), (5, "")], "cursor")
    want = (120 + ramp(20), 90 + ramp(10))
    check(fails, "cursor-ramp", r.ptr() == want, f"pointer {r.ptr()}, expected {want}")
    c = r.counts()
    check(fails, "cursor-events", c[3] == 0 and c[0] == 30, f"events {c}: 30 moves, no keys")

    # A (row 2 bit 6) tapped for 3 frames, SHIFT+1 (row 6 bit 0, row 0 bit 1)
    # for 3 frames, SPACE (row 8 bit 0) as the button for 5 frames.
    r = Run(syms, [(5, "key_down 2 0x40"), (3, "key_up 2 0x40"),
                   (5, "key_down 6 0x01; key_down 0 0x02"), (3, "key_up 0 0x02; key_up 6 0x01"),
                   (5, "key_down 8 0x01"), (5, "key_up 8 0x01"), (5, "")], "keys")
    c = r.counts()
    check(fails, "key-events", c[3] == 3 and c[1] == 1 and c[2] == 1,
          f"events {c}: 3 keys, one press, one release")
    status = r.nt()[STATROW * COLS:STATROW * COLS + 4]
    check(fails, "status-echo", status == b"A! " + b" ", f"status row {status!r}")
    check(fails, "last-key", r.peek("LastKey") == ord(" "), f"last key {r.peek('LastKey')}")

    # Held for 30 frames: one press, one repeat at 20, then every 3.
    r = Run(syms, [(5, "key_down 3 0x01"), (30, "key_up 3 0x01"), (5, "")], "repeat")
    c = r.counts()
    check(fails, "key-repeat", c[3] == 1 + 1 + (30 - 20) // 3, f"events {c}: expected 5 keys")
    check(fails, "repeat-echo", r.nt()[STATROW * COLS:STATROW * COLS + 5] == b"CCCCC",
          f"status row {r.nt()[STATROW * COLS:STATROW * COLS + 6]!r}")


STATROW = 23


def main():
    syms = build()
    ensure_display()
    fails = []
    vram0 = boot_checks(syms, fails)
    mouse_checks(syms, fails, vram0)
    key_checks(syms, fails)
    if fails:
        print("FAILED:", ", ".join(fails))
        return 1
    print("all subjects pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
