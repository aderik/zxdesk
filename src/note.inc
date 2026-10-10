; ============================================================
;  Notepad
;  Sixteen 14-character chunks in the existing 256-byte document.
;  Byte 14 terminates each chunk; byte 15 is 1 when the next chunk
;  continues the same logical line (0 in legacy documents). Window
;  width affects only clipping and horizontal scrolling, never text.
;  Cursor coordinates address chunks; NoteMeasure maps them to lines.
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

; NoteState, NoteBuf, NoteName, NoteCX, NoteCY, NoteTop, NoteEdited,
; Viewport, backend and selection follow the document fields in the
; contiguous per-instance state; NOTESTSZ is their total.

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

; Count logical rows and map the chunk cursor to a logical cell.
NoteMeasure:
                ; Valid documents contain only 0/1 links. Each link removes
                ; one hard row; this count needs no cursor work per chunk.
                ld      hl,NoteBuf+15
                ld      de,NOTESTRIDE
                ld      b,NOTEROWS
                ld      a,b
NmCount:
                sub     (hl)
                add     hl,de
                djnz    NmCount
                ld      (NoteRows),a
                ld      a,(NoteCY)
                ld      (NoteCursorY),a
                ld      b,a
                ld      c,0                     ; columns before the cursor chunk
                or      a
                jr      z,NmCursor
                ld      hl,NoteBuf+15
NmLoop:
                ld      a,(hl)
                or      a
                jr      nz,NmLink
                ld      c,0
                jr      NmNext
NmLink:
                ld      a,c
                add     a,NOTECOLS
                ld      c,a
                ld      a,(NoteCursorY)
                dec     a
                ld      (NoteCursorY),a
NmNext:
                add     hl,de
                djnz    NmLoop
NmCursor:
                ld      a,(NoteCX)
                add     a,c
                ld      (NoteCursorX),a
                ret

NoteVisRows:
                ld      a,(WinH)
                sub     2
                ld      b,a
                ld      a,(NoteRows)
                cp      b
                ret     c
                ld      a,b
                ret

; Follow the caret horizontally, including after a resize. Widening
; reveals earlier columns again; no document bytes are changed.
NoteHScroll:
                ld      a,(WinW)
                sub     3                       ; final visible caret column
                ld      b,a
                ld      a,(NoteCursorX)
                sub     b
                jr      nc,NhsSet
                xor     a
NhsSet:
                ld      (NoteLeftCol),a
                ret

NoteScroll:
                call    NoteMeasure
                call    NoteHScroll
                ld      a,(NoteCursorY)
                ld      c,a
                ld      a,(NoteTop)
                cp      c
                jr      c,NsBelowTop
                ld      a,c
                ld      (NoteTop),a
                ret
NsBelowTop:
                call    NoteVisRows
                ld      b,a
                ld      a,(NoteTop)
                add     a,b
                dec     a
                cp      c
                ret     nc
                ld      a,c
                sub     b
                inc     a
                ld      (NoteTop),a
                ret

; Walk chunks, concatenating continuation chunks on the same row.
; All text and caret writes go through the same interior clip.
NoteDraw:
                ld      a,(NoteTop)
                call    NoteScrollTo            ; also measures rows and caret
                call    NoteHScroll
                xor     a
                ld      (NoteDrawX),a
                ld      (NoteDrawY),a
                ld      hl,NoteBuf
                ld      b,NOTEROWS
NdRow:
                push    bc
                push    hl
                call    NotePaintChunk
                pop     hl
                ld      de,15
                add     hl,de
                ld      a,(hl)                  ; continuation flag
                inc     hl
                or      a
                jr      z,NdBreak
                ld      a,(NoteDrawX)
                add     a,NOTECOLS
                ld      (NoteDrawX),a
                jr      NdNext
NdBreak:
                xor     a
                ld      (NoteDrawX),a
                ld      a,(NoteDrawY)
                inc     a
                ld      (NoteDrawY),a
NdNext:
                pop     bc
                djnz    NdRow
                call    NoteSelectionBounds
                ret     nz
                ld      a,(NoteCursorX)
                ld      (NoteDrawX),a
                ld      a,(NoteCursorY)
                ld      (NoteDrawY),a
                call    NotePaintAddr
                ret     c
                ld      a,(hl)
                or      INVBANK
                ld      (hl),a
                ret

NotePaintChunk:
                ld      a,(NoteTop)
                ld      c,a
                ld      a,(NoteDrawY)
                sub     c
                ret     c
                ld      b,a
                ld      a,(WinH)
                sub     2
                cp      b
                ret     z
                ret     c
                inc     b
                ld      a,(NoteLeftCol)
                ld      c,a
                ld      a,(NoteDrawX)
                sub     c
                jr      nc,NpcColumn
                neg
                cp      NOTECOLS
                ret     nc
                ld      e,a
                ld      d,0
                add     hl,de
                xor     a
NpcColumn:
                ld      c,a
                inc     c
                xor     a
                ld      (PrintInv),a
                push    bc
                call    NotePaintSelection
                pop     bc
                jp      WinPrint

