; Two device panes, adapted from ZX Desk's commander (MIT, Damian Cooper).
; Cached pages keep disk I/O out of APP_DRAW. State belongs to the window;
; transfer scratch is shared because events run sequentially.
CMDW            equ     30
CMDH            equ     12
CMDROWS         equ     6
CMDENT          equ     15              ; name[13], size[2]
CMDCACHE        equ     CMDROWS*CMDENT

; The pages are read once the window is open and the mode known, so
; the picker reads one pane. The window is composed next frame and
; blitted the one after (RepaintDue 4): the frame of the press has
; the menu's close and the open, the next the flush of the menu's
; rows and the compose, the third the blits (lf-1590). Opened,
; composed and blitted in one frame it measured 30.4 ms at 60 Hz.
CommanderOpen:
                ld      a,APP_CMD
                call    WndOpen
                ret     c
CmdOpened:
                call    CmdRefresh
                ld      a,4
                ld      (RepaintDue),a
                or      a
                ret
; FILE > OPEN uses the active backend in one pane. VIEW retains transfers.
CmdBrowseOpen:
                ld      a,APP_CMD
                call    WndOpen
                ret     c
                ld      a,1
                ld      (CmdBrowse),a
                jr      CmdOpened
; The caches are not cleared here: CmdRefresh writes every entry of
; the panes it reads, and the picker never reads its second pane.
CmdInit:
                ld      hl,CmdState
                ld      de,CmdState+1
                ld      bc,CmdCache0-CmdState-1
                ld      (hl),0
                ldir
                call    StIdent
                ld      (CmdBk0),a
                ld      a,ST_RAM
                ld      (CmdBk1),a
                ret

; CmdPane selects the live side. Global storage is restored at public exits.
CmdUse:
                ld      hl,CmdBk0
                call    CmdField
                ld      a,(hl)
                jp      StSelect
CmdField:
                ld      a,(CmdPane)
                or      a
                ret     z
                inc     hl
                ret
CmdSelAt:
                ld      hl,CmdSel0
                jp      CmdField
CmdTopAt:
                ld      hl,CmdTop0
                jp      CmdField
CmdCacheAt:
                ld      hl,CmdCache0
                ld      a,(CmdPane)
                or      a
                ret     z
                ld      hl,CmdCache1
                ret

CmdRefresh:
                call    StIdent
                push    af
                xor     a
                ld      (CmdPane),a
                call    CmdReadPane
                ld      a,(CmdBrowse)
                or      a
                jr      nz,CrfDone
                ld      a,1
                ld      (CmdPane),a
                call    CmdReadPane
CrfDone:
                pop     af
                jp      StSelect
; The live pane's page from its top into its cache: each name with
; its terminator and its size, and a zero first byte for the rows
; past the directory's end. Nothing is cleared first: that LDIR was
; 0.55 ms of every read.
CmdReadPane:
                call    CmdUse
                call    CmdCacheAt
                ld      (CmdCacheP),hl
                call    CmdTopAt
                ld      a,(hl)
                ld      (CmdIndex),a
                ld      a,CMDROWS
                ld      (CmdLeft),a
CrpLoop:
                ld      a,(CmdIndex)
                call    StDir
                jr      c,CrpBlank
                push    de                      ; size
                ld      de,(CmdCacheP)
                ld      b,12
CrpName:
                ld      a,(hl)
                or      a
                jr      z,CrpEnd
                ld      (de),a
                inc     hl
                inc     de
                djnz    CrpName
CrpEnd:
                xor     a
                ld      (de),a                  ; the terminator, cell 12 at most
                pop     de
                ld      hl,(CmdCacheP)
                ld      bc,13
                add     hl,bc
                ld      (hl),e
                inc     hl
                ld      (hl),d
                inc     hl
                ld      (CmdCacheP),hl
                ld      hl,CmdLeft
                dec     (hl)
                ret     z
                ld      hl,CmdIndex
                inc     (hl)
                jr      nz,CrpLoop              ; 8-bit directory index limit
CrpBlank:
                ld      hl,(CmdCacheP)          ; the rows left: empty
                ld      de,CMDENT
                ld      a,(CmdLeft)
                ld      b,a
CrbLoop:
                ld      (hl),0
                add     hl,de
                djnz    CrbLoop
                ret

; Drawing only reads the cached page; WinDraw has cleared the interior.
CmdDraw:
                xor     a
                ld      (CmdPane),a
                call    CmdDrawPane
                ld      a,(CmdBrowse)
                or      a
                jr      nz,CdBrowseHelp
                ld      a,1
                ld      (CmdPane),a
                call    CmdDrawPane
                xor     a
                ld      (PrintInv),a
                ld      hl,TxtCmdHelp1
                ld      bc,$0901
                call    WinPrint
                call    CmdDrawHelp2
                jr      CdStatus
; The help's second row: the active pane's keys. A tape pane has no
; file to open, copy or delete: its keys import (lf-1575). TAB paints
; this row again (CmdPaneSet).
CmdDrawHelp2:
                ld      a,(CmdActive)
                ld      (CmdPane),a
                ld      hl,CmdBk0
                call    CmdField
                ld      a,(hl)
                cp      ST_TAPE
                ld      hl,TxtCmdHelpTape
                jr      z,CdHelp2
                ld      hl,TxtCmdHelp2
CdHelp2:
                ld      bc,$0A01
                jp      WinPrint
CdBrowseHelp:
                xor     a
                ld      (PrintInv),a
                ld      hl,TxtBrowseHelp
                ld      bc,$0901
                call    WinPrint
                ld      hl,TxtBrowseHelp2
                ld      bc,$0A01
                call    WinPrint
CdStatus:
                ld      a,(CmdStatus)
                add     a,a
                ld      e,a
                ld      d,0
                ld      hl,CmdMessages
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ex      de,hl
                ld      bc,$0801
                jp      WinPrint
CmdDrawPane:
                call    CmdUseCol
                call    CmdDrawHeader
                call    CmdCacheAt
                ld      (CmdCacheP),hl
                call    CmdTopAt
                ld      a,(hl)
                ld      (CmdIndex),a
                ld      a,2
                ld      (CmdRow),a
