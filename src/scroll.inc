; The right frame column holds arrows and a proportional position
; marker. APP_SCROLL returns A=first, B=total, C=visible units.
WndScrollInfo:
                ld      hl,(WinApp)
                ld      de,APP_SCROLL
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ld      a,d
                or      e
                jr      z,WsiNone
                ex      de,hl
                call    EvCallHL
                ld      (ScrFirst),a
                ld      a,c
                ld      (ScrShow),a
                cp      b
                jr      nc,WsiNone
                or      a
                ret
WsiNone:
                scf
                ret

; Thumb row = 2 + first * (height-5) / (total-visible).
ScrPosition:
                ld      a,b
                sub     c
                ld      c,a
                ld      a,(WinH)
                sub     5
                jr      nc,SpLength
                xor     a
SpLength:
                ld      b,a
                ld      a,(ScrFirst)
                ld      e,a
                ld      d,0
                ld      hl,0
                inc     b
SpMul:
                dec     b
                jr      z,SpDivide
                add     hl,de
                jr      SpMul
SpDivide:
                ld      b,2
                ld      e,c
SpDiv:
                or      a
                sbc     hl,de
                jr      c,SpDone
                inc     b
                jr      SpDiv
SpDone:
                ld      a,b
                ld      (ScrThumb),a
                ret
WndScrollBar:
                call    WndScrollInfo
                ret     c
                call    ScrPosition
                ld      b,1
                ld      a,(WinW)
                dec     a
                ld      c,a
                call    WinCell
                ld      a,(WinW)
                ld      e,a
                ld      d,0
                ld      b,1
WsbRow:
                ld      a,b
                cp      1
                ld      a,'^'
                jr      z,WsbPut
                ld      a,(WinH)
                sub     2
                cp      b
                ld      a,'v'
                jr      z,WsbPut
                ld      a,(ScrThumb)
                cp      b
                ld      a,' '+INVBANK
                jr      z,WsbPut
                ld      a,'|'
WsbPut:
                ld      (hl),a
                add     hl,de
                inc     b
                ld      a,(WinH)
                dec     a
                cp      b
                jr      nz,WsbRow
                ret
ScrPress:
                call    WndScrollInfo
                ret     c
                call    ScrPosition
                ld      a,(PtrY)
                srl     a
                srl     a
                srl     a
                ld      hl,WinY
                sub     (hl)
                cp      1
                ld      c,1
                jr      z,ScUp
                ld      b,a
                ld      a,(WinH)
                sub     2
                cp      b
                jr      z,ScDown
                ld      a,(ScrShow)
                ld      c,a
                ld      a,(ScrThumb)
                cp      b
                ret     z
                jr      c,ScDown
ScUp:
                ld      a,(ScrFirst)
                sub     c
                jr      nc,ScGo
                xor     a
                jr      ScGo
ScDown:
                ld      a,(ScrFirst)
                add     a,c
ScGo:
                ld      c,APP_SCROLLTO
                call    AppCall
                ld      a,(WndCur)
                call    WndTouch
                jp      RowsAddLive