NotePaintAddr:
                ld      a,(NoteTop)
                ld      c,a
                ld      a,(NoteDrawY)
                sub     c
                ret     c
                ld      b,a
                ld      a,(WinH)
                sub     2
                cp      b
                jr      z,NpaOutside
                jr      c,NpaOutside
                inc     b
                ld      a,(NoteLeftCol)
                ld      c,a
                ld      a,(NoteDrawX)
                sub     c
                ret     c
                ld      c,a
                ld      a,(WinW)
                sub     2
                cp      c
                jr      z,NpaOutside
                jr      c,NpaOutside
                inc     c
                call    WinCell
                or      a
                ret
NpaOutside:
                scf
                ret

NoteInit:
                call    StIdent
                ld      (NoteBackend),a
                ld      hl,TxtNoteName
                call    NoteSetName
NoteNew:
                call    NoteClearSelection
                ; Build one blank row, then spread it through the document.
                xor     a
                call    NoteBlankRow
                ld      hl,NoteBuf
                ld      de,NoteBuf+NOTESTRIDE
                ld      bc,NOTESIZE-NOTESTRIDE
                ldir
                xor     a
                ld      (NoteSaved),a
                ld      (NoteRowIx),a
                ld      (NoteCX),a
                ld      (NoteCY),a
                ld      (NoteTop),a
                ld      (NoteLeftCol),a
                ld      (NoteEdited),a
                ld      (NoteDirty),a
                ld      (NoteModified),a
                ret

; Append one continuation chunk after A. Refuse when the final
; chunk contains text: reaching capacity must not discard another line.
NoteExtend:
                ld      (NoteExtendRow),a
                call    NoteLinkAddr
                ld      a,(hl)
                ld      (NoteOldLink),a
                ld      a,(NoteExtendRow)
                cp      NOTELASTROW
                jr      c,NexCapacity
                scf
                ret
NexCapacity:
                ld      a,NOTELASTROW
                call    NoteLineLen
                or      a
                scf
                ret     nz
                ld      a,(NoteExtendRow)
                inc     a
                call    NoteOpenRow
                ld      a,(NoteExtendRow)
                inc     a
                call    NoteBlankRow
                ld      hl,NoteBuf+NOTESIZE-1
                ld      (hl),0                  ; never link beyond the buffer
                ld      a,(NoteExtendRow)
                call    NoteLinkAddr
                ld      (hl),1
                or      a
                ret

NoteLinkAddr:
                call    NoteRowAddr
                ld      de,15
                add     hl,de
                ret

; Insertion shifts the whole logical line, carrying between chunks.
; Allocate its tail before modifying text, so a full document is safe.
NoteInsert:
                ld      (NoteChar),a
                ld      a,(NoteCY)
                ld      (NoteRowIx),a
NiFindTail:
                call    NoteLinkAddr
                ld      a,(hl)
                or      a
                jr      z,NiTail
                ld      hl,NoteRowIx
                inc     (hl)
                ld      a,(hl)
                jr      NiFindTail
NiTail:
                dec     hl
                dec     hl
                ld      a,(hl)
                cp      ' '
                jr      nz,NiExtend
                ld      a,(NoteCX)
                cp      NOTECOLS
                jr      nz,NiReady
                ld      a,(NoteCY)
                ld      hl,NoteRowIx
                cp      (hl)
                jr      nz,NiReady
NiExtend:
                ld      a,(NoteRowIx)
                call    NoteExtend
                ret     c
NiReady:
                ld      a,(NoteCX)
                cp      NOTECOLS
                jr      nz,NiStart
                ld      hl,NoteCY
                inc     (hl)
                xor     a
                ld      (NoteCX),a
NiStart:
                call    NoteCurAddr
                ld      a,(NoteCX)
                ld      e,a
                ld      d,0
                add     hl,de
                ld      a,NOTECOLS
                sub     e
                ld      b,a
                ld      a,(NoteChar)
NiShift:
                ld      c,(hl)
                ld      (hl),a
                ld      a,c
                inc     hl
                djnz    NiShift
                inc     hl
                ld      c,(hl)
                inc     hl
                ld      b,NOTECOLS
                inc     c
                dec     c
                jr      nz,NiShift
                ld      hl,NoteCX
                inc     (hl)                    ; 14 is an end-of-chunk caret
                jp      NoteMarkEdited

NoteMarkEdited:
                ld      a,1
                ld      (NoteEdited),a
                ld      (NoteModified),a
                or      a                       ; carry clear: the edit went in
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

; Delete at the chunk cursor and pull the following chunks left.
NoteDeleteAt:
                call    NoteCurAddr
                ld      a,(NoteCX)
                ld      e,a
                ld      d,0
                add     hl,de
                ld      a,NOTECOLS
                sub     e
                ld      b,a
NdaChunk:
                dec     b
                jr      z,NdaLast
                ld      d,h
                ld      e,l
                inc     hl
                ld      a,(hl)
                ld      (de),a
                jr      NdaChunk
NdaLast:
                push    hl
                inc     hl
                inc     hl
                ld      a,(hl)
                inc     hl
                or      a
                jr      z,NdaBlank
                ld      a,(hl)
                pop     de
                ld      (de),a
                ld      b,NOTECOLS
                jr      NdaChunk
NdaBlank:
                pop     hl
                ld      (hl),' '
                ret

NoteBack:
                ld      a,(NoteCX)
                or      a
                jr      nz,NbDelete
                ld      a,(NoteCY)
                or      a
                ret     z
                dec     a
                call    NoteLinkAddr
                ld      a,(hl)
                or      a
                jr      z,NbHard
                ld      hl,NoteCY
                dec     (hl)
                ld      a,NOTECOLS
