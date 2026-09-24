; ============================================================
;  MSX Desk
;  ZX Desk, Damian Cooper's desktop for the Spectrum, ported to
;  the MSX1 and renamed for the machine it now runs on.
;  Phase 1: the skeleton. Screen 2 with a tile desktop and a
;  menu bar in the name table, a hardware sprite for the
;  pointer, the H.TIMI hook for the frame clock, and keyboard
;  and mouse arriving in the event ring ported from the ZX
;  version.
;
;  A 32K cartridge at $4000-$BFFF; work RAM from $C000, laid out
;  by the `var` macro below so the budget is one number, RamEnd.
;
;  Copyright (c) 2026 Damian Cooper (ZX Desk, MIT).
;  MSX port work under the same licence.
; ============================================================

; ---- BIOS entry points (C-BIOS and the real one agree on these)
CHGMOD          equ     $005F           ; A = screen mode
RG1SAV          equ     $F3E0           ; BIOS shadow of VDP register 1
CGTABL          equ     $0004           ; word: the BIOS font, 256 glyphs of 8 bytes
HTIMI           equ     $FD9F           ; five byte hook, called each VDP interrupt
HRUNC           equ     $FECB           ; the hook BASIC's cold start calls; the disk ROM's second half hangs on it
HMAIN           equ     $FF0C           ; the hook at the top of BASIC's main loop, after every init is done
PUTPNT          equ     $F3F8           ; the keyboard buffer's write pointer
EXPTBL          equ     $FCC1           ; per primary slot: bit 7 set if expanded
SLTTBL          equ     $FCC5           ; per primary slot: its secondary slot register
PSLOT           equ     $A8             ; the primary slot register

; ---- I/O
VDPDATA         equ     $98
VDPCTRL         equ     $99
PPIB            equ     $A9             ; keyboard column bits, active low
PPIC            equ     $AA             ; low nibble selects the row
PSGADR          equ     $A0
PSGWR           equ     $A1
PSGRD           equ     $A2

; ---- VRAM layout after CHGMOD 2 with the standard tables
PGT             equ     $0000           ; 6144 bytes, three thirds of 256 patterns
NT              equ     $1800           ; 768 bytes
SAT             equ     $1B00           ; sprite attributes, 4 bytes each
CT              equ     $2000           ; 6144 bytes, a colour byte per pattern row
SPT             equ     $3800           ; sprite patterns

; ---- Screen geometry. Screen 2 is 32 by 24 cells, as the ZX was.
SCRCOLS         equ     32
SCRROWS         equ     24
MENUROW         equ     0
STATROW         equ     23
PTRXMAX         equ     247
PTRYMAX         equ     172             ; keeps the pointer clear of the status band
MOUSEMAX        equ     64              ; the most a mouse may move in one frame, as
                                        ; on the ZX; 16 threw away fast moves
MOUSEWAIT       equ     10              ; settle loops between strobe and sample; a real
                                        ; mouse may want more, tune on hardware

; ---- Tiles. Codes 32-127 are the BIOS font; these are ours.
T_LATTICE       equ     $80             ; the desktop halftone
T_RULE          equ     $81             ; lattice with the menu bar's rule on top

; ---- Colours, ink in the high nibble
C_TEXT          equ     $1F             ; black on white: the glyphs, every third
C_INVERSE       equ     $F1             ; white on black: the inverted bank
C_LATTICE       equ     $EF             ; grey on white
C_POINTER       equ     $01             ; black
STACKRES        equ     $380            ; the stack, under HIMEM; the heap ends below it

; ---- Glyph banks. The font sits at $20-$7F as itself and again at
; $A0-$FF with the colours swapped, so inverted text is a code with
; bit 7 set, in any third, without a second pattern set.
INVBANK         equ     $80

; ------------------------------------------------------------
;  Work RAM. `var name,size` hands out addresses from $C000 up;
;  RamEnd is the budget the harness checks against the BIOS
;  area at $F380. The first two entries are at fixed offsets
;  because the harness reads them before it has the symbols.
; ------------------------------------------------------------
_ram            defl    $C000
                macro   var, name, size
