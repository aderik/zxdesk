; Write-only stream. Check readiness before every BIOS output: an unplugged
; printer must never enter LPTOUT's busy wait. No buffers or document edits.
PrintOpen:
                ld      a,b
                cp      FA_OVERWRITE
                jp      nz,StNoDir
                call    PrintReady
                ret     c
                ld      a,1
                ret
PrintReady:
                ld      ix,$00A8             ; LPTSTT
                call    PrintBios
                or      a
                ret     nz
                scf
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
