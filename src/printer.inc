; ============================================================
;  Printer backend
;  A printer is a write only device with no directory, which is
;  exactly what the capability byte was invented to describe. So
;  it is a storage backend rather than a special case bolted onto
;  the notepad, and two things fall out of that for nothing: the
;  commander gets a third pane, and copying a file onto it prints
;  the file.
;
;  The device is the ZX Printer, which is what an unmodified 48K
;  could be sold with, and the Alphacom 32 and Timex 2040 answer
;  the same port. It is driven through the ROM rather than by
;  banging port $FB here, for the same reason the tape backend
;  calls SA_BYTES: the timing is the ROM's business, it is
;  already written, and a second copy of it would be a second
;  thing to be wrong.
;
;    CHAN_OPEN  $1601  A = stream. 3 is the printer, 2 the screen
;    PRINT_A    $0010  RST 16, prints A to the current channel
;    COPY_BUFF  $0ECD  pushes the buffered line out to the stylus
;
;  Calling into ROM 1 on a 128K assumes ROM 1 is the 48K ROM, and
;  it is not: the two differ in 1177 bytes. The three entry points
;  above were compared byte for byte between 48.rom and 128-1.rom
;  and are identical in both, as are the tape backend's two. The
;  differences are six small patches below $2650 and the whole
;  $386E to $3CFF tail, which the 48K ROM leaves as $FF filler and
;  the 128K ROM 1 fills with code. Nothing this project calls is
;  in either. SetupIM2's comment is about the other end of that
;  same fact.
;
;  The same two conditions the tape backend documents apply and
;  for the same reasons: IY must be $5C3A because the ROM indexes
;  the system variables through it, and interrupts are off while
;  the stylus is being driven because that timing is counted in T
;  states. The frame counter stops for the duration, which is
;  expected and is what A3's drop count will show.
;
;  The ROM buffers a line at $5B00 before printing it. F3 had
;  taken that page as its bank staging area on the grounds that
;  nothing used it; it has moved, and bank.inc says why.
;
;  Nothing here can be checked without a printer, and the
;  headless harness has none. What it can check is that the
;  registry entry is right, that the capabilities say what the
;  device can do, and that asking a printer to read or to list
;  itself is refused rather than attempted. Whether ink reaches
;  paper is a Fuse question and Fuse answers it: it emulates the
;  ZX Printer and writes what comes out to a file.
; ============================================================
CHAN_OPEN       equ     $1601
COPY_BUFF       equ     $0ECD
CHAN_PRINTER    equ     3
CHAN_SCREEN     equ     2

; ------------------------------------------------------------
;  PrtPresent
;  Z set if a printer answers. Bit 6 of port $FB reads low when
;  one is attached and high when the bus floats, which is what an
;  empty edge connector gives.
;
;  This is asked before the ROM is called rather than after,
;  because the ROM's printer routines are written for a machine
;  that has one. Finding out by calling them and seeing what
;  happens is how a desktop becomes a machine that has to be
;  switched off and on again.
; ------------------------------------------------------------
PrtPresent:
                in      a,($FB)
                and     $40
                ret

; HL = name, B = mode. The name is dropped: a printer has one
; destination and it is not a filesystem pretending otherwise.
PrtOpen:
                ld      a,b
                cp      FA_OVERWRITE
                jr      z,PrtoHere
                ld      a,STERR_NOSUP           ; nothing to read back off
                scf                             ; a sheet of paper
                ret
PrtoHere:
                call    PrtPresent
                jr      z,PrtoGo
                ld      a,STERR_NOTFOUND        ; no printer on the bus
                scf
                ret
PrtoGo:
                ld      a,1                     ; one handle, and it is this
                or      a
                ret

; HL = buffer, BC = bytes. Returns BC = bytes accepted.
PrtWrite:
                ld      a,b
                or      c
                ret     z
                ld      (PrtLeft),bc
                ld      (PrtCount),bc
                ld      (PrtPtr),hl
                call    PrtPresent
                jr      z,PrtwGo
                ld      bc,0
                ld      a,STERR_NOTFOUND
                scf
                ret
PrtwGo:
                ld      iy,SYSVARS
                di
                ld      a,CHAN_PRINTER
                call    CHAN_OPEN
PrtwLoop:
                ld      hl,(PrtPtr)
                ld      a,(hl)
                inc     hl
                ld      (PrtPtr),hl
                rst     $10                     ; PRINT-A, to the printer
                ld      hl,(PrtLeft)
                dec     hl
                ld      (PrtLeft),hl
                ld      a,h
                or      l
                jr      nz,PrtwLoop
                ld      a,CHAN_SCREEN           ; put the channel back before
                call    CHAN_OPEN               ; anything else prints
                ei
                ld      bc,(PrtCount)
                or      a
                ret

; The buffered line goes out and the channel goes back to the screen.
; A printer's close is a form feed rather than a directory update.
PrtClose:
                call    PrtPresent
                jr      z,PrtcGo
                or      a                       ; nothing to flush, and a
                ret                             ; close that fails is no use
PrtcGo:
                ld      iy,SYSVARS
                di
                ld      a,CHAN_PRINTER
                call    CHAN_OPEN
                call    COPY_BUFF
                ld      a,CHAN_SCREEN
                call    CHAN_OPEN
                ei
                or      a
                ret

; Reading a printer is not a thing that can happen.
PrtRead:
                ld      a,STERR_NOSUP
                scf
                ret

PrtPtr:         defw    0
PrtLeft:        defw    0
PrtCount:       defw    0
