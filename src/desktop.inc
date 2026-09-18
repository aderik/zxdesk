; ============================================================
;  Desktop shortcuts
;  GEM put icons on the desktop and this is the same idea with
;  the same constraints taken seriously. An icon is sixteen
;  pixels square with a label under it, one bit, no bevel and no
;  shadow, which is the same rule the rest of the chrome follows.
;
;  A press and release without movement opens it. A press and a
;  drag moves it. That distinction is already in the system for
;  window title bars, so it costs a comparison rather than a
;  double click timer, and a double click on a machine whose
;  owners mostly had no mouse would have been a poor thing to
;  depend on.
;
;  An entry carries a routine rather than an application index,
;  because not everything worth putting on a desktop is an
;  application: the file panel is a panel. GEM's icons opened
;  windows and ran programs through the same table and for the
;  same reason.
;
;  This lives in the fast region although it is desktop chrome,
;  because DskPaintIn runs on the drag path: a window dragged
;  across an icon vacates a strip, the strip is filled with
;  lattice, and whatever was showing through it has to go back.
;  Windows already did that through WndPaintUnder and icons are
;  the same problem one layer lower.
; ============================================================
DSKMAX          equ     6
DSKENTSZ        equ     8
DSKPRESENT      equ     0
DSKX            equ     1               ; byte column
DSKY            equ     2               ; pixel row
DSKLABEL        equ     3               ; word
DSKACTION       equ     5               ; word
DSKBITMAP       equ     7               ; index into DskShapes

DSKICONW        equ     2               ; byte columns
DSKICONH        equ     16              ; pixel rows
DSKLABELY       equ     17
DSKSLOTH        equ     DSKLABELY+8     ; icon, a gap, and the label
; The label is centred on the icon and can be wider than it: CLOCK is
; five columns against the icon's two, so it reaches one column left
; and two right. A slot is therefore wider than a shape, and the
; damage test has to use the slot or a drag that clips the end of a
; label will not know it has to put the label back.
DSKSLOTL        equ     3               ; columns the slot reaches left
DSKSLOTW        equ     8               ; and its total width
DSKMOVE         equ     3               ; pixels of movement that mean a drag
                                        ; rather than a press

; ------------------------------------------------------------
;  DskInit
;  The shortcuts a fresh machine has. Positions are a column
;  down the left, which is where GEM put its discs and where
;  nothing else on this desktop wants to be.
; ------------------------------------------------------------
DskInit:
                ld      hl,DskDefault
                ld      de,DskTab
                ld      bc,DSKMAX*DSKENTSZ
                ldir
                ret

DskDefault:
                defb    1, 1, 16
                defw    TxtDskNote, NoteMenuNew
                defb    0
                defb    1, 1, 48
                defw    TxtDskClock, ClockOpen
                defb    1
                defb    1, 1, 80
                defw    TxtDskCal, WndOpenCal
                defb    2
                defb    1, 1, 112
                defw    TxtDskFiles, CmdOpenWin
                defb    3
                defb    0, 1, 144
                defw    TxtDskAbout, AboutOpen
                defb    0
                defb    0, 4, 144
                defw    TxtDskSet, DlgOpen
                defb    0

TxtDskNote:     defb    "NOTE",0
TxtDskClock:    defb    "CLOCK",0
TxtDskCal:      defb    "CAL",0
TxtDskFiles:    defb    "FILES",0
TxtDskAbout:    defb    "ABOUT",0
TxtDskSet:      defb    "SETUP",0

; ------------------------------------------------------------
;  DskEntry
;  in:  A = 0..DSKMAX-1
;  out: HL = its entry
; ------------------------------------------------------------
DskEntry:
                ld      hl,DskTab
                or      a
                ret     z
                ld      b,a
                ld      de,DSKENTSZ
DskEntLoop:
                add     hl,de
                djnz    DskEntLoop
                ret

; ------------------------------------------------------------
;  DskPaintAll
;  Called at the end of DrawDesktop, so the icons are part of
;  what a repaint puts back rather than something painted over
;  it afterwards and forgotten about.
; ------------------------------------------------------------
DskPaintAll:
                xor     a
                ld      (DskIx),a
DpaLoop:
                ld      a,(DskIx)
                call    DskEntry
                ld      a,(hl)
                or      a
                call    nz,DskPaintOne
                ld      hl,DskIx
                inc     (hl)
                ld      a,(hl)
                cp      DSKMAX
                jr      c,DpaLoop
                ret

