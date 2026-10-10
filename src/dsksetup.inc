; ============================================================
;  Desktop setup: VIEW > DESKTOP, GRAPH+D
;  Which shortcuts are on the desktop, as a row of toggles each.
;  Where they sit is not here: that is what dragging them is for,
;  and a panel with six pairs of coordinates in it would be a
;  worse way to say the same thing than picking the icon up.
;
;  The ZX drew this as a panel over a save-under; here it is a
;  window of the SETTINGS kind, with a byte of row focus per
;  window, the same keys and the same button column, so it moves,
;  stacks and closes like everything else. A toggle marks the
;  icon's rows on the desktop for repaint under the window; SAVE
;  writes the SETTINGS record, which carries the table.
; ============================================================
DSKSW           equ     19
DSKSH           equ     10
DSKSBTN         equ     12              ; the buttons' column within the window
DSKSROWS        equ     DSKMAX+2        ; the toggles, SAVE, DONE

DskSetOpen:
                ld      a,APP_DESK
                jp      WndOpenLater

DskSetInit:
                xor     a
                ld      (DskSetRow),a
                ret

; The SETTINGS keys: ESC closes, TAB and the arrows move the focus,
; ENTER, SPACE, LEFT and RIGHT act on the row.
DskSetKey:
                cp      KEY_ESC
                jr      z,DskSetDone
                cp      9
                jr      z,DskSetNext
                cp      KEY_DOWN
                jr      z,DskSetNext
                cp      KEY_UP
                jr      z,DskSetPrev
                cp      KEY_RIGHT
                jr      z,DskSetChange
                cp      KEY_LEFT
                jr      z,DskSetChange
                cp      KEY_ENTER
                jr      z,DskSetChange
                cp      ' '
                jr      z,DskSetChange
                xor     a
                ret
DskSetNext:
                ld      a,(DskSetRow)
                inc     a
                cp      DSKSROWS
                jr      c,DskSetFocus
                xor     a
                jr      DskSetFocus
DskSetPrev:
                ld      a,(DskSetRow)
                sub     1
                jr      nc,DskSetFocus
                ld      a,DSKSROWS-1
DskSetFocus:
                ld      (DskSetRow),a
DskSetChanged:
                ld      a,1
                ret

; The focused row acted on. Returns A = 1 if the window is to be
; recomposed, 0 if not.
DskSetChange:
                ld      a,(DskSetRow)
                cp      DSKSROWS-1
                jr      z,DskSetDone
                cp      DSKSROWS-2
                jr      z,DskSetStore
                ; a toggle: the icon comes or goes, so its rows of the
                ; desktop are repainted, under the window
                call    DskEntry
                ld      a,(hl)
                xor     1
                ld      (hl),a
                inc     hl
                inc     hl
                ld      b,(hl)
                ld      c,DSKSLOTH
                call    RowsAdd
                jr      DskSetChanged
DskSetStore:
                call    SetSave
                jr      nc,DskSetChanged
                ld      hl,SetSaveError
                ld      de,0
                call    DlgAlert
                xor     a
                ret
DskSetDone:
                call    WndClose
                xor     a                       ; do not touch the closed slot
                ret

; A press in the body, after the window was focused. Only the button
; cells act, and only within the frame of a window that may have
; been resized, as SetPress does.
DskSetPress:
                ld      a,(PtrX)
                srl     a
                srl     a
                srl     a
                ld      hl,WinX
                sub     (hl)
                cp      DSKSBTN
                ret     c
                cp      DSKSBTN+6
                ret     nc
                ld      hl,WinW
                ld      d,(hl)
                dec     d
                cp      d
                ret     nc
                ld      a,(PtrY)
                srl     a
                srl     a
                srl     a
                ld      hl,WinY
                sub     (hl)
                ld      hl,WinH
                ld      d,(hl)
                dec     d
                cp      d
                ret     nc
                dec     a                       ; row 0 is the title
                cp      DSKSROWS
                ret     nc
                ld      (DskSetRow),a
                call    DskSetChange
                or      a
                ret     z
                ld      a,(WndCur)
                call    WndTouch
                jp      RowsAddLive

; Six rows of label and [ON ]/[OFF ], then [SAVE] and [DONE].
DskSetDraw:
                ld      b,1
DsdLoop:
                push    bc
                ld      a,b
                dec     a
                call    DskEntry                ; HL = the entry, DE = constants
                pop     bc
                ld      a,(hl)
                push    af                      ; present
                push    bc
                ex      de,hl
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ex      de,hl                   ; HL = the label
                xor     a
                ld      (PrintInv),a
                ld      c,1
                call    WinPrint
                pop     bc
                pop     af
                push    bc
                ld      hl,SetOff
                or      a
                jr      z,DsdOff
                ld      hl,SetOn
DsdOff:
                call    DskSetButton
                pop     bc
                inc     b
                ld      a,b
                cp      DSKMAX+1
                jr      c,DsdLoop
                ld      hl,SetSaveLabel
                ld      b,DSKMAX+1
                call    DskSetButton
                ld      hl,SetDoneLabel
                ld      b,DSKMAX+2
                call    DskSetButton
                xor     a
                ld      (PrintInv),a
                ret

; HL = the text of row B's button, inverted when the row has the focus.
DskSetButton:
                ld      a,(DskSetRow)
                inc     a
                cp      b
                ld      a,0
                jr      nz,DsbNormal
                ld      a,INVBANK
DsbNormal:
                ld      (PrintInv),a
                ld      c,DSKSBTN
                jp      WinPrint

TxtDskSetup:    defb    "DESKTOP",0
