; ============================================================
;  D2: controls
;  A panel is a stack of rows, one text line each, and every
;  control is a row. That is the whole model, and it comes from
;  the two things that were already built rather than from a
;  widget kit: the settings panel was six rows hit tested by
;  dividing a pixel row by eight, and it was right.
;
;  A row's index is (pixel row - PnlRow0) >> 3, three shifts and
;  no search, which is why rows and not rectangles.
;
;  The design plan and its self critique are in
;  docs/control-design.md. The short version of what is not here:
;  no scrollbar, because a panel is at most seven rows and the
;  screen shows all of them; no list box, because a panel is a
;  list; no checkbox, because a checkbox is a cycle whose limit
;  is two, and drawing a tick in an eight by eight cell in one
;  bit gives you a full stop.
;
;  Note the prefix. hittest.inc already owns Ctl for the screen
;  region table, which is a different thing: that one is z order
;  and says what was clicked, this one is inside one panel and
;  says which row.
; ============================================================
PT_LABEL        equ     0               ; inert text
PT_ACTION       equ     1               ; param = routine to call
PT_CYCLE        equ     2               ; param = the byte, limit = wrap
PT_FIELD        equ     3               ; param = buffer, limit = capacity
PTSIZE          equ     8

; A row is eight bytes:
;   +0  type
;   +1  label, a null terminated string
;   +3  param, meaning by type
;   +5  limit, the wrap for PT_CYCLE and the capacity for PT_FIELD
;   +6  names, PT_CYCLE only
;
; Eight rather than the six it needs, so a row address is three
; shifts. The notepad made the same trade for the same reason, and
; two spare bytes a row over a panel of six is twelve bytes.
;
; PT_CYCLE's names are a table of word pointers, one per state, so
; the screen shows OFF and ON rather than 0 and 1. Digits on screen
; are the internal representation leaking out, and INVERT Y 1 was
; long enough to collide with its own value.

PNLMARGIN       equ     $30             ; the focus bar, bits 5 and 4
PNLROWH         equ     8

; ------------------------------------------------------------
;  The panel record
;  One panel is described at a time, and these fields are laid
;  out contiguously so that D3 can push and pop the lot with an
;  LDIR. B3 made the pixels underneath a panel a stack; without
;  this the pixels would come back and the panel that owned them
;  would not, because a confirm opening over a panel overwrites
;  every one of these.
;
;  PnlIdx and PnlLen are deliberately outside it. They are
;  scratch within a single routine and there is nothing about
;  them worth carrying across a nested panel.
; ------------------------------------------------------------
PnlRec:
PnlX:           defb    0               ; byte column of the left edge
PnlY:           defb    0               ; pixel row of the top
PnlW:           defb    0               ; byte columns
PnlH:           defb    0               ; pixel rows
PnlRow0:        defb    0               ; pixel row of the first control row
PnlRows:        defb    0
PnlTab:         defw    0
PnlTitle:       defw    0
PnlAfter:       defw    0               ; called after any activation, A = row
PnlFocus:       defb    0               ; the row the keyboard is pointing at
PnlFCur:        defb    0               ; the cursor column inside a field
PNLRECSZ        equ     $-PnlRec

PNLDEPTH        equ     3               ; a panel, a confirm over it, and one
PnlRecStk:      defs    PNLDEPTH*PNLRECSZ
PnlRecDep:      defb    0

PnlIdx:         defb    0               ; scratch, the row being worked on
PnlLen:         defb    0

; ------------------------------------------------------------
;  PnlRowAddr
;  in:  A = row index
;  out: HL = the row's seven bytes
; ------------------------------------------------------------
PnlRowAddr:
                ld      l,a
                ld      h,0
                add     hl,hl
                add     hl,hl
                add     hl,hl                   ; x8, the row size
                ld      de,(PnlTab)
                add     hl,de
                ret

