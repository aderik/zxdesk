; ============================================================
;  Keyboard, MSX matrix through the PPI
;  The ZX version scanned eight half rows off the ULA port. An
;  MSX has eleven rows of eight keys behind PPI port C (row
;  select, low nibble) and port B (columns, a bit reads low when
;  its key is down). Rows 9 and 10 are the numeric keypad and
;  are not scanned.
;
;    row 0: 7 6 5 4 3 2 1 0            row 5: Z Y X W V U T S
;    row 1: ; ] [ \ = - 9 8            row 6: F3 F2 F1 CODE CAPS GRAPH CTRL SHIFT
;    row 2: B A dead / . , ` '         row 7: RET SEL BS STOP TAB ESC F5 F4
;    row 3: J I H G F E D C            row 8: RIGHT DOWN UP LEFT DEL INS HOME SPACE
;    row 4: R Q P O N M L K
;
;  Bit 0 is the rightmost key listed. Only one non-modifier key
;  is reported at a time, as before: a text field has no use for
;  chords. The cursor keys move the pointer in ReadInput and are
;  nought here, so a key never does two things at once. SHIFT is
;  the one modifier decoded; CTRL clicks the pointer in ReadInput.
;  GRAPH is not mapped.
; ============================================================
KEY_LEFT        equ     1
KEY_RIGHT       equ     2
KEY_UP          equ     3
KEY_DOWN        equ     4
KEY_DELETE      equ     8
KEY_ENTER       equ     13
KEY_ESC         equ     27

KBDROWS         equ     9
KBDDELAY        equ     20              ; frames before a held key repeats
KBDRATE         equ     3               ; frames between repeats after that

; Read the nine rows into KbdMatrix, active high so a set bit means
; a key is down. The upper nibble of port C carries the cassette
; motor, the caps LED and the key click, so it is kept.
KbdScan:
                ld      hl,KbdMatrix
                ld      c,0
                ld      b,KBDROWS
KbsLoop:
                in      a,(PPIC)
                and     $F0
                or      c
                out     (PPIC),a
                in      a,(PPIB)
                cpl
                ld      (hl),a
                inc     hl
                inc     c
                djnz    KbsLoop
                ret

; Returns A = matrix index 0..71 of the first key held that is not a
; modifier, or $FF if none. Row 6 is modifiers and function keys and
; is skipped whole.
KbdFirst:
                ld      hl,KbdMatrix
                ld      b,KBDROWS
                ld      c,0             ; running index
KbfRow:
                ld      a,c
                cp      6*8
                jr      z,KbfNext
                ld      a,(hl)
                or      a
                jr      z,KbfNext
                push    bc
                ld      e,(hl)
                ld      b,8
                ld      d,0             ; bit within the row
KbfBit:
                rr      e
                jr      nc,KbfNoBit
                pop     bc
                ld      a,c
                add     a,d
                ret
KbfNoBit:
                inc     d
                djnz    KbfBit
                pop     bc
KbfNext:
                ld      a,c
                add     a,8
                ld      c,a
                inc     hl
                djnz    KbfRow
                ld      a,$FF
                ret

; Bit 0 = SHIFT held.
KbdMods:
                ld      a,(KbdMatrix+6)
                and     $01
                ret

; A = matrix index, KbdModState = the shift key. Returns the code, or
; zero if that combination is not mapped.
KbdDecode:
                ld      l,a
                ld      h,0
                ld      de,KbdBase
                ld      a,(KbdModState)
                bit     0,a
                jr      z,KbdLook
                ld      de,KbdShiftT
KbdLook:
                add     hl,de
                ld      a,(hl)
                ret

; Upper case without SHIFT: the desktop's font is upper case, as the
; ZX one was, and lower case waits for a font that has it.
KbdBase:
                defb    '0','1','2','3','4','5','6','7'
                defb    '8','9','-','=',92,'[',']',';'
                defb    39,'`',',','.','/',0,'A','B'
                defb    'C','D','E','F','G','H','I','J'
                defb    'K','L','M','N','O','P','Q','R'
                defb    'S','T','U','V','W','X','Y','Z'
                defb    0,0,0,0,0,0,0,0
                defb    0,0,KEY_ESC,9,0,KEY_DELETE,KEY_ENTER,0
                defb    ' ',0,0,KEY_DELETE,0,0,0,0

KbdShiftT:
                defb    ')','!','@','#','$','%','^','&'
                defb    '*','(','_','+','|','{','}',':'
                defb    34,'~','<','>','?',0,'A','B'
                defb    'C','D','E','F','G','H','I','J'
                defb    'K','L','M','N','O','P','Q','R'
                defb    'S','T','U','V','W','X','Y','Z'
                defb    0,0,0,0,0,0,0,0
                defb    0,0,KEY_ESC,9,0,KEY_DELETE,KEY_ENTER,0
                defb    ' ',0,0,KEY_DELETE,KEY_LEFT,KEY_UP,KEY_DOWN,KEY_RIGHT

; Called once a frame. Posts EV_KEY on a fresh press, then again on a
; repeat once the key has been held past KBDDELAY. Only one key is
; tracked, so rolling from one key to another posts the new one at
; once rather than waiting out the old one's timer.
KbdPoll:
                call    KbdScan
                call    KbdMods
                ld      (KbdModState),a
                call    KbdFirst
                cp      $FF
                jr      z,KbpNone
                call    KbdDecode
                or      a
                jr      z,KbpNone               ; held, but not mapped
                ld      c,a
                ld      hl,KbdHeld
                inc     (hl)
                ld      a,(KbdLast)
                cp      c
                jr      nz,KbpNew
                ld      a,(KbdTimer)
                dec     a
                ld      (KbdTimer),a
                ret     nz
                ld      a,KBDRATE
                ld      (KbdTimer),a
                jr      KbpPost
KbpNew:
                ld      a,c
                ld      (KbdLast),a
                ld      a,KBDDELAY
                ld      (KbdTimer),a
KbpPost:
                ld      a,(KbdLast)
                ld      b,a
                ld      c,0
                ld      a,EV_KEY
                jp      EvPost
KbpNone:
                xor     a
                ld      (KbdLast),a
                ret
