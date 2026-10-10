; ============================================================
;  The correctness subjects, TEST=1 build only
;  The ZX harness called routines through a Python Z80; here the
;  machine is openMSX, so the subjects run on the ROM at boot and
;  leave their results in RAM for msxtest.py to read. Each
;  subject fills a fixed record; the harness knows the layout
;  from the symbols.
; ============================================================
IFDEF TEST
TestRun:
                call    TestHeap
                call    TestCalendar
                call    TestStorage
                call    TestBankSys
                call    TestBank
                call    TestApp
                call    TestHit
                call    TestDiskNames
                call    TestSettings
                call    TestTapeBuffer
                ld      a,1
                ld      (TestDone),a
                ret

; Pure buffering contract, no cassette BIOS needed on C-BIOS. The
; physical open/close path is exercised by note-tape on real ROMs.
TtResults       equ     CmdBuf+200      ; after the 43 bytes of TsetApplied
TestTapeBuffer:
                ld      hl,NoteBuf
                ld      b,0
TtPattern:
                ld      (hl),b
                inc     hl
                djnz    TtPattern
                ld      hl,TtName
                ld      b,FA_OVERWRITE
                call    TapeOpen
                ld      hl,NoteBuf
                ld      bc,257
                call    TapeWrite
                ld      de,TtResults
                call    TtRecord
                ld      a,1
                ld      hl,NoteBuf
                ld      bc,1
                call    TapeWrite
                ld      de,TtResults+3
                call    TtRecord
                xor     a                       ; invalid handle
                ld      bc,1
                call    TapeWrite
                ld      de,TtResults+6
                call    TtRecord
                ld      hl,NoteBuf
                ld      b,0
TtClear:
                ld      (hl),0
                inc     hl
                djnz    TtClear
                ld      a,FA_READ               ; simulate a loaded file
                ld      (TapeMode),a
                ld      hl,NoteBuf
                ld      bc,64
                call    TapeRead
                ld      de,TtResults+9
                call    TtRecord
                ld      a,1
                ld      hl,NoteBuf+64
                ld      bc,257
                call    TapeRead
                ld      de,TtResults+12
                call    TtRecord
                ld      a,1
                ld      bc,1
                call    TapeRead
                ld      de,TtResults+15
                call    TtRecord
                ld      a,1
                call    TapeClose
                ld      a,1                     ; read after close fails
                ld      bc,1
                call    TapeRead
                ld      de,TtResults+18
                jp      TtRecord
TtRecord:
                ld      a,0
                adc     a,0
                ld      (de),a
                inc     de
                ld      a,c
                ld      (de),a
                inc     de
                ld      a,b
                ld      (de),a
                ret
TtName:         defb    "BUFFER",0

; Settings round trip, rejected headers and payload clamping. Each
; snapshot is carry (0/1) followed by the eight record bytes before
; the desktop icons; the icons' round trip is a normal-build subject.
; Reuse unused tail of the commander scratch buffer in the TEST ROM.
TsetResults     equ     CmdBuf+120
TsetApplied     equ     CmdBuf+156
TestSettings:
                call    SetDefaults
                ld      a,ST_RAM
                ld      (SetBackend),a
                ld      a,2
                ld      (SetSpeed),a
                xor     a
                ld      (SetInvertY),a
                call    SetSave
                ld      de,TsetResults
                call    TsetSnapshot
                ld      hl,SetRec
                ld      b,SETSIZE
