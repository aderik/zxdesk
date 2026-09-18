IFDEF ESXTEST
; ============================================================
;  E2 acceptance test
;  Built with ESXTEST=1. It began as a probe for whether `RST
;  $08` could be made at all from this program, which it can, and
;  is now the acceptance test for the backend that came out of
;  that: it drives esxdos.inc through E1's vectors rather than
;  calling esxDOS directly, so what it proves is the thing
;  applications will actually use.
;
;  **Load it from the card, never from tape.** esxDOS's API only
;  works for a program esxDOS itself launched: the copier in the
;  EEPROM pages the DivMMC in and out around every byte it
;  fetches, using bookkeeping that only the card launch path sets
;  up. From tape both of those bytes are zero, the copy is
;  garbage, and every call carrying a filename fails with
;  whatever the garbage parsed as. That cost a day to find.
;  `./esxdos.sh` drives the loop and docs/esxdos.md has the rest.
;
;  Read down the screen. Carry set means the number is an error
;  from E1 rather than a result.
;
;    SELECT  StSelect on ST_ESXDOS. If this fails the registry
;            row is wrong and nothing below it means anything.
;    OPEN    StOpen on README.TXT, which the card root has.
;    READ    StRead of eight bytes, and BC back.
;    HEX     what landed in the buffer.
;    TEXT    the same as characters. README.TXT begins "esxDOS
;            v0.8.9", so this line is the one that says the whole
;            path worked rather than merely returned.
;    CLOSE   StClose, which should be quiet.
;    DIR0    StDir on entry nought: its length in hex, because a
;            file here is bigger than three digits.
;    NAME    that entry's name, which proves the walk across
;            esxDOS's iterator lands on a real file.
; ============================================================
ETROW_SELECT    equ     4
ETROW_OPEN      equ     6
ETROW_READ      equ     8
ETROW_HEX       equ     10
ETROW_TEXT      equ     12
ETROW_CLOSE     equ     14
ETROW_DIR       equ     16
ETROW_NAME      equ     18
ETROW_RT        equ     20

ESXPROBE        equ     8               ; bytes read, and the width of the
                                        ; hex line at three columns each

EsxTestMain:
                di
                ld      sp,STACKTOP
                call    DetectMachine
                call    SetupIM2
                call    BuildScrTab
                ei

                ld      b,0
EtClear:
                push    bc
                ld      a,b
                call    ClearRow
                pop     bc
                inc     b
                jr      nz,EtClear

                ld      hl,EtT1
                ld      b,0
                ld      c,0
                call    PrintStr
                ld      hl,EtT2
                ld      b,2
                ld      c,0
                call    PrintStr

                ; ----------------------------------------------
                ;  Select the backend, then use it through E1
                ;  alone. Nothing below this line names esxDOS.
                ; ----------------------------------------------
                ld      a,ST_ESXDOS
                call    StSelect
                push    af
                ld      a,(StBackend)
                ld      b,ETROW_SELECT
                ld      hl,EtLSel
                call    EtReport
                pop     af
                jp      c,EtDone

                ld      hl,EtName
                ld      b,FA_READ
                call    StOpen
                ld      (EtHandle),a
                push    af
                ld      b,ETROW_OPEN
                ld      hl,EtLOpen
                call    EtReport
                pop     af
                jp      c,EtDir         ; no handle, but the directory
                                        ; walk does not need one

                ld      a,(EtHandle)
                ld      hl,EtBuf
                ld      bc,ESXPROBE
                call    StRead
                push    af
                ld      a,c             ; the count back, low byte, which
                ld      b,ETROW_READ    ; is all eight bytes needs
                ld      hl,EtLRead
                call    EtReport
                pop     af

                call    EtDumpBuf

                ld      a,(EtHandle)
                call    StClose
                ld      b,ETROW_CLOSE
                ld      hl,EtLClose
                call    EtReport

                ; ----------------------------------------------
                ;  StDir, where E1 and esxDOS disagree most: an
                ;  index against an iterator.
                ; ----------------------------------------------
EtDir:
                xor     a
                call    StDir
                jr      c,EtDirBad
                push    hl                      ; the name
                ex      de,hl                   ; HL = the length
                push    hl
                ld      hl,EtLDir
                ld      b,ETROW_DIR
                ld      c,0
                call    PrintStr
                pop     hl
                ld      b,ETROW_DIR
                ld      c,8
                call    EtPrintWord
                ld      hl,EtLName
                ld      b,ETROW_NAME
                ld      c,0
                call    PrintStr
                pop     hl
                ld      b,ETROW_NAME
                ld      c,8
                call    PrintStr
                jp      EtRound
EtDirBad:
                ld      b,ETROW_DIR
                ld      hl,EtLDir
                call    EtReport

                ; ----------------------------------------------
                ;  The round trip, which is the only way to
                ;  exercise StWrite and StDelete without leaving
                ;  anything behind. Create, write, close, reopen,
                ;  read back, compare, delete, and confirm it has
                ;  gone. One line, because every step of it has
                ;  to pass for the line to say OK, and any one
                ;  failing is the same answer: the backend is not
                ;  finished.
                ; ----------------------------------------------
