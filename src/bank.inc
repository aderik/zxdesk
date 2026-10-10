; ============================================================
;  Bank backend: a RAM disk in memory the desktop cannot see
;  The ZX 128K version kept files in the free banks; an MSX has
;  two kinds of memory the cartridge hides. On a 64K MSX1 the
;  BIOS sits in page 0 and the cartridge in pages 1 and 2, so 48K
;  of the RAM is behind them (source 1: hidden pages). A memory
;  mapper, the main RAM of an MSX2 or a cartridge on an MSX1, has
;  16K segments selected per page through ports $FC-$FF (source
;  2). BkInit measures both and keeps the larger.
;
;  Every transfer goes through BkCode, a routine copied to work
;  RAM, because it puts the page the ROM sits in on the RAM slot
;  for the length of one LDIR: under DI it reads the primary slot
;  register and the secondary one at $FFFF, selects the page,
;  copies up to 256 bytes between a page-3 buffer and the window,
;  and puts both registers back exactly. ENASLT cannot do page 0
;  and nothing in ROM can do its own page. The routine is a
;  template whose operands the ROM patches: masks and bits for
;  the two slot registers, a segment, the window and the buffer.
;
;  The disk is 256 byte blocks. The first BkSumBlocks hold a byte
;  per entry, a hash of its name or 0 while it is free; the next
;  BkDirBlocks a directory of sixteen byte entries in the RAM
;  backend's layout (name, size, used), one per data block; and
;  every file owns one block: 256 bytes, the document size, as the
;  RAM backend. Both are read in 64 byte chunks through one cache
;  and written through, so there is nothing to flush. A name is
;  looked up, a free entry found and the directory counted on the
;  summary, 64 entries a transfer; an entry is only read when its
;  hash matches, since a transfer costs 1.6 ms whatever it moves
;  and a full 512K bank has 1,859 entries. Capabilities WRITE,
;  RANDOM, DIR: it does not survive power off.
; ============================================================
BKWIN           equ     2               ; the window page for a mapper
BKPORT          equ     $FC             ; mapper register of page 0; +page
BKCHUNK         equ     64              ; four directory entries
BKCODESZ        equ     63              ; BkTpl, checked below

; ---- The transfer routine. Lives at BkCode in RAM; the ROM patches
; the operands at the offsets listed after it. Both JR displacements
; are 4 to skip a segment select and 0 to perform it.
BkTpl:
                di
                in      a,(PSLOT)
                ld      (BkCode+BkOA8),a        ; restored at the end
BkTPm:          and     $FF                     ; the page's bits cleared
BkTPb:          or      $00                     ; and the RAM slot's put in
                ld      c,a
                ld      a,($FFFF)               ; the secondary register of
BkTSx:          xor     $00                     ; page 3's primary slot reads
                ld      (BkCode+BkOSc),a        ; back inverted; or plain RAM
BkTSm:          and     $FF
BkTSb:          or      $00
                ld      ($FFFF),a               ; same byte back on plain RAM
                ld      a,c
                out     (PSLOT),a
BkTJ1:          jr      BkTLd
BkTSg:          ld      a,$00                   ; the segment
BkTPo1:         out     (BKPORT),a
BkTLd:          ld      hl,$0000
BkTDe:          ld      de,$0000
BkTBc:          ld      bc,$0000
                ldir
BkTJ2:          jr      BkTA8
BkTSh:          ld      a,$00                   ; the segment the page had
BkTPo2:         out     (BKPORT),a
BkTA8:          ld      a,$00
                out     (PSLOT),a
BkTSc:          ld      a,$00
                ld      ($FFFF),a
BkTEi:          ei
                ret
BkTplEnd:
                defs    BKCODESZ-(BkTplEnd-BkTpl)   ; negative: the template outgrew BkCode
BkOA8           equ     BkTA8+1-BkTpl
BkOSc           equ     BkTSc+1-BkTpl
BkOPm           equ     BkTPm+1-BkTpl
BkOPb           equ     BkTPb+1-BkTpl
BkOSx           equ     BkTSx+1-BkTpl
BkOSm           equ     BkTSm+1-BkTpl
BkOSb           equ     BkTSb+1-BkTpl
BkOJ1           equ     BkTJ1+1-BkTpl
BkOJ2           equ     BkTJ2+1-BkTpl
BkOSg           equ     BkTSg+1-BkTpl
BkOSh           equ     BkTSh+1-BkTpl
BkOPo1          equ     BkTPo1+1-BkTpl
BkOPo2          equ     BkTPo2+1-BkTpl
BkOHl           equ     BkTLd+1-BkTpl
BkODe           equ     BkTDe+1-BkTpl
BkOBc           equ     BkTBc+1-BkTpl
BkOEi           equ     BkTEi-BkTpl

; Moves BC bytes between the buffer HL in page 3 and the window
; address DE, with page BkPage on slot BkSlot and, unless BkSeg is
; $FF, mapper segment BkSeg under it (BkSegHome afterwards). BkDirn
; is 0 from the bank into the buffer, 1 the other way.
BkRaw:
                ld      (BkCode+BkOBc),bc
                ld      a,(BkDirn)
                or      a
                jr      nz,BkRawStore
                ld      (BkCode+BkOHl),de
                ld      (BkCode+BkODe),hl
                jr      BkRawSlot