TsetClear:
                ld      (hl),0
                inc     hl
                djnz    TsetClear
                call    SetLoad
                ld      de,TsetResults+9
                call    TsetSnapshot
                xor     a
                ld      (SetRec),a
                call    SetWriteRecord
                call    SetLoad
                ld      de,TsetResults+18
                call    TsetSnapshot
                ld      a,99
                ld      (SetRec+1),a
                call    SetWriteRecord
                call    SetLoad
                ld      de,TsetResults+27
                call    TsetSnapshot
                ; Exercise all three table selections and mouse signs.
                xor     a
                ld      (SetSpeed),a
                call    SetApply
                ld      hl,(AccelPtr)
                ld      de,TsetApplied
                ld      bc,5
                ldir
                ld      a,1
                ld      (SetSpeed),a
                call    SetApply
                ld      hl,(AccelPtr)
                ld      de,TsetApplied+5
                ld      bc,5
                ldir
                ld      a,2
                ld      (SetSpeed),a
                call    SetApply
                ld      hl,(AccelPtr)
                ld      de,TsetApplied+10
                ld      bc,5
                ldir
                ld      a,1                     ; inverted: 13 comes back as 13
                ld      (SetInvertY),a
                ld      a,13
                call    SetMouseY
                ld      (TsetApplied+15),a
                xor     a
                ld      (SetInvertY),a
                ld      a,13
                call    SetMouseY
                ld      (TsetApplied+16),a
                ld      a,255
                ld      (SetSpeed),a
                ld      (SetInvertY),a
                ld      (SetBackend),a
                ld      (SetSound),a
                ld      (SetKeyPtr),a
                ld      (SetLattice),a
                ld      hl,DskTab               ; and every icon byte: present
                ld      b,DSKTABSZ              ; becomes 1, x and y are clamped
TsetDesk:                                       ; to the desktop
                ld      (hl),a
                inc     hl
                djnz    TsetDesk
                call    SetApply
                ld      hl,SetRec
                ld      de,TsetApplied+17
                ld      bc,SETSIZE
                ldir
                ; Leave a non-default, valid record on the disk image, with
                ; the default icons again: the clamp above left six at (30, 20).
                ld      a,ST_RAM
                ld      (SetBackend),a
                ld      a,2
                ld      (SetSpeed),a
                xor     a
                ld      (SetInvertY),a
                call    DskInit
                call    SetApply
                jp      SetSave
TsetSnapshot:
                ld      a,0
                adc     a,0
                ld      (de),a
                inc     de
                ld      hl,SetRec
                ld      bc,SETHDR
                ldir
                ret

; Three allocations, a payload scribbled over, a free, a free by
; owner. The harness walks the heap afterwards and reads the stats.
; Record: 3 pointers, then HeapStat (free, biggest) after the
; allocations, after the free, after the free by owner.
TestHeap:
                call    HeapInit
                ; Sizes that fit the smallest heap the build sees: the TEST
                ; build includes the tape buffer, the bank's transfer routine
                ; and directory cache, the desktop icons and the test records.
                ld      bc,160
                ld      a,$10
                call    HeapAlloc
                ld      (ThPtr),hl
                ld      bc,120
                ld      a,$11
                call    HeapAlloc
                ld      (ThPtr+2),hl
                ld      bc,96
                ld      a,$12
                call    HeapAlloc
                ld      (ThPtr+4),hl
                call    HeapStat
                ld      (ThStat),hl
                ld      (ThStat+2),de
                ; scribble over the second payload, the case the ZX
                ; version's free once healed by accident
                ld      hl,(ThPtr+2)
                ld      de,(ThPtr+2)
                inc     de
                ld      (hl),$A5
                ld      bc,119
                ldir
                ld      hl,(ThPtr+2)
                call    HeapFree
                call    HeapStat
                ld      (ThStat+4),hl
                ld      (ThStat+6),de
                ld      a,$12
                call    HeapFreeOwner
                call    HeapStat
                ld      (ThStat+8),hl
                ld      (ThStat+10),de
                ret

; Every month from 1980 to 2079: the weekday of the 1st and the
; length, 2,400 bytes the harness compares with Python's calendar.
; Then August 2026 as the grid rows, and the selection walking
; back over the 1st into July.
TestCalendar:
                ld      hl,TcMonths
                xor     a
                ld      (CalYear),a
TcYear:
                xor     a
                ld      (CalMonth),a
