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
                cp      ST_DISK
                jr      nz,SetBackendDefault
                call    DskPresent
                jr      nz,SetBackendDefault
                ld      a,ST_DISK
SetBackendOK:
                jp      StSelect
SetBackendDefault:
                ld      a,(SetDevice)
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
SetDraw:
                xor     a
                ld      (PrintInv),a
                ld      (SetText+1),a
                ld      hl,SetSpeedLabel
                ld      bc,$0101
                call    WinPrint
                ld      a,(SetSpeed)
                ld      bc,$0110
                call    SetDigit
                ld      hl,SetYLabel
                ld      bc,$0201
                call    WinPrint
                ld      a,(SetInvertY)
                ld      bc,$0210
                call    SetDigit
                ld      hl,SetBackendLabel
                ld      bc,$0301
                call    WinPrint
                ld      a,(SetBackend)
                ld      hl,SetRamLabel
                cp      ST_RAM
                jr      z,SetDrawBackend
                ld      hl,SetDiskLabel
SetDrawBackend:
                ld      bc,$030E
                jp      WinPrint
SetDigit:
                add     a,'0'
                ld      (SetText),a
                ld      hl,SetText
                jp      WinPrint
SetTitle:       defb    "SETTINGS",0
SetSpeedLabel:  defb    "POINTER RAMP",0
SetYLabel:      defb    "MOUSE Y INVERT",0
SetBackendLabel: defb   "BACKEND",0
SetRamLabel:    defb    "RAM",0
SetDiskLabel:   defb    "DISK",0
