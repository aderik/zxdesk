; ============================================================
;  Desktop shortcuts
;  The ZX's desktop.inc on the cell grid. An icon is sixteen
;  pixels square with a label under it, one bit, no bevel and no
;  shadow, as on the ZX; here that is a block of two by two tiles
;  and a row of text. The label is centred under the icon and may
;  be wider than it, so the slot, icon and label together, is
;  what the hit test and the repaint go by.
;
;  A press and release without movement opens it. A press and a
;  drag moves it, in whole cells, the way a title bar drag moves a
;  window. That distinction costs a comparison rather than a
;  double click timer, and a double click on a machine whose
;  owners mostly had no mouse would have been a poor thing to
;  depend on.
;
;  An entry carries a routine rather than an application index,
;  because not everything worth putting on a desktop is an
;  application: the commander opens its own way.
;
;  Icons lie under the windows. The ZX put them back strip by
;  strip as a drag vacated lattice (DskPaintIn, DskGrowClip);
;  here the repaint is already by row range, so DrawDesktopRows
;  paints the lattice and then every icon's rows within the
;  range, and the windows go on top as before. A row outside the
;  range is not touched: it was not marked dirty and a window may
;  be covering it.
;
;  The part a person changes, whether an icon is present and
;  where it sits, is three bytes an entry in RAM and lives inside
;  the SETTINGS record, so it is saved and loaded with the rest.
;  The label, the action and the tile are constants in ROM.
; ============================================================
DSKMAX          equ     6
DSKENTSZ        equ     3               ; present, x, y: in RAM, in SETTINGS
DSKPRESENT      equ     0
DSKX            equ     1               ; cell column of the icon's left tile
DSKY            equ     2               ; cell row of its top tile
DSKCONSZ        equ     6               ; label, action (words), tile, label length
DSKICONW        equ     2               ; cells
DSKSLOTH        equ     DSKICONW+1      ; two rows of tiles and the label row
DSKMOVE         equ     3               ; pixels of movement that make a drag
DSKXMAX         equ     SCRCOLS-DSKICONW
DSKYMAX         equ     WINBOTTOM-DSKSLOTH
DSKSHAPES       equ     4
T_ICON          equ     $88             ; four tiles a shape: $88-$97, the first
                                        ; free codes after the frame tiles
DSKTABSZ        equ     DSKMAX*DSKENTSZ

; ------------------------------------------------------------
;  The defaults: a column down the left, where GEM put its discs
;  and where nothing else on this desktop wants to be, and ABOUT
;  beside SETTINGS on the last row as SETUP sat beside ABOUT on
;  the ZX. DskDefault directly follows SetInitial, so SetDefaults
;  copies both with one LDIR; DskInit alone is what an older
;  SETTINGS file gets.
; ------------------------------------------------------------
DskInit:
                ld      hl,DskDefault
                ld      de,DskTab
                ld      bc,DSKTABSZ
                ldir
                ret

DskConst:
                defw    TxtDskNote, NoteMenuNew
                defb    T_ICON+0*4, TXTDSKNOTEL
                defw    TxtDskClock, ClockOpen
                defb    T_ICON+1*4, TXTDSKCLOCKL
                defw    TxtDskCal, WndOpenCal
                defb    T_ICON+2*4, TXTDSKCALL
                defw    TxtDskCmd, CommanderOpen
                defb    T_ICON+3*4, TXTDSKCMDL
                defw    TxtDskSet, SetOpen
                defb    T_ICON+0*4, TXTDSKSETL
                defw    TxtDskAbout, AboutOpen
                defb    T_ICON+0*4, TXTDSKABOUTL

TxtDskNote:     defb    "NOTEPAD",0
TXTDSKNOTEL     equ     $-TxtDskNote-1
TxtDskClock:    defb    "CLOCK",0
TXTDSKCLOCKL    equ     $-TxtDskClock-1
TxtDskCal:      defb    "CALENDAR",0
TXTDSKCALL      equ     $-TxtDskCal-1
TxtDskCmd:      defb    "COMMANDER",0
TXTDSKCMDL      equ     $-TxtDskCmd-1
TxtDskSet:      defb    "SETTINGS",0
TXTDSKSETL      equ     $-TxtDskSet-1
TxtDskAbout:    defb    "ABOUT",0
TXTDSKABOUTL    equ     $-TxtDskAbout-1

; ------------------------------------------------------------
;  DskEntry
;  in:  A = 0..DSKMAX-1
;  out: HL = its RAM entry, DE = its constants, DskIx = A
; ------------------------------------------------------------
DskEntry:
                ld      (DskIx),a
                ld      c,a
                add     a,a
                add     a,c                     ; three bytes an entry
                ld      c,a
                ld      b,0
                ld      hl,DskTab
                add     hl,bc
                ld      a,c
                add     a,a                     ; six bytes of constants
                ld      e,a
                ld      d,0
                push    hl
                ld      hl,DskConst
                add     hl,de
                ex      de,hl
                pop     hl
                ret

