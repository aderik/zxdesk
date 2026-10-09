; ============================================================
;  Windows on the cell grid
;  The ZX window model with the pixels taken out. A window is a
;  record, swapped into the live copy WinRec by WndSelect as
;  before, and a buffer of W by H name table codes from the heap
;  under the slot's owner byte. WinDraw composes the frame and
;  the title into that buffer and the application paints its
;  interior there; WndRepaintAll lays the desktop and every
;  buffer back to front into the name table shadow, and the
;  flush does the rest. No damage rectangles, no save-under, no
;  beam: at 700 bytes a recompose is cheaper than deciding what
;  not to paint, and the drag test measures that it drops
;  nothing.
; ============================================================
WNDMAX          equ     4
WR_X            equ     0
WR_Y            equ     1
WR_W            equ     2
WR_H            equ     3
WR_TITLE        equ     4
WR_BUFP         equ     6
WR_APP          equ     8
WR_STATEP       equ     10
WR_OLDX         equ     12
WR_OLDY         equ     13
WR_MOVED        equ     14
WNDRECSZ        equ     15

WINTOP          equ     MENUROW+1       ; the highest row a window may take
WINBOTTOM       equ     STATROW         ; and the row below the lowest

; ---- The frame tiles, codes $82 up, patterns in the Tiles block.
T_LEFT          equ     $82
T_RIGHT         equ     $83
T_BOTTOM        equ     $84
T_BL            equ     $85
T_BR            equ     $86             ; diagonal resize grip
T_CLOSE         equ     $87

; A = slot. Returns HL = its record in the table.
WndRecAt:
                ld      hl,WndTab
                or      a
                ret     z
                ld      b,a
                ld      de,WNDRECSZ
WraLoop:
                add     hl,de
                djnz    WraLoop
                ret

; A = slot: make it the live window, saving the one going out.
WndSelect:
                ld      hl,WndCur
                cp      (hl)
                ret     z
                push    af
                call    WndFlush
                pop     af
                ld      (WndCur),a
                call    WndRecAt
                ld      de,WinRec
                ld      bc,WNDRECSZ
                ldir
                jp      AppLoad

; The live record and the live application state, written back to
; their slots. Nothing to write when there is no window.
WndFlush:
                ld      a,(WndCount)
                or      a
                ret     z
                ld      a,(WndCur)
                cp      WNDMAX
                ret     nc
                call    AppSave
                ld      a,(WndCur)
                call    WndRecAt
                ex      de,hl
                ld      hl,WinRec
                ld      bc,WNDRECSZ
                ldir
                ret

; Z set if the live window is the front one.
WndIsFront:
                ld      a,(WndZ)
                ld      hl,WndCur
                cp      (hl)
                ret

; A = slot: to the front of the z order.
WndRaise:
                ld      b,a
                ld      a,(WndZ)
                cp      b
                ret     z
                ld      hl,WndZ
                ld      c,0
WrFind:
                ld      a,(hl)
                cp      b
                jr      z,WrFound
                inc     hl
                inc     c
                ld      a,(WndCount)
                cp      c
                jr      nz,WrFind
                ret
WrFound:
                ld      a,c
                or      a
                ret     z
WrShift:
                dec     hl
                ld      a,(hl)
                inc     hl
                ld      (hl),a
                dec     hl
                dec     c
                jr      nz,WrShift
                ld      (hl),b
                ret

; Returns A = a free slot, carry set if all are taken.
WndFreeSlot:
                ld      c,0
WfsTry:
                ld      a,(WndCount)
                or      a
                jr      z,WfsFound
                ld      b,a
                ld      hl,WndZ
WfsScan:
                ld      a,(hl)
                cp      c
                jr      z,WfsNext
                inc     hl
                djnz    WfsScan
WfsFound:
                ld      a,c
                or      a
                ret
WfsNext:
                inc     c
                ld      a,c
                cp      WNDMAX
                jr      c,WfsTry
                scf
                ret

; A buffer of W by H cells for the live window, from the heap.
WndAllocBuf:
                ld      hl,(WinBufP)
                call    HeapFree
                ld      hl,0
                ld      (WinBufP),hl
                ld      a,(WinH)
                ld      b,a
                ld      a,(WinW)
                ld      e,a
                ld      d,0
                ld      hl,0
