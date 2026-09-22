; ============================================================
;  Event queue
;  Ported from the ZX version unchanged in mechanism: a sixteen
;  slot ring of four byte events. Posting to a full queue drops
;  the event silently, which is the right failure here: a lost
;  pointer move is invisible on the next frame, a stalled loop is
;  not.
;
;  EvPoll runs at the end of a frame, after ReadInput, and turns
;  changes in the raw input state into events. Dispatch happens
;  in the same frame, so the model is settled before the next
;  frame paints it.
;
;  The handlers are the phase 1 skeleton's: they count what
;  arrived and echo keys on the status row, so the harness can
;  assert that input reached the ring. Windows and menus take
;  them over in the later phases.
; ============================================================
EVSLOTS         equ     16
EVMASK          equ     EVSLOTS-1

EV_PTRMOVE      equ     1               ; B = PtrX, C = PtrY
EV_BTNDOWN      equ     2               ; B = button mask
EV_BTNUP        equ     3
EV_KEY          equ     4               ; B = key code
EV_TYPES        equ     4

; A = type, B and C = parameters. Carry set if it was dropped.
EvPost:
                push    bc
                push    af
                ld      a,(EvHead)
                inc     a
                and     EVMASK
                ld      hl,EvTail
                cp      (hl)
                jr      z,EvpFull
                ld      e,a
                ld      a,(EvHead)
                ld      l,a
                ld      h,0
                add     hl,hl
                add     hl,hl
                ld      bc,EvQueue
                add     hl,bc
                pop     af
                ld      (hl),a
                inc     hl
                pop     bc
                ld      (hl),b
                inc     hl
                ld      (hl),c
                ld      a,e
                ld      (EvHead),a
                or      a
                ret
EvpFull:
                pop     af
                pop     bc
                ld      hl,EvDropped
                inc     (hl)
                scf
                ret

; Returns A = type, or zero when the queue is empty. B and C carry
; the parameters.
EvNext:
                ld      a,(EvTail)
                ld      hl,EvHead
                cp      (hl)
                jr      z,EvnEmpty
                ld      l,a
                ld      h,0
                add     hl,hl
                add     hl,hl
                ld      bc,EvQueue
                add     hl,bc
                ld      a,(EvTail)
                inc     a
                and     EVMASK
                ld      (EvTail),a
                ld      a,(hl)
                inc     hl
                ld      b,(hl)
                inc     hl
                ld      c,(hl)
                ret
EvnEmpty:
                xor     a
                ret

; Drain the queue. Handlers receive B and C and may post further
; events, which are picked up on the next turn of this loop.
EvDispatch:
                call    EvNext
                or      a
                ret     z
                cp      EV_TYPES+1
                jr      nc,EvDispatch   ; unknown type, discard
                dec     a
                add     a,a
                ld      l,a
                ld      h,0
                ld      de,EvHandlers
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ex      de,hl
                call    EvCallHL
                jr      EvDispatch
EvCallHL:
                jp      (hl)

EvHandlers:
                defw    HdlPtrMove
                defw    HdlBtnDown
                defw    HdlBtnUp
                defw    HdlKey

; The skeleton's handlers. Each bumps its counter in EvCounts so a
; run can be read back: counts are per type, in event order.
HdlPtrMove:
                ld      hl,EvCounts+0
                inc     (hl)
                ret
HdlBtnDown:
                ld      hl,EvCounts+1
                inc     (hl)
                ret
HdlBtnUp:
                ld      hl,EvCounts+2
                inc     (hl)
                ret
HdlKey:
                ld      hl,EvCounts+3
                inc     (hl)
                ld      a,b
                ld      (LastKey),a
                ; Echo on the status row, wrapping at the right edge.
                ld      a,(StatCol)
                ld      c,a
                inc     a
                and     SCRCOLS-1
                ld      (StatCol),a
                ld      a,b
                ld      b,STATROW
                jp      PutChar

; Turn raw input state into events. Runs after ReadInput, at the end
; of a frame. Only the left button is reported for now.
EvPoll:
                ld      a,(EvLastBtn)
                ld      c,a
                ld      a,(Buttons)
                ld      (EvLastBtn),a
                xor     c
                bit     1,a             ; did the left button change?
                jr      z,EvpNoBtn
                ld      a,(Buttons)
                ld      b,a
                bit     1,a             ; active low, so zero is pressed
                ld      a,EV_BTNDOWN
                jr      z,EvpBtnGo
                ld      a,EV_BTNUP
EvpBtnGo:
                ld      c,0
                call    EvPost
EvpNoBtn:
                ld      a,(PtrX)
                ld      b,a
                ld      a,(EvLastX)
                cp      b
                jr      nz,EvpMoved
                ld      a,(PtrY)
                ld      b,a
                ld      a,(EvLastY)
                cp      b
                jp      z,KbdPoll
EvpMoved:
                ld      a,(PtrX)
                ld      (EvLastX),a
                ld      b,a
                ld      a,(PtrY)
                ld      (EvLastY),a
                ld      c,a
                ld      a,EV_PTRMOVE
                call    EvPost
                jp      KbdPoll

EvInit:
                xor     a
                ld      (EvHead),a
                ld      (EvTail),a
                ld      (EvDropped),a
                ld      (StatCol),a
                ld      (LastKey),a
                ld      hl,EvCounts
                ld      (hl),a
                inc     hl
                ld      (hl),a
                inc     hl
                ld      (hl),a
                inc     hl
                ld      (hl),a
                ld      a,$FF
                ld      (EvLastBtn),a
                ld      a,(PtrX)
                ld      (EvLastX),a
                ld      a,(PtrY)
                ld      (EvLastY),a
                ret