name            equ     _ram
_ram            defl    _ram+size
                endm

                var     Marker, 8       ; "ZXMSX",0 at $C000
                var     Frames, 2       ; main loop iterations, at $C008
                var     IrqCnt, 2       ; interrupts taken, counted by H.TIMI
                var     Dropped, 2      ; frames the loop was late for
                var     DropLog, 32     ; the frame numbers of the first 16 late ones
                var     DropN, 1
                var     Recomposes, 2   ; how often WndRepaintAll ran
                var     Buttons, 1      ; Kempston layout, active low: bit 1 left, bit 0 right
                var     PtrX, 1
                var     PtrY, 1
                var     Dirs, 1
                var     HeldX, 1        ; frames a horizontal key was held, for
                var     HeldY, 1        ; the harness: injection lands within a
                var     KbdHeld, 1      ; frame either way, the ramp is exact
                var     HoldCnt, 1
                var     Speed, 1
                var     MouseDX, 1      ; last frame's raw deltas, for the harness
                var     MouseDY, 1
                var     KbdMatrix, KBDROWS
                var     KbdModState, 1
                var     KbdLast, 1
                var     KbdTimer, 1
                var     EvHead, 1
                var     EvTail, 1
                var     EvLastBtn, 1
                var     EvLastX, 1
                var     EvLastY, 1
                var     EvDropped, 1
                var     EvCounts, 4     ; ptrmove, btndown, btnup, key
                var     LastKey, 1
                var     StatCol, 1
                var     PrintInv, 1     ; nonzero: PrintStr uses the inverted bank
                var     EvQueue, EVSLOTS*4
                var     LastHit, 1      ; what the last press landed on
                ; the name table's shadow: everything paints here and
                ; NtFlush copies the dirty rows out after the interrupt
                var     ShadowNT, SCRCOLS*SCRROWS
                var     DirtyRows, 3    ; a bit per row
                ; the heap
                var     HpWant, 2
                var     HpOwner, 1
                var     HpTotal, 2
                var     HpBiggest, 2
                ; storage: the six vectors are contiguous, StSelect copies
                ; a registry row over them
                var     StBackend, 1
                var     StCapability, 1
                var     StVecOpen, 2
                var     StVecClose, 2
                var     StVecRead, 2
                var     StVecWrite, 2
                var     StVecDir, 2
                var     StVecDelete, 2
                var     RamName, RAMNAMESZ
                var     RamMode, 1
                var     RamHandle, 1
                var     RamPos, 2
                var     RamBuf, 2
                var     RamCnt, 2
                var     RamDir, RAMFILES*RAMENTSZ
                var     RamHeap, RAMFILES*RAMCHUNK
                ; menus, the part hit testing reads
                var     MenuOpen, 1
                var     MnX, 1
                var     MnY, 1
                var     MnW, 1
                var     MnH, 1
                var     MnCount, 1
                var     MnItemPtr, 2
                var     MnTitleCol, 1
                var     MnTitleW, 1
                var     MenuPick, 1     ; last item chosen, $FF for none
                var     MnLastMenu, 1
                var     MnSave, MNSAVESZ
                var     DgOpenFlag, 1
                var     DgFocus, 1
                var     DgCount, 1
                var     DgAfter, 2
                var     DgLine1, 2
                var     DgLine2, 2
                var     CtlTable, 21    ; CTLTABSZ, which is defined later
                ; windows: the live record, the table, the z order
                var     WinRec, 0
                var     WinX, 1
                var     WinY, 1
                var     WinW, 1
                var     WinH, 1
                var     WinTitle, 2
                var     WinBufP, 2
                var     WinApp, 2
                var     WinStateP, 2
                var     WinOldX, 1
                var     WinOldY, 1
                var     WinMoved, 1
                var     WndTab, 4*15    ; WNDMAX*WNDRECSZ, defined later
                var     WndZ, 4
                var     WndCount, 1
                var     ArrI, 1
                var     ArrTab, 2
                var     WndCur, 1
                var     Dragging, 1
                var     DragDX, 1
                var     DragDY, 1
                var     WndWantKind, 1
                var     WndForce, 1
                var     WhtCol, 1
                var     WhtRow, 1
                var     WndHit, 1
                var     WdCapFill, 1
                var     RepaintDue, 1
                var     RowLo, 1        ; the pending repaint's rows
                var     RowHi, 1
                var     WndDirty, 1     ; a bit per slot whose buffer is stale
                var     WndCapDirty, 1  ; and per slot whose title row is
                var     SpSave, 2       ; while the stack fills the desktop
                var     WdFull, 1
                var     HeapEnd, 2      ; HIMEM minus STACKRES, at Init
                ; the disk backend
                var     DskFcb, FCBSIZE
                var     DskEnt, 40      ; the DTA for a search
                var     DskEntName, 13
                var     DskName, 13
                var     DskMode, 1
                var     DskHandle, 1
                var     DskPos, 2
                var     DskCnt, 2
                var     DskDirN, 1
                var     CalPair, 3
                ; calendar: the three state bytes are contiguous, so are
                ; today's
                var     CalState, 0
                var     CalYear, 1
                var     CalMonth, 1
                var     CalSel, 1
                var     TodayY, 1
                var     TodayM, 1
                var     TodayD, 1
                var     CalLeapY, 1
                var     CalDow, 1
                var     CalFirst, 1
                var     CalLen, 1
                var     CalIx, 1
                var     CalCol, 1
                var     CalRem, 1
                var     CalRow, 24
                ; the notepad's per instance state, one record
                var     NoteState, 0
                var     NoteBuf, NOTESIZE
                var     NoteName, NOTENAMEMAX+1
                var     NoteCX, 1
                var     NoteCY, 1
                var     NoteTop, 1
                var     NoteEdited, 1
                var     NoteDirty, 1
                var     NoteModified, 1
                var     NoteBackend, 1
NOTESTSZ        equ     _ram-NoteState
                var     NoteRowIx, 1
                var     NoteRowDst, 1
                var     NoteJoinAt, 1
                var     NoteChar, 1
                var     NoteLine, 2
                var     NoteHandle, 1
                var     NoteGot, 2
                var     NoteResult, 1
                var     NotePrevBackend, 1
                ; Commander: cached visible names belong to each instance.
                var     CmdState, 0
                var     CmdBk0, 1
                var     CmdBk1, 1
                var     CmdSel0, 1
                var     CmdSel1, 1
                var     CmdTop0, 1
                var     CmdTop1, 1
                var     CmdActive, 1
                var     CmdStatus, 1
                var     CmdPending, 1
                var     CmdDeleteName, 13
                var     CmdCache0, CMDCACHE
                var     CmdCache1, CMDCACHE
