; ============================================================
;  The application model
;  Ported from the ZX version: an application is a descriptor, a
;  constant table of where its state lives, how big that state
;  is, and the routines a window needs from whatever owns it.
;  State is swapped into a live copy rather than indirected
;  through, one LDIR each way.
;
;  The record layout follows the ZX version; rendering uses cells.
; ============================================================
APP_STSZ        equ     0       ; word: bytes of per instance state
APP_LIVE        equ     2       ; word: where the live copy of it sits
APP_INIT        equ     4       ; word: called once, on a fresh instance
APP_DRAW        equ     6       ; word: paint the interior
APP_KEY         equ     8       ; word: A = the key; returns A = 1 if it repainted
APP_TITLE       equ     10      ; word
APP_X           equ     12      ; where the first instance opens, in cells
APP_Y           equ     13
APP_W           equ     14
APP_H           equ     15
APP_CLOSE       equ     16      ; word: carry set means "not yet"
APP_SCROLL      equ     18      ; word: the scroll model, null for no bar
APP_SCROLLTO    equ     20      ; word: A = the unit to make first
; A byte of flags, new here. APPF_FULL says the draw vector writes
; every interior cell, so WinDraw need not blank them first: the
; calendar's blank was measured at 1.6 ms of a 16 ms window open.
APP_FLAGS       equ     22
APPF_FULL       equ     $01
APPSIZE         equ     23

APP_CAL         equ     0
APP_ABOUT       equ     1
APP_NOTE        equ     2
APP_CLOCK       equ     3
APP_CMD         equ     4
APP_SETTINGS    equ     5

AppTable:
AppCal:         defw    CALSTSZ, CalState, CalInit, CalDraw
                defw    CalKey, TxtCalTitle
                defb    4, 4, CALW, CALH
                defw    0
                defw    0, 0
                defb    APPF_FULL
                ; No state, no key vector: it drops what is typed at it.
AppAbout:       defw    0, 0, 0, WdAbout
                defw    0, TxtAboutTitle
                defb    8, 12, ABOUTW, ABOUTH
                defw    0
                defw    0, 0
                defb    0
                ; The notepad: a document per window, painted whole.
AppNote:        defw    NOTESTSZ, NoteState, NoteInit, NoteDraw
                defw    NoteKey, TxtWinTitle
                defb    8, 6, NOTEW, NOTEH
                defw    NoteClose
                defw    NoteScrollInfo, NoteScrollTo
                defb    APPF_FULL
                ; The clock keeps nothing per window: the time is the
                ; machine's, so two clock windows are two views of it.
AppClock:       defw    0, 0, 0, ClkDraw
                defw    ClkKey, TxtClockTitle
                defb    18, 3, CLKW, CLKH
                defw    0
                defw    0, 0
                defb    0

AppCommander:   defw    CMDSTSZ, CmdState, CmdInit, CmdDraw
                defw    CmdKey, TxtCmdTitle
                defb    1, 3, CMDW, CMDH
                defw    0, 0, 0
                defb    0

AppSettings:    defw    1, SetRow, SetInit, SetDraw
                defw    SetKey, SetTitle
                defb    6, 6, 24, 7
                defw    0, 0, 0
                defb    0

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
                xor     a                       ; a null vector changed nothing
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