; HL = an entry that is present. Paints its shape and its label.
DskPaintOne:
                push    hl
                inc     hl
                ld      a,(hl)                  ; x
                ld      (DskPx),a
                inc     hl
                ld      a,(hl)                  ; y
                ld      (DskPy),a
                pop     hl
                push    hl
                ld      de,DSKBITMAP
                add     hl,de
                ld      a,(hl)
                call    DskShapeAt
                ld      a,(DskPx)
                ld      c,a
                ld      a,(DskPy)
                call    DskBlitShape
                pop     hl
                ; the label, centred under the icon as nearly as a byte
                ; column allows, and clamped to the left edge
                push    hl
                ld      de,DSKLABEL
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ex      de,hl                   ; HL = the label
                push    hl
                call    DskStrLen               ; A = its length
                srl     a
                ld      b,a
                ld      a,(DskPx)
                inc     a                       ; the icon's centre column
                sub     b
                jr      nc,DskLabX
                xor     a
DskLabX:
                ld      c,a
                ld      a,(DskPy)
                add     a,DSKLABELY
                ld      (PxRow),a
                pop     hl
                xor     a
                ld      (PrintInv),a
                call    PrintStrPx2
                pop     hl
                ret

; HL = a null terminated string. Returns A = its length.
DskStrLen:
                ld      b,0
DslLoop:
                ld      a,(hl)
                or      a
                jr      z,DslDone
                inc     hl
                inc     b
                jr      DslLoop
DslDone:
                ld      a,b
                ret

; A = a shape index. Returns HL = its sixteen rows of two bytes.
DskShapeAt:
                ld      l,a
                ld      h,0
                add     hl,hl
                add     hl,hl
                add     hl,hl
                add     hl,hl
                add     hl,hl                   ; times 32
                ld      de,DskShapes
                add     hl,de
                ret

; HL = a shape, C = byte column, A = pixel row. Writes it.
DskBlitShape:
                ld      (DskShp),hl
                ld      (DskRow),a
                ld      a,c
                ld      (DskCol),a
                ld      b,DSKICONH
DbsRow:
                push    bc
                ld      a,(DskCol)
                ld      c,a
                ld      a,(DskRow)
                call    AddrAt
                ex      de,hl
                ld      hl,(DskShp)
                ld      a,(hl)
                inc     hl
                ld      c,(hl)
                inc     hl
                ld      (DskShp),hl
                ex      de,hl
                ld      (hl),a
                inc     hl
                ld      (hl),c
                ld      hl,DskRow
                inc     (hl)
                pop     bc
                djnz    DbsRow
                ret

; ------------------------------------------------------------
;  DskPaintIn
;  The strip in Cl* has just been filled with lattice. Any icon
;  overlapping it goes back.
;
;  It walks the table with a pointer rather than calling DskEntry
;  six times, and the difference is not cosmetic. DskEntry
;  multiplies by adding DSKENTSZ to a running total, so calling
;  it per entry is a loop inside a loop: the first version cost
;  1,216 T on every erase, which the bench reported as seventy
;  six fewer turns of its counting loop. This one is about 370 T,
;  and it is on the drag path, which is why it is worth the
;  fifteen bytes.
; ------------------------------------------------------------
DskPaintIn:
                ld      hl,DskTab
                ld      b,DSKMAX
DpiLoop:
                ld      a,(hl)
                or      a
                jr      z,DpiNext
                push    bc
                push    hl
                call    DskOverlaps
                pop     hl
                push    hl
                jr      nz,DpiClear
                call    DskPaintOne
                call    DskGrowClip
DpiClear:
                pop     hl
                pop     bc
DpiNext:
                ld      de,DSKENTSZ
                add     hl,de
                djnz    DpiLoop
                ret

; HL = an entry. Z set if its slot overlaps the rectangle in Cl*, and
; the slot's left column is left in DskSlotX either way.
DskOverlaps:
                inc     hl
                ld      a,(hl)                  ; the icon's left column
                sub     DSKSLOTL                ; the slot starts left of it
                jr      nc,DoLeft
                xor     a
DoLeft:
                ld      (DskSlotX),a
                ld      b,a
                inc     hl
                ld      c,(hl)                  ; and its top row
                ; right edge of the slot against the left of the strip
                ld      a,b
                add     a,DSKSLOTW
                ld      d,a
                ld      a,(ClX)
                cp      d
                jr      nc,DoNo                 ; the strip starts past it
                ld      e,a
                ld      a,(ClW)
                add     a,e
                cp      b
                jr      c,DoNo                  ; the strip ends before it
                jr      z,DoNo
                ld      a,c
                add     a,DSKSLOTH
                ld      d,a
                ld      a,(ClY)
                cp      d
                jr      nc,DoNo
                ld      e,a
                ld      a,(ClH)
                add     a,e
                cp      c
                jr      c,DoNo
                jr      z,DoNo
                xor     a                       ; Z: they overlap
                ret