CMDSTSZ         equ     _ram-CmdState
                var     CmdPane, 1
                var     CmdCacheP, 2
                var     CmdIndex, 1
                var     CmdLeft, 1
                var     CmdCol, 1
                var     CmdRow, 1
                var     CmdKeyCh, 1
                var     CmdWasK, 1
                var     CmdSize, 2
                var     CmdGot, 2
                var     CmdHandle, 1
                var     CmdSrcBk, 1
                var     CmdWork, 13
                var     CmdBuf, 256
                ; the clock
                var     ClkText, 8
                var     ClkH, 1
                var     ClkM, 1
                var     ClkS, 1
                var     ClkField, 1
                var     ClkFrames, 1
                var     ClkNeed, 1
                var     ClkBase, 1
                var     ClkFrac, 1
                var     ClkFracAdd, 1
                var     ClkDirty, 1
                var     ClkLast, 2
                var     ClkDelta, 2
                var     ClkSetAt, 2     ; IrqCnt when the clock was last set
                ; SETTINGS record plus one byte to detect oversized files.
                var     SetRec, 2
                var     SetSpeed, 1
                var     SetInvertY, 1
                var     SetBackend, 1
                var     SetExtra, 1
                var     SetDevice, 1
                var     SetHandle, 1
                var     AccelPtr, 2
                var     SetText, 2
IFDEF TEST
                var     TestDone, 1
                var     ThPtr, 6
                var     ThStat, 12
                var     TcMonths, CALYEARS*12*2
                var     TcGrid, 9+6*21
                var     TcSel, 9
                var     TsSrc, 64
                var     TsDst, 64
                var     TsHandle, 1
                var     TsWrote, 2
                var     TsRead, 2
                var     TsBad, 1
                var     TsErrCode, 1
                var     TsDirN, 1
                var     TsFull, 1
                var     TsDel, 1
                var     TsDirAfter, 1
                var     TaDesc, 2
                var     TaState, 6
                var     ThitRes, 5
ENDIF
RamEnd          equ     _ram
; The heap takes everything from here to HeapEnd, set at Init.
HeapBase        equ     RamEnd

                org     $4000
                defb    "AB"
                defw    Init
                defw    0               ; STATEMENT
                defw    0               ; DEVICE
                defw    0               ; TEXT
                defs    6,0

Init:
                ; A disk ROM initialises in two halves: its INIT, which ran
                ; before this one because it sits in a lower slot, takes a
                ; driver work area and hooks H.RUNC (measured: RST $30,
                ; slot, address, RET); the DOS kernel, the BDOS jump and
                ; the five kilobytes under HIMEM only arrive when BASIC's
                ; cold start calls that hook, and that handler does not
                ; return, it carries on into BASIC (measured: chaining it
                ; ended at the Ok prompt). So if H.RUNC is hooked, this
                ; INIT hooks H.MAIN, the top of BASIC's main loop, which
                ; comes after every init there is, returns to the BIOS,
                ; and the desktop starts from there. BASIC calls the hook
                ; with page 1 on its own ROM, so the hook is an inter-slot
                ; call, RST $30 with this cartridge's slot id: a plain JP
                ; was measured to land in BASIC at the same address.
                ld      a,(HRUNC)
                cp      $C9                     ; a RET: nobody is waiting
                jr      z,Start
                ; The disk ROM asks for the date on a machine with no clock
                ; and waits for a key; a return in the keyboard buffer is
                ; what a person would give it.
                ld      hl,(PUTPNT)
                ld      (hl),13
                inc     hl
                ld      (PUTPNT),hl
                call    GetSlot1
                ld      (HMAIN+1),a
                ld      a,$F7                   ; RST $30: CALLF
                ld      (HMAIN),a
                ld      hl,Start
                ld      (HMAIN+2),hl
                ld      a,$C9
                ld      (HMAIN+4),a
                ret

; The slot id of whatever is in page 1 now, which at INIT is this
; cartridge: primary slot from the slot register, and the secondary
; slot from SLTTBL if that primary slot is expanded.
GetSlot1:
                in      a,(PSLOT)
                rrca
                rrca
                and     3
                ld      c,a
                ld      b,0
                ld      hl,EXPTBL
                add     hl,bc
                ld      a,(hl)
                and     $80
                jr      nz,GsExpanded
                ld      a,c                     ; not expanded: the id is the
                ret                             ; primary slot
GsExpanded:
                or      c
                ld      c,a
                ld      hl,SLTTBL
                ld      a,c
                and     3
                ld      e,a
                ld      d,0
                add     hl,de
                ld      a,(hl)
                rrca
                rrca
                and     $0C
                or      c
                ret