CdpRow:
                call    CmdPaintRow
                ld      hl,(CmdCacheP)
                ld      de,CMDENT
                add     hl,de
                ld      (CmdCacheP),hl
                ld      hl,CmdIndex
                inc     (hl)
                ld      hl,CmdRow
                inc     (hl)
                ld      a,(hl)
                cp      2+CMDROWS
                jr      c,CdpRow
                ret
; The live pane's header, its device's name, inverted when it is the
; active pane; CmdUseCol done.
CmdDrawHeader:
                ld      hl,CmdBk0
                call    CmdField
                ld      a,(hl)
                cp      ST_DISK
                ld      hl,TxtCmdDisk
                jr      z,CdpHeader
                cp      ST_PRINT
                ld      hl,TxtCmdPrn
                jr      z,CdpHeader
                cp      ST_BANK
                ld      hl,TxtCmdBank
                jr      z,CdpHeader
                cp      ST_TAPE
                ld      hl,TxtCmdTape
                jr      z,CdpHeader
                ld      hl,TxtCmdRam
CdpHeader:
                push    hl
                call    CmdInverse
                pop     hl
                ld      a,(CmdCol)
                ld      c,a
                ld      b,1
                jp      WinPrint
; A = a pane: its header and its selection's row painted again into
; the window's buffer and marked, the live pane after. TAB changes
; the inversion of both (CmdPaneSet, lf-1592).
CmdPaintPaneSel:
                ld      (CmdPane),a
                call    CmdUseCol
                ld      a,1
                ld      (CmdRow),a
                call    CmdBlankRow
                call    CmdDrawHeader
                ld      a,1
                call    CmdMarkRow
                call    CmdSelAt
                ld      a,(hl)
                push    af
                call    CmdTopAt
                pop     af
                sub     (hl)
                cp      CMDROWS
                ret     nc                      ; off the page: no row of it
                jp      CmdPaintPageRow
; A = a row of the page, 0 to CMDROWS-1: CmdRow, CmdIndex and
; CmdCacheP on it, for the live pane.
CmdRowAt:
                ld      b,a
                add     a,2
                ld      (CmdRow),a
                push    bc
                call    CmdTopAt
                pop     bc
                ld      a,(hl)
                add     a,b
                ld      (CmdIndex),a
                push    bc
                call    CmdCacheAt
                pop     bc
                ld      de,CMDENT
                inc     b
CraOffset:
                dec     b
                jr      z,CraDone
                add     hl,de
                jr      CraOffset
CraDone:
                ld      (CmdCacheP),hl
                ret
; A = a row of the page, of the live pane (CmdUse done): NZ when the
; directory no longer says what the cache does on it, a file saved or
; deleted from another window since the page was read (lf-1593). A
; blank row where StDir finds a file, a name where it finds none or
; another one: all stale. The name is compared, not the size. On RAM
; this is a walk of four entries; on the disk one BDOS search.
; CmdRowNamed is the same check with the directory's answer in hand:
; HL = the name StDir found at the row's index. SHIFT+DOWN looks the
; next index up to know there is a file, and that lookup is the row's
; check too (lf-1592): it was made again here, a second BDOS search on
; the disk for every move.
CmdRowNamed:
                push    hl
                call    CmdRowAt
                pop     hl
                jr      CrsCompare
CmdRowStale:
                call    CmdRowAt
                ld      a,(CmdIndex)
                call    StDir
                jr      c,CrsNone
CrsCompare:
                ld      de,(CmdCacheP)
                ld      b,12
CrsName:
                ld      a,(de)
                cp      (hl)
                ret     nz
                or      a
                ret     z                       ; both ended: the same name
                inc     hl
                inc     de
                djnz    CrsName
                cp      a                       ; twelve cells the same
                ret
CrsNone:
                ld      de,(CmdCacheP)
                ld      a,(de)
                or      a                       ; Z on a blank row
                ret
; The row CmdRow from its cached entry: inverted when it is the
; active pane's selection, (EMPTY) on the first row of a pane with
; no files (the keys that read from it on a tape's, which has no
; directory), nothing on a blank row (WinDraw left the interior
; blank), in the picker the size too.
CmdPaintRow:
                xor     a
                ld      (PrintInv),a
                call    CmdSelAt
                ld      a,(CmdIndex)
                cp      (hl)
                call    z,CmdInverse
                ld      hl,(CmdCacheP)
                ld      a,(hl)
                or      a
                jr      nz,CprText
                ld      a,(CmdRow)
                cp      2
                ret     nz
                ld      hl,CmdBk0
                call    CmdField
                ld      a,(hl)
                cp      ST_TAPE
                ld      hl,TxtCmdTapeHint
                jr      z,CprText
                ld      hl,TxtCmdEmpty
CprText:
                ld      a,(CmdCol)
                ld      c,a
                ld      a,(CmdRow)
                ld      b,a
                call    WinPrint
                ld      a,(CmdBrowse)
                or      a
                ret     z
                jp      CmdDrawSize
; A = a row of the page, of the live pane: its cells blanked, painted
; and marked for the blit, in the live window's buffer.
CmdPaintPageRow:
                push    af
                call    CmdUseCol
                pop     af
                call    CmdRowAt
                call    CmdBlankRow
                call    CmdPaintRow
                ld      a,(CmdRow)
; A = a row of the window: for the blit, its content alone.
CmdMarkRow:
                ld      hl,WinY
                add     a,(hl)
                ld      b,a
                ld      c,1
                jp      RowsAddText
; The pane's cells of CmdRow: thirteen in a pane of two, the interior
; in the picker, whose size stands at column 22.
CmdBlankRow:
                ld      a,(CmdRow)
                ld      b,a
                ld      a,(CmdCol)
                ld      c,a
                ld      a,(CmdBrowse)
                or      a
                ld      a,13
                jr      z,CbrGo
                ld      a,CMDW-2
CbrGo:
                jp      WinBlank