BkRawStore:
                ld      (BkCode+BkOHl),hl
                ld      (BkCode+BkODe),de
BkRawSlot:
                ld      a,(BkPage)
                ld      c,a
                ld      a,3
                call    BkShift
                cpl
                ld      (BkCode+BkOPm),a
                ld      b,a                     ; the same mask for the secondary
                ld      a,(BkSlot)
                and     3
                call    BkShift
                ld      (BkCode+BkOPb),a
                ld      a,(BkSlot)
                bit     7,a
                jr      z,BkRawPrimary
                rrca
                rrca
                and     3
                call    BkShift
                ld      (BkCode+BkOSb),a
                ld      a,b
                ld      (BkCode+BkOSm),a
                jr      BkRawSec
BkRawPrimary:                                   ; $FFFF is written back as it was
                xor     a
                ld      (BkCode+BkOSb),a
                ld      a,$FF
                ld      (BkCode+BkOSm),a
BkRawSec:
                ; what $FFFF reads: the secondary slot register, inverted,
                ; when page 3's primary slot is expanded, else a RAM byte;
                ; either is written back as it was unless bits are changed
                in      a,(PSLOT)
                rlca
                rlca
                and     3
                ld      e,a
                ld      d,0
                ld      hl,EXPTBL
                add     hl,de
                ld      a,(hl)
                and     $80
                jr      z,BkRawSx
                ld      a,$FF
BkRawSx:
                ld      (BkCode+BkOSx),a
                ld      a,(BkPage)
                add     a,BKPORT
                ld      (BkCode+BkOPo1),a
                ld      (BkCode+BkOPo2),a
                ld      a,(BkSeg)
                ld      (BkCode+BkOSg),a
                ld      a,(BkSegHome)
                ld      (BkCode+BkOSh),a
                ld      a,(BkSeg)
                inc     a
                ld      a,4                     ; $FF: no mapper, skip both OUTs
                jr      z,BkRawJump
                xor     a
BkRawJump:
                ld      (BkCode+BkOJ1),a
                ld      (BkCode+BkOJ2),a
                jp      BkCode

; A shifted left twice for every page in C: the two bits a page has
; in a slot register. B and C are kept.
BkShift:
                push    bc
                inc     c
BkShLoop:
                dec     c
                jr      z,BkShDone
                add     a,a
                add     a,a
                jr      BkShLoop
BkShDone:
                pop     bc
                ret

; A block of the bank: DE = block, A = offset in it, HL = buffer,
; BC = length, which must stay inside the block.
BkLoad:
                push    af
                xor     a
                jr      BkXfer
BkStore:
                push    af
                ld      a,1
BkXfer:
                ld      (BkDirn),a
                pop     af
                push    hl
                push    bc
                ld      l,a                     ; the offset
                ld      a,(BkSource)
                cp      2
                jr      z,BkXferMap
                ; hidden pages, from the second block: a disk ROM keeps its
                ; RST and interrupt vectors in the first 256 bytes of page
                ; 0's RAM for the time it pages the BIOS out. Then bits 6-7
                ; of the block say which 16K.
                inc     de
                ld      a,e
                rlca
                rlca
                and     3
                ld      c,a
                ld      b,0
                push    hl
                ld      hl,BkPageTab
                add     hl,bc
                ld      a,(hl)
                ld      (BkPage),a
                ld      hl,BkSlotTab
                add     hl,bc
                ld      a,(hl)
                ld      (BkSlot),a
                pop     hl
                ld      a,$FF
                ld      (BkSeg),a
                jr      BkXferWin
BkXferMap:
                ld      a,BKWIN
                ld      (BkPage),a
                ld      a,(BkMapSlot)
                ld      (BkSlot),a
                ; the 16K index, moved up past every segment the system
                ; keeps (BkSkip is ascending)
                ld      a,e
                rlca
                rlca
                and     3
                ld      c,a
                ld      a,d
                add     a,a
                add     a,a
                or      c
                push    hl
                ld      hl,BkSkip
                ld      b,4
BkXferSkip:
                cp      (hl)
                jr      c,BkXferSeg
                inc     a
                inc     hl
                djnz    BkXferSkip
BkXferSeg:
                pop     hl
                ld      (BkSeg),a
BkXferWin:
                ; the window: page * $4000 + (block & 63) * 256 + offset
                ld      a,e
                and     63
                ld      h,a
                ld      a,(BkPage)
                rrca
                rrca
                or      h
                ld      h,a
                ex      de,hl                   ; DE = window
                pop     bc
                pop     hl
                jp      BkRaw