Start:
                ; The BIOS called us on its own stack, wherever that was.
                ; HIMEM is $F380 on a bare machine and lower by a disk
                ; ROM's work area, so the stack and the heap's end come
                ; from it rather than from an equate.
                ld      hl,(HIMEM)
                ld      sp,hl
                ld      de,STACKRES
                or      a
                sbc     hl,de
                ld      (HeapEnd),hl
                ld      a,2
                call    CHGMOD
                ld      b,$0F           ; white border, as the ZX had
                ld      c,7
                call    WrtVdp
                ld      b,$A2           ; 16K, display OFF, interrupts, 16x16
                ld      c,1             ; sprites: CHGMOD leaves them 8x8 and
                call    WrtVdp          ; the arrow lost its tail. The display
                                        ; stays off until the desktop has been
                                        ; flushed once: CHGMOD leaves the font
                                        ; table on the screen and it showed
                ld      a,$E2
                ld      (RG1SAV),a      ; keep the BIOS's shadow honest
                call    LoadFont
                call    LoadTiles
                call    LoadColours
                call    LoadSprite
                call    InitScreen
                ld      a,$FF                   ; the whole shadow goes out on
                ld      (DirtyRows),a           ; the first frame, whatever
                ld      (DirtyRows+1),a         ; the marks say
                ld      (DirtyRows+2),a

                ld      hl,TxtMarker
                ld      de,Marker
                ld      bc,6
                ldir
                ld      hl,0
                ld      (Frames),hl
                ld      (IrqCnt),hl
                ld      (Dropped),hl
                ld      (Recomposes),hl
                xor     a
                ld      (DropN),a
                xor     a
                ld      (HoldCnt),a
                ld      (KbdLast),a
                ld      (HeldX),a
                ld      (HeldY),a
                ld      (KbdHeld),a
                ld      (MouseDX),a
                ld      (MouseDY),a
                ld      a,$FF
                ld      (Buttons),a
                ld      a,120
                ld      (PtrX),a
                ld      a,90
                ld      (PtrY),a
                call    EvInit
                call    HeapInit
                call    StInit
                call    StIdent
                ld      (SetDevice),a
                call    SetLoad
                call    SetApply
                call    CtlInit
                xor     a
                ld      (LastHit),a
                ld      (MnLastMenu),a
                dec     a
                ld      (MenuPick),a
                xor     a
                ld      (TodayY),a
                ld      (TodayM),a
                inc     a
                ld      (TodayD),a
                call    WndInit
                call    PtrUpdate
                call    SetupIrq
                call    ClkInit         ; after the hook, so it starts from
                                        ; the count as it is
IFDEF TEST
                call    TestRun
ENDIF

; ------------------------------------------------------------
;  The frame. HALT wakes on the VDP interrupt, which the BIOS
;  handler services before H.TIMI counts it. The sprite is
;  moved first, while the beam is still in the border, so the
;  pointer never tears. Input is read and dispatched at the end
;  of the frame, as before, so the model is settled before the
;  next frame paints it.
; ------------------------------------------------------------
                ; The first frame: the whole desktop out, then the display on.
                ei
                halt
                call    NtFlush
                ld      b,$E2           ; display on
                ld      c,1
                call    WrtVdp
                ld      hl,(IrqCnt)     ; that frame was not a loop frame:
                ld      (Frames),hl     ; keep the watchdog's two counts level
MainLoop:
                ei
                halt
                call    FrameWatch
                call    PtrUpdate
                call    NtFlush
                call    ReadInput
                call    EvPoll          ; ends in KbdPoll
                call    EvDispatch
                call    ClkService      ; the minute may have rolled
                call    WinRedraw       ; whatever changed, recomposed once
                ld      hl,(Frames)
                inc     hl
                ld      (Frames),hl
                jr      MainLoop

; ------------------------------------------------------------
;  H.TIMI. The BIOS interrupt handler saves every register
;  before calling the hook, so unlike the ZX handler this one
;  can use HL. INC HL still leaves the flags alone.
; ------------------------------------------------------------
SetupIrq:
                di
                ld      a,$C3           ; JP IrqTick
                ld      (HTIMI),a
                ld      hl,IrqTick
                ld      (HTIMI+1),hl
                ei
                ret

IrqTick:
                ld      hl,(IrqCnt)
                inc     hl
                ld      (IrqCnt),hl
                ret

; Counts the frames the loop reached; the difference from IrqCnt is
; the number the loop was late for.
FrameWatch:
                ld      hl,(Frames)
                inc     hl              ; this frame
                ld      de,(IrqCnt)
                ex      de,hl
                or      a
                sbc     hl,de           ; interrupts minus frames
                ret     z
                ld      de,(Dropped)
                add     hl,de
                ld      (Dropped),hl
                ld      a,(DropN)
                cp      16
                jr      nc,FwNoLog
                inc     a
                ld      (DropN),a
                dec     a
                add     a,a
                ld      e,a
                ld      d,0
                ld      hl,DropLog
                add     hl,de
                ld      de,(Frames)
                ld      (hl),e
                inc     hl
                ld      (hl),d
FwNoLog:
                ld      hl,(IrqCnt)     ; resync so a late frame counts once
                dec     hl
                ld      (Frames),hl
                ret