NbDelete:
                dec     a
                ld      (NoteCX),a
                call    NoteDeleteAt
                call    NoteTrimTail
                jp      NoteMarkEdited
NbHard:
                ; Join at a hard newline by linking its chunks. Keeping
                ; padding here would add spaces, so delete the unused tail.
                ld      a,(NoteCY)
                dec     a
                call    NoteLineLen
                ld      (NoteJoinAt),a
                ld      a,NOTECOLS
                ld      hl,NoteJoinAt
                sub     (hl)
                ld      (NoteDeleteCount),a
                ld      a,(NoteCY)
                dec     a
                ld      (NoteCY),a
                call    NoteLinkAddr
                ld      (hl),1
                ld      a,(NoteJoinAt)
                ld      (NoteCX),a
NbJoinLoop:
                ld      a,(NoteDeleteCount)
                or      a
                jp      z,NbJoined
                dec     a
                ld      (NoteDeleteCount),a
                call    NoteDeleteAt
                jr      NbJoinLoop
NbJoined:
                call    NoteTrimTail
                jp      NoteMarkEdited

; Reclaim empty continuation chunks after deleting/splitting. Hard
; blank lines remain intact. A caret on a reclaimed chunk becomes the
; end-of-chunk caret in its predecessor.
NoteTrimTail:
                ld      a,(NoteCY)
                ld      (NoteRowIx),a
NttFind:
                call    NoteLinkAddr
                ld      a,(hl)
                or      a
                jr      z,NttTail
                ld      hl,NoteRowIx
                inc     (hl)
                ld      a,(hl)
                jr      NttFind
NttTail:
                ld      a,(NoteRowIx)
                or      a
                ret     z
                dec     a
                call    NoteLinkAddr
                ld      a,(hl)
                or      a
                ret     z                       ; a hard line, keep it
                ld      a,(NoteRowIx)
                call    NoteLineLen
                or      a
                ret     nz
                ld      a,(NoteRowIx)
                call    NoteCloseRow
                ld      a,(NoteRowIx)
                ld      hl,NoteCY
                cp      (hl)
                jr      nz,NttUnlink
                dec     (hl)
                ld      a,NOTECOLS
                ld      (NoteCX),a
NttUnlink:
                ld      a,(NoteRowIx)
                dec     a
                call    NoteLinkAddr
                ld      (hl),0
                jr      NoteTrimTail

; Split a logical line. The remaining chunks retain their continuation
; marks; remove the prefix from the new line without dropping its tail.
NoteEnter:
                ld      a,(NoteCY)
                call    NoteExtend            ; also checks capacity
                ret     c
                ld      a,(NoteCY)
                call    NoteRowAddr
                push    hl
                ld      a,(NoteCY)
                inc     a
                call    NoteRowAddr
                ex      de,hl
                pop     hl
                ld      bc,NOTECOLS
                ldir
                ; NoteExtend inserted a blank chunk. Transfer the link
                ; that originally followed this chunk (saved below).
                ld      a,(NoteOldLink)
                inc     de
                ld      (de),a
                call    NoteCurAddr
                ld      a,(NoteCX)
                ld      (NoteDeleteCount),a
                ld      e,a
                ld      d,0
                add     hl,de
                ld      a,NOTECOLS
                sub     e
                jr      z,NeTerm
                ld      b,a
NeBlank:
                ld      (hl),' '
                inc     hl
                djnz    NeBlank
NeTerm:
                inc     hl
                ld      (hl),0
                ld      hl,NoteCY
                inc     (hl)
                xor     a
                ld      (NoteCX),a
NeDelete:
                ld      a,(NoteDeleteCount)
                or      a
                jp      z,NbJoined
                dec     a
                ld      (NoteDeleteCount),a
                call    NoteDeleteAt
                jr      NeDelete

NoteLeft:
                ld      a,(NoteCX)
                or      a
                jr      nz,NleftDec
                ld      a,(NoteCY)
                or      a
                ret     z
                dec     a
                call    NoteLinkAddr
                ld      a,(hl)
                or      a
                ret     z
                ld      hl,NoteCY
                dec     (hl)
                ld      a,NOTECOLS
NleftDec:
                dec     a
                ld      (NoteCX),a
                ret
NoteRight:
                ld      a,(NoteCX)
                cp      NOTELAST
                jr      c,NrightInc
                call    NoteCurLink
                ld      a,(hl)
                or      a
                jr      z,NrightEnd
                ld      hl,NoteCY
                inc     (hl)
                ld      a,(NoteCX)
                sub     NOTELAST
                ld      (NoteCX),a
                ret
NrightEnd:
                ld      a,(NoteCX)
                cp      NOTECOLS
                ret     nc
NrightInc:
                ld      hl,NoteCX
                inc     (hl)
                ret
NoteCurLink:
                ld      a,(NoteCY)
                jp      NoteLinkAddr

NoteUp:
                call    NoteMeasure
                ld      a,(NoteCursorY)
                or      a
                ret     z
                dec     a
                jr      NoteGotoLine
NoteDown:
                call    NoteMeasure
                ld      a,(NoteRows)
                ld      b,a
                ld      a,(NoteCursorY)
                inc     a
                cp      b
                ret     nc
NoteGotoLine:
                ld      c,a                     ; desired logical row
                ld      b,0                     ; physical chunk
