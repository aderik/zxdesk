; ============================================================
;  The heap, ported from the ZX version as it stands.
;  Everything in this system has been static. Two windows exist
;  because two buffers were declared at fixed addresses, and a
;  third would have been a third declaration. That is what this
;  changes: how many windows there are becomes a question about
;  memory rather than about the source.
;
;  It is also a reclamation. A window buffer was 24 by 96 whatever
;  the window was, so the about window at 13 by 52 cost 2,304
;  bytes to hold 676. Sized to their windows, the two together
;  take 1,836 where they took 4,608.
;
;  Blocks are contiguous and carry a four byte header, so the
;  free list is the heap itself walked from the bottom and there
;  are no link pointers to keep straight:
;
;      +0,+1   payload size
;      +2      used
;      +3      owner
;      +4      the payload starts here
;
;  The owner byte is the part worth having. Without it, closing a
;  window means the window code remembering every allocation it
;  made; with it, HeapFreeOwner is one pass and F2's "the thing
;  that owned this is gone" is a single call. The idea is taken
;  from the yos allocator in retro-vault/xyz, which carries an
;  owner in its block header for exactly this reason.
;
;  First fit, split on allocate, coalesce on free. With a handful
;  of blocks that is the right shape: a best fit search costs
;  more than the fragmentation it saves, and a free list threaded
;  through pointers costs two bytes a block to avoid a walk of
;  perhaps six.
; ============================================================
HEAPHDR         equ     4
HEAPSPLIT       equ     HEAPHDR+16      ; not worth splitting off less
; HeapBase is set in msxdesk.asm; the end is HeapEnd, a variable,
; because a disk ROM lowers HIMEM by five kilobytes and the heap
; stops STACKRES under whatever HIMEM is. Nothing here ever reaches
; $10000, so the wrap the ZX version stopped a page short for cannot
; happen.

HEAPOWN_NONE    equ     0               ; owned by nothing in particular
HEAPOWN_WIN     equ     $10             ; plus the window index

; ------------------------------------------------------------
;  HeapInit
;  One free block covering everything.
; ------------------------------------------------------------
HeapInit:
                ld      hl,(HeapEnd)
                ld      de,HeapBase+HEAPHDR
                or      a
                sbc     hl,de
                ex      de,hl                   ; DE = the one block's payload
                ld      hl,HeapBase
                ld      (hl),e
                inc     hl
                ld      (hl),d
                inc     hl
                ld      (hl),0                  ; free
                inc     hl
                ld      (hl),HEAPOWN_NONE
                ret

; ------------------------------------------------------------
;  HeapAlloc
;  in:  BC = payload bytes, A = owner
;  out: HL = the payload, carry set if there was no room
; ------------------------------------------------------------
HeapAlloc:
                ld      (HpOwner),a
                ld      (HpWant),bc
                ld      hl,HeapBase
HpaLoop:
                ld      de,(HeapEnd)              ; walked off the end?
                push    hl
                or      a
                sbc     hl,de
                pop     hl
                jr      nc,HpaNone
                ld      e,(hl)                  ; this block's size
                inc     hl
                ld      d,(hl)
                inc     hl
                ld      a,(hl)                  ; and whether it is taken
                dec     hl
                dec     hl
                or      a
                jr      nz,HpaNext
                ; free. Is it big enough?
                push    hl
                ex      de,hl                   ; HL = size
                ld      de,(HpWant)
                or      a
                sbc     hl,de
                pop     hl
                jr      c,HpaNext               ; too small
                jr      HpaFits
HpaNext:
                ; The size is read again here for the same reason HpaFits
                ; reads it again: the too small test above needs DE for
                ; HpWant and leaves it holding that, so the block was
                ; stepped over by the size that was asked for rather than
                ; the size that is there. That only goes wrong when a free
                ; block sits in front of a request bigger than it, which
                ; needs a block freed in the middle of the heap, which took
                ; a window resize to arrange. Until then every free block
                ; was the last one.
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                dec     hl
                push    hl
                ld      hl,HEAPHDR
                add     hl,de
                ex      de,hl                   ; DE = header plus payload
                pop     hl
                add     hl,de
                jr      HpaLoop
HpaNone:
                scf
                ret

HpaFits:
                ; The size is read again rather than carried here. The test
                ; above needs DE for HpWant and leaves it holding that, so
                ; arriving with DE assumed to be the block's size split
                ; against the wrong number and never split at all.
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                dec     hl                      ; DE = size, HL = the block
                push    hl
                ex      de,hl
                ld      de,(HpWant)
                or      a
                sbc     hl,de                   ; HL = bytes over
                ld      de,HEAPSPLIT
                or      a
                sbc     hl,de
                pop     hl
                jr      c,HpaWhole
                ; the tail becomes a free block
                push    hl
                ld      bc,(HpWant)
                ld      e,(hl)
                inc     hl
                ld      d,(hl)                  ; DE = the old size
                push    de
                ld      (hl),b                  ; this block is now HpWant
                dec     hl
                ld      (hl),c
                ld      bc,HEAPHDR
                add     hl,bc
                ld      bc,(HpWant)
                add     hl,bc                   ; HL = the tail's header
                pop     de                      ; DE = the old size
                ld      bc,(HpWant)
                ex      de,hl
                or      a
                sbc     hl,bc
                ld      bc,HEAPHDR
                or      a
                sbc     hl,bc                   ; HL = the tail's payload size
                ex      de,hl                   ; DE = that, HL = the tail
                ld      (hl),e
                inc     hl
                ld      (hl),d
                inc     hl
                ld      (hl),0                  ; free
                inc     hl
                ld      (hl),HEAPOWN_NONE
                pop     hl
