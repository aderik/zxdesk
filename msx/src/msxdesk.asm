; ============================================================
;  ZX Desk for MSX1: phase 0 boot ROM
;
;  A 32K cartridge at $4000-$BFFF. It puts Screen 2 up, writes a
;  pattern into VRAM that the harness can recompute independently,
;  plants a marker in work RAM, and then sits in a HALT loop that
;  copies the keyboard matrix and the joystick-port mouse into RAM
;  every frame. Nothing here is the desktop; it is the thing the
;  harness asserts on before any desktop code exists.
;
;  Copyright (c) 2026 Damian Cooper (ZX Desk, MIT).
;  MSX port work under the same licence.
; ============================================================

; ---- BIOS entry points (C-BIOS and the real one agree on these)
CHGMOD          equ     $005F           ; A = screen mode
SETWRT          equ     $0053           ; HL = VRAM address for writes

; ---- I/O
VDPDATA         equ     $98
PPIB            equ     $A9             ; keyboard column bits, active low
PPIC            equ     $AA             ; low nibble selects the row
PSGADR          equ     $A0
PSGWR           equ     $A1
PSGRD           equ     $A2

; ---- Work RAM. The harness reads this block back.
WORK            equ     $C000
Marker          equ     WORK            ; "ZXMSX",0
Frames          equ     WORK+$08        ; main loop iterations, 16 bit
KeyRows         equ     WORK+$10        ; 11 bytes, PPI rows 0..10
MouseDX         equ     WORK+$20        ; accumulated, signed 8 bit
MouseDY         equ     WORK+$21
MouseReads      equ     WORK+$22        ; how many frames read the mouse

; ---- VRAM layout after CHGMOD 2 on a BIOS with the standard tables
PGT             equ     $0000           ; 6144 bytes
NT              equ     $1800           ; 768 bytes
CT              equ     $2000           ; 6144 bytes

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

                ; Pattern table: byte n is (n and $FF) xor (n shr 8),
                ; which the harness recomputes in Python. Every OUT here
                ; is separated by far more than the 29 cycles the VDP
                ; wants during the active display, so no timing tricks
                ; are needed for this fill.
                ld      hl,PGT
                call    SETWRT
                ld      hl,0
                ld      bc,$1800
FillPGT:
                ld      a,l
                xor     h
                out     (VDPDATA),a
                inc     hl
                dec     bc
                ld      a,b
                or      c
                jr      nz,FillPGT

                ; Name table: cell n shows pattern n and $FF.
                ld      hl,NT
                call    SETWRT
                ld      hl,0
                ld      bc,$0300
FillNT:
                ld      a,l
                out     (VDPDATA),a
                inc     hl
                dec     bc
                ld      a,b
                or      c
                jr      nz,FillNT

                ; Colour table: white ink on black paper everywhere.
                ld      hl,CT
                call    SETWRT
                ld      bc,$1800
FillCT:
                ld      a,$F1
                out     (VDPDATA),a
                dec     bc
                ld      a,b
                or      c
                jr      nz,FillCT

                ; RAM marker and counters.
                ld      hl,TxtMarker
                ld      de,Marker
                ld      bc,6
                ldir
                ld      hl,0
                ld      (Frames),hl
                xor     a
                ld      (MouseDX),a
                ld      (MouseDY),a
                ld      (MouseReads),a

MainLoop:
                ei
                halt
                call    ScanKeys
                call    ReadMouse
                ld      hl,(Frames)
                inc     hl
                ld      (Frames),hl
                jr      MainLoop

; ------------------------------------------------------------
;  Keyboard: select each of the eleven rows in turn through the
;  low nibble of PPI port C, keeping the upper bits (cassette
;  motor, caps lock LED, click) as the BIOS left them.
; ------------------------------------------------------------
ScanKeys:
                ld      hl,KeyRows
                ld      c,0
                ld      b,11
ScanRow:
                in      a,(PPIC)
                and     $F0
                or      c
                out     (PPIC),a
                in      a,(PPIB)
                ld      (hl),a
                inc     hl
                inc     c
                djnz    ScanRow
                ret

; ------------------------------------------------------------
;  Mouse in joystick port A, the standard strobe protocol: pin 8
;  toggles through PSG register 15 and each edge presents the
;  next nibble on pins 1-4, read through register 14. Four
;  nibbles make dx and dy, both signed 8 bit.
; ------------------------------------------------------------
ReadMouse:
                ld      a,$13           ; port A, pin 8 high
                call    MouseNibble
                rlca
                rlca
                rlca
                rlca
                ld      b,a
                ld      a,$03           ; pin 8 low
                call    MouseNibble
                or      b
                ld      c,a             ; dx
                ld      a,$13
                call    MouseNibble
                rlca
                rlca
                rlca
                rlca
                ld      b,a
                ld      a,$03
                call    MouseNibble
                or      b               ; dy
                ld      hl,MouseDY
                add     a,(hl)
                ld      (hl),a
                ld      hl,MouseDX
                ld      a,c
                add     a,(hl)
                ld      (hl),a
                ld      hl,MouseReads
                inc     (hl)
                ret

; A = value for register 15. Returns the low nibble of register 14.
MouseNibble:
                ld      e,a
                ld      a,15
                out     (PSGADR),a
                ld      a,e
                out     (PSGWR),a
                ld      d,8             ; settle before sampling
MnWait:         dec     d
                jr      nz,MnWait
                ld      a,14
                out     (PSGADR),a
                in      a,(PSGRD)
                and     $0F
                ret

TxtMarker:      defb    "ZXMSX",0

RomEnd:
                ; Pad to a full 32K image so every ROM type detector
                ; and every flash cart sees the same file.
                defs    $C000-RomEnd,$FF
