; BIOS cassette stream: long leader + "MSXT", name[13], LE length;
; short leader + payload (0..256 bytes). Not an MSX BASIC file.
; One buffered file, handle 1. No directory or delete. Reading an
; empty name accepts whichever file comes next. BIOS owns PSG
; and interrupt masking during transfers; the wrapper preserves buffers.
TAPION          equ     $00E1
TAPIN           equ     $00E4
TAPIOF          equ     $00E7
TAPOON          equ     $00EA
TAPOUT          equ     $00ED
TAPOOF          equ     $00F0

TapeOpen:
                call    NwInvalidate            ; the notepad's wrap table lives
                xor     a                       ; in TapeBuf (note.inc)
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
                ; Asked by name for the file the buffer still holds (lf-1576):
                ; the tape has played past it, so it is served from the
                ; buffer, at its start, without a leader being waited for.
                cp      FA_READ
                jr      nz,TapeFresh
                call    TapeHeld
                jr      nz,TapeFresh
                ld      hl,0
                ld      (TapePos),hl
                jr      TapeOpened
TapeFresh:
                xor     a                       ; a write takes the header and
                ld      (TapeKeep),a            ; the buffer, a read the buffer
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
                call    TapeReading
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
                ld      a,(RamName)
                or      a
                jr      nz,TapeCompare
                ld      b,4                     ; empty name: the next file, name in TapeIn+4
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
                xor     a
                ld      (TapeBusy),a
                ei
                ; The file is in: TapeIn names it, the sum says it is intact
                call    TapeSumBuf
                ld      (TapeSum),hl
                ld      a,1
                ld      (TapeKeep),a
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

; Z when the buffer still holds the file RamName asks for (lf-1576):
; TapeKeep set by the read that brought it in and dropped by whatever
; has used the buffer or the header since (a write, Commander's
; import, the notepad's wrap table and painters, which live in
; TapeBuf: NwStartJob, NoteDraw, NoteKey, NoteLayService), the name the
; header carries equal to RamName through its terminator (what a header
; holds behind the name is not part of it, lf-1602), and the bytes
; summing as they did when they came in: a scribble by a path that does
; not drop the flag sends the open to the tape, the doubt resolved the
; tape's way. An empty name is the next file on the tape, never the
; buffer.
TapeHeld:
                ld      a,(TapeKeep)
                or      a
                jr      z,ThNo
                ld      a,(RamName)
                or      a
                jr      z,ThNo
                ld      hl,RamName
                ld      de,TapeIn+4
                ld      b,13
ThName:
                ld      a,(de)
                cp      (hl)
                jr      nz,ThNo
                or      a
                jr      z,ThSum                 ; equal through the terminator
                inc     hl
                inc     de
                djnz    ThName
ThSum:
                call    TapeSumBuf
                ld      de,(TapeSum)
                or      a
                sbc     hl,de
                ret     z
ThNo:
                or      1
                ret

; HL = a sum of TapeBuf's TapeLen bytes: the sum so far rotated left a
; bit and the byte added, so a byte changed or moved changes it. 256
; bytes measured at 25,388 cycles, 7.1 ms (openMSX breakpoints either
; side), at a tape open only: after the read that brought the file in
; and when the buffer is asked for by name.
TapeSumBuf:
                ld      hl,0
                ld      de,TapeBuf
                ld      bc,(TapeLen)
TsbLoop:
                ld      a,b
                or      c
                ret     z
                add     hl,hl
                jr      nc,TsbAdd
                inc     l                       ; bit 15 round to bit 0
TsbAdd:
                ld      a,(de)
                add     a,l
                ld      l,a
                jr      nc,TsbNext
                inc     h
TsbNext:
                inc     de
                dec     bc
                jr      TsbLoop

; The BIOS holds the frame for a transfer: a read waits on a silent
; tape for ever, a recording runs as long as the file. Before the first
; block the status band says what runs and that CTRL+STOP ends it, on
; the screen now rather than at the frame's end (upstream 277c7ca,
; lf-1576; the Commander's status row says RECORDING as well, lf-1603):
; the band in the shadow, inverted as the band is, from the left with
; the key echoes cleared, and NtFlush puts out the dirty rows, the band
; and whatever a menu's close or a dialogue's answer left, so the user
; looks at the desktop while the tape runs, not at the FILE menu. The
; frame after the transfer gives the band back (TapeBandIdle).
TapeReading:
                ld      hl,TxtTapeReading
TapeNotice:
                push    hl
                call    TapeBandClear
                pop     hl
                ld      a,(PrintInv)
                push    af
                ld      a,INVBANK
                ld      (PrintInv),a
                ld      b,STATROW
                ld      c,0
                call    PrintStr
                pop     af
                ld      (PrintInv),a
                ld      a,1
                ld      (TapeShown),a
                jp      NtFlush
; The band blank (CellAddr marks the row), the echoes from the left again.
TapeBandClear:
                xor     a
                ld      (StatCol),a
                ld      a,' '+INVBANK
                ld      b,STATROW
                jp      FillRow
; Every frame, before the flush: a notice shown goes with the first
; frame after the transfer, whichever caller ran it.
TapeBandIdle:
                ld      a,(TapeShown)
                or      a
                ret     z
                xor     a
                ld      (TapeShown),a
                jr      TapeBandClear
TxtTapeReading: defb    "READING TAPE, CTRL+STOP ENDS",0

; Commander's import (lf-1575). A tape has no directory, so the next
; file is whatever header comes by: TapeNext reads header blocks into
; TapeIn until one carries the MSXT magic and passes over the others
; (another format's header, the payload a refused file left behind).
; Carry when the tape ends: TAPION found no leader, or CTRL+STOP, so
; nothing of the block was read (BC is what TapeGetBlock did not read).
; A block shorter than a header is passed over too. TapeOpen keeps its
; own seek: FILE > OPEN by name fails on the first wrong block rather
; than wait for a leader that is not coming.
TapeNext:
                xor     a                       ; TapeIn names another file than
                ld      (TapeKeep),a            ; the buffer holds (lf-1576)
                ld      hl,TapeIn
                ld      bc,19
                call    TapeBlock
                jr      nc,TnMagic
                ld      a,c
                cp      19
                jr      nz,TapeNext             ; a short block: the next one
                scf
                ret
TnMagic:
                ld      hl,TapeIn
                ld      de,TapeMagic
                ld      b,4
TnCompare:
                ld      a,(de)
                cp      (hl)
                jr      nz,TapeNext
                inc     hl
                inc     de
                djnz    TnCompare
                or      a
                ret
; HL = buffer, BC = count: a block read, the PSG and the interrupt
; mask the BIOS's meanwhile, as in TapeOpen. Carry from the BIOS.
TapeBlock:
                push    hl
                push    bc
                call    TapeReading             ; the band says so first (lf-1576)
                pop     bc
                pop     hl
                ld      a,1
                ld      (TapeBusy),a
                di
                call    TapeGetBlock
                push    af
                xor     a
                ld      (TapeBusy),a
                pop     af
                ei
                ret

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
                ld      hl,TxtCmdRecording      ; RECORDING, CTRL+STOP ENDS on the
                call    TapeNotice              ; band before the header (lf-1576)
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