TcMonth:
                push    hl
                call    CalFirstDow
                pop     hl
                ld      a,(CalDow)
                ld      (hl),a
                inc     hl
                ld      a,(CalMonth)
                push    hl
                call    CalMonthLen
                pop     hl
                ld      (hl),a
                inc     hl
                ld      a,(CalMonth)
                inc     a
                ld      (CalMonth),a
                cp      12
                jr      c,TcMonth
                ld      a,(CalYear)
                inc     a
                ld      (CalYear),a
                cp      CALYEARS
                jr      c,TcYear
                ; August 2026
                ld      a,46
                ld      (CalYear),a
                ld      a,7
                ld      (CalMonth),a
                call    CalPrepare
                call    CalBuildHead
                ld      hl,CalRow
                ld      de,TcGrid
                ld      bc,9
                ldir
                ld      de,TcGrid+9
                xor     a
TcWeek:
                ld      (CalIx),a
                push    de
                call    CalBuildWeek
                pop     de
                ld      hl,CalRow
                ld      bc,21
                ldir
                ld      a,(CalIx)
                inc     a
                cp      6
                jr      c,TcWeek
                ; a step back from the 1st, then forward, then ENTER
                ld      a,1
                ld      (CalSel),a
                ld      a,KEY_LEFT
                call    CalKey
                ld      hl,CalYear
                ld      de,TcSel
                ld      bc,3
                ldir
                ld      a,KEY_RIGHT
                call    CalKey
                ld      a,KEY_ENTER
                call    CalKey
                ld      hl,TodayY
                ld      de,TcSel+3
                ld      bc,3
                ldir
                ; and midnight on the 31st
                ld      a,31
                ld      (TodayD),a
                call    CalNextDay
                ld      hl,TodayY
                ld      de,TcSel+6
                ld      bc,3
                ldir
                ; months and years: 31 Jan 1980, a month on is cut to
                ; 29 Feb, a year on to 28 Feb 1981, two months back
                ; is December 1980; both ends of the range stop
                ld      hl,TcStep
                ld      (TcStepPtr),hl
                ld      bc,0
                ld      a,31
                call    TcSetCal
                ld      a,'.'
                call    TcStepKey
                ld      a,'>'
                call    TcStepKey
                ld      a,','
                call    CalKey
                ld      a,','
                call    TcStepKey
                ld      bc,0
                ld      a,5
                call    TcSetCal
                ld      a,','
                call    CalKey
                ld      a,'<'
                call    TcStepKey
                ld      bc,+(CALYEARS-1)*256+11
                ld      a,31
                call    TcSetCal
                ld      a,'.'
                call    CalKey
                ld      a,'>'
                jp      TcStepKey
; B = year, C = month, A = day
TcSetCal:
                ld      (CalSel),a
                ld      a,b
                ld      (CalYear),a
                ld      a,c
                ld      (CalMonth),a
                ret
; A key to the calendar, then its state appended at TcStepPtr
TcStepKey:
                call    CalKey
                ld      hl,CalYear
                ld      de,(TcStepPtr)
                ld      bc,3
                ldir
                ld      (TcStepPtr),de
                ret

; A file written, closed, reopened and read back; then the
; directory, a delete, and the fifth open that must fail.
; Record: wrote, read, bad, err, dir count, delete result, full err.
TestStorage:
                xor     a
                ld      (TsErrCode),a
                call    StInit
                ld      hl,TsSrc                ; 64 bytes of pattern
                ld      b,64
                ld      a,1
TsFill:
                ld      (hl),a
                inc     hl
                add     a,7
                djnz    TsFill
                ld      hl,TxtTsName
                ld      b,FA_OVERWRITE
                call    StOpen
                jp      c,TsErr
                ld      (TsHandle),a
                ld      hl,TsSrc
                ld      bc,64
                call    StWrite
                ld      (TsWrote),bc
                ld      a,(TsHandle)
                call    StClose
                ld      hl,TxtTsName
                ld      b,FA_READ
                call    StOpen
                jp      c,TsErr
                ld      (TsHandle),a
                ld      hl,TsDst
                ld      bc,64
                call    StRead
                ld      (TsRead),bc
                ld      hl,TsSrc
                ld      de,TsDst
                ld      b,64
                ld      c,0
