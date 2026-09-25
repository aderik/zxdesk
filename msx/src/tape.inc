; BIOS cassette stream: long leader + "MSXT", name[13], LE length;
; short leader + payload (0..256 bytes). Not an MSX BASIC file.
; One buffered file, handle 1. No directory or delete. BIOS owns PSG
; and interrupt masking during transfers; the wrapper preserves buffers.
TAPION          equ     $00E1
TAPIN           equ     $00E4
TAPIOF          equ     $00E7
TAPOON          equ     $00EA
TAPOUT          equ     $00ED
TAPOOF          equ     $00F0

TapeOpen:
                xor     a
                ld      (TapeMode),a
                ld      a,b
                cp      FA_READ
                jr      z,TapeOpenMode
                cp      FA_OVERWRITE
                jp      nz,StNoDelete
TapeOpenMode:
                push    bc
                call    RamNorm
                pop     bc
                ret     c
                ld      a,b
                ld      (TapeMode),a
                ld      hl,0
                ld      (TapeLen),hl
                ld      (TapePos),hl
                ld      hl,TapeMagic
                ld      de,TapeHdr
                ld      bc,4
                ldir
                ld      hl,RamName
                ld      bc,13
                ldir
                ld      a,(TapeMode)
                cp      FA_OVERWRITE
                jr      z,TapeOpened
                ld      a,1
                ld      (TapeBusy),a
                di
                ld      hl,TapeIn
                ld      bc,19
                call    TapeGetBlock
                jr      c,TapeLoadFail
                ld      hl,TapeHdr
                ld      de,TapeIn
                ld      b,17
TapeCompare:
                ld      a,(de)
                cp      (hl)
                jr      nz,TapeLoadFail
                inc     hl
                inc     de
                djnz    TapeCompare
                ld      hl,(TapeIn+17)
                ld      de,257
                or      a
                sbc     hl,de
                jr      nc,TapeLoadFail
                ld      bc,(TapeIn+17)
                ld      (TapeLen),bc
                ld      hl,TapeBuf
                call    TapeGetBlock
                jr      c,TapeLoadFail
                push    af
                xor     a
                ld      (TapeBusy),a
                pop     af
                ei
TapeOpened:
                ld      a,1
                or      a
                ret
TapeLoadFail:
                push    af
                xor     a
                ld      (TapeBusy),a
                pop     af
                ei
                xor     a
                ld      (TapeMode),a
                ld      a,STERR_NOTFOUND
                scf
                ret
TapeMagic:      defb    "MSXT"

; A=handle, E=required mode. Invalid operations transfer zero bytes.
TapeValid:
                cp      1
                jp      nz,TapeInvalid
                ld      a,(TapeMode)
                cp      e
                ret     z                       ; CP clears carry on equality
TapeInvalid:
                ld      bc,0
                ld      a,STERR_HANDLE
                scf
                ret

; A = handle, HL = buffer, BC = count. Returns BC = bytes moved.
TapeRead:
                ld      e,FA_READ
                call    TapeValid
                ret     c
                call    TapeAvail               ; BC = min(count, len-pos)
                ld      a,b
                or      c
                ret     z
                push    bc
                ex      de,hl                   ; DE = caller's buffer
                ld      hl,(TapePos)
                ld      bc,TapeBuf
                add     hl,bc
                pop     bc
                push    bc
                ldir
                pop     bc
                ld      hl,(TapePos)
                add     hl,bc
                ld      (TapePos),hl
                or      a
                ret

; A = handle, HL = buffer, BC = count. Returns BC = bytes accepted.
TapeWrite:
                ld      e,FA_OVERWRITE
                call    TapeValid
                ret     c
                call    TapeRoom                ; BC = min(count, max-len)
                ld      a,b
                or      c
                ret     z
                push    bc
                ld      de,(TapeLen)
                push    hl
                ld      hl,TapeBuf
                add     hl,de
                ex      de,hl                   ; DE = where it goes
                pop     hl                      ; HL = where it comes from
                pop     bc
                push    bc
                ldir
                pop     bc
                ld      hl,(TapeLen)
                add     hl,bc
                ld      (TapeLen),hl
                or      a
                ret

; BC = min(BC, TapeLen - TapePos)
TapeAvail:
                push    hl
                ld      hl,(TapeLen)
                ld      de,(TapePos)
                or      a
                sbc     hl,de
                jr      TapeClamp

; BC = min(BC, 256 - TapeLen)
TapeRoom:
                push    hl
                ld      hl,256
                ld      de,(TapeLen)
                or      a
                sbc     hl,de
TapeClamp:
                ; HL = what is available, BC = what was asked for
                push    hl
                or      a
                sbc     hl,bc
                pop     hl
                jr      c,TapcUse               ; available < asked, use it
                pop     hl
                or      a
                ret                             ; asked for fits, keep BC
TapcUse:
                ld      b,h
                ld      c,l
                pop     hl
                or      a
                ret

TapeClose:
                cp      1
                jp      nz,TapeInvalid
                ld      a,(TapeMode)
                or      a
                jp      z,TapeInvalid
                ld      b,a
                xor     a
                ld      (TapeMode),a
                ld      a,b
                cp      FA_OVERWRITE
                jr      z,TapeSave
                or      a
                ret
TapeSave:
                ld      hl,(TapeLen)
                ld      (TapeHdr+17),hl
                ld      a,1
                ld      (TapeBusy),a
                di
                ld      a,1                     ; long header leader
                ld      hl,TapeHdr
                ld      bc,19
                call    TapePutBlock
                jr      c,TapeSaveDone
                xor     a                       ; short data leader
                ld      hl,TapeBuf
                ld      bc,(TapeLen)
                call    TapePutBlock
TapeSaveDone:
                push    af
                xor     a
                ld      (TapeBusy),a
                pop     af
                ei
                ret

; BIOS calls may destroy every register. IX selects the BIOS entry;
; the wrapper preserves the buffer pointer, count and application IY.
TapeBios:
                push    bc
                push    de
                push    hl
                push    ix
                push    iy
                call    TapeBiosJump
                pop     iy
                pop     ix
                pop     hl
                pop     de
                pop     bc
                ret
TapeBiosJump:   jp      (ix)

TapeGetBlock:
                ld      ix,TAPION
                call    TapeBios
                jr      c,TapeGetDone
TapeGetLoop:
                ld      a,b
                or      c
                jr      z,TapeGetDone
                ld      ix,TAPIN
                call    TapeBios
                jr      c,TapeGetDone
                ld      (hl),a
                inc     hl
                dec     bc
                jr      TapeGetLoop
TapeGetDone:
                push    af
                ld      ix,TAPIOF
                call    TapeBios
                pop     af
                ret
TapePutBlock:
                ld      ix,TAPOON
                call    TapeBios
                jr      c,TapePutDone
TapePutLoop:
                ld      a,b
                or      c
                jr      z,TapePutDone
                ld      a,(hl)
                ld      ix,TAPOUT
                call    TapeBios
                jr      c,TapePutDone
                inc     hl
                dec     bc
                jr      TapePutLoop
TapePutDone:
                push    af
                ld      ix,TAPOOF
                call    TapeBios
                pop     de                      ; retain earlier byte failure
                ret     c
                bit     0,e
                ret     z
                scf
                ret
