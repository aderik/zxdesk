; MSX format v6: magic 'M', version, ramp (0..2), mouse Y inverted
; (0 = as the hand moves, 1 = upside down), backend id, sound (0/1),
; key pointer (0/1), lattice (0..2), then the six desktop icons at
; three bytes each: present (0/1), cell column, cell row, then the
; notepad's word wrap (0/1). Separate magic from the ZX record.
; Version 5 ends at the icons, version 4 before them; both load
; with WRAP on.
SETMAGIC        equ     $4D
SETVERSION      equ     6
SETHDR          equ     8               ; the record before the icons
SETSIZE         equ     SETHDR+DSKTABSZ+1
SETH            equ     11              ; the window: nine rows of buttons
SETROWS         equ     9

SetDefaults:
                ld      hl,SetInitial
                ld      de,SetRec
                ld      bc,SETSIZE
                ldir
                ld      a,(SetDevice)
                ld      (SetBackend),a
                ret
SetInitial:     defb    SETMAGIC,SETVERSION,1,0,ST_RAM,1,1,1
DskDefault:                                     ; continues the record: DskInit
                defb    1, 1, 2                 ; copies these alone
                defb    1, 1, 6
                defb    1, 1, 10
                defb    1, 1, 14
                defb    1, 1, 18
                defb    1, 10, 18
                defb    1                       ; WRAP on
SetName:        defb    "SETTINGS",0

; Always use the boot device for the preferences file, even when
; the saved application backend is RAM. Restore the caller's vectors.
SetFileOpen:
                push    bc
                ld      a,(SetDevice)
                call    StSelect
                pop     bc
                ld      hl,SetName
                jp      StOpen
SetRestore:
                pop     bc                      ; saved backend in B
                push    af
                ld      a,b
                call    StSelect
                pop     af
                ret

; Carry means defaults were installed. Reject I/O errors, short and
; oversized records as well as foreign magic/version bytes.
SetLoad:
                call    StIdent
                push    af
                ld      a,(SetDevice)
                cp      ST_TAPE                 ; never wait for tape at boot
                jp      z,SetLoadBad
                ld      b,FA_READ
                call    SetFileOpen
                jp      c,SetLoadBad
                ld      (SetHandle),a
                ld      hl,SetRec
                ld      bc,SETSIZE+1
                call    StRead
                push    af
                push    bc
                ld      a,(SetHandle)
                call    StClose
                pop     bc
                pop     de                      ; E = read flags
                jp      c,SetLoadBad
                bit     0,e
                jp      nz,SetLoadBad
                ld      a,b
                or      a
                jp      nz,SetLoadBad
                ld      a,(SetRec)
                cp      SETMAGIC
                jr      nz,SetLoadBad
                ld      a,(SetRec+1)
                cp      1
                jr      z,SetLoadV1
                cp      2
                jr      z,SetLoadV2
                cp      3
                jr      z,SetLoadV3
                cp      4
                jr      z,SetLoadV4
                cp      5
                jr      z,SetLoadV5
                cp      SETVERSION
                jr      nz,SetLoadBad
                ld      a,c
                cp      SETSIZE
                jr      nz,SetLoadBad
                or      a
                jp      SetRestore
SetLoadV5:
                ld      a,c
                cp      SETSIZE-1               ; the icons, no wrap byte
                jr      nz,SetLoadBad
                jr      SetLoadWrap
SetLoadV1:
                ld      a,c
                cp      5
                jr      nz,SetLoadBad
                ld      a,1
                ld      (SetSound),a
                jr      SetLoadUpgrade
SetLoadV2:
                ld      a,c
                cp      6
                jr      nz,SetLoadBad
SetLoadUpgrade:
                ld      a,1
                ld      (SetKeyPtr),a
                jr      SetLoadLattice
SetLoadV3:
                ld      a,c
                cp      7
                jr      nz,SetLoadBad
SetLoadLattice:
                ld      a,1
                ld      (SetLattice),a
                jr      SetLoadDesk
SetLoadV4:
                ld      a,c
                cp      SETHDR
                jr      nz,SetLoadBad
SetLoadDesk:
                call    DskInit                 ; the icons where a fresh
