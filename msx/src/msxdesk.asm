; ============================================================
;  ZX Desk for MSX1
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
CGTABL          equ     $0004           ; word: the BIOS font, 256 glyphs of 8 bytes
HTIMI           equ     $FD9F           ; five byte hook, called each VDP interrupt

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
MOUSEMAX        equ     16              ; largest delta believed per frame
MOUSEWAIT       equ     10              ; settle loops between strobe and sample; a real
                                        ; mouse may want more, tune on hardware

; ---- Tiles. Codes 32-127 are the BIOS font; these are ours.
T_LATTICE       equ     $80             ; the desktop halftone
T_RULE          equ     $81             ; lattice with the menu bar's rule on top

; ---- Colours, ink in the high nibble
C_BAR           equ     $1F             ; black on white: menu bar glyphs
C_LATTICE       equ     $EF             ; grey on white
C_STATUS        equ     $F1             ; white on black: status row glyphs
C_POINTER       equ     $01             ; black

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
                var     Buttons, 1      ; Kempston layout, active low: bit 1 left, bit 0 right
                var     PtrX, 1
                var     PtrY, 1
                var     Dirs, 1
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
                var     EvQueue, EVSLOTS*4
RamEnd          equ     _ram

                org     $4000
                defb    "AB"
                defw    Init
                defw    0               ; STATEMENT
                defw    0               ; DEVICE
                defw    0               ; TEXT
                defs    6,0

Init:
                ld      a,2
                call    CHGMOD
                ld      a,$0F           ; white border, as the ZX had
                out     (VDPCTRL),a
                ld      a,$87           ; register 7, backdrop
                out     (VDPCTRL),a
                call    LoadFont
                call    LoadTiles
                call    LoadColours
                call    LoadSprite
                call    InitScreen

                ld      hl,TxtMarker
                ld      de,Marker
                ld      bc,6
                ldir
                ld      hl,0
                ld      (Frames),hl
                ld      (IrqCnt),hl
                ld      (Dropped),hl
                xor     a
                ld      (HoldCnt),a
                ld      (KbdLast),a
                ld      (MouseDX),a
                ld      (MouseDY),a
                ld      a,$FF
                ld      (Buttons),a
                ld      a,120
                ld      (PtrX),a
                ld      a,90
                ld      (PtrY),a
                call    EvInit
                call    PtrUpdate
                call    SetupIrq

; ------------------------------------------------------------
;  The frame. HALT wakes on the VDP interrupt, which the BIOS
;  handler services before H.TIMI counts it. The sprite is
;  moved first, while the beam is still in the border, so the
;  pointer never tears. Input is read and dispatched at the end
;  of the frame, as before, so the model is settled before the
;  next frame paints it.
; ------------------------------------------------------------
MainLoop:
                ei
                halt
                call    FrameWatch
                call    PtrUpdate
                call    ReadInput
                call    EvPoll          ; ends in KbdPoll
                call    EvDispatch
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
;  keys with the ZX version's acceleration ramp, then SPACE as
;  the left button. The Kempston button layout is kept so
;  EvPoll did not have to change.
; ------------------------------------------------------------
ReadInput:
                ld      a,$FF
                ld      (Buttons),a
                call    ReadMouse

                ; cursor keys: row 8, bit 4 left, 5 up, 6 down, 7 right
                ld      c,0
                in      a,(PPIC)
                and     $F0
                or      8
                out     (PPIC),a
                in      a,(PPIB)
                ld      e,a
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
                ld      hl,AccelTab
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
                ; SPACE acts as the left button. Row 8 is still selected.
                in      a,(PPIB)
                bit     0,a
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
                ld      a,$13           ; port A, pin 8 high
                call    MouseNibble
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
                neg
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

; A = character, B = row, C = column.
PutChar:
                push    af
                call    CellAddr
                call    SetWrt
                pop     af
                out     (VDPDATA),a
                ret

; HL = null terminated text, B = row, C = column.
PrintStr:
                push    hl
                call    CellAddr
                call    SetWrt
                pop     hl
PsLoop:
                ld      a,(hl)
                or      a
                ret     z
                out     (VDPDATA),a
                inc     hl
                jr      PsLoop          ; 7+4+5+11+6+12 = 45 between OUTs

