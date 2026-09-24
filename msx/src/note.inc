; ============================================================
;  Notepad
;  The ZX editor as it stands: sixteen lines on a sixteen byte
;  stride, byte 14 a permanent zero so every row is a null
;  terminated string, the cursor, a view that scrolls, and the
;  document saved and loaded as one 256 byte file. What changed
;  is the painting: the visible rows go into the window buffer
;  through WinPrint and the cursor is the inverted bank, so the
;  pixel cursor, the one-line repaint and the clip are gone.
;
;  The per instance state is one record, contiguous, so a second
;  notepad is a second document.
; ============================================================
NOTECOLS        equ     14
NOTELAST        equ     NOTECOLS-1
NOTEROWS        equ     16
NOTELASTROW     equ     NOTEROWS-1
NOTESTRIDE      equ     16
NOTESIZE        equ     NOTEROWS*NOTESTRIDE     ; 256, one RAMCHUNK
NOTENAMEMAX     equ     12
NOTEW           equ     NOTECOLS+2
NOTEH           equ     9               ; seven lines showing
NOTEREFLOW      equ     2

; NoteState, NoteBuf, NoteName, NoteCX, NoteCY, NoteTop, NoteEdited,
; NoteDirty and NoteModified are contiguous in the var block;
; NOTESTSZ is their total.

; A = row. Returns HL = its address in the document.
NoteRowAddr:
                ld      l,a
                ld      h,0
                add     hl,hl
                add     hl,hl
                add     hl,hl
                add     hl,hl
                ld      de,NoteBuf
                add     hl,de
                ret

NoteCurAddr:
                ld      a,(NoteCY)
                jp      NoteRowAddr

; How many document rows the window shows.
NoteVisRows:
                ld      a,(WinH)
                sub     2
                jr      z,NvrOne
                jr      c,NvrOne
                cp      NOTEROWS
                ret     c
                ld      a,NOTEROWS
                ret
NvrOne:
                ld      a,1
                ret

; Keep the cursor in view. NZ if the view moved.
NoteScroll:
                ld      a,(NoteCY)
                ld      c,a
                ld      a,(NoteTop)
                cp      c
                jr      z,NsSame
                jr      c,NsBelowTop
                ld      a,c
                ld      (NoteTop),a
                or      1
                ret
NsBelowTop:
                call    NoteVisRows
                ld      b,a
                ld      a,(NoteTop)
                add     a,b
                dec     a
                cp      c
                jr      nc,NsSame
                ld      a,c
                sub     b
                inc     a
                ld      (NoteTop),a
                or      1
                ret
NsSame:
                xor     a
                ret

; The draw vector: the visible rows, then the cursor inverted.
NoteDraw:
                call    NoteScroll
                xor     a
                ld      (PrintInv),a
                call    NoteVisRows
                ld      b,a
                ld      a,(NoteTop)
                ld      (NoteRowIx),a
                ld      c,1                     ; the window row
NdRow:
                push    bc
                ld      a,(NoteRowIx)
                cp      NOTEROWS
                jr      nc,NdNoRow
                call    NoteRowAddr
                ld      b,c
                ld      c,1
                call    WinPrint
NdNoRow:
                ld      hl,NoteRowIx
                inc     (hl)
                pop     bc
                inc     c
                djnz    NdRow
                ld      a,(NoteCY)
                ld      b,a
                ld      a,(NoteTop)
                neg
                add     a,b
                inc     a
                ld      b,a                     ; the cursor's window row
                ld      a,(NoteCX)
                inc     a
                ld      c,a
                call    WinCell
                ld      a,(hl)
                or      INVBANK
                ld      (hl),a
                ret

NoteInit:
                call    StIdent
                ld      (NoteBackend),a
                ld      hl,TxtNoteName
                call    NoteSetName
NoteNew:
                xor     a
                ld      (NoteRowIx),a
                ld      b,NOTEROWS
NnRow:
                push    bc
                ld      a,(NoteRowIx)
                call    NoteBlankRow
                ld      a,(NoteRowIx)
                inc     a
                ld      (NoteRowIx),a
                pop     bc
                djnz    NnRow
                xor     a
                ld      (NoteCX),a
                ld      (NoteCY),a
                ld      (NoteTop),a
                ld      (NoteEdited),a
                ld      (NoteDirty),a
                ld      (NoteModified),a
                ret

