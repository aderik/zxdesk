; ============================================================
;  The application model
;  Ported from the ZX version: an application is a descriptor, a
;  constant table of where its state lives, how big that state
;  is, and the routines a window needs from whatever owns it.
;  State is swapped into a live copy rather than indirected
;  through, one LDIR each way.
;
;  Only the calendar is registered yet; the others arrive with
;  their windows. The record layout is the ZX one so the later
;  ports drop in.
; ============================================================
APP_STSZ        equ     0       ; word: bytes of per instance state
APP_LIVE        equ     2       ; word: where the live copy of it sits
APP_INIT        equ     4       ; word: called once, on a fresh instance
APP_DRAW        equ     6       ; word: paint the interior
APP_KEY         equ     8       ; word: A = the key. Zero drops it
APP_TITLE       equ     10      ; word
APP_X           equ     12      ; where the first instance opens, in cells
APP_Y           equ     13
APP_W           equ     14
APP_H           equ     15
APP_CLOSE       equ     16      ; word: carry set means "not yet"
APP_SCROLL      equ     18      ; word: the scroll model, null for no bar
APP_SCROLLTO    equ     20      ; word: A = the unit to make first
APPSIZE         equ     22

APP_CAL         equ     0

AppTable:
AppCal:         defw    CALSTSZ, CalState, CalInit, 0
                defw    CalKey, TxtCalTitle
                defb    4, 4, CALW, CALH
                defw    0
                defw    0, 0

; in:  A = an application index
; out: HL = its descriptor
AppAt:
                ld      hl,AppTable
                or      a
                ret     z
                ld      b,a
                ld      de,APPSIZE
AppAtLoop:
                add     hl,de
                djnz    AppAtLoop
                ret

; in:  C = one of the APP_ offsets, A = whatever that vector wants
; Calls it through the live window's descriptor, or returns having
; done nothing if the vector is null.
AppCall:
                push    af
                ld      hl,(WinApp)
                ld      a,h
                or      l
                jr      z,AcNone
                ld      e,c
                ld      d,0
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ld      a,d
                or      e
                jr      z,AcNone
                pop     af
                ex      de,hl
                jp      (hl)
AcNone:
                pop     af
                ret

; The state of the window going out, written back to its own block,
; and the state of the window coming in, read into the live copy.
AppSave:
                call    AppStateAt
                ret     z
                ldir                            ; live into the block
                ret

AppLoad:
                call    AppStateAt
                ret     z
                ex      de,hl                   ; and the block into live
                ldir
                ret

; out: HL = the live copy, DE = this window's block, BC = the size,
;      Z set if there is nothing to move either way
AppStateAt:
                ld      hl,(WinApp)
                ld      a,h
                or      l
                ret     z
                ld      c,(hl)
                inc     hl
                ld      b,(hl)
                inc     hl                      ; BC = the state size
                ld      a,b
                or      c
                ret     z
                ld      e,(hl)
                inc     hl
                ld      d,(hl)                  ; DE = the live copy
                push    de
                ld      de,(WinStateP)
                ld      a,d
                or      e                       ; Z if it never got a block
                pop     hl                      ; HL = the live copy
                ret