WabRow:
                add     hl,de
                djnz    WabRow
                ld      b,h
                ld      c,l
                ld      a,(WndCur)
                add     a,HEAPOWN_WIN
                call    HeapAlloc
                ret     c
                ld      (WinBufP),hl
                or      a
                ret

; And its application state, initialised through APP_INIT.
WndAllocState:
                ld      hl,(WinApp)
                ld      c,(hl)
                inc     hl
                ld      b,(hl)
                ld      a,b
                or      c
                ret     z
                ld      a,(WndCur)
                add     a,HEAPOWN_WIN
                call    HeapAlloc
                ret     c
                ld      (WinStateP),hl
                ld      c,APP_INIT
                call    AppCall
                ; The live state is saved by WndFlush before repaint or
                ; selecting another slot; copying it here doubles that work.
                or      a
                ret

; ------------------------------------------------------------
;  WndOpen
;  in:  A = an application index
;  out: carry set if the table or the heap was full
;  Geometry and title come from the descriptor; each new window
;  steps two columns and one row from the application's home,
;  clamped to the screen.
; ------------------------------------------------------------
WndOpen:
                ld      (WndWantKind),a
                ld      a,(WndCount)
                cp      WNDMAX
                jp      nc,WndNoRoom
                call    WndFlush
                call    WndFreeSlot
                jp      c,WndNoRoom
                ld      (WndCur),a
                ld      hl,0
                ld      (WinBufP),hl
                ld      (WinStateP),hl
                ld      a,(WndWantKind)
                call    AppAt
                ld      (WinApp),hl
                ld      de,APP_TITLE
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                inc     hl
                ld      (WinTitle),de
                push    hl                      ; APP_X
                inc     hl
                inc     hl
                ld      a,(hl)
                ld      (WinW),a
                inc     hl
                ld      a,(hl)
                ld      (WinH),a
                pop     hl
                ld      a,(WndCount)
                add     a,a
                add     a,(hl)
                ld      c,a
                ld      a,(WinW)
                neg
                add     a,SCRCOLS
                cp      c
                jr      nc,WoXok
                ld      c,a
WoXok:
                ld      a,c
                ld      (WinX),a
                ld      (WinOldX),a
                inc     hl
                ld      a,(WndCount)
                add     a,(hl)
                ld      c,a
                ld      a,(WinH)
                neg
                add     a,WINBOTTOM
                cp      c
                jr      nc,WoYok
                ld      c,a
WoYok:
                ld      a,c
                ld      (WinY),a
                ld      (WinOldY),a
                xor     a
                ld      (WinMoved),a
                call    WndAllocBuf
                jr      c,WoNoBuf
                call    WndAllocState
                jr      c,WoNoBuf
                ld      a,(WndCount)
                or      a
                jr      z,WoFirst
                ld      e,a
                ld      d,0
                ld      hl,WndZ
                add     hl,de
                ld      d,h
                ld      e,l
                dec     hl
                ld      b,a
WoShift:
                ld      a,(hl)
                ld      (de),a
                dec     hl
                dec     de
                djnz    WoShift
WoFirst:
                ld      a,(WndCur)
                ld      (WndZ),a
                ld      hl,WndCount
                inc     (hl)
                ld      a,(WndCount)
                cp      2
                jr      c,WoNoOld
                ld      a,(WndZ+1)              ; the old front loses its cap
                call    WndTouchCap
                ld      a,(WndZ+1)
                call    RowsAddSlot
WoNoOld:
                ld      a,(WndCur)
                call    WndTouch
                call    RowsAddLive
                or      a
                ret
WndNoRoom:
                scf
                ret
WoNoBuf:
                ld      a,(WndCur)
                add     a,HEAPOWN_WIN
                call    HeapFreeOwner
                ld      hl,0
                ld      (WinBufP),hl
                ld      (WinStateP),hl
                ld      a,(WndCount)
                or      a
                jr      z,WoNone
                ld      a,$FF
                ld      (WndCur),a              ; nothing live, so the select
                ld      a,(WndZ)                ; below reloads the front
                call    WndSelect
                scf
                ret
WoNone:
                ld      a,$FF
                ld      (WndCur),a
                scf
                ret

