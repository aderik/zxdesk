; ============================================================
;  Scroll bars
;  D2's control library was specified as "button, checkbox,
;  radio, list box, scrollbar, single line text field" and four
;  of those were built. This is the one that was missed and the
;  one GEM's windows were practically defined by.
;
;  It matters more than a tidy frame. The notepad scrolls sixteen
;  lines through seven and says nothing about where in the
;  document you are, and the commander lists files with no way of
;  knowing how many are off the bottom. A window that hides part
;  of its contents and does not say so is lying quietly.
;
;  THE APPLICATION IS ASKED, NOT TOLD. The descriptor carries a
;  vector that answers three numbers: how many units there are,
;  how many are showing, and which is first. That is the whole
;  interface, and it is the one GEM's AES used, because the
;  window frame has no business knowing that a unit is a line of
;  text in one window and a filename in another.
;
;  AND THE BAR SENDS KEYS. Pressing an arrow posts KEY_UP or
;  KEY_DOWN to the application's own key vector, so the mouse and
;  the keyboard scroll by exactly the same code and an
;  application that scrolls from the keyboard gets the bar for
;  nothing. No second path to keep in step, and no clamping to
;  write twice.
;
;  It costs the interior a byte column, which is why the notepad
;  opens two columns wider than it did.
; ============================================================
SCRARROW        equ     8               ; pixel rows for each arrow

; ------------------------------------------------------------
;  WndScrollInfo
;  out: carry set if this window does not scroll, otherwise
;       A = first visible unit, B = total, C = showing
; ------------------------------------------------------------
WndScrollInfo:
                ld      hl,(WinApp)
                ld      a,h
                or      l
                jr      z,WsiNone
                ld      de,APP_SCROLL
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ld      a,d
                or      e
                jr      z,WsiNone
                ex      de,hl
                call    WsiJump
                ld      (WsiFirst),a            ; what the application just
                                                ; said, before B and C are
                                                ; tested and A is spent
                ; a total of nought or a view as big as the document needs
                ; no bar, and saying so here keeps the test in one place
                ld      a,b
                or      a
                jr      z,WsiNone
                ld      a,c
                cp      b
                jr      nc,WsiNone
                ld      a,(WsiFirst)
                or      a                       ; carry clear
                ret
WsiJump:
                jp      (hl)
WsiNone:
                scf
                ret

; ------------------------------------------------------------
;  WndScrollBar
;  Draws it down the column just inside the right border, from
;  the bottom of the cap to the bottom border. An up arrow, a
;  down arrow, and a channel between them with a thumb whose
;  length and position say how much of the document is showing
;  and where.
; ------------------------------------------------------------
; The bar's presence, cached in a byte. WinPrintClip needs to know
; whether a column of the interior is spoken for, and it is called
; once per line of text: asking the application every time would have
; the notepad scanning its whole document for every line it draws.
WndBarCheck:
                call    WndScrollInfo
                ld      a,0
                jr      c,WbcSet
                inc     a
WbcSet:
                ld      (WinBarOn),a
                ret

WndScrollBar:
                call    WndBarCheck
                call    WndScrollInfo
                ret     c
                ld      (WsiFirst),a
                ld      a,b
                ld      (WsiTotal),a
                ld      a,c
                ld      (WsiShow),a
                ; the column, and the rows it runs between
                ld      a,(WinX)
                ld      b,a
                ld      a,(WinW)
                add     a,b
                sub     2
                ld      (ScrCol),a
                ld      a,(WinY)
                add     a,WinCapH
                ld      (ScrTop),a
                ld      a,(WinH)
                sub     WinCapH+1
                ld      (ScrLen),a
                cp      SCRARROW*2+4
                ret     c                       ; too short to be a bar at all
                ; the trough is what is left between the two arrows
                sub     SCRARROW*2
                ld      (ScrTrough),a
                ; thumb length = show * trough / total, at least three
                ld      a,(WsiShow)
                ld      h,0
                ld      l,a
                ld      a,(ScrTrough)
                call    ScrMul
                ld      a,(WsiTotal)
                ld      c,a
                call    ScrDiv
                ld      a,l
                cp      3
                jr      nc,ScrThLen
                ld      a,3
ScrThLen:
                ld      (ScrThumbH),a
                ; thumb top = first * trough / total
                ld      a,(WsiFirst)
                ld      h,0
                ld      l,a
                ld      a,(ScrTrough)
                call    ScrMul
                ld      a,(WsiTotal)
                ld      c,a
                call    ScrDiv
                ld      a,l
                ld      (ScrThumbY),a
                ; and it must not hang off the end of the trough
                ld      b,a
                ld      a,(ScrThumbH)
                add     a,b
                ld      c,a
                ld      a,(ScrTrough)
                cp      c
                jr      nc,ScrDrawIt
                ld      b,a
                ld      a,(ScrThumbH)
                neg
                add     a,b
                ld      (ScrThumbY),a
ScrDrawIt:
                ; the up arrow
                ld      hl,ScrArrUp
                ld      a,(ScrTop)
                ld      (ScrRow),a
                ld      b,SCRARROW
                call    ScrBlit
                ; the trough, with the thumb in it
                xor     a
                ld      (ScrIx),a
