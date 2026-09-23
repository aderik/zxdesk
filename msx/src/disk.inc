; ============================================================
;  Disk backend: MSX-DOS through the BDOS entry
;  The disk ROM, initialised before this cartridge because it
;  sits in a lower slot, leaves a JP at $F37D and its work area
;  under HIMEM. Every call here goes through that jump with an
;  FCB in RAM: open, create, delete, close, random block read and
;  write with a record size of one byte so a read is a byte
;  count, and search first/next for the directory. Names are
;  normalised as uppercase 8.3 names, including the extension.
;
;  The BDOS pages the disk ROM into page 1 for the duration of a
;  call and pages this cartridge back before returning, so the
;  code may live in page 1 as long as everything it hands the
;  BDOS is in RAM.
; ============================================================
BDOS            equ     $F37D
HIMEM           equ     $FC4A
FCBSIZE         equ     37
FCB_NAME        equ     1
FCB_RECSZ       equ     14
FCB_RNDREC      equ     33
DIR_NAME        equ     1               ; the search result at the DTA: a drive
DIR_SIZE        equ     29              ; byte, then the 32 byte directory entry
BD_OPEN         equ     $0F
BD_CLOSE        equ     $10
BD_SFIRST       equ     $11
BD_SNEXT        equ     $12
BD_DELETE       equ     $13
BD_CREATE       equ     $16
BD_SETDTA       equ     $1A
BD_RNDWRITE     equ     $26
BD_RNDREAD      equ     $27

; Is there a disk system? The BDOS jump is only planted by a disk
; ROM's init. Z set when there is one.
DskPresent:
                ld      a,(BDOS)
                cp      $C3
                ret

; Clear the FCB, with a space-padded 8+3 name field.
DskFcbClear:
                ld      hl,DskFcb
                ld      de,DskFcb+1
                ld      bc,FCBSIZE-1
                ld      (hl),0
                ldir
                ld      hl,DskFcb+FCB_NAME
                ld      b,11
DfcPad:
                ld      (hl),' '
                inc     hl
                djnz    DfcPad
                ret

; HL = NAME.EXT, up to 8+3 characters. Never truncate to another
; file's name and never accept wildcard/path characters for mutations.
DskFcbName:
                push    hl
                call    DskFcbClear
                pop     hl
                ld      de,DskFcb+FCB_NAME
                ld      b,8
                ld      c,0
DfnBase:
                ld      a,(hl)
                or      a
                jr      z,DfnEnd
                cp      '.'
                jr      z,DfnDot
                call    DfnChar
                ret     c
                inc     hl
                jr      DfnBase
DfnDot:
                ld      a,c
                or      a
                jr      z,DfnBad
                inc     hl
                ld      de,DskFcb+FCB_NAME+8
                ld      b,3
                ld      c,0
DfnExt:
                ld      a,(hl)
                or      a
                jr      z,DfnEnd
                call    DfnChar
                ret     c
                inc     hl
                jr      DfnExt
DfnEnd:
                ld      a,c
                or      a
                ret     nz
DfnBad:
                ld      a,STERR_NAME
                scf
                ret
DfnChar:
                cp      'a'
                jr      c,DfnUpper
                cp      'z'+1
                jr      nc,DfnUpper
                sub     $20
DfnUpper:
                cp      '!'
                jr      c,DfnBad
                cp      127
                jr      nc,DfnBad
                push    hl
                ld      hl,TxtDskInvalid
DfnCheck:
                cp      (hl)
                jr      z,DfnInvalid
                inc     hl
                push    af
                ld      a,(hl)
                or      a
                jr      z,DfnValid
                pop     af
                jr      DfnCheck
DfnValid:
                pop     af
                pop     hl
                inc     b
                dec     b
                jr      z,DfnBad
                ld      (de),a
                inc     de
                dec     b
                inc     c
                or      a
                ret
DfnInvalid:
                pop     hl
                jr      DfnBad
TxtDskInvalid:  defb    34,"*+,./:;<=>?",91,92,93,"|",0

; A record is one byte, and the random record is DskPos.
DskFcbSetup:
                ld      hl,DskFcb+FCB_RECSZ
                ld      (hl),1
                inc     hl
                ld      (hl),0
                ret
DskFcbSeek:
                ld      hl,(DskPos)
                ld      (DskFcb+FCB_RNDREC),hl
                ld      hl,0
                ld      (DskFcb+FCB_RNDREC+2),hl
                ret

; HL = buffer for the next transfer.
DskSetDta:
                ex      de,hl
                ld      c,BD_SETDTA
                jp      BDOS

