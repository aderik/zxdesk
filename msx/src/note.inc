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

; Count logical rows and map the chunk cursor to a logical cell.
NoteMeasure:
                ld      hl,NoteBuf+15
                ld      b,NOTEROWS
                ld      c,0                     ; logical row
                ld      d,0                     ; logical column of chunk
                ld      e,0                     ; physical chunk
NmLoop:
                ld      a,(NoteCY)
                cp      e
                jr      nz,NmNext
                ld      a,(NoteCX)
                add     a,d
                ld      (NoteCursorX),a
                ld      a,c
                ld      (NoteCursorY),a
NmNext:
                ld      a,(hl)
                or      a
                jr      z,NmBreak
                ld      a,d
                add     a,NOTECOLS
                ld      d,a
                jr      NmAdvance
NmBreak:
                inc     c
                ld      d,0
NmAdvance:
                push    de
                ld      de,NOTESTRIDE
                add     hl,de
                pop     de
                inc     e
                djnz    NmLoop
                ld      a,c
                ld      (NoteRows),a
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
                call    NoteMeasure
                call    NoteHScroll
                ld      a,(NoteTop)
                call    NoteScrollTo
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
                ld      a,1
                ld      (NoteDirty),a
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
                call    NoteSave
                jr      c,NoteSaveAlert
NcaGo:
                ld      a,1
                ld      (WndForce),a
                jp      WndClose
NoteSaveAlert:
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
                or      a
                jp      nz,NoteSaveAlert
                ret

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
