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
                ld      hl,TxtCmdHelp2
                ld      bc,$0A01
                call    WinPrint
                jr      CdStatus
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
                ld      hl,TxtCmdRam
CdpHeader:
                push    hl
                call    CmdInverse
                pop     hl
                ld      a,(CmdCol)
                ld      c,a
                ld      b,1
                call    WinPrint
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
; The row CmdRow from its cached entry: inverted when it is the
; active pane's selection, (EMPTY) on the first row of a pane with
; no files, nothing on a blank row (WinDraw left the interior blank),
; in the picker the size too.
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
                ld      b,a
                ld      hl,CmdCache0
                ld      de,CMDENT
                or      a
                jr      z,CprRow
CprNext:
                add     hl,de
                dec     a
                jr      nz,CprNext
CprRow:
                ld      a,(hl)
                or      a
                ret     z                       ; blank rows cannot select a file
                ld      a,(CmdTop0)
                add     a,b
                ld      (CmdSel0),a
                jr      CprPaint
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
                call    CmdKey
CprPaint:
                ld      a,(WndCur)
                call    WndTouch
                jp      RowsAddLive

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
                jp      z,CmdDone
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
CmdPaneSet:
                ld      (CmdActive),a
                xor     a
                jp      CmdResult
; A move of the selection paints the rows it changes into the window's
; buffer and marks them, and returns 2: nothing is recomposed. A move
; was CmdRefresh (both panes read again) and a recompose and blit of
; the whole window, 21 ms at 60 Hz in the picker (lf-1590).
CmdUp:
                call    CmdSelAt
                ld      a,(hl)
                or      a
                jp      z,CmdDone
                ld      b,a                     ; the selection it leaves
                dec     a
                ld      (hl),a
                ld      c,a                     ; and the one it is on
                push    bc
                call    CmdTopAt
                pop     bc
                ld      a,c
                cp      (hl)
                jr      nc,CmdMoved             ; still on the page
                ld      (hl),a                  ; the page scrolls up a row
                jr      CmdPaged
CmdDown:
                call    CmdUse
                call    CmdSelAt
                ld      a,(hl)
                inc     a
                jp      z,CmdDone
                push    af
                call    StDir
                pop     bc                      ; B = proposed selection
                jp      c,CmdDone
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
                jr      c,CmdMoved
                inc     (hl)                    ; the page scrolls down a row
; The top moved: the cache described the page before it, so the pane
; is read again from its new top and all of its rows painted.
CmdPaged:
                call    CmdReadPane
                xor     a
CpgRow:
                push    af
                call    CmdPaintPageRow
                pop     af
                inc     a
                cp      CMDROWS
                jr      c,CpgRow
                jr      CmdPainted
; B = the selection left, C = the one it is on, both on the page: the
; cache still describes it, so the directory is not read and those
; two rows alone are painted.
CmdMoved:
                push    bc
                call    CmdTopAt
                ld      a,(hl)
                pop     bc
                ld      d,a
                ld      a,b
                sub     d
                push    bc
                push    de
                call    CmdPaintPageRow
                pop     de
                pop     bc
                ld      a,c
                sub     d
                call    CmdPaintPageRow
; Painted and marked: READY on the status row if it said something
; else, the backend given back, and 2 for the dispatcher.
CmdPainted:
                ld      a,(CmdStatus)
                or      a
                call    nz,CmdPaintReady
                ld      a,(CmdWasK)
                call    StSelect
                ld      a,2
                ret
CmdPaintReady:
                xor     a
                ld      (CmdStatus),a
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
                ret     c
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
                call    CmdDelete
                ld      a,(WndCur)
                call    WndTouch
                jp      RowsAddLive

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
                jp      nz,CmdDone
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
CopNew:
                ld      a,APP_NOTE
                call    WndOpen
                ld      a,7
                jp      c,CmdResult
                jr      CopLoad
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
CmdDone:
                ld      a,(CmdWasK)
                call    StSelect
                ld      a,1
                ret

CmdMessages:    defw    TxtCmdReady, TxtCmdCopied, TxtCmdConfirm, TxtCmdExists
                defw    TxtCmdLarge, TxtCmdError, TxtCmdNone, TxtCmdNoRoom
                defw    TxtCmdSame, TxtCmdDeleted, TxtCmdNotNote
TxtCmdTitle:    defb    "COMMANDER",0
TxtCmdPrn:      defb    "PRN",0
TxtCmdBank:     defb    "BANK",0
TxtCmdRam:      defb    "RAM",0
TxtCmdDisk:     defb    "DISK",0
TxtCmdEmpty:    defb    "(EMPTY)",0
TxtCmdHelp1:    defb    "TAB PANE P PRN B BANK R LIST",0
TxtCmdHelp2:    defb    "ENTER OPEN C COPY D DELETE",0
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
