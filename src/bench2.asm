WINADDR         equ     $4808
WINROWS         equ     64
WINBYTES        equ     16
                org     40000

ScrDown:        inc     h
                ld      a,h
                and     7
                ret     nz
                ld      a,l
                add     a,32
                ld      l,a
                ret     c
                ld      a,h
                sub     8
                ld      h,a
                ret

; Stack fill, but aware that the eight pixel rows inside a character
; cell differ only in H. Seven rows out of eight cost one INC H
; instead of a full row step.
FillPushFast:
                di
                ld      (SaveSP),sp
                ld      de,0
                ld      hl,WINADDR+WINBYTES
                ld      c,WINROWS/8
FpfCell:
                ld      b,8
FpfRow:
                ld      sp,hl
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                inc     h
                djnz    FpfRow
                ld      a,l             ; one cell step, done once per eight
                add     a,32
                ld      l,a
                jr      c,FpfNext
                ld      a,h
                sub     8
                ld      h,a
FpfNext:
                dec     c
                jr      nz,FpfCell
                ld      sp,(SaveSP)
                ei
                ret

; Highlight in a monochrome design is a constant store, because we
; already know what every attribute byte holds.
AttrSetConst8:
                ld      hl,$5800
                ld      b,8
Asc8:           ld      (hl),$47
                inc     hl
                djnz    Asc8
                ret

AttrSetConst32:
                ld      hl,$5800
                ld      b,32
Asc32:          ld      (hl),$47
                inc     hl
                djnz    Asc32
                ret

; XOR outline with the same cell awareness on the two side edges
XorOutlineFast:
                ld      hl,WINADDR
                ld      b,WINBYTES
XofTop:         ld      a,(hl)
                xor     $FF
                ld      (hl),a
                inc     l
                djnz    XofTop
                ld      hl,WINADDR
                ld      c,WINROWS/8
XofCell:        ld      b,8
XofRow:         ld      a,(hl)
                xor     $80
                ld      (hl),a
                ld      a,l
                add     a,WINBYTES-1
                ld      l,a
                ld      a,(hl)
                xor     $01
                ld      (hl),a
                ld      a,l
                sub     WINBYTES-1
                ld      l,a
                inc     h
                djnz    XofRow
                ld      a,l
                add     a,32
                ld      l,a
                jr      c,XofNext
                ld      a,h
                sub     8
                ld      h,a
XofNext:        dec     c
                jr      nz,XofCell
                ret

SaveSP:         defw    0
                end