NglRow:
                ld      a,c
                or      a
                jr      z,NglColumn
                ld      a,b
                push    bc
                call    NoteLinkAddr
                pop     bc
                ld      a,(hl)
                or      a
                jr      nz,NglNext
                dec     c
NglNext:
                inc     b
                jr      NglRow
NglColumn:
                ld      a,(NoteCursorX)
                ld      c,a
NglChunk:
                ld      a,c
                cp      NOTECOLS
                jr      c,NglSet
                ld      a,b
                push    bc
                call    NoteLinkAddr
                pop     bc
                ld      a,(hl)
                or      a
                ld      a,NOTECOLS
                jr      z,NglSet
                ld      a,c
                sub     NOTECOLS
                ld      c,a
                inc     b
                jr      NglChunk
NglSet:
                ld      (NoteCX),a
                ld      a,b
                ld      (NoteCY),a
                ret

; The key vector. Returns A = 1: every key it takes moves the cursor,
; and the cursor is painted, so the window is recomposed.
NoteKey:
                ld      (NoteChar),a
                xor     a
                ld      (NoteEdited),a
                ld      a,(NoteChar)
                cp      KEY_SELECT
                jp      z,NoteToggleSelection
                cp      KEY_ESC
                jr      nz,NkSelectionEdit
                call    NoteClearSelection
                jr      NkView
NkSelectionEdit:
                cp      KEY_DELETE
                jr      z,NkEraseSelection
                cp      KEY_ENTER
                jr      z,NkReplaceSelection
                cp      ' '
                jr      c,NkNavigation
                cp      128
                jr      nc,NkNavigation
NkReplaceSelection:
                push    af
                call    NoteEraseSelection
                pop     af
                jr      NkNavigation
NkEraseSelection:
                call    NoteEraseSelection
                jr      nz,NkView
                ld      a,KEY_DELETE
NkNavigation:
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
                ld      a,(NoteAnchor)
                bit     7,a
                jr      z,NkScroll
                ld      hl,(NoteCX)
                ld      (NoteEnd),hl
NkScroll:
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
                push    hl
                call    NoteSelectBackend
                pop     hl
                push    hl
                call    NoteSaveNamed
                pop     hl
                call    nc,NoteSetName
                jp      NoteRestoreBackend
NoteSave:
                call    NoteSelectBackend
                call    NoteSaveFile
                jp      NoteRestoreBackend
NoteSaveFile:
                ld      hl,NoteName
NoteSaveNamed:
                ld      b,FA_OVERWRITE
                call    StOpen
                ret     c
                ld      (NoteHandle),a
                ld      hl,NoteBuf
                ld      bc,NOTESIZE
                call    StWrite
                ld      (NoteGot),bc
                push    af                      ; keep write errors through CLOSE
                ld      a,(NoteHandle)
                call    StClose
                jr      c,NsCloseFail
                pop     af
                ret     c
                ld      hl,(NoteGot)
                ld      de,NOTESIZE
                or      a
                sbc     hl,de
                jr      nz,NsShort
                xor     a
                ld      (NoteModified),a
                inc     a
                ld      (NoteSaved),a
                or      a
                ret
NsCloseFail:
                pop     af
NsShort:
                scf
                ret

NoteLoad:
                ld      hl,NoteName
NoteLoadAs:
                push    hl                      ; commit the name only after success
                call    NoteLoadNamed
                pop     hl
                ret     c
                call    NoteSetName
                or      a
                ret
NoteLoadNamed:
                push    hl
                call    NoteSelectBackend
                pop     hl
                call    NoteLoadFile
                jp      NoteRestoreBackend
NoteLoadFile:
                ld      b,FA_READ
                call    StOpen
                jr      c,NlFail
                ld      (NoteHandle),a
                ld      hl,CmdBuf                ; shared scratch, before any modal alert
                ld      bc,NOTESIZE
                call    StRead
                ld      (NoteGot),bc
                push    af                      ; preserve read errors through close
                ld      a,(NoteHandle)
                call    StClose
                jr      c,NlCloseFail
                pop     af
                jr      c,NlFail
                ld      hl,(NoteGot)
                ld      de,NOTESIZE
                or      a
                sbc     hl,de
                jr      nz,NlFail
                ld      hl,CmdBuf
                ld      de,NoteBuf
                ld      bc,NOTESIZE
                ldir
                call    NoteSanitise
                call    NoteClearSelection
                xor     a
                ld      (NoteCX),a
                ld      (NoteCY),a
                ld      (NoteTop),a
                ld      (NoteModified),a
                ld      a,1
                ld      (NoteDirty),a
                ld      (NoteSaved),a
                or      a
                ret
NlCloseFail:
                pop     af
NlFail:
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
                ld      a,(NoteRowIx)
                cp      NOTELASTROW
                jr      z,NsanEnd
                ld      a,(hl)
                cp      1
                jr      z,NsanLink
NsanEnd:
                ld      (hl),0
NsanLink:
                ld      a,(NoteRowIx)
                inc     a
                ld      (NoteRowIx),a
                pop     bc
                djnz    NsanRow
                ret

NoteClose:
                ld      a,(NoteModified)
                or      a
                ret     z
                ld      hl,NoteName
                ld      de,TxtNoteLose
                ld      bc,NoteCloseAnswer
                call    DlgSave3
                scf
                ret