; B = row, C = column. Returns HL = name table address.
CellAddr:
                ld      l,b
                ld      h,0
                add     hl,hl
                add     hl,hl
                add     hl,hl
                add     hl,hl
                add     hl,hl
                ld      b,0
                add     hl,bc
                ld      bc,NT
                add     hl,bc
                ret

; A = tile, B = row: a whole row of one tile.
FillRow:
                push    af
                ld      c,0
                call    CellAddr
                call    SetWrt
                pop     af
                ld      b,SCRCOLS
                jp      FillVram

; The menu bar, the desktop lattice with its rule, the status band.
InitScreen:
                ld      a,' '
                ld      b,MENUROW
                call    FillRow
                ld      hl,TxtMenu
                ld      b,MENUROW
                ld      c,1
                call    PrintStr
                ld      a,T_RULE
                ld      b,1
                call    FillRow
                ld      b,2
IsLattice:
                push    bc
                ld      a,T_LATTICE
                call    FillRow
                pop     bc
                inc     b
                ld      a,b
                cp      STATROW
                jr      c,IsLattice
                ld      a,' '
                ld      b,STATROW
                jp      FillRow

; The BIOS font, glyphs 32-127, into all three thirds. CHGMOD 2 on
; C-BIOS leaves the pattern table clear, the real BIOS fills it with
; the font; copying it ourselves makes the two the same.
LoadFont:
                ld      hl,PGT+32*8
                ld      b,3
LfThird:
                push    bc
                push    hl
                call    SetWrt
                ld      hl,(CGTABL)
                ld      bc,32*8
                add     hl,bc
                ld      bc,96*8
                call    CopyVram
                pop     hl
                ld      bc,$0800
                add     hl,bc
                pop     bc
                djnz    LfThird
                ret

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

; The colour table is per pattern per third, so a glyph's colour
; depends on which band of the screen it sits in: the bar's third
; is black on white, the status row's third white on black, the
; middle third grey on white for the lattice. Later phases put
; text in the middle third and will want a second glyph bank.
LoadColours:
                ld      hl,CT
                call    SetWrt
                ld      a,C_BAR
                call    FillThirdGlyphs
                call    FillThirdTiles
                ld      a,C_LATTICE
                call    FillThirdGlyphs
                call    FillThirdTiles
                ld      a,C_STATUS
                call    FillThirdGlyphs
                jp      FillThirdTiles

; A = colour for the 128 font patterns of one third, 1024 bytes.
FillThirdGlyphs:
                ld      d,4
FtgLoop:
                push    de
                ld      b,0             ; 256
                push    af
                call    FillVram
                pop     af
                pop     de
                dec     d
                jr      nz,FtgLoop
                ret

; Our tiles' colours, then the rest of the third: codes $80-$FF are
; 128 patterns, 1024 bytes, of which the tiles take TILESEND-Tiles.
FillThirdTiles:
                ld      hl,TileColours
                ld      bc,TILESEND-Tiles
                call    CopyVram
                ld      a,$F1
                ld      b,0             ; 256
                call    FillVram
                ld      a,$F1
                ld      b,0
                call    FillVram
                ld      a,$F1
                ld      b,0
                call    FillVram
                ld      a,$F1
                ld      b,256-(TILESEND-Tiles)
                jp      FillVram

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
TxtMenu:        defb    "ZX DESK   FILE   VIEW   HELP",0

; The measured default ramp: pixels per frame as the hold builds.
AccelTab:       defb    1,2,3,5,7

Tiles:
                ; T_LATTICE: a halftone
                defb    $AA,$55,$AA,$55,$AA,$55,$AA,$55
                ; T_RULE: the same under a solid line
                defb    $FF,$55,$AA,$55,$AA,$55,$AA,$55
TILESEND:
TileColours:
                defb    C_LATTICE,C_LATTICE,C_LATTICE,C_LATTICE
                defb    C_LATTICE,C_LATTICE,C_LATTICE,C_LATTICE
                defb    C_BAR,C_LATTICE,C_LATTICE,C_LATTICE
                defb    C_LATTICE,C_LATTICE,C_LATTICE,C_LATTICE

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

RomEnd:
                defs    $C000-RomEnd,$FF
                end
