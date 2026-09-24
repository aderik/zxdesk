; Resize in cells. Commit on release, keeping allocations out of
; pointer-motion frames. Minimum frame is 6 x 4 cells.
RszStart:
                ld      a,1
                ld      (Resizing),a
                ret
RszEnd:
                ld      a,(Resizing)
                or      a
                ret     z
                xor     a
                ld      (Resizing),a
                call    RowsAddLive
                ld      hl,(WinW)
                push    hl
                ld      a,(PtrX)
                srl     a
                srl     a
                srl     a
                ld      hl,WinX
                sub     (hl)
                ld      b,6
                call    RszClamp
                ld      (WinW),a
                ld      a,(PtrY)
                srl     a
                srl     a
                srl     a
                ld      hl,WinY
                sub     (hl)
                ld      b,4
                call    RszClamp
                ld      b,a
                ld      a,WINBOTTOM
                sub     (hl)
                cp      b
                jr      c,RszHeight
                ld      a,b
RszHeight:
                ld      (WinH),a
                call    WndAllocBuf
                pop     hl
                jr      nc,RszPaint
                ; Free-before-allocate guarantees the old size fits again.
                ld      (WinW),hl
                call    WndAllocBuf
RszPaint:
                ; Compose the replacement now, blit next frame. Combining
                ; a large recompose with desktop restoration exceeds NTSC.
                call    WinDraw
                ld      a,2
                ld      (Resizing),a
                jp      RowsAddLive
RszClamp:
                jr      c,RszMinimum
                inc     a
                cp      b
                ret     nc
RszMinimum:
                ld      a,b
                ret
