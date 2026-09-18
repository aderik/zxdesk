
; Keyboard decode test. The matrix is written directly rather than
; pressed, so every combination can be checked including ones that
; are awkward to reach on a real keyboard.
;
; Each case is: row index, row bits, modifier bits, expected code.
; Result counts the passes; BKBDN of BKBDN is a pass.
BKbdTest:
                xor     a
                ld      (BKbdRes),a
                ld      hl,BKbdCases
                ld      b,BKBDN
BktLoop:
                push    bc
                push    hl
                ; clear the matrix, then set the one row under test
                ld      de,KbdMatrix
                ld      b,8
                xor     a
BktClr:
                ld      (de),a
                inc     de
                djnz    BktClr
                pop     hl
                push    hl
                ld      a,(hl)                  ; row index
                ld      e,a
                ld      d,0
                inc     hl
                ld      a,(hl)                  ; row bits
                push    hl
                ld      hl,KbdMatrix
                add     hl,de
                ld      (hl),a
                pop     hl
                inc     hl
                ld      a,(hl)                  ; modifiers
                ld      (KbdModState),a
                inc     hl
                ld      a,(hl)                  ; expected
                ld      (BKbdWant),a            ; not in C: KbdFirst uses C as
                call    KbdFirst                ; its running index and eats it
                cp      $FF
                jr      z,BktMiss
                call    KbdDecode
                ld      c,a
                ld      a,(BKbdWant)
                cp      c
                jr      nz,BktMiss
                ld      a,(BKbdRes)
                inc     a
                ld      (BKbdRes),a
BktMiss:
                pop     hl
                ld      de,4
                add     hl,de
                pop     bc
                djnz    BktLoop
                xor     a
                ld      (KbdModState),a
                ld      (KbdLast),a
                ret

; row, bits, mods, expected
BKbdCases:
                defb    0,$02,0,'Z'             ; bit 1 of the CAPS row
                defb    0,$10,0,'V'             ; bit 4, the far end of a row
                defb    3,$01,0,'1'
                defb    3,$10,0,'5'
                defb    7,$01,0,' '             ; SPACE
                defb    6,$01,0,KEY_ENTER
                defb    3,$10,1,KEY_LEFT        ; CAPS and 5
                defb    4,$04,1,KEY_RIGHT       ; CAPS and 8
                defb    4,$08,1,KEY_UP          ; CAPS and 7
                defb    4,$10,1,KEY_DOWN        ; CAPS and 6
                defb    4,$01,1,KEY_DELETE      ; CAPS and 0
                defb    7,$04,2,'.'             ; SYM and M is bit 2
                defb    7,$08,2,','             ; SYM and N is bit 3
                defb    5,$01,2,34              ; SYM and P is a quote
                defb    6,$08,2,'-'             ; SYM and J
                defb    4,$01,2,'_'             ; SYM and 0
                defb    0,$08,2,'?'             ; SYM and C
                defb    3,$01,2,'!'             ; SYM and 1
BKBDN           equ     18

BKbdRes:        defb    0
BKbdWant:       defb    0