TsCmp:
                ld      a,(de)
                cp      (hl)
                jr      z,TsSame
                inc     c
TsSame:
                inc     hl
                inc     de
                djnz    TsCmp
                ld      a,c
                ld      (TsBad),a
                ; three more files, then the directory has four
                ld      hl,TxtTsName2
                ld      b,FA_OVERWRITE
                call    StOpen
                ld      hl,TxtTsName3
                ld      b,FA_OVERWRITE
                call    StOpen
                ld      hl,TxtTsName4
                ld      b,FA_OVERWRITE
                call    StOpen
                xor     a
                ld      (TsDirN),a
TsDir:
                ld      a,(TsDirN)
                call    StDir                   ; clobbers BC
                jr      c,TsDirDone
                ld      hl,TsDirN
                inc     (hl)
                jr      TsDir
TsDirDone:
                ld      hl,TxtTsName5
                ld      b,FA_OVERWRITE
                call    StOpen
                ld      a,0
                jr      nc,TsNotFull
                ld      a,STERR_FULL
TsNotFull:
                ld      (TsFull),a
                ld      hl,TxtTsName3
                call    StDelete
                ld      a,0
                jr      nc,TsDeleted
                inc     a
TsDeleted:
                ld      (TsDel),a
                ld      a,3
                call    StDir                   ; the fourth file is gone
                ld      a,0
                jr      nc,TsDir3
                inc     a
TsDir3:
                ld      (TsDirAfter),a
                ret
TsErr:
                ld      (TsErrCode),a
                ret

TxtTsName:      defb    "SETTINGS",0
TxtTsName2:     defb    "NOTE1",0
TxtTsName3:     defb    "NOTE2",0
TxtTsName4:     defb    "NOTE3",0
TxtTsName5:     defb    "NOTE4",0

; ---- The bank. Records at the tail of CmdBuf, after the tape ones:
;   +0 wrote, +2 read, +4 bytes wrong, +5 open error, +6 files created
;   before the bank was full, +8 the error then, +9 directory count
;   (to 255), +10 delete result, +11 the entry the next file took,
;   +12 that entry's name from StDir, +25 bytes wrong in a full file
;   read back, +26 files the bank-sys subject wrote
TbResults       equ     CmdBuf+221              ; after the 21 bytes of TtResults
TbName          equ     CmdWork                 ; "F0000" and so on

; A hundred files of 256 bytes written with interrupts off throughout:
; the routine's EI is a NOP while this runs, so the harness can
; compare the BIOS work area and the ROM before and after without a
; frame counter moving between the two snapshots.
TestBankSys:
                call    BkPresent
                ret     z
                call    StIdent
                push    af
                ld      a,ST_BANK
                call    StSelect
                xor     a
                ld      (TbResults+26),a
                ld      (BkCode+BkOEi),a        ; NOP for EI
                di
TbsStart:
                ld      b,100
TbsLoop:
                push    bc
                ld      a,100
                sub     b
                call    TbSetName
                ld      a,'S'                   ; "S0000".."S0099"
                ld      (TbName),a
                call    TbWriteFull
                pop     bc
                jr      c,TbsEnd
                ld      hl,TbResults+26
                inc     (hl)
                djnz    TbsLoop
TbsEnd:
                ld      a,$FB                   ; EI again
                ld      (BkCode+BkOEi),a
                ei
                pop     af
                jp      StSelect

; A 64 byte round trip, the bank filled until STERR_FULL, the
; directory counted, a delete, the slot reused, a full file read back.
TestBank:
                call    BkPresent
                ret     z
                call    StIdent
                push    af
                ld      a,ST_BANK
                call    StSelect
                ld      hl,TsSrc                ; 64 bytes of pattern
                ld      b,64
                ld      a,1
