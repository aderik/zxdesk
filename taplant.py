#!/usr/bin/env python3
"""Append a Spectrum header and data block to a .tap file.

Used to plant a file on the tape *after* the program, so the tape
backend's load path can be tested end to end: Fuse leaves the tape
positioned where the program's own load finished, so LD_BYTES reads
what follows.

    ./taplant.py build/bench3.tap SETTINGS 01,02,03
"""
import sys

def block(flag, payload):
    body = bytes([flag]) + payload
    chk = 0
    for b in body:
        chk ^= b
    body += bytes([chk])
    return len(body).to_bytes(2, "little") + body

def header(name, length, p1=0, p2=32768):
    n = name.upper().ljust(10)[:10].encode("ascii")
    return block(0x00, bytes([3]) + n
                 + length.to_bytes(2, "little")
                 + p1.to_bytes(2, "little")
                 + p2.to_bytes(2, "little"))

def main():
    tap, name, data = sys.argv[1], sys.argv[2], sys.argv[3]
    payload = bytes(int(x, 16) for x in data.split(","))
    with open(tap, "ab") as f:
        f.write(header(name, len(payload)))
        f.write(block(0xFF, payload))
    print(f"appended {name} with {len(payload)} bytes to {tap}")

main()
