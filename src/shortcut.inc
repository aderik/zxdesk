; ============================================================
;  Keyboard shortcuts
;  The ZX took both shifts, EXTEND MODE, because a Spectrum has
;  no other modifier. An MSX has one SHIFT bit for both keys, and
;  CTRL is the pointer's button, so the modifier here is GRAPH:
;  the MSX's own key for a key's other meaning, and one nothing
;  else in the desk reads. The structure is the ZX's: GRAPH and a
;  letter decode to a code from $81 up in kbd.inc, above anything
;  text can be, and HdlKey takes those before any application is
;  offered a key. An open menu or a dialogue swallows them.
;
;  Only what the port can already do has a letter.
;  PRINT and the desktop shortcuts panel are not here yet, so
;  neither are R and D. O is new: the ZX's F was its file
;  manager, which here is the commander, and the MSX has an OPEN.
;
;  A shortcut does not repeat when held, so holding GRAPH and N
;  opens one notepad rather than filling the window table.
; ============================================================
SC_BASE         equ     $80
SCCOUNT         equ     13

; The letters, in code order: the Nth letter is SC_BASE+N.
ScLetters:      defb    "XCTWNOSKLFGIV"

; ------------------------------------------------------------
;  ScDispatch
;  in: A = a code from SC_BASE+1 upwards
;  The front window is selected first, as the ZX did, so the
;  shortcut acts on the window a person is looking at.
; ------------------------------------------------------------
ScDispatch:
                sub     SC_BASE+1
                cp      SCCOUNT
                ret     nc
                push    af
                ld      a,(WndCount)
                or      a
                ld      a,(WndZ)
                call    nz,WndSelect
                pop     af
                add     a,a
                ld      e,a
                ld      d,0
                ld      hl,ScTable
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ex      de,hl
                jp      (hl)

ScTable:
                defw    WndNext                 ; X
                defw    WndCascade              ; C
                defw    WndTile                 ; T
                defw    WndClose                ; W, and it still asks about unsaved work
                defw    NoteMenuNew             ; N
                defw    NoteMenuOpen            ; O
                defw    NoteMenuSave            ; S
                defw    ClockOpen               ; K
                defw    WndOpenCal              ; L
                defw    CommanderOpen           ; F
                defw    SetOpen                 ; G
                defw    AboutOpen               ; I
                defw    NoteMenuSaveAs          ; V

; The window at the back comes to the front, so pressing it again and
; again walks the whole z order rather than swapping two for ever.
WndNext:
                ld      a,(WndCount)
                cp      2
                ret     c
                dec     a
                ld      e,a
                ld      d,0
                ld      hl,WndZ
                add     hl,de
                ld      a,(hl)
                jp      WndFocusTo

; ------------------------------------------------------------
;  HELP, KEYS: the list, as a window. No state and no key vector:
;  there is nothing to type at a list.
; ------------------------------------------------------------
KEYSW           equ     15
KEYSH           equ     20

KeysOpen:
                ld      a,APP_KEYS
                jp      WndOpen

KeysDraw:
                xor     a
                ld      (PrintInv),a
                ld      hl,KeysText
                ld      b,1
KdLoop:
                ld      a,(hl)
                or      a
                ret     z                       ; an empty string ends it
                push    bc
                push    hl
                ld      c,1
                call    WinPrint
                pop     hl
KdSkip:
                ld      a,(hl)
                inc     hl
                or      a
                jr      nz,KdSkip
                pop     bc
                inc     b
                jr      KdLoop

TxtKeysTitle:   defb    "KEYS",0
KeysText:
                defb    "GRAPH +",0
                defb    "N NEW NOTE",0
                defb    "O OPEN",0
                defb    "S SAVE",0
                defb    "V SAVE AS",0
                defb    "W CLOSE",0
                defb    "X NEXT WINDOW",0
                defb    "C CASCADE",0
                defb    "T TILE",0
                defb    "K CLOCK",0
                defb    "L CALENDAR",0
                defb    "F COMMANDER",0
                defb    "G SETTINGS",0
                defb    "I ABOUT",0
                defb    "SELECT MARK",0
                defb    "CALENDAR:",0
                defb    ", . MONTH",0
                defb    "< > YEAR",0
                defb    0