NoteCloseAnswer:
                or      a
                ret     z
                cp      2
                jr      nz,NcaGo
                ld      a,(NoteSaved)
                or      a
                jr      nz,NcaSave
                ld      a,1
                ld      (NoteClosePending),a
                jp      NoteAskName
NcaSave:
                call    NoteSave
                jr      c,NoteSaveAlert
NcaGo:
                ld      a,1
                ld      (WndForce),a
                jp      WndClose
NoteSaveAlert:
                ld      a,1
                ld      (NoteResult),a
                ld      hl,TxtNoteNoSave
                ld      de,0
                jp      DlgAlert
NoteLoadAlert:
                ld      hl,TxtNoteNoLoad
                ld      de,0
                jp      DlgAlert
TxtNoteNoLoad:  defb    "COULD NOT LOAD",0
TxtNoteLose:    defb    "UNSAVED WORK",0
TxtNoteNoSave:  defb    "COULD NOT SAVE",0

; ---- The FILE menu
NoteMenuNew:
                ld      a,APP_NOTE
                call    WndOpen
                ret     c
NoteComposeLater:
                ; Compose now, then restore the desktop and blit next frame,
                ; as for SETTINGS. Mouse selection uses the same frame split.
                call    WinDraw
                ld      a,(WndCur)
                ld      b,a
                inc     b
                ld      a,$80
NmnBit:
                rlca
                djnz    NmnBit
                cpl
                ld      hl,WndDirty
                and     (hl)
                ld      (hl),a
                ld      a,2
                ld      (RepaintDue),a
                or      a                       ; successful open
                ret

; Z if the front window is a notepad, which is the live one after
; the menu's press.
NoteIsFront:
                ld      a,(WndCount)
                or      a
                jr      nz,NifApp
                inc     a                       ; NZ: no live window
                ret
NifApp:
                ld      hl,(WinApp)
                ld      de,AppNote
                or      a
                sbc     hl,de
                ret

NoteMenuSave:
                call    NoteIsFront
                ret     nz
                ld      a,(NoteSaved)
                or      a
                jp      z,NoteMenuSaveAs
                call    NoteSave
                ld      a,0
                jr      nc,NmsOk
                ld      a,1
NmsOk:
                ld      (NoteResult),a
                or      a
                jp      nz,NoteSaveAlert
                ret

; Unsupported backends report their own error before opening a picker.
NoteMenuDelete:
                ld      hl,(StVecDelete)
                ld      de,StNoDelete
                or      a
                sbc     hl,de
                jr      nz,NmdFront
                call    StNoDelete
                jr      NmdError
NmdFront:
                ld      a,(WndCount)
                or      a
                jp      z,CmdBrowseOpen
                call    NoteIsFront
                jp      nz,CmdBrowseOpen
                ld      a,(NoteSaved)
                or      a
                jp      z,CmdBrowseOpen
                ; A full 8.3 name needs its own line in the 20-cell panel.
                ld      hl,NoteName
                ld      b,12
NmdLength:
                ld      a,(hl)
                or      a
                jr      z,NmdPrompt
                inc     hl
                djnz    NmdLength
                ld      hl,NoteName
                ld      de,TxtBrowseDelete
                jr      NmdDialog
NmdPrompt:
                ; The dialogue owns only the first 140 bytes of CmdBuf.
                ld      hl,TxtNoteDelete
                ld      de,CmdBuf+140
                ld      bc,7
                ldir
                ld      hl,NoteName
NmdName:
                ld      a,(hl)
                or      a
                jr      z,NmdQuestion
                ld      (de),a
                inc     hl
                inc     de
                jr      NmdName
NmdQuestion:
                ld      a,'?'
                ld      (de),a
                inc     de
                xor     a
                ld      (de),a
                ld      hl,CmdBuf+140
                ld      de,0
NmdDialog:
                ld      bc,NoteDeleteAnswer
                jp      DlgDelete2
NoteDeleteAnswer:
                or      a
                ret     z
                ld      hl,NoteName
                call    StDelete
                jr      c,NmdError
                xor     a
                ld      (NoteSaved),a
                inc     a
                ld      (NoteModified),a
                ld      (NoteEdited),a
                ret
NmdError:
                cp      STERR_NOSUP
                ld      hl,TxtNoteNoDelete
                jr      nz,NmdAlert
                ld      hl,TxtNoteUnsupported
NmdAlert:
                ld      de,0
                jp      DlgAlert
TxtNoteDelete:  defb    "DELETE "
TxtNoteNoDelete: defb   "COULD NOT DELETE",0
TxtNoteUnsupported: defb "NOT SUPPORTED",0

; Tape has no directory; retain its sequential load operation.
NoteMenuOpen:
                call    StIdent
                cp      ST_TAPE
                jp      nz,CmdBrowseOpen
                call    NoteIsFront
                ret     nz
                call    NoteLoad
                ld      a,0
                jr      nc,NmoOk
                ld      a,1
NmoOk:
                ld      (NoteResult),a
                or      a
                jp      nz,NoteLoadAlert
                ld      a,(WndCur)
                call    WndTouch
                jp      RowsAddLive

TxtNoteName:    defb    "NOTE",0
TxtWinTitle:    defb    "NOTEPAD",0