ScrTroughLoop:
                ld      a,(ScrIx)
                ld      b,a
                ld      a,(ScrTrough)
                cp      b
                jr      z,ScrArrowDn
                ; inside the thumb?
                ld      a,(ScrThumbY)
                ld      c,a
                ld      a,b
                sub     c
                jr      c,ScrPlain
                ld      c,a
                ld      a,(ScrThumbH)
                cp      c
                jr      z,ScrPlain
                jr      c,ScrPlain
                ld      a,$FF                   ; the thumb, filled
                jr      ScrPut
ScrPlain:
                ld      a,$81                   ; the channel it slides in
ScrPut:
                ld      c,a
                ld      a,(ScrCol)
                ld      b,a
                ld      a,(ScrRow)
                push    bc
                ld      c,b
                call    AddrAt
                pop     bc
                ld      (hl),c
                ld      hl,ScrRow
                inc     (hl)
                ld      hl,ScrIx
                inc     (hl)
                jr      ScrTroughLoop
ScrArrowDn:
                ld      hl,ScrArrDn
                ld      b,SCRARROW
                ; fall through

; HL = B rows of one byte, written down ScrCol from ScrRow.
ScrBlit:
                ld      a,(hl)
                ld      (ScrByte),a
                push    hl
                push    bc
                ld      a,(ScrCol)
                ld      c,a
                ld      a,(ScrRow)
                call    AddrAt
                ld      a,(ScrByte)
                ld      (hl),a
                pop     bc
                pop     hl
                inc     hl
                ld      a,(ScrRow)
                inc     a
                ld      (ScrRow),a
                djnz    ScrBlit
                ret

; HL = HL * A, for the small numbers a scroll bar deals in.
ScrMul:
                ld      b,a
                ld      de,0
                ld      a,b
                or      a
                jr      z,ScrMulDone
ScrMulLoop:
                ex      de,hl
                add     hl,de
                ex      de,hl
                djnz    ScrMulLoop
ScrMulDone:
                ex      de,hl
                ret

; HL / C, quotient in HL. Sixteen shifts rather than a subtraction
; loop, because a total of one would make that loop as long as the
; trough is tall and this runs on every repaint.
ScrDiv:
                ld      a,c
                or      a
                ret     z
                xor     a
                ld      b,16
ScrDivLoop:
                add     hl,hl
                rla
                cp      c
                jr      c,ScrDivNo
                sub     c
                inc     l
ScrDivNo:
                djnz    ScrDivLoop
                ret

; ------------------------------------------------------------
;  ScrPress
;  in: C = the pixel row pressed
;  An arrow steps one unit, the channel a page. Both do it by
;  sending the application the key it would have got from the
;  keyboard, so there is one scrolling path and not two.
; ------------------------------------------------------------
ScrPress:
                ld      a,c
                ld      (ScrHitY),a
                call    WndScrollInfo
                ret     c
                ld      (WsiFirst),a
                ld      a,b
                ld      (WsiTotal),a
                ld      a,c
                ld      (WsiShow),a
                ld      a,(WinY)
                add     a,WinCapH
                ld      b,a
                ld      a,(ScrHitY)
                sub     b                       ; rows down the bar
                ret     c
                ld      (ScrHitY),a
                cp      SCRARROW
                jr      c,ScrStepUp
                ld      b,a
                ld      a,(WinH)
                sub     WinCapH+1+SCRARROW
                cp      b
                jr      c,ScrStepDn             ; in the bottom arrow
                ; the channel, a page either side of the thumb
                ld      a,(WsiShow)
                dec     a
                jr      z,ScrStepDn
                ld      (ScrStep),a
                ld      a,(ScrHitY)
                sub     SCRARROW
                ld      b,a
                ld      a,(ScrThumbY)
                cp      b
                jr      c,ScrDownBy
                jr      ScrUpBy
ScrStepUp:
                ld      a,1
                ld      (ScrStep),a
                jr      ScrUpBy
ScrStepDn:
                ld      a,1
                ld      (ScrStep),a
                ; fall through

; The view moves rather than the cursor. An arrow that moved a caret
; and only scrolled when the caret ran out of window would be a
; cursor key with a picture of an arrow on it.
ScrDownBy:
                ld      a,(WsiFirst)
                ld      b,a
                ld      a,(ScrStep)
                add     a,b
                jr      ScrGo
ScrUpBy:
                ld      a,(WsiFirst)
                ld      b,a
                ld      a,(ScrStep)
                ld      c,a
                ld      a,b
                sub     c
                jr      nc,ScrGo
                xor     a                       ; not past the top
ScrGo:
                ld      c,APP_SCROLLTO
                jp      AppCall

ScrArrUp:       defb    $00,$18,$3C,$7E,$FF,$81,$81,$81
ScrArrDn:       defb    $81,$81,$81,$FF,$7E,$3C,$18,$00

WsiFirst:       defb    0
WsiTotal:       defb    0
WsiShow:        defb    0
ScrCol:         defb    0
ScrTop:         defb    0
ScrLen:         defb    0
ScrTrough:      defb    0
ScrThumbY:      defb    0
ScrThumbH:      defb    0
ScrRow:         defb    0
ScrIx:          defb    0
ScrByte:        defb    0
ScrHitY:        defb    0
ScrStep:        defb    0
WinBarOn:       defb    0