; Close the live window. Its memory comes back by owner, the window
; behind it becomes live, or nothing does.
WndClose:
                ld      a,(WndCount)
                or      a
                ret     z
                ld      a,(WndForce)
                or      a
                jr      nz,WcGo
                or      a
                ld      c,APP_CLOSE
                call    AppCall
                ret     c
WcGo:
                xor     a
                ld      (WndForce),a
                ld      hl,WndZ
                ld      de,WndZ+1
                ld      a,(WndCount)
                dec     a
                ld      b,a
                jr      z,WcShifted
WcShift:
                ld      a,(de)
                ld      (hl),a
                inc     hl
                inc     de
                djnz    WcShift
WcShifted:
                call    RowsAddLive             ; the rows it vacates
                ld      a,(WndCur)
                add     a,HEAPOWN_WIN
                call    HeapFreeOwner
                ld      hl,WndCount
                dec     (hl)
                ld      a,$FF
                ld      (WndCur),a
                ld      a,(hl)
                or      a
                ret     z
                ld      a,(WndZ)
                call    WndSelect
                ld      a,(WndZ)                ; the new front gets the cap
                call    WndTouchCap
                jp      RowsAddLive

; A = slot: raise, focus and repaint.
WndFocusTo:
                ld      hl,WndZ
                cp      (hl)
                jp      z,WndSelect
                push    af
                ld      a,(WndZ)                ; the old front loses its cap
                call    WndTouchCap
                ld      a,(WndZ)
                call    RowsAddSlot
                pop     af
                call    WndRaise
                ld      a,(WndZ)
                call    WndSelect
                ld      a,(WndZ)
                call    WndTouchCap
                jp      RowsAddLive

; in:  B = cell column, C = cell row
; out: A = CTL_CLOSEBOX, CTL_TITLEBAR, CTL_RESIZE, CTL_SCROLL,
;      CTL_WININTERIOR or CTL_NONE;
;      WndHit = the slot, front to back so the first hit wins
WndHitTest:
                ld      a,b
                ld      (WhtCol),a
                ld      a,c
                ld      (WhtRow),a
                call    WndFlush
                ld      a,(WndCount)
                or      a
                ret     z
                ld      b,a
                ld      c,0
WhtLoop:
                push    bc
                ld      e,c
                ld      d,0
                ld      hl,WndZ
                add     hl,de
                ld      a,(hl)
                ld      (WndHit),a
                call    WndRecAt
                ld      e,(hl)                  ; x
                inc     hl
                ld      d,(hl)                  ; y
                inc     hl
                ld      c,(hl)                  ; w
                inc     hl
                ld      b,(hl)                  ; h
                ld      a,(WhtCol)
                sub     e
                jr      c,WhtNext
                cp      c
                jr      nc,WhtNext
                ld      e,a                     ; the column within
                ld      a,(WhtRow)
                sub     d
                jr      c,WhtNext
                cp      b
                jr      nc,WhtNext
                or      a
                jr      nz,WhtBody
                ld      a,e
                or      a
                ld      a,CTL_CLOSEBOX
                jr      z,WhtDone
                ld      a,CTL_TITLEBAR
                jr      WhtDone
WhtBody:
                dec     c
                push    af
                ld      a,e
                cp      c
                jr      nz,WhtNotRight
                pop     af
                dec     b
                cp      b
                ld      a,CTL_RESIZE
                jr      z,WhtDone
                ; HL still points at WR_H. Only scrolling applications
                ; own the right edge; ScrPress checks the visible range.
                ld      de,WR_APP-WR_H
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ex      de,hl
                ld      de,APP_SCROLL
                add     hl,de
                ld      a,(hl)
                inc     hl
                or      (hl)
                ld      a,CTL_SCROLL
                jr      nz,WhtDone
                jr      WhtInterior
WhtNotRight:
                pop     af
WhtInterior:
                ld      a,CTL_WININTERIOR
WhtDone:
                pop     bc
                ret
WhtNext:
                pop     bc
                inc     c
                djnz    WhtLoop
                ld      a,CTL_NONE
                ret