HpaWhole:
                push    hl
                inc     hl
                inc     hl
                ld      (hl),1                  ; used
                inc     hl
                ld      a,(HpOwner)
                ld      (hl),a
                pop     hl
                ld      bc,HEAPHDR
                add     hl,bc
                or      a
                ret

; ------------------------------------------------------------
;  HeapFree
;  in: HL = a payload pointer from HeapAlloc
;  A null pointer is ignored, because a window that never got a
;  buffer should still be closable.
; ------------------------------------------------------------
HeapFree:
                ld      a,h
                or      l
                ret     z
                ; One dec per field, and the first version had two before
                ; the first write, so it cleared the used flag and then
                ; put a nought in the high byte of the size. That was
                ; invisible for a long time: HeapCoalesce found a short
                ; free block, looked past it into a payload that happened
                ; to be zeros, read those as an empty free block and
                ; absorbed it, and went on absorbing four bytes at a time
                ; until it arrived back at the true size. The heap healed
                ; itself and the numbers agreed. It only showed when a
                ; freed block held something other than zeros, which is
                ; the first block big enough to have held window pixels.
                dec     hl                      ; the owner
                ld      (hl),HEAPOWN_NONE
                dec     hl                      ; and the used flag
                ld      (hl),0
                jr      HeapCoalesce

; ------------------------------------------------------------
;  HeapFreeOwner
;  in: A = owner
;  Frees everything that owner allocated, in one pass. This is
;  what the owner byte is for.
; ------------------------------------------------------------
HeapFreeOwner:
                ld      (HpOwner),a
                ld      hl,HeapBase
HpfLoop:
                ld      de,(HeapEnd)
                push    hl
                or      a
                sbc     hl,de
                pop     hl
                jr      nc,HeapCoalesce
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                inc     hl
                ld      a,(hl)                  ; used?
                or      a
                jr      z,HpfNext
                inc     hl
                ld      a,(hl)                  ; owner
                dec     hl
                ld      b,a
                ld      a,(HpOwner)
                cp      b
                jr      nz,HpfNext
                ld      (hl),0                  ; free it
                inc     hl
                ld      (hl),HEAPOWN_NONE
                dec     hl
HpfNext:
                dec     hl
                dec     hl
                push    hl
                ld      hl,HEAPHDR
                add     hl,de
                ex      de,hl
                pop     hl
                add     hl,de
                jr      HpfLoop

; ------------------------------------------------------------
;  HeapCoalesce
;  Merges every run of adjacent free blocks into one. Done after
;  a free rather than before an allocate, so the common path is
;  the cheap one.
; ------------------------------------------------------------
HeapCoalesce:
                ld      hl,HeapBase
HpcLoop:
                ld      de,(HeapEnd)
                push    hl
                or      a
                sbc     hl,de
                pop     hl
                ret     nc
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                inc     hl
                ld      a,(hl)
                dec     hl
                dec     hl
                or      a
                jr      nz,HpcNext              ; in use, so nothing to merge
                ; HL = a free block, DE = its size. Look at the next one.
                push    hl
                push    de
                ld      bc,HEAPHDR
                add     hl,bc
                add     hl,de                   ; HL = the next header
                ld      de,(HeapEnd)
                push    hl
                or      a
                sbc     hl,de
                pop     hl
                jr      nc,HpcNoMerge           ; there is no next one
                ld      c,(hl)
                inc     hl
                ld      b,(hl)                  ; BC = its size
                inc     hl
                ld      a,(hl)
                or      a
                jr      nz,HpcNoMerge           ; in use
                ; absorb it, header and all
                pop     de
                pop     hl
                push    hl
                ld      a,HEAPHDR
                add     a,c
                ld      c,a
                jr      nc,HpcNoCy
                inc     b
HpcNoCy:
                ex      de,hl
                add     hl,bc
                ex      de,hl                   ; DE = the merged size
                pop     hl
                ld      (hl),e
                inc     hl
                ld      (hl),d
                dec     hl
                jr      HpcLoop                 ; and try to absorb another
HpcNoMerge:
                pop     de
                pop     hl
HpcNext:
                push    hl
                ld      hl,HEAPHDR
                add     hl,de
                ex      de,hl
                pop     hl
                add     hl,de
                jr      HpcLoop

; ------------------------------------------------------------
;  HeapStat
;  out: HL = total free bytes, DE = the largest single free block
;  For the harness, and for anything that wants to ask before it
;  tries.
; ------------------------------------------------------------
HeapStat:
                ld      hl,0
                ld      (HpTotal),hl
                ld      (HpBiggest),hl
                ld      hl,HeapBase
HpsLoop:
                ld      de,(HeapEnd)
                push    hl
                or      a
                sbc     hl,de
                pop     hl
                jr      nc,HpsDone
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                inc     hl
                ld      a,(hl)
                dec     hl
                dec     hl
                or      a
                jr      nz,HpsNext
                push    hl
                ld      hl,(HpTotal)
                add     hl,de
                ld      (HpTotal),hl
                ld      hl,(HpBiggest)
                or      a
                sbc     hl,de
                jr      nc,HpsNotBig
                ld      (HpBiggest),de
HpsNotBig:
                pop     hl
HpsNext:
                push    hl
                ld      hl,HEAPHDR
                add     hl,de
                ex      de,hl
                pop     hl
                add     hl,de
                jr      HpsLoop
HpsDone:
                ld      hl,(HpTotal)
                ld      de,(HpBiggest)
                ret

; HpWant, HpOwner, HpTotal and HpBiggest live in the var block.
