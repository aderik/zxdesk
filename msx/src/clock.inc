; ============================================================
;  Clock
;  The ZX clock, which counts interrupts because there is no
;  clock chip, ported with one change: the rate. A PAL MSX frame
;  is 313 lines of 228 cycles at 3.579545 MHz, 50.159 Hz, and an
;  NTSC one 262 lines, 59.923 Hz. So a second is 50 interrupts
;  and every so often 51, or 59 and every so often 60: the ZX's
;  hundredths accumulator with the base and the addend chosen by
;  bit 7 of the BIOS's $002B, which says which video the machine
;  has. The harness runs the same arithmetic in Python against
;  the true rate for an hour and asserts the drift.
;
;  It counts IrqCnt rather than its own frames, so a frame the
;  main loop missed still advances the clock. Hours and minutes
;  are shown; seconds are counted, because a minute cannot roll
;  without them, and not painted.
;
;  The time is the machine's rather than the window's, so the
;  application keeps no per instance state and two clock windows
;  show the same time.
; ============================================================
CLKW            equ     8
CLKH            equ     3
CLKBASE50       equ     50
CLKADD50        equ     16              ; hundredths: 50.16 a second
CLKBASE60       equ     59
CLKADD60        equ     92              ; 59.92
BIOSVIDEO       equ     $002B           ; bit 7 set: 50 Hz

; Noon, because midnight is indistinguishable from unset.
ClkInit:
                ld      a,12
                ld      (ClkH),a
                xor     a
                ld      (ClkM),a
                ld      (ClkS),a
                ld      (ClkFrames),a
                ld      (ClkFrac),a
                ld      (ClkField),a
                ld      (ClkDirty),a
                ld      hl,(IrqCnt)
                ld      (ClkLast),hl
                ld      (ClkSetAt),hl
                ld      a,(BIOSVIDEO)
                ld      b,CLKBASE60
                ld      c,CLKADD60
                bit     7,a
                jr      z,ClkRate
                ld      b,CLKBASE50
                ld      c,CLKADD50
ClkRate:
                ld      a,b
                ld      (ClkBase),a
                ld      (ClkNeed),a
                ld      a,c
                ld      (ClkFracAdd),a
                ret

; Called once a frame. Advances by however many interrupts have
; happened since the last look, which is normally one.
ClkTick:
                ld      hl,(IrqCnt)
                ld      de,(ClkLast)
                or      a
                sbc     hl,de
                ret     z
                ld      (ClkDelta),hl
                ld      hl,(IrqCnt)
                ld      (ClkLast),hl
                ld      hl,(ClkDelta)
ClkAccum:
                ld      a,(ClkFrames)
                inc     a
                ld      (ClkFrames),a
                ld      b,a
                ld      a,(ClkNeed)
                cp      b
                jr      nz,ClkNoSec
                push    hl
                call    ClkSecond
                pop     hl
ClkNoSec:
                dec     hl
                ld      a,h
                or      l
                jr      nz,ClkAccum
                ret

ClkSecond:
                xor     a
                ld      (ClkFrames),a
                ld      a,(ClkFrac)
                ld      b,a
                ld      a,(ClkFracAdd)
                add     a,b
                cp      100
                jr      c,ClkNoExtra
                sub     100
                ld      (ClkFrac),a
                ld      a,(ClkBase)
                inc     a
                ld      (ClkNeed),a
                jr      ClkBump
ClkNoExtra:
                ld      (ClkFrac),a
                ld      a,(ClkBase)
                ld      (ClkNeed),a
ClkBump:
                ld      hl,ClkS
                inc     (hl)
                ld      a,(hl)
                cp      60
                ret     c
                ld      (hl),0
                ld      a,1
                ld      (ClkDirty),a
                ld      hl,ClkM
                inc     (hl)
                ld      a,(hl)
                cp      60
                ret     c
                ld      (hl),0
                ld      hl,ClkH
                inc     (hl)
                ld      a,(hl)
                cp      24
                ret     c
                ld      (hl),0
                jp      CalNextDay              ; midnight is the one moment