DoNo:
                or      1
                ret

; ------------------------------------------------------------
;  DskGrowClip
;  An icon is painted whole, not clipped to the strip, so the part
;  of it that reaches outside lands on whatever was there, which
;  during a drag is a window. Rather than teach the shape and the
;  label to clip, the strip is grown to cover what was just
;  painted: WndPaintUnder runs next and puts the windows back over
;  all of it. That is the same order the desktop already uses, and
;  it is the reason CLOCK was appearing on top of the file
;  manager while CAL and FILES beside it were correctly behind.
;
;  On entry DskSlotX and DskPy hold the slot that was painted.
; ------------------------------------------------------------
DskGrowClip:
                ; The union, and the far edges are worked out before the
                ; near ones move: shifting ClX left without widening ClW
                ; would drag the right edge left with it and lose whatever
                ; was there.
                ld      a,(ClX)
                ld      b,a
                ld      a,(ClW)
                add     a,b
                ld      (DgcRight),a
                ld      a,(ClY)
                ld      b,a
                ld      a,(ClH)
                add     a,b
                ld      (DgcBot),a
                ld      a,(DskSlotX)
                ld      b,a
                ld      a,(ClX)
                cp      b
                jr      c,DgcL1
                ld      a,b
                ld      (ClX),a
DgcL1:
                ld      a,(DskSlotX)
                add     a,DSKSLOTW
                cp      SCRCOLS+1               ; the screen is thirty two
                jr      c,DgcRfit
                ld      a,SCRCOLS
DgcRfit:
                ld      b,a
                ld      a,(DgcRight)
                cp      b
                jr      nc,DgcR1
                ld      a,b
                ld      (DgcRight),a
DgcR1:
                ld      a,(ClX)
                neg
                ld      b,a
                ld      a,(DgcRight)
                add     a,b
                ld      (ClW),a
                ld      a,(DskPy)
                ld      b,a
                ld      a,(ClY)
                cp      b
                jr      c,DgcT1
                ld      a,b
                ld      (ClY),a
DgcT1:
                ld      a,(DskPy)
                add     a,DSKSLOTH
                cp      DESKTOP_BOT+1
                jr      c,DgcBfit
                ld      a,DESKTOP_BOT+1
DgcBfit:
                ld      b,a
                ld      a,(DgcBot)
                cp      b
                jr      nc,DgcB1
                ld      a,b
                ld      (DgcBot),a
DgcB1:
                ld      a,(ClY)
                neg
                ld      b,a
                ld      a,(DgcBot)
                add     a,b
                ld      (ClH),a
                ret

DgcRight:       defb    0
DgcBot:         defb    0

; ------------------------------------------------------------
;  DskHit
;  in:  B = byte column, C = pixel row
;  out: A = the icon's index plus one, or nought
;  The label counts as part of the icon, because a person aiming
;  at a shortcut aims at the word as often as at the picture.
; ------------------------------------------------------------
DskHit:
                ld      a,b
                ld      (DskHx),a
                ld      a,c
                ld      (DskHy),a
                xor     a
                ld      (DskIx),a
DhLoop:
                ld      a,(DskIx)
                call    DskEntry
                ld      a,(hl)
                or      a
                jr      z,DhNext
                inc     hl
                ld      a,(hl)                  ; x
                ld      b,a
                inc     hl
                ld      c,(hl)                  ; y
                ld      a,(DskHx)
                sub     b
                jr      c,DhNext
                cp      DSKICONW
                jr      nc,DhNext
                ld      a,(DskHy)
                sub     c
                jr      c,DhNext
                cp      DSKSLOTH
                jr      nc,DhNext
                ld      a,(DskIx)
                inc     a
                ret
DhNext:
                ld      hl,DskIx
                inc     (hl)
                ld      a,(hl)
                cp      DSKMAX
                jr      c,DhLoop
                xor     a
                ret

; ------------------------------------------------------------
;  DskPress, DskDrag, DskRelease
;  A press records where it landed and does nothing else. If the
;  pointer moves far enough the icon follows it as a ghost; if it
;  does not, the release opens the shortcut. Which of the two
;  happened is decided by DSKMOVE pixels rather than by a clock.
; ------------------------------------------------------------
; A = the icon's index plus one, from DskHit.
DskPress:
                ld      (DskDrag),a
                ld      a,(PtrX)
                ld      (DskPressX),a
                ld      a,(PtrY)
                ld      (DskPressY),a
                xor     a
                ld      (DskMoved),a
                ret