SetLoadWrap:                                    ; machine has them
                ld      a,1
                ld      (SetWrap),a             ; and the notepad wrapping
                ld      a,SETVERSION
                ld      (SetRec+1),a
                or      a
                jp      SetRestore
SetLoadBad:
                call    SetDefaults
                scf
                jp      SetRestore

; Preserve write and close failures. SetApply validates payload fields.
SetSave:
                ld      a,SETMAGIC
                ld      (SetRec),a
                ld      a,SETVERSION
                ld      (SetRec+1),a
SetWriteRecord:
                call    StIdent
                push    af
                ld      b,FA_OVERWRITE
                call    SetFileOpen
                jp      c,SetRestore
                ld      (SetHandle),a
                ld      hl,SetRec
                ld      bc,SETSIZE
                call    StWrite
                push    af
                push    bc
                ld      a,(SetHandle)
                call    StClose
                pop     bc
                pop     de
                jr      c,SetSaveBad
                bit     0,e
                jr      nz,SetSaveBad
                ld      a,b
                or      a
                jr      nz,SetSaveBad
                ld      a,c
                cp      SETSIZE
                jr      nz,SetSaveBad
                or      a
                jp      SetRestore
SetSaveBad:
                scf
                jp      SetRestore

SetApply:
                call    SetApplyLattice
                call    DskApply
                ld      a,(SetWrap)
                cp      2
                jr      c,SetWrapOK
                ld      a,1
                ld      (SetWrap),a
SetWrapOK:
                ld      a,(SetKeyPtr)
                cp      2
                jr      c,SetKeyPtrOK
                ld      a,1
                ld      (SetKeyPtr),a
SetKeyPtrOK:
                ld      a,(SetSound)
                cp      2
                jr      c,SetSoundOK
                ld      a,1
                ld      (SetSound),a
SetSoundOK:
                ld      a,(SetSpeed)
                cp      3
                jr      c,SetSpeedOK
                ld      a,1
                ld      (SetSpeed),a
SetSpeedOK:
                ld      e,a
                add     a,a
                add     a,a
                add     a,e
                ld      e,a
                ld      d,0
                ld      hl,AccelTabs
                add     hl,de
                ld      (AccelPtr),hl
                ld      a,(SetInvertY)
                cp      2
                jr      c,SetYOK
                xor     a                       ; anything but 0 or 1 is a
                ld      (SetInvertY),a          ; damaged file, not a wish
SetYOK:                                         ; to have the axis upside down
                ld      a,(SetBackend)
                cp      ST_RAM
                jr      z,SetBackendOK
IF TAPE
                cp      ST_TAPE
                jr      z,SetBackendOK
ENDIF
                cp      ST_BANK
                jr      z,SetBackendAvail
                cp      ST_DISK
                jr      nz,SetBackendDefault
SetBackendAvail:
                ld      b,a
                call    StAvail                 ; a bank, a BDOS
                ld      a,b
                jr      nz,SetBackendDefault
SetBackendOK:
                jp      StSelect
SetBackendDefault:
                ld      a,(SetDevice)
                cp      ST_RAM
                jr      z,SetFallbackOK
IF TAPE
                cp      ST_TAPE
                jr      z,SetFallbackOK
ENDIF
                cp      ST_DISK
                jr      nz,SetFallbackRam
                call    DskPresent
                ld      a,ST_DISK
                jr      z,SetFallbackOK
SetFallbackRam:
                ld      a,ST_RAM
SetFallbackOK:
                ld      (SetBackend),a
                jp      StSelect

; Change only pattern bytes, never the shadow/name table or window buffers.
; Screen 2 has a separate pattern bank for each third of the screen.
SetApplyLattice:
                ld      a,(SetLattice)
                cp      3
                jr      c,SetLatticeOK
                ld      a,1
                ld      (SetLattice),a
SetLatticeOK:
                add     a,a
                add     a,a
                add     a,a
                ld      e,a
                ld      d,0
                ld      hl,SetPatterns
                add     hl,de
                ex      de,hl
                ld      hl,PGT+T_LATTICE*8
                ld      b,3