; ------------------------------------------------------------
;  PnlRowY
;  in:  A = row index
;  out: A = the row's first pixel row
; ------------------------------------------------------------
PnlRowY:
                add     a,a
                add     a,a
                add     a,a                     ; x8, the row height
                ld      hl,PnlRow0
                add     a,(hl)
                ret

; ------------------------------------------------------------
;  PnlStrLen
;  in:  HL = a null terminated string
;  out: A = its length, HL unchanged
;  Values are right aligned, so their width has to be known
;  before they are printed. Nothing in the system had a strlen.
; ------------------------------------------------------------
PnlStrLen:
                push    hl
                ld      c,0
PslLoop:
                ld      a,(hl)
                or      a
                jr      z,PslDone
                inc     c
                inc     hl
                jr      PslLoop
PslDone:
                ld      a,c
                pop     hl
                ret

; ------------------------------------------------------------
;  PnlValue
;  in:  A = row index
;  out: HL = the string to show on the right of the row, or 0
;  PT_CYCLE reads its state name out of the row's name table.
;  PT_FIELD shows its buffer. Nothing else has a value.
; ------------------------------------------------------------
PnlValue:
                call    PnlRowAddr
                ld      a,(hl)
                cp      PT_CYCLE
                jr      z,PnvCycle
                cp      PT_FIELD
                jr      z,PnvField
                ld      hl,0
                ret
PnvField:
                inc     hl
                inc     hl
                inc     hl
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ex      de,hl                   ; HL = the buffer itself
                ret
PnvCycle:
                push    hl
                inc     hl
                inc     hl
                inc     hl
                ld      e,(hl)
                inc     hl
                ld      d,(hl)                  ; DE = where the value lives
                ld      a,(de)
                pop     hl
                push    af
                ld      de,6
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)                  ; DE = the name table
                pop     af
                add     a,a                     ; two bytes an entry
                ld      l,a
                ld      h,0
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ex      de,hl
                ret

; ------------------------------------------------------------
;  PnlRowPaint
;  in: A = row index
;  Clears the row, draws the focus bar if it has the focus, the
;  label at the left and the value right aligned.
; ------------------------------------------------------------
PnlRowPaint:
                ld      (PnlIdx),a
                ; clear the row's ten usable columns
                call    PnlRowY
                ld      (FrY),a
                ld      a,(PnlX)
                inc     a
                ld      (FrX),a
                ld      a,(PnlW)
                sub     2
                ld      (FrW),a
                ld      a,PNLROWH
                ld      (FrH),a
                ld      hl,0
                ld      (FrPat),hl
                call    DevFillRect

                ; the focus bar, in the margin the border already reserved
                ld      a,(PnlIdx)
                ld      hl,PnlFocus
                cp      (hl)
                jr      nz,PrpNoFocus
                call    PnlRowY
                ld      (PxRow),a
                ld      b,PNLROWH
PrpBar:
                push    bc
                ld      a,(PnlX)
                ld      c,a
                ld      a,(PxRow)
                call    AddrAt
                ld      a,(hl)
                or      PNLMARGIN
                ld      (hl),a
                ld      a,(PxRow)
                inc     a
                ld      (PxRow),a
                pop     bc
                djnz    PrpBar
                jr      PrpLabel
PrpNoFocus:
                ; erase any bar left over, without touching the border bit
                call    PnlRowY
                ld      (PxRow),a
                ld      b,PNLROWH
PrpNoBar:
                push    bc
                ld      a,(PnlX)
                ld      c,a
                ld      a,(PxRow)
                call    AddrAt
                ld      a,(hl)
                and     255-PNLMARGIN
                ld      (hl),a
                ld      a,(PxRow)
                inc     a
                ld      (PxRow),a
                pop     bc
                djnz    PrpNoBar
PrpLabel:
                ld      a,(PnlIdx)
                call    PnlRowY
                ld      (PxRow),a
                ld      a,(PnlIdx)
                call    PnlRowAddr
                inc     hl
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ld      a,d
                or      e
                jr      z,PrpValue
                ex      de,hl
                ld      a,(PnlX)
                inc     a
                ld      c,a
                call    PrintStrPx2