; ------------------------------------------------------------
;  DskSlot
;  in:  HL = a RAM entry, DE = its constants
;  out: DskPx, DskPy the icon; DskLx, DskLen the label, centred
;       under the icon as nearly as a cell allows and kept on the
;       screen; DskSx, DskSw the slot's columns, from the leftmost
;       of icon and label to the rightmost; DskLabel, DskBase.
; ------------------------------------------------------------
DskSlot:
                inc     hl
                ld      a,(hl)
                ld      (DskPx),a
                inc     hl
                ld      a,(hl)
                ld      (DskPy),a
                ex      de,hl
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ld      (DskLabel),de
                inc     hl
                inc     hl
                inc     hl
                ld      a,(hl)
                ld      (DskBase),a
                inc     hl
                ld      a,(hl)
                ld      (DskLen),a
                srl     a
                ld      c,a
                ld      a,(DskPx)
                inc     a                       ; the icon's centre column
                sub     c
                jr      nc,DsLeft
                xor     a
DsLeft:
                ld      c,a
                ld      a,(DskLen)
                neg
                add     a,SCRCOLS               ; the rightmost column it fits at
                cp      c
                jr      nc,DsFits
                ld      c,a
DsFits:
                ld      a,c
                ld      (DskLx),a
                ld      a,(DskPx)
                cp      c
                jr      c,DsSx
                ld      a,c
DsSx:
                ld      (DskSx),a
                ld      b,a
                ld      a,(DskLx)
                ld      c,a
                ld      a,(DskLen)
                add     a,c                     ; where the label ends
                ld      c,a
                ld      a,(DskPx)
                add     a,DSKICONW              ; and the icon
                cp      c
                jr      nc,DsEnd
                ld      a,c
DsEnd:
                sub     b
                ld      (DskSw),a
                ret

; ------------------------------------------------------------
;  DskPaintRows
;  Called at the end of DrawDesktopRows: the rows RowLo..RowHi-1
;  have just been filled with lattice and every present icon's
;  rows within the range go back, tiles and label.
;
;  This is on the drag path and on every window open, and the
;  calendar open had 0.8 ms to spare on the 60 Hz machine. The
;  first version, DskEntry and DskSlot for each icon and a
;  ShadowRow per row, measured 2.2 ms for three slots and dropped
;  that frame. So the tables are walked with pointers, an icon
;  with no row in the range is rejected on two compares, the
;  shadow address is worked out once a slot and stepped a row at
;  a time, and the label goes in with LDIR: measured 1.5 ms for
;  the same three slots (a Z80 instruction is seven cycles and a
;  frame at 60 Hz is 59,700 of them). The rest of the room came
;  from WndOpenLater: a window open composes in one frame and
;  repaints in the next.
; ------------------------------------------------------------
DskPaintRows:
                ld      hl,DskTab
                ld      de,DskConst
                ld      b,DSKMAX
DprLoop:
                ld      a,(hl)                  ; present?
                or      a
                jr      z,DprNext
                inc     hl
                inc     hl
                ld      a,(hl)                  ; its top row
                dec     hl
                dec     hl
                ld      c,a
                ld      a,(RowHi)
                cp      c
                jr      z,DprNext               ; the range ends above it
                jr      c,DprNext
                ld      a,c
                add     a,DSKSLOTH
                ld      c,a
                ld      a,(RowLo)
                cp      c
                jr      nc,DprNext              ; the range starts below it
                push    hl
                push    de
                push    bc
                call    DskPaintSlot
                pop     bc
                pop     de
                pop     hl
DprNext:
                inc     hl
                inc     hl
                inc     hl
                ld      a,e
                add     a,DSKCONSZ
                ld      e,a
                jr      nc,DprSame
                inc     d
DprSame:
                djnz    DprLoop
                ret

; HL = a present entry with a row in the range, DE = its constants.
; The two tile rows and the label row, each if it is in the range.
DskPaintSlot:
                inc     hl
                ld      a,(hl)
                ld      (DskPx),a
                inc     hl
                ld      b,(hl)                  ; B = the top row
                push    bc
                push    de
                call    ShadowRow               ; HL = its shadow row
                pop     de
                ex      de,hl                   ; HL = the constants, DE = the row
                ld      c,(hl)
                inc     hl
                ld      b,(hl)
                ld      (DskLabel),bc
                inc     hl
                inc     hl
                inc     hl
                ld      a,(hl)
                ld      (DskBase),a
                inc     hl
                ld      a,(hl)
                ld      (DskLen),a
                pop     bc
                ld      a,(DskBase)
                ld      c,a                     ; C = the row's first tile
                call    DskRowIn
                call    z,DskTileRow
                call    DskNextRow
                inc     c
                inc     c
                call    DskRowIn
                call    z,DskTileRow
                call    DskNextRow
                call    DskRowIn
                ret     nz
                ; the label, centred under the icon as nearly as a cell
                ; allows and kept on the screen
                ld      a,(DskLen)
                srl     a
                ld      c,a
                ld      a,(DskPx)
                inc     a                       ; the icon's centre column
                sub     c
                jr      nc,DpsLeft
                xor     a