; Five decimal digits, including zero, from the StDir cached length,
; straight into the buffer at column 22 of CmdRow, on the name's bank.
CmdDrawSize:
                ld      hl,(CmdCacheP)
                ld      a,(hl)
                or      a
                ret     z
                ld      de,13
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                push    de
                ld      a,(CmdRow)
                ld      b,a
                ld      c,22
                call    WinCell
                pop     de
                ex      de,hl                   ; HL = the size, DE = the cell
                ld      bc,10000
                call    CdsDigit
                ld      bc,1000
                call    CdsDigit
                ld      bc,100
                call    CdsDigit
                ld      bc,10
                call    CdsDigit
                ld      bc,1
                call    CdsDigit
                ld      a,(PrintInv)
                or      a
                ret     z
                ld      b,5
CdsInvert:
                dec     de
                ld      a,(de)
                or      INVBANK
                ld      (de),a
                djnz    CdsInvert
                ret
; HL = a number, BC = a power of ten, DE = a cell: the digit written
; there, DE on to the next, HL the remainder. The TEST build names its
; bank files with it.
CdsDigit:
                ld      a,'0'-1
CdsSub:
                inc     a
                or      a
                sbc     hl,bc
                jr      nc,CdsSub
                add     hl,bc
                ld      (de),a
                inc     de
                ret

CmdUseCol:
                ld      a,(CmdPane)
                or      a
                ld      a,1
                jr      z,CucStore
                ld      a,16
CucStore:
                ld      (CmdCol),a
                ret
CmdInverse:
                ld      a,(CmdPane)
                ld      b,a
                ld      a,(CmdActive)
                cp      b
                ld      a,0
                jr      nz,CivStore
                ld      a,INVBANK
CivStore:
                ld      (PrintInv),a
                ret

; Single-pane picker: one click selects a cached row; [OPEN] activates it.
; The event dispatcher has focused this window and excluded its frame.
; The row is checked against the directory first (CmdRowStale, one
; StDir, lf-1604): when it no longer says what the cache does, a file
; saved or deleted from another window since the page was read, the
; page is read again and the click selects what the row holds now,
; nothing when it is blank. A row the directory confirms reads
; nothing, and a blank one is no click. The click paints what it
; changes, as a key's move does (lf-1592): the row the selection left
; and the row it took, or the page's six rows when it was read again;
; it had the whole window recomposed over a fresh desktop, 18.2 ms
; with the picker alone and 20.3 with a notepad behind it, a frame
; dropped at 60 Hz, and at 50 (22.0 ms) with the notepad.
CmdPress:
                ld      a,(CmdBrowse)
                or      a
                ret     z
                ld      a,(PtrY)
                srl     a
                srl     a
                srl     a
                ld      hl,WinY
                sub     (hl)
                cp      10
                jr      z,CprOpen
                sub     2
                cp      CMDROWS
                ret     nc
                push    af
                call    StIdent
                ld      (CmdWasK),a
                xor     a
                ld      (CmdPane),a             ; the picker's one pane
                call    CmdUse
                pop     af
                call    CmdRowStale
                jr      z,CprRow
                ld      a,(CmdRow)              ; CmdRowAt's, kept through the read
                sub     2
                push    af
                call    CmdReadPane
                pop     af
                call    CmdRowAt
                ld      hl,(CmdCacheP)
                ld      a,(hl)
                or      a
                jr      z,CprRows               ; blank now: nothing to select
                ld      a,(CmdIndex)
                ld      (CmdSel0),a
CprRows:
                call    CmdPaintRows            ; the page as it is now
                jp      CmdPainted
CprRow:
                ld      hl,(CmdCacheP)
                ld      a,(hl)
                or      a
                jp      z,CmdStill              ; blank rows cannot select a file
                ld      a,(CmdRow)
                sub     2
                ld      c,a                     ; C = the row clicked
                ld      hl,CmdSel0
                ld      a,(CmdIndex)
                cp      (hl)
                jp      z,CmdStill              ; selected already: as it is
                ld      b,(hl)
                ld      (hl),a                  ; B = the selection left
                push    bc
                call    CmdTopAt
                pop     bc
                ld      a,b
                sub     (hl)
                cp      CMDROWS
                jp      nc,CprOn                ; off the page: no row of it
                ld      b,a
                jp      CmdPaintMove            ; B = the row left, C = the one taken
CprOn:
                ld      a,c
                call    CmdPaintPageRow
                jp      CmdPainted
CprOpen:
                ld      a,(PtrX)
                srl     a
                srl     a
                srl     a
                ld      hl,WinX
                sub     (hl)
                dec     a
                cp      6                       ; [OPEN], columns 1 through 6
                ret     nc
                ld      a,KEY_ENTER
                jp      CmdKey                  ; its rows marked, as a key's

CmdKey:
                ld      (CmdKeyCh),a
                call    StIdent
                ld      (CmdWasK),a
                ld      a,(CmdActive)
                ld      (CmdPane),a
                ld      a,(CmdPending)
                or      a
                jp      nz,CmdConfirm
                ld      a,(CmdKeyCh)
                cp      KEY_UP
                jp      z,CmdUp
                cp      KEY_DOWN
                jp      z,CmdDown
                cp      KEY_DELETE
                jp      z,CmdAskDialog
                cp      KEY_DEL
                jp      z,CmdAskDialog
                ld      b,a
                ld      a,(CmdBrowse)
                or      a
                ld      a,b
                jr      z,CkTransfer
                cp      KEY_ENTER
                jp      z,CmdOpen
                cp      'R'
                jp      z,CmdReload
                xor     a
                ret
CkTransfer:
                cp      'P'
                jr      z,CmdPrinterTarget
                cp      'B'
                jr      z,CmdBankTarget
                cp      KEY_LEFT
                jr      z,CmdLeftPane
                cp      KEY_RIGHT
                jr      z,CmdRightPane
                cp      9                       ; TAB
                jr      z,CmdTab
                cp      'R'
                jp      z,CmdReload
                cp      'C'
                jp      z,CmdCopy
                cp      'D'
                jp      z,CmdAskDelete
                cp      KEY_ENTER
                jp      z,CmdOpen
                cp      'I'
                jp      z,CmdImport
                cp      'A'
                jp      z,CmdImport
                xor     a
                ret