DskDragMove:
                ld      a,(DskDrag)
                or      a
                ret     z
                ld      a,(DskMoved)
                or      a
                jr      nz,DdmGhost
                ; not a drag yet: has it moved far enough to be one?
                ld      a,(PtrX)
                ld      b,a
                ld      a,(DskPressX)
                sub     b
                call    DskAbs
                cp      DSKMOVE
                jr      nc,DdmStart
                ld      a,(PtrY)
                ld      b,a
                ld      a,(DskPressY)
                sub     b
                call    DskAbs
                cp      DSKMOVE
                ret     c
DdmStart:
                ld      a,1
                ld      (DskMoved),a
                call    DskGhostPos             ; where it should be now, not
                jp      DskGhostAt              ; whatever DskGx last held,
                                                ; which was a flash of icon in
                                                ; the top left corner
DdmGhost:
                call    DskGhostAt              ; rub the old one out
                call    DskGhostPos             ; work out the new place
                jp      DskGhostAt              ; and draw it there

; A, made positive.
DskAbs:
                bit     7,a
                ret     z
                neg
                ret

; Where the ghost should be for the pointer's current position,
; clamped to the desktop.
DskGhostPos:
                ld      a,(PtrX)
                srl     a
                srl     a
                srl     a
                cp      SCRCOLS-DSKICONW
                jr      c,DgpX
                ld      a,SCRCOLS-DSKICONW
DgpX:
                ld      (DskGx),a
                ld      a,(PtrY)
                cp      DESKTOP_TOP
                jr      nc,DgpYlo
                ld      a,DESKTOP_TOP
DgpYlo:
                cp      DESKTOP_BOT-DSKSLOTH
                jr      c,DgpY
                ld      a,DESKTOP_BOT-DSKSLOTH
DgpY:
                ld      (DskGy),a
                ret

; The dragged icon's shape, XORed at DskGx, DskGy. Drawing it twice
; leaves the screen as it was, which is why there is nothing to save.
DskGhostAt:
                ld      a,(DskDrag)
                dec     a
                call    DskEntry
                ld      de,DSKBITMAP
                add     hl,de
                ld      a,(hl)
                call    DskShapeAt
                ld      (DskShp),hl
                ld      a,(DskGy)
                ld      (DskRow),a
                ld      b,DSKICONH
DgaRow:
                push    bc
                ld      a,(DskGx)
                ld      c,a
                ld      a,(DskRow)
                call    AddrAt
                ex      de,hl
                ld      hl,(DskShp)
                ld      a,(hl)
                inc     hl
                ld      c,(hl)
                inc     hl
                ld      (DskShp),hl
                ex      de,hl
                xor     (hl)
                ld      (hl),a
                inc     hl
                ld      a,c
                xor     (hl)
                ld      (hl),a
                ld      hl,DskRow
                inc     (hl)
                pop     bc
                djnz    DgaRow
                ret

DskRelease:
                ld      a,(DskDrag)
                or      a
                ret     z
                ; The index goes to memory rather than to B, and DskDrag is
                ; not cleared yet. It was, and both were wrong for the same
                ; reason: DskGhostAt reads DskDrag to find the shape and
                ; uses B as its row counter, so clearing one and holding
                ; the other in the other meant a drop erased a ghost
                ; belonging to entry 255 and then moved nothing.
                ld      (DskWas),a
                ld      a,(DskMoved)
                or      a
                jr      nz,DskDropped
                xor     a
                ld      (DskDrag),a
                ; a press and a release in the same place: open it
                ld      a,(DskWas)
                dec     a
                call    DskEntry
                ld      de,DSKACTION
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ex      de,hl
                ld      a,h
                or      l
                ret     z
                jp      (hl)
DskDropped:
                call    DskGhostAt              ; the ghost goes, and it still
                                                ; needs DskDrag to know which
                xor     a
                ld      (DskDrag),a
                ld      a,(DskWas)
                dec     a
                call    DskEntry
                inc     hl
                ld      a,(DskGx)
                ld      (hl),a
                inc     hl
                ld      a,(DskGy)
                ld      (hl),a
                jp      WndRepaintAll           ; the icon was somewhere else
                                                ; a moment ago and that place
                                                ; is still showing it

