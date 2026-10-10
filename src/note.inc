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
; NoteDirty, NoteSaved and NoteModified are contiguous in the var block;
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
                ld      a,(SetWrap)
                or      a
                jr      z,NsPlain
                ld      a,1                     ; wrapped: the draw lays the
                ld      (NoteFollow),a          ; rows out and follows the caret
                ret
NsPlain:
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
                ld      a,(SetWrap)
                or      a
                jp      nz,NoteDrawWrap
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
                ld      (NwBusy),a
                ld      (NoteLayOK),a
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

; A = row. Returns A = its length without trailing spaces. Two
; characters a step: a pair ORed is a space only when both are, as
; text is never below a space. The wrap layout asks this of every
; line it lays out, and a blank chunk is the common case.
NoteLineLen:
                call    NoteRowAddr
                ld      de,NOTELAST
                add     hl,de
                ld      b,NOTECOLS/2
NllPair:
                ld      a,(hl)
                dec     hl
                or      (hl)
                dec     hl
                cp      ' '
                jr      nz,NllGot
                djnz    NllPair
                xor     a
                ret
NllGot:
                inc     hl
                inc     hl
                ld      a,b
                add     a,a                     ; 2B: the upper's index and one
                ld      b,a
                ld      a,(hl)                  ; the upper of the pair
                cp      ' '
                ld      a,b
                ret     nz                      ; text there: 2B characters
                dec     a                       ; only the lower: one fewer
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
                ld      a,(SetWrap)
                or      a
                jp      nz,NoteUpWrap
                call    NoteMeasure
                ld      a,(NoteCursorY)
                or      a
                ret     z
                dec     a
                jr      NoteGotoLine
NoteDown:
                ld      a,(SetWrap)
                or      a
                jp      nz,NoteDownWrap
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

; HOME: the start of the logical line, or of the screen row with WRAP.
NoteHome:
                ld      a,(SetWrap)
                or      a
                jp      nz,NoteHomeWrap
                xor     a
                ld      (NoteCX),a
NhFirst:
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
                jr      NhFirst

; The key vector. Returns A = 1: every key it takes moves the cursor,
; and the cursor is painted, so the window is recomposed.
NoteKey:
                ld      (NoteChar),a
                xor     a
                ld      (NoteEdited),a
                ld      (NwBusy),a              ; a layout under way would be
                ld      a,(NoteLayOK)           ; of the text as it was
                and     $01
                ld      (NoteLayOK),a           ; (a job leaves it 0 anyway)
                ld      a,(NoteChar)
                cp      KEY_LEFT
                jr      nz,NkNotL
                call    NoteLeft
                jr      NkView
NkNotL:
                cp      KEY_HOME
                jr      nz,NkNotH
                call    NoteHome
                jr      NkView
NkNotH:
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
                call    NwLineCount
                push    af
                call    NoteBack
                jr      NkEdited
NkNotBs:
                cp      KEY_ENTER
                jr      nz,NkNotEnt
                call    NwLineCount
                push    af
                call    NoteEnter
                jr      NkEdited
NkNotEnt:
                cp      ' '
                jr      c,NkNone                ; below space, not ours
                cp      128
                jr      nc,NkNone
                call    NwLineCount
                push    af
                ld      a,(NoteChar)
                call    NoteInsert
NkEdited:
                ; As many lines as before: the edit stayed within the
                ; caret's line, whose rows are laid out again. Otherwise a
                ; line was split, joined, extended into the last chunk or
                ; gave one back, and the whole table is stale.
                pop     bc
                call    NwLineCount
                cp      b
                jr      z,NkSameLines
                call    NwInvalidate
                jr      NkView
NkSameLines:
                call    NwEditLine
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
                xor     a
                ld      (NoteCX),a
                ld      (NoteCY),a
                ld      (NoteTop),a
                ld      (NoteModified),a
                ld      (NwBusy),a
                ld      (NoteLayOK),a
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
                ; With WRAP the layout job lays the new document out this
                ; frame and the next composes and paints it (NoteLayService).
                ld      a,(SetWrap)
                or      a
                ret     nz
                ; Compose now, then restore the desktop and blit next frame,
                ; as for SETTINGS. Do not compose the new buffer twice.
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

