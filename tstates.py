#!/usr/bin/env python3
"""Turn raw bench3 counters into T states.

    cost = (K-K0)*(FRAME-HRET) - LOOP*(C-C0) - delay_cost

FRAME is the 48K interrupt period, HRET the exact cost of the returning
interrupt path in bench3.asm, LOOP the cost of one turn of the counting
loop. K0 and C0 come from the CAL row, which cancels every fixed
overhead in the harness.

If a measurement straddles the interrupt while the subject has
interrupts disabled, the interrupt is lost rather than deferred: the
Spectrum holds INT low for only 32 T. A lost interrupt still costs a
whole frame of wall time but no handler, so it is scored with the
missed=True flag and FRAME rather than FRAME-HRET.
"""
FRAME = 69888
HRET = 118
LOOP = 16

def delay_cost(n):
    return 19 if n == 0 else 26 * n + 18

def cost(k, c, n, k0, c0, missed=False):
    frame_term = k * (FRAME if missed else FRAME - HRET)
    return frame_term - LOOP * (c - c0) - (delay_cost(n) - delay_cost(0))

def report(rows):
    k0, c0 = rows[0][2], rows[0][3]
    print(f"{'subject':<10}{'start T':>9}{'K':>3}{'count':>7}{'T states':>10}  note")
    for name, n, k, c, *rest in rows:
        missed = bool(rest and rest[0])
        t = cost(k, c, n, k0, c0, missed)
        note = "interrupt lost in a DI window" if missed else ""
        print(f"{name:<10}{delay_cost(n)-19:>9}{k:>3}{c:>7}{t:>10}  {note}")