EtRound:
                ld      hl,EtScratch
                ld      b,FA_OVERWRITE
                call    StOpen
                jp      c,EtRtBad
                ld      (EtHandle),a
                ld      hl,EtT1                 ; any eight bytes we know
                ld      bc,ESXPROBE
                ld      a,(EtHandle)
                call    StWrite
                jp      c,EtRtBad
                ld      a,(EtHandle)
                call    StClose
                jp      c,EtRtBad

                ld      hl,EtScratch            ; read it back
                ld      b,FA_READ
                call    StOpen
                jp      c,EtRtBad
                ld      (EtHandle),a
                ld      hl,EtBuf
                ld      bc,ESXPROBE
                ld      a,(EtHandle)
                call    StRead
                jp      c,EtRtBad
                ld      a,(EtHandle)
                call    StClose

                ld      hl,EtT1                 ; and compare
                ld      de,EtBuf
                ld      b,ESXPROBE
EtRtCmp:
                ld      a,(de)
                cp      (hl)
                jp      nz,EtRtBad
                inc     hl
                inc     de
                djnz    EtRtCmp

                ld      hl,EtScratch            ; then take it away again
                call    StDelete
                jp      c,EtRtBad
                ld      hl,EtScratch
                ld      b,FA_READ
                call    StOpen                  ; which must now fail
                jp      nc,EtRtBad

                xor     a
                ld      b,ETROW_RT
                ld      hl,EtLRound
                call    EtReport
                jp      EtDone
EtRtBad:
                scf
                ld      b,ETROW_RT
                ld      hl,EtLRound
                call    EtReport
EtDone:
                jr      EtDone

; ------------------------------------------------------------
;  Reporting
; ------------------------------------------------------------
; HL = label, B = row, A = value, carry = the call's carry.
; Prints the label, the value as three digits, and OK or ERR.
EtReport:
                push    af              ; the value and the carry, both of
                ld      a,b             ; which have to survive to the end
                ld      (EtRow),a
                ld      c,0
                call    PrintStr        ; B is still the row
                pop     af
                push    af
                ld      hl,EtRow
                ld      b,(hl)
                ld      c,8
                call    PrintDec3
                pop     af
                ld      hl,EtOK
                jr      nc,EtRepGo
                ld      hl,EtErr
EtRepGo:
                ld      a,(EtRow)
                ld      b,a
                ld      c,12
                jp      PrintStr

; The buffer as hex and as characters.
EtDumpBuf:
                ld      hl,EtLHex
                ld      b,ETROW_HEX
                ld      c,0
                call    PrintStr
                ld      hl,EtBuf
                ld      d,ESXPROBE
                ld      e,8             ; first column
EtHexLoop:
                push    de
                ld      a,(hl)
                push    hl
                ld      b,ETROW_HEX
                ld      c,e
                call    EtPrintHex
                pop     hl
                pop     de
                inc     hl
                inc     e
                inc     e
                inc     e
                dec     d
                jr      nz,EtHexLoop

                ld      hl,EtLText
                ld      b,ETROW_TEXT
                ld      c,0
                call    PrintStr
                ld      hl,EtBuf
                ld      d,ESXPROBE
                ld      e,8
EtTxtLoop:
                push    de
                ld      a,(hl)
                cp      32              ; anything unprintable shows as a dot
                jr      c,EtTxtDot      ; rather than as whatever the font
                cp      127             ; has at that code
                jr      c,EtTxtGo
EtTxtDot:
                ld      a,'.'
EtTxtGo:
                push    hl
                ld      b,ETROW_TEXT
                ld      c,e
                call    PrintChar
                pop     hl
                pop     de
                inc     hl
                inc     e
                dec     d
                jr      nz,EtTxtLoop
                ret

; HL = value, B = row, C = column. Four hex digits.
EtPrintWord:
                push    hl
                ld      a,h
                call    EtPrintHex
                pop     hl
                push    bc
                inc     c
                inc     c
                ld      a,l
                call    EtPrintHex
                pop     bc
                ret

; A = byte, B = row, C = column. Two hex digits.
; BC is saved across the first digit rather than assumed intact:
; PrintChar's contract does not promise C back, and the second
; digit needs the column.
EtPrintHex:
                push    bc
                push    af
                rrca
                rrca
                rrca
                rrca
                call    EtNibble
                pop     af
                pop     bc
                push    bc
                inc     c
                call    EtNibble
                pop     bc
                ret

EtNibble:
                and     $0F
                add     a,'0'
                cp      '9'+1
                jr      c,EtNibGo
                add     a,7             ; 'A' is seven past '9'
EtNibGo:
                jp      PrintChar

EtT1:           defb    "E2 ACCEPTANCE",0
EtT2:           defb    "LOAD FROM THE CARD, NOT TAPE",0
EtLSel:         defb    "SELECT",0
EtLOpen:        defb    "OPEN",0
EtLRead:        defb    "READ",0
EtLHex:         defb    "HEX",0
EtLText:        defb    "TEXT",0
EtLClose:       defb    "CLOSE",0
EtLDir:         defb    "DIR0",0
EtLName:        defb    "NAME",0
EtOK:           defb    "OK ",0
EtErr:          defb    "ERR",0
EtName:         defb    "README.TXT",0
EtScratch:      defb    "E2SCRTCH.TMP",0
EtLRound:       defb    "RTRIP",0

EtHandle:       defb    0
EtRow:          defb    0
EtBuf:          defs    ESXPROBE
ENDIF