PrpValue:
                ld      a,(PnlIdx)
                call    PnlRowAddr
                ld      a,(hl)
                cp      PT_FIELD
                jr      z,PrpField
                ld      a,(PnlIdx)
                call    PnlValue
                ld      a,h
                or      l
                ret     z
                push    hl
                call    PnlStrLen
                ld      (PnlLen),a
                ld      a,(PnlX)
                ld      hl,PnlW
                add     a,(hl)
                dec     a                       ; one past the last usable column,
                                                ; which is the right border less one
                ld      hl,PnlLen
                sub     (hl)                    ; right aligned
                ld      c,a
                ld      a,(PnlIdx)
                call    PnlRowY
                ld      (PxRow),a
                pop     hl
                jp      PrintStrPx2

; ------------------------------------------------------------
;  A field is the one value that is left aligned, because it is
;  being typed into and text that shuffles right as it grows is
;  unreadable. Its area is the last `limit` columns of the row,
;  and the cursor is an inverted cell sitting in it.
; ------------------------------------------------------------
PrpField:
                call    PnlFieldCol             ; C = the field's first column
                ld      a,(PnlIdx)
                call    PnlRowY
                ld      (PxRow),a
                ld      a,(PnlIdx)
                call    PnlValue                ; HL = the buffer
                push    bc
                call    PrintStrPx2
                pop     bc
                ; the cursor, but only on the row that has the focus
                ld      a,(PnlIdx)
                ld      hl,PnlFocus
                cp      (hl)
                ret     nz
                ld      a,(PnlFCur)
                add     a,c
                ld      c,a
                ld      a,(PnlIdx)
                call    PnlRowY
                ld      (PxRow),a
                ld      a,1
                ld      (PrintInv),a
                ld      hl,PnlCurTxt
                call    PrintStrPx2
                xor     a
                ld      (PrintInv),a
                ret
PnlCurTxt:      defb    " ",0

; ------------------------------------------------------------
;  PnlFieldCol
;  in:  PnlIdx = a PT_FIELD row
;  out: C = the byte column its text starts at
;
;  A field draws one column wider than it holds. With the buffer
;  full the cursor sits one past the last character, and without
;  that column it would be drawn on the border. Which means a
;  panel of twelve columns holds a nine character name, not ten,
;  and a ten character tape name needs a wider save under
;  surface. That is B3's constraint rather than D2's, and it is
;  the second thing the one buffer limit has decided.
; ------------------------------------------------------------
PnlFieldCol:
                ld      a,(PnlIdx)
                call    PnlRowAddr
                ld      de,5
                add     hl,de
                ld      a,(hl)                  ; the capacity
                inc     a                       ; and the cursor's parking space
                ld      c,a
                ld      a,(PnlX)
                ld      hl,PnlW
                add     a,(hl)
                dec     a
                sub     c
                ld      c,a
                ret

; ------------------------------------------------------------
;  PnlPaint
;  The whole panel: solid frame, white interior, the border
;  columns put back, the title knocked out of the bar, the rows.
; ------------------------------------------------------------
PnlPaint:
                ld      a,(PnlX)
                ld      (FrX),a
                ld      a,(PnlY)
                ld      (FrY),a
                ld      a,(PnlW)
                ld      (FrW),a
                ld      a,(PnlH)
                ld      (FrH),a
                ld      hl,$FFFF
                ld      (FrPat),hl
                call    DevFillRect
                ld      a,(PnlY)
                add     a,9
                ld      (FrY),a
                ld      (PxRow),a
                ld      a,(PnlH)
                sub     10
                ld      (FrH),a
                ld      b,a
                ld      hl,0
                ld      (FrPat),hl
                push    bc
                call    DevFillRect
                pop     bc