; ------------------------------------------------------------
;  Pointer: sprite 0. Two bytes into the attribute table, with
;  more than 29 cycles between the OUTs so it is safe even if
;  the beam has left the border.
; ------------------------------------------------------------
PtrUpdate:
                ld      hl,SAT
                call    SetWrt
                ld      a,(PtrY)
                dec     a               ; the VDP shows a sprite one line below its Y
                out     (VDPDATA),a
                ld      a,(PtrX)
                ld      b,a
                ld      a,(PtrY)        ; padding: 13 + 4 + 13 + 11 = 41 cycles
                nop
                ld      a,b
                out     (VDPDATA),a
                ret

; ------------------------------------------------------------
;  Input. The mouse in joystick port A first, then the cursor
;  keys with the ZX version's acceleration ramp, then CTRL as
;  the left button. The Kempston button layout is kept so
;  EvPoll did not have to change.
; ------------------------------------------------------------
ReadInput:
                ld      a,$FF
                ld      (Buttons),a
                call    ReadMouse

                ; cursor keys: row 8, bit 4 left, 5 up, 6 down, 7 right.
                ; With SHIFT held they are the application's, as KEY_LEFT
                ; and friends through the shift table, and the pointer
                ; stays put.
                ld      c,0
                in      a,(PPIC)
                and     $F0
                or      6
                out     (PPIC),a
                in      a,(PPIB)
                ld      d,a                     ; bit 0 = SHIFT, active low
                in      a,(PPIC)
                and     $F0
                or      8
                out     (PPIC),a
                in      a,(PPIB)
                ld      e,a
                bit     0,d
                jr      nz,RiK0
                ld      e,$FF                   ; shifted: no pointer keys
RiK0:
                bit     4,e
                jr      nz,RiK1
                set     0,c
RiK1:           bit     7,e
                jr      nz,RiK2
                set     1,c
RiK2:           bit     5,e
                jr      nz,RiK3
                set     2,c
RiK3:           bit     6,e
                jr      nz,RiK4
                set     3,c
RiK4:
                ld      a,c
                ld      (Dirs),a
                and     $03
                jr      z,RiNoHx
                ld      hl,HeldX
                inc     (hl)
RiNoHx:
                ld      a,c
                and     $0C
                jr      z,RiNoHy
                ld      hl,HeldY
                inc     (hl)
RiNoHy:
                ld      a,c

                ; A digital device needs a ramp or the pointer is either
                ; twitchy or glacial. One pixel to start, rising to seven
                ; after about two thirds of a second of holding.
                or      a
                jr      nz,RiHeld
                xor     a
                ld      (HoldCnt),a
                jr      RiSpeed
RiHeld:
                ld      a,(HoldCnt)
                cp      32
                jr      nc,RiSpeed
                inc     a
                ld      (HoldCnt),a
RiSpeed:
                ld      a,(HoldCnt)
                srl     a
                srl     a
                srl     a
                ld      e,a
                ld      d,0
                ld      hl,(AccelPtr)
                add     hl,de
                ld      a,(hl)
                ld      (Speed),a

                ld      b,0
                ld      a,(Dirs)
                bit     0,a
                jr      z,RiNoLeft
                ld      a,(Speed)
                neg
                ld      b,a
RiNoLeft:
                ld      a,(Dirs)
                bit     1,a
                jr      z,RiNoRight
                ld      a,(Speed)
                ld      b,a
RiNoRight:
                ld      a,b
                or      a
                jr      z,RiVert
                ld      hl,PtrX
                ld      b,(hl)
                ld      c,PTRXMAX
                call    ApplyDelta
                ld      (PtrX),a
RiVert:
                ld      b,0
                ld      a,(Dirs)
                bit     2,a
                jr      z,RiNoUp
                ld      a,(Speed)
                neg
                ld      b,a
RiNoUp:
                ld      a,(Dirs)
                bit     3,a
                jr      z,RiNoDown
                ld      a,(Speed)
                ld      b,a
RiNoDown:
                ld      a,b
                or      a
                jr      z,RiBtn
                ld      hl,PtrY
                ld      b,(hl)
                ld      c,PTRYMAX
                call    ApplyDelta
                ld      (PtrY),a
RiBtn:
                ; CTRL is the keyboard left button; SPACE is text only.
                in      a,(PPIC)
                and     $F0
                or      6
                out     (PPIC),a
                in      a,(PPIB)
                bit     1,a
                ret     nz
                ld      a,(Buttons)
                res     1,a
                ld      (Buttons),a
                ret

; A = signed delta, B = current, C = maximum. Returns A clamped to
; 0..C. Ported as is.
ApplyDelta:
                ld      e,a
                ld      d,0
                bit     7,e
                jr      z,ApdPos
                ld      d,$FF
ApdPos:
                ld      l,b
                ld      h,0
                add     hl,de
                bit     7,h
                jr      nz,ApdMin
                ld      a,h
                or      a
                jr      nz,ApdMax
                ld      a,l
                cp      c
                ret     c
                ret     z
ApdMax:
                ld      a,c
                ret
ApdMin:
                xor     a
                ret

; A = signed delta, clamped to +-MOUSEMAX.
ClampDelta:
                or      a
                ret     z
                bit     7,a
                jr      nz,CdNeg
                cp      MOUSEMAX+1
                ret     c
                ld      a,MOUSEMAX
                ret
CdNeg:
                cp      256-MOUSEMAX
                ret     nc
                ld      a,256-MOUSEMAX
                ret

