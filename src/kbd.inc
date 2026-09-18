; ============================================================
;  C3: keyboard
;  ReadInput reads a handful of keys directly off the ports to
;  move the pointer. That was enough for a pointer and is nowhere
;  near enough for a text field, so this scans the whole matrix,
;  decodes it with the two shift keys, and posts EV_KEY.
;
;  The eight half rows, selected by pulling one address line low.
;  Bit 0 is the first key listed, bit 4 the last, and a bit reads
;  low when its key is down.
;
;    $FE  CAPS  Z  X  C  V        $EF  0  9  8  7  6
;    $FD  A  S  D  F  G           $DF  P  O  I  U  Y
;    $FB  Q  W  E  R  T           $BF  ENTER  L  K  J  H
;    $F7  1  2  3  4  5           $7F  SPACE  SYM  M  N  B
;
;  Only one non-shift key is reported at a time. The Spectrum
;  matrix cannot tell some three key combinations apart anyway,
;  and a text field has no use for chords.
; ============================================================
KEY_LEFT        equ     1
KEY_RIGHT       equ     2
KEY_UP          equ     3
KEY_DOWN        equ     4
KEY_DELETE      equ     8
KEY_ENTER       equ     13

KBDDELAY        equ     20              ; frames before a held key repeats
KBDRATE         equ     3               ; frames between repeats after that

KbdRows:        defb    $FE,$FD,$FB,$F7,$EF,$DF,$BF,$7F

; Read all eight half rows into KbdMatrix, active high so a set bit
; means a key is down and the decode does not have to keep inverting.
KbdScan:
                ld      hl,KbdRows
                ld      de,KbdMatrix
                ld      b,8
KbsLoop:
                push    bc
                ld      a,(hl)
                ld      b,a
                ld      c,$FE
                in      a,(c)
                cpl                     ; now one means pressed
                and     $1F
                ld      (de),a
                inc     hl
                inc     de
                pop     bc
                djnz    KbsLoop
                ret

; Returns A = matrix index 0..39 of the first non-shift key held, or
; $FF if none. CAPS is index 0 and SYM is index 36, both skipped here
; and picked up separately as modifiers.
KbdFirst:
                ld      hl,KbdMatrix
                ld      b,8
                ld      c,0             ; running index
KbfRow:
                ld      a,(hl)
                or      a
                jr      z,KbfNext
                push    bc
                ld      e,(hl)
                ld      b,5
                ld      d,0             ; bit within the row
KbfBit:
                rr      e
                jr      nc,KbfNoBit
                ld      a,c
                add     a,d
                cp      0               ; CAPS SHIFT
                jr      z,KbfNoBit
                cp      36              ; SYM SHIFT
                jr      z,KbfNoBit
                pop     bc
                ret
KbfNoBit:
                inc     d
                djnz    KbfBit
                pop     bc
KbfNext:
                inc     hl
                ld      a,c
                add     a,5
                ld      c,a
                djnz    KbfRow
                ld      a,$FF
                ret

; Bit 0 of A set means CAPS is down, bit 1 means SYM.
KbdMods:
                ld      a,(KbdMatrix)
                and     $01             ; CAPS is bit 0 of the first row
                ld      c,a
                ld      a,(KbdMatrix+7)
                and     $02             ; SYM is bit 1 of the last
                or      c
                ret

; A = matrix index, KbdModState = the shift keys. Returns the code,
; or zero if that combination is not mapped.
KbdDecode:
                ld      l,a
                ld      h,0
                ld      a,(KbdModState)
                and     3
                cp      3
                jr      z,KbdExt                ; both shifts, tested first
                bit     1,a
                jr      nz,KbdSym
                bit     0,a
                jr      nz,KbdCaps
                ld      de,KbdBase
                jr      KbdLook
KbdExt:
                ld      de,KbdExtT
                jr      KbdLook
KbdCaps:
                ld      de,KbdCapsT
                jr      KbdLook
KbdSym:
                ld      de,KbdSymT
KbdLook:
                add     hl,de
                ld      a,(hl)
                ret

KbdBase:
                defb    0,'Z','X','C','V'
                defb    'A','S','D','F','G'
                defb    'Q','W','E','R','T'
                defb    '1','2','3','4','5'
                defb    '0','9','8','7','6'
                defb    'P','O','I','U','Y'
                defb    KEY_ENTER,'L','K','J','H'
                defb    ' ',0,'M','N','B'

; CAPS turns the digit row into the cursor keys and delete, exactly
; where forty years of muscle memory expects them.
KbdCapsT:
                defb    0,'Z','X','C','V'
                defb    'A','S','D','F','G'
                defb    'Q','W','E','R','T'
                defb    0,0,0,0,KEY_LEFT
                defb    KEY_DELETE,0,KEY_RIGHT,KEY_UP,KEY_DOWN
                defb    'P','O','I','U','Y'
                defb    KEY_ENTER,'L','K','J','H'
                defb    ' ',0,'M','N','B'

; ------------------------------------------------------------
;  EXTEND MODE, both shifts held, is where a Spectrum puts a
;  third meaning for a key. G2 already used it to gate the
;  pointer keys, but it did that by reading the ports in
;  ReadInput while KbdDecode still handed the key to the symbol
;  table, so both shifts and O moved the pointer left and typed a
;  semicolon at the same time. Nobody holds both shifts to type a
;  symbol, so this table takes the mode over completely and the
;  double meaning goes with it.
;
;  Every entry is either a shortcut or nought. Nought is what the
;  four pointer keys and the cursor digits get, because ReadInput
;  reads those from the ports and a key that also posted an event
;  would do two things at once, which is the fault this table is
;  fixing.
;
;  Nothing here can reach an application: the codes are $81 and
;  up and HdlKey takes them before a window is selected, so a
;  shortcut cannot be mistaken for text and text cannot be
;  mistaken for a shortcut.
; ------------------------------------------------------------
KbdExtT:
                defb    0,0,SC_NEXT,SC_CASCADE,SC_SAVEAS
                defb    0,SC_SAVE,SC_DESKTOP,SC_FILES,SC_SETTINGS
                defb    0,SC_CLOSE,0,SC_PRINT,SC_TILE
                defb    0,0,0,0,0
                defb    0,0,0,0,0
                defb    0,0,SC_ABOUT,0,0
                defb    0,SC_CALENDAR,SC_CLOCK,0,0
                defb    0,0,0,SC_NEW,0

KbdSymT:
                defb    0,':',0,'?','/'
                defb    0,0,0,0,0
                defb    0,0,0,'<','>'
                defb    '!','@','#','$','%'
                defb    '_',')','(',39,'&'
                defb    34,';',0,0,0
                defb    KEY_ENTER,'=','+','-','^'
                defb    ' ',0,'.',',','*'

; Called once a frame. Posts EV_KEY on a fresh press, then again on a
; repeat once the key has been held past KBDDELAY. Only one key is
; tracked, so rolling from one key to another posts the new one at
; once rather than waiting out the old one's timer.
KbdPoll:
                call    KbdScan
                call    KbdFirst
                cp      $FF
                jr      z,KbpNone
                call    KbdMods
                ld      (KbdModState),a
                call    KbdFirst
                call    KbdDecode
                or      a
                jr      z,KbpNone               ; held, but not mapped
                ld      c,a
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

KbdMatrix:      defs    8
KbdModState:    defb    0
KbdLast:        defb    0
KbdTimer:       defb    0