; P toggles the opposite pane between PRN and the device it showed
; before, so a pane B put on DISK is DISK again after a print. One PRN pane
; is enough: P from a PRN pane does nothing, since a write-only pane is no
; source. Browse cannot reach it.
CmdPrinterTarget:
                ld      hl,CmdBk0               ; CmdKey set CmdPane active
                call    CmdField
                ld      a,(hl)
                cp      ST_PRINT
                jp      z,CmdStill              ; nothing changes
                ld      a,(CmdPane)
                xor     1
                ld      (CmdPane),a
                ld      hl,CmdBk0
                call    CmdField
                ld      a,(hl)
                cp      ST_PRINT
                ld      a,(CmdPrnWas)
                jr      z,CptSet
                ld      a,(hl)
                ld      (CmdPrnWas),a
                ld      a,ST_PRINT
CptSet:
                ld      (hl),a
                jp      CmdReload
; B takes the opposite pane round RAM, BANK and DISK, those that are
; there; from PRN it goes back to the device the pane had before P, as
; P itself would.
CmdBankTarget:
                ld      a,(CmdActive)
                xor     1
                ld      (CmdPane),a
                ld      hl,CmdBk0
                call    CmdField
                ld      a,(hl)
                cp      ST_PRINT
                ld      a,(CmdPrnWas)
                jr      z,CptSet
                ld      a,(hl)
                ld      b,1
                push    hl
                call    StRingStep
                pop     hl
                jr      CptSet
CmdLeftPane:
                xor     a
                jr      CmdPaneSet
CmdRightPane:
                ld      a,1
                jr      CmdPaneSet
CmdTab:
                ld      a,(CmdActive)
                xor     1
; A = the pane to make active. What changes is painted and marked,
; nothing recomposed (lf-1592): the two headers and the two
; selections swap their inversion, the help's second row says the
; active pane's keys, and READY replaces another status. The
; recompose was 16.2 ms at 60 Hz with a notepad behind, a frame
; dropped. The pane it is on already: READY alone.
CmdPaneSet:
                ld      hl,CmdActive
                cp      (hl)
                jp      z,CmdPainted
                ld      (hl),a
                xor     a
                call    CmdPaintPaneSel
                ld      a,1
                call    CmdPaintPaneSel
                xor     a
                ld      (PrintInv),a
                ld      b,10
                ld      c,1
                ld      a,CMDW-2
                call    WinBlank
                call    CmdDrawHelp2
                ld      a,10
                call    CmdMarkRow
                jp      CmdPainted
; A move of the selection paints the rows it changes into the window's
; buffer and marks them, and returns 2: nothing is recomposed. A move
; was CmdRefresh (both panes read again) and a recompose and blit of
; the whole window, 21 ms at 60 Hz in the picker (lf-1590). A move
; that moves nothing paints nothing (CmdStill): the recompose it had
; was 17.4 ms with a notepad behind the picker (lf-1592).
CmdUp:
                call    CmdUse
                call    CmdSelAt
                ld      a,(hl)
                or      a
                jp      z,CmdStill
                ld      b,a                     ; the selection it leaves
                dec     a
                ld      (hl),a
                ld      c,a                     ; and the one it is on
                push    bc
                call    CmdTopAt
                pop     bc
                ld      a,c
                cp      (hl)
                jp      nc,CmdMoved             ; still on the page
                ld      (hl),a                  ; the page scrolls up a row
                jr      CmdPaged
CmdDown:
                call    CmdUse
                call    CmdSelAt
                ld      a,(hl)
                inc     a
                jp      z,CmdStill
                push    af
                call    StDir
                pop     bc                      ; B = proposed selection
                jr      c,CdnEnd
                push    hl                      ; the name the directory has there
                call    CmdSelAt
                ld      a,(hl)
                ld      (hl),b
                ld      c,b
                ld      b,a                     ; B = left, C = on
                push    bc
                call    CmdTopAt
                pop     bc
                ld      a,c
                sub     (hl)
                cp      CMDROWS
                jr      c,CdnOnPage
                inc     (hl)                    ; the page scrolls down a row
                pop     hl
                jr      CmdPaged
; Still on the page: the row it is on is checked against the name
; StDir has just found there (CmdRowNamed, no second lookup), the row
; it left as every move checks its rows (CmdMovedLeft).
CdnOnPage:
                call    CmdRows
                pop     hl
                ld      a,c
                push    bc
                call    CmdRowNamed
                pop     bc
                jr      nz,CmdPaged
                jp      CmdMovedLeft
; The top moved: the cache described the page before it, so the pane
; is read again from its new top and all of its rows painted.
CmdPaged:
                call    CmdPaintPage
                jr      CmdPainted
; The live pane read again from its top into its cache and its six
; rows painted and marked; CmdPaintRows the rows alone.
CmdPaintPage:
                call    CmdReadPane
CmdPaintRows:
                xor     a
CpgRow:
                push    af
                call    CmdPaintPageRow
                pop     af
                inc     a
                cp      CMDROWS
                jr      c,CpgRow
                ret
; No file after the selection: nothing moves, unless a row no longer
; says what the directory does, a file deleted from another window
; since the page was read: the row the move was for, which the cache
; names where StDir has just found nothing (lf-1604, no second StDir
; for it), or the selection's own (lf-1593). Then the page is read
; again and painted as it is, the selection where it stood, on a
; blank row when its own file went.
CdnEnd:
                push    bc
                call    CmdTopAt
                ld      a,(hl)
                pop     bc
                ld      c,a                     ; B = the proposed selection, C = the top
                ld      a,b
                sub     c
                cp      CMDROWS
                jr      nc,CdnSelf              ; past the page: no row cached
                push    bc
                call    CmdRowAt
                pop     bc
                ld      hl,(CmdCacheP)
                ld      a,(hl)
                or      a
                jr      nz,CmdPaged             ; a name where the directory has none
CdnSelf:
                ld      a,b
                dec     a
                sub     c
                call    CmdRowStale
                jp      z,CmdStill
                jr      CmdPaged
; B = the selection left, C = the one it is on, both on the page:
; their rows.
CmdRows:
                push    bc
                call    CmdTopAt
                ld      a,(hl)
                pop     bc
                ld      d,a
                ld      a,b
                sub     d
                ld      b,a                     ; B = the row left
                ld      a,c
                sub     d
                ld      c,a                     ; C = the row it is on
                ret