; A = the character, at the cursor, shifting the rest of the line.
NoteInsert:
                ld      (NoteChar),a
                call    NoteCurAddr
                ld      (NoteLine),hl
                ld      a,NOTELAST
                ld      c,a
                ld      a,(NoteCX)
                cp      c
                jr      nc,NiPut
                ld      b,a
                ld      a,NOTELAST-1
                sub     b
                inc     a
                ld      c,a
                ld      b,0
                ld      hl,(NoteLine)
                ld      de,NOTELAST-1
                add     hl,de
                ld      d,h
                ld      e,l
                inc     de
                lddr
NiPut:
                ld      hl,(NoteLine)
                ld      a,(NoteCX)
                ld      e,a
                ld      d,0
                add     hl,de
                ld      a,(NoteChar)
                ld      (hl),a
                ld      a,1
                ld      (NoteEdited),a
                ld      (NoteModified),a
                ld      a,(NoteCX)
                inc     a
                cp      NOTECOLS
                jr      c,NiSetX
                ld      a,(NoteCY)
                inc     a
                cp      NOTEROWS
                jr      nc,NiEnd
                ld      (NoteCY),a
                xor     a
NiSetX:
                ld      (NoteCX),a
                ret
NiEnd:
                ld      a,NOTELAST
                ld      (NoteCX),a
                ret

NoteBlankRow:
                call    NoteRowAddr
                ld      b,NOTECOLS
                ld      a,' '
NbrLoop:
                ld      (hl),a
                inc     hl
                djnz    NbrLoop
                ld      (hl),0
                inc     hl
                ld      (hl),0
                ret

; A = row. Returns A = its length without trailing spaces.
NoteLineLen:
                call    NoteRowAddr
                ld      de,NOTELAST
                add     hl,de
                ld      b,NOTECOLS
NllLoop:
                ld      a,(hl)
                cp      ' '
                jr      nz,NllGot
                dec     hl
                djnz    NllLoop
                xor     a
                ret
NllGot:
                ld      a,b
                ret

; A = row: the rows from it down move down one, the last is lost.
NoteOpenRow:
                ld      (NoteRowIx),a
                ld      a,NOTELASTROW
                ld      (NoteRowDst),a
NorLoop:
                ld      a,(NoteRowDst)
                ld      hl,NoteRowIx
                cp      (hl)
                ret     z
                dec     a
                call    NoteRowAddr
                push    hl
                ld      a,(NoteRowDst)
                call    NoteRowAddr
                ex      de,hl
                pop     hl
                ld      bc,NOTESTRIDE
                ldir
                ld      a,(NoteRowDst)
                dec     a
                ld      (NoteRowDst),a
                jr      NorLoop

; A = row: removed, the rows below move up, the last is blanked.
NoteCloseRow:
                ld      (NoteRowDst),a
NcrLoop:
                ld      a,(NoteRowDst)
                cp      NOTELASTROW
                jr      nc,NcrBlank
                inc     a
                call    NoteRowAddr
                push    hl
                ld      a,(NoteRowDst)
                call    NoteRowAddr
                ex      de,hl
                pop     hl
                ld      bc,NOTESTRIDE
                ldir
                ld      a,(NoteRowDst)
                inc     a
                ld      (NoteRowDst),a
                jr      NcrLoop
NcrBlank:
                ld      a,NOTELASTROW
                jp      NoteBlankRow

; Backspace: the character before the cursor, or a join with the
; line above when it fits.
NoteBack:
                ld      a,(NoteCX)
                or      a
                jr      z,NbEdge
                dec     a
                ld      (NoteCX),a
                ld      c,a
                call    NoteCurAddr
                ld      e,c
                ld      d,0
                add     hl,de
                ld      d,h
                ld      e,l
                inc     hl
                ld      a,NOTELAST
                sub     c
                ld      c,a
                ld      b,0
                or      a
                jr      z,NbTail
                ldir
NbTail:
                call    NoteCurAddr
                ld      de,NOTELAST
                add     hl,de
                ld      (hl),' '
                ld      a,1
                ld      (NoteEdited),a
                ld      (NoteModified),a
                ret