; ------------------------------------------------------------
;  DskSave, DskLoad
;  Which shortcuts are on the desktop and where. Three bytes an
;  entry, through the same storage layer as the settings,
;  because a desktop a person has arranged and that forgets
;  itself is not a desktop.
; ------------------------------------------------------------
DskSave:
                call    DskPack
                ld      hl,TxtDskFile
                ld      b,FA_OVERWRITE
                call    StOpen
                ret     c
                ld      (DskHandle),a
                ld      hl,DskPacked
                ld      bc,DSKMAX*3
                call    StWrite
                ld      a,(DskHandle)
                jp      StClose

DskLoad:
                ld      hl,TxtDskFile
                ld      b,FA_READ
                call    StOpen
                ret     c                       ; never saved, so the defaults
                                                ; stand and that is not a fault
                ld      (DskHandle),a
                ld      hl,DskPacked
                ld      bc,DSKMAX*3
                call    StRead
                ld      a,(DskHandle)
                call    StClose
                ; fall through and unpack

DskUnpack:
                ld      hl,DskPacked
                xor     a
                ld      (DskIx),a
DuLoop:
                push    hl
                ld      a,(DskIx)
                call    DskEntry
                pop     de
                ld      a,(de)
                ld      (hl),a                  ; present
                inc     de
                inc     hl
                ld      a,(de)
                ld      (hl),a                  ; x
                inc     de
                inc     hl
                ld      a,(de)
                ld      (hl),a                  ; y
                inc     de
                push    de
                ld      hl,DskIx
                inc     (hl)
                ld      a,(hl)
                pop     hl
                cp      DSKMAX
                jr      c,DuLoop
                ret

DskPack:
                ld      de,DskPacked
                xor     a
                ld      (DskIx),a
DpkLoop:
                push    de
                ld      a,(DskIx)
                call    DskEntry
                pop     de
                ld      a,(hl)
                ld      (de),a
                inc     hl
                inc     de
                ld      a,(hl)
                ld      (de),a
                inc     hl
                inc     de
                ld      a,(hl)
                ld      (de),a
                inc     de
                push    de
                ld      hl,DskIx
                inc     (hl)
                ld      a,(hl)
                pop     de
                cp      DSKMAX
                jr      c,DpkLoop
                ret

TxtDskFile:     defb    "DESKTOP",0

; ------------------------------------------------------------
;  The shapes
;  Sixteen rows of sixteen pixels, a one pixel frame and a mark
;  inside it that says which is which. No bevel, no shadow and
;  no diagonal: this display has one bit and a CRT, and a
;  dithered edge on a sixteen pixel icon shimmers.
; ------------------------------------------------------------
DskShapes:
                ; Bytes rather than words, because DEFW is little endian
                ; and the left half of an icon has to be the first byte in
                ; memory. The side pixels are bit 7 of the left byte and
                ; bit 0 of the right one.
                ; 0: a document, with lines written on it
                defb    $FF,$FF, $80,$01, $80,$01, $BF,$FD
                defb    $80,$01, $80,$01, $BF,$FD, $80,$01
                defb    $80,$01, $BF,$FD, $80,$01, $80,$01
                defb    $BF,$FD, $80,$01, $80,$01, $FF,$FF
                ; 1: a clock, at a quarter past three
                defb    $FF,$FF, $80,$01, $80,$01, $80,$01
                defb    $81,$01, $81,$01, $81,$01, $81,$01
                defb    $81,$F9, $80,$01, $80,$01, $80,$01
                defb    $80,$01, $80,$01, $80,$01, $FF,$FF
                ; 2: a calendar, with a filled head and a grid of days
                defb    $FF,$FF, $FF,$FF, $FF,$FF, $80,$01
                defb    $91,$11, $80,$01, $80,$01, $91,$11
                defb    $80,$01, $80,$01, $91,$11, $80,$01
                defb    $80,$01, $80,$01, $80,$01, $FF,$FF
                ; 3: a drawer, with a shelf and a handle
                defb    $FF,$FF, $80,$01, $80,$01, $80,$01
                defb    $80,$01, $80,$01, $80,$01, $FF,$FF
                defb    $80,$01, $83,$C1, $80,$01, $80,$01
                defb    $80,$01, $80,$01, $80,$01, $FF,$FF

DskTab:         defs    DSKMAX*DSKENTSZ
DskPacked:      defs    DSKMAX*3
DskIx:          defb    0
DskPx:          defb    0
DskPy:          defb    0
DskRow:         defb    0
DskCol:         defb    0
DskShp:         defw    0
DskHx:          defb    0
DskHy:          defb    0
DskDrag:        defb    0               ; the icon being pressed, plus one
DskMoved:       defb    0
DskPressX:      defb    0
DskPressY:      defb    0
DskGx:          defb    0
DskGy:          defb    0
DskWas:         defb    0
DskSlotX:       defb    0
DskHandle:      defb    0
