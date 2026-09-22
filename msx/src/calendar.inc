; ============================================================
;  Calendar
;  The arithmetic half of the ZX calendar, ported as it stands:
;  day of the week counted forward from 1 January 1980, a
;  Tuesday, because Zeller and Sakamoto both want division by 4,
;  100 and 400 and this has no divide instruction. 1980 to 2079,
;  where a leap year is exactly a year that divides by four. The
;  grid rows are built into CalRow as text; painting them into a
;  window is phase 3's, when there is a window.
; ============================================================
CALW            equ     23
CALH            equ     10              ; cells: title, header, labels, six weeks, bottom
CALYEAR0        equ     1980
CALYEARS        equ     100
CALSTSZ         equ     3               ; the year, the month and the selected day

; CalState is the live copy: CalYear, CalMonth, CalSel, contiguous
; in the var block.

CalInit:
                ld      a,(TodayY)
                ld      (CalYear),a
                ld      a,(TodayM)
                ld      (CalMonth),a
                ld      a,(TodayD)
                ld      (CalSel),a
                ret

CalSetLeap:
                ld      a,(CalYear)
                call    CalLeap
                ld      a,0
                jr      nz,CslNo
                inc     a
CslNo:
                ld      (CalLeapY),a
                ret

; The selection, forward and back a day, carrying into the next month
; and the next year.
CalFwd:
                call    CalSetLeap
                ld      a,(CalMonth)
                call    CalMonthLen
                ld      b,a
                ld      a,(CalSel)
                inc     a
                cp      b
                jr      z,CfSet
                jr      c,CfSet
                ld      a,1
                ld      (CalSel),a
                ld      hl,CalMonth
                inc     (hl)
                ld      a,(hl)
                cp      12
                ret     c
                ld      (hl),0
                ld      hl,CalYear
                inc     (hl)
                ld      a,(hl)
                cp      CALYEARS
                ret     c
                dec     (hl)                    ; the end of the range stops
                ld      a,11                    ; rather than wrapping
                ld      (CalMonth),a
                ld      a,31
CfSet:
                ld      (CalSel),a
                ret

CalBack:
                ld      a,(CalSel)
                dec     a
                jr      z,CbPrevMonth
                ld      (CalSel),a
                ret
CbPrevMonth:
                ld      a,(CalMonth)
                or      a
                jr      nz,CbSameYear
                ld      a,(CalYear)
                or      a
                ret     z                       ; the start of the range
                dec     a
                ld      (CalYear),a
                ld      a,12
CbSameYear:
                dec     a
                ld      (CalMonth),a
                call    CalSetLeap
                ld      a,(CalMonth)
                call    CalMonthLen
                ld      (CalSel),a
                ret

; B times, C = 1 forward, 0 back.
CalMoveB:
                push    bc
                ld      a,c
                or      a
                jr      z,CmbBack
                call    CalFwd
                jr      CmbNext
CmbBack:
                call    CalBack
CmbNext:
                pop     bc
                djnz    CalMoveB
                ret

; in:  A = years since 1980
; out: Z set if it is a leap year
CalLeap:
                and     3
                ret

; in:  A = month 0..11, CalLeapY = whether the year is a leap
; out: A = days in it
CalMonthLen:
                ld      e,a
                ld      d,0
                ld      hl,CalLens
                add     hl,de
                ld      a,(hl)
                ld      b,a
                ld      a,e
                cp      1                       ; February
                ld      a,b
                ret     nz
                ld      a,(CalLeapY)
                or      a
                ld      a,b
                ret     z
                inc     a
                ret

CalLens:        defb    31,28,31,30,31,30,31,31,30,31,30,31

; in:  CalYear, CalMonth
; out: CalDow = weekday of the 1st, Monday nought; CalLeapY set
CalFirstDow:
                call    CalSetLeap
                ld      a,1                     ; 1 Jan 1980 was a Tuesday
                ld      (CalDow),a
                xor     a
                ld      (CalIx),a
CfdYear:
                ld      a,(CalIx)
                ld      hl,CalYear
                cp      (hl)
                jr      z,CfdMonths
                call    CalLeap
                ld      b,1
                jr      nz,CfdNotL2
                inc     b                       ; 366 is 2 modulo 7
CfdNotL2:
                ld      a,(CalDow)
                add     a,b
                call    CalMod7
                ld      (CalDow),a
                ld      hl,CalIx
                inc     (hl)
                jr      CfdYear
CfdMonths:
                xor     a
                ld      (CalIx),a