PnpEdge:
                push    bc
                ld      a,(PnlX)
                ld      c,a
                ld      a,(PxRow)
                call    AddrAt
                ld      a,(hl)
                or      $80
                ld      (hl),a
                ld      a,(PnlW)
                dec     a
                ld      e,a
                ld      d,0
                add     hl,de
                ld      a,(hl)
                or      $01
                ld      (hl),a
                ld      a,(PxRow)
                inc     a
                ld      (PxRow),a
                pop     bc
                djnz    PnpEdge

                ld      a,1
                ld      (PrintInv),a
                ld      a,(PnlY)
                inc     a
                ld      (PxRow),a
                ld      a,(PnlX)
                inc     a
                ld      c,a
                ld      hl,(PnlTitle)
                call    PrintStrPx2
                xor     a
                ld      (PrintInv),a

                xor     a
PnpRow:
                push    af
                call    PnlRowPaint
                pop     af
                inc     a
                ld      hl,PnlRows
                cp      (hl)
                jr      c,PnpRow
                ret

; ------------------------------------------------------------
;  PnlRowAt
;  in:  C = pixel row
;  out: A = the row index, or $FF if the point is not on one
; ------------------------------------------------------------
PnlRowAt:
                ld      a,c
                ld      hl,PnlRow0
                sub     (hl)
                jr      c,PnaNone
                srl     a
                srl     a
                srl     a
                ld      hl,PnlRows
                cp      (hl)
                ret     c
PnaNone:
                ld      a,$FF
                ret

; ------------------------------------------------------------
;  PnlActivate
;  in: A = row index
;  A cycle counts up and wraps at its own limit, an action calls
;  its routine, and a label and a field do nothing. A field's
;  activation is typing into it, which is PnlKey's business.
; ------------------------------------------------------------
PnlActivate:
                ld      (PnlIdx),a
                call    PnlRowAddr
                ld      a,(hl)
                cp      PT_CYCLE
                jr      z,PnaCycle
                cp      PT_ACTION
                ret     nz
                inc     hl
                inc     hl
                inc     hl
                ld      e,(hl)
                inc     hl
                ld      d,(hl)                  ; DE = the routine
                ld      hl,PnaDone
                push    hl                      ; so it returns into the hook
                ex      de,hl
                jp      (hl)
PnaCycle:
                push    hl
                inc     hl
                inc     hl
                inc     hl
                ld      e,(hl)
                inc     hl
                ld      d,(hl)                  ; DE = where the value lives
                inc     hl
                ld      c,(hl)                  ; C = the wrap limit
                ld      a,(de)
                inc     a
                cp      c
                jr      c,PnaStore
                xor     a
PnaStore:
                ld      (de),a
                pop     hl
                ; fall through
PnaDone:
                ; The owner gets told which row changed, so a panel that has
                ; to do something about it does not have to duplicate the
                ; hit test to find out what. An action's routine returns
                ; here too, so closing the panel and cycling a value take
                ; the same path out.
                ld      hl,(PnlAfter)
                ld      a,h
                or      l
                ret     z
                ld      a,(PnlIdx)
                jp      (hl)

; ------------------------------------------------------------
;  PnlFocusTo
;  in: A = row index
;  Moves the bar, repainting only the two rows that changed.
;  Repainting the panel would be simpler and would also throw the
;  cursor of an open field away.
; ------------------------------------------------------------
PnlFocusTo:
                ld      hl,PnlFocus
                cp      (hl)
                ret     z
                ld      c,a                     ; C = the row taking the bar
                ld      a,(hl)                  ; A = the row losing it
                ld      (hl),c
                push    af
                ld      a,c
                call    PnlCurEnd               ; the new row owns the cursor
                pop     af
                cp      $FF
                jr      z,PftNew                ; nothing had it
                push    bc
                call    PnlRowPaint
                pop     bc
PftNew:
                ld      a,c
                cp      $FF
                ret     z
                jp      PnlRowPaint