; ------------------------------------------------------------
;  Mouse in joystick port A, the standard strobe protocol: pin 8
;  toggles through PSG register 15 and each edge presents the
;  next nibble on pins 1-4, read through register 14. Four
;  nibbles make dx and dy, both signed 8 bit, and the mouse
;  reports the negative of the movement, so they are subtracted.
;  The buttons ride on bits 4 and 5 of the last read.
;
;  With a joystick or nothing in the port every nibble reads $F,
;  which would be a drift of minus one per frame; four identical
;  $F nibbles are therefore taken as no mouse.
; ------------------------------------------------------------
ReadMouse:
                di                      ; the four nibbles are one strobe
                ld      a,$13           ; sequence; an interrupt handler that
                call    MouseNibble     ; touches PSG register 15 in between
                                        ; would shuffle them
                ld      d,a
                rlca
                rlca
                rlca
                rlca
                ld      b,a
                ld      a,$03           ; pin 8 low
                call    MouseNibble
                and     d
                ld      d,a
                ld      a,e
                and     $0F
                or      b
                ld      (MouseDX),a     ; dx, kept here because ApplyDelta wants C
                ld      a,$13
                call    MouseNibble
                and     d
                ld      d,a
                ld      a,e
                and     $0F
                rlca
                rlca
                rlca
                rlca
                ld      b,a
                ld      a,$03
                call    MouseNibble
                and     d
                ei
                cp      $0F
                ret     z               ; four $F nibbles: nothing there
                ; Buttons first, while E still holds the last byte read:
                ; bit 4 left, bit 5 right, active low, into the Kempston
                ; layout the event poll expects. ApplyDelta uses E.
                ld      a,e
                ld      c,$FF
                bit     4,a
                jr      nz,RmNoLeft
                res     1,c
RmNoLeft:
                bit     5,a
                jr      nz,RmNoRight
                res     0,c
RmNoRight:
                ld      a,c
                ld      (Buttons),a
                ld      a,e
                and     $0F
                or      b               ; dy
                ld      (MouseDY),a
                call    SetMouseY
                call    ClampDelta
                ld      hl,PtrY
                ld      b,(hl)
                ld      c,PTRYMAX
                call    ApplyDelta
                ld      (PtrY),a
                ld      a,(MouseDX)
                neg
                call    ClampDelta
                ld      hl,PtrX
                ld      b,(hl)
                ld      c,PTRXMAX
                call    ApplyDelta
                ld      (PtrX),a
                ; ponytail: a joystick in port A reads as four direction
                ; nibbles and would move the pointer; a MouseOn setting or
                ; a sanity check on the strobe timing is the upgrade path.
                ret

; A = value for register 15. Returns A = the low nibble of register
; 14 and E = the whole byte.
MouseNibble:
                ld      e,a
                ld      a,15
                out     (PSGADR),a
                ld      a,e
                out     (PSGWR),a
                ld      a,MOUSEWAIT     ; settle before sampling
MnWait:         dec     a
                jr      nz,MnWait
                ld      a,14
                out     (PSGADR),a
                in      a,(PSGRD)
                ld      e,a
                and     $0F
                ret

; ------------------------------------------------------------
;  DEVICE LAYER
;  Everything below writes VRAM through the port, with at least
;  29 cycles between consecutive OUTs so it is correct during
;  the active display as well as in the border.
; ------------------------------------------------------------

; B = value, C = register. The same latch race as SetWrt: an interrupt
; between the two control writes and the value lands in the wrong
; register, so the pair is held under DI.
WrtVdp:
                di
                ld      a,b
                out     (VDPCTRL),a
                ld      a,c
                or      $80
                out     (VDPCTRL),a
                ei
                ret

; HL = VRAM address to write from. The two control writes are held
; under DI: the BIOS interrupt handler reads the status register,
; which resets the VDP's address latch, and an interrupt between the
; two OUTs sends the data somewhere else. On the 60 Hz machine that
; landed on the same row of the desktop every boot.
SetWrt:
                di
                ld      a,l
                out     (VDPCTRL),a
                ld      a,h
                and     $3F
                or      $40
                out     (VDPCTRL),a
                ei
                ret

; Write B bytes of value A from the current address.
; The loop is 4+11+13 = 28 cycles plus the OUT itself, so 39 apart.
FillVram:
                ld      c,a
FvLoop:
                ld      a,c
                out     (VDPDATA),a
                djnz    FvLoop
                ret

; Copy BC bytes from HL to the current VRAM address.
; 7+11+6+6+4+4+12 = 50 cycles between OUTs.
CopyVram:
                ld      a,(hl)
                out     (VDPDATA),a
                inc     hl
                dec     bc
                ld      a,b
                or      c
                jr      nz,CopyVram
                ret

; ------------------------------------------------------------
;  The name table's shadow. Menus, windows and the desktop paint
;  cells here, in RAM, where a save-under is an LDIR and a
;  read-modify-write is a byte; NtFlush copies the rows that
;  changed to VRAM once a frame, in the border, 32 OUTs a row at
;  more than 29 cycles apart. This is the Konami approach the
;  port was chosen for: 768 bytes of name table stand in for
;  12K of pattern and colour data.
; ------------------------------------------------------------