; ---- The EDIT menu: CUT, COPY and PASTE (GRAPH+E, Y, P) through one
; clipboard shared by every notepad, ClipBuf and ClipLen. The text is
; the selection's cells with a CLIP_CR for every hard line break it
; crosses; a line-ending chunk loses its padding, a continued chunk
; is text through to its 14th cell. Each action wants the front
; window to be a notepad and does nothing otherwise, like the menu
; items that have nothing to act on.
CLIP_CR         equ     13
CLIPSIZE        equ     NOTEROWS*NOTECOLS       ; 224, the document budget

NoteMenuCopy:
                call    NoteIsFront
                ret     nz
                jp      NoteClipCopy

NoteMenuCut:
                call    NoteIsFront
                ret     nz
                call    NoteClipCopy
                ret     c                       ; nothing copied, nothing cut
                call    NoteEraseSelection
                jp      NoteRefresh

; The clipboard goes in at the caret, through NoteInsert and NoteEnter
; as if typed, over a selection if there is one. Everything that could
; change is saved first, and a byte that does not fit puts it all back
; and says so: no half pasted text.
NoteMenuPaste:
                call    NoteIsFront
                ret     nz
                ld      a,(ClipLen)
                or      a
                ret     z
                ld      hl,NoteBuf
                ld      de,CmdBuf               ; free: no dialogue is open
                ld      bc,NOTESIZE
                ldir
                ld      hl,NoteCX
                ld      de,CmdWork              ; 13 bytes, NOTECURSZ of them
                ld      bc,NOTECURSZ
                ldir
                call    NoteEraseSelection
                xor     a
                ld      (ClipIx),a
NpaLoop:
                ld      a,(ClipIx)
                ld      hl,ClipLen
                cp      (hl)
                jr      z,NoteRefresh
                ld      hl,ClipBuf
                ld      c,a
                ld      b,0
                add     hl,bc
                inc     a
                ld      (ClipIx),a
                ld      a,(hl)
                cp      CLIP_CR
                jr      z,NpaBreak
                call    NoteInsert
                jr      nc,NpaLoop
                jr      NpaFull
NpaBreak:
                call    NoteEnter
                jr      nc,NpaLoop
NpaFull:
                ld      hl,CmdBuf
                ld      de,NoteBuf
                ld      bc,NOTESIZE
                ldir
                ld      hl,CmdWork
                ld      de,NoteCX
                ld      bc,NOTECURSZ
                ldir
                ld      hl,TxtNoteFull
                ld      de,0
                jp      DlgAlert
TxtNoteFull:    defb    "DOCUMENT FULL",0

; The caret back in view, the buffer composed now and the desktop
; and blit next frame, the split mouse selection uses: a paste of
; HELLO measured 7.3 ms (1.8 of them the snapshot) before the 6 ms
; compose, and the repaint on top of that dropped a frame at 50 Hz.
NoteRefresh:
                call    NoteScroll
                call    RowsAddLive
                jp      NoteComposeLater

; A = a packed endpoint (NotePack). Returns A = its chunk row.
NoteUnpackRow:
                rrca
                rrca
                rrca
                rrca
                and     $0F
                ret

; The selection into the clipboard. It is built in CmdBuf, which
; holds the most a selection can give (224 cells and 15 breaks), and
; moved when it fits: carry, and the clipboard as it was, when it
; does not or when there is no selection.
NoteClipCopy:
                call    NoteSelectionBounds
                scf
                ret     z
                ld      a,c
                ld      (NoteChar),a            ; the exclusive upper endpoint
                call    NoteUnpackRow
                ld      (NoteExtendRow),a       ; and its row
                ld      a,b
                and     $0F
                ld      (NoteJoinAt),a          ; the first chunk's start column
                ld      a,b
                call    NoteUnpackRow
                ld      (NoteRowIx),a
                ld      de,CmdBuf
NccChunk:
                push    de                      ; the row routines take DE
                ld      a,(NoteRowIx)
                call    NoteLinkAddr
                ld      a,(hl)
                ld      (NoteOldLink),a
                ld      a,(NoteRowIx)
                ld      hl,NoteExtendRow
                cp      (hl)
                ld      a,NOTECOLS              ; a chunk before the last: all of it
                jr      c,NccEnd
                ld      a,(NoteChar)
                and     $0F                     ; the last: up to the endpoint
NccEnd:
                cp      NOTECOLS
                jr      nz,NccCopy
                ld      a,(NoteOldLink)
                or      a
                ld      a,NOTECOLS
                jr      nz,NccCopy              ; a continued chunk is all text
                ld      a,(NoteRowIx)
                call    NoteLineLen             ; a line's end, without its padding
NccCopy:
                ld      hl,NoteJoinAt
                sub     (hl)
                jr      c,NccSkip               ; the start is past the text
                jr      z,NccSkip
                ld      c,a
                ld      b,0
                ld      a,(NoteRowIx)
                call    NoteRowAddr
                ld      a,(NoteJoinAt)
                add     a,l
                ld      l,a
                jr      nc,NccFrom
                inc     h
NccFrom:
                pop     de
                ldir
                push    de
NccSkip:
                pop     de
NccBreak:
                xor     a
                ld      (NoteJoinAt),a          ; later chunks start at column 0
                ld      a,(NoteRowIx)
                ld      hl,NoteExtendRow
                cp      (hl)
                jr      nc,NccDone              ; the last chunk is in
                inc     a
                ld      (NoteRowIx),a
                ld      a,(NoteOldLink)
                or      a
                jr      nz,NccChunk             ; the line goes on in the next chunk
                ld      a,CLIP_CR
                ld      (de),a
                inc     de
                jr      NccChunk