; ------------------------------------------------------------
;  PnlKey
;  in: A = key code
;  Up and down move the bar, ENTER activates. Anything else goes
;  to the focused row if it is a field, and is dropped if it is
;  not. This is what makes a panel usable on a machine with no
;  mouse, which is most of them.
; ------------------------------------------------------------
PnlKey:
                cp      KEY_UP
                jr      z,PnkUp
                cp      KEY_DOWN
                jr      z,PnkDown
                cp      KEY_ENTER
                jr      z,PnkEnter
                ; anything else belongs to the focused row if it is a field,
                ; and is dropped if it is not
                ld      c,a
                ld      a,(PnlFocus)
                cp      $FF
                ret     z
                ld      (PnlIdx),a
                call    PnlRowAddr
                ld      a,(hl)
                cp      PT_FIELD
                ret     nz
                ld      a,c
                jp      PnlFieldKey
PnkUp:
                ld      a,(PnlFocus)
                or      a
                ret     z
                dec     a
                jp      PnlFocusTo
PnkDown:
                ld      a,(PnlFocus)
                inc     a
                ld      hl,PnlRows
                cp      (hl)
                ret     nc
                jp      PnlFocusTo

; ------------------------------------------------------------
;  PnlCurEnd
;  in: A = a row index
;  Puts the field cursor at the end of that row's text, or at
;  nought if the row is not a field. Arriving at a field with the
;  cursor at the start means typing inserts in front of whatever
;  is already there, which is wrong every time: a name box seeded
;  with the current name exists so that saving again is two
;  keystrokes, and a cursor at the front turns it into nine.
; ------------------------------------------------------------
PnlCurEnd:
                push    af
                push    bc
                ld      (PnlIdx),a
                call    PnlRowAddr
                ld      a,(hl)
                cp      PT_FIELD
                jr      nz,PceZero
                ld      a,(PnlIdx)
                call    PnlValue
                call    PnlStrLen
                ld      (PnlFCur),a
                pop     bc
                pop     af
                ret
PceZero:
                xor     a
                ld      (PnlFCur),a
                pop     bc
                pop     af
                ret
PnkEnter:
                ld      a,(PnlFocus)
                cp      $FF
                ret     z
                jp      PnlActivate

; ------------------------------------------------------------
;  PnlClick
;  in: C = pixel row of the press
;  A press both moves the focus and activates, so the mouse and
;  the keyboard end in the same state and the bar always says
;  where the next keypress will go.
; ------------------------------------------------------------
PnlClick:
                call    PnlRowAt
                cp      $FF
                ret     z
                push    af
                call    PnlFocusTo
                pop     af
                jp      PnlActivate

; ------------------------------------------------------------
;  PnlFieldKey
;  in: A = key code, PnlIdx = the focused PT_FIELD row
;  A single line editor. Insert, backspace and the two cursor
;  keys, and nothing else: no selection, no clipboard, no undo.
;  It is a name box in a dialogue, not the notepad, and the
;  notepad is not being rewritten in terms of it. They share the
;  focus model and nothing else.
; ------------------------------------------------------------
PnlFieldKey:
                ld      (PnlFChar),a
                ld      a,(PnlIdx)
                call    PnlRowAddr
                push    hl
                inc     hl
                inc     hl
                inc     hl
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ld      (PnlFBuf),de            ; the buffer
                pop     hl
                ld      de,5
                add     hl,de
                ld      a,(hl)
                ld      (PnlFMax),a             ; and its capacity
                ld      hl,(PnlFBuf)
                call    PnlStrLen
                ld      (PnlFLen),a

                ld      a,(PnlFChar)
                cp      KEY_LEFT
                jr      z,PfkLeft
                cp      KEY_RIGHT
                jr      z,PfkRight
                cp      KEY_DELETE
                jr      z,PfkBack
                cp      ' '
                ret     c                       ; below space, not ours
                cp      128
                ret     nc
                jr      PfkInsert