; ------------------------------------------------------------
;  Detection, at StInit. Page 3's slot first for every page, then
;  the other primary slots and, if page 3's is expanded, its
;  secondary ones: an expanded slot other than page 3's would need
;  page 3 itself switched to reach its register, which nothing in
;  RAM can do, and no machine puts its RAM there.
;
;  The mapper probes use page 0 as their window, so the only
;  mapper register whose value is lost is page 0's. The segments
;  under pages 1-3 are read back through markers; page 0's is
;  taken as 3 when they are 2, 1, 0 (the BIOS's layout) and as 0
;  otherwise, which is what a mapper nobody initialised has.
; ------------------------------------------------------------
BkQ             equ     CmdWork                 ; page 3's slot id
BkPg            equ     CmdWork+1               ; the page being probed
BkCn            equ     CmdWork+2               ; the candidate index
BkFound         equ     CmdWork+3               ; pages with RAM behind them
BkS1            equ     CmdWork+4               ; the segments under pages 1-3
BkS2            equ     CmdWork+5
BkS3            equ     CmdWork+6
BkMark          equ     CmdWork+7               ; the byte a probe writes
BkSave0         equ     CmdWork+8
BkSave1         equ     CmdWork+9
BkS0            equ     CmdWork+10
BkRead          equ     CmdWork+11              ; a probe's read back; a transfer leaves BC 0
BkWit           equ     CmdWork+12              ; source 1's witness: $FF page 3, else a found page
BkSlotC         equ     BkS1                    ; source 1's candidate slot; BkS1 is source 2's

BkInit:
                ld      hl,BkTpl
                ld      de,BkCode
                ld      bc,BKCODESZ
                ldir
                xor     a
                ld      (BkSource),a
                ld      (BkMode),a
                ld      (BkSegHome),a
                ld      (BkFound),a
                ld      hl,0
                ld      (BkBlocks),hl
                ld      (BkTop),hl
                ld      (BkFreeHint),hl
                dec     hl
                ld      (BkCache),hl
                ld      a,$FF
                ld      (BkSkip),a
                ld      (BkSeg),a
                ; page 3's slot id: primary from the register, secondary
                ; from SLTTBL when that primary slot is expanded
                in      a,(PSLOT)
                rlca
                rlca
                and     3
                ld      c,a
                ld      b,0
                ld      hl,EXPTBL
                add     hl,bc
                bit     7,(hl)
                ld      a,c
                jr      z,BkInitQ
                ld      hl,SLTTBL
                add     hl,bc
                ld      a,(hl)
                rrca                            ; page 3's bits, 7-6, to 3-2
                rrca
                rrca
                rrca
                and     $0C
                or      c
                or      $80
BkInitQ:
                ld      (BkQ),a
                ; ---- source 1: for pages 0-2, the first slot with RAM behind it
                xor     a
                ld      (BkPg),a
BkInitPage:
                xor     a
                ld      (BkCn),a
BkInitCand:
                call    BkCand
                jr      c,BkInitPageNext
                ld      (BkSlot),a
                ld      a,(BkPg)
                ld      (BkPage),a
                call    BkProbeRam
                jr      nz,BkInitCand
                call    BkProbeOwn
                jr      nz,BkInitCand
                ld      a,(BkFound)
                ld      c,a
                ld      b,0
                ld      hl,BkPageTab
                add     hl,bc
                ld      a,(BkPg)
                ld      (hl),a
                ld      hl,BkSlotTab
                add     hl,bc
                ld      a,(BkSlot)
                ld      (hl),a
                ld      hl,BkFound
                inc     (hl)
BkInitPageNext:
                ld      hl,BkPg
                inc     (hl)
                ld      a,(hl)
                cp      3
                jr      c,BkInitPage
                ld      a,(BkFound)
                or      a
                jr      z,BkInitMapper
                ld      l,a
                ld      h,0
                call    BkTimes64
                dec     hl                      ; less the first block, the disk ROM's
                ld      (BkBlocks),hl
                ld      a,1
                ld      (BkSource),a
                ; ---- source 2: a memory mapper in any slot, page 0 the window
BkInitMapper:
                xor     a
                ld      (BkCn),a
BkInitMap:
                call    BkCand
                jr      c,BkInitFormat
                ld      (BkSlot),a
                xor     a
                ld      (BkPage),a
                call    BkProbeRam
                jr      nz,BkInitMap
                call    BkProbeMapper
                jr      nz,BkInitMap
                ld      a,(BkSlot)
                ld      (BkMapSlot),a
                call    BkMapperSize            ; HL = segments, 1..256
                call    BkMapperUsed            ; BkSkip, HL = segments left
                call    BkTimes64
                ld      de,(BkBlocks)
                or      a
                sbc     hl,de
                jr      z,BkInitFormat
                jr      c,BkInitFormat          ; the hidden pages are more
                add     hl,de
                ld      (BkBlocks),hl
                ld      a,2
                ld      (BkSource),a
BkInitFormat:
                ; the summary: a block per 256, which is more entries than
                ; there are; the directory: a block per seventeen of the
                ; rest, which holds the entries of the other sixteen; both
                ; cleared
                ld      hl,(BkBlocks)
                ld      a,h
                or      l
                ret     z
                ld      a,l
                or      a
                ld      l,h
                ld      h,0
                jr      z,BkInitSum
                inc     hl
BkInitSum:
                ld      (BkSumBlocks),hl
                ex      de,hl
                ld      hl,(BkBlocks)
                or      a
                sbc     hl,de
                ld      de,16
                add     hl,de
                ld      de,17
                call    BkDiv                   ; HL = (blocks - summary + 16) / 17
                ld      (BkDirBlocks),hl
                ld      de,(BkSumBlocks)
                add     hl,de
                ld      (BkDataBlock),hl
                ex      de,hl
                ld      hl,(BkBlocks)
                or      a
                sbc     hl,de
                ld      (BkFiles),hl
                ld      hl,CmdBuf
                ld      de,CmdBuf+1
                ld      bc,255
                ld      (hl),0
                ldir
                ld      de,0
BkInitClear:
                push    de
                ld      hl,CmdBuf
                ld      bc,256
                xor     a
                call    BkStore
                pop     de
                inc     de
                ld      hl,(BkDataBlock)
                or      a
                sbc     hl,de
                jr      nz,BkInitClear
                ret

; HL = HL * 64
BkTimes64:
                ld      b,6
BkT64:          add     hl,hl
                djnz    BkT64
                ret

; HL = HL / DE, by subtraction: it divides a block count by 17.
BkDiv:
                ld      bc,0
BkDivLoop:
                or      a
                sbc     hl,de
                jr      c,BkDivDone
                inc     bc
                jr      BkDivLoop
BkDivDone:
                ld      h,b
                ld      l,c
                ret

; The next candidate slot id from BkCn, which it advances: index 0
; is page 3's slot, then every primary slot that is not expanded and,
; for page 3's primary slot when it is, its secondary slots. Carry
; when there are no more.
BkCand:
                ld      a,(BkCn)
                cp      17
                ccf
                ret     c
                ld      hl,BkCn
                inc     (hl)
                or      a
                jr      nz,BkCandN
                ld      a,(BkQ)
                ret
BkCandN:
                dec     a
                ld      c,a                     ; primary in bits 2-3, secondary 0-1
                rrca
                rrca
                and     3
                ld      e,a                     ; the primary slot
                ld      a,(BkQ)
                and     3
                cp      e
                jr      z,BkCandQ
                ld      d,0
                ld      hl,EXPTBL
                add     hl,de
                bit     7,(hl)
                jr      nz,BkCand               ; expanded, not page 3's: unreachable
                ld      a,c
                and     3
                jr      nz,BkCand               ; one id for a plain slot
                ld      a,e
                ret
BkCandQ:
                ld      a,(BkQ)
                bit     7,a
                jr      z,BkCand                ; plain: index 0 had it
                ld      a,c
                and     3
                rlca
                rlca
                or      e
                or      $80
                ld      c,a
                ld      a,(BkQ)
                cp      c
                jr      z,BkCand                ; index 0 had it
                ld      a,c
                ret

; One byte from or to the window at the probe's offset in BkPage,
; the offset of BkProbe so that a mapper aliasing the page with page
; 3 lands on that byte and nothing else.
BkProbeGet:
                xor     a
                ld      (BkDirn),a
                ld      hl,BkMark
                call    BkProbeWin
                ld      a,(BkMark)
                ret
BkProbePut:                                     ; A = the byte
                ld      (BkMark),a
                ld      a,1
                ld      (BkDirn),a
                ld      hl,BkMark
BkProbeWin:
                ld      de,BkProbe-$C000
                ld      a,(BkPage)
                rrca
                rrca
                or      d
                ld      d,a
                ld      bc,1
                ld      a,(BkSeg)
                ld      (BkSegHome),a           ; probes leave the segment selected
                jp      BkRaw

; Z if the byte behind the window takes a write. Put back as it was.
BkProbeRam:
                call    BkProbeGet
                ld      (BkSave0),a
                cpl
                ld      (BkSave1),a
                call    BkProbePut
                call    BkProbeGet
                ld      (BkRead),a
                ld      a,(BkSave0)
                call    BkProbePut
                ld      a,(BkSave1)
                ld      hl,BkRead
                cp      (hl)
                ret

; Z if the RAM behind the window is its own: not page 3 and none of
; the pages found before it. A 16K or 32K MSX that does not decode
; every address line shows its RAM again in the other pages of its
; slot, and BkProbeRam passes a page that mirrors page 3 since the
; probe's offset is BkProbe's own; a bank there wrote into the work
; RAM. A witness, BkProbe or a found page at the same offset, is read
; before and after an inverted write through the window: an alias
; sees it change. The window's byte is put back as it was.
BkProbeOwn:
                ld      a,(BkSlot)
                ld      (BkSlotC),a
                ld      a,$FF
                ld      (BkWit),a               ; page 3 first
BkOwnLoop:
                call    BkProbeGet
                ld      (BkSave0),a
                call    BkWitness
                ld      (BkSave1),a
                ld      a,(BkSave0)
                cpl
                call    BkProbePut
                call    BkWitness
                ld      (BkRead),a
                ld      a,(BkSave0)
                call    BkProbePut
                ld      a,(BkSave1)
                ld      hl,BkRead
                cp      (hl)
                ret     nz                      ; the witness moved: an alias
                ld      hl,BkWit
                inc     (hl)
                ld      a,(BkFound)
                cp      (hl)
                jr      nz,BkOwnLoop
                ret                             ; Z: every witness held
; A = the witness's byte at the probe's offset; the window is the
; candidate's again afterwards.
BkWitness:
                ld      a,(BkWit)
                inc     a
                jr      nz,BkWitPage
                ld      a,(BkProbe)
                ret
BkWitPage:
                dec     a
                ld      c,a
                ld      b,0
                ld      hl,BkPageTab
                add     hl,bc
                ld      a,(hl)
                ld      (BkPage),a
                ld      hl,BkSlotTab
                add     hl,bc
                ld      a,(hl)
                ld      (BkSlot),a
                call    BkProbeGet
                push    af
                ld      a,(BkPg)
                ld      (BkPage),a
                ld      a,(BkSlotC)
                ld      (BkSlot),a
                pop     af
                ret

; Z if segments 0 and 1 are different memory behind the window: a
; marker in each, and the first one is still there.
BkProbeMapper:
                xor     a
                ld      (BkSeg),a
                call    BkProbeGet
                ld      (BkSave0),a
                ld      a,1
                ld      (BkSeg),a
                call    BkProbeGet
                ld      (BkSave1),a
                xor     a
                ld      (BkSeg),a
                ld      a,$5A
                call    BkProbePut
                ld      a,1
                ld      (BkSeg),a
                ld      a,$A5
                call    BkProbePut
                xor     a
                ld      (BkSeg),a
                call    BkProbeGet
                ld      (BkRead),a
                ld      a,1
                ld      (BkSeg),a
                ld      a,(BkSave1)
                call    BkProbePut
                xor     a
                ld      (BkSeg),a
                ld      a,(BkSave0)
                call    BkProbePut
                ld      a,$FF
                ld      (BkSeg),a
                ld      a,(BkRead)
                cp      $5A
                ret

; HL = the number of segments. Segment number k is written into
; segment k for all 256 values, so a register with fewer bits leaves
; 256 minus the size in segment 0; the originals go back afterwards.
BkMapperSize:
                xor     a
                ld      (BkIdx),a
BkMsWrite:
                ld      a,(BkIdx)
                ld      (BkSeg),a
                call    BkProbeGet
                ld      e,a
                call    BkMsSlot
                ld      (hl),e
                ld      a,(BkIdx)
                call    BkProbePut
                ld      hl,BkIdx
                inc     (hl)
                jr      nz,BkMsWrite
                xor     a
                ld      (BkSeg),a
                call    BkProbeGet
                neg                             ; 256 minus what segment 0 holds
                ld      (BkTmp),a               ; the size's low byte, 0 for 256
                xor     a
                ld      (BkIdx),a
BkMsBack:
                ld      a,(BkIdx)
                ld      (BkSeg),a
                call    BkMsSlot
                ld      a,(hl)
                call    BkProbePut
                ld      hl,BkIdx
                inc     (hl)
                ld      a,(BkTmp)
                cp      (hl)
                jr      nz,BkMsBack
                ld      l,a
                ld      h,0
                or      a
                ret     nz
                inc     h                       ; 256
                ret
; HL = the save slot of segment BkIdx.
BkMsSlot:
                ld      a,(BkIdx)
                ld      c,a
                ld      b,0
                ld      hl,CmdBuf
                add     hl,bc
                ret

; HL = segments; marks every segment with its number through page 0,
; reads which ones pages 1-3 show, puts the bytes back, fills BkSkip
; with the segments the system keeps and returns HL = the rest.
BkMapperUsed:
                push    hl
                ld      a,l
                ld      (BkTmp),a               ; the size's low byte, 0 for 256
                ld      a,$FF
                ld      (BkProbe),a
                xor     a
                ld      (BkIdx),a
BkMuMark:
                ld      a,(BkIdx)
                ld      (BkSeg),a
                call    BkProbeGet
                ld      e,a
                call    BkMsSlot
                ld      (hl),e
                ld      a,(BkIdx)
                call    BkProbePut
                ld      hl,BkIdx
                inc     (hl)
                ld      a,(BkTmp)
                cp      (hl)
                jr      nz,BkMuMark
                ld      a,$FF
                ld      (BkSeg),a
                ld      a,1
                ld      (BkPage),a
                call    BkProbeGet
                ld      (BkS1),a
                ld      a,2
                ld      (BkPage),a
                call    BkProbeGet
                ld      (BkS2),a
                ld      a,(BkProbe)
                ld      (BkS3),a
                xor     a
                ld      (BkPage),a
                ld      (BkIdx),a
BkMuBack:
                ld      a,(BkIdx)
                ld      (BkSeg),a
                call    BkMsSlot
                ld      a,(hl)
                call    BkProbePut
                ld      hl,BkIdx
                inc     (hl)
                ld      a,(BkTmp)
                cp      (hl)
                jr      nz,BkMuBack
                ; page 0's segment: 3 under the BIOS's layout, else 0
                xor     a
                ld      (BkS0),a
                ld      a,(BkS3)
                or      a
                jr      nz,BkMuGuessed
                ld      a,(BkS2)
                dec     a
                jr      nz,BkMuGuessed
                ld      a,(BkS1)
                cp      2
                jr      nz,BkMuGuessed
                ld      a,3
                ld      (BkS0),a
BkMuGuessed:
                ld      a,(BkS0)
                out     (BKPORT),a
                ld      a,(BkS2)
                ld      (BkSegHome),a           ; the window page's own segment
                ld      a,$FF
                ld      (BkSeg),a
                ld      (BkSkip),a
                pop     hl                      ; the size
                ; page 3 on this mapper: its segment and the others the
                ; system maps are kept out
                ld      a,(BkS3)
                inc     a
                ret     z                       ; $FF: the marker never reached page 3
                dec     a
                ld      e,a
                ld      a,h
                or      a
                jr      nz,BkMuOn               ; 256 segments: every number is one
                ld      a,e
                cp      l
                ret     nc
BkMuOn:
                push    hl
                ld      a,(BkS3)
                call    BkSkipAdd
                ld      a,(BkS2)
                call    BkSkipSeg
                ld      a,(BkS1)
                call    BkSkipSeg
                ld      a,(BkS0)
                call    BkSkipAdd
                pop     hl
                ld      de,BkSkip
                ld      b,4
BkMuCount:
                ld      a,(de)
                inc     a
                ret     z
                dec     hl
                inc     de
                djnz    BkMuCount
                ret

; A = what a page's window showed: a segment number, into BkSkip, when
; it is one the markers reached (below the size, kept at the stack top).
BkSkipSeg:
                ld      hl,2
                add     hl,sp
                inc     hl
                ld      c,(hl)                  ; the size's high byte
                dec     hl
                inc     c
                dec     c
                jr      nz,BkSkipAdd            ; 256: every number is one
                cp      (hl)
                ret     nc
; A = a segment into BkSkip, kept ascending without repeats.
BkSkipAdd:
                ld      hl,BkSkip
BkSaLoop:
                cp      (hl)
                ret     z
                jr      c,BkSaInsert
                inc     hl
                jr      BkSaLoop
BkSaInsert:
                ld      c,(hl)
                ld      (hl),a
                inc     hl
                ld      a,c
                inc     a
                jr      nz,BkSaInsert           ; bubble the rest down
                ld      (hl),$FF
                ret

; ------------------------------------------------------------
;  The directory. Entry n is sixteen bytes at n*16 of the directory
;  blocks and its summary byte is byte n of the summary blocks; the
;  cache holds the 64 byte chunk either was last read from, tagged
;  with bit 15 for a summary chunk.
; ------------------------------------------------------------
BKSUMTAG        equ     $8000

; DE = chunk index, tagged. Returns HL = BkDirBuf holding it.
BkChunkLoad:
                ld      hl,(BkCache)
                or      a
                sbc     hl,de
                ld      hl,BkDirBuf
                ret     z
                ld      (BkCache),de
                push    de
                call    BkChunkAddr
                ld      hl,BkDirBuf
                ld      bc,BKCHUNK
                call    BkLoad
                pop     de
                ld      hl,BkDirBuf
                ret

; The cached chunk back into the bank.
BkChunkStore:
                ld      de,(BkCache)
                call    BkChunkAddr
                ld      hl,BkDirBuf
                ld      bc,BKCHUNK
                jp      BkStore

; DE = chunk, tagged. Returns DE = its block, A = its offset in it.
BkChunkAddr:
                ld      a,e
                and     3
                rrca
                rrca
                srl     d
                rr      e
                srl     d
                rr      e
                bit     5,d                     ; the tag, two places down
                jr      z,BkChunkDir
                res     5,d                     ; a summary chunk: block from 0
                ret
BkChunkDir:
                push    hl
                push    af
                ld      hl,(BkSumBlocks)
                add     hl,de
                ex      de,hl
                pop     af
                pop     hl
                ret

; DE = entry index. Returns HL = the entry, in the cache; DE kept.
BkEntryAt:
                push    de
                srl     d
                rr      e
                srl     d
                rr      e
                call    BkChunkLoad
                pop     de
                ld      a,e
                and     3
                add     a,a
                add     a,a
                add     a,a
                add     a,a
                ld      c,a
                ld      b,0
                add     hl,bc
                ret

; DE = entry index. Returns HL = its summary byte, in the cache; DE kept.
BkSumAt:
                push    de
                ld      a,e
                and     63
                ld      b,6
BkSumChunk:
                srl     d
                rr      e
                djnz    BkSumChunk
                set     7,d
                push    af
                call    BkChunkLoad
                pop     af
                pop     de
                ld      c,a
                ld      b,0
                add     hl,bc
                ret

; A = RamName's hash: a rotate and an exclusive or per byte, never 0,
; which is a free entry.
BkHash:
                ld      hl,RamName
                ld      b,RAMNAMESZ
                xor     a
BkHashLoop:
                rlca
                xor     (hl)
                inc     hl
                djnz    BkHashLoop
                or      a
                ret     nz
                inc     a
                ret

; The used entry whose name is RamName: HL = the entry, DE = its
; index, carry when there is none. Only entries whose summary byte is
; the name's hash are read. Entries past BkTop were never used, so
; the walk stops there.
BkFind:
                call    BkHash
                ld      (BkTmp+1),a
                ld      de,0
BkFindLoop:
                ld      hl,(BkTop)
                or      a
                sbc     hl,de
                jr      z,BkFindNo
                call    BkSumAt
                ld      a,(BkTmp+1)
                cp      (hl)
                jr      nz,BkFindNext
                call    BkEntryAt
                push    de
                push    hl
                ld      de,RamName
                ld      b,RAMNAMESZ
BkFindCmp:
                ld      a,(de)
                cp      (hl)
                jr      nz,BkFindDiff
                inc     hl
                inc     de
                djnz    BkFindCmp
                pop     hl
                pop     de
                or      a
                ret
BkFindDiff:
                pop     hl
                pop     de
BkFindNext:
                inc     de
                jr      BkFindLoop
BkFindNo:
                ld      a,STERR_NOTFOUND
                scf
                ret

; The first unused entry: DE = its index; carry with STERR_FULL when
; every data block has a file. The walk, over the summary, starts at
; BkFreeHint, below which nothing is free, and leaves it past the entry
; it hands out; a delete lowers it. That keeps filling the bank linear.
BkFree:
                ld      de,(BkFreeHint)
BkFreeLoop:
                ld      hl,(BkTop)
                or      a
                sbc     hl,de
                jr      z,BkFreeNew
                call    BkSumAt
                ld      a,(hl)
                or      a
                jr      z,BkFreeGot
                inc     de
                jr      BkFreeLoop
BkFreeNew:
                ld      hl,(BkFiles)
                or      a
                sbc     hl,de
                jr      z,BkFreeFull
                ld      hl,(BkTop)
                inc     hl
                ld      (BkTop),hl
BkFreeGot:
                inc     de                      ; nothing below the next one is free
                ld      (BkFreeHint),de
                dec     de
                or      a
                ret
BkFreeFull:
                ld      a,STERR_FULL
                scf
                ret

; DE = entry index, A = its summary byte: written through.
BkSumSet:
                push    af
                call    BkSumAt
                pop     af
                ld      (hl),a
                jp      BkChunkStore

; Carry unless the bank exists and A is the open file's handle.
BkValid:
                cp      1
                jr      nz,BkBadHandle
                ld      a,(BkMode)
                or      a
                ret     nz
BkBadHandle:
                ld      a,STERR_HANDLE
                scf
                ret

; BC = the smaller of BC and HL.
BkMin:
                push    hl
                or      a
                sbc     hl,bc
                pop     hl
                ret     nc
                ld      b,h
                ld      c,l
                ret

; NZ if there is a bank. Only A: BankOpen's name is still in HL.
BkPresent:
                ld      a,(BkBlocks)
                or      a
                ret     nz
                ld      a,(BkBlocks+1)
                or      a
                ret

; ------------------------------------------------------------
;  The backend's six entry points, the RAM backend's contract.
; ------------------------------------------------------------
; HL = name, B = mode. Returns A = handle 1 (one file open at a
; time, as the disk), carry on error.
BankOpen:
                call    BkPresent
                jp      z,StNoDelete            ; STERR_NOSUP
                ld      a,b
                ld      (BkTmp),a
                call    RamNorm
                ret     c
                call    BkFind
                jr      nc,BankOpenHave
                ld      a,(BkTmp)
                cp      FA_OVERWRITE
                jr      z,BankOpenNew
                ld      a,STERR_NOTFOUND
                scf
                ret
BankOpenNew:
                call    BkFree
                ret     c
                push    de
                call    BkEntryAt
                ex      de,hl
                ld      hl,RamName
                ld      bc,RAMNAMESZ
                ldir
                xor     a
                ld      (de),a
                inc     de
                ld      (de),a                  ; size 0
                inc     de
                inc     a
                ld      (de),a                  ; used
                call    BkChunkStore
                pop     de
                push    de
                ld      a,(BkTmp+1)             ; the hash BkFind worked out
                call    BkSumSet
                pop     de
                jr      BankOpenReady
BankOpenHave:
                ld      a,(BkTmp)
                cp      FA_OVERWRITE
                jr      nz,BankOpenReady
                push    de
                ld      bc,RAMOFFSIZE
                add     hl,bc
                xor     a
                ld      (hl),a
                inc     hl
                ld      (hl),a                  ; truncated
                call    BkChunkStore
                pop     de
BankOpenReady:
                ld      (BkEntry),de
                ld      hl,0
                ld      (BkPos),hl
                ld      a,(BkTmp)
                ld      (BkMode),a
                ld      a,1
                or      a
                ret

; A = handle. Everything was written through.
BankClose:
                call    BkValid
                ret     c
                xor     a
                ld      (BkMode),a
                ret

; A = handle, HL = buffer, BC = count. Returns BC = bytes read.
BankRead:
                call    BkValid
                ret     c
                push    hl
                push    bc
                ld      de,(BkEntry)
                call    BkEntryAt
                ld      bc,RAMOFFSIZE
                add     hl,bc
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ld      hl,(BkPos)
                ex      de,hl
                or      a
                sbc     hl,de                   ; HL = bytes left in the file
                pop     bc
                call    BkMin
                pop     hl
                ld      a,b
                or      c
                jr      z,BankReadDone
                push    bc
                call    BkFileBlock             ; DE = the block, A = the offset
                call    BkLoad
                pop     bc
                ld      hl,(BkPos)
                add     hl,bc
                ld      (BkPos),hl
BankReadDone:
                or      a
                ret

; DE = the open file's data block, A = the position in it.
BkFileBlock:
                ld      de,(BkEntry)
                push    hl
                ld      hl,(BkDataBlock)
                add     hl,de
                ex      de,hl
                pop     hl
                ld      a,(BkPos)
                ret

; A = handle, HL = buffer, BC = count. Returns BC = bytes written: a
; full block stops accepting bytes, a short write, as the RAM backend.
BankWrite:
                call    BkValid
                ret     c
                push    hl
                push    bc
                ld      hl,256
                ld      de,(BkPos)
                or      a
                sbc     hl,de                   ; HL = room
                pop     bc
                call    BkMin
                pop     hl
                ld      a,b
                or      c
                jr      z,BankWriteDone
                push    bc
                call    BkFileBlock
                call    BkStore
                pop     bc
                ld      hl,(BkPos)
                add     hl,bc
                ld      (BkPos),hl
                ; the size is the high water mark
                push    bc
                ld      de,(BkEntry)
                call    BkEntryAt
                ld      bc,RAMOFFSIZE
                add     hl,bc
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ld      bc,(BkPos)
                push    hl
                ex      de,hl
                or      a
                sbc     hl,bc
                pop     hl
                jr      nc,BankWriteKeep
                ld      (hl),b
                dec     hl
                ld      (hl),c
                call    BkChunkStore
BankWriteKeep:
                pop     bc
BankWriteDone:
                or      a
                ret

; A = the nth used entry from nought, counted on the summary. Returns
; HL = its name, in the cache until the next bank operation, DE = its
; length; carry if none.
BankDirEnt:
                ld      c,a
                ld      de,0
BankDirLoop:
                ld      hl,(BkTop)
                or      a
                sbc     hl,de
                jr      z,BankDirNone
                push    bc
                call    BkSumAt
                pop     bc
                ld      a,(hl)
                or      a
                jr      z,BankDirNext
                ld      a,c
                or      a
                jr      z,BankDirFound
                dec     c
BankDirNext:
                inc     de
                jr      BankDirLoop
BankDirFound:
                call    BkEntryAt
                push    hl
                ld      de,RAMOFFSIZE
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                pop     hl
                or      a
                ret
BankDirNone:
                ld      a,STERR_NOTFOUND
                scf
                ret

; HL = a name. Carry if there is no such file.
BankDelete:
                call    BkPresent
                jp      z,StNoDelete
                call    RamNorm
                ret     c
                call    BkFind
                ret     c
                push    hl
                ld      hl,(BkFreeHint)
                or      a
                sbc     hl,de
                jr      c,BankDeleteHint        ; the hint is below this entry
                jr      z,BankDeleteHint
                ld      (BkFreeHint),de
BankDeleteHint:
                pop     hl
                push    de
                ld      de,RAMOFFUSED
                add     hl,de
                ld      (hl),0
                call    BkChunkStore
                pop     de
                xor     a                       ; and free in the summary
                call    BkSumSet
                or      a
                ret

; ------------------------------------------------------------
;  What the rest of the desktop asks
; ------------------------------------------------------------
; A = backend id, B = 1 for the next one, 2 for the previous: the
; next available id round the ring RAM, BANK, DISK. RAM is always
; there, so it ends.
StRingStep:
                ld      c,0
                cp      ST_BANK
                jr      nz,StRsNotBank
                inc     c
StRsNotBank:
                cp      ST_DISK
                jr      nz,StRsStep
                ld      c,2
StRsStep:
                ld      a,c
                add     a,b
                cp      3
                jr      c,StRsIn
                sub     3
StRsIn:
                ld      c,a
                ld      hl,StRing
                add     a,l
                ld      l,a
                jr      nc,StRsNoCy
                inc     h
StRsNoCy:
                ld      a,(hl)
                push    bc
                call    StAvail
                pop     bc
                jr      nz,StRsStep
                ld      hl,StRing
                ld      a,c
                add     a,l
                ld      l,a
                jr      nc,StRsNoCy2
                inc     h
StRsNoCy2:
                ld      a,(hl)
                ret
StRing:         defb    ST_RAM, ST_BANK, ST_DISK

; A = backend id. Z if it can be selected: RAM always, BANK with a
; bank, DISK with a BDOS.
StAvail:
                cp      ST_RAM
                ret     z
                cp      ST_DISK
                jp      z,DskPresent
                cp      ST_BANK
                ret     nz
                call    BkPresent
                jr      z,StAvNo
                xor     a
                ret
StAvNo:
                inc     a
                ret

; The bank's size as "48 KB" at CmdWork, rounded up: the hidden pages
; are a block short of their 48. Returns HL = the text.
BkKbText:
                ld      hl,(BkBlocks)
                inc     hl
                inc     hl
                inc     hl
                srl     h
                rr      l
                srl     h
                rr      l
                ld      ix,CmdWork
                ld      c,0
                ld      de,10000
                call    BkKbDigit
                ld      de,1000
                call    BkKbDigit
                ld      de,100
                call    BkKbDigit
                ld      de,10
                call    BkKbDigit
                ld      a,l
                add     a,'0'
                ld      (ix+0),a
                ld      (ix+1),' '
                ld      (ix+2),'K'
                ld      (ix+3),'B'
                ld      (ix+4),0
                ld      hl,CmdWork
                ret
BkKbDigit:
                ld      a,'0'-1
BkKbSub:
                inc     a
                or      a
                sbc     hl,de
                jr      nc,BkKbSub
                add     hl,de
                cp      '0'
                jr      nz,BkKbPut
                inc     c
                dec     c
                ret     z                       ; a leading zero
BkKbPut:
                ld      (ix+0),a
                inc     ix
                ld      c,1
                ret