; ------------------------------------------------------------
;  Composition
;  WinDraw: the frame and the title into the live window's
;  buffer, the interior blank, then the application's draw
;  vector. WndRepaintAll: desktop, then every buffer back to
;  front into the shadow, recomposing each so the front one's
;  title is the inverted one.
; ------------------------------------------------------------
WinDraw:
                ld      hl,(WinBufP)
                ld      a,h
                or      l
                ret     z
                call    WinDrawCap
                jr      WdBodyStart

; The title row alone: the close box, then the title on the inverted
; bank if this is the front window.
WinDrawCap:
                ld      hl,(WinBufP)
                ld      a,h
                or      l
                ret     z
                ld      (hl),T_CLOSE
                inc     hl
                push    hl
                call    WndIsFront              ; uses HL: the first version
                pop     hl                      ; filled the cap over WndCur
                ld      a,' '                   ; and twenty variables after it
                jr      nz,WdCap
                ld      a,' '+INVBANK
WdCap:
                ld      (WdCapFill),a
                ld      a,(WinW)
                dec     a
                ld      b,a
                ld      a,(WdCapFill)
WdCapLoop:
                ld      (hl),a
                inc     hl
                djnz    WdCapLoop
                ld      hl,(WinTitle)
                ld      a,(WdCapFill)
                and     INVBANK
                ld      (PrintInv),a
                ld      b,0
                ld      c,1
                call    WinPrint
                xor     a
                ld      (PrintInv),a
                ret
WdBodyStart:
                ; the body rows: edge, blank, edge, unless the application
                ; paints the whole interior
                ld      hl,(WinApp)
                ld      de,APP_FLAGS
                add     hl,de
                ld      a,(hl)
                and     APPF_FULL
                ; Full painters cover their home size only. Tiled windows
                ; need the remaining interior cleared as well.
                push    af
                ld      hl,(WinApp)
                ld      de,APP_W
                add     hl,de
                ld      a,(WinW)
                cp      (hl)
                jr      nz,WdNotFull
                inc     hl
                ld      a,(WinH)
                cp      (hl)
                jr      nz,WdNotFull
                pop     af
                jr      WdFullReady
WdNotFull:
                pop     af
                xor     a
WdFullReady:
                ld      (WdFull),a
                ld      a,(WinH)
                sub     2
                ld      b,a
                ld      hl,(WinBufP)
                ld      a,(WinW)
                ld      e,a
                ld      d,0
                add     hl,de                   ; row 1
                ld      a,(WdFull)
                or      a
                jr      nz,WdEdgesOnly
WdBody:
                push    bc
                ld      (hl),T_LEFT
                inc     hl
                ld      (hl),' '                ; the blank spreads itself by
                ld      d,h                     ; LDIR, 21 cycles a cell
                ld      e,l
                inc     de
                ld      a,(WinW)
                sub     3
                ld      c,a
                ld      b,0
                ldir
                ex      de,hl
                ld      (hl),T_RIGHT
                inc     hl
                pop     bc
                djnz    WdBody
                jr      WdBottomRow
WdEdgesOnly:
                ; the application writes every interior cell itself
                ld      (hl),T_LEFT
                ld      a,(WinW)
                dec     a
                ld      e,a
                ld      d,0
                add     hl,de
                ld      (hl),T_RIGHT
                inc     hl
                djnz    WdEdgesOnly
WdBottomRow:
                ; the bottom row
                ld      (hl),T_BL
                inc     hl
                ld      a,(WinW)
                sub     2
                ld      b,a
WdBottom:
                ld      (hl),T_BOTTOM
                inc     hl
                djnz    WdBottom
                ld      (hl),T_BR
                ld      c,APP_DRAW
                call    AppCall
                jp      WndScrollBar

; B = row, C = column within the live window's buffer. Returns HL =
; the cell, for an application that builds its rows in place.
WinCell:
                ld      a,(WinW)
                ld      e,a
                ld      d,0
                ld      hl,0
                inc     b
WcRow:
                dec     b
                jr      z,WcRowDone
                add     hl,de
                jr      WcRow
WcRowDone:
                ld      b,0
                add     hl,bc
                ld      de,(WinBufP)
                add     hl,de
                ret

