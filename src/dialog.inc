; Modal cell-grid dialogues. Save-under comes from ShadowNT; pending
; window redraws wait until the answer restores it. No nested event loop.
; Commander cannot transfer while a dialogue owns input. Share its
; 256-byte scratch buffer; neither note saving nor painting uses it.
DgSave          equ     CmdBuf
DGX             equ     6
DGY             equ     8
DGW             equ     20
DGH             equ     7

; HL, DE = message lines; BC = callback (A: cancel=0, discard=1, save=2).
DlgSave3:
                ld      (DgAfter),bc
                ld      a,3
                jr      DgOpen
; Two answers: cancel (0), delete (1), initially cancel.
DlgDelete2:
                ld      (DgAfter),bc
                ld      a,2
                jr      DgOpen
; HL, DE = message lines. An alert has one OK answer and no callback.
DlgAlert:
                ld      bc,0
                ld      (DgAfter),bc
                ld      a,1
DgOpen:
                ld      (DgMode),a
                push    af
                ld      a,SND_BEEP
                call    SndPlay
                pop     af
                cp      4
                jr      c,DgCountReady
                ld      a,2
DgCountReady:
                ld      (DgCount),a
                ld      (DgLine1),hl
                ld      (DgLine2),de
                call    WinRedraw               ; settle any focus change first
                xor     a
                ld      (DgFocus),a
                ld      de,DgSave
                ld      b,DGY
DgSaveRow:
                push    bc
                push    de
                ld      c,DGX
                call    CellAddr
                pop     de
                ld      bc,DGW
                ldir
                pop     bc
                inc     b
                ld      a,b
                cp      DGY+DGH
                jr      nz,DgSaveRow
                ld      a,1
                ld      (DgOpenFlag),a
                jp      DgPaint

DgPaint:
                ld      b,DGY
DgPaintRow:
                push    bc
                ld      c,DGX
                call    CellAddr
                pop     bc
                push    bc
                ld      a,b
                sub     DGY+3
                ld      c,a
                ld      a,(DgFocus)
                cp      c
                ld      a,' '
                jr      nz,DgFill
                or      INVBANK
DgFill:
                ld      b,DGW
DgFillCell:
                ld      (hl),a
                inc     hl
                djnz    DgFillCell
                pop     bc
                inc     b
                ld      a,b
                cp      DGY+DGH
                jr      nz,DgPaintRow
                xor     a
                ld      (PrintInv),a
                ld      hl,(DgLine1)
                ld      b,DGY+1
                call    DgText
                ld      hl,(DgLine2)
                ld      b,DGY+2
                call    DgText
                ld      a,(DgMode)
                cp      4
                jp      z,DgInputPaint
                ld      hl,DgOverwrite
                cp      5
                jr      z,DgLabels
                ld      hl,DgAnswers
                ld      a,(DgCount)
                cp      2
                jr      nz,DgNotDelete
                ld      hl,DgDelete
                jr      DgLabels
DgNotDelete:
                cp      1
                jr      nz,DgLabels
                ld      hl,DgOk
DgLabels:
                ld      b,0
DgLabel:
                push    bc
                push    hl
                ld      a,(DgFocus)
                cp      b
                ld      a,0
                jr      nz,DgNormal
                ld      a,INVBANK
DgNormal:
                ld      (PrintInv),a
                ld      a,b
                add     a,DGY+3
                ld      b,a
                call    DgText
                pop     hl
DgSkip:
                ld      a,(hl)
                inc     hl
                or      a
                jr      nz,DgSkip
                pop     bc
                inc     b
                ld      a,(DgCount)
                cp      b
                jr      nz,DgLabel
                xor     a
                ld      (PrintInv),a
                ret
DgText:
                ld      a,h
                or      l
                ret     z
                ld      c,DGX+1
                jp      PrintStr

DgPress:
                ld      a,(DgMode)
                cp      4
                ret     z
                push    bc
                call    HitTest
                pop     bc
                ld      (LastHit),a
                cp      CTL_DIALOG
                ret     nz
                ld      a,c
                sub     DGY+3
                ret     c
                ld      b,a
                ld      a,(DgCount)
                cp      b
                ret     z
                ret     c
                ld      a,b
                jp      DgAnswer

; SHIFT+arrows follows the desktop convention; TAB cycles, ESC stays.
DgKey:
                ld      b,a
                ld      a,(DgMode)
                cp      4
                ld      a,b
                jp      z,DgInputKey
                cp      KEY_ESC
                jr      z,DgCancel
                cp      KEY_ENTER
                jr      z,DgChoose
                cp      KEY_UP
                jr      z,DgPrev
                cp      KEY_LEFT
                jr      z,DgPrev
                cp      KEY_DOWN
                jr      z,DgNext
                cp      KEY_RIGHT
                jr      z,DgNext
                cp      9
                ret     nz
DgNext:
                ld      a,(DgFocus)
                inc     a
                ld      hl,DgCount
                cp      (hl)
                jr      c,DgSet
                xor     a
                jr      DgSet
