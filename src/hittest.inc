; ============================================================
;  Hit testing
;  HdlBtnDown used to walk straight into a title bar test with
;  the window geometry inlined. Everything clickable is now a row
;  in a control table, so a menu or a button is added by
;  extending the table rather than by editing the handler.
;
;  A control is five bytes: x, y, w, h, id. x and w are byte
;  columns, y and h pixel rows, matching what the drawing code
;  already works in. $FF ends the table.
;
;  The window's rows are refilled from WinX, WinY, WinW and WinH
;  before every search rather than being kept in step by hand.
;  A second copy of the window position that had to be updated
;  separately is exactly the sort of thing that goes stale
;  without anyone noticing.
;
;  The table is searched front to back and the first hit wins, so
;  its order is also the z order. Small things go above large.
; ============================================================
CTLSIZE         equ     5

CTL_NONE        equ     0
CTL_CLOSEBOX    equ     1
CTL_TITLEBAR    equ     2
CTL_WININTERIOR equ     3
CTL_MENUBAR     equ     4
CTL_DESKTOP     equ     5
CTL_MENUDROP    equ     6
CTL_DIALOG      equ     7
CTL_SCROLL      equ     9               ; the bar down a window's right edge
CTL_RESIZE      equ     8               ; the grip in a window's bottom right.
                                        ; WndHitTest's, not this table's: it
                                        ; belongs to a window and this table
                                        ; holds the things there is one of

; Refill the live rows from the model. An open menu is above
; everything, and a closed one gets a height of zero, which can never
; match because the row test is exclusive at the far edge.
CtlSync:
                ; The dialogue row tracks whatever panel is on top rather
                ; than the settings panel's fixed geometry. D3 put a
                ; confirm over a panel, and the two are different sizes in
                ; different places, so a fixed rectangle would send a press
                ; on the confirm to whatever was underneath it.
                ld      a,(DlgUp)
                or      a
                ld      a,0
                jr      z,CsNoDlg
                ld      a,(PnlX)
                ld      (CtlDlg),a
                ld      a,(PnlY)
                ld      (CtlDlg+1),a
                ld      a,(PnlW)
                ld      (CtlDlg+2),a
                ld      a,(PnlH)
CsNoDlg:
                ld      (CtlDlg+3),a
                ld      a,(MnX)
                ld      (CtlDrop),a
                ld      a,(MnY)
                ld      (CtlDrop+1),a
                ld      a,(MnW)
                ld      (CtlDrop+2),a
                ld      a,(MenuOpen)
                or      a
                ld      a,0
                jr      z,CsShut
                ld      a,(MnH)
CsShut:
                ld      (CtlDrop+3),a
                ret

; in:  B = pointer byte column, C = pointer pixel row
; out: A = control id, zero if nothing was hit
HitTest:
                call    CtlSync
                ld      hl,CtlTable
HtLoop:
                ld      a,(hl)
                inc     a                       ; $FF ends the table
                jr      z,HtMiss
                ld      a,(hl)                  ; x
                cp      b
                jr      z,HtXlo
                jr      nc,HtNext
HtXlo:
                inc     hl
                inc     hl
                add     a,(hl)                  ; x + w
                dec     hl
                dec     hl
                cp      b
                jr      z,HtNext
                jr      c,HtNext
                inc     hl
                ld      a,(hl)                  ; y
                cp      c
                jr      z,HtYlo
                jr      nc,HtNextDec
HtYlo:
                inc     hl
                inc     hl
                add     a,(hl)                  ; y + h
                dec     hl
                dec     hl
                cp      c
                jr      z,HtNextDec
                jr      c,HtNextDec
                dec     hl
                ld      de,4
                add     hl,de
                ld      a,(hl)                  ; hit: return the id
                ret
HtNextDec:
                dec     hl
HtNext:
                ld      de,CTLSIZE
                add     hl,de
                jr      HtLoop
HtMiss:
                xor     a
                ret

; Front to back is the z order. The close box is above the title bar
; it sits inside, the window is above the desktop it sits on.
CtlTable:
CtlDlg:         defb    0,0,0,0,            CTL_DIALOG
CtlDrop:        defb    0,0,0,0,            CTL_MENUDROP
CtlMenu:        defb    0,0,SCRCOLS,MENUH,       CTL_MENUBAR
CtlDesk:        defb    0,MENUH,SCRCOLS,STATRULE-MENUH, CTL_DESKTOP
                defb    $FF
