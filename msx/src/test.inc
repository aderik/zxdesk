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
                call    TestApp
                call    TestHit
                ld      a,1
                ld      (TestDone),a
                ret

; Three allocations, a payload scribbled over, a free, a free by
; owner. The harness walks the heap afterwards and reads the stats.
; Record: 3 pointers, then HeapStat (free, biggest) after the
; allocations, after the free, after the free by owner.
TestHeap:
                call    HeapInit
                ; Sizes that fit the smallest heap the build sees: the TEST
                ; build's records leave 1,494 bytes under a disk ROM's HIMEM.
                ld      bc,512
                ld      a,$10
                call    HeapAlloc
                ld      (ThPtr),hl
                ld      bc,300
                ld      a,$11
                call    HeapAlloc
                ld      (ThPtr+2),hl
                ld      bc,128
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
                ld      bc,299
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
ENDIF