; B = row, C = column. Returns HL = the shadow cell, and marks the
; row dirty.
CellAddr:
                push    bc
                ld      a,b
                call    MarkRow
                pop     bc
                ld      l,b
                ld      h,0
                add     hl,hl
                add     hl,hl
                add     hl,hl
                add     hl,hl
                add     hl,hl
                ld      b,0
                add     hl,bc
                ld      bc,ShadowNT
                add     hl,bc
                ret

; A = row: set its bit in DirtyRows. A table for the bit: the shift
; loop was measured at a tenth of a window open, thirty calls at
; three hundred cycles.
MarkRow:
                ld      c,a
                and     7
                ld      e,a
                ld      d,0
                ld      hl,BitTab
                add     hl,de
                ld      e,(hl)
                ld      a,c
                rrca
                rrca
                rrca
                and     3
                ld      hl,DirtyRows
                add     a,l
                ld      l,a
                jr      nc,MrNoCy
                inc     h
MrNoCy:
                ld      a,(hl)
                or      e
                ld      (hl),a
                ret
BitTab:         defb    $01,$02,$04,$08,$10,$20,$40,$80

; A = character, B = row, C = column.
PutChar:
                push    af
                call    CellAddr
                pop     af
                ld      (hl),a
                ret

; HL = null terminated text, B = row, C = column. Bit 7 of every code
; is set when PrintInv is nonzero, which is the inverted bank.
PrintStr:
                push    hl
                call    CellAddr
                pop     de
                ex      de,hl                   ; HL = text, DE = cell
                ld      a,(PrintInv)
                ld      c,a
PsLoop:
                ld      a,(hl)
                or      a
                ret     z
                or      c
                ld      (de),a
                inc     hl
                inc     de
                jr      PsLoop

; A = tile, B = row: a whole row of one tile.
FillRow:
                push    af
                ld      c,0
                call    CellAddr
                pop     af
                rept    SCRCOLS                 ; unrolled: 13 cycles a cell
                ld      (hl),a                  ; against 26, and the desktop
                inc     hl                      ; is 22 rows of these
                endm
                ret

; The dirty rows of the shadow to VRAM. Called right after the
; interrupt, so the first rows go out in the border; the rest are
; still safe because OUTI, NOP, JP NZ is 30 cycles between OUTs.
; Measured: 22 rows at 47 cycles a cell, plus a recompose, plus a
; second recompose from a key in the same frame, dropped 7 frames
; of a 115 frame drag; at 30 with one recompose a frame it drops
; none.
NtFlush:
                ld      a,(DirtyRows)
                ld      hl,(DirtyRows+1)
                or      h
                or      l
                ret     z
                ld      c,0                     ; the row
NfRow:
                ld      a,c
                rrca
                rrca
                rrca
                and     3
                ld      hl,DirtyRows
                add     a,l
                ld      l,a
                jr      nc,NfNoCy
                inc     h
NfNoCy:
                ld      a,c
                and     7
                ld      b,a
                ld      a,$01
                jr      z,NfBit
NfShift:
                add     a,a
                djnz    NfShift
NfBit:
                and     (hl)
                jr      z,NfNext
                push    bc
                push    hl
                ld      b,c
                ld      c,0
                call    ShadowRow               ; HL = the shadow row
                push    hl
                ld      l,b
                ld      h,0
                add     hl,hl
                add     hl,hl
                add     hl,hl
                add     hl,hl
                add     hl,hl
                ld      bc,NT
                add     hl,bc
                call    SetWrt
                pop     hl
                ld      b,SCRCOLS
                ld      c,VDPDATA
NfOut:
                outi                            ; 16, and the VDP wants 29
                nop                             ; between OUTs during the
                jp      nz,NfOut                ; display: 16+4+10 = 30
                pop     hl
                pop     bc
NfNext:
                inc     c
                ld      a,c
                cp      SCRROWS
                jr      c,NfRow
                xor     a
                ld      (DirtyRows),a
                ld      (DirtyRows+1),a
                ld      (DirtyRows+2),a
                ret

; B = row. Returns HL = its shadow row, without marking. Everything
; else is kept: SaveUnder walks with DE as its destination and the
; first version handed it ShadowNT instead, which put the lattice
; on the menu bar.
ShadowRow:
                push    de
                ld      l,b
                ld      h,0
                add     hl,hl
                add     hl,hl
                add     hl,hl
                add     hl,hl
                add     hl,hl
                ld      de,ShadowNT
                add     hl,de
                pop     de
                ret

; The menu bar, the desktop lattice with its rule, the status band.
InitScreen:
                xor     a
                ld      (PrintInv),a
                ld      a,' '
                ld      b,MENUROW
                call    FillRow
                ld      hl,TxtMenu
                ld      b,MENUROW
                ld      c,1
                call    PrintStr
                call    DrawDesktop
                ld      a,' '+INVBANK           ; the status band is inverted
                ld      b,STATROW               ; text on the inverted bank
                jp      FillRow

; The BIOS font, glyphs 32-127, into all three thirds, twice: at $20
; as itself and at $A0 for the inverted bank. CHGMOD 2 on C-BIOS
; leaves the pattern table clear, the real BIOS fills it with the
; font; copying it ourselves makes the two the same.
LoadFont:
                ld      hl,PGT+32*8
                ld      b,3