; Scroll model: count hard lines, not the chunks of a long line; with
; WRAP the screen rows.
NoteScrollInfo:
                ld      a,(SetWrap)
                or      a
                jr      z,NsiPlain
                ld      hl,NwDerived            ; NoteDrawWrap just derived the
                ld      a,(hl)                  ; caret and the rows for this
                ld      (hl),0                  ; window: the scroll bar is
                or      a                       ; composed right after it
                call    z,NoteMeasureWrap
                jr      NsiReport
NsiPlain:
                call    NoteMeasure
NsiReport:
                call    NoteVisRows
                ld      c,a
                ld      a,(NoteRows)
                ld      b,a
                ld      a,(NoteTop)
                ret

; NoteMeasure, or with WRAP the row table (laid out now if it is
; stale) and the caret derived from it.
NoteMeasureAny:
                ld      a,(SetWrap)
                or      a
                jp      z,NoteMeasure
NoteMeasureWrap:
                call    NwEnsure
                jp      NwDerive

; Moving the view does not move the caret. NoteKey brings it back
; into view on the next edit/navigation. NoteDraw uses the same clamp
; when a larger window exposes more of the document.
NoteScrollTo:
                push    af
                call    NoteMeasureAny
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

; ------------------------------------------------------------
;  Word wrap (lf-1529). Display only: the document is the same
;  sixteen chunks. With WRAP on, a logical line is laid out as
;  screen rows of at most W = WinW-2 cells, broken at the last
;  space that fits (the space itself is not shown) or hard at W
;  when a word is longer. Trailing spaces do not wrap. The caret
;  shows at its column in the last row that starts at or before
;  it, or in the first cell of the row after when that column
;  would be past the edge, which may be a row below the document.
;
;  The layout is a table, a byte a screen row: the row's length,
;  a bit for the space consumed after it, a bit for the end of
;  its logical line. A job fills it, resumable, a budget a frame,
;  because a full document in four columns is 57 rows and the
;  first version laid them out and painted them in 34 ms; the
;  table does not depend on the caret, so moving the caret costs
;  nothing, and an edit within a line lays out that line alone,
;  the rows after it parked at the end of the table meanwhile.
;  WinRedraw runs the job for the front window before it paints
;  and waits for it; anything else that needs the table and finds
;  it stale finishes the job at once. The painter and the caret
;  read the table. Measurements: README, "Word wrap".
; ------------------------------------------------------------
NWTABSZ         equ     128             ; rows at most: 224 characters at
                                        ; three a row and sixteen line ends
NW_LEN          equ     $1F
NW_SOFT         equ     $20             ; a space follows the row
NW_END          equ     $40             ; the logical line ends with it
NWBUDGET        equ     200             ; a frame's work: W+10 a row

; NZ when the table is not the live window's at its width.
NwStale:
                ld      a,(NoteLayOK)
                or      a
                jr      z,NwStaleYes
                ld      a,(NoteLayWnd)
                ld      hl,WndCur
                cp      (hl)
                ret     nz
                ld      a,(NwCacheW)
                ld      hl,WinW
                cp      (hl)
                ret
NwStaleYes:
                or      1
                ret

; The table is no longer any use: a line was added or taken away, or
; the document replaced. A job under way is dropped with it.
NwInvalidate:
                xor     a
                ld      (NwBusy),a
                ld      (NoteLayOK),a
                ret

; The table, laid out now if it is stale. A job under way for this
; window at this width is finished; any other is started afresh.
NwEnsure:
                call    NwStale
                ret     z
                ld      a,(NwBusy)
                or      a
                jr      z,NweFresh
                ld      a,(NoteLayWnd)
                ld      hl,WndCur
                cp      (hl)
                jr      nz,NweFresh
                ld      a,(WinW)
                sub     2
                ld      hl,NwWidth
                cp      (hl)
                jr      z,NweRun
NweFresh:
                call    NwStartFull
NweRun:
                call    NwStep
                jr      nz,NweRun
                ret

; A job over every line of the live window: the chunks from 0 to 16.
NwStartFull:
                xor     a
                ld      (NwLine),a
                ld      (NwTabI),a
                ld      (NwTailN),a
                ld      (NwInLine),a
                ld      (NoteLayOK),a
                ld      a,NOTEROWS
                ld      (NwJobEnd),a
NwStartJob:
                ld      a,(WinW)
                sub     2
                ld      (NwWidth),a
                ld      a,(WndCur)
                ld      (NoteLayWnd),a
                ld      a,1
                ld      (NwBusy),a
                ret

; After an edit within the caret's line, with a valid table: a job
; over that line alone. Its rows are replaced; the rows of the lines
; after it wait at the end of the table and come back when it is done.
NwEditLine:
                ld      a,(SetWrap)
                or      a
                ret     z
                call    NwStale
                ret     nz                      ; the next draw lays out all
                ld      a,(NoteCY)
                call    NwLineOf                ; A = the line, C = its chunk
                push    af
                ld      a,c
                ld      (NwLine),a
                call    NwLastOf
                inc     a
                ld      (NwJobEnd),a            ; the chunk after the line
                pop     af
                ld      hl,NwTab
                ld      c,0                     ; the entry
                or      a
                jr      z,NelFound
                ld      b,a
NelFind:
                ld      a,(hl)
                inc     hl
                inc     c
                and     NW_END
                jr      z,NelFind
                djnz    NelFind
NelFound:
                ld      a,c
                ld      (NwTabI),a              ; the line's first row
NelEnd:
                ld      a,(hl)
                inc     hl
                inc     c
                and     NW_END
                jr      z,NelEnd                ; C = the entry after its last
                ld      a,(NwTabN)
                sub     c
                ld      (NwTailN),a
                jr      z,NelGo
                ; park the tail at the end, highest first: the regions may
                ; overlap and the destination is above the source
                ld      c,a
                ld      b,0
                ld      hl,NwTab+NWTABSZ-1
                ld      de,NwTab-1
                ld      a,(NwTabN)
                add     a,e
                ld      e,a
                jr      nc,NelSrc
                inc     d
NelSrc:
                ex      de,hl                   ; HL = the tail's last entry
                lddr
NelGo:
                xor     a
                ld      (NwInLine),a
                ld      (NoteLayOK),a
                jr      NwStartJob

; The logical lines in the document: a link joins two chunks.
NwLineCount:
                ld      hl,NoteBuf+15
                ld      de,NOTESTRIDE
                ld      b,NOTEROWS
                ld      a,b
NlcLoop:
                sub     (hl)
                add     hl,de
                djnz    NlcLoop
                ret

; A = a chunk. Returns A = the index of its logical line and C = the
; line's first chunk.
NwLineOf:
                ld      b,a
                ld      c,0
                ld      e,0                     ; the line
                or      a
                ret     z
                ld      hl,NoteBuf+15
                ld      d,0                     ; the chunk
NloLoop:
                ld      a,(hl)
                or      a
                jr      nz,NloNext
                inc     e
                ld      a,d
                inc     a
                ld      c,a
NloNext:
                push    de
                ld      de,NOTESTRIDE
                add     hl,de
                pop     de
                inc     d
                djnz    NloLoop
                ld      a,e
                ret

; A = a line's first chunk. Returns A = its last.
NwLastOf:
NlaLoop:
                call    NoteLinkAddr
                inc     a
                ld      b,a
                ld      a,(hl)
                or      a
                ld      a,b
                jr      nz,NlaLoop
                dec     a
                ret

; The job, a budget's worth: rows into the table from where it left
; off. Returns Z when the job is done and the table valid, NZ when
; the budget ran out first.
NwStep:
                xor     a
                ld      (NwWork),a
NwStepLoop:
                ld      a,(NwLine)
                ld      hl,NwJobEnd
                cp      (hl)
                jr      nc,NwJobDone
                ld      a,(NwInLine)
                or      a
                call    z,NwLineBegin
NwStepRow:
                call    NwRow
                jr      z,NwStepLineDone
                ld      a,(NwWork)
                cp      NWBUDGET
                jr      c,NwStepRow
                or      1
                ret
NwStepLineDone:
                xor     a
                ld      (NwInLine),a
                ld      a,(NwLast)
                inc     a
                ld      (NwLine),a
                ld      a,(NwWork)
                cp      NWBUDGET
                jr      c,NwStepLoop
                or      1
                ret
NwJobDone:
                ld      a,(NwTailN)
                or      a
                jr      z,NwJobCount
                ld      c,a
                ld      b,0
                ld      hl,NwTab+NWTABSZ
                or      a
                sbc     hl,bc                   ; the parked tail
                ld      de,NwTab
                ld      a,(NwTabI)
                add     a,e
                ld      e,a
                jr      nc,NwJobBack
                inc     d
NwJobBack:
                ldir                            ; back after the line's rows
NwJobCount:
                ld      a,(NwTailN)
                ld      hl,NwTabI
                add     a,(hl)
                ld      (NwTabN),a
                xor     a
                ld      (NwBusy),a
                ld      (NwTailN),a
                ld      a,(WndCur)
                ld      (NoteLayWnd),a
                ld      a,(WinW)
                ld      (NwCacheW),a
                ld      a,1
                ld      (NoteLayOK),a
                xor     a
                ret

; The line at NwLine: its last chunk, its length N without trailing
; spaces, and the scan set at its second character.
NwLineBegin:
                ld      a,(NwLine)
                call    NwLastOf
                ld      (NwLast),a
NwTrimLoop:
                ld      (NwN),a                 ; the chunk being measured
                call    NoteLineLen
                or      a
                jr      nz,NwTrimGot
                ld      a,(NwN)
                ld      hl,NwLine
                cp      (hl)
                jr      z,NwTrimEmpty
                dec     a
                jr      NwTrimLoop
NwTrimEmpty:
                xor     a
                jr      NwTrimSet
NwTrimGot:
                ld      c,a
                ld      a,(NwN)
                ld      hl,NwLine
                sub     (hl)
                call    NwTimes14
                add     a,c
NwTrimSet:
                ld      (NwN),a
                xor     a
                ld      (NwPos),a
                ld      a,(NwLine)
                call    NoteRowAddr
                ld      (NwSegP),hl
                inc     hl                      ; a row never breaks at its
                ld      (NwScanP),hl            ; first character: the scan
                ld      a,(NwWidth)             ; covers indices 1..W
                ld      (NwScanB),a
                ld      a,1
                ld      (NwInLine),a
                ret

; One row of the line into the table. Returns Z when it was the
; line's last. Every character is read once: the scan goes on from
; where it stopped, because there is no space between the one it
; broke at and the limit it had reached.
NwRow:
                ld      a,(NwN)
                ld      hl,NwPos
                sub     (hl)                    ; what is left of the line
                ld      c,a
                ld      a,(NwWidth)
                cp      c
                jr      c,NwScanInit            ; more than fits: find a break
                ld      a,c
                or      NW_END
                call    NwPut
                xor     a
                ret
NwScanInit:
                ld      hl,(NwScanP)
                ld      a,(NwScanB)
                ld      b,a
                xor     a
                ld      (NwSpaceB),a
NwScan:
                ld      a,(hl)
                cp      ' '
                jr      c,NwHop
NwChar:
                jr      z,NwSpace
NwStepChar:
                inc     hl
                djnz    NwScan
                jr      NwScanDone
NwHop:
                inc     hl                      ; a chunk's terminator and link:
                inc     hl                      ; the text goes on in the next
                ld      a,(hl)
                cp      ' '
                jr      NwChar
NwSpace:
                ld      (NwSpaceP),hl
                ld      a,b
                ld      (NwSpaceB),a
                jr      NwStepChar
NwScanDone:
                ld      (NwScanP),hl            ; the character after the limit
                ld      a,(NwSpaceB)
                or      a
                jr      z,NwHard
                ; a soft break at the space: the row is W+1-B long, the
                ; next row starts after the space and the next scan runs
                ; from the old limit to the new one, len+1 characters
                ld      c,a
                ld      a,(NwWidth)
                inc     a
                sub     c
                ld      c,a
                inc     a
                ld      (NwScanB),a
                ld      hl,(NwSpaceP)
                ld      a,1
                call    NwAdvance
                ld      (NwSegP),hl
                ld      a,c
                or      NW_SOFT
                call    NwPut
                ld      a,(NwScanB)             ; len + 1
                ld      hl,NwPos
                add     a,(hl)
                ld      (hl),a
                or      1
                ret
NwHard:
                ; no space: W characters, the next row starts at the limit
                ld      a,(NwWidth)
                ld      (NwScanB),a
                ld      hl,(NwScanP)
                dec     hl
                ld      (NwSegP),hl
                ld      a,(NwWidth)
                call    NwPut
                ld      a,(NwWidth)
                ld      hl,NwPos
                add     a,(hl)
                ld      (hl),a
                or      1
                ret

; A = an entry, into the table at NwTabI, within the room the parked
; tail leaves. Counts the work.
NwPut:
                ld      c,a
                ld      a,(NwTailN)
                ld      b,a
                ld      a,NWTABSZ
                sub     b
                ld      hl,NwTabI
                cp      (hl)
                jr      z,NwPutWork             ; full: the row is not kept
                jr      c,NwPutWork
                ld      a,(hl)
                inc     (hl)
                ld      hl,NwTab
                add     a,l
                ld      l,a
                jr      nc,NwPutStore
                inc     h
NwPutStore:
                ld      (hl),c
NwPutWork:
                ld      a,(NwWidth)
                add     a,10
                ld      hl,NwWork
                add     a,(hl)
                jr      nc,NwPutCount
                ld      a,$FF
NwPutCount:
                ld      (hl),a
                ret

; A = A*14, E clobbered.
NwTimes14:
                ld      e,a
                add     a,a
                add     a,e
                add     a,a
                add     a,e
                add     a,a
                ret

; HL = a character's address, A = how many characters on. A chunk's
; end is hopped when a link follows; without one the terminator is
; the end of chunk caret (NoteCX 14), as the editor represents it.
NwAdvance:
                or      a
                ret     z
                ld      b,a
NwaLoop:
                inc     hl
                ld      a,(hl)
                or      a
                jr      nz,NwaNext
                inc     hl
                ld      a,(hl)
                dec     a
                jr      z,NwaHop
                dec     hl
                jr      NwaNext
NwaHop:
                inc     hl
NwaNext:
                djnz    NwaLoop
                ret

; The caret from the table: NoteCursorX its logical column, NoteCursorY
; its screen row, NwCaretCol its screen column, and NoteRows the rows,
; one more when the caret sits below the last.
NwDerive:
                ld      a,(NoteCY)
                call    NwLineOf
                ld      (NwCurLine),a
                ld      a,(NoteCY)
                sub     c
                call    NwTimes14
                ld      hl,NoteCX
                add     a,(hl)
                ld      (NoteCursorX),a
                xor     a
                ld      (NoteCursorY),a
                ld      (NwCaretCol),a
                ld      a,(NwTabN)
                ld      hl,NwTab
                ld      c,0                     ; the row
                ld      d,0                     ; its start within the line
                ld      e,0                     ; the line
                or      a
                jr      z,NwdDone
                ld      b,a
NwdLoop:
                ld      a,(NwCurLine)
                cp      e
                jr      nz,NwdNext
                ld      a,(NoteCursorX)
                cp      d
                jr      c,NwdNext               ; the row starts after the caret
                ld      a,c
                ld      (NoteCursorY),a
                ld      a,(NoteCursorX)
                sub     d
                ld      (NwCaretCol),a
NwdNext:
                ld      a,(hl)
                inc     hl
                inc     c
                bit     6,a
                jr      nz,NwdLineEnd
                bit     5,a
                jr      z,NwdNoSpace
                inc     d                       ; the space consumed after it
NwdNoSpace:
                and     NW_LEN
                add     a,d
                ld      d,a
                djnz    NwdLoop
                jr      NwdDone
NwdLineEnd:
                inc     e
                ld      d,0
                djnz    NwdLoop
NwdDone:
                ld      a,(WinW)
                sub     2
                ld      hl,NwCaretCol
                cp      (hl)                    ; W against the column
                jr      z,NwdPast
                jr      nc,NwdRows              ; within the row
NwdPast:
                ld      (hl),0                  ; past the edge: the first cell
                ld      hl,NoteCursorY          ; of the row after
                inc     (hl)
NwdRows:
                ld      a,(NoteCursorY)
                cp      c
                jr      nz,NwdSet
                inc     c                       ; the caret's row below the text
NwdSet:
                ld      a,c
                ld      (NoteRows),a
                ret

; ---- The table walk: a cursor of row, chunk and offset that the
; painter and the navigation move an entry at a time.
NwWalkInit:
                xor     a
                ld      (NwPRow),a
                ld      (NwPChunk),a
                ld      (NwPOff),a
                ld      (NwPFirst),a
                ld      hl,NwTab
                ret

; HL = the row's entry: the cursor on to the next row's first
; character. A line's last row hands over to the chunk after the
; line; a row starting at a chunk's very end stays there, which is
; the end of chunk caret, unless a link carries on.
NwpAdvance:
                ld      a,(hl)
                ld      (NwPEntry),a
                bit     6,a
                jr      nz,NwpLineEnd
                ld      c,a
                and     NW_LEN
                bit     5,c
                jr      z,NwpAdv
                inc     a
NwpAdv:
                ld      hl,NwPOff
                add     a,(hl)
NwpNorm:
                cp      NOTECOLS
                jr      c,NwpOffSet
                push    af
                ld      a,(NwPChunk)
                call    NoteLinkAddr
                ld      a,(hl)
                or      a
                jr      z,NwpAtEnd
                pop     af
                sub     NOTECOLS
                ld      hl,NwPChunk
                inc     (hl)
                jr      NwpNorm
NwpAtEnd:
                pop     af
                ld      a,NOTECOLS
NwpOffSet:
                ld      (NwPOff),a
                jr      NwpNext
NwpLineEnd:
                ld      a,(NwPFirst)
                call    NwLastOf
                inc     a
                ld      (NwPFirst),a
                ld      (NwPChunk),a
                xor     a
                ld      (NwPOff),a
NwpNext:
                ld      hl,NwPRow
                inc     (hl)
                ret

; HL = the address of the cursor's character.
NwpAddr:
                ld      a,(NwPChunk)
                call    NoteRowAddr
                ld      a,(NwPOff)
                add     a,l
                ld      l,a
                ret     nc
                inc     h
                ret

; A = a row. Returns Z, HL = its first character and NwPEntry = its
; entry, or NZ when the table has no such row.
NwRowAt:
                ld      c,a
                ld      a,(NwTabN)
                cp      c
                jr      z,NwraNone
                jr      c,NwraNone
                push    bc
                call    NwWalkInit
                pop     bc
NwraLoop:
                ld      a,(NwPRow)
                cp      c
                jr      z,NwraFound
                push    bc
                push    hl
                call    NwpAdvance
                pop     hl
                inc     hl
                pop     bc
                jr      NwraLoop
NwraFound:
                ld      a,(hl)
                ld      (NwPEntry),a
                call    NwpAddr
                xor     a
                ret
NwraNone:
                or      1
                ret

; ---- The painter: the rows in view from the table into the buffer,
; the text and then spaces to the edge when WinDraw left the
; interior as it was (the home size, APPF_FULL).
NwPaint:
                call    NwWalkInit
                push    hl
                ld      hl,(WinBufP)            ; row 1, column 1
                ld      a,(WinW)
                ld      e,a
                ld      d,0
                add     hl,de
                inc     hl
                ld      (NwPCell),hl
                pop     hl
                ld      a,(NwTabN)
                or      a
                ret     z
                ld      b,a
NwpLoop:
                push    bc
                push    hl
                call    NwpRow
                pop     hl
                push    hl
                call    NwpAdvance
                pop     hl
                inc     hl
                pop     bc
                ld      a,(NoteTop)
                ld      c,a
                ld      a,(WinH)
                sub     2
                add     a,c                     ; the first row below the view
                ld      c,a
                ld      a,(NwPRow)
                cp      c
                ret     nc                      ; the rest is out of view
                djnz    NwpLoop
                ret

NwpRow:
                ld      a,(hl)
                ld      (NwPEntry),a
                ld      a,(NoteTop)
                ld      c,a
                ld      a,(NwPRow)
                sub     c
                ret     c
                ld      c,a                     ; the row within the view
                ld      a,(WinH)
                sub     2
                cp      c
                ret     z
                ret     c
                ld      hl,(NwPCell)            ; the row's first cell, and
                push    hl                      ; the next row's for later
                ld      a,(WinW)
                ld      e,a
                ld      d,0
                add     hl,de
                ld      (NwPCell),hl
                call    NwpAddr                 ; HL = the row's first character
                pop     de                      ; DE = the row's first cell
                ld      a,(NwPEntry)
                and     NW_LEN
                jr      z,NwpPad
                ld      (NwPLeft),a
                ld      a,(NwPOff)
                ld      c,a
NwpPiece:
                ld      a,NOTECOLS
                sub     c                       ; room left in this chunk
                ld      c,a
                ld      a,(NwPLeft)
                cp      c
                jr      nc,NwpWhole
                ld      c,a                     ; the rest fits
NwpWhole:
                sub     c
                ld      (NwPLeft),a
                ld      b,0
                ldir
                ld      a,(NwPLeft)
                or      a
                jr      z,NwpPad
                inc     hl                      ; the chunk's end: the text
                inc     hl                      ; goes on in the next
                ld      c,0
                jr      NwpPiece
NwpPad:
                ld      a,(WdFull)
                or      a
                ret     z                       ; WinDraw blanked the interior
                ld      a,(NwPEntry)
                and     NW_LEN
                ld      c,a
                ld      a,(WinW)
                sub     2
                sub     c
                ret     z
                ld      b,a
                ld      a,' '
NwpSpace:
                ld      (de),a
                inc     de
                djnz    NwpSpace
                ret

; ---- The draw with WRAP: the table (laid out now if it is stale),
; the caret from it, the top following the caret after a key and
; clamped to the last page, the rows in view, the caret inverted.
NoteDrawWrap:
                xor     a
                ld      (NoteLeftCol),a
                call    NwEnsure
                call    NwDerive
                ld      a,(NoteTop)
                ld      c,a                     ; the top wanted
                ld      a,(NoteFollow)
                or      a
                jr      z,NdwClamp
                xor     a
                ld      (NoteFollow),a
                ld      a,(NoteCursorY)
                cp      c
                jr      nc,NdwBelow
                ld      c,a                     ; above the view
                jr      NdwClamp
NdwBelow:
                ld      b,a
                ld      a,(WinH)
                sub     2
                ld      e,a
                ld      a,c
                add     a,e
                dec     a                       ; the last row in view
                cp      b
                jr      nc,NdwClamp
                ld      a,b
                sub     e
                inc     a
                ld      c,a
NdwClamp:
                ld      a,(WinH)
                sub     2
                ld      b,a
                ld      a,(NoteRows)
                sub     b
                jr      nc,NdwMax
                xor     a
NdwMax:
                cp      c
                jr      nc,NdwTopSet
                ld      c,a
NdwTopSet:
                ld      a,c
                ld      (NoteTop),a
                call    NwPaint
                ; the rows in view below the table are blank
                ld      a,(WdFull)
                or      a
                jr      z,NdwCaret
                ld      a,(NoteTop)
                ld      c,a
                ld      a,(NwTabN)
                sub     c
                jr      nc,NdwBlank
                xor     a
NdwBlank:
                ld      b,a                     ; the first row to blank
                ld      a,(WinH)
                sub     2
                cp      b
                jr      z,NdwCaret
                jr      c,NdwCaret
                sub     b
                ld      c,a                     ; rows to blank
                inc     b
NdwBlankRow:
                push    bc
                ld      c,1
                call    WinCell
                ld      a,(WinW)
                sub     2
                ld      b,a
                ld      a,' '
NdwBlankCell:
                ld      (hl),a
                inc     hl
                djnz    NdwBlankCell
                pop     bc
                inc     b
                dec     c
                jr      nz,NdwBlankRow
NdwCaret:
                ld      a,1
                ld      (NwDerived),a           ; for the scroll bar's model
                ld      a,(NoteTop)
                ld      c,a
                ld      a,(NoteCursorY)
                sub     c
                ret     c
                ld      b,a
                ld      a,(WinH)
                sub     2
                cp      b
                ret     z
                ret     c
                inc     b
                ld      a,(NwCaretCol)
                inc     a
                ld      c,a
                call    WinCell
                ld      a,(hl)
                or      INVBANK
                ld      (hl),a
                ret

; ---- Up and down move a screen row, keeping the column where the
; row is long enough; HOME goes to the row's start. The row wanted
; comes from the table and the caret becomes chunk coordinates again.
NoteUpWrap:
                call    NoteMeasureWrap
                ld      a,(NoteCursorY)
                or      a
                ret     z
                dec     a
                jr      NoteGotoRow