; HL = null terminated text, B = row and C = column within the live
; window's buffer, bit 7 from PrintInv. Body text is clipped to the
; interior; row zero is the title and may use the final column.
WinPrint:
                ld      a,b
                or      a
                ld      a,(WinW)
                jr      z,WpWidth
                ld      a,(WinH)
                dec     a
                cp      b
                ret     z
                ret     c
                ld      a,(WinW)
                dec     a
WpWidth:
                sub     c
                ret     z
                ret     c
                push    af
                push    hl
                ld      a,(WinW)
                ld      e,a
                ld      d,0
                ld      hl,0
                inc     b
WpRow:
                dec     b
                jr      z,WpRowDone
                add     hl,de
                jr      WpRow
WpRowDone:
                ld      b,0
                add     hl,bc
                ld      de,(WinBufP)
                add     hl,de
                ex      de,hl                   ; DE = the cell
                pop     hl
                pop     af
                ld      b,a                     ; cells to the edge
                ld      a,(PrintInv)
                ld      c,a
WpLoop:
                ld      a,(hl)
                or      a
                ret     z
                or      c
                ld      (de),a
                inc     hl
                inc     de
                djnz    WpLoop
                ret

; The live window's buffer into the shadow, the rows within
; RowLo..RowHi-1 only. HL walks the buffer and DE the shadow, a row
; at a time; a row outside the range is stepped over. Measured
; against CellAddr per row: 3.2 ms to 2.3 for ten rows of 23.
WinBlit:
                ld      hl,(WinBufP)
                ld      a,h
                or      l
                ret     z
                ld      a,(WinY)
                ld      b,a
                push    hl
                ld      a,(WinX)
                ld      c,a
                call    ShadowRow
                ld      b,0
                add     hl,bc
                ex      de,hl                   ; DE = the shadow cell
                pop     hl                      ; HL = the buffer
                ld      a,(WinH)
                ld      b,a
                ld      a,(WinY)
                ld      c,a                     ; C = the row
WbRow:
                push    bc
                ld      a,(RowLo)
                cp      c
                jr      z,WbIn
                jr      nc,WbSkip
WbIn:
                ld      a,(RowHi)
                cp      c
                jr      z,WbSkip
                jr      c,WbSkip
                ld      a,(WinW)
                ld      c,a
                ld      b,0
                ldir                            ; HL and DE both advance W
                ld      a,(WinW)
                neg
                add     a,SCRCOLS               ; DE on to the next row
                ld      c,a
                ld      b,0
                ex      de,hl
                add     hl,bc
                ex      de,hl
                jr      WbNext
WbSkip:
                ld      a,(WinW)
                ld      c,a
                ld      b,0
                add     hl,bc
                ex      de,hl
                ld      bc,SCRCOLS
                add     hl,bc
                ex      de,hl
WbNext:
                pop     bc
                inc     c
                djnz    WbRow
                ret

; ------------------------------------------------------------
;  Repainting is by row range and by dirty slot. Every change
;  says which rows it touched (RowsAdd) and which windows need
;  recomposing (WndTouch: a fresh window, a key, a focus change
;  that flips two title bars); WinRedraw at the end of the frame
;  then paints the desktop for those rows, recomposes the dirty
;  buffers, and blits every window's rows within the range,
;  back to front. A drag recomposes nothing and blits eleven
;  rows. Measured: a full recompose and flush of 22 rows was
;  about 30 ms, more than a frame, and dropped two frames on
;  every window open.
; ------------------------------------------------------------

; B = first row, C = count: widen the pending range.
RowsAdd:
                ld      a,1
                ld      (RepaintDue),a
                ld      a,(RowLo)
                cp      b
                jr      c,RaLoOk
                ld      a,b
                ld      (RowLo),a
RaLoOk:
                ld      a,b
                add     a,c
                ld      b,a
                ld      a,(RowHi)
                cp      b
                jr      nc,RaHiOk
                ld      a,b
                ld      (RowHi),a
RaHiOk:
                ret

; The live window's rows.
RowsAddLive:
                ld      a,(WinY)
                ld      b,a
                ld      a,(WinH)
                ld      c,a
                jr      RowsAdd

; A = slot: its buffer is recomposed on the next repaint.
WndTouch:
                ld      hl,WndDirty
WtSet:
                ld      b,a
                inc     b
                ld      a,$80
