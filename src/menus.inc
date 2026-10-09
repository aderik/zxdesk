; ============================================================
;  Menus
;  The ZX pull downs on the cell grid. A menu definition is six
;  bytes as before: the title's column and width on the bar, the
;  drop's width, the item count, and a pointer to the items,
;  null terminated strings laid end to end. The drop is one cell
;  row per item under the bar, black on white, with the title
;  shown inverted while it is open.
;
;  Save under is an LDIR out of the name table shadow: MnW cells
;  a row for MnH rows, at most MNSAVESZ bytes, and putting them
;  back is the same copy the other way.
; ============================================================
MNDEFSZ         equ     6
MNSAVESZ        equ     10*6            ; the widest drop by the tallest

MenuDefs:
                defb    1,8,10,2
                defw    MnItems1
                defb    10,6,8,6
                defw    MnItems2
                defb    17,6,10,6
                defw    MnItems3
                defb    24,6,8,2
                defw    MnItems4
MNCOUNT         equ     4

MnItems1:       defb    "ABOUT",0
                defb    "SETTINGS",0
MnItems2:       defb    "NEW",0
                defb    "OPEN",0
                defb    "SAVE",0
                defb    "SAVE AS",0
                defb    "PRINT",0
                defb    "DELETE",0
MnItems3:       defb    "CLOCK",0
                defb    "CALENDAR",0
                defb    "CASCADE",0
                defb    "TILE",0
                defb    "DESKTOP",0
                defb    "COMMANDER",0
MnItems4:       defb    "KEYS",0
                defb    "ABOUT",0

; Which title is under cell column B? Returns A = index+1, or zero.
MenuHitTitle:
                ld      hl,MenuDefs
                ld      c,1
MhtLoop:
                ld      a,c
                cp      MNCOUNT+1
                jr      nc,MhtNone
                ld      a,(hl)                  ; title column
                cp      b
                jr      z,MhtIn
                jr      nc,MhtNext
MhtIn:
                inc     hl
                add     a,(hl)                  ; + its width
                dec     hl
                cp      b
                jr      z,MhtNext
                jr      c,MhtNext
                ld      a,c
                ret
MhtNext:
                ld      de,MNDEFSZ
                add     hl,de
                inc     c
                jr      MhtLoop
MhtNone:
                xor     a
                ret

; Open menu A, one based. A repaint still pending from this frame
; goes first, or it would paint over the drop and the save-under
; would put back the wrong cells.
MenuOpenDrop:
                push    af
                call    WinRedraw
                pop     af
                ld      (MenuOpen),a
                dec     a
                ld      hl,MenuDefs
                or      a
                jr      z,MoGot
                ld      b,a
                ld      de,MNDEFSZ
MoSkip:
                add     hl,de
                djnz    MoSkip
MoGot:
                ld      a,(hl)                  ; title column
                ld      (MnX),a
                ld      (MnTitleCol),a
                inc     hl
                ld      a,(hl)                  ; title width
                ld      (MnTitleW),a
                inc     hl
                ld      a,(hl)                  ; drop width
                ld      (MnW),a
                inc     hl
                ld      a,(hl)                  ; item count, one row each
                ld      (MnCount),a
                ld      (MnH),a
                inc     hl
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ld      (MnItemPtr),de
                ld      a,MENUROW+1
                ld      (MnY),a
                ; nudge it left if it would run off the right edge
                ld      a,(MnX)
                ld      b,a
                ld      a,(MnW)
                add     a,b
                cp      SCRCOLS+1
                jr      c,MoFits
                ld      a,(MnW)
                neg
                add     a,SCRCOLS
                ld      (MnX),a
MoFits:
                call    MenuTitleInv
                call    SaveUnder
                jp      MenuPaint

MenuClose:
                ld      a,(MenuOpen)
                or      a
                ret     z
                xor     a
                ld      (MenuOpen),a
                call    RestoreUnder
                jp      MenuTitleInv            ; the same toggle puts it back

; The open menu's title on the bar, toggled between the banks.
MenuTitleInv:
                ld      a,(MnTitleCol)
                ld      c,a
                ld      b,MENUROW
                call    CellAddr
                ld      a,(MnTitleW)
                ld      b,a
MtiLoop:
                ld      a,(hl)
                xor     INVBANK
                ld      (hl),a
                inc     hl
                djnz    MtiLoop
                ret