LfThird:
                push    bc
                push    hl
                call    LfBank
                pop     hl
                push    hl
                ld      bc,INVBANK*8
                add     hl,bc
                call    LfBank
                pop     hl
                ld      bc,$0800
                add     hl,bc
                pop     bc
                djnz    LfThird
                ret
LfBank:
                call    SetWrt
                ld      hl,(CGTABL)
                ld      bc,32*8
                add     hl,bc
                ld      bc,96*8
                jp      CopyVram

; Our tiles into all three thirds of the pattern table.
LoadTiles:
                ld      hl,PGT+T_LATTICE*8
                ld      b,3
LtThird:
                push    bc
                push    hl
                call    SetWrt
                ld      hl,Tiles
                ld      bc,TILESEND-Tiles
                call    CopyVram
                pop     hl
                ld      bc,$0800
                add     hl,bc
                pop     bc
                djnz    LtThird
                ret

; The colour table is per pattern per third, and the same for every
; third here: the font black on white, the inverted bank white on
; black, the tiles their own. Text in any band of the screen has the
; same two schemes, which is what windows and the status row want.
LoadColours:
                ld      hl,CT
                call    SetWrt
                ld      b,3
LcThird:
                push    bc
                ld      a,C_TEXT                ; $00-$7F, 1024 bytes
                call    Fill1K
                ld      hl,TileColours          ; our tiles at $80
                ld      bc,TILESEND-Tiles
                call    CopyVram
                ld      a,C_TEXT                ; the rest up to $A0
                ld      b,(INVBANK+32)*8-(TILESEND-Tiles)-256
                call    FillVram
                ld      a,C_INVERSE             ; $A0-$FF, 768 bytes
                call    Fill768
                pop     bc
                djnz    LcThird
                ret

Fill1K:
                ld      c,a
                ld      b,0
                call    FvLoop
                ld      a,c
Fill768:
                ld      c,a
                ld      b,0
                call    FvLoop
                ld      b,0
                call    FvLoop
                ld      b,0
                jp      FvLoop

; Sprite pattern 0 (16 by 16, 32 bytes) and the attribute table:
; sprite 0 is the pointer, sprite 1 closes the table.
LoadSprite:
                ld      hl,SPT
                call    SetWrt
                ld      hl,PtrShape
                ld      bc,32
                call    CopyVram
                ld      hl,SAT
                call    SetWrt
                ld      hl,SatInit
                ld      bc,8
                jp      CopyVram

; ---- Data
TxtMarker:      defb    "ZXMSX",0
TxtMenu:        defb    "MSX DESK  FILE   VIEW   HELP",0

; The measured default ramp: pixels per frame as the hold builds.
AccelTabs:      defb    1,1,2,2,3
AccelTab:       defb    1,2,3,5,7
                defb    2,3,5,7,11

Tiles:
                ; $80 T_LATTICE: a halftone
                defb    $AA,$55,$AA,$55,$AA,$55,$AA,$55
                ; $81 T_RULE: the same under a solid line
                defb    $FF,$55,$AA,$55,$AA,$55,$AA,$55
                ; $82 T_LEFT, $83 T_RIGHT: a window's side edges
                defb    $80,$80,$80,$80,$80,$80,$80,$80
                defb    $01,$01,$01,$01,$01,$01,$01,$01
                ; $84 T_BOTTOM, $85 T_BL, $86 T_BR
                defb    $00,$00,$00,$00,$00,$00,$00,$FF
                defb    $80,$80,$80,$80,$80,$80,$80,$FF
                defb    $01,$01,$01,$01,$01,$01,$01,$FF
                ; $87 T_CLOSE: the close box
                defb    $FF,$81,$BD,$A5,$A5,$BD,$81,$FF
TILESEND:
TileColours:
                defb    C_LATTICE,C_LATTICE,C_LATTICE,C_LATTICE
                defb    C_LATTICE,C_LATTICE,C_LATTICE,C_LATTICE
                defb    C_TEXT,C_LATTICE,C_LATTICE,C_LATTICE
                defb    C_LATTICE,C_LATTICE,C_LATTICE,C_LATTICE
                defb    C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT
                defb    C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT
                defb    C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT
                defb    C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT
                defb    C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT
                defb    C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT,C_TEXT

; The arrow, eleven rows, in the left half of a 16 by 16 sprite.
PtrShape:
                defb    %10000000
                defb    %11000000
                defb    %11100000
                defb    %11110000
                defb    %11111000
                defb    %11111100
                defb    %11111110
                defb    %11111000
                defb    %11011000
                defb    %10001100
                defb    %00001100
                defb    0,0,0,0,0
                defs    16,0

SatInit:        defb    89,120,0,C_POINTER      ; y-1, x, pattern, colour
                defb    $D0,0,0,0               ; end of table

                include "events.inc"
                include "kbd.inc"
                include "heap.inc"
                include "storage.inc"
                include "disk.inc"
                include "settings.inc"
                include "hittest.inc"
                include "app.inc"
                include "calendar.inc"
                include "menus.inc"
                include "windows.inc"
                include "arrange.inc"
                include "clock.inc"
                include "dialog.inc"
                include "note.inc"
                include "commander.inc"
                include "test.inc"

RomEnd:
                defs    $C000-RomEnd,$FF
                end