WtShift:
                rlca
                djnz    WtShift
                or      (hl)
                ld      (hl),a
                ret

; A = slot: only its title row is redrawn. A focus change flips two
; title bars and nothing else, and the calendar's interior was
; measured at six milliseconds to rebuild.
WndTouchCap:
                ld      hl,WndCapDirty
                jr      WtSet

; Slot A's rows, from the table.
RowsAddSlot:
                call    WndRecAt
                inc     hl
                ld      b,(hl)                  ; y
                inc     hl
                inc     hl
                ld      c,(hl)                  ; h
                jr      RowsAdd

; Everything, for a first paint or the test build.
WndRepaintAll:
                ld      b,WINTOP
                ld      c,WINBOTTOM-WINTOP
                call    RowsAdd
                ld      a,$FF
                ld      (WndDirty),a
                ; fall through

; The pending range: desktop, dirty buffers recomposed, all windows
; blitted within the range, back to front.
WinRedraw:
                ld      a,(DgOpenFlag)
                or      a
                ret     nz                      ; keep the save-under stable
                ld      a,(Resizing)
                cp      2
                jr      nz,WrdReady
                xor     a
                ld      (Resizing),a
                ret                             ; replacement composed this frame
WrdReady:
                ld      a,(RepaintDue)
                cp      2
                jr      nz,WrdPaint
                dec     a
                ld      (RepaintDue),a
                ret                             ; SETTINGS open: paint next frame
WrdPaint:
                or      a
                ret     z
                ld      hl,(Recomposes)
                inc     hl
                ld      (Recomposes),hl
                call    WndFlush
                call    MarkRows                ; once, for the whole range
                call    DrawDesktopRows
                ld      a,(WndCount)
                or      a
                jr      z,WrdDone
                ld      b,a
WraPaint:
                push    bc
                ld      a,b
                dec     a
                ld      e,a
                ld      d,0
                ld      hl,WndZ
                add     hl,de
                ld      a,(hl)
                call    WndSelect
                ld      a,(WndCur)
                ld      b,a
                inc     b
                ld      a,$80
WraBit:
                rlca
                djnz    WraBit
                ld      hl,WndDirty
                and     (hl)
                ld      c,a
                ld      a,(WndCur)
                ld      b,a
                inc     b
                ld      a,$80
WraBit2:
                rlca
                djnz    WraBit2
                ld      hl,WndCapDirty
                and     (hl)
                or      c                       ; bit 7..4: content or cap
                jr      z,WraBlit
                ld      a,c
                or      a
                jr      z,WraCap
                call    WinDraw
                jr      WraBlit
WraCap:
                call    WinDrawCap
WraBlit:
                call    WinBlit
                pop     bc
                djnz    WraPaint
WrdDone:
                xor     a
                ld      (RepaintDue),a
                ld      (WndDirty),a
                ld      (WndCapDirty),a
                ld      a,WINBOTTOM
                ld      (RowLo),a
                ld      a,WINTOP
                ld      (RowHi),a
                ret

; The desktop rows within the range: the rule under the bar, then
; the lattice.
; A row is sixteen PUSHes with the stack pointed at its end, eleven
; cycles a pair against thirteen a cell for stores: 2.4 ms to 1.3
; for ten rows, measured. Interrupts are off while the stack is
; borrowed, or the BIOS handler would push into the desktop. The
; rows are marked dirty once by WinRedraw.
DrawDesktopRows:
                ld      a,(RowLo)
                ld      b,a
                call    ShadowRow
                ld      de,SCRCOLS
                add     hl,de                   ; the end of the first row
                ld      (SpSave),sp
DdLoop:
                ld      a,(RowHi)
                cp      b
                jr      z,DdDone
                jr      c,DdDone
                ld      de,T_LATTICE*257        ; a pair of lattice cells
                ld      a,b
                cp      WINTOP
                jr      nz,DdTile
                ld      de,T_RULE*257
DdTile:
                di
                ld      sp,hl
                rept    SCRCOLS/2
                push    de
                endm
                ld      sp,(SpSave)
                ei
                ld      de,SCRCOLS
                add     hl,de
                inc     b
                jr      DdLoop
DdDone:
                ret