; B = the selection left, C = the one it is on, both on the page: the
; cache still describes it, so the page is not read again and those
; two rows alone are painted. Unless another window saved or deleted
; a file since the page was read (lf-1593): each of the two rows is
; checked against the directory first, and when either no longer
; says what the cache does the move takes the page route above.
CmdMoved:
                call    CmdRows
                ld      a,c
                push    bc
                call    CmdRowStale
                pop     bc
                jr      nz,CmdPaged
CmdMovedLeft:
                ld      a,b
                push    bc
                call    CmdRowStale
                pop     bc
                jr      nz,CmdPaged
; B = the row left, C = the row taken: both painted and marked.
CmdPaintMove:
                ld      a,b
                push    bc
                call    CmdPaintPageRow
                pop     bc
                ld      a,c
                call    CmdPaintPageRow
; Painted and marked: READY on the status row if it said something
; else, the backend given back, and 2 for the dispatcher.
CmdPainted:
                ld      a,(CmdStatus)
                or      a
                call    nz,CmdPaintReady
; Nothing to paint and nothing marked: a key that moves nothing, a
; click on the selection or on a blank row. The backend given back
; and 2 for the dispatcher, which then recomposes nothing.
CmdStill:
                ld      a,(CmdWasK)
                call    StSelect
                ld      a,2
                ret
CmdPaintReady:
                xor     a
; A = a status: into CmdStatus and onto the status row of the live
; window's buffer, the row marked for the blit.
CmdPaintStatus:
                ld      (CmdStatus),a
                xor     a
                ld      (PrintInv),a
                ld      b,8
                ld      c,1
                ld      a,CMDW-2
                call    WinBlank
                call    CdStatus
                ld      a,8
                jp      CmdMarkRow
CmdReload:
                xor     a
                ld      (CmdSel0),a
                ld      (CmdSel1),a
                ld      (CmdTop0),a
                ld      (CmdTop1),a
                call    CmdRefresh
                xor     a
                jp      CmdResult

; Resolve the displayed name again, not its old directory index. Another
; window may have removed an earlier entry since this page was cached.
CmdPicked:
                call    CmdSelAt
                ld      a,(hl)
                push    af
                call    CmdTopAt
                pop     af
                sub     (hl)
                cp      CMDROWS
                jr      nc,CpkMissing
                push    af
                call    CmdCacheAt
                pop     af
                ld      de,CMDENT
                or      a
                jr      z,CpkAt
CpkOffset:
                add     hl,de
                dec     a
                jr      nz,CpkOffset
CpkAt:
                ld      a,(hl)
                or      a
                jr      z,CpkMissing
                ld      de,CmdWork
                ld      bc,13
                ldir
                call    CmdUse
                xor     a
                ld      (CmdIndex),a
CpkFind:
                ld      a,(CmdIndex)
                call    StDir
                jr      c,CpkGone
                push    de                      ; current size, not cached size
                ld      de,CmdWork
                ld      b,13
CpkCompare:
                ld      a,(de)
                cp      (hl)
                jr      nz,CpkNext
                or      a
                jr      z,CpkFound
                inc     hl
                inc     de
                djnz    CpkCompare
CpkNext:
                pop     de
                ld      hl,CmdIndex
                inc     (hl)
                jr      nz,CpkFind
CpkMissing:
                scf
                ret
; The directory ended without the name the cache has on the selection's
; row: its file deleted from another window since the page was read
; (lf-1604). The key ends here with NO FILE SELECTED over a page read
; again, the selection on its blank row: the caller's return address
; goes (every caller is a CmdKey route that would say the same through
; CmdEmpty, with a recompose of the whole window and a frame dropped at
; 60 Hz) and the six rows and the status row are painted and marked
; instead, the page route of a move.
CpkGone:
                pop     hl
                call    CmdPaintPage
                ld      a,6
                call    CmdPaintStatus
                jp      CmdStill
CpkFound:
                pop     de
                ld      (CmdSize),de
                or      a
                ret
; Modal deletion captures the name before painting can touch scratch.
CmdAskDialog:
                call    CmdPicked
                jp      c,CmdEmpty
                ld      hl,CmdWork
                ld      de,CmdDeleteName
                ld      bc,13
                ldir
                ld      a,(CmdWasK)
                call    StSelect
                ld      hl,CmdDeleteName
                ld      de,TxtBrowseDelete
                ld      bc,CmdDeleteAnswer
                call    DlgDelete2
                xor     a
                ret
CmdDeleteAnswer:
                or      a
                ret     z
                ld      a,(CmdActive)
                ld      (CmdPane),a
                jp      CmdDelete               ; CmdDone marks the rows

CmdAskDelete:
                call    CmdPicked
                jp      c,CmdEmpty
                ld      a,1
                ld      (CmdPending),a
                ld      a,2
                jp      CmdResult
CmdConfirm:
                ld      a,(CmdKeyCh)
                cp      'Y'
                jr      z,CmdDelete
                cp      'N'
                jr      z,CmdCancel
                cp      KEY_ESC
                jp      nz,CmdStill             ; another key: the question stays
CmdCancel:
                xor     a
                ld      (CmdPending),a
                jp      CmdResult
CmdDelete:
                xor     a
                ld      (CmdPending),a
                call    CmdUse
                ; Name was captured when confirmation was requested, not
                ; looked up again after a directory change.
                ld      hl,CmdDeleteName
                call    StDelete
                jp      c,CmdIoError
                xor     a
                ld      (CmdSel0),a
                ld      (CmdSel1),a
                ld      (CmdTop0),a
                ld      (CmdTop1),a
                call    CmdRefresh
                ld      a,9
                jp      CmdResult

CmdCopy:
                call    CmdPicked
                jp      c,CmdEmpty
                ld      hl,(CmdSize)
                ld      de,257
                or      a
                sbc     hl,de
                jp      nc,CmdTooLarge
                ld      hl,CmdBk0
                ld      a,(hl)
                inc     hl
                cp      (hl)
                ld      a,8
                jp      z,CmdResult
                ld      hl,CmdWork
                ld      b,FA_READ
                call    StOpen
                jp      c,CmdIoError
                ld      (CmdHandle),a
                ld      hl,CmdBuf
                ld      bc,(CmdSize)
                call    StRead
                ld      (CmdGot),bc
                push    af
                ld      a,(CmdHandle)
                call    StClose
                jr      nc,CcpSourceClosed
                pop     af
                jp      CmdIoError