SetLatticeBank:
                push    bc
                push    hl
                push    de
                call    SetWrt
                ex      de,hl
                push    hl
                ld      bc,8
                call    CopyVram
                ; The rule follows immediately; replace only its top row.
                ld      a,$FF
                out     (VDPDATA),a
                pop     hl
                inc     hl
                ld      bc,7
                call    CopyVram
                pop     de
                pop     hl
                ld      bc,$0800
                add     hl,bc
                pop     bc
                djnz    SetLatticeBank
                ret
SetPatterns:    defb    0,0,0,0,0,0,0,0
                defb    $AA,$55,$AA,$55,$AA,$55,$AA,$55
                defb    $FF,$55,$FF,$55,$FF,$55,$FF,$55

; A = the mouse's raw dy. An MSX mouse reports the negative of the
; movement, so the screen delta is always the negation; the setting
; flips it once more. 0 is therefore a mouse that moves the pointer
; the way the hand went, which is what a default should be: the first
; version had 0 mean "no negation" and a SETTINGS file the harness
; left on the disk image turned the Y axis upside down on a real
; screen.
SetMouseY:
                neg
                push    bc
                ld      b,a
                ld      a,(SetInvertY)
                or      a
                ld      a,b
                pop     bc
                ret     z
                neg
                ret

SetOpen:
                ld      a,APP_SETTINGS
                jp      WndOpenLater
SetInit:
                xor     a
                ld      (SetRow),a
                ret

; Nine rows: cyclic values, SAVE, DONE. Focus is per window.
SetKey:
                cp      KEY_ESC
                jp      z,SetDone
                cp      9
                jp      z,SetNext
                cp      KEY_DOWN
                jp      z,SetNext
                cp      KEY_UP
                jp      z,SetPrev
                ld      b,1
                cp      KEY_RIGHT
                jp      z,SetChange
                cp      KEY_ENTER
                jp      z,SetChange
                cp      ' '
                jp      z,SetChange
                ld      b,2                       ; -1 modulo three
                cp      KEY_LEFT
                jp      z,SetChange
                xor     a
                ret
SetNext:
                ld      a,(SetRow)
                inc     a
                cp      SETROWS
                jp      c,SetFocus
                xor     a
                jp      SetFocus
SetPrev:
                ld      a,(SetRow)
                sub     1
                jp      nc,SetFocus
                ld      a,SETROWS-1
; A = the row to focus. The two buttons change places in the live
; buffer, their cells flipped between the banks, which is what SetDraw
; would compose: the whole window (sixteen prints, 4.5 ms) stays as it
; is. Returns 2: rows to blit, nothing to recompose.
SetFocus:
                ld      hl,SetRow
                ld      b,(hl)
                ld      (hl),a
                cp      b
                jr      z,SetRowsOnly
                push    af
                ld      a,b
                call    SetFlipButton
                pop     af
                call    SetFlipButton
SetRowsOnly:
                call    RowsLiveText
                ld      a,2
                ret
SetChanged:
                ld      a,1
                ret
; A = a row: its button's six cells, the other bank.
SetFlipButton:
                ld      b,a
                inc     b
                ld      c,16
                call    WinCell
                ld      b,6
SetFlipCell:
                ld      a,(hl)
                xor     INVBANK
                ld      (hl),a
                inc     hl
                djnz    SetFlipCell
                ret
SetChange:
                ld      a,(SetRow)
                cp      8
                jp      z,SetDone
                cp      7
                jp      z,SetStore
                cp      6
                jp      z,SetCycleWrap
                cp      5
                jp      z,SetCycleLattice
                cp      4
                jp      z,SetCycleKeyPtr
                cp      3
                jp      z,SetCycleSound
                cp      2
                jp      z,SetCycleBackend
                cp      1
                jp      z,SetCycleY
                ld      a,(SetSpeed)
                add     a,b
                cp      3
                jp      c,SetCycleSpeed
                sub     3
SetCycleSpeed:
                ld      (SetSpeed),a
                jp      SetActivate
SetCycleLattice:
                ld      a,(SetLattice)
                add     a,b
                cp      3
                jr      c,SetCycleLatticeStore
                sub     3
SetCycleLatticeStore:
                ld      (SetLattice),a
                jp      SetActivate