; HL = name, B = mode. Returns A = handle (always 1: one file open at
; a time, as the RAM backend), carry on error.
DskOpen:
                ld      a,b
                ld      (DskMode),a
                push    hl
                ld      de,DskName              ; kept: the FCB is rebuilt
                ld      bc,13                   ; after a delete
                ldir
                pop     hl
                call    DskFcbName
                jr      c,DoBadName
                ld      a,(DskMode)
                cp      FA_OVERWRITE
                jr      z,DoNew
                ld      de,DskFcb
                ld      c,BD_OPEN
                call    BDOS
                or      a
                jr      nz,DoNotFound
                jr      DoReady
DoNew:
                ld      de,DskFcb               ; whatever was there goes
                ld      c,BD_DELETE
                call    BDOS
                ld      hl,DskName              ; delete clears fields the
                call    DskFcbName              ; create wants blank
                ld      de,DskFcb
                ld      c,BD_CREATE
                call    BDOS
                or      a
                jr      nz,DoFull
DoReady:
                call    DskFcbSetup
                ld      hl,0
                ld      (DskPos),hl
                ld      a,1
                ld      (DskHandle),a
                or      a
                ret
DoBadName:
                ld      a,STERR_NAME
                scf
                ret
DoNotFound:
                ld      a,STERR_NOTFOUND
                scf
                ret
DoFull:
                ld      a,STERR_FULL
                scf
                ret

; A = handle.
DskClose:
                ld      de,DskFcb
                ld      c,BD_CLOSE
                call    BDOS
                or      a
                ret     z
                scf
                ret

; A = handle, HL = buffer, BC = count. Returns BC = bytes read.
DskRead:
                ld      (DskCnt),bc
                call    DskSetDta
                call    DskFcbSeek
                ld      hl,(DskCnt)
                ld      de,DskFcb
                ld      c,BD_RNDREAD
                call    BDOS                    ; HL = records read
                ld      b,h
                ld      c,l
                ld      hl,(DskPos)
                add     hl,bc
                ld      (DskPos),hl
                or      a
                ret

; A = handle, HL = buffer, BC = count. Returns BC = bytes written.
DskWrite:
                ld      (DskCnt),bc
                call    DskSetDta
                call    DskFcbSeek
                ld      hl,(DskCnt)
                ld      de,DskFcb
                ld      c,BD_RNDWRITE
                call    BDOS
                or      a
                jr      nz,DwFail
                ld      bc,(DskCnt)
                ld      hl,(DskPos)
                add     hl,bc
                ld      (DskPos),hl
                ret
DwFail:
                ld      bc,0
                ret

; A = the nth file, counting from nought. Returns HL = its name, DE =
; its length, carry set if there is no nth file. Search first, then
; next n times; the entry lands at the DTA, which is DskEnt here.
DskDirEnt:
                ld      (DskDirN),a
                call    DskFcbClear
                ld      hl,DskFcb+FCB_NAME
                ld      b,11
DdeWild:
                ld      (hl),'?'
                inc     hl
                djnz    DdeWild
                ld      hl,DskEnt
                call    DskSetDta
                ld      de,DskFcb
                ld      c,BD_SFIRST
                call    BDOS
                or      a
                jr      nz,DdeNone
DdeNext:
                ld      a,(DskEnt+12)          ; FAT attribute: no volume labels or folders
                and     $18
                jr      nz,DdeAdvance
                ld      a,(DskDirN)
                or      a
                jr      z,DdeFound
                dec     a
                ld      (DskDirN),a
DdeAdvance:
                ld      de,DskFcb
                ld      c,BD_SNEXT
                call    BDOS
                or      a
                jr      z,DdeNext
DdeNone:
                ld      a,STERR_NOTFOUND
                scf
                ret
DdeFound:
                ld      hl,DskEnt+DIR_NAME
                ld      de,DskEntName
                ld      b,8
                call    DdePart
                ld      hl,DskEnt+DIR_NAME+8
                ld      a,(hl)
                cp      ' '
                jr      z,DdeEnd
                ld      a,'.'
                ld      (de),a
                inc     de
                ld      b,3
                call    DdePart
DdeEnd:
                xor     a
                ld      (de),a
                ld      hl,(DskEnt+DIR_SIZE+2)
                ld      a,h
                or      l
                ld      hl,$FFFF                ; saturate sizes beyond the 16-bit API
                jr      nz,DdeSize
                ld      hl,(DskEnt+DIR_SIZE)
DdeSize:
                ex      de,hl
                ld      hl,DskEntName
                or      a
                ret

DdePart:
                ld      a,(hl)
                cp      ' '
                ret     z
                ld      (de),a
                inc     hl
                inc     de
                djnz    DdePart
                ret

; HL = a name. Carry set if it could not be removed.
DskDelete:
                call    DskFcbName
                ret     c
                ld      de,DskFcb
                ld      c,BD_DELETE
                call    BDOS
                or      a
                ret     z
                ld      a,STERR_NOTFOUND
                scf
                ret