CcpSourceClosed:
                pop     af
                jp      c,CmdIoError
                ld      hl,(CmdGot)
                ld      de,(CmdSize)
                or      a
                sbc     hl,de
                jp      nz,CmdIoError
                ld      a,(CmdActive)
                xor     1
                ld      (CmdPane),a
                call    CmdUse
                call    StIdent
                cp      ST_PRINT
                jr      z,CmdPrintCopy
                cp      ST_TAPE
                jp      z,CmdTapeCopy
                ld      hl,CmdWork
                ld      b,FA_READ
                call    StOpen
                jr      c,CcpCreate
                call    StClose
                ld      a,3
                jp      CmdResult
CcpCreate:
                cp      STERR_NOTFOUND
                jp      nz,CmdIoError
CcpWrite:
                ld      hl,CmdWork
                ld      b,FA_OVERWRITE
                call    StOpen
                jp      c,CmdIoError
                ld      (CmdHandle),a
                ld      hl,CmdBuf
                ld      bc,(CmdSize)
                call    StWrite
                ld      (CmdGot),bc
                push    af
                ld      a,(CmdHandle)
                call    StClose
                jr      nc,CcpTargetClosed
                pop     af
                jp      CmdAbortCopy
CcpTargetClosed:
                pop     af
                jp      c,CmdAbortCopy
                ld      hl,(CmdGot)
                ld      de,(CmdSize)
                or      a
                sbc     hl,de
                jp      nz,CmdAbortCopy
                call    CmdRefresh
                ld      a,1
                jp      CmdResult
CmdPrintCopy:
                ld      hl,(CmdSize)
                ld      de,NOTESIZE
                or      a
                sbc     hl,de
                jr      z,CpcDocument
                ld      b,FA_OVERWRITE
                call    StOpen
                jr      c,CpcError
                ld      hl,CmdBuf
                ld      bc,(CmdSize)
                call    StWrite
                jr      c,CpcError
                call    StClose
                jr      CpcResult
CpcDocument:
                ld      hl,CmdBuf
                call    PrintDocument
CpcResult:
                jr      c,CpcError
                ld      a,1
                jp      CmdResult
CpcError:
                call    CmdIoError
                call    PrintAlert
                ld      a,1
                ret
; A tape as the target (lf-1603): it has no directory, so there is no
; existence check to make, and opening it for reading would wait for
; a leader that is not coming. The copy goes straight to the write,
; which TapeSave records at the close. Before a byte goes out the
; status row says so and how to stop it, on the screen now: the BIOS
; keeps the frame for the recording, as a SAVE to tape does. CTRL+STOP
; or a failed block is carry from the close, STORAGE ERROR and no
; COPIED; the delete of the half file is the tape's StNoDelete.
CmdTapeCopy:
                ld      a,15
                call    CmdNotice
                jp      CcpWrite
; A = a status: on the status row of the live window and on the
; screen now rather than at the frame's end. WinRedraw blits the row
; as the frame's end would; NtFlush puts it out, its address pair
; under SetWrt's DI.
CmdNotice:
                call    CmdPaintStatus
                call    WinRedraw
                jp      NtFlush
CmdAbortCopy:
                ; The destination did not exist before this copy. Remove a
                ; partial new file, never an existing target or the source.
                ld      hl,CmdWork
                call    StDelete
                call    CmdRefresh
                jp      CmdIoError
CmdOpen:
                call    CmdPicked
                jp      c,CmdEmpty
                ld      hl,(CmdSize)
                ld      de,NOTESIZE
                or      a
                sbc     hl,de
                jp      nz,CmdBadNote
                call    StIdent
                ld      (CmdSrcBk),a
                ld      a,(CmdBrowse)
                or      a
                jr      z,CopNew
                ; The picker closes, then find the foremost notepad by z order.
                call    WndClose
                ld      a,(WndCount)
                or      a
                jr      z,CopNew
                ld      b,a
                ld      hl,WndZ
CopFind:
                push    bc
                push    hl
                ld      a,(hl)
                call    WndSelect
                call    NoteIsFront
                pop     hl
                pop     bc
                jr      z,CopExisting
                inc     hl
                djnz    CopFind
; A fresh notepad: its load waits for the next frame (CmdLoadService),
; the file's name and the pane's device kept aside until then, not
; set in the notepad: one skipped (closed or sent behind first) stays
; NOTE on its own device, so no SAVE writes the blank over the file
; (lf-1624). The
; open (the old front's state saved, three heap walks, the blank
; document, the old front's cap flipped: 6.6 ms) and the load (the
; read, the undo snapshot, NoteSanitise: 7.1) were one frame of 16.8
; ms at 60 Hz from the Commander with a notepad open, one dropped
; (lf-1592); nothing of the notepad is composed before the load.
CopNew:
                ld      a,APP_NOTE
                call    WndOpen
                ld      a,7
                jp      c,CmdResult
                ld      a,(CmdSrcBk)
                ld      (CmdLoadBk),a
                ld      hl,CmdWork
                ld      de,CmdLoadName
                call    NoteCopyName
                ld      a,(WndCur)
                inc     a
                ld      (CmdLoadDue),a          ; the slot, plus one
                call    RepaintLater
                jp      CmdStill
CopExisting:
                ld      a,(WndCur)
                call    WndFocusTo
CopLoad:
                ld      a,(NoteBackend)
                push    af                      ; retain document ownership on failure
                ld      a,(CmdSrcBk)
                ld      (NoteBackend),a
                ld      hl,CmdWork
                call    NoteLoadAs
                jr      nc,CopOk
                pop     af
                ld      (NoteBackend),a
                ld      a,(CmdWasK)
                call    StSelect
                ld      a,(WndCur)
                call    WndTouch
                call    RowsAddLive
                call    NoteLoadAlert
                ld      a,1
                ret