; Once a frame: tick, and when the minute changed, every clock window
; is recomposed at the end of the frame.
ClkService:
                call    ClkTick
                ld      a,(ClkDirty)
                or      a
                ret     z
                xor     a
                ld      (ClkDirty),a
                ld      a,(WndCount)
                or      a
                ret     z
                call    WndFlush
                ld      b,a
                ld      c,0
CsSlot:
                push    bc
                ld      e,c
                ld      d,0
                ld      hl,WndZ
                add     hl,de
                ld      a,(hl)
                ld      c,a
                call    WndRecAt
                ld      de,WR_APP
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ld      hl,AppClock
                or      a
                sbc     hl,de
                jr      nz,CsNext
                ld      a,c
                call    WndTouch
                ld      a,c
                call    RowsAddSlot
CsNext:
                pop     bc
                inc     c
                djnz    CsSlot
                ret

; The draw vector: " HH:MM" with the field the keys are on inverted.
ClkDraw:
                ld      hl,ClkText
                ld      (hl),' '
                inc     hl
                ld      a,(ClkH)
                call    ClkPut2
                ld      (hl),':'
                inc     hl
                ld      a,(ClkM)
                call    ClkPut2
                ld      (hl),0
                ld      a,(ClkField)
                ld      b,a
                add     a,a
                add     a,b                     ; three columns to a field
                inc     a
                ld      e,a
                ld      d,0
                ld      hl,ClkText
                add     hl,de
                ld      a,(hl)
                or      INVBANK
                ld      (hl),a
                inc     hl
                ld      a,(hl)
                or      INVBANK
                ld      (hl),a
                xor     a
                ld      (PrintInv),a
                ld      hl,ClkText
                ld      b,1
                ld      c,1
                jp      WinPrint

ClkPut2:
                ld      b,'0'-1
ClkP2T:
                inc     b
                sub     10
                jr      nc,ClkP2T
                add     a,10
                ld      (hl),b
                inc     hl
                add     a,'0'
                ld      (hl),a
                inc     hl
                ret

; The key vector: left and right pick the field, up and down set it.
; Returns A = 1 when the face changed.
ClkKey:
                cp      KEY_LEFT
                jr      nz,CkNotL
                ld      a,(ClkField)
                or      a
                jr      z,CkPaint
                dec     a
                ld      (ClkField),a
                jr      CkPaint
CkNotL:
                cp      KEY_RIGHT
                jr      nz,CkNotR
                ld      a,(ClkField)
                cp      1
                jr      nc,CkPaint
                inc     a
                ld      (ClkField),a
                jr      CkPaint
CkNotR:
                cp      KEY_UP
                jr      nz,CkNotU
                ld      c,1
                call    ClkAdjust
                jr      CkPaint
CkNotU:
                cp      KEY_DOWN
                ld      a,0
                ret     nz
                ld      c,0
                call    ClkAdjust
CkPaint:
                ld      a,1
                ret

ClkAdjust:
                ld      a,(ClkField)
                ld      hl,ClkH
                or      a
                jr      z,CkaGot
                inc     hl
CkaGot:
                ld      b,60
                ld      a,(ClkField)
                or      a
                jr      nz,CkaMod
                ld      b,24
CkaMod:
                ld      a,c
                or      a
                jr      z,CkaDown
                ld      a,(hl)
                inc     a
                cp      b
                jr      c,CkaSet
                xor     a
                jr      CkaSet
CkaDown:
                ld      a,(hl)
                or      a
                jr      nz,CkaDec
                ld      a,b
CkaDec:
                dec     a
CkaSet:
                ld      (hl),a
                xor     a                       ; the minute restarts from the
                ld      (ClkFrames),a           ; moment it was set
                ld      (ClkS),a
                ld      (ClkFrac),a
                ld      hl,(IrqCnt)             ; for the harness: which
                ld      (ClkSetAt),hl           ; interrupt the count began at
                ret

ClockOpen:
                ld      a,APP_CLOCK
                jp      WndOpen

TxtClockTitle:  defb    "CLOCK",0