DpsLeft:
                ld      c,a
                ld      a,(DskLen)
                neg
                add     a,SCRCOLS               ; the rightmost column it fits at
                cp      c
                jr      nc,DpsFits
                ld      c,a
DpsFits:
                ld      a,c
                add     a,e
                ld      e,a
                jr      nc,DpsNoCy
                inc     d
DpsNoCy:
                ld      hl,(DskLabel)
                ld      a,(DskLen)
                ld      c,a
                ld      b,0
                ldir
                ret

; B = a row. Z if it is within RowLo..RowHi-1. A only.
DskRowIn:
                ld      a,(RowLo)
                cp      b
                jr      z,DriLo
                jr      nc,DriOut
DriLo:
                ld      a,(RowHi)
                cp      b
                jr      z,DriOut
                jr      c,DriOut
                xor     a
                ret
DriOut:
                or      1
                ret

; DE = a shadow row, C = a tile: it and the next at the icon's column.
DskTileRow:
                push    de
                ld      a,(DskPx)
                add     a,e
                ld      e,a
                jr      nc,DtrNoCy
                inc     d
DtrNoCy:
                ld      a,c
                ld      (de),a
                inc     de
                inc     a
                ld      (de),a
                pop     de
                ret

; B the next row, DE the next shadow row.
DskNextRow:
                inc     b
                ld      a,e
                add     a,SCRCOLS
                ld      e,a
                ret     nc
                inc     d
                ret

; ------------------------------------------------------------
;  DskHit
;  in:  B = cell column, C = cell row
;  out: A = the icon's index plus one, or nought
;  The slot counts, label and all, because a person aiming at a
;  shortcut aims at the word as often as at the picture. The last
;  entry is tried first: it is painted last, so it is on top.
; ------------------------------------------------------------
DskHit:
                ld      a,b
                ld      (WhtCol),a              ; WndHitTest's cell, and it was
                ld      a,c                     ; given the same one
                ld      (WhtRow),a
                ld      a,DSKMAX-1
DhLoop:
                call    DskEntry
                ld      a,(hl)
                or      a
                jr      z,DhNext
                call    DskSlot
                ld      a,(WhtRow)
                ld      hl,DskPy
                sub     (hl)
                jr      c,DhNext
                cp      DSKSLOTH
                jr      nc,DhNext
                ld      a,(WhtCol)
                ld      hl,DskSx
                sub     (hl)
                jr      c,DhNext
                ld      hl,DskSw
                cp      (hl)
                jr      nc,DhNext
                ld      a,(DskIx)
                inc     a
                ret
DhNext:
                ld      a,(DskIx)
                sub     1
                jr      nc,DhLoop
                xor     a
                ret

; ------------------------------------------------------------
;  DskPress, DskDragMove, DskRelease
;  A press records where it landed and does nothing else. If the
;  pointer moves far enough the icon follows it, cell by cell,
;  keeping the offset of the press within the slot so it does not
;  snap its corner to the pointer; if it does not, the release
;  opens the shortcut. Which of the two happened is decided by
;  DSKMOVE pixels rather than by a clock.
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
                ld      a,(DskDrag)
                dec     a
                call    DskEntry
                inc     hl
                ld      a,(PtrX)
                rrca
                rrca
                rrca
                and     $1F
                sub     (hl)                    ; may be negative: the label
                ld      (DskOffX),a             ; reaches left of the icon
                inc     hl
                ld      a,(PtrY)
                rrca
                rrca
                rrca
                and     $1F
                sub     (hl)
                ld      (DskOffY),a
                ret

DskDragMove:
                ld      a,(DskDrag)
                or      a
                ret     z
                ld      a,(DskMoved)
                or      a
                jr      nz,DdmMove
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
DdmMove:
                ; the cell the icon belongs in for this pointer position,
                ; clamped to the desktop with room for the whole slot
                ld      a,(DskDrag)
                dec     a
                call    DskEntry
                inc     hl                      ; HL = its x
                push    hl
                ld      a,(PtrX)
                rrca
                rrca
                rrca
                and     $1F
                ld      hl,DskOffX
                sub     (hl)
                bit     7,a
                jr      z,DdmXpos
                xor     a