CopOk:
                pop     af
                call    NoteUndoBackend         ; UNDO gives that device back
                ld      a,(CmdWasK)
                call    StSelect
                ld      a,(WndCur)              ; the notepad, recomposed and
                call    WndTouch                ; blitted from the next frame
                call    RowsLiveText            ; on: the load is this frame's
                call    RepaintLater            ; budget (lf-1590)
                ld      a,2
                ret
; The frame after CopNew's open: the file loaded into the notepad it
; opened, under the name and device kept there, when that notepad is
; still the front window (a close or a focus change in between leaves
; it as it is, blank, NOTE on its own device: lf-1624). Then as CopOk
; has it, the compose and the blit from the next frame on; a failed
; load gives the name and the device back and says so, as CopLoad
; does. Called once a frame from the main loop, before the repaint.
CmdLoadService:
                ld      a,(CmdLoadDue)
                or      a
                ret     z
                ld      hl,CmdLoadDue
                ld      (hl),0
                dec     a
                ld      hl,WndZ
                cp      (hl)
                ret     nz
                call    WndSelect
                call    NoteIsFront
                ret     nz
                ld      a,(NoteBackend)
                push    af
                ld      a,(CmdLoadBk)
                ld      (NoteBackend),a
                ld      hl,CmdLoadName
                call    NoteSetName
                ld      hl,NoteName
                call    NoteLoadNamed
                jr      nc,ClsOk
                pop     af
                ld      (NoteBackend),a
                ld      hl,TxtNoteName          ; the name only with the file
                call    NoteSetName
                ld      a,(WndCur)
                call    WndTouch
                call    RowsLiveText
                jp      NoteLoadAlert
ClsOk:
                pop     af                      ; the snapshot has the file's
                ld      a,(WndCur)              ; device, as before lf-1624
                call    WndTouch
                call    RowsLiveText
                jp      RepaintLater
; I imports the next file on the tape into the other pane's device, A
; every file (upstream eb98c06, lf-1575): a tape has no directory, so
; this is how its files get one. The active pane must be the tape; the
; keys mean nothing elsewhere. PRN is no target, nor a tape in the
; other pane (lf-1603), refused before a block is read. A file is read whole
; into CmdBuf and written under its header's name, NOTE when SAVE AS
; on the target would refuse that name (lf-1551); a file of the name
; already there is replaced, so importing a tape again replaces its
; files. One that does not fit (a header past 256 bytes, a full device,
; a short write) is refused whole with an alert, no half file left.
; A stops when the first name comes round again, the tape ends, or
; ESC or SPACE is held between files; within a block the BIOS's own
; CTRL+STOP ends the transfer. The first name written is kept in
; TapeHdr, free while no tape file is open: the header's name as the
; tape holds it in the name field, before the NOTE fallback, so two
; refused names are not the same file (lf-1602), and a flag in the
; length field that says whether anything went in at all, so A refused
; on its first file reads READY under the alert, as I does (lf-1598).
; The names are compared through their terminator: what a header
; carries behind it is not part of the name.
CmdImport:
                ld      hl,CmdBk0
                call    CmdField                ; CmdPane is the active pane
                ld      a,(hl)
                cp      ST_TAPE
                jr      z,CimTarget
                xor     a
                ret
CimTarget:
                ld      a,(CmdPane)
                xor     1
                ld      (CmdPane),a             ; the target pane from here on
                ld      hl,CmdBk0
                call    CmdField
                ld      a,(hl)
                cp      ST_PRINT
                ld      a,13
                jp      z,CmdResult
                ld      a,(hl)                  ; a tape in both panes (lf-1603):
                cp      ST_TAPE                 ; the open for writing would take
                ld      a,16                    ; TapeHdr and the close record
                jp      z,CmdResult             ; over the tape being read
                xor     a
                ld      (TapeHdr+17),a          ; no first name yet
CimNext:
                call    TapeNext
                jp      c,CimEnd
                call    CmdImportName
                ld      hl,(TapeIn+17)
                ld      de,257
                or      a
                sbc     hl,de
                jp      nc,CimRefuse            ; longer than any file here: unread
                ld      bc,(TapeIn+17)
                ld      (CmdSize),bc
                ld      hl,CmdBuf
                call    TapeBlock
                jp      c,CimError
                ld      a,(TapeHdr+17)
                or      a
                jr      z,CimWrite              ; nothing written yet: no name to meet
                ld      hl,TapeIn+4             ; the header's name, not CmdWork's NOTE
                ld      de,TapeHdr+4
                ld      b,13
CimSame:
                ld      a,(de)
                cp      (hl)
                jr      nz,CimWrite
                or      a
                jr      z,CimEnd                ; the same through the terminator
                inc     hl
                inc     de
                djnz    CimSame
                jp      CimEnd                  ; the first name again: the tape came round
CimWrite:
                call    CmdUse
                ld      hl,CmdWork
                ld      b,FA_OVERWRITE
                call    StOpen
                jr      c,CimNoOpen
                ld      (CmdHandle),a
                ld      hl,CmdBuf
                ld      bc,(CmdSize)
                call    StWrite
                ld      (CmdGot),bc
                push    af
                ld      a,(CmdHandle)
                call    StClose
                jr      nc,CimClosed
                pop     af
                jr      CimBroken
CimClosed:
                pop     af
                jr      c,CimBroken
                ld      hl,(CmdGot)
                ld      de,(CmdSize)
                or      a
                sbc     hl,de
                jr      nz,CimNoRoom            ; a short write: it did not fit
                ld      a,(TapeHdr+17)
                or      a
                jr      nz,CimKept
                ld      hl,TapeIn+4             ; the first file in: its header's name
                ld      de,TapeHdr+4
                ld      bc,13
                ldir
                inc     a                       ; A was the flag's 0: now set
                ld      (TapeHdr+17),a
CimKept:
                ld      a,(CmdKeyCh)
                cp      'A'
                jr      nz,CimDone
                call    CmdHeld
                jp      nz,CimNext
                ld      a,14                    ; IMPORT STOPPED
                jr      CimStatus
CimEnd:
                ld      a,(TapeHdr+17)
                or      a
                ld      a,12                    ; NO FILE ON TAPE
                jr      z,CimStatus
CimDone:
                ld      a,11                    ; IMPORTED