NccDone:
                ld      hl,CmdBuf
                ex      de,hl
                or      a
                sbc     hl,de                   ; the length, at most 239
                ld      a,l
                cp      CLIPSIZE+1
                ccf
                ret     c                       ; more than the clipboard holds
                ld      (ClipLen),a
                or      a
                ret     z                       ; padding alone: an empty clipboard
                ld      c,a
                ld      b,0
                ld      de,ClipBuf
                ld      hl,CmdBuf
                ldir
                or      a
                ret

; Scroll model: count hard lines, not the chunks of a long line.
NoteScrollInfo:
                call    NoteMeasure
                call    NoteVisRows
                ld      c,a
                ld      a,(NoteRows)
                ld      b,a
                ld      a,(NoteTop)
                ret

; Moving the view does not move the caret. NoteKey brings it back
; into view on the next edit/navigation. NoteDraw uses the same clamp
; when a larger window exposes more of the document.
NoteScrollTo:
                push    af
                call    NoteMeasure
                call    NoteVisRows
                ld      b,a
                ld      a,(NoteRows)
                sub     b
                ld      b,a
                pop     af
                cp      b
                jr      c,NstSet
                ld      a,b
NstSet:
                ld      (NoteTop),a
                ret

; The modal callback keeps the live front document selected throughout.
NoteMenuSaveAs:
                call    NoteIsFront
                ret     nz
                xor     a
                ld      (NoteClosePending),a
NoteAskName:
                ld      hl,TxtSaveAs
                ld      de,NoteNameValid
                ld      bc,NoteNameAnswer
                jp      DlgInput
TxtSaveAs:      defb    "SAVE AS",0
NoteNameValid:
                ; Common safe filename characters; disk additionally limits
                ; the two components to 8 and 3. A trailing dot is a valid
                ; editing prefix, but is rejected on submission below.
                ld      b,0
                ld      c,0
NnvLoop:
                ld      a,(hl)
                or      a
                ret     z
                cp      '.'
                jr      z,NnvDot
                cp      '0'
                jr      c,NnvPunct
                cp      '9'+1
                jr      c,NnvChar
                cp      'A'
                jr      c,NnvPunct
                cp      'Z'+1
                jr      c,NnvChar
NnvPunct:
                cp      '_'
                jr      z,NnvChar
                cp      '-'
                jr      nz,NnvBad
NnvChar:
                inc     b
                ld      a,(NoteBackend)
                cp      ST_DISK
                jr      nz,NnvNext
                ld      a,c
                or      a
                ld      a,b
                jr      nz,NnvExt
                cp      9
                jr      nc,NnvBad
                jr      NnvNext
NnvExt:
                cp      4
                jr      nc,NnvBad
NnvNext:
                inc     hl
                jr      NnvLoop
NnvDot:
                ld      a,b
                or      a
                jr      z,NnvBad
                ld      a,c
                or      a
                jr      nz,NnvBad
                inc     c
                ld      b,0
                jr      NnvNext
NnvBad:
                scf
                ret
NoteNameAnswer:
                or      a
                ret     z
                ld      a,(DgLength)
                ld      e,a
                ld      d,0
                ld      hl,DgTextBuf-1
                add     hl,de
                ld      a,(hl)
                cp      '.'
                jr      nz,NnaCheck
                ld      hl,TxtSaveAs
                ld      de,0
                ld      a,4
                jp      DgOpen
NnaCheck:
                call    NoteSelectBackend
                ld      a,(StCapability)
                and     STCAP_DIR
                jr      z,NnaWrite
                ld      hl,DgTextBuf
                ld      b,FA_READ
                call    StOpen
                jr      c,NnaMissing
                call    StClose
                jr      c,NnaError
                call    NoteRestoreBackend
                ld      hl,DgTextBuf
                ld      de,0
                ld      bc,NoteOverwriteAnswer
                jp      DlgOverwrite
NnaMissing:
                cp      STERR_NOTFOUND
                jr      z,NnaWrite
NnaError:
                call    NoteRestoreBackend
                jp      NoteSaveAlert
NnaWrite:
                call    NoteRestoreBackend
                ld      a,1
NoteOverwriteAnswer:
                or      a
                ret     z
                ld      hl,DgTextBuf
                call    NoteSaveAs
                jp      c,NoteSaveAlert
                ld      a,(WndCur)
                call    WndTouch
                call    RowsAddLive
                xor     a
                ld      (NoteResult),a
                ld      a,(NoteClosePending)
                or      a
                jp      nz,NcaGo
                ret

; Four per-instance bytes. Endpoints are chunk cells; bit 7 of the
; anchor X enables keyboard marking, anchor Y=$FF means no selection.
NoteClearSelection:
                xor     a
                ld      (NoteAnchor),a
                ld      (NoteEnd),a
                ld      (NoteEnd+1),a
                dec     a
                ld      (NoteAnchor+1),a
                ret
NoteToggleSelection:
                ld      hl,NoteAnchor
                bit     7,(hl)
                jr      z,NselStart
                res     7,(hl)
                ld      a,1
                ret
NselStart:
                ld      hl,(NoteCX)
                ld      (NoteEnd),hl
                set     7,l
                ld      (NoteAnchor),hl
                ld      a,1
                ret

