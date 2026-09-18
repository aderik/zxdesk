IFDEF MOUSETEST
; ============================================================
;  Kempston mouse diagnostic
;  Built with MOUSETEST=1. It exists because "the mouse does not
;  move" has at least three causes that look identical from the
;  outside, and this tells them apart in one screen:
;
;    DETECTED 0    MouseInit did not believe there is a mouse, so
;                  ReadInput never reads the ports at all.
;
;    RAW never changes while you move the mouse. The ports are
;                  being read and Fuse is not feeding them. In a
;                  window Fuse only does that once you click on
;                  the Spectrum display to grab the pointer, and
;                  Escape gives it back.
;
;    RAW changes and MAX is large. The counters are arriving and
;                  something above is throwing them away. That is
;                  what ClampDelta used to do at 25.
;
;    RAW moves freely and freezes only while a button is held.
;                  Fuse for macOS, 1.9.2, stops delivering mouse
;                  movement with a button down, so the counters
;                  stand still exactly when a drag needs them.
;                  Nothing here is at fault and nothing here can
;                  fix it: this screen reads the ports once each
;                  with no gate, and they still do not move. Use
;                  SPACE as the button, which is what RiBtn is
;                  for. The likely cause is on the Cocoa side,
;                  where AppKit sends mouseDragged rather than
;                  mouseMoved once a button is down; a real
;                  Kempston mouse does not care what the buttons
;                  are doing and drags normally.
;
;  The ports are the Kempston ones: X at $FBDF, Y at $FFDF and
;  the buttons at $FADF, all active low on the buttons.
; ============================================================
MouseTestMain:
                di
                ld      sp,STACKTOP
                call    DetectMachine
                call    SetupIM2
                call    BuildScrTab
                call    MouseInit
                ei

                ld      b,0
MtClear:
                push    bc
                ld      a,b
                call    ClearRow
                pop     bc
                inc     b
                jr      nz,MtClear

                ld      hl,MtT1
                ld      b,0
                ld      c,0
                call    PrintStr
                ld      hl,MtT2
                ld      b,2
                ld      c,0
                call    PrintStr
                ld      hl,MtT3
                ld      b,3
                ld      c,0
                call    PrintStr

                ; the starting counters, so the first frame's delta is real
                ld      bc,$FBDF
                in      a,(c)
                ld      (MtPrevX),a
                ld      bc,$FFDF
                in      a,(c)
                ld      (MtPrevY),a

MtLoop:
                halt

                ld      bc,$FBDF
                in      a,(c)
                ld      (MtRawX),a
                ld      bc,$FFDF
                in      a,(c)
                ld      (MtRawY),a
                ld      bc,$FADF
                in      a,(c)
                ld      (MtRawB),a

                ; deltas, as magnitudes, and the largest seen so far
                ld      a,(MtRawX)
                ld      hl,MtPrevX
                sub     (hl)
                call    WeAbs
                ld      (MtDeltaX),a
                ld      hl,MtMaxX
                cp      (hl)
                jr      c,MtNoMaxX
                ld      (hl),a
MtNoMaxX:
                ld      a,(MtRawX)
                ld      (MtPrevX),a

                ld      a,(MtRawY)
                ld      hl,MtPrevY
                sub     (hl)
                call    WeAbs
                ld      (MtDeltaY),a
                ld      hl,MtMaxY
                cp      (hl)
                jr      c,MtNoMaxY
                ld      (hl),a
MtNoMaxY:
                ld      a,(MtRawY)
                ld      (MtPrevY),a

                ; and what the desktop would have made of it
                ld      a,(MtDeltaX)
                call    ClampDelta
                ld      (MtUsedX),a

                ld      hl,MtT4                 ; DETECTED
                ld      b,5
                ld      c,0
                call    PrintStr
                ld      a,(MouseOn)
                ld      b,5
                ld      c,10
                call    PrintDec3

                ld      hl,MtT5                 ; RAW
                ld      b,7
                ld      c,0
                call    PrintStr
                ld      a,(MtRawX)
                ld      b,7
                ld      c,10
                call    PrintDec3
                ld      a,(MtRawY)
                ld      b,7
                ld      c,16
                call    PrintDec3
                ld      a,(MtRawB)
                ld      b,7
                ld      c,22
                call    PrintDec3

                ld      hl,MtT6                 ; DELTA
                ld      b,9
                ld      c,0
                call    PrintStr
                ld      a,(MtDeltaX)
                ld      b,9
                ld      c,10
                call    PrintDec3
                ld      a,(MtDeltaY)
                ld      b,9
                ld      c,16
                call    PrintDec3

                ld      hl,MtT7                 ; MAX SEEN
                ld      b,11
                ld      c,0
                call    PrintStr
                ld      a,(MtMaxX)
                ld      b,11
                ld      c,10
                call    PrintDec3
                ld      a,(MtMaxY)
                ld      b,11
                ld      c,16
                call    PrintDec3

                ld      hl,MtT8                 ; USED
                ld      b,13
                ld      c,0
                call    PrintStr
                ld      a,(MtUsedX)
                ld      b,13
                ld      c,10
                call    PrintDec3
                ld      a,MOUSEMAX
                ld      b,13
                ld      c,16
                call    PrintDec3

                jp      MtLoop          ; jp, not jr: out of range from here

MtT1:           defb    "KEMPSTON MOUSE TEST",0
MtT2:           defb    "CLICK THE SCREEN TO GRAB IT",0
MtT3:           defb    "ESCAPE GIVES IT BACK",0
MtT4:           defb    "DETECTED",0
MtT5:           defb    "RAW  XYB",0
MtT6:           defb    "DELTA X Y",0
MtT7:           defb    "MAX   X Y",0
MtT8:           defb    "USED, CAP",0

MtRawX:         defb    0
MtRawY:         defb    0
MtRawB:         defb    0
MtPrevX:        defb    0
MtPrevY:        defb    0
MtDeltaX:       defb    0
MtDeltaY:       defb    0
MtMaxX:         defb    0
MtMaxY:         defb    0
MtUsedX:        defb    0
ENDIF