DdmXpos:
                cp      DSKXMAX
                jr      c,DdmXok
                ld      a,DSKXMAX
DdmXok:
                ld      c,a
                ld      a,(PtrY)
                rrca
                rrca
                rrca
                and     $1F
                ld      hl,DskOffY
                sub     (hl)
                bit     7,a
                jr      nz,DdmYtop
                cp      WINTOP
                jr      nc,DdmYpos
DdmYtop:
                ld      a,WINTOP
DdmYpos:
                cp      DSKYMAX
                jr      c,DdmYok
                ld      a,DSKYMAX
DdmYok:
                ld      b,a
                pop     hl
                ld      a,(hl)
                cp      c
                jr      nz,DdmChanged
                inc     hl
                ld      a,(hl)
                dec     hl
                cp      b
                ret     z                       ; the same cell: nothing to do
DdmChanged:
                ld      (hl),c
                inc     hl
                ld      a,(hl)                  ; the row it is leaving
                ld      (hl),b
                push    bc
                ld      b,a
                ld      c,DSKSLOTH
                call    RowsAdd                 ; the old slot's rows
                pop     bc
                ld      c,DSKSLOTH
                jp      RowsAdd                 ; and the new one's

; A, made positive.
DskAbs:
                bit     7,a
                ret     z
                neg
                ret

DskRelease:
                ld      a,(DskDrag)
                or      a
                ret     z
                dec     a
                call    DskEntry
                xor     a
                ld      (DskDrag),a
                ld      a,(DskMoved)
                or      a
                ret     nz                      ; dropped: it moved as it went
                ; a press and a release in the same place: open it, with the
                ; click a menu pick gives
                push    de
                ld      a,SND_CLICK
                call    SndPlay
                pop     hl
                inc     hl
                inc     hl
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ex      de,hl
                ld      a,h
                or      l
                ret     z
                jp      (hl)

; ------------------------------------------------------------
;  DskApply
;  The table as it came out of a SETTINGS file: anything present
;  is 1, and a position is clamped so the whole slot is on the
;  desktop, the same clamp a drag applies.
; ------------------------------------------------------------
DskApply:
                ld      hl,DskTab
                ld      b,DSKMAX
DaLoop:
                ld      a,(hl)
                or      a
                jr      z,DaPresent
                ld      a,1
DaPresent:
                ld      (hl),a
                inc     hl
                ld      a,(hl)
                cp      DSKXMAX+1
                jr      c,DaX
                ld      a,DSKXMAX
DaX:
                ld      (hl),a
                inc     hl
                ld      a,(hl)
                cp      WINTOP
                jr      nc,DaYlo
                ld      a,WINTOP
DaYlo:
                cp      DSKYMAX+1
                jr      c,DaY
                ld      a,DSKYMAX
DaY:
                ld      (hl),a
                inc     hl
                djnz    DaLoop
                ret

; ------------------------------------------------------------
;  The shapes, the ZX's bitmaps as tiles: for each, the top left
;  tile, top right, bottom left, bottom right, eight rows each.
;  A one pixel frame and a mark inside it that says which is
;  which. No bevel, no shadow and no diagonal: one bit and a CRT,
;  and a dithered edge on a sixteen pixel icon shimmers.
;  LoadTiles copies them to T_ICON in all three thirds; the
;  colour table gives them the text colours.
; ------------------------------------------------------------
DskShapes:
                ; 0: a document, with lines written on it
                defb    $FF,$80,$80,$BF,$80,$80,$BF,$80
                defb    $FF,$01,$01,$FD,$01,$01,$FD,$01
                defb    $80,$BF,$80,$80,$BF,$80,$80,$FF
                defb    $01,$FD,$01,$01,$FD,$01,$01,$FF
                ; 1: a clock, at a quarter past three
                defb    $FF,$80,$80,$80,$81,$81,$81,$81
                defb    $FF,$01,$01,$01,$01,$01,$01,$01
                defb    $81,$80,$80,$80,$80,$80,$80,$FF
                defb    $F9,$01,$01,$01,$01,$01,$01,$FF
                ; 2: a calendar, with a filled head and a grid of days
                defb    $FF,$FF,$FF,$80,$91,$80,$80,$91
                defb    $FF,$FF,$FF,$01,$11,$01,$01,$11
                defb    $80,$80,$91,$80,$80,$80,$80,$FF
                defb    $01,$01,$11,$01,$01,$01,$01,$FF
                ; 3: a drawer, with a shelf and a handle
                defb    $FF,$80,$80,$80,$80,$80,$80,$FF
                defb    $FF,$01,$01,$01,$01,$01,$01,$FF
                defb    $80,$83,$80,$80,$80,$80,$80,$FF
                defb    $01,$C1,$01,$01,$01,$01,$01,$FF
DSKSHAPESEND:
