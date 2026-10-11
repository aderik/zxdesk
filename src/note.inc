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
; NoteRows, NoteCursorX and NoteCursorY are NwDerive's too: what it
; derived is gone (NwDrvOK).
NoteMeasure:
                xor     a
                ld      (NwDrvOK),a
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
                ld      a,(WndCur)              ; wrapped: this window's draw
                inc     a                       ; follows the caret
                ld      (NoteFollow),a
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
                ; At the home size WinDraw leaves the interior as it was
                ; (APPF_FULL), and a chunk entirely left of the view paints
                ; nothing, nor does the last chunk paint the cells past its
                ; end: whatever was there stays. Seen after a WRAP toggle
                ; (lf-1542): blank the interior first.
                ld      a,(WdFull)
                or      a
                call    nz,NoteBlankInterior
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

; The live buffer's interior, spaces.
NoteBlankInterior:
                ld      b,1
                ld      c,1
                call    WinCell
                ld      a,(WinH)
                sub     2
                ld      b,a
NbiRow:
                push    bc
                ld      a,(WinW)
                sub     2
                ld      b,a
                ld      a,' '
NbiCell:
                ld      (hl),a
                inc     hl
                djnz    NbiCell
                inc     hl                      ; the edges
                inc     hl
                pop     bc
                djnz    NbiRow
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
                ld      a,(NoteAnchor+1)        ; $FF: nothing selected, the
                inc     a                       ; usual case. 27 T a chunk
                jp      z,WinPrint              ; against 240 for the call:
                push    bc                      ; a full-screen compose at
                call    NotePaintSelection      ; 60 Hz dropped a frame.
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
                call    NwInvalidate            ; another document: no table
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
                inc     a
                ld      (NwKeyed),a             ; the job's budget this frame
                call    NwLinksSave             ; for NkEdited, whatever the key
                ld      a,(NoteChar)
                cp      KEY_SELECT
                jp      z,NoteToggleSelection
                cp      KEY_ESC
                jr      nz,NkSelectionEdit
                call    NoteClearSelection
                jp      NkView
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
                jp      nz,NkEdited             ; the table: lines may have gone
                ld      a,KEY_DELETE
NkNavigation:
                cp      KEY_LEFT
                jr      nz,NkNotL
                call    NoteLeft
                jr      NkMoved
NkNotL:
                cp      KEY_HOME
                jr      nz,NkNotH
                call    NoteHome
                jr      NkMoved
NkNotH:
                cp      KEY_RIGHT
                jr      nz,NkNotR
                call    NoteRight
                jr      NkMoved
NkNotR:
                cp      KEY_UP
                jr      nz,NkNotU
                call    NoteUp
                jr      NkMoved
NkNotU:
                cp      KEY_DOWN
                jr      nz,NkNotD
                call    NoteDown
NkMoved:
                call    NwCaretCheck            ; with WRAP: a caret among
                jr      NkView                  ; trailing spaces has its row
NkNotD:
                cp      KEY_DELETE
                jr      nz,NkNotBs
                call    NoteBack
                jr      NkEdited
NkNotBs:
                cp      KEY_ENTER
                jr      nz,NkNotEnt
                call    NoteEnter
                jr      NkEdited
NkNotEnt:
                cp      ' '
                jr      c,NkNone                ; below space, not ours
                cp      128
                jr      nc,NkNone
                call    NoteInsert
NkEdited:
                ld      a,2
                ld      (NwKeyed),a             ; an edit: the repaint waits a frame
                ; The same links as before: the edit stayed within the
                ; caret's line, whose rows are laid out again. Otherwise a
                ; line was split, joined, extended into another chunk or
                ; gave one back, the chunks after it moved, and the whole
                ; table is stale. (The count of lines is not enough: a join
                ; that frees a chunk leaves a blank line at the end.)
                call    NwLinksSame
                jr      z,NkSameLines
                call    NwInvalidate
                jr      NkView
NkSameLines:
                call    NwEditLine
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
                call    NwInvalidate            ; another document: no table
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

; Tape has no directory: load the next file, whatever its name, and
; take the name from its header (a SAVE AS name survives closing).
NoteMenuOpen:
                call    StIdent
                cp      ST_TAPE
                jp      nz,CmdBrowseOpen
                call    NoteIsFront
                ret     nz
                ld      hl,NoteName
                ld      a,(NoteBackend)
                cp      ST_TAPE
                jr      nz,NmoLoad              ; a note kept elsewhere: by name
                ld      hl,TxtNoteName+4        ; empty name
NmoLoad:
                push    hl
                call    NoteLoadNamed
                pop     hl
                ld      a,1
                jr      c,NmoOk
                ld      a,(hl)
                or      a
                ld      hl,TapeIn+4
                call    z,NoteSetName
                xor     a
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
                ld      a,(SetWrap)
                or      a
                jr      z,NrPlain
                call    NwInvalidate            ; the text changed under the
                ld      a,(WndCur)              ; table: NoteLayService lays
                call    WndTouch                ; it out and composes, a
                jp      RowsAddLive             ; frame at a time
