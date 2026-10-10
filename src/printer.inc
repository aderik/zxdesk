; Write-only stream over the BIOS printer port. LPTSTT answers busy or
; ready and nothing else: an unplugged port reads busy for ever, a real
; printer reads busy while it works through its buffer, which on a one
; line buffer is most of every job. So each output waits for ready up to
; PRINTWAIT interrupts and gives up after that, rather than failing on the
; first busy status or entering LPTOUT's unbounded wait. No buffers or
; document edits.
PRINTWAIT       equ     150                  ; 3 s at 50 Hz, 2.5 s at 60 Hz
PrintOpen:
                ld      a,b
                cp      FA_OVERWRITE
                jp      nz,StNoDir
                call    PrintReady
                ret     c
                ld      a,1
                ret
; Carry set if the printer stayed busy for PRINTWAIT interrupts or ESC
; was pressed while it was busy. IrqCnt keeps counting because PrintBios
; leaves interrupts enabled.
PrintReady:
                push    hl
                push    de
                ld      de,(IrqCnt)
PrPoll:
                ld      ix,$00A8             ; LPTSTT: A = $FF ready, 0 busy
                call    PrintBios
                or      a
                jr      nz,PrDone            ; OR cleared carry: ready
                call    PrintEsc
                jr      z,PrCancel
                ld      hl,(IrqCnt)
                or      a
                sbc     hl,de                ; interrupts waited so far
                ld      a,h
                or      a
                jr      nz,PrTimeout
                ld      a,l
                cp      PRINTWAIT
                jr      c,PrPoll
                jr      PrTimeout
; ESC gives up at once. Wait for its release, or the alert that follows
; would read the same key as its own ESC and close before it is seen.
PrCancel:
                call    PrintEsc
                jr      z,PrCancel
PrTimeout:
                ld      a,STERR_NOTFOUND     ; no printer answered
                scf
PrDone:
                pop     de
                pop     hl
                ret
; Z while ESC (row 7, bit 2) is down. DI keeps the row select ours.
PrintEsc:
                di
                in      a,(PPIC)
                and     $F0
                or      7
                out     (PPIC),a
                in      a,(PPIB)
                ei
                bit     2,a
                ret
PrintChar:
                push    af
                call    PrintReady
                jr      c,PchBusy
                pop     af
                ld      ix,$00A5             ; LPTOUT: carry means failure
                jp      PrintBios
PchBusy:
                pop     af
                scf
                ret
PrintBios:
                push    bc
                push    de
                push    hl
                push    ix
                push    iy
                ld      iy,($FCC0)           ; EXPTBL: main BIOS slot in IYH
                call    $001C                ; CALSLT
                ei
                pop     iy
                pop     ix
                pop     hl
                pop     de
                pop     bc
                ret
PrintWrite:
                push    de
                ld      de,0
PwLoop:
                ld      a,b
                or      c
                jr      z,PwDone
                ld      a,(hl)
                call    PrintChar
                jr      c,PwDone
                inc     hl
                inc     de
                dec     bc
                jr      PwLoop
PwDone:
                ld      b,d
                ld      c,e
                pop     de
                ret
PrintClose:
                ld      a,12
                jp      PrintChar

; HL = 256-byte document. Drop empty trailing chunks, join continuation
; chunks, trim padding at hard line ends, and terminate each logical line.
PrintDocument:
                push    hl
                call    StIdent
                push    af
                ld      a,ST_PRINT
                call    StSelect
                ld      b,FA_OVERWRITE
                call    StOpen
                jr      nc,PdOpened
                pop     bc
                pop     hl
                push    bc
                jr      PdRestore
PdOpened:
                pop     af
                pop     hl
                push    af
                call    PrintLines
PdRestore:
                pop     bc
                push    af
                ld      a,b
                call    StSelect
                pop     af
                ret

PrintLines:
                push    hl
                ld      d,16
                ld      e,0                  ; last nonempty chunk + 1
PdScanRow:
                ld      b,14
PdScanChar:
                ld      a,(hl)
                cp      ' '+1
                jr      c,PdScanNext
                ld      a,17
                sub     d
                ld      e,a
PdScanNext:
                inc     hl
                djnz    PdScanChar
                inc     hl
                inc     hl
                dec     d
                jr      nz,PdScanRow
                pop     hl
                ld      d,e
                ld      a,d
                or      a
                jp      z,StClose
PdRow:
                push    hl
                ld      bc,15
                add     hl,bc
                ld      e,(hl)
                dec     hl
                ld      c,14
                ld      a,e
                or      a
                jr      nz,PdLength
PdTrim:
                dec     hl
                ld      a,(hl)
                cp      ' '+1
                jr      nc,PdLength
                dec     c
                jr      nz,PdTrim
PdLength:
                pop     hl
                push    hl
                call    StWrite
                pop     hl
                ret     c
                ld      a,e
                or      a
                jr      z,PdNewline
                ld      a,d
                cp      1
                jr      nz,PdNext
PdNewline:
                push    hl
                ld      hl,PrintCRLF
                ld      bc,2
                call    StWrite
                pop     hl
                ret     c
PdNext:
                ld      bc,16
                add     hl,bc
                dec     d
                jr      nz,PdRow
                jp      StClose
PrintCRLF:      defb    13,10

NoteMenuPrint:
                call    NoteIsFront
                ret     nz
                ld      hl,NoteBuf
                call    PrintDocument
                ret     nc
PrintAlert:
                ld      hl,TxtPrintBusy
                ld      de,0
                jp      DlgAlert
TxtPrintBusy:   defb    "PRINTER NOT READY",0