NbEdge:
                ld      a,(NoteCY)
                or      a
                ret     z
                dec     a
                call    NoteLineLen
                ld      (NoteJoinAt),a
                ld      a,(NoteCY)
                call    NoteLineLen
                ld      c,a
                ld      a,(NoteJoinAt)
                add     a,c
                cp      NOTECOLS+1
                jr      nc,NbMoveOnly
                ld      a,c
                or      a
                jr      z,NbJoinEmpty
                ld      b,0
                ld      a,(NoteCY)
                call    NoteRowAddr
                push    hl
                ld      a,(NoteCY)
                dec     a
                call    NoteRowAddr
                ld      a,(NoteJoinAt)
                ld      e,a
                ld      d,0
                add     hl,de
                ex      de,hl
                pop     hl
                ldir
NbJoinEmpty:
                ld      a,(NoteCY)
                call    NoteCloseRow
                ld      hl,NoteCY
                dec     (hl)
                ld      a,(NoteJoinAt)
                ld      (NoteCX),a
                ld      a,NOTEREFLOW
                ld      (NoteEdited),a
                ld      a,1
                ld      (NoteModified),a
                ret
NbMoveOnly:
                ld      hl,NoteCY
                dec     (hl)
                ld      a,NOTELAST
                ld      (NoteCX),a
                ret

; ENTER: the line splits at the cursor.
NoteEnter:
                ld      a,(NoteCY)
                cp      NOTELASTROW
                ret     nc
                inc     a
                call    NoteOpenRow
                ld      a,(NoteCY)
                inc     a
                call    NoteBlankRow
                ld      a,NOTECOLS
                ld      c,a
                ld      a,(NoteCX)
                neg
                add     a,c
                ld      (NoteRowDst),a
                or      a
                jr      z,NeNoTail
                ld      c,a
                ld      b,0
                ld      a,(NoteCY)
                call    NoteRowAddr
                ld      a,(NoteCX)
                ld      e,a
                ld      d,0
                add     hl,de
                push    hl
                ld      a,(NoteCY)
                inc     a
                call    NoteRowAddr
                ex      de,hl
                pop     hl
                ldir
                ld      a,(NoteCY)
                call    NoteRowAddr
                ld      a,(NoteCX)
                ld      e,a
                ld      d,0
                add     hl,de
                ld      a,(NoteRowDst)
                ld      b,a
                ld      a,' '
NeBlank:
                ld      (hl),a
                inc     hl
                djnz    NeBlank
NeNoTail:
                ld      hl,NoteCY
                inc     (hl)
                xor     a
                ld      (NoteCX),a
                ld      a,NOTEREFLOW
                ld      (NoteEdited),a
                ld      a,1
                ld      (NoteModified),a
                ret

NoteLeft:
                ld      a,(NoteCX)
                or      a
                ret     z
                dec     a
                ld      (NoteCX),a
                ret
NoteRight:
                ld      a,(NoteCX)
                cp      NOTELAST
                ret     nc
                inc     a
                ld      (NoteCX),a
                ret
NoteUp:
                ld      a,(NoteCY)
                or      a
                ret     z
                dec     a
                ld      (NoteCY),a
                ret
NoteDown:
                ld      a,(NoteCY)
                cp      NOTELASTROW
                ret     nc
                inc     a
                ld      (NoteCY),a
                ret

; The key vector. Returns A = 1: every key it takes moves the cursor,
; and the cursor is painted, so the window is recomposed.
NoteKey:
                ld      (NoteChar),a
                xor     a
                ld      (NoteEdited),a
                ld      a,(NoteChar)
                cp      KEY_LEFT
                jr      nz,NkNotL
                call    NoteLeft
                jr      NkView
NkNotL:
                cp      KEY_RIGHT
                jr      nz,NkNotR
                call    NoteRight
                jr      NkView
NkNotR:
                cp      KEY_UP
                jr      nz,NkNotU
                call    NoteUp
                jr      NkView
NkNotU:
                cp      KEY_DOWN
                jr      nz,NkNotD
                call    NoteDown
                jr      NkView
NkNotD:
                cp      KEY_DELETE
                jr      nz,NkNotBs
                call    NoteBack
                jr      NkView
NkNotBs:
                cp      KEY_ENTER
                jr      nz,NkNotEnt
                call    NoteEnter
                jr      NkView
NkNotEnt:
                cp      ' '
                jr      c,NkNone                ; below space, not ours
                cp      128
                jr      nc,NkNone
                call    NoteInsert
NkView:
                call    NoteScroll
                ld      a,1
                ret
NkNone:
                xor     a
                ret

