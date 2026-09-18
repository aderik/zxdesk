; ============================================================
;  Resizable windows
;  This was an open question rather than a plan, and the answer
;  turned out to be that F1 had already paid for it. A window's
;  pixels are W by H from the heap, so a resize is a free and an
;  allocate, and WndAllocBuf was written for exactly that: "give
;  back the old one first, so a resize is two calls".
;
;  What the heap does not answer is what the screen does while
;  the corner is being dragged. Recomposing the window every
;  frame would be a fresh WinDraw and WinGrab, about 70,000 T,
;  which is a frame on its own. So the drag moves an outline and
;  nothing else, and the window is composed once when the button
;  comes up. That is what every system of this period did and the
;  reason is the same one.
;
;  The outline is XOR, so erasing it is drawing it again and
;  there is no save under to size, no arena to add to and nothing
;  to restore in the wrong order.
; ============================================================
RSZGRIP         equ     8               ; the grip is the last column and the
                                        ; last eight rows, which is one
                                        ; character cell
RSZMINW         equ     6               ; narrower than this and the close box
                                        ; and the title have nowhere to go
RSZMINH         equ     WinCapH+8       ; the cap plus one line of anything

; ------------------------------------------------------------
;  WndGrip
;  A single 45 degree line in the corner cell, so that a window
;  which can be resized says so. The grip has been there since
;  the day it was written and has been invisible the whole time,
;  which makes it a feature you have to be told about rather than
;  one you can see.
;
;  It is ORed over the borders rather than written instead of
;  them, so the two ends of the line disappear into the frame and
;  what shows is the diagonal across the corner. One line and not
;  three: this display has one bit and eight pixels to say it in,
;  and three lines at that size is a smudge.
;
;  Drawn last, after the interior fill and the borders and
;  anything the application put inside, because it is the
;  outermost piece of chrome there is.
; ------------------------------------------------------------
WndGrip:
                ld      a,(WinX)
                ld      b,a
                ld      a,(WinW)
                add     a,b
                dec     a
                ld      (GripCol),a
                ld      a,(WinY)
                ld      b,a
                ld      a,(WinH)
                add     a,b
                sub     RSZGRIP                 ; the cell's top row
                ld      (GripRow),a
                ld      hl,GripBits
                ld      b,RSZGRIP
GripLoop:
                ld      a,(hl)
                ld      (GripByte),a
                push    hl
                push    bc
                ld      a,(GripCol)
                ld      c,a
                ld      a,(GripRow)
                call    AddrAt
                ld      a,(GripByte)
                or      (hl)                    ; over the border, not instead
                ld      (hl),a
                pop     bc
                pop     hl
                inc     hl
                ld      a,(GripRow)
                inc     a
                ld      (GripRow),a
                djnz    GripLoop
                ret

; Bottom left to top right, which is the way every system that has
; ever drawn one of these has drawn it.
GripBits:       defb    $01,$02,$04,$08,$10,$20,$40,$80
GripCol:        defb    0
GripRow:        defb    0
GripByte:       defb    0

; ------------------------------------------------------------
;  RszStart
;  The grip was pressed. The window keeps its size until the
;  button comes up; what moves is an outline.
; ------------------------------------------------------------
RszStart:
                call    NoteSync                ; the same reason a drag does
                                                ; it: what is on screen has to
                                                ; be what the buffer holds
                ld      a,1
                ld      (Resizing),a
                ld      a,(WinW)
                ld      (RszW),a
                ld      (RszOldW),a             ; kept, because the heap can
                ld      a,(WinH)                ; refuse the new size and the
                ld      (RszH),a                ; old one is what it goes back
                ld      (RszOldH),a             ; to
                jp      RszShow

; ------------------------------------------------------------
;  RszMove
;  The pointer moved with the grip held. Work out the size the
;  corner is asking for, clamp it, and move the outline if it
;  changed.
; ------------------------------------------------------------
RszMove:
                call    RszWant
                ld      hl,RszW
                cp      (hl)
                jr      nz,RszChanged
                ld      a,(RszWantH)
                ld      hl,RszH
                cp      (hl)
                ret     z                       ; the same rectangle, so the
                                                ; outline is already right
RszChanged:
                call    RszShow                 ; XOR the old one away
                call    RszWant
                ld      (RszW),a
                ld      a,(RszWantH)
                ld      (RszH),a
                jp      RszShow

; out: A = the width the pointer is asking for, RszWantH = the height
;
; Both are worked out as pointer minus window edge, and both check the
; borrow. Doing it as a NEG and an ADD instead reads the same and is
; not: a pointer to the left of the window gave a width of 249, which
; sailed past the minimum test and was then cut down by the screen
; edge, so dragging the corner up and left made the window as large as
; it could be rather than as small.
RszWant:
                ld      a,(PtrX)
                srl     a
                srl     a
                srl     a
                ld      c,a                     ; the pointer's byte column
                ld      a,(WinX)
                ld      b,a
                ld      a,c
                sub     b
                jr      c,RszWmin
                inc     a                       ; the width that reaches it
                cp      RSZMINW
                jr      nc,RszWclamp
