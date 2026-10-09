; Window arrangements, ported from arrange.inc onto the cell grid.
; Cascade walks back to front from (1, WINTOP+1), two columns and
; one title row per window, clamped to the desktop. Z order is kept.
WndCascade:
                ld      a,(WndCount)
                or      a
                ret     z
                ld      (ArrI),a
ArcLoop:
                ld      hl,ArrI
                dec     (hl)
                call    ArrSelect
                ld      a,(ArrI)
                ld      b,a
                ld      a,(WndCount)
                dec     a
                sub     b
                ld      b,a
                add     a,a
                inc     a
                ld      c,a
                ld      a,(WinW)
                neg
                add     a,SCRCOLS
                cp      c
                jr      c,ArcX
                ld      a,c
ArcX:
                ld      (WinX),a
                ld      (WinOldX),a
                ld      a,b
                add     a,WINTOP+1
                ld      c,a
                ld      a,(WinH)
                neg
                add     a,WINBOTTOM
                cp      c
                jr      c,ArcY
                ld      a,c
ArcY:
                ld      (WinY),a
                ld      (WinOldY),a
                xor     a
                ld      (WinMoved),a
                ld      a,(ArrI)
                or      a
                jr      nz,ArcLoop
                jp      WndRepaintAll

; Tile front to back: whole, halves, left plus two right cells,
; or quarters. Allocate before releasing each old buffer: a refused
; allocation leaves that window's geometry and buffer intact. State
; travels through WndSelect, including each notepad's own document.
WndTile:
                ld      a,(WndCount)
                or      a
                ret     z
                dec     a
                ld      e,a
                ld      d,0
                ld      hl,ArrTileAt
                add     hl,de
                ld      e,(hl)
                ld      hl,ArrTiles
                add     hl,de
                ld      (ArrTab),hl
                xor     a
                ld      (ArrI),a
ArtLoop:
                call    ArrSelect
                ld      hl,(WinBufP)
                push    hl
                ld      hl,(WinW)               ; old width and height
                push    hl
                ld      hl,(ArrTab)
                inc     hl
                inc     hl
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ld      (WinW),de
                ld      hl,0
                ld      (WinBufP),hl
                call    WndAllocBuf
                pop     de
                pop     hl
                jr      nc,ArtAllocated
                ld      (WinW),de
                ld      (WinBufP),hl
                jr      ArtNext
ArtAllocated:
                call    HeapFree
                ld      hl,(ArrTab)
                ld      a,(hl)
                ld      (WinX),a
                ld      (WinOldX),a
                inc     hl
                ld      a,(hl)
                ld      (WinY),a
                ld      (WinOldY),a
                xor     a
                ld      (WinMoved),a
ArtNext:
                ld      hl,(ArrTab)
                ld      de,4
                add     hl,de
                ld      (ArrTab),hl
                ld      hl,ArrI
                inc     (hl)
                ld      a,(WndCount)
                cp      (hl)
                jr      nz,ArtLoop
                ld      a,(WndZ)
                call    WndSelect
                jp      WndRepaintAll

ArrSelect:
                ld      a,(ArrI)
                ld      e,a
                ld      d,0
                ld      hl,WndZ
                add     hl,de
                ld      a,(hl)
                jp      WndSelect

ARRDESKH        equ     WINBOTTOM-WINTOP
ARRHALFH        equ     ARRDESKH/2
ArrTileAt:      defb    0,4,12,24
ArrTiles:
                defb    0,WINTOP,32,ARRDESKH
                defb    0,WINTOP,16,ARRDESKH
                defb    16,WINTOP,16,ARRDESKH
                defb    0,WINTOP,16,ARRDESKH
                defb    16,WINTOP,16,ARRHALFH
                defb    16,WINTOP+ARRHALFH,16,ARRDESKH-ARRHALFH
                defb    0,WINTOP,16,ARRHALFH
                defb    0,WINTOP+ARRHALFH,16,ARRDESKH-ARRHALFH
                defb    16,WINTOP,16,ARRHALFH
                defb    16,WINTOP+ARRHALFH,16,ARRDESKH-ARRHALFH