SetCycleKeyPtr:
                ld      a,(SetKeyPtr)
                xor     1
                ld      (SetKeyPtr),a
                jp      SetActivate
; Every open notepad is composed again, one a frame (NoteLayService):
; the setting is global, the layout is per window.
SetCycleWrap:
                ld      a,(SetWrap)
                xor     1
                ld      (SetWrap),a
                ld      a,1
                ld      (NwToggled),a
                call    WndFlush
                ld      hl,AppNote
                call    SetTouchApp
                jp      SetMark                 ; nothing for SetApply to do
SetCycleSound:
                ld      a,(SetSound)
                xor     1
                ld      (SetSound),a
                jp      SetActivate
SetCycleY:
                ld      a,(SetInvertY)
                xor     1
                ld      (SetInvertY),a
                jp      SetActivate
; B = 1 for the next backend round RAM, BANK, DISK, 2 for the previous
SetCycleBackend:
                ld      a,(SetBackend)
                call    StRingStep
SetCycleDevice:
                ld      (SetBackend),a
SetActivate:
                call    SetApply
                ; Values are global, but each window has its own buffer
                ; and focus. The live window gets the button printed again
                ; where it is; every other SETTINGS slot is marked, without
                ; selecting it. Returns 2: rows to blit, no recompose.
SetMark:
                call    WndFlush
                ld      hl,AppSettings
                call    SetTouchApp
                ld      a,(SetRow)
                inc     a
                ld      b,a
                call    SetDrawRow
                call    RowsLiveText
                ld      a,2
                ret

; HL = an application descriptor: every window of it is recomposed
; and its rows repainted. The live record is flushed by the caller.
SetTouchApp:
                push    hl
                pop     ix
                ld      a,(WndCount)
                or      a
                ret     z
                ld      b,a
                ld      c,0
SetTouchSlot:
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
                push    ix
                pop     hl
                or      a
                sbc     hl,de
                jr      nz,SetTouchNext
                ld      a,(WndCur)
                cp      c
                jr      z,SetTouchNext          ; the live one prints its own
                ld      a,c
                call    WndTouch
                ld      a,c
                call    RowsAddSlotText         ; its content: the desktop under it stays
SetTouchNext:
                pop     bc
                inc     c
                djnz    SetTouchSlot
                ret
SetStore:
                call    SetSave
                jp      nc,SetChanged
                ld      hl,SetSaveError
                ld      de,0
                call    DlgAlert
                xor     a
                ret
SetDone:
                call    WndClose
                xor     a                       ; do not touch the closed slot
                ret

; Called after focusing a body hit. Derive coordinates again because
; WndFocusTo uses BC. Only the nine visible button rectangles activate.
SetPress:
                ld      a,(PtrX)
                srl     a
                srl     a
                srl     a
                ld      hl,WinX
                sub     (hl)
                cp      16
                ret     c
                cp      22
                ret     nc
                ld      hl,WinW
                ld      d,(hl)
                dec     d
                cp      d
                ret     nc
                ld      a,(PtrY)
                srl     a
                srl     a
                srl     a
                ld      hl,WinY
                sub     (hl)
                ld      hl,WinH
                ld      d,(hl)
                dec     d
                cp      d
                ret     nc
                dec     a
                cp      SETROWS
                ret     nc
                call    SetFocus                ; the focus moves to the row hit
                ld      b,1
                call    SetChange
                or      a
                ret     z
                cp      2
                ret     z                       ; printed and marked already
                ld      a,(WndCur)
                call    WndTouch
                jp      RowsAddLive

; The seven labels are one block in ROM, a row of SETLABELW cells
; each, copied into the rows: seven WinPrints measured 2.2 ms of the
; 5.1 ms SetDraw, and the SETTINGS open over a notepad (WndOpenLater
; composes in the frame of the open) was 14.9 ms, one 60 Hz frame.
SETLABELW       equ     14
SetDraw:
                xor     a
                ld      (PrintInv),a
                ld      hl,SetLabels
                ld      b,1
SetDrawLabels:
                push    bc
                push    hl
                ld      c,1
                call    WinCell
                ex      de,hl
                pop     hl
                ld      bc,SETLABELW
                ldir
                pop     bc
                inc     b
                ld      a,b
                cp      8
                jr      c,SetDrawLabels
                ld      b,1