DgPrev:
                ld      a,(DgFocus)
                sub     1
                jr      nc,DgSet
                ld      a,(DgCount)
                dec     a
DgSet:
                ld      (DgFocus),a
                jp      DgPaint
DgChoose:
                ld      a,(DgFocus)
                jr      DgAnswer
DgCancel:
                xor     a
DgAnswer:
                push    af
                ld      a,SND_CLICK
                call    SndPlay
                pop     af
                push    af
                ld      de,DgSave
                ld      b,DGY
DgRestoreRow:
                push    bc
                push    de
                ld      c,DGX
                call    CellAddr
                pop     de
                ex      de,hl
                ld      bc,DGW
                ldir
                ex      de,hl
                pop     bc
                inc     b
                ld      a,b
                cp      DGY+DGH
                jr      nz,DgRestoreRow
                xor     a
                ld      (DgOpenFlag),a
                ld      hl,(DgAfter)
                ld      a,h
                or      l
                jr      z,DgNoCallback
                pop     af
                jp      (hl)
DgNoCallback:
                pop     af
                ret
DgAnswers:      defb    "CANCEL",0,"DISCARD",0,"SAVE",0
DgDelete:       defb    "CANCEL",0,"DELETE",0
DgOk:           defb    "OK",0

; Reusable modal text field. HL = caption, BC = answer callback;
; DE = validator (HL = candidate string, carry = reject). Buffer and
; cursor belong to the dialog, outside its 140-byte save-under.
DgTextBuf       equ     CmdBuf+160
DgTextOld       equ     CmdBuf+173
DlgInput:
                ld      (DgValidate),de
                ld      (DgAfter),bc
                xor     a
                ld      (DgTextBuf),a
                ld      (DgCursor),a
                ld      (DgLength),a
                ld      de,0
                ld      a,4
                jp      DgOpen
DlgOverwrite:
                ld      (DgAfter),bc
                ld      a,5
                jp      DgOpen
DgOverwrite:    defb    "CANCEL",0,"OVERWRITE",0
DgInputHelp:    defb    "ENTER SAVE ESC BACK",0
DgInputPaint:
                ld      hl,DgTextBuf
                ld      b,DGY+2
                call    DgText
                ld      a,(DgCursor)
                add     a,DGX+1
                ld      c,a
                ld      b,DGY+2
                call    CellAddr
                ld      a,(hl)
                xor     INVBANK
                ld      (hl),a
                ld      hl,DgInputHelp
                ld      b,DGY+5
                jp      DgText
DgInputKey:
                cp      KEY_ESC
                jp      z,DgCancel
                cp      KEY_ENTER
                jp      z,DiSubmit
                cp      KEY_LEFT
                jp      z,DiLeft
                cp      KEY_RIGHT
                jp      z,DiRight
                cp      KEY_DELETE
                jp      z,DiBack
                cp      '!'
                ret     c
                cp      127
                ret     nc
                ld      (DgChar),a
                ld      a,(DgLength)
                cp      12
                ret     nc
                ld      hl,DgTextBuf
                ld      de,DgTextOld
                ld      bc,13
                ldir
                ld      a,(DgLength)
                ld      e,a
                ld      d,0
                ld      hl,DgTextBuf
                add     hl,de
                ld      a,(DgCursor)
                ld      b,a
                ld      a,(DgLength)
                sub     b
                inc     a
                ld      c,a
                ld      b,0
                ld      d,h
                ld      e,l
                inc     de
                lddr
                inc     hl
                ld      a,(DgChar)
                ld      (hl),a
                call    DiValidate
                jr      nc,DiAccepted
                ld      hl,DgTextOld
                ld      de,DgTextBuf
                ld      bc,13
                ldir
                ret
DiAccepted:
                ld      hl,DgLength
                inc     (hl)
                ld      hl,DgCursor
                inc     (hl)
                jp      DgPaint
DiValidate:
                ld      hl,(DgValidate)
                push    hl
                ld      hl,DgTextBuf
                ret
DiSubmit:
                call    DiValidate
                ret     c
                ld      a,(DgLength)
                or      a
                ret     z
                ld      a,1
                jp      DgAnswer
DiLeft:
                ld      hl,DgCursor
                ld      a,(hl)
                or      a
                ret     z
                dec     (hl)
                jp      DgPaint
DiRight:
                ld      a,(DgLength)
                ld      hl,DgCursor
                cp      (hl)
                ret     z
                inc     (hl)
                jp      DgPaint
DiBack:
                ld      a,(DgCursor)
                or      a
                ret     z
                dec     a
                ld      (DgCursor),a
                ld      e,a
                ld      d,0
                ld      hl,DgTextBuf
                add     hl,de
                ld      d,h
                ld      e,l
                inc     hl
DiShift:
                ld      a,(hl)
                ld      (de),a
                inc     hl
                inc     de
                or      a
                jr      nz,DiShift
                ld      hl,DgLength
                dec     (hl)
                jp      DgPaint
