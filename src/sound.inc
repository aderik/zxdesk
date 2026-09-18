; ============================================================
;  Sound
;  The desktop has been silent since the first build. GEM clicked
;  when you picked a menu item and beeped when it put an alert
;  up, and an alert you can miss because you were looking
;  somewhere else is an alert that has not been delivered.
;
;  TWO MACHINES, TWO SOUNDS, AND THEY ARE NOT THE SAME KIND OF
;  THING. On a 48K the only sound is the beeper on bit 4 of port
;  $FE, and a tone is the processor sitting in a counted loop
;  toggling it with interrupts off. A sixty millisecond beep is
;  three frames in which nothing else happens, which is the same
;  bargain the tape and the printer already make.
;
;  On a 128K there is an AY-3-8912, and it has an envelope
;  generator. Set the tone, set the envelope to decay once and
;  stay down, and the chip plays the note by itself while the
;  processor walks away. A beep costs seven register writes and
;  no waiting at all. DetectMachine already knows which machine
;  this is, the same way the clock's correction does.
;
;  IT IS IN THE FAST REGION, and for a reason that is about
;  sound rather than about speed. The beeper's pitch *is* its
;  timing loop, and below $8000 the ULA steals cycles from that
;  loop depending on where the beam is. A tone whose pitch
;  wobbles with the raster is not a tone, it is a noise.
;
;  And it can be turned off. A machine that beeps at you and
;  cannot be told to stop is worse than one that never beeped.
; ============================================================
AY_SELECT       equ     $FFFD
AY_WRITE        equ     $BFFD
SND_CLICK       equ     0
SND_BEEP        equ     1

; Four bytes a sound: the AY's tone period and envelope period, then
; the beeper's half cycle count and the delay inside each one. The two
; machines are given their own numbers rather than one set scaled,
; because a square wave from a counted loop and an envelope from a
; sound chip do not sound alike and should not be tuned as if they do.
SndTable:
                defb    60, 3, 4, 40            ; click: short and high
                defb    111, 24, 120, 110       ; beep: about 1kHz, 60ms

; A = SND_CLICK or SND_BEEP.
SndPlay:
                ld      (SndWant),a
                ld      a,(SetSound)
                or      a
                ret     z                       ; silenced, and that is a
                                                ; setting rather than a build
                ld      a,(SndWant)
                add     a,a
                add     a,a
                ld      e,a
                ld      d,0
                ld      hl,SndTable
                add     hl,de
                ld      a,(Is128)
                or      a
                jr      nz,SndAY
                ; the 48K's beeper, from the third and fourth bytes
                inc     hl
                inc     hl
                ld      b,(hl)
                inc     hl
                ld      c,(hl)
                jp      SndBeeper
SndAY:
                ld      a,(hl)
                ld      (SndPitch),a
                inc     hl
                ld      a,(hl)
                ld      (SndEnv),a
                ; fall through

; ------------------------------------------------------------
;  SndAYPlay
;  Tone on channel A, volume taken from the envelope, and the
;  envelope shape written last because writing it is what starts
;  the note. Shape nought is a single decay that stays down, so
;  nothing has to come back later and turn it off.
; ------------------------------------------------------------
SndAYPlay:
                ld      a,(SndPitch)
                ld      e,a
                xor     a
                call    SndReg                  ; R0, tone A fine
                ld      e,0
                ld      a,1
                call    SndReg                  ; R1, and coarse is nought
                ld      e,$3E                   ; tone A on, the rest and all
                ld      a,7                     ; the noise off. Active low,
                call    SndReg                  ; which is the AY's way
                ld      e,$10                   ; volume from the envelope
                ld      a,8
                call    SndReg
                ld      a,(SndEnv)
                ld      e,a
                ld      a,11
                call    SndReg                  ; envelope period, fine
                ld      e,0
                ld      a,12
                call    SndReg
                ld      e,0
                ld      a,13                    ; and the shape, which is the
                jp      SndReg                  ; trigger

; A = register, E = value.
SndReg:
                ld      bc,AY_SELECT
                out     (c),a
                ld      bc,AY_WRITE
                out     (c),e
                ret

; ------------------------------------------------------------
;  SndBeeper
;  in: B = half cycles, C = the delay inside each one
;  The border goes out of the same port as the speaker, so it is
;  carried through every toggle rather than being changed by
;  accident. Seven is white, which is what InitScreen set.
; ------------------------------------------------------------
SndBeeper:
                di
                ld      d,7
SndbLoop:
                ld      a,d
                xor     $10                     ; the speaker bit
                ld      d,a
                out     (ULA),a
                ld      e,c
SndbWait:
                dec     e
                jr      nz,SndbWait
                djnz    SndbLoop
                ld      a,7                     ; speaker down, border white
                out     (ULA),a
                ei
                ret

SndWant:        defb    0
SndPitch:       defb    0
SndEnv:         defb    0
