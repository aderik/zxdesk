; ============================================================
;  Keyboard shortcuts
;  A Spectrum has no control key and no alt key. It has two
;  shifts, and holding both of them is EXTEND MODE, which is
;  where the machine's own ROM puts a third meaning for a key.
;  That is the modifier, because it is the only one that exists
;  and because it is the one a Sinclair owner already knows.
;
;  The requirement was that this must not interfere with normal
;  typing, and the way it does not is structural rather than
;  careful. EXTEND MODE has its own decode table in kbd.inc, so
;  a shortcut is not a letter with a flag on it, it is a
;  different code entirely: $81 upwards, above everything the
;  text tables can produce. HdlKey takes those codes before it
;  selects a window, so no application is ever offered one, and
;  no application can produce one either.
;
;  It also fixed something rather than only adding. EXTEND MODE
;  was already gating the pointer keys, but by reading the ports
;  in ReadInput while KbdDecode still handed the key to the
;  symbol table, so both shifts and O moved the pointer and typed
;  a semicolon at once. The pointer keys are nought in the extend
;  table, so they do one thing now.
;
;  A dialogue or an open menu swallows shortcuts, because HdlKey
;  returns before this on both. That is deliberate: a modal panel
;  that could be walked out from under by a shortcut is not
;  modal.
;
;  In the slow region. A shortcut is a key press, not a frame.
; ============================================================
SC_BASE         equ     $80

SC_NEXT         equ     $81     ; X, the window behind comes forward
SC_CASCADE      equ     $82     ; C
SC_TILE         equ     $83     ; T
SC_CLOSE        equ     $84     ; W, and it still asks about unsaved work
SC_NEW          equ     $85     ; N
SC_FILES        equ     $86     ; F
SC_SAVE         equ     $87     ; S
SC_SAVEAS       equ     $88     ; V
SC_CLOCK        equ     $89     ; K
SC_CALENDAR     equ     $8A     ; L
SC_DESKTOP      equ     $8B     ; D
SC_SETTINGS     equ     $8C     ; G
SC_ABOUT        equ     $8D     ; I
; R rather than P, because P is the pointer's right and taking it back
; would cost the keyboard pointer a direction to save a mnemonic.
SC_PRINT        equ     $8E     ; R
SCCOUNT         equ     14

; ------------------------------------------------------------
;  ScDispatch
;  in: A = a code from SC_BASE+1 upwards
;  The front window is selected first, so a shortcut acts on the
;  window a person is looking at rather than on whichever record
;  happened to be live. Closing is the one that would notice, and
;  it is the one that matters most.
; ------------------------------------------------------------
ScDispatch:
                ld      (ScCode),a
                ld      a,(WndZ)
                call    WndSelect
                ld      a,(ScCode)
                sub     SC_BASE+1
                cp      SCCOUNT
                ret     nc                      ; not one of ours after all
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
                defw    WndClose                ; W
                defw    NoteMenuNew             ; N
                defw    CmdOpenWin              ; F
                defw    NoteMenuSave            ; S
                defw    FmSavePanel             ; V
                defw    ClockOpen               ; K
                defw    WndOpenCal              ; L
                defw    DskSetup                ; D
                defw    DlgOpen                 ; G
                defw    AboutOpen               ; I
                defw    NotePrint               ; R

; ------------------------------------------------------------
;  WndNext
;  The window at the back comes to the front, so pressing it
;  again and again walks the whole z order and returns to where
;  it started. Bringing the second one forward instead would
;  swap two windows back and forth for ever and never reach the
;  third.
; ------------------------------------------------------------
WndNext:
                ld      a,(WndCount)
                cp      2
                ret     c                       ; one window is already in
                                                ; front of everything
                dec     a
                ld      e,a
                ld      d,0
                ld      hl,WndZ
                add     hl,de
                ld      a,(hl)
                jp      WndFocusTo

; ------------------------------------------------------------
;  The list, as a window
;  A shortcut nobody can find is half a feature, and HELP, KEYS
;  has been drawn on the menu bar since the first build without
;  doing anything. GEM printed the shortcut beside its menu item;
;  a ten column drop down has nowhere to put one, so the list
;  lives in a window instead.
;
;  A window rather than a panel for the same reason the commander
;  is one: fourteen lines is 112 pixel rows and SUMAXH is 96, so
;  a panel would have wanted a bigger arena and a window wants
;  nothing but heap it already has.
;
;  It keeps no state and has no key vector. There is nothing to
;  type at a list.
; ------------------------------------------------------------
KEYSW           equ     16
KEYSH           equ     WinCapH+2+15*8+4

KeysDraw:
                ld      a,(WinY)
                add     a,WinCapH+2
                ld      (KeyRow),a
                ld      hl,KeysText
KdLoop:
                ld      a,(hl)
                or      a
                ret     z                       ; an empty string ends it
                push    hl
                ld      a,(KeyRow)
                ld      (PxRow),a
                ld      a,(WinX)
                inc     a
                ld      c,a
                pop     hl
                push    hl
                xor     a
                ld      (PrintInv),a
                call    WinPrintClip
                pop     hl
KdSkip:
                ld      a,(hl)
                inc     hl
                or      a
                jr      nz,KdSkip
                ld      a,(KeyRow)
                add     a,8
                ld      (KeyRow),a
                jr      KdLoop

KeysOpen:
                ld      hl,AppKeys
                call    WndFindApp
                jp      nc,WndFocusTo
                ld      a,APP_KEYS
                jp      WndOpen

KeysText:
                defb    "BOTH SHIFTS +",0
                defb    "N NEW NOTE",0
                defb    "F FILES",0
                defb    "S SAVE",0
                defb    "V SAVE AS",0
                defb    "W CLOSE",0
                defb    "X NEXT WINDOW",0
                defb    "C CASCADE",0
                defb    "T TILE",0
                defb    "K CLOCK",0
                defb    "L CALENDAR",0
                defb    "D DESKTOP",0
                defb    "G SETTINGS",0
                defb    "I ABOUT",0
                defb    "R PRINT",0
                defb    0

KeyRow:         defb    0
ScCode:         defb    0