CfdMonth:
                ld      a,(CalIx)
                ld      hl,CalMonth
                cp      (hl)
                ret     z
                call    CalMonthLen
                ld      b,a
                ld      a,(CalDow)
                add     a,b
                call    CalMod7
                ld      (CalDow),a
                ld      hl,CalIx
                inc     (hl)
                jr      CfdMonth

CalMod7:
                cp      7
                ret     c
                sub     7
                jr      CalMod7

; Midnight. Rolls today's date with today's year's month lengths.
CalNextDay:
                ld      a,(TodayY)
                call    CalLeap
                ld      a,0
                jr      nz,CndNotLeap
                inc     a
CndNotLeap:
                ld      (CalLeapY),a
                ld      a,(TodayM)
                call    CalMonthLen
                ld      b,a
                ld      a,(TodayD)
                inc     a
                cp      b
                jr      z,CndSet
                jr      c,CndSet
                ld      a,1
                ld      (TodayD),a
                ld      hl,TodayM
                inc     (hl)
                ld      a,(hl)
                cp      12
                ret     c
                ld      (hl),0
                ld      hl,TodayY
                inc     (hl)
                ld      a,(hl)
                cp      CALYEARS
                ret     c
                ld      (hl),0
                ret
CndSet:
                ld      (TodayD),a
                ret

; The header, "AUG 2026", into CalRow, null terminated.
CalBuildHead:
                ld      hl,CalRow
                ld      a,(CalMonth)
                ld      b,a
                add     a,a
                add     a,b
                ld      e,a
                ld      d,0
                push    hl
                ld      hl,CalNames
                add     hl,de
                pop     de
                ld      bc,3
                ldir
                ex      de,hl
                ld      (hl),' '
                inc     hl
                ld      a,(CalYear)
                ld      b,a
                ld      a,CALYEAR0/100
                ld      c,a
                ld      a,CALYEAR0-1900
                add     a,b
                cp      100
                jr      c,CalCent
                sub     100
                inc     c
CalCent:
                ld      b,a
                ld      a,c
                ld      c,0
                push    bc
                call    CalPut2
                pop     bc
                ld      a,b
                ld      c,0
                call    CalPut2
                ld      (hl),0
                ret

; Sets CalFirst and CalLen for the month on show; CalBuildWeek wants
; them.
CalPrepare:
                call    CalFirstDow
                ld      a,(CalDow)
                ld      (CalFirst),a
                ld      a,(CalMonth)
                call    CalMonthLen
                ld      (CalLen),a
                ret

; The grid row for week CalIx: seven cells of two digits and a space,
; twenty characters, null terminated, into CalRow or, through
; CalBuildWeekAt, wherever HL points. A running day number in E
; rather than the cell arithmetic per cell: the grid was measured at
; five milliseconds of a window open the other way.
CalBuildWeek:
                ld      hl,CalRow
CalBuildWeekAt:
                ld      a,(CalIx)
                ld      b,a
                add     a,a
                add     a,a
                add     a,a
                sub     b                       ; times seven
                inc     a
                ld      b,a
                ld      a,(CalFirst)
                neg
                add     a,b
                ld      e,a                     ; the day in the first cell,
                ld      a,(CalLen)              ; nought or below before the 1st
                ld      d,a
                ld      b,7
CalCellLoop:
                ld      a,e
                or      a
                jr      z,CalBlank
                jp      m,CalBlank              ; before the first
                cp      d
                jr      z,CalDay
                jr      nc,CalBlank             ; past the last
CalDay:
                push    hl                      ; two digits from the table:
                push    bc                      ; a division loop a cell was
                ld      l,a                     ; measured at 0.8 ms a week
                ld      h,0
                add     hl,hl
                ld      bc,CalDayTab
                add     hl,bc
                ld      a,(hl)
                ld      (CalPair),a
                inc     hl
                ld      a,(hl)                  ; through memory: B is the
                ld      (CalPair+1),a           ; cell count and D and E are
                pop     bc                      ; the month, so no register
                pop     hl                      ; is free to carry it
                ld      a,(CalPair)
                ld      (hl),a
                inc     hl
                ld      a,(CalPair+1)
                ld      (hl),a
                inc     hl
                jr      CalCellDone
CalBlank:
                ld      (hl),' '
                inc     hl
                ld      (hl),' '
                inc     hl
CalCellDone:
                ld      (hl),' '
                inc     hl
                inc     e
                djnz    CalCellLoop
                dec     hl                      ; the trailing space is not
                ld      (hl),0                  ; wanted
                ret

; A = a value below 100, HL = where its two digits go, C = 1 to print
; a leading nought as a space.
CalPut2:
                ld      b,'0'-1
