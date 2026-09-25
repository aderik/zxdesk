; AY envelope decay, channel A. No frame work or delay loop.
SND_CLICK       equ     0
SND_BEEP        equ     1
SndTable:       defb    60,3,111,24

; A = sound id. Preserve caller registers and interrupt state.
; The mouse leaves the address latch at R14. Never write R14/R15,
; and retain the mixer's I/O direction bits (MSX port B is output).
SndPlay:
                push    af
                push    bc
                push    de
                push    hl
                ld      e,a
                ld      a,(SetSound)
                or      a
                jr      z,SndDone
                ld      a,(TapeBusy)
                or      a
                jr      nz,SndDone
                ld      a,e
                and     1
                add     a,a
                ld      e,a
                ld      d,0
                ld      hl,SndTable
                add     hl,de
                ld      a,i
                push    af
                di
                ld      e,(hl)
                xor     a
                call    SndReg
                ld      e,0
                ld      a,1
                call    SndReg
                ld      a,7
                out     (PSGADR),a
                in      a,(PSGRD)
                and     $C0
                or      $3E
                ld      e,a
                ld      a,7
                call    SndReg
                ld      e,$10
                ld      a,8
                call    SndReg
                inc     hl
                ld      e,(hl)
                ld      a,11
                call    SndReg
                ld      e,0
                ld      a,12
                call    SndReg
                ld      a,13
                call    SndReg
                ld      a,14
                out     (PSGADR),a
                pop     af
                jp      po,SndDone
                ei
SndDone:
                pop     hl
                pop     de
                pop     bc
                pop     af
SndReturn:      ret

; A = register, E = value.
SndReg:
                out     (PSGADR),a
                ld      a,e
                out     (PSGWR),a
                ret