NrPlain:
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
;  Word wrap (lf-1529, lf-1541). Display only: the document is
;  the same sixteen chunks. With WRAP on, a logical line is laid
;  out as screen rows of at most W = WinW-2 cells, broken at the
;  last space that fits (the space itself is not shown) or hard
;  at W when a word is longer. Trailing spaces do not wrap, except
;  that the caret's line is laid out as far as the caret, so a
;  caret among trailing spaces has a row to be shown in. The caret
;  shows at its column in the last row that starts at or before
;  it, or in the first cell of the row after when that column
;  would be past the edge, which may be a row below the document.
;
;  The layout is a table, a byte a screen row: the row's length,
;  a bit for the space consumed after it, a bit for the end of
;  its logical line. A job fills it, resumable, a budget a frame,
;  because a full document in four columns is 57 rows and the
;  first version laid them out and painted them in 34 ms. The
;  table does not depend on where the caret is (the one exception
;  above re-lays a line), so moving the caret costs no layout, and
;  an edit within a line lays out that line alone, the rows after
;  it parked at the end of the table meanwhile. NoteLayService,
;  first thing in WinRedraw, runs the job for one notepad a frame,
;  composes that window when the job is done and lets the repaint
;  go ahead only in a frame that did little else; anything else
;  that needs the table and finds it stale finishes the job at
;  once (NwEnsure). The painter and the caret read the table with
;  a walk kept in registers: the table pointer, the document
;  address of the row and its offset within the chunk.
;
;  The table and the job's scratch live in the tape buffer: that
;  buffer is only in use between TapeOpen and the close of the
;  same synchronous transfer, and TapeOpen invalidates the table.
;  Static RAM for the whole feature is the six flags below, with
;  SetWrap and RowsDesk. Measurements: README, "Word wrap".
; ------------------------------------------------------------
NWTABSZ         equ     128             ; rows at most: 224 characters at
                                        ; three a row and sixteen line ends
NW_LEN          equ     $1F
NW_SOFT         equ     $20             ; a space follows the row
NW_END          equ     $40             ; the logical line ends with it
NWNONE          equ     $FF
; The job's budget, in units of about 45 cycles: a character scanned,
; NWROWCOST a row, NWLINECOST a line. Measured on C-BIOS: a row of
; fourteen costs 1,150 cycles, an empty line 1,700.
NWBUDGET        equ     300             ; a frame's work, about 5 ms
NWROWCOST       equ     12
NWLINECOST      equ     20
NWSMALL         equ     40              ; a frame that did no more than this
                                        ; goes on to compose and paint, about
                                        ; 0.7 ms: a short line laid out again