PfkLeft:
                ld      a,(PnlFCur)
                or      a
                ret     z
                dec     a
                ld      (PnlFCur),a
                jr      PfkPaint
PfkRight:
                ld      a,(PnlFCur)
                ld      hl,PnlFLen
                cp      (hl)
                ret     nc                      ; never past the last character
                inc     a
                ld      (PnlFCur),a
                jr      PfkPaint
PfkBack:
                ld      a,(PnlFCur)
                or      a
                ret     z
                dec     a
                ld      (PnlFCur),a
                ; close the gap, moving the terminator down with it
                ld      e,a
                ld      d,0
                ld      hl,(PnlFBuf)
                add     hl,de
                ld      d,h
                ld      e,l
                inc     hl                      ; HL = source, DE = destination
PfkShift:
                ld      a,(hl)
                ld      (de),a
                or      a
                jr      z,PfkPaint
                inc     hl
                inc     de
                jr      PfkShift
PfkInsert:
                ld      a,(PnlFLen)
                ld      hl,PnlFMax
                cp      (hl)
                ret     nc                      ; full, so the key is dropped
                ; open a gap at the cursor by walking the terminator up
                ld      a,(PnlFLen)
                ld      e,a
                ld      d,0
                ld      hl,(PnlFBuf)
                add     hl,de
                ld      d,h
                ld      e,l
                inc     de                      ; DE = one past the terminator
                ld      a,(PnlFLen)
                ld      c,a
                ld      a,(PnlFCur)
                ld      b,a
                ld      a,c
                sub     b
                inc     a
                ld      b,a                     ; bytes to move, terminator too
PfkOpen:
                ld      a,(hl)
                ld      (de),a
                dec     hl
                dec     de
                djnz    PfkOpen
                ld      a,(PnlFCur)
                ld      e,a
                ld      d,0
                ld      hl,(PnlFBuf)
                add     hl,de
                ld      a,(PnlFChar)
                ld      (hl),a
                ld      a,(PnlFCur)
                inc     a
                ld      (PnlFCur),a
PfkPaint:
                ld      a,(PnlIdx)
                jp      PnlRowPaint

PnlFChar:       defb    0
PnlFBuf:        defw    0
PnlFMax:        defb    0
PnlFLen:        defb    0

; ------------------------------------------------------------
;  PnlPush and PnlPop
;  Save and restore the whole panel record, so a panel can open
;  over a panel. Carry set and nothing done if the stack is full
;  or empty, on the same principle as the surface stack: refusing
;  is recoverable and half doing it is not.
;
;  A caller always pairs these with SaveUnder and RestoreUnder,
;  and always in that nesting: push the record, save the pixels,
;  and unwind the other way round. They are two stacks rather
;  than one because the pixels under the menu bar are pushed by
;  things that have no panel record at all.
; ------------------------------------------------------------
PnlPush:
                ld      a,(PnlRecDep)
                cp      PNLDEPTH
                jr      nc,PnlStkErr
                call    PnlRecSlot
                ex      de,hl
                ld      hl,PnlRec
                ld      bc,PNLRECSZ
                ldir
                ld      hl,PnlRecDep
                inc     (hl)
                or      a
                ret

PnlPop:
                ld      a,(PnlRecDep)
                or      a
                jr      z,PnlStkErr
                dec     a
                ld      (PnlRecDep),a
                call    PnlRecSlot
                ld      de,PnlRec
                ld      bc,PNLRECSZ
                ldir
                or      a
                ret

PnlStkErr:
                scf
                ret

; A = a slot. Returns HL pointing at its PNLRECSZ bytes.
PnlRecSlot:
                ld      l,a
                ld      h,0
                ld      de,PNLRECSZ
                ld      b,a
                ld      hl,PnlRecStk
                or      a
                ret     z
PrsLoop:
                add     hl,de
                djnz    PrsLoop
                ret