NoteDownWrap:
                call    NoteMeasureWrap
                ld      a,(NoteCursorY)
                inc     a
                ld      hl,NwTabN               ; nothing below the last row
                cp      (hl)
                ret     nc
NoteGotoRow:
                call    NwRowAt
                ret     nz
                ld      a,(NwPEntry)
                and     NW_LEN
                ld      c,a
                ld      a,(NwCaretCol)
                cp      c
                jr      c,NgrCol
                ld      a,c
NgrCol:
                call    NwAdvance
NoteCaretAt:
                ; HL = a document address: chunk Y is its sixteenth, X
                ; the rest, the editor's own coordinates
                ld      de,NoteBuf
                or      a
                sbc     hl,de
                ld      a,l
                and     $0F
                ld      (NoteCX),a
                ld      a,l
                rrca
                rrca
                rrca
                rrca
                and     $0F
                ld      (NoteCY),a
                ret
NoteHomeWrap:
                call    NoteMeasureWrap
                ld      a,(NoteCursorY)
                call    NwRowAt
                ret     nz
                jr      NoteCaretAt

; ---- Before a repaint (WinRedraw): the layout job for the front
; window, or for the window being resized, a budget's worth. Returns
; A = 1 while the repaint has to wait for it, 0 to go ahead. A full
; job starts when the window's buffer is to be recomposed and its
; table is stale. A job for a window that is no longer the live one
; is dropped; that window lays out when it is composed.
NoteLayService:
                ld      a,(SetWrap)
                or      a
                ret     z
                ld      a,(NwBusy)
                or      a
                jr      nz,NlsRun
                ld      a,(WndCount)
                or      a
                ret     z
                ld      a,(Resizing)
                cp      2
                jr      nc,NlsLive              ; the resized window is live
                ld      a,(RepaintDue)
                or      a
                ret     z
                ; the frontmost notepad whose buffer is to be recomposed
                ; and whose table is stale, if there is one
                ld      b,a
                ld      a,(WndCount)
                ld      b,a
                ld      hl,WndZ
NlsScan:
                push    bc
                push    hl
                ld      a,(hl)
                ld      c,a                     ; the slot
                ld      b,a
                inc     b
                ld      a,$80
NlsBit:
                rlca
                djnz    NlsBit
                ld      hl,WndDirty
                and     (hl)
                jr      z,NlsNext               ; its buffer is not recomposed
                ld      a,c
                ld      hl,WndCur
                cp      (hl)
                jr      nz,NlsTable
                ld      de,(WinApp)             ; the live record: the table's
                jr      NlsApp                  ; copy waits for WndFlush
NlsTable:
                call    WndRecAt
                ld      de,WR_APP
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
NlsApp:
                ld      hl,AppNote
                or      a
                sbc     hl,de
                jr      nz,NlsNext
                ld      a,c
                call    WndSelect               ; the front: live already
                call    NwStale
                jr      z,NlsNext               ; its table serves
                pop     hl
                pop     bc
                jr      NlsStart
NlsNext:
                pop     hl
                pop     bc
                inc     hl
                djnz    NlsScan
                xor     a
                ret
NlsLive:
                ld      hl,(WinApp)
                ld      de,AppNote
                or      a
                sbc     hl,de
                jr      nz,NlsGo
                call    NwStale
                jr      z,NlsGo
NlsStart:
                ; A job started here paints next frame at the earliest, as
                ; WndOpenLater splits a window's open: the frame has had
                ; the event that made the window stale in it already.
                call    NwStartFull
                call    NwStep
                ld      a,1
                ret
NlsRun:
                ld      a,(NoteLayWnd)
                ld      hl,WndCur
                cp      (hl)
                jr      nz,NlsDrop
                ld      a,(WinW)
                sub     2
                ld      hl,NwWidth
                cp      (hl)
                jr      nz,NlsAgain
                call    NwStep
                ld      a,1
                ret     nz                      ; more next frame
NlsGo:
                xor     a
                ret
NlsAgain:
                call    NwStartFull             ; the window changed width
                jr      NlsRun
NlsDrop:
                call    NwInvalidate
                xor     a
                ret