; The whole desktop, for InitScreen.
DrawDesktop:
                ld      a,WINTOP
                ld      (RowLo),a
                ld      a,WINBOTTOM
                ld      (RowHi),a
                call    MarkRows
                jp      DrawDesktopRows

; RowLo..RowHi-1 marked dirty for the flush.
MarkRows:
                ld      a,(RowLo)
                ld      c,a
MrsLoop:
                ld      a,(RowHi)
                cp      c
                ret     z
                ret     c
                ld      a,c
                push    bc
                call    MarkRow
                pop     bc
                inc     c
                jr      MrsLoop

; ------------------------------------------------------------
;  Dragging, from a press on the title bar. Offsets in cells.
; ------------------------------------------------------------
WndGrab:
                ld      a,1
                ld      (Dragging),a
                ld      a,(PtrX)
                rrca
                rrca
                rrca
                and     $1F
                ld      c,a
                ld      a,(WinX)
                neg
                add     a,c
                ld      (DragDX),a
                ld      a,(PtrY)
                rrca
                rrca
                rrca
                and     $1F
                ld      c,a
                ld      a,(WinY)
                neg
                add     a,c
                ld      (DragDY),a
                ret

DdMove:
                ld      a,(WinX)
                ld      (WinOldX),a
                ld      a,(WinY)
                ld      (WinOldY),a
                ld      a,(PtrX)
                rrca
                rrca
                rrca
                and     $1F
                ld      hl,DragDX
                sub     (hl)
                jr      nc,DdXpos
                xor     a
DdXpos:
                ld      c,a
                ld      a,(WinW)
                neg
                add     a,SCRCOLS
                cp      c
                jr      nc,DdXset
                ld      c,a
DdXset:
                ld      a,c
                ld      (WinX),a
                ld      a,(PtrY)
                rrca
                rrca
                rrca
                and     $1F
                ld      hl,DragDY
                sub     (hl)
                jr      c,DdYtop
                cp      WINTOP
                jr      nc,DdYpos
DdYtop:
                ld      a,WINTOP
DdYpos:
                ld      c,a
                ld      a,(WinH)
                neg
                add     a,WINBOTTOM
                cp      c
                jr      nc,DdYset
                ld      c,a
DdYset:
                ld      a,c
                ld      (WinY),a
                ld      a,(WinOldX)
                ld      c,a
                ld      a,(WinX)
                cp      c
                jr      nz,DdMoved
                ld      a,(WinOldY)
                ld      c,a
                ld      a,(WinY)
                cp      c
                ret     z
DdMoved:
                ld      a,(WinOldY)
                ld      b,a
                ld      a,(WinH)
                ld      c,a
                call    RowsAdd
                jp      RowsAddLive

WndInit:
                xor     a
                ld      (WndCount),a
                ld      (Dragging),a
                ld      (Resizing),a
                ld      (WndForce),a
                ld      (WinMoved),a
                ld      (RepaintDue),a
                ld      (WndDirty),a
                ld      (WndCapDirty),a
                ld      a,WINBOTTOM
                ld      (RowLo),a
                ld      a,WINTOP
                ld      (RowHi),a
                ld      a,$FF
                ld      (WndCur),a
                ld      hl,0
                ld      (WinBufP),hl
                ld      (WinStateP),hl
                ld      (WinApp),hl
                ret

; The menu actions that open windows.
WndOpenCal:
                ld      a,APP_CAL
                jp      WndOpen
AboutOpen:
                ld      a,APP_ABOUT
                jp      WndOpen

; ------------------------------------------------------------
;  The about window: no state, three lines of text.
; ------------------------------------------------------------
ABOUTW          equ     18
ABOUTH          equ     5
WdAbout:
                xor     a
                ld      (PrintInv),a
                ld      hl,TxtAbout1
                ld      b,1
                ld      c,1
                call    WinPrint
                ld      hl,TxtAbout2
                ld      b,2
                ld      c,1
                call    WinPrint
                ld      hl,TxtAbout3
                ld      b,3
                ld      c,1
                jp      WinPrint
TxtAboutTitle:  defb    "ABOUT",0
TxtAbout1:      defb    "MSX DESK",0
TxtAbout2:      defb    "AFTER ZX DESK BY",0
TxtAbout3:      defb    "DAMIAN COOPER",0
