; Notepad test. NoteKey is driven directly with key codes and the
; buffer is checked afterwards, with no screen involved at all, which
; is why the editing and the painting were kept in separate routines.
;
; The last two cases are the ones that matter most, because they are
; the paths a bad file takes: a byte that would index the font past
; $4000 and a row whose terminator has been overwritten.
BNoteTest:
                xor     a
                ld      (BNoteRes),a
                ld      a,ST_RAM                ; the tape test may have left
                call    StSelect                ; tape selected
                call    NoteNew

                ; 1: a fresh buffer is spaces
                ld      hl,NoteBuf
                ld      a,(hl)
                ld      c,' '
                call    BNoteEq
                ; 2: and the cursor is home
                ld      a,(NoteCX)
                ld      c,0
                call    BNoteEq
                ld      a,(NoteCY)
                ld      c,0
                call    BNoteEq

                ; 3: typing lands the character and advances
                ld      a,'A'
                call    NoteKey
                ld      hl,NoteBuf
                ld      a,(hl)
                ld      c,'A'
                call    BNoteEq
                ld      a,(NoteCX)
                ld      c,1
                call    BNoteEq

                ld      a,'B'
                call    NoteKey
                ld      hl,NoteBuf+1
                ld      a,(hl)
                ld      c,'B'
                call    BNoteEq

                ; 4: left, then a character inserts rather than overwrites
                ld      a,KEY_LEFT
                call    NoteKey
                ld      a,'X'
                call    NoteKey
                ld      hl,NoteBuf+1
                ld      a,(hl)
                ld      c,'X'
                call    BNoteEq
                ld      hl,NoteBuf+2
                ld      a,(hl)
                ld      c,'B'                   ; pushed right, not replaced
                call    BNoteEq

                ; 5: delete pulls it back
                ld      a,KEY_DELETE
                call    NoteKey
                ld      hl,NoteBuf+1
                ld      a,(hl)
                ld      c,'B'
                call    BNoteEq
                ld      a,(NoteCX)
                ld      c,1
                call    BNoteEq

                ; 6: enter goes down and home
                ld      a,KEY_ENTER
                call    NoteKey
                ld      a,(NoteCY)
                ld      c,1
                call    BNoteEq
                ld      a,(NoteCX)
                ld      c,0
                call    BNoteEq
                ld      a,'C'
                call    NoteKey
                ld      hl,NoteBuf+NOTESTRIDE
                ld      a,(hl)
                ld      c,'C'
                call    BNoteEq

                ; 7: a save and load round trip through the storage layer
                call    NoteSave
                jr      c,BntNoSave
                ld      hl,NoteBuf+NOTESTRIDE
                ld      (hl),'?'                ; wipe it in memory
                ld      hl,NoteBuf
                ld      (hl),'?'
                call    NoteLoad
                jr      c,BntNoSave
                ld      hl,NoteBuf
                ld      a,(hl)
                ld      c,'A'
                call    BNoteEq
                ld      hl,NoteBuf+NOTESTRIDE
                ld      a,(hl)
                ld      c,'C'
                call    BNoteEq
BntNoSave:

                ; 8: the two things that stop a bad file hurting the printer.
                ; A byte of 200 would fetch its glyph from $4000 upwards and
                ; render screen memory as a letterform, and a row with no
                ; terminator walks PrintStrPx2 off the end of the buffer.
                ld      hl,NoteBuf+2
                ld      (hl),200
                ld      hl,NoteBuf+NOTECOLS
                ld      (hl),'!'                ; clobber the terminator
                call    NoteSanitise
                ld      hl,NoteBuf+2
                ld      a,(hl)
                ld      c,' '
                call    BNoteEq
                ld      hl,NoteBuf+NOTECOLS
                ld      a,(hl)
                ld      c,0
                call    BNoteEq

                call    NoteNew
                ret

; A = actual, C = expected. Counts a pass and nothing else, so a
; failure shows up as a number below BNOTEN rather than as a hang.
BNoteEq:
                cp      c
                ret     nz
                ld      a,(BNoteRes)
                inc     a
                ld      (BNoteRes),a
                ret

BNOTEN          equ     17

BNoteRes:       defb    0