RszWmin:
                ld      a,RSZMINW
RszWclamp:
                ld      (RszWantW),a
                ld      a,(WinX)
                neg
                add     a,SCRCOLS               ; the widest that still fits.
                                                ; A window close enough to the
                                                ; right edge gets less than
                                                ; the minimum, and running off
                                                ; the screen would be worse
                ld      hl,RszWantW
                cp      (hl)
                jr      nc,RszHeight
                ld      (hl),a
RszHeight:
                ld      a,(PtrY)
                ld      c,a
                ld      a,(WinY)
                ld      b,a
                ld      a,c
                sub     b
                jr      c,RszHmin
                inc     a
                cp      RSZMINH
                jr      nc,RszHclamp
RszHmin:
                ld      a,RSZMINH
RszHclamp:
                ld      (RszWantH),a
                ld      a,(WinY)
                neg
                add     a,STATRULE              ; and the lowest it may reach
                ld      hl,RszWantH
                cp      (hl)
                jr      nc,RszWdone
                ld      (hl),a
RszWdone:
                ld      a,(RszWantW)
                ret

; ------------------------------------------------------------
;  RszEnd
;  The button came up. Now the window actually changes, once.
; ------------------------------------------------------------
RszEnd:
                ld      a,(Resizing)
                or      a
                ret     z
                xor     a
                ld      (Resizing),a
                call    RszShow                 ; the outline goes
                ld      a,(RszW)
                ld      (WinW),a
                ld      a,(RszH)
                ld      (WinH),a
                call    WndAllocBuf
                jr      c,RszNoRoom
                ; WndRepaintAll clears the desktop and composes every window
                ; from scratch, so the old rectangle needs no erase of its
                ; own and this window needs no WinDraw of its own. Both were
                ; written here first and both were doing the same work twice.
RszPaint:
                jp      WndRepaintAll
RszNoRoom:
                ; The heap could not carry the new size. WndAllocBuf gave
                ; the old buffer back before it asked, so the space for it
                ; is certainly there: putting the old size back and asking
                ; again cannot fail. A window left with no pixels at all
                ; would be a resize that loses the window.
                ld      a,(RszOldW)
                ld      (WinW),a
                ld      a,(RszOldH)
                ld      (WinH),a
                call    WndAllocBuf
                jr      RszPaint

; ------------------------------------------------------------
;  RszShow
;  XOR a one pixel outline of RszW by RszH at the window's
;  corner. Drawing it twice leaves the screen as it was, which is
;  why there is no save under here.
; ------------------------------------------------------------
RszShow:
                ld      a,(WinY)
                ld      (RszRow),a
                call    RszHLine                ; the top
                ld      a,(WinY)
                ld      c,a
                ld      a,(RszH)
                add     a,c
                dec     a
                ld      (RszRow),a
                call    RszHLine                ; and the bottom
                ; the sides, a bit at a time down the rows between them
                ld      a,(WinY)
                inc     a
                ld      (RszRow),a
                ld      a,(RszH)
                sub     2
                ret     c
                ret     z
                ld      (RszLeft),a
RszSide:
                ld      a,(WinX)
                ld      c,a
                ld      a,(RszRow)
                call    AddrAt
                ld      a,(hl)
                xor     $80                     ; the leftmost pixel of the
                ld      (hl),a                  ; window's first column
                ld      a,(WinX)
                ld      c,a
                ld      a,(RszW)
                add     a,c
                dec     a
                ld      c,a
                ld      a,(RszRow)
                call    AddrAt
                ld      a,(hl)
                xor     $01                     ; and the rightmost of its last
                ld      (hl),a
                ld      hl,RszRow
                inc     (hl)
                ld      hl,RszLeft
                dec     (hl)
                jr      nz,RszSide
                ret

; One row of the outline, RszW bytes at RszRow.
RszHLine:
                ld      a,(WinX)
                ld      c,a
                ld      a,(RszRow)
                call    AddrAt
                ld      a,(RszW)
                ld      b,a
RszHLoop:
                ld      a,(hl)
                cpl
                ld      (hl),a
                inc     hl
                djnz    RszHLoop
                ret

Resizing:       defb    0
RszW:           defb    0
RszH:           defb    0
RszOldW:        defb    0
RszOldH:        defb    0
RszWantW:       defb    0
RszWantH:       defb    0
RszRow:         defb    0
RszLeft:        defb    0