; Pack chunk Y and X into a sortable byte (terminators are skipped).
NotePack:
                ld      a,h
                rlca
                rlca
                rlca
                rlca
                ld      h,a
                ld      a,l
                and     $0F
                add     a,h
                ret
; NZ = nonempty, B = lower endpoint, C = exclusive upper endpoint.
NoteSelectionBounds:
                ld      hl,(NoteAnchor)
                ld      a,h
                cp      $FF
                jr      z,NselEmpty
                call    NotePack
                ld      b,a
                ld      hl,(NoteEnd)
                call    NotePack
                ld      c,a
                cp      b
                ret     z
                ret     nc
                ld      c,b
                ld      b,a
                or      1
                ret
NselEmpty:
                xor     a
                ret

; Prepare only the clipped chunk. WinPrint writes the inverse-bank codes
; to the window buffer, which the compositor copies through ShadowNT.
NotePaintSelection:
                push    hl
                ld      a,(WndZ)
                ld      hl,WndCur
                cp      (hl)
                jr      nz,NpsPlain
                call    NoteSelectionBounds
                jr      z,NpsPlain
                pop     hl
                push    hl
                ld      de,NoteBuf
                or      a
                sbc     hl,de
                ld      a,l
                cp      c
                jr      nc,NpsPlain
                or      $0F
                dec     a                       ; exclusive chunk text end
                cp      b
                jr      c,NpsPlain
                jr      z,NpsPlain
                cp      c
                jr      c,NpsWhole
                jr      z,NpsWhole
NpsPartial:
                ld      a,l
                pop     hl
                ld      de,NotePaintBuf
NpsLoop:
                push    af
                cp      b
                jr      c,NpsNormal
                cp      c
                jr      nc,NpsNormal
                ld      a,(hl)
                or      a
                jr      z,NpsEnd
                or      INVBANK
                jr      NpsPut
NpsNormal:
                ld      a,(hl)
                or      a
                jr      z,NpsEnd
NpsPut:
                ld      (de),a
                inc     de
                inc     hl
                pop     af
                inc     a
                jr      NpsLoop
NpsEnd:
                ld      (de),a
                pop     af
                ld      hl,NotePaintBuf
                ret
NpsWhole:
                ld      a,l
                cp      b
                jr      c,NpsPartial
                ld      a,INVBANK
                ld      (PrintInv),a
NpsPlain:
                pop     hl
                ret

; Delete backwards so the lower endpoint remains stable while chunks
; above it are reclaimed and hard line breaks are joined.
NoteEraseSelection:
                call    NoteSelectionBounds
                jr      nz,NesSelected
                call    NoteClearSelection
                xor     a
                ret
NesSelected:
                ld      a,b
                push    af
                ld      a,c
                and     $0F
                ld      (NoteCX),a
                ld      a,c
                rrca
                rrca
                rrca
                rrca
                and     $0F
                ld      (NoteCY),a
NesLoop:
                pop     af
                push    af
                ld      b,a
                ld      hl,(NoteCX)
                call    NotePack
                cp      b
                jr      c,NesDone
                jr      z,NesDone
                call    NoteBack
                jr      NesLoop
NesDone:
                pop     af
                call    NoteClearSelection
                ld      a,1
                or      a
                ret

NoteMouseStart:
                call    NoteClearSelection
                call    NoteMousePosition
                ld      hl,(NoteCX)
                ld      (NoteAnchor),hl
                ld      (NoteEnd),hl
                ld      a,1
                ld      (NoteMouse),a
                jr      NmouseTouch
NoteMouseFrame:
                ld      a,(NoteMouse)
                or      a
                ret     z
                ld      a,(RepaintDue)
                or      a
                ret     nz
                call    NoteIsFront
                ret     nz
                ld      hl,(NoteCX)
                push    hl
                call    NoteMousePosition
                pop     de
                ld      hl,(NoteCX)
                ld      (NoteEnd),hl
                or      a
                sbc     hl,de
                ret     z
NmouseTouch:
                call    NoteScroll
                call    RowsAddLive
                jp      NoteComposeLater

; Pointer cells are clamped to the viewport. Holding beyond the top or
; bottom advances one logical row each frame, even without new motion.
NoteMousePosition:
                call    NoteMeasure
                ld      a,(PtrX)
                rrca
                rrca
                rrca
                and     31
                ld      hl,WinX
                sub     (hl)
                sub     1
                jr      nc,NmpX
                xor     a
NmpX:
                ld      b,a
                ld      a,(WinW)
                sub     2
                cp      b
                jr      nc,NmpColumn
                ld      b,a
NmpColumn:
                ld      a,(NoteLeftCol)
                add     a,b
                ld      (NoteCursorX),a
                ld      a,(PtrY)
                rrca
                rrca
                rrca
                and     31
                ld      hl,WinY
                sub     (hl)
                sub     1
                jr      nc,NmpY
                ld      a,(NoteTop)
                or      a
                jr      z,NmpGoto
                dec     a
                jr      NmpGoto
NmpY:
                ld      b,a
                ld      a,(WinH)
                sub     2
                cp      b
                jr      nc,NmpRow
                ld      b,a
NmpRow:
                ld      a,(NoteTop)
                add     a,b
                ld      b,a
                ld      a,(NoteRows)
                dec     a
                cp      b
                jr      c,NmpGoto
                ld      a,b
NmpGoto:
                jp      NoteGotoLine