TbFill:
                ld      (hl),a
                inc     hl
                add     a,7
                djnz    TbFill
                ld      hl,TxtTbName
                ld      b,FA_OVERWRITE
                call    StOpen
                jp      c,TbErr
                ld      (TsHandle),a
                ld      hl,TsSrc
                ld      bc,64
                call    StWrite
                ld      (TbResults),bc
                ld      a,(TsHandle)
                call    StClose
                ld      hl,TxtTbName
                ld      b,FA_READ
                call    StOpen
                jp      c,TbErr
                ld      (TsHandle),a
                ld      hl,TsDst
                ld      bc,64
                call    StRead
                ld      (TbResults+2),bc
                ld      a,(TsHandle)
                call    StClose
                ld      hl,TsSrc
                ld      de,TsDst
                ld      b,64
                call    TbCompare
                ld      (TbResults+4),a
                ; files F0000 onwards, each a full block, until it is full
                ld      hl,0
                ld      (TbResults+6),hl
TbMore:
                call    TbSetNameHL             ; HL = the file number, past 255 on a mapper
                call    TbWriteFull
                jr      c,TbFull
                ld      hl,(TbResults+6)
                inc     hl
                ld      (TbResults+6),hl
                jr      TbMore
TbFull:
                ld      (TbResults+8),a
                ; the directory, to 255
                xor     a
TbDir:
                push    af
                call    StDir
                pop     bc
                ld      a,b
                jr      c,TbDirDone
                inc     a
                jr      nz,TbDir
                dec     a
TbDirDone:
                ld      (TbResults+9),a
                ld      hl,TxtTbName3
                call    StDelete
                ld      a,0
                jr      nc,TbDeleted
                inc     a
TbDeleted:
                ld      (TbResults+10),a
                ld      hl,TxtTbNew
                ld      b,FA_OVERWRITE
                call    StOpen
                ld      hl,(BkEntry)
                ld      a,l
                ld      (TbResults+11),a
                ld      a,1
                call    StClose
                ld      a,(TbResults+11)
                call    StDir                   ; the entry F0002 had
                ld      de,TbResults+12
                ld      bc,13
                ldir
                ; F0001 again, every byte, into the RAM backend's third
                ; chunk: nothing reads that after TestStorage
                ld      a,1
                call    TbSetName
                ld      a,(TbName+4)
                call    TbPattern
                ld      hl,TbName
                ld      b,FA_READ
                call    StOpen
                jp      c,TbErr
                ld      hl,RamHeap+512
                ld      bc,256
                call    StRead
                ld      a,1
                call    StClose
                ld      hl,NoteBuf
                ld      de,RamHeap+512
                ld      b,0
                call    TbCompare
                ld      (TbResults+25),a
                pop     af
                jp      StSelect
TbErr:
                ld      (TbResults+5),a
                pop     af
                jp      StSelect

; A (or HL) = the file number: TbName = "Fnnnn".
TbSetName:
                ld      l,a
                ld      h,0
TbSetNameHL:
                ld      ix,TbName+1
                ld      de,1000
                call    CdsDigit
                ld      de,100
                call    CdsDigit
                ld      de,10
                call    CdsDigit
                ld      de,1
                call    CdsDigit
                ld      (ix+0),0
                ld      a,'F'
                ld      (TbName),a
                ret

; NoteBuf = the pattern for file number A: byte n is n xor the number.
TbPattern:
                ld      hl,NoteBuf
                ld      b,0
                ld      c,a
                ld      e,0
TbPatLoop:
                ld      a,e
                xor     c
                ld      (hl),a
                inc     hl
                inc     e
                djnz    TbPatLoop
                ret