; ------------------------------------------------------------
;  The document as a file: one write and one read of NOTESIZE
;  through the storage layer, whichever backend is selected.
; ------------------------------------------------------------
NoteSaveAs:
                call    NoteSetName
NoteSave:
                call    NoteSelectBackend
                call    NoteSaveFile
                jp      NoteRestoreBackend
NoteSaveFile:
                ld      hl,NoteName
                ld      b,FA_OVERWRITE
                call    StOpen
                ret     c
                ld      (NoteHandle),a
                ld      hl,NoteBuf
                ld      bc,NOTESIZE
                call    StWrite
                ld      (NoteGot),bc
                ld      a,(NoteHandle)
                call    StClose
                ld      hl,(NoteGot)
                ld      de,NOTESIZE
                or      a
                sbc     hl,de
                jr      nz,NsShort
                xor     a
                ld      (NoteModified),a
                or      a
                ret
NsShort:
                scf
                ret

NoteLoadAs:
                call    NoteSetName
NoteLoad:
                call    NoteSelectBackend
                call    NoteLoadFile
                jp      NoteRestoreBackend
NoteLoadFile:
                ld      hl,NoteName
                ld      b,FA_READ
                call    StOpen
                jr      c,NlFail
                ld      (NoteHandle),a
                ld      hl,NoteBuf
                ld      bc,NOTESIZE
                call    StRead
                ld      (NoteGot),bc
                ld      a,(NoteHandle)
                call    StClose
                ld      hl,(NoteGot)
                ld      de,NOTESIZE
                or      a
                sbc     hl,de
                jr      nz,NlFail
                call    NoteSanitise
                xor     a
                ld      (NoteCX),a
                ld      (NoteCY),a
                ld      (NoteTop),a
                ld      (NoteModified),a
                ld      a,1
                ld      (NoteDirty),a
                or      a
                ret
NlFail:
                call    NoteNew
                scf
                ret

NoteSelectBackend:
                call    StIdent
                ld      (NotePrevBackend),a
                ld      a,(NoteBackend)
                jp      StSelect
NoteRestoreBackend:
                push    af
                ld      a,(NotePrevBackend)
                call    StSelect
                pop     af
                ret

; HL = a name, into NoteName, truncated to NOTENAMEMAX.
NoteSetName:
                ld      de,NoteName
                ld      b,NOTENAMEMAX
NsnLoop:
                ld      a,(hl)
                ld      (de),a
                or      a
                ret     z
                inc     hl
                inc     de
                djnz    NsnLoop
                xor     a
                ld      (de),a
                ret

; Whatever came off the file: anything unprintable becomes a space,
; and the terminators are put back.
NoteSanitise:
                ld      b,NOTEROWS
                xor     a
                ld      (NoteRowIx),a
NsanRow:
                push    bc
                ld      a,(NoteRowIx)
                call    NoteRowAddr
                ld      b,NOTECOLS
NsanCol:
                ld      a,(hl)
                cp      ' '
                jr      c,NsanBad
                cp      128
                jr      c,NsanOk
NsanBad:
                ld      (hl),' '
NsanOk:
                inc     hl
                djnz    NsanCol
                ld      (hl),0
                inc     hl
                ld      (hl),0
                ld      a,(NoteRowIx)
                inc     a
                ld      (NoteRowIx),a
                pop     bc
                djnz    NsanRow
                ret

; ---- The FILE menu
NoteMenuNew:
                ld      a,APP_NOTE
                jp      WndOpen

; Z if the front window is a notepad, which is the live one after
; the menu's press.
NoteIsFront:
                ld      hl,(WinApp)
                ld      de,AppNote
                or      a
                sbc     hl,de
                ret

NoteMenuSave:
                call    NoteIsFront
                ret     nz
                call    NoteSave
                ld      a,0
                jr      nc,NmsOk
                ld      a,1
NmsOk:
                ld      (NoteResult),a
                ret

; OPEN reads the front notepad's file back over the document. The
; Commander is available separately from VIEW > COMMANDER.
NoteMenuOpen:
                call    NoteIsFront
                ret     nz
                call    NoteLoad
                ld      a,0
                jr      nc,NmoOk
                ld      a,1
NmoOk:
                ld      (NoteResult),a
                ld      a,(WndCur)
                call    WndTouch
                jp      RowsAddLive

TxtNoteName:    defb    "NOTE",0
TxtWinTitle:    defb    "NOTEPAD",0