NwTab           equ     TapeBuf         ; NWTABSZ entries
NwTabN          equ     TapeBuf+128     ; entries in the table
NwJobEnd        equ     TapeBuf+129     ; the first chunk the job leaves alone
NwTabI          equ     TapeBuf+130     ; the entry it writes next
NwTailN         equ     TapeBuf+131     ; entries parked at the table's end
NwWork          equ     TapeBuf+132     ; the frame's budget used (16 bits)
NwWidth         equ     TapeBuf+134     ; W, the row width in cells
NwLine          equ     TapeBuf+135     ; the line's first chunk
NwLast          equ     TapeBuf+136     ; and its last
NwN             equ     TapeBuf+137     ; its length, trailing spaces trimmed
NwPos           equ     TapeBuf+138     ; the row's start within the line
NwScanP         equ     TapeBuf+139     ; the break scan's pointer
NwScanB         equ     TapeBuf+141     ; and how many characters it covers
NwSpaceP        equ     TapeBuf+142     ; the last space seen in the scan
NwSpaceB        equ     TapeBuf+144     ; 0: none
NwInLine        equ     TapeBuf+145     ; the job is part way through a line
NwExtLine       equ     TapeBuf+146     ; the line laid out as far as the
NwExtX          equ     TapeBuf+147     ; caret (its first chunk), and to where
NwCurLine       equ     TapeBuf+148     ; the caret's line
NwCaretCol      equ     TapeBuf+149     ; the caret's screen column
NwDerived       equ     TapeBuf+150     ; the draw derived the caret for the scroll bar
NwPCell         equ     TapeBuf+151     ; the painter's buffer cell
NwPLeft         equ     TapeBuf+153     ; and what is left of the row
NwPRows         equ     TapeBuf+154     ; rows of the view still to paint
NlsLive         equ     TapeBuf+155     ; the live window at the service's entry
NlsSlot         equ     TapeBuf+156     ; the window the service works on
NwPPad          equ     TapeBuf+157     ; and the cells after its text
NwKeyed         equ     TapeBuf+158     ; a key reached the notepad this frame (2: an edit)
NwTopOK         equ     TapeBuf+159     ; the painter's walk at NwTopRow is known:
NwTopRow        equ     TapeBuf+176     ; row, link byte, offset, as of this table
NwTopDE         equ     TapeBuf+177
NwTopC          equ     TapeBuf+179
NwSelOn         equ     TapeBuf+180     ; the draw: a selection to invert, its
NwSelLo         equ     TapeBuf+181     ; packed cells (chunk*16+column), lower
NwSelHi         equ     TapeBuf+182     ; and exclusive upper
NwExtCX         equ     TapeBuf+183     ; the caret when its line was extended:
NwExtCY         equ     TapeBuf+184     ; unchanged, nothing to lay out again
NwGen           equ     TapeBuf+185     ; counts the jobs: the table's generation
NwDrvOK         equ     TapeBuf+186     ; NwDerive's result stands for this caret,
NwDrvCX         equ     TapeBuf+187     ; generation and width (1.4 ms for 57
NwDrvCY         equ     TapeBuf+188     ; rows, twice in a cursor key's frame)
NwDrvGen        equ     TapeBuf+189
NwDrvW          equ     TapeBuf+190
NwLinks         equ     TapeBuf+160     ; the sixteen links before an edit

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

; The table is no longer any use: a line was added or taken away,
; the document replaced, or the tape buffer used. A job under way
; is dropped with it.
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
                ld      a,NWNONE
                ld      (NwExtLine),a
                ld      a,NOTEROWS
                ld      (NwJobEnd),a
NwStartJob:
                xor     a
                ld      (NwTopOK),a             ; the table changes under the painter
                ld      (NwDrvOK),a             ; and under the caret
                ld      hl,NwGen
                inc     (hl)
                ld      a,(WinW)
                sub     2
                ld      (NwWidth),a
                ld      a,(WndCur)
                ld      (NoteLayWnd),a
                ld      a,1
                ld      (NwBusy),a
                ret

; After an edit within the caret's line, or a caret move that changes
; how far its line is laid out: a job over that line alone, given a
; valid table. Its rows are replaced; the rows of the lines after it
; wait at the end of the table and come back when it is done. A job
; under way has the text as it was and is dropped: the service lays
; everything out again.
NwEditLine:
                ld      a,(SetWrap)
                or      a
                ret     z
                ld      a,(NwBusy)
                or      a
                jr      nz,NwInvalidate
                call    NwStale
                ret     nz                      ; another window's table serves it still
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
                jp      NwStartJob

; After a caret move with WRAP: the caret's line is laid out again
; when the caret sits past the line's text, among trailing spaces
; that are laid out as far as it, or sat there before the move. A
; stale table is laid out whole, with the caret where it is now.
NwCaretCheck:
                ld      a,(SetWrap)
                or      a
                ret     z
                ld      a,(NwBusy)
                or      a
                ret     nz                      ; the job sees the caret as it is
                call    NwStale
                ret     nz
                ld      a,(NwExtLine)
                cp      NWNONE
                jr      z,NccNone
                ld      hl,(NoteCX)             ; extended to the caret as it
                ld      de,(NwExtCX)            ; is: nothing moved
                or      a
                sbc     hl,de
                ret     z
                jr      NccFull
NccNone:
                ; nothing extended: only a caret past its line's text asks
                ; for anything, and that is text at or after it in its chunk
                ; or in a chunk the line goes on in. Cheap, for every key.
                ld      a,(NoteCY)
                call    NoteLineLen
                ld      hl,NoteCX
                cp      (hl)
                ret     nc                      ; text at the caret or after it
                ld      a,(NoteCY)
NccChunkNext:
                call    NoteLinkAddr
                inc     a
                ld      b,(hl)
                inc     b
                dec     b
                jr      z,NccFull               ; the line's end: past its text
                push    af
                call    NoteLineLen
                or      a
                pop     af
                jr      z,NccChunkNext
                ret                             ; text further on
NccFull:
                ld      a,(NoteCY)
                call    NwLineOf                ; C = the line's first chunk
                ld      a,(NwExtLine)
                cp      NWNONE
                jr      z,NccNoOld
                cp      c
                jp      nz,NwInvalidate         ; another line carries the extension
NccNoOld:
                push    bc
                ld      a,c
                call    NwTrimmed               ; A = the text's length
                pop     bc
                ld      d,a
                ld      a,(NoteCY)
                sub     c
                call    NwTimes14
                ld      hl,NoteCX
                add     a,(hl)                  ; A = the caret's column
                cp      d
                jr      z,NccWithin
                jr      c,NccWithin
                ; past the text: as laid out, or laid out again to here
                ld      hl,NwExtLine
                ld      e,a
                ld      a,c
                cp      (hl)
                jp      nz,NwEditLine
                inc     hl
                ld      a,e
                cp      (hl)                    ; NwExtX
                ret     z
                jp      NwEditLine
NccWithin:
                ld      a,(NwExtLine)
                cp      c
                ret     nz                      ; nothing extended here
                jp      NwEditLine              ; the extension goes

; The sixteen links as they are, kept across an edit (NwLinksSave),
; and Z when they are still the same (NwLinksSame).
NwLinksSave:
                ld      hl,NoteBuf+15
                ld      de,NwLinks
                ld      bc,NOTESTRIDE*256+NOTEROWS
NlsvLoop:
                ld      a,(hl)
                ld      (de),a
                inc     de
                ld      a,l
                add     a,b
                ld      l,a
                jr      nc,NlsvNext
                inc     h
NlsvNext:
                dec     c
                jr      nz,NlsvLoop
                ret
NwLinksSame:
                ld      hl,NoteBuf+15
                ld      de,NwLinks
                ld      bc,NOTESTRIDE*256+NOTEROWS
NlsmLoop:
                ld      a,(de)
                cp      (hl)
                ret     nz
                inc     de
                ld      a,l
                add     a,b
                ld      l,a
                jr      nc,NlsmNext
                inc     h
NlsmNext:
                dec     c
                jr      nz,NlsmLoop
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

; A = a line's first chunk. Returns A = its length without trailing
; spaces, and NwLast = its last chunk.
NwTrimmed:
                ld      (NwLine),a
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
                ret
NwTrimGot:
                ld      c,a
                ld      a,(NwN)
                ld      hl,NwLine
                sub     (hl)
                call    NwTimes14
                add     a,c
                ret

; The job, a budget's worth: rows into the table from where it left
; off. Returns Z when the job is done and the table valid, NZ when
; the budget ran out first.
NwStep:
                ld      hl,0
                ld      (NwWork),hl
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
                call    NwOverBudget
                jr      c,NwStepRow
                or      1
                ret
NwStepLineDone:
                xor     a
                ld      (NwInLine),a
                ld      a,(NwLast)
                inc     a
                ld      (NwLine),a
                call    NwOverBudget
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

; Carry while the frame's work is within the budget: half of it in a
; frame that handled a key in the notepad, which has had the edit, the
; caret's move and the line's rows found before the job starts.
NwOverBudget:
                ld      hl,(NwWork)
                ld      de,NWBUDGET
                ld      a,(NwKeyed)
                or      a
                jr      z,NwObFull
                ld      de,NWBUDGET/2
NwObFull:
                or      a
                sbc     hl,de
                ret

; A = units of work done.
NwCharge:
                ld      e,a
                ld      d,0
                ld      hl,(NwWork)
                add     hl,de
                ld      (NwWork),hl
                ret

; The line at NwLine: its last chunk, its length N without trailing
; spaces (but as far as the caret when the caret is on this line),
; and the scan set at its second character.
NwLineBegin:
                ld      a,NWLINECOST
                call    NwCharge
                ld      a,(NwLine)
                call    NwTrimmed
                ld      c,a
                ld      a,(NoteCY)
                ld      hl,NwLine
                cp      (hl)
                jr      c,NwLbPlain             ; the caret's line is before this one
                ld      hl,NwLast
                cp      (hl)
                jr      z,NwLbCaret
                jr      nc,NwLbPlain            ; or after it
NwLbCaret:
                ld      hl,NwLine
                sub     (hl)
                call    NwTimes14
                ld      hl,NoteCX
                add     a,(hl)                  ; A = the caret's column
                cp      c
                jr      z,NwLbNoExt
                jr      c,NwLbNoExt
                ld      c,a                     ; past the text: lay out to the caret
                ld      (NwExtX),a
                ld      a,(NwLine)
                ld      (NwExtLine),a
                ld      hl,(NoteCX)
                ld      (NwExtCX),hl
                jr      NwLbSet
NwLbNoExt:
                ld      a,(NwExtLine)
                ld      hl,NwLine
                cp      (hl)
                jr      nz,NwLbSet
                ld      a,NWNONE                ; no longer extended
                ld      (NwExtLine),a
                jr      NwLbSet
NwLbPlain:
NwLbSet:
                ld      a,c
                ld      (NwN),a
                xor     a
                ld      (NwPos),a
                ld      a,(NwLine)
                call    NoteRowAddr
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
                ld      a,(NwScanB)
                call    NwCharge                ; the characters about to be read
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
                ld      a,NWROWCOST
                jp      NwCharge

; A = A*14, E clobbered.
NwTimes14:
                ld      e,a
                add     a,a
                add     a,e
                add     a,a
                add     a,e
                add     a,a
                ret

; ---- The walk: DE = the link byte of the chunk a row starts in (its
; base + 15, so the link is one read away), C = the row's offset within
; the chunk (0..14), A = the row's entry. On to the next row. A line's
; last row hands over to the chunk after the line; a row that ends at
; a chunk's end goes on to the next chunk when a link carries on, and
; otherwise stays at that end, which is the end of chunk caret (NoteCX
; 14), as the editor represents it. Only A is used; HL and B are kept,
; so the callers loop in registers.
NwAdvRow:
                bit     6,a
                jr      nz,NwAdvLine
                and     NW_LEN+NW_SOFT
                cp      NW_SOFT
                jr      c,NwAdvAdd
                sub     NW_SOFT-1               ; the row and the space after it
NwAdvAdd:
                add     a,c
                ld      c,a                     ; the offset after the row
NwAdvNorm:
                cp      NOTECOLS
                ret     c                       ; within the chunk
                ld      a,(de)                  ; the chunk's link
                or      a
                jr      z,NwAdvStay
                call    NwNextChunk
                ld      a,c
                sub     NOTECOLS
                ld      c,a
                jr      NwAdvNorm
NwAdvStay:
                ld      c,NOTECOLS              ; the chunk's end, no further
                ret
NwAdvLine:
                ld      a,(de)
                or      a
                jr      z,NwAdvLineEnd
                call    NwNextChunk             ; the line goes on there
                jr      NwAdvLine
NwAdvLineEnd:
                call    NwNextChunk             ; the chunk after the line
                ld      c,0
                ret
NwNextChunk:
                ld      a,NOTESTRIDE
                add     a,e
                ld      e,a
                ret     nc
                inc     d
                ret

; DE = a chunk's link byte, C = an offset in it: A characters on,
; hopping a chunk's end when a link follows and stopping at it
; otherwise.
NwSkip:
                or      a
                ret     z
                ld      b,a
NwSkipLoop:
                inc     c
                ld      a,c
                cp      NOTECOLS
                jr      c,NwSkipNext
                ld      a,(de)                  ; the link
                or      a
                ret     z                       ; the chunk's end: no further
                call    NwNextChunk
                ld      c,0
NwSkipNext:
                djnz    NwSkipLoop
                ret

; DE = a chunk's link byte, C = an offset: HL = the character there.
NwCharAt:
                ld      hl,-15
                add     hl,de
                ld      b,0
                add     hl,bc
                ret

; A = a row. Returns Z with DE = its chunk's link byte, C = its offset
; and A = its entry, or NZ when the table has no such row.
NwRowAt:
                ld      hl,NwTabN
                cp      (hl)
                jr      nc,NwraNone
                ld      hl,NwTab
                ld      de,NoteBuf+15
                ld      c,0
                or      a
                jr      z,NwraFound
                ld      b,a
NwraLoop:
                ld      a,(hl)
                inc     hl
                call    NwAdvRow
                djnz    NwraLoop
NwraFound:
                ld      a,(hl)
                cp      a                       ; Z
                ret
NwraNone:
                or      1
                ret

; The caret from the table: NoteCursorX its logical column, NoteCursorY
; its screen row, NwCaretCol its screen column, and NoteRows the rows,
; one more when the caret sits below the last.
NwDerive:
                ld      a,(NwDrvOK)
                or      a
                jr      z,NwdGo
                ld      hl,(NoteCX)
                ld      de,(NwDrvCX)
                or      a
                sbc     hl,de
                jr      nz,NwdGo
                ld      a,(NwGen)
                ld      hl,NwDrvGen
                cp      (hl)
                jr      nz,NwdGo
                ld      a,(WinW)
                inc     hl
                cp      (hl)
                ret     z                       ; as derived before
NwdGo:
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
                jr      c,NwdDone               ; past the caret's line
                jr      nz,NwdNext              ; before it
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
                ld      a,(NwTabN)
                ld      c,a
                ld      a,(NoteCursorY)
                cp      c
                jr      nz,NwdSet
                inc     c                       ; the caret's row below the text
NwdSet:
                ld      a,c
                ld      (NoteRows),a
                ld      hl,(NoteCX)
                ld      (NwDrvCX),hl
                ld      a,(NwGen)
                ld      (NwDrvGen),a
                ld      a,(WinW)
                ld      (NwDrvW),a
                ld      a,1
                ld      (NwDrvOK),a
                ret

; ---- The painter: the rows in view from the table into the buffer,
; the text and then spaces to the edge when WinDraw left the
; interior as it was (the home size, APPF_FULL). The walk goes
; over the rows above the view without touching the buffer, at
; about eighty cycles a row.
NwPaint:
                ld      hl,(WinBufP)            ; row 1, column 1
                ld      a,(WinW)
                ld      e,a
                ld      d,0
                add     hl,de
                inc     hl
                ld      (NwPCell),hl
                ld      a,(WinH)
                sub     2
                ld      (NwPRows),a
                ld      a,(NoteTop)
                ld      (NwPLeft),a             ; rows above the view
                ; the walk to a row at or above the top, kept from the last
                ; draw of this table: a cursor key or a scroll down does not
                ; walk the rows above the view again (55 rows were 3.2 ms)
                ld      a,(NwTopOK)
                or      a
                jr      z,NwpFresh
                ld      hl,NwTopRow
                ld      a,(NoteTop)
                sub     (hl)
                jr      c,NwpFresh              ; the top went up: from the start
                ld      (NwPLeft),a             ; the rows still to skip
                ld      a,(NwTabN)
                sub     (hl)
                jr      c,NwpFresh              ; cannot be: the table shrank
                ld      b,a                     ; the rows from there
                ld      a,(hl)
                ld      hl,NwTab
                add     a,l
                ld      l,a
                jr      nc,NwpFromTop
                inc     h
NwpFromTop:
                ld      de,(NwTopDE)
                ld      a,(NwTopC)
                ld      c,a
                jr      NwpSkip
NwpFresh:
                ld      hl,NwTab
                ld      de,NoteBuf+15
                ld      c,0
                ld      a,(NwTabN)
                ld      b,a
NwpSkip:
                ld      a,(NwPLeft)
                or      a
                jr      z,NwpTop
                dec     a
                ld      (NwPLeft),a
                ld      a,b
                or      a
                jr      z,NwpRows               ; the table ends above the view
                ld      a,(hl)
                inc     hl
                call    NwAdvRow
                dec     b
                jr      NwpSkip
NwpTop:
                ld      a,(NoteTop)             ; the walk at the top, for next time
                ld      (NwTopRow),a
                ld      (NwTopDE),de
                ld      a,c
                ld      (NwTopC),a
                ld      a,1
                ld      (NwTopOK),a
NwpRows:
                ld      a,(NwPRows)
                or      a
                ret     z
                dec     a
                ld      (NwPRows),a
                ld      a,b
                or      a
                jr      z,NwpBelow
                ld      a,(hl)
                call    NwpRow
                ld      a,(hl)
                inc     hl
                call    NwAdvRow
                dec     b
                jr      NwpRows
NwpBelow:
                xor     a                       ; a row below the table: blank
                call    NwpRow
                jr      NwpRows

; A = an entry, DE = the chunk's link byte, C = the row's offset in
; it: the row's text into the cell at NwPCell, then spaces to the edge
; when the interior was not blanked, and the cell on to the next row.
; HL, DE and BC are kept.
NwpRow:
                push    hl
                push    de
                push    bc
                and     NW_LEN
                ld      (NwPLeft),a
                ld      b,a
                ld      a,(WinW)
                sub     2
                sub     b
                ld      (NwPPad),a              ; cells after the text
                call    NwCharAt                ; HL = the text
                ld      de,(NwPCell)
                ld      a,(NwPLeft)
                or      a
                jr      z,NwpPad
                ld      a,NOTECOLS
                sub     c                       ; room in the first chunk
NwpPiece:
                ld      b,a
                ld      a,(NwPLeft)
                cp      b
                jr      nc,NwpWhole
                ld      b,a                     ; the rest fits
NwpWhole:
                ld      a,b
                or      a
                jr      z,NwpHop                ; no room: the chunk's end
                ld      a,(NwPLeft)
                sub     b
                ld      (NwPLeft),a
                ld      c,b
                ld      b,0
                ldir
                ld      a,(NwPLeft)
                or      a
                jr      z,NwpSel
NwpHop:
                inc     hl                      ; the terminator and the link:
                inc     hl                      ; the text goes on in the next
                ld      a,NOTECOLS
                jr      NwpPiece
NwpSel:
                ; the selected cells of the row, inverted: a packed cell
                ; (chunk*16+column) a character, over the chunk's end
                ld      a,(NwSelOn)
                or      a
                jr      z,NwpPad
                pop     bc                      ; the entry's registers again:
                pop     de                      ; HL the entry, DE the link,
                pop     hl                      ; C the offset
                push    hl
                push    de
                push    bc
                ld      a,(hl)
                and     NW_LEN
                jr      z,NwpPad
                push    af
                call    NwCharAt
                pop     af
                ld      b,a                     ; cells in the row
                ld      de,NoteBuf
                or      a
                sbc     hl,de                   ; packed: the document offset's
                ld      c,l                     ; low byte
                ld      hl,(NwPCell)
NwpSelLoop:
                ld      a,(NwSelLo)
                cp      c
                jr      z,NwpSelIn
                jr      nc,NwpSelOut
NwpSelIn:
                ld      a,(NwSelHi)
                cp      c
                jr      z,NwpSelOut
                jr      c,NwpSelOut
                ld      a,(hl)
                or      INVBANK
                ld      (hl),a
NwpSelOut:
                inc     hl
                inc     c
                ld      a,c
                and     $0F
                cp      NOTECOLS
                jr      nz,NwpSelNext
                inc     c                       ; over the terminator and link
                inc     c
NwpSelNext:
                djnz    NwpSelLoop
                ld      de,(NwPCell)            ; the pad goes after the text
                ld      a,(NwPPad)
                ld      b,a
                ld      a,(WinW)
                sub     2
                sub     b
                ld      b,0
                ld      c,a
                ex      de,hl
                add     hl,bc
                ex      de,hl
NwpPad:
                ld      a,(WdFull)
                or      a
                jr      z,NwpDone               ; WinDraw blanked the interior
                ld      a,(NwPPad)
                or      a
                jr      z,NwpDone
                ld      b,a
                ld      a,' '
NwpSpace:
                ld      (de),a
                inc     de
                djnz    NwpSpace
NwpDone:
                ld      hl,(NwPCell)
                ld      a,(WinW)
                ld      e,a
                ld      d,0
                add     hl,de
                ld      (NwPCell),hl
                pop     bc
                pop     de
                pop     hl
                ret

; ---- The draw with WRAP: the table (laid out now if it is stale),
; the caret from it, the top following the caret after a key in
; this window and clamped to the last page, the rows in view, the
; caret inverted.
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
                ld      b,a
                ld      a,(WndCur)
                inc     a
                cp      b
                jr      nz,NdwClamp             ; another window's key
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
                ; the selection, inverted by the painter in the front
                ; window only, as NotePaintSelection has it
                xor     a
                ld      (NwSelOn),a
                ld      a,(WndZ)
                ld      hl,WndCur
                cp      (hl)
                jr      nz,NdwNoSel
                call    NoteSelectionBounds
                jr      z,NdwNoSel
                ld      a,b
                ld      (NwSelLo),a
                ld      a,c
                ld      (NwSelHi),a
                ld      a,1
                ld      (NwSelOn),a
NdwNoSel:
                call    NwPaint
                ld      a,1
                ld      (NwDerived),a           ; for the scroll bar's model
                call    NoteSelectionBounds
                ret     nz                      ; no caret with a selection, as
                                                ; NoteDraw: in any instance
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
                and     NW_LEN
                ld      hl,NwCaretCol
                cp      (hl)
                jr      c,NgrCol                ; the row is shorter: its end
                ld      a,(hl)
NgrCol:
                call    NwSkip
NoteCaretAt:
                ; DE = a chunk's link byte and C an offset in it: chunk Y
                ; is the sixteenth, X the rest, the editor's own coordinates
                call    NwCharAt
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

; ---- Before a repaint (WinRedraw): one notepad a frame. Returns
; A = 1 while the repaint has to wait, 0 to go ahead.
;
; A resize (Resizing 2 or 3) lays out the live window's table over
; frames and leaves the composition to WinRedraw's own steps. Else
; a job under way is continued for its window, selected if it is
; not the live one, and when it is done the window is composed here
; and its dirty bit cleared, so the repaint only blits it. With no
; job, the frontmost notepad whose buffer is to be recomposed gets
; one: a full layout when its table is stale, straight to the
; composition when the table serves or WRAP is off and the toggle
; asked for every notepad again. The live window is put back before
; returning. A job that ends with a step above NWSMALL leaves the
; composition to the next frame, and the repaint goes ahead in the
; frame of the composition only when the window was the live one,
; the step cost no more than NWSMALL, the desktop under it is not
; repainted and no other notepad is waiting; otherwise it is the
; next frame's, the way WndOpenLater splits an open.
NoteLayService:
                ld      a,(SetWrap)
                or      a
                jr      nz,NlsOn
                ld      a,(NwToggled)
                or      a
                ret     z
NlsOn:
                ld      a,(WndCount)
                or      a
                jp      z,NlsNoWindow
                ld      a,(WndCur)
                ld      (NlsLive),a
                ld      (NlsSlot),a
                ld      hl,0
                ld      (NwWork),hl
                ld      a,(Resizing)
                cp      2
                jp      c,NlsNoResize
                ; the resized window is live: its table, nothing else
                ld      a,(SetWrap)
                or      a
                jp      z,NlsZero
                ld      hl,(WinApp)
                ld      de,AppNote
                or      a
                sbc     hl,de
                jp      nz,NlsZero
                ld      a,(NwBusy)
                or      a
                jr      z,NlsRszFresh
                ld      a,(NoteLayWnd)
                ld      hl,WndCur
                cp      (hl)
                jr      nz,NlsRszFresh
                ld      a,(WinW)
                sub     2
                ld      hl,NwWidth
                cp      (hl)
                jr      z,NlsRszStep
NlsRszFresh:
                call    NwStale
                jp      z,NlsZero
                call    NwStartFull
NlsRszStep:
                call    NwStep
                jp      nz,NlsOne
                jp      NlsDecide
NlsNoResize:
                ld      a,(NwBusy)
                or      a
                jp      z,NlsScan
                ; the job's window: still open, a notepad, the same width?
                ld      a,(NoteLayWnd)
                call    NlsOpen
                jp      nz,NlsDrop
                ld      a,(NoteLayWnd)
                ld      (NlsSlot),a
                call    WndSelect
                ld      hl,(WinApp)
                ld      de,AppNote
                or      a
                sbc     hl,de
                jp      nz,NlsDrop
                ld      a,(WinW)
                sub     2
                ld      hl,NwWidth
                cp      (hl)
                jr      z,NlsStep
                call    NwStartFull             ; the width changed under it
NlsStep:
                call    NwStep
                jp      nz,NlsWait              ; more next frame
NlsDone:
                ld      hl,(NwWork)             ; a big step: the composition
                ld      de,NWSMALL+1            ; is the next frame's too
                or      a
                sbc     hl,de
                jp      nc,NlsWait
                jp      NlsCompose
NlsDrop:
                call    NwInvalidate
NlsScan:
                ld      a,(RepaintDue)
                or      a
                jp      z,NlsIdle
                call    NlsFind
                jp      nz,NlsIdle
                ld      a,(NlsSlot)
                call    WndSelect
                ld      a,(SetWrap)
                or      a
                jp      z,NlsCompose            ; WRAP off: composed afresh
                call    NwStale
                jp      z,NlsCompose            ; its table serves
                call    NwStartFull
                call    NwStep
                jp      nz,NlsWait
                jp      NlsDone
NlsCompose:
                call    WinDraw
                ld      a,(NlsSlot)
                call    WndSlotBit
                cpl
                ld      c,a
                ld      hl,WndDirty
                and     (hl)
                ld      (hl),a
                ld      a,c
                ld      hl,WndCapDirty
                and     (hl)
                ld      (hl),a
                call    NlsFind                 ; another one waiting?
                jp      z,NlsWait
NlsDecide:
                call    NlsRestore
                ld      a,(NlsSlot)
                ld      hl,NlsLive
                cp      (hl)
                jp      nz,NlsOne               ; two selects: enough for a frame
                ld      a,(RowsDesk)
                or      a
                jp      nz,NlsOne               ; the desktop under it too: next frame
                ld      a,(NwKeyed)
                cp      2
                jp      z,NlsOne                ; an edit's frame: ENTER into a full
                ld      hl,(NwWork)             ; line is 5 ms before any layout
                ld      de,NWSMALL+1
                or      a
                sbc     hl,de
                jp      nc,NlsOne
NlsIdle:
                xor     a
                ld      (NwToggled),a
NlsZero:
                xor     a
                ld      (NwKeyed),a
                ret
NlsNoWindow:
                ld      (NwKeyed),a
                ret
NlsWait:
                call    NlsRestore
NlsOne:
                xor     a
                ld      (NwKeyed),a
                inc     a
                ret
NlsRestore:
                ld      a,(NlsLive)
                cp      WNDMAX
                ret     nc
                jp      WndSelect

; Z with NlsSlot = the frontmost notepad whose buffer is to be
; recomposed, NZ when there is none.
NlsFind:
                ld      a,(WndCount)
                ld      b,a
                ld      hl,WndZ
NlfLoop:
                ld      a,(hl)
                ld      (NlsSlot),a
                push    hl
                push    bc
                call    WndSlotBit
                ld      hl,WndDirty
                and     (hl)
                jr      z,NlfNext
                ld      a,(NlsSlot)
                ld      hl,WndCur
                cp      (hl)
                ld      hl,(WinApp)
                jr      z,NlfApp
                call    WndRecAt
                ld      de,WR_APP
                add     hl,de
                ld      a,(hl)
                inc     hl
                ld      h,(hl)
                ld      l,a
NlfApp:
                ld      de,AppNote
                or      a
                sbc     hl,de
                jr      z,NlfFound
NlfNext:
                pop     bc
                pop     hl
                inc     hl
                djnz    NlfLoop
                or      1
                ret
NlfFound:
                pop     bc
                pop     hl
                xor     a
                ret

; Z when slot A is one of the open windows.
NlsOpen:
                ld      c,a
                ld      a,(WndCount)
                ld      b,a
                ld      hl,WndZ
NloLoop2:
                ld      a,(hl)
                cp      c
                ret     z
                inc     hl
                djnz    NloLoop2
                or      1
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
; Left of the interior the caret takes the first visible column, and
; NoteHScroll brings the earlier columns back. SCF + SBC: one borrow
; for cell - Win - 1, so a pointer above or left is not a large cell.
; With WRAP the rows are the table's screen rows (NoteMouseWrap).
NoteMousePosition:
                ld      a,(SetWrap)
                or      a
                jp      nz,NoteMouseWrap
                call    NoteMeasure
                ld      a,(PtrX)
                rrca
                rrca
                rrca
                and     31
                ld      hl,WinX
                scf
                sbc     a,(hl)
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
                scf
                sbc     a,(hl)
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

; The pointer's cell with WRAP: the screen row from the table, the
; column within the row's text (its end at most), as NoteGotoRow has
; it. Above the view the row before the top, below it the last in
; view, so a held button scrolls a screen row a frame.
NoteMouseWrap:
                call    NoteMeasureWrap
                ld      a,(PtrX)
                rrca
                rrca
                rrca
                and     31
                ld      hl,WinX
                scf
                sbc     a,(hl)
                jr      nc,NmwX
                xor     a
NmwX:
                ld      b,a
                ld      a,(WinW)
                sub     2
                cp      b
                jr      nc,NmwColumn
                ld      b,a
NmwColumn:
                ld      a,b
                ld      (NwCaretCol),a          ; the column wanted
                ld      a,(PtrY)
                rrca
                rrca
                rrca
                and     31
                ld      hl,WinY
                scf
                sbc     a,(hl)
                jr      nc,NmwY
                ld      a,(NoteTop)
                or      a
                jr      z,NmwRow
                dec     a
                jr      NmwRow
NmwY:
                ld      b,a
                ld      a,(WinH)
                sub     2                       ; one past the view: a held
                cp      b                       ; button scrolls a row a frame
                jr      nc,NmwInView
                ld      b,a
NmwInView:
                ld      a,(NoteTop)
                add     a,b
                ld      b,a
                ld      a,(NwTabN)
                dec     a
                cp      b
                jr      c,NmwRow                ; below the table: its last row
                ld      a,b
NmwRow:
                jp      NoteGotoRow