; The file TbName created with its 256 byte pattern. Carry with the
; error code when it could not be opened, or when the write or the
; close went wrong.
TbWriteFull:
                ld      a,(TbName+4)            ; the units digit, enough to vary
                call    TbPattern
                ld      hl,TbName
                ld      b,FA_OVERWRITE
                call    StOpen
                ret     c
                ld      hl,NoteBuf
                ld      bc,256
                call    StWrite
                ret     c
                ld      a,b
                dec     a
                or      c
                jr      nz,TbWriteShort
                ld      a,1
                jp      StClose
TbWriteShort:
                ld      a,STERR_FULL+100        ; not a code a backend returns
                scf
                ret

; B bytes (0 for 256) at HL and DE compared: A = how many differ.
TbCompare:
                ld      c,0
TbCmpLoop:
                ld      a,(de)
                cp      (hl)
                jr      z,TbCmpSame
                inc     c
TbCmpSame:
                inc     hl
                inc     de
                djnz    TbCmpLoop
                ld      a,c
                ret

TxtTbName:      defb    "BANKTEST",0
TxtTbName3:     defb    "F0002",0
TxtTbNew:       defb    "NEW",0

; The calendar as the live application: a state block from the heap,
; init from today, a save, a change, a load that undoes it.
; Record: descriptor address, state after init, after load.
TestApp:
                ld      a,APP_CAL
                call    AppAt
                ld      (TaDesc),hl
                ld      (WinApp),hl
                ld      bc,CALSTSZ
                ld      a,HEAPOWN_WIN
                call    HeapAlloc
                ld      (WinStateP),hl
                ld      a,46
                ld      (TodayY),a
                ld      a,7
                ld      (TodayM),a
                ld      a,30
                ld      (TodayD),a
                ld      c,APP_INIT
                call    AppCall
                ld      hl,CalState
                ld      de,TaState
                ld      bc,3
                ldir
                call    AppSave
                ld      a,5
                ld      (CalMonth),a
                ld      a,9
                ld      (CalSel),a
                call    AppLoad
                ld      hl,CalState
                ld      de,TaState+3
                ld      bc,3
                ldir
                ret

; Cells on the bar, the desktop, the status band and past the edge.
; Record: five ids.
TestHit:
                call    CtlInit
                ld      hl,ThitRes
                ld      b,3
                ld      c,0
                call    ThitOne
                ld      b,15
                ld      c,11
                call    ThitOne
                ld      b,0
                ld      c,STATROW
                call    ThitOne
                ld      b,SCRCOLS
                ld      c,5
                call    ThitOne
                ; an open menu drop over the desktop
                ld      a,1
                ld      (MenuOpen),a
                ld      a,10
                ld      (MnX),a
                ld      a,1
                ld      (MnY),a
                ld      a,8
                ld      (MnW),a
                ld      a,4
                ld      (MnH),a
                ld      b,12
                ld      c,3
                call    ThitOne
                xor     a
                ld      (MenuOpen),a
                ret

; HitTest clobbers HL, so the record pointer is kept across it.
ThitOne:
                push    hl
                call    HitTest
                pop     hl
                ld      (hl),a
                inc     hl
                ret
; FCB parsing does not require a disk ROM. Use idle Commander scratch
; for the result records, avoiding permanent test-only work RAM.
TestDiskNames:
                ld      hl,TdnNames
                ld      de,CmdBuf
                ld      b,10
TdnLoop:
                push    bc
                push    de
                push    hl
                call    DskFcbName
                pop     hl
                pop     de
                ld      a,0
                adc     a,0
                ld      (de),a
                inc     de
                push    hl
                ld      hl,DskFcb+FCB_NAME
                ld      bc,11
                ldir
                pop     hl
TdnSkip:
                ld      a,(hl)
                inc     hl
                or      a
                jr      nz,TdnSkip
                pop     bc
                djnz    TdnLoop
                ret
TdnNames:       defb    "note",0,"note.txt",0,"abcdefgh.xyz",0
                defb    "ABCDEFGHI",0,".TXT",0,"A.BAKX",0,"A.B.C",0
                defb    "A?",0,"A/B",0,0

ENDIF
