; MSX format v1: magic 'M', version, ramp (0..2), mouse Y inverted
; (0 = as the hand moves, 1 = upside down), backend id. Separate magic from the ZX record.
SETMAGIC        equ     $4D
SETVERSION      equ     1
SETSIZE         equ     5

SetDefaults:
                ld      hl,SetInitial
                ld      de,SetRec
                ld      bc,SETSIZE
                ldir
                ld      a,(SetDevice)
                ld      (SetBackend),a
                ret
SetInitial:     defb    SETMAGIC,SETVERSION,1,0,ST_RAM
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
                jr      z,SetLoadBad
                ld      b,FA_READ
                call    SetFileOpen
                jr      c,SetLoadBad
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
                jr      c,SetLoadBad
                bit     0,e
                jr      nz,SetLoadBad
                ld      a,b
                or      a
                jr      nz,SetLoadBad
                ld      a,c
                cp      SETSIZE
                jr      nz,SetLoadBad
                ld      a,(SetRec)
                cp      SETMAGIC
                jr      nz,SetLoadBad
                ld      a,(SetRec+1)
                cp      SETVERSION
                jr      nz,SetLoadBad
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
                cp      ST_DISK
                jr      nz,SetBackendDefault
                call    DskPresent
                jr      nz,SetBackendDefault
                ld      a,ST_DISK
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
                jp      WndOpen
SetInit:
                xor     a
                ld      (SetRow),a
                ret

; Five rows: cyclic values, SAVE, DONE. Focus is per window.
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
                cp      5
                jp      c,SetFocus
                xor     a
                jp      SetFocus
SetPrev:
                ld      a,(SetRow)
                sub     1
                jp      nc,SetFocus
                ld      a,4
SetFocus:
                ld      (SetRow),a
SetChanged:
                ld      a,1
                ret
SetChange:
                ld      a,(SetRow)
                cp      4
                jp      z,SetDone
                cp      3
                jp      z,SetStore
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
SetCycleY:
                ld      a,(SetInvertY)
                xor     1
                ld      (SetInvertY),a
                jp      SetActivate
SetCycleBackend:
                call    DskPresent
                ld      a,ST_RAM
                jp      nz,SetCycleDevice
                ld      a,(SetBackend)
                cp      ST_DISK
                ld      a,ST_RAM
                jp      z,SetCycleDevice
                ld      a,ST_DISK
SetCycleDevice:
                ld      (SetBackend),a
SetActivate:
                call    SetApply
                ; Values are global, but each window has its own buffer
                ; and focus. Mark every SETTINGS slot without selecting it.
                call    WndFlush
                ld      a,(WndCount)
                or      a
                jp      z,SetChanged
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
                ld      hl,AppSettings
                or      a
                sbc     hl,de
                jr      nz,SetTouchNext
                ld      a,c
                call    WndTouch
                ld      a,c
                call    RowsAddSlot
SetTouchNext:
                pop     bc
                inc     c
                djnz    SetTouchSlot
                jp      SetChanged
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
; WndFocusTo uses BC. Only the five visible button rectangles activate.
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
                cp      5
                ret     nc
                ld      (SetRow),a
                ld      b,1
                call    SetChange
                or      a
                ret     z
                ld      a,(WndCur)
                call    WndTouch
                jp      RowsAddLive

SetDraw:
                xor     a
                ld      (PrintInv),a
                ld      hl,SetSpeedLabel
                ld      bc,$0101
                call    WinPrint
                ld      hl,SetYLabel
                ld      bc,$0201
                call    WinPrint
                ld      hl,SetBackendLabel
                ld      bc,$0301
                call    WinPrint
                ld      a,(SetSpeed)
                ld      hl,SetSlow
                or      a
                jr      z,SetDrawSpeed
                ld      hl,SetMed
                dec     a
                jr      z,SetDrawSpeed
                ld      hl,SetFast
SetDrawSpeed:
                ld      b,1
                call    SetButton
                ld      a,(SetInvertY)
                ld      hl,SetOff
                or      a
                jr      z,SetDrawY
                ld      hl,SetOn
SetDrawY:
                ld      b,2
                call    SetButton
                ld      a,(SetBackend)
                ld      hl,SetRamLabel
                cp      ST_RAM
                jr      z,SetDrawBackend
                ld      hl,SetTapeLabel
                cp      ST_TAPE
                jr      z,SetDrawBackend
                ld      hl,SetDiskLabel
SetDrawBackend:
                ld      b,3
                call    SetButton
                ld      hl,SetSaveLabel
                ld      b,4
                call    SetButton
                ld      hl,SetDoneLabel
                ld      b,5
                call    SetButton
                xor     a
                ld      (PrintInv),a
                ret
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
SetSpeedLabel:  defb    "POINTER RAMP",0
SetYLabel:      defb    "MOUSE Y INVERT",0
SetBackendLabel: defb   "BACKEND",0
SetSlow:        defb    "[SLOW]",0
SetMed:         defb    "[MED ]",0
SetFast:        defb    "[FAST]",0
SetOff:         defb    "[OFF ]",0
SetOn:          defb    "[ON  ]",0
SetRamLabel:    defb    "[RAM ]",0
SetTapeLabel:   defb    "[TAPE]",0
SetDiskLabel:   defb    "[DISK]",0
SetSaveLabel:   defb    "[SAVE]",0
SetDoneLabel:   defb    "[DONE]",0
SetSaveError:   defb    "COULD NOT SAVE",0