CalP2T:
                inc     b
                sub     10
                jr      nc,CalP2T
                add     a,10
                ld      e,a
                ld      a,b
                cp      '0'
                jr      nz,CalP2Put
                ld      a,c
                or      a
                ld      a,'0'
                jr      z,CalP2Put
                ld      a,' '
CalP2Put:
                ld      (hl),a
                inc     hl
                ld      a,e
                add     a,'0'
                ld      (hl),a
                inc     hl
                ret

; A divided by seven. Returns A = the quotient, CalRem = the rest.
CalDiv7:
                ld      c,0
CalD7:
                cp      7
                jr      c,CalD7Done
                sub     7
                inc     c
                jr      CalD7
CalD7Done:
                ld      (CalRem),a
                ld      a,c
                ret

; The key vector. Left and right walk the days, up and down the
; weeks, ENTER makes the selected day today. Returns A = 1 if the
; window needs recomposing, 0 if the key meant nothing to it: SPACE
; is the button as well as a key, and a full grid rebuild on every
; press was measured to drop the frame a drag starts in.
CalKey:
                cp      KEY_LEFT
                jr      nz,CkyNotL
                ld      b,1
                ld      c,0
                jr      CkyMove
CkyNotL:
                cp      KEY_RIGHT
                jr      nz,CkyNotR
                ld      b,1
                ld      c,1
                jr      CkyMove
CkyNotR:
                cp      KEY_UP
                jr      nz,CkyNotU
                ld      b,7
                ld      c,0
                jr      CkyMove
CkyNotU:
                cp      KEY_DOWN
                jr      nz,CkyNotD
                ld      b,7
                ld      c,1
CkyMove:
                call    CalMoveB
                ld      a,1                     ; the grid changed
                ret
CkyNotD:
                cp      KEY_ENTER
                ld      a,0                     ; anything else meant nothing,
                ret     nz                      ; and nothing is repainted
                ld      a,(CalYear)
                ld      (TodayY),a
                ld      a,(CalMonth)
                ld      (TodayM),a
                ld      a,(CalSel)
                ld      (TodayD),a
                ld      a,1
                ret

; ------------------------------------------------------------
;  CalDraw
;  The draw vector: header, weekday letters and six week rows
;  into the live window's buffer, the selected day on the
;  inverted bank. Today gets no rule: there is no pixel row on
;  the grid to put one on.
; ------------------------------------------------------------
CalDraw:
                call    CalPrepare
                xor     a
                ld      (PrintInv),a
                call    CalBuildHead
                ld      hl,CalRow
                ld      b,1
                ld      c,1
                call    WinPrint
                ld      b,1                     ; APPF_FULL: the rest of the
                ld      c,9                     ; header row and the cell
                call    WinCell                 ; after the labels are ours
                ld      b,CALW-2-8              ; to blank
CdPad:
                ld      (hl),' '
                inc     hl
                djnz    CdPad
                ld      hl,CalDays
                ld      b,2
                ld      c,1
                call    WinPrint
                ld      b,2
                ld      c,CALW-2
                call    WinCell
                ld      (hl),' '
                xor     a
                ld      (CalIx),a
CdWeek:
                ld      a,(CalIx)               ; each week straight into the
                add     a,3                     ; buffer rather than through
                ld      b,a                     ; CalRow and a copy
                ld      c,1
                call    WinCell
                call    CalBuildWeekAt
                ld      (hl),' '                ; over the terminator
                ld      hl,CalIx
                inc     (hl)
                ld      a,(hl)
                cp      6
                jr      c,CdWeek
                ; the selected day again, inverted
                ld      a,(CalSel)
                dec     a
                ld      b,a
                ld      a,(CalFirst)
                add     a,b
                call    CalDiv7                 ; A = week, CalRem = column
                add     a,3
                ld      b,a
                ld      a,(CalRem)
                ld      c,a
                add     a,a
                add     a,c
                inc     a
                ld      c,a
                push    bc
                ld      a,(CalSel)
                ld      hl,CalPair
                ld      c,1
                call    CalPut2
                ld      (hl),0
                pop     bc
                ld      hl,CalPair
                ld      a,INVBANK
                ld      (PrintInv),a
                call    WinPrint
                xor     a
                ld      (PrintInv),a
                ret

; Days 0 to 31 as two characters, a leading nought shown as a space.
CalDayTab:      defb    "   1 2 3 4 5 6 7 8 9"
                defb    "10111213141516171819"
                defb    "20212223242526272829"
                defb    "3031"
CalNames:       defb    "JANFEBMARAPRMAYJUNJULAUGSEPOCTNOVDEC"
CalDays:        defb    "MO TU WE TH FR SA SU",0
TxtCalTitle:    defb    "CALENDAR",0
