; ============================================================
;  Hit testing
;  Ported from the ZX version: everything clickable is a row in
;  a control table, searched front to back, first hit wins, so
;  the order is the z order. A control is five bytes: x, y, w,
;  h, id. On the ZX x and w were byte columns and y and h pixel
;  rows; here everything is cells, because the desktop is a tile
;  grid and nothing sits between cell boundaries. $FF ends the
;  table.
;
;  The menu drop row is refilled from the menu model before every
;  search; a closed menu gets a height of zero, which can never
;  match because the row test is exclusive at the far edge. The
;  dialogue row owns hits above the menus and windows.
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
CTL_RESIZE      equ     8
CTL_SCROLL      equ     9

; Refill the live rows from the model. The table lives in RAM
; (CtlTable) because a ROM cannot be patched; CtlInit copies the
; constant part in.
CtlDrop         equ     CtlTable+CTLDROPOFF
CtlSync:
                ld      a,(DgOpenFlag)
                or      a
                ld      a,0
                jr      z,CsDialog
                ld      a,DGH
CsDialog:
                ld      (CtlTable+3),a
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

CtlInit:
                ld      hl,CtlTableRom
                ld      de,CtlTable
                ld      bc,CTLTABSZ
                ldir
                xor     a
                ld      (DgOpenFlag),a
                ld      (MenuOpen),a
                ld      (MnX),a
                ld      (MnY),a
                ld      (MnW),a
                ld      (MnH),a
                ret

; in:  B = pointer cell column, C = pointer cell row
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

; Front to back is the z order. The drop is above the bar, the bar
; and the desktop above nothing.
CtlTableRom:
                defb    DGX,DGY,DGW,0,          CTL_DIALOG
                defb    0,0,0,0,                CTL_MENUDROP
                defb    0,MENUROW,SCRCOLS,1,    CTL_MENUBAR
                defb    0,1,SCRCOLS,STATROW-1,  CTL_DESKTOP
                defb    $FF
CTLTABSZ        equ     $-CtlTableRom
CTLDROPOFF      equ     5               ; the drop row's offset in the table
