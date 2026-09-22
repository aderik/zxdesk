#!/usr/bin/env python3
"""Assemble the MSX ROM, boot it headlessly in openMSX and assert on memory.

    ./msxtest.py            build, run every subject, assert
    ./msxtest.py --keep     also leave the dumps in build/out for a look

Checksums are the assertion; build/out/shot.png is for people.
The model is zxtest.py in the ZX tree, with openMSX standing in for the
Python Z80 because the subjects here are VRAM, the PPI and the PSG,
none of which a bare CPU model has.

Runs inside the msx/docker image (pasmo 0.5.5, openMSX, Xvfb,
xdotool on PATH); msx/test.sh wraps the docker invocation.
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

WORK = 0xC000


def build():
    os.makedirs(BUILD, exist_ok=True)
    subprocess.run(["pasmo", "--bin", SRC, ROM, SYM], check=True)
    size = os.path.getsize(ROM)
    if size != 0x8000:
        sys.exit(f"ROM is {size} bytes, expected 32768")
    print(f"msxdesk.rom: {size} bytes, crc32 {zlib.crc32(open(ROM, 'rb').read()):08x}")


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


def run(steps, name):
    """Boot the ROM, run the (frames, tcl) step list, return (vram, ram, log)."""
    out = os.path.join(OUT, name)
    os.makedirs(out, exist_ok=True)
    for f in ("vram.bin", "ram.bin", "log.txt", "shot.png"):
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
               SDL_AUDIODRIVER="dummy", HOME=os.environ.get("HOME", "/tmp"))
    cmd = ["openmsx", "-machine", MACHINE, "-cart", ROM, "-romtype", "page12",
           "-script", os.path.join(ROOT, "harness", "run.tcl")]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=180)
    log = open(os.path.join(out, "log.txt")).read() if os.path.exists(os.path.join(out, "log.txt")) else ""
    if r.returncode != 0 or "done" not in log:
        print(r.stdout, r.stderr, log)
        sys.exit(f"openMSX run '{name}' failed (rc {r.returncode})")
    vram = open(os.path.join(out, "vram.bin"), "rb").read()
    ram = open(os.path.join(out, "ram.bin"), "rb").read()
    assert len(vram) == 16384 and len(ram) == 16384
    return vram, ram, log


def expected_vram():
    """The pattern the ROM writes, computed here rather than copied from a dump."""
    pgt = bytes((n & 0xFF) ^ (n >> 8) for n in range(0x1800))
    nt = bytes(n & 0xFF for n in range(0x300))
    ct = bytes([0xF1]) * 0x1800
    return pgt, nt, ct


def vram_checks(vram):
    fails = []
    pgt, nt, ct = expected_vram()
    for label, base, want in (("PGT", 0x0000, pgt), ("NT", 0x1800, nt), ("CT", 0x2000, ct)):
        got = vram[base:base + len(want)]
        ok = got == want
        print(f"  {label:4s} @{base:04X} {len(want):5d} bytes crc32 {zlib.crc32(got):08x} "
              f"{'ok' if ok else 'MISMATCH, expected ' + format(zlib.crc32(want), '08x')}")
        if not ok:
            fails.append(f"vram-{label.lower()}")
    print(f"  whole VRAM crc32 {zlib.crc32(vram):08x}")
    return fails


def ram_checks(ram, syms):
    fails = []
    marker = ram[syms["Marker"] - WORK:syms["Marker"] - WORK + 6]
    print(f"  marker {marker!r}")
    if marker != b"ZXMSX\0":
        fails.append("ram-marker")
    frames = int.from_bytes(ram[syms["Frames"] - WORK:syms["Frames"] - WORK + 2], "little")
    print(f"  frames {frames}")
    if frames == 0:
        fails.append("ram-frames")
    return fails


def key_rows(ram, syms):
    base = syms["KeyRows"] - WORK
    return list(ram[base:base + 11])


def mouse(ram, syms):
    b = syms["MouseDX"] - WORK
    dx, dy = ram[b], ram[b + 1]
    reads = ram[syms["MouseReads"] - WORK]
    return (dx - 256 if dx > 127 else dx), (dy - 256 if dy > 127 else dy), reads


def main():
    build()
    syms = load_symbols(SYM)
    ensure_display()
    failures = []

    # Subject 1: boot, pattern, marker. Sixty frames after boot is well
    # past the fill.
    print("boot:")
    vram, ram, _ = run([(60, "")], "boot")
    failures += vram_checks(vram)
    failures += ram_checks(ram, syms)
    rows = key_rows(ram, syms)
    print(f"  key rows {' '.join(f'{r:02X}' for r in rows)}")
    if rows != [0xFF] * 11:
        failures.append("keys-idle")

    # Subject 2: a key held through the PPI. Row 0 bit 0 is "0",
    # row 8 bit 0 is SPACE.
    print("keys:")
    vram2, ram, _ = run([(30, "key_down 0 0x01"), (10, "key_down 8 0x01"), (10, "")], "keys")
    rows = key_rows(ram, syms)
    print(f"  key rows {' '.join(f'{r:02X}' for r in rows)}")
    if rows[0] != 0xFE or rows[8] != 0xFE:
        failures.append("keys-down")
    if vram2 != vram:
        failures.append("keys-vram-changed")

    # Subject 3: the mouse. Plug it, park the Xvfb pointer inside the
    # window, zero the ROM's counters, then move and read what the strobe
    # protocol delivered. Measured on openMSX 20.0: a host move of n
    # pixels arrives as -n/2, because openMSX halves host motion and an
    # MSX mouse reports the negative of the movement.
    print("mouse:")
    dx_addr, dy_addr = syms["MouseDX"], syms["MouseDY"]
    _, ram, log = run([(10, "plug joyporta mouse"),
                       (10, "exec xdotool mousemove 300 200"),
                       (10, f"debug write memory {dx_addr} 0; debug write memory {dy_addr} 0"),
                       (5, "mouse_move 100 0"), (5, "mouse_move 0 -60"),
                       (20, "")], "mouse")
    dx, dy, reads = mouse(ram, syms)
    print(f"  dx {dx} dy {dy} over {reads} reads, expected -50 30")
    if (dx, dy) != (-50, 30):
        failures.append("mouse")

    if failures:
        print("FAILED:", ", ".join(failures))
        return 1
    print("all subjects pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
