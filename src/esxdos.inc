; ============================================================
;  E2: the esxDOS backend
;  A DivMMC with an SD card, driven through esxDOS's `RST $08`
;  API. The fifth backend behind E1's six vectors, and the first
;  one written to fit somebody else's interface rather than the
;  other way round. That was the bet the storage registry made
;  and this is where it gets tested.
;
;  Two conditions, and they are the same two tape.inc and
;  printer.inc document for the ROM: IY must be $5C3A, and
;  interrupts must be off. EsxEnter and EsxLeave carry them, so
;  when one turns out to be unnecessary it changes in one place.
;
;  A third condition is peculiar to esxDOS and is not optional:
;  **the program has to have been launched by esxDOS**, from the
;  card. Loaded from tape, every call carrying a filename fails,
;  because the copier in the EEPROM pages the DivMMC in and out
;  around every byte it fetches using bookkeeping that only the
;  card launch path sets up. docs/esxdos.md has the whole story
;  and it cost a day to find. On a real machine with a DivMMC
;  fitted this is how ZX Desk would be loaded anyway.
;
;  The registers do not line up with E1's and that is the whole
;  of the translation. E1 passes names and buffers in HL because
;  everything else in this system does; esxDOS wants them in IX.
;  So each vector is a push hl / pop ix and a drive byte.
;
;    A = '*' is the default drive. The EEPROM tests for it at
;    $0B35 and substitutes the byte at $2D46, so it is a real
;    part of the API rather than a convention.
; ============================================================
ESX_GETSETDRV   equ     $89
ESX_OPEN        equ     $9A
ESX_CLOSE       equ     $9B
ESX_READ        equ     $9D
ESX_WRITE       equ     $9E
ESX_OPENDIR     equ     $A3
ESX_READDIR     equ     $A4
ESX_UNLINK      equ     $AD

ESXDRIVE        equ     '*'             ; the default drive
ESXDIRBUF       equ     40              ; one directory entry: an
                                        ; attribute byte, a name of up to
                                        ; twelve and a terminator, then
                                        ; FAT's time and date at two bytes
                                        ; each, and then the four byte size

; ------------------------------------------------------------
;  The two halves of a call
;  IY because the ROM's restart mechanism is shared, interrupts
;  off because a card transfer is not something to have an
;  interrupt land inside. Written as routines rather than inline
;  so that the day one of them proves unnecessary, it goes in one
;  place rather than six.
; ------------------------------------------------------------
EsxEnter:
                ld      iy,SYSVARS
                di
                ret

; Leaves A and the carry alone, because they are the call's
; result: EI affects neither.
EsxLeave:
                ei
                ret

; esxDOS's error numbers are its own. The ones E1 has a name for
; are translated and the rest become NOSUP, which is honest: the
; layer above can only act on the ones it knows.
;   5 no such file, 7 invalid filename, 9 drive full,
;   12 too many open files, 13 bad descriptor, 19 invalid path
EsxErr:
                cp      5
                jr      nz,EeNotFound
                ld      a,STERR_NOTFOUND
                scf
                ret
EeNotFound:
                cp      7
                jr      z,EeName
                cp      19
                jr      nz,EeFull
EeName:
                ld      a,STERR_NAME
                scf
                ret
EeFull:
                cp      9
                jr      nz,EeHandle
                ld      a,STERR_FULL
                scf
                ret
EeHandle:
                cp      12
                jr      z,EeIsHandle
                cp      13
                jr      nz,EeOther
EeIsHandle:
                ld      a,STERR_HANDLE
                scf
                ret
EeOther:
                ld      a,STERR_NOSUP
                scf
                ret

; ------------------------------------------------------------
;  EsxOpen
;  in:  HL = name, B = mode
;  out: A = handle, carry on error
;  E1's modes are esxDOS's, which storage.inc said when it wrote
;  FA_READ and FA_OVERWRITE down: they were taken from this API
;  in the first place, so there is nothing to translate.
; ------------------------------------------------------------
EsxOpen:
                call    EsxEnter
                push    hl
                pop     ix
                ld      a,ESXDRIVE
                rst     $08
                defb    ESX_OPEN
                call    EsxLeave
                ret     nc
                jp      EsxErr

; A = handle.
EsxClose:
                call    EsxEnter
                rst     $08
                defb    ESX_CLOSE
                call    EsxLeave
                ret     nc
                jp      EsxErr

; in:  A = handle, HL = buffer, BC = count
; out: BC = bytes read
EsxRead:
                call    EsxEnter
                push    hl
                pop     ix
                rst     $08
                defb    ESX_READ
                call    EsxLeave
                ret     nc
                jp      EsxErr          ; BC is left as esxDOS returned it:
                                        ; a partial read is documented and
                                        ; zeroing it reports a half written
                                        ; file as an untouched one