SetDrawLoop:
                push    bc
                call    SetDrawRow
                pop     bc
                inc     b
                ld      a,b
                cp      SETROWS+1
                jr      c,SetDrawLoop
                xor     a
                ld      (PrintInv),a
                ret

; B = a button row, 1 to SETROWS: its label as the setting stands,
; inverted when it has the focus; the bank's size after its label.
SetDrawRow:
                ld      a,b
                dec     a
                jr      z,SetDrawSpeed
                dec     a
                jr      z,SetDrawY
                dec     a
                jr      z,SetDrawBackend
                dec     a
                jr      z,SetDrawSound
                dec     a
                jr      z,SetDrawKeyPtr
                dec     a
                jr      z,SetDrawLattice
                dec     a
                jr      z,SetDrawWrap
                dec     a
                ld      hl,SetSaveLabel
                jp      z,SetButton
                ld      hl,SetDoneLabel
                jp      SetButton
SetDrawSpeed:
                ld      a,(SetSpeed)
                ld      hl,SetSlow
                or      a
                jp      z,SetButton
                ld      hl,SetMed
                dec     a
                jp      z,SetButton
                ld      hl,SetFast
                jp      SetButton
SetDrawY:
                ld      a,(SetInvertY)
                jr      SetDrawOnOff
SetDrawSound:
                ld      a,(SetSound)
                jr      SetDrawOnOff
SetDrawKeyPtr:
                ld      a,(SetKeyPtr)
                jr      SetDrawOnOff
SetDrawWrap:
                ld      a,(SetWrap)
SetDrawOnOff:
                ld      hl,SetOff
                or      a
                jp      z,SetButton
                ld      hl,SetOn
                jp      SetButton
SetDrawLattice:
                ld      a,(SetLattice)
                ld      hl,SetEmpty
                or      a
                jp      z,SetButton
                ld      hl,SetDots
                dec     a
                jp      z,SetButton
                ld      hl,SetDense
                jp      SetButton
SetDrawBackend:
                ld      a,(SetBackend)
                ld      hl,SetRamLabel
                cp      ST_RAM
                jr      z,SetDrawNoBank
                ld      hl,SetTapeLabel
                cp      ST_TAPE
                jr      z,SetDrawNoBank
                ld      hl,SetBankLabel
                cp      ST_BANK
                jr      z,SetDrawBank
                ld      hl,SetDiskLabel
SetDrawNoBank:
                call    SetButton
                ld      hl,SetNoKb              ; where the bank's size was
                jr      SetDrawKb
SetDrawBank:
                call    SetButton
                call    BkKbText                ; the bank's size after the label
SetDrawKb:
                xor     a
                ld      (PrintInv),a
                ld      bc,$0309
                jp      WinPrint
SetNoKb:        defb    "       ",0
SetButton:
                ld      a,(SetRow)
                inc     a
                cp      b
                ld      a,0
                jr      nz,SetButtonNormal
                ld      a,INVBANK
SetButtonNormal:
                ld      (PrintInv),a
                ld      c,16
                jp      WinPrint
SetTitle:       defb    "SETTINGS",0
SetLabels:      defb    "POINTER RAMP  "
                defb    "MOUSE Y INVERT"
                defb    "BACKEND       "
                defb    "SOUND         "
                defb    "KEY PTR       "
                defb    "LATTICE       "
                defb    "WRAP          "
SetSlow:        defb    "[SLOW]",0
SetMed:         defb    "[MED ]",0
SetFast:        defb    "[FAST]",0
SetOff:         defb    "[OFF ]",0
SetOn:          defb    "[ON  ]",0
SetRamLabel:    defb    "[RAM ]",0
SetTapeLabel:   defb    "[TAPE]",0
SetDiskLabel:   defb    "[DISK]",0
SetBankLabel:   defb    "[BANK]",0
SetSaveLabel:   defb    "[SAVE]",0
SetDoneLabel:   defb    "[DONE]",0
SetSaveError:   defb    "COULD NOT SAVE",0


SetEmpty:       defb    "[NONE]",0
SetDots:        defb    "[DOTS]",0
SetDense:       defb    "[GRID]",0