; C = cell row. Returns A = the item index under it, or $FF.
MenuItemAt:
                ld      a,(MnY)
                ld      b,a
                ld      a,c
                sub     b
                jr      c,MiaNone
                ld      b,a
                ld      a,(MnCount)
                cp      b
                jr      z,MiaNone
                jr      c,MiaNone
                ld      a,b
                ret
MiaNone:
                ld      a,$FF
                ret

; The cells under the drop, out of the shadow into MnSave.
SaveUnder:
                ld      de,MnSave
                ld      a,(MnY)
                ld      b,a
SuRow:
                push    bc
                call    ShadowRow
                ld      a,(MnX)
                ld      c,a
                ld      b,0
                add     hl,bc
                ld      a,(MnW)
                ld      c,a
                ldir
                pop     bc
                inc     b
                ld      a,(MnH)
                add     a,MENUROW+1
                cp      b
                jr      nz,SuRow
                ret

; And back again, marking the rows. DE walks the shadow a row at a
; time rather than through CellAddr for each.
RestoreUnder:
                ld      a,(MnY)
                ld      b,a
                ld      a,(MnH)
                ld      c,a
                push    bc
                call    RowsMark                ; MnY for MnH rows
                pop     bc
                call    ShadowRow
                ld      a,(MnX)
                ld      c,a
                ld      b,0
                add     hl,bc
                ex      de,hl                   ; DE = the first cell
                ld      hl,MnSave
                ld      a,(MnH)
                ld      b,a
RuRow:
                push    bc
                ld      a,(MnW)
                ld      c,a
                ld      b,0
                ldir
                ld      a,(MnW)
                neg
                add     a,SCRCOLS
                ld      c,a
                ex      de,hl
                add     hl,bc
                ex      de,hl
                pop     bc
                djnz    RuRow
                ret

; B = first row, C = count: marked dirty.
RowsMark:
                ld      a,b
                push    bc
                call    MarkRow
                pop     bc
                inc     b
                dec     c
                jr      nz,RowsMark
                ret

; One row of spaces per item, the item's text one cell in.
MenuPaint:
                xor     a
                ld      (PrintInv),a
                ld      hl,(MnItemPtr)
                ld      a,(MnY)
                ld      b,a
MpItem:
                push    bc
                push    hl
                ld      a,(MnX)
                ld      c,a
                call    CellAddr
                ld      a,(MnW)
                ld      b,a
MpBlank:
                ld      (hl),' '
                inc     hl
                djnz    MpBlank
                pop     hl
                pop     bc
                push    bc
                push    hl
                ld      a,(MnX)
                inc     a
                ld      c,a
                call    PrintStr
                pop     hl
MpSkip:
                ld      a,(hl)
                inc     hl
                or      a
                jr      nz,MpSkip
                pop     bc
                inc     b
                ld      a,(MnH)
                add     a,MENUROW+1
                cp      b
                jr      nz,MpItem
                ret

; A = the item picked from MnLastMenu. The actions arrive with the
; things they open; until then a pick is recorded and nothing more.
MenuAction:
                push    af
                ld      a,SND_CLICK
                call    SndPlay
                pop     af
                ld      c,a
                ld      a,(MnLastMenu)
                ld      b,a
                ld      hl,MnActions
MnaLoop:
                ld      a,(hl)
                cp      $FF
                ret     z
                cp      b
                jr      nz,MnaNext
                inc     hl
                ld      a,(hl)
                cp      c
                jr      nz,MnaNext2
                inc     hl
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ex      de,hl
                jp      (hl)
MnaNext2:
                dec     hl
MnaNext:
                ld      de,4
                add     hl,de
                jr      MnaLoop

MnActions:
                defb    1,1
                defw    SetOpen
                defb    1,0
                defw    AboutOpen               ; MSX DESK, ABOUT
                defb    2,0
                defw    NoteMenuNew             ; FILE, NEW
                defb    2,1
                defw    NoteMenuOpen            ; FILE, OPEN
                defb    2,2
                defw    NoteMenuSave            ; FILE, SAVE
                defb    3,0
                defw    ClockOpen               ; VIEW, CLOCK
                defb    3,1
                defw    WndOpenCal              ; VIEW, CALENDAR
                defb    3,2
                defw    WndCascade
                defb    3,3
                defw    WndTile
                defb    3,5
                defw    CommanderOpen
                defb    4,1
                defw    AboutOpen               ; HELP, ABOUT
                defb    $FF