; in:  A = handle, HL = buffer, BC = count
; out: BC = bytes written
EsxWrite:
                call    EsxEnter
                push    hl
                pop     ix
                rst     $08
                defb    ESX_WRITE
                call    EsxLeave
                ret     nc
                jp      EsxErr          ; as above: the partial count stands

; ------------------------------------------------------------
;  EsxDirEnt
;  in:  A = the nth file that exists, counting from nought
;  out: HL = its name, DE = its length, carry if there is no nth
;
;  E1 asks for the nth entry because its caller is a panel that
;  paints row n. esxDOS offers an iterator instead, so this opens
;  the root, reads forward n+1 times and closes. That is O(n) per
;  row and so O(n squared) to paint a panel, which is the wrong
;  shape and is fine at eight rows: a directory read is a file
;  operation, not a frame.
;
;  The entry esxDOS returns is an attribute byte, then the name
;  as ASCIIZ, then its dates and its size. Directories have bit 4
;  of the attribute set and are skipped, because E1 has no notion
;  of one and a panel that offered to open a directory as a file
;  would be lying.
; ------------------------------------------------------------
EsxDirEnt:
                ld      (EsxDirWant),a
                call    EsxEnter
                ld      ix,EsxRoot
                ld      a,ESXDRIVE
                ld      b,0                     ; the access mode, and leaving
                                                ; it to chance is dangerous
                                                ; rather than untidy. On
                                                ; NextZXOS B selects long
                                                ; filename mode, which returns
                                                ; names of up to 261 bytes
                                                ; into EsxDirBuf, which is 40
                                                ; and is the last object in
                                                ; this file. esxDOS's own LS
                                                ; sets it to zero for the same
                                                ; reason.
                rst     $08
                defb    ESX_OPENDIR
                call    EsxLeave
                jp      c,EsxErr
                ld      (EsxDirH),a

EsxDeLoop:
                call    EsxEnter
                ld      a,(EsxDirH)
                ld      ix,EsxDirBuf
                rst     $08
                defb    ESX_READDIR
                call    EsxLeave
                jr      c,EsxDeEnd      ; ran off the end of the directory
                or      a
                jr      z,EsxDeEnd      ; and nought entries read means the
                                        ; same thing
                ld      a,(EsxDirBuf)
                and     $10             ; a directory, which E1 has no
                jr      nz,EsxDeLoop    ; notion of
                ld      a,(EsxDirWant)
                or      a
                jr      z,EsxDeFound
                dec     a
                ld      (EsxDirWant),a
                jr      EsxDeLoop

EsxDeEnd:
                call    EsxDirShut
                ld      a,STERR_NOTFOUND
                scf
                ret

EsxDeFound:
                call    EsxDirShut
                ld      hl,EsxDirBuf+1  ; past the attribute byte
                push    hl
EsxDeName:
                ld      a,(hl)
                inc     hl
                or      a
                jr      nz,EsxDeName
                ; What follows the terminator is FAT's time and date,
                ; two bytes each, and only then the size. Reading the
                ; four bytes straight after the name gives the stamp:
                ; the first attempt at this reported ESXMMC.BIN, which
                ; is 8,192 bytes, as $874A.
                inc     hl
                inc     hl
                inc     hl
                inc     hl
                ld      e,(hl)
                inc     hl
                ld      d,(hl)          ; the low half of a four byte size,
                                        ; which is all DE has room for and
                                        ; more than a 48K can hold anyway
                pop     hl
                or      a
                ret

; Closes the directory handle. Fifteen exist, they are not
; reference counted, and DivMMC RAM carries them across a reset,
; so leaking one leaks it until the machine is reflashed. The
; probe that found this backend's calling convention leaked one
; per run and poisoned a day of measurements doing it.
EsxDirShut:
                push    af
                call    EsxEnter
                ld      a,(EsxDirH)
                rst     $08
                defb    ESX_CLOSE
                call    EsxLeave
                pop     af
                ret

; HL = a name. Carry set if it could not be removed.
EsxDelete:
                call    EsxEnter
                push    hl
                pop     ix
                ld      a,ESXDRIVE
                rst     $08
                defb    ESX_UNLINK
                call    EsxLeave
                ret     nc
                jp      EsxErr

EsxRoot:        defb    "/",0
EsxDirH:        defb    0
EsxDirWant:     defb    0
EsxDirBuf:      defs    ESXDIRBUF