CimStatus:
                push    af
                call    CmdRefresh
                pop     af
                jp      CmdResult
CimNoOpen:
                cp      STERR_FULL
                jr      z,CimRefuse             ; no room for another file
CimError:
                call    CmdRefresh
                jp      CmdIoError
; The half file goes: the target did not have it before.
CimBroken:
                ld      hl,CmdWork
                call    StDelete
                jr      CimError
CimNoRoom:
                ld      hl,CmdWork
                call    StDelete
; Refused whole, with an alert over the status the files before it earned.
CimRefuse:
                ld      a,(TapeHdr+17)
                or      a
                ld      a,0
                jr      z,CimRefused
                ld      a,11
CimRefused:
                push    af
                call    CmdRefresh
                pop     af
                call    CmdResult
                ld      hl,TxtCmdNoFit
                ld      de,CmdWork
                call    DlgAlert
                ld      a,1
                ret
; The header's name into CmdWork, as it is when SAVE AS on the target
; device would accept it, NOTE otherwise (lf-1551): a FILE > SAVE of
; the file opened from there writes it back without a dialogue.
CmdImportName:
                ld      hl,TapeIn+4
                ld      de,CmdWork
                ld      bc,12
                ldir
                xor     a
                ld      (de),a                  ; a terminator whatever the header holds
                ld      hl,CmdWork
                ld      a,(hl)
                or      a
                jr      z,CinNote
                ld      hl,CmdBk0
                call    CmdField
                ld      a,(hl)
                ld      hl,CmdWork
                call    NoteNameValidFor        ; HL ends on the terminator
                jr      c,CinNote
                dec     hl
                ld      a,(hl)
                cp      '.'
                ret     nz
CinNote:
                ld      hl,TxtNoteName
                ld      de,CmdWork
                ld      bc,5
                ldir
                ret
; Z while ESC or SPACE (row 8, bit 0) is held: the way out of an
; import of a whole tape, between its files.
CmdHeld:
                call    PrintEsc
                ret     z
                di
                in      a,(PPIC)
                and     $F0
                or      8
                out     (PPIC),a
                in      a,(PPIB)
                ei
                bit     0,a
                ret
CmdBadNote:
                ld      a,10
                jr      CmdResult
CmdTooLarge:
                ld      a,4
                jr      CmdResult
CmdIoError:
                ld      a,5
                jr      CmdResult
CmdEmpty:
                ld      a,6
CmdResult:
                ld      (CmdStatus),a
                cp      2
                jr      nz,CmdDone
                ld      hl,CmdWork
                ld      de,CmdDeleteName
                ld      bc,13
                ldir
; The window recomposed: the next frame composes it (WndComposeFront)
; and the one after blits it, RepaintDue 4 as the open has it
; (lf-1590). The delete, R, B, P and a copy read the directories again
; in their frame, and the compose of 30x12 (7.3 ms) and the blit
; (3.3) on top of that were 18 to 23 ms: a frame dropped at 60 Hz, and
; at 50 by DELETE in the picker (lf-1592). Its rows are marked here
; and 2 goes to the dispatcher. With no window left (the picker closed
; for a notepad there was no room for) there is nothing to compose.
CmdDone:
                ld      a,(WndCount)
                or      a
                jp      z,CmdStill
                ld      a,(WndCur)
                call    WndTouch
                call    RowsLiveText
                ld      a,4
                ld      (RepaintDue),a
                jp      CmdStill

CmdMessages:    defw    TxtCmdReady, TxtCmdCopied, TxtCmdConfirm, TxtCmdExists
                defw    TxtCmdLarge, TxtCmdError, TxtCmdNone, TxtCmdNoRoom
                defw    TxtCmdSame, TxtCmdDeleted, TxtCmdNotNote
                defw    TxtCmdImported, TxtCmdNoTape, TxtCmdNoPrn, TxtCmdStopped
                defw    TxtCmdRecording, TxtCmdNoTapeTarget
TxtCmdTitle:    defb    "COMMANDER",0
TxtCmdPrn:      defb    "PRN",0
TxtCmdBank:     defb    "BANK",0
TxtCmdRam:      defb    "RAM",0
TxtCmdDisk:     defb    "DISK",0
TxtCmdTape:     defb    "TAPE",0
TxtCmdEmpty:    defb    "(EMPTY)",0
TxtCmdTapeHint: defb    "I NEXT A ALL",0
TxtCmdHelp1:    defb    "TAB PANE P PRN B BANK R LIST",0
TxtCmdHelp2:    defb    "ENTER OPEN C COPY D DELETE",0
TxtCmdHelpTape: defb    "I IMPORT NEXT A IMPORT ALL",0
TxtCmdImported: defb    "IMPORTED",0
TxtCmdNoTape:   defb    "NO FILE ON TAPE",0
TxtCmdNoPrn:    defb    "PRN IS NO TARGET",0
TxtCmdStopped:  defb    "IMPORT STOPPED",0
TxtCmdNoFit:    defb    "DOES NOT FIT",0
TxtCmdRecording: defb   "RECORDING, CTRL+STOP ENDS",0
TxtCmdNoTapeTarget: defb "TAPE IS NO TARGET",0
TxtCmdReady:    defb    "READY",0
TxtCmdCopied:   defb    "COPIED",0
TxtCmdConfirm:  defb    "DELETE SELECTED FILE? Y/N",0
TxtCmdExists:   defb    "TARGET EXISTS",0
TxtCmdLarge:    defb    "COPY LIMIT: 256 BYTES",0
TxtCmdError:    defb    "STORAGE ERROR",0
TxtCmdNone:     defb    "NO FILE SELECTED",0
TxtCmdNoRoom:   defb    "NO ROOM FOR NOTEPAD",0
TxtCmdSame:     defb    "SAME DEVICE",0
TxtCmdDeleted:  defb    "DELETED",0
TxtCmdNotNote:  defb    "NOT A 256 BYTE DOCUMENT",0

TxtBrowseHelp:  defb    "SHIFT+ARROWS ENTER OPEN",0
TxtBrowseHelp2: defb    "[OPEN] DELETE REMOVE R LIST",0
TxtBrowseDelete: defb   "DELETE THIS FILE?",0
