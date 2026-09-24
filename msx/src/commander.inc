; Two device panes, adapted from ZX Desk's commander (MIT, Damian Cooper).
; Cached pages keep disk I/O out of APP_DRAW. State belongs to the window;
; transfer scratch is shared because events run sequentially.
CMDW            equ     30
CMDH            equ     12
CMDROWS         equ     6
CMDENT          equ     15              ; name[13], size[2]
CMDCACHE        equ     CMDROWS*CMDENT

CommanderOpen:
                ld      a,APP_CMD
                jp      WndOpen
; FILE > OPEN uses the active backend in one pane. VIEW retains transfers.
CmdBrowseOpen:
                call    CommanderOpen
                ret     c
                ld      a,1
                ld      (CmdBrowse),a
                ret
CmdInit:
                ld      hl,CmdState
                ld      de,CmdState+1
                ld      bc,CMDSTSZ-1
                ld      (hl),0
                ldir
                call    StIdent
                ld      (CmdBk0),a
                ld      a,ST_RAM
                ld      (CmdBk1),a
                jp      CmdRefresh

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
CmdReadPane:
                call    CmdUse
                call    CmdCacheAt
                ld      (CmdCacheP),hl
                ld      d,h
                ld      e,l
                inc     de
                ld      (hl),0
                ld      bc,CMDCACHE-1
                ldir
                call    CmdTopAt
                ld      a,(hl)
                ld      (CmdIndex),a
                ld      a,CMDROWS
                ld      (CmdLeft),a
CrpLoop:
                ld      a,(CmdIndex)
                call    StDir
                ret     c
                push    de                      ; size
                ld      de,(CmdCacheP)
                ld      b,12
CrpName:
                ld      a,(hl)
                or      a
                jr      z,CrpSize
                ld      (de),a
                inc     hl
                inc     de
                djnz    CrpName
CrpSize:
                pop     de
                ld      hl,(CmdCacheP)
                ld      bc,13
                add     hl,bc
                ld      (hl),e
                inc     hl
                ld      (hl),d
                inc     hl
                ld      (CmdCacheP),hl
                ld      hl,CmdIndex
                inc     (hl)
                ret     z                       ; 8-bit directory index limit
                ld      hl,CmdLeft
                dec     (hl)
                jr      nz,CrpLoop
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
                xor     a
                ld      (PrintInv),a
                call    CmdSelAt
                ld      a,(CmdIndex)
                cp      (hl)
                call    z,CmdInverse
                ld      hl,(CmdCacheP)
                ld      a,(hl)
                or      a
                jr      nz,CdpText
                ld      hl,TxtCmdEmpty
                ld      a,(CmdRow)
                cp      2
                jr      z,CdpText
                ld      hl,TxtCmdBlank
CdpText:
                ld      a,(CmdCol)
                ld      c,a
                ld      a,(CmdRow)
                ld      b,a
                call    WinPrint
                ld      a,(CmdBrowse)
                or      a
                call    nz,CmdDrawSize
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
; Five decimal digits, including zero, from the StDir cached length.
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
                ex      de,hl
                ld      ix,CmdNumber
                ld      de,10000
                call    CdsDigit
                ld      de,1000
                call    CdsDigit
                ld      de,100
                call    CdsDigit
                ld      de,10
                call    CdsDigit
                ld      de,1
                call    CdsDigit
                ld      (ix+0),0
                ld      hl,CmdNumber
                ld      a,(CmdRow)
                ld      b,a
                ld      c,22
                jp      WinPrint
CdsDigit:
                ld      a,'0'-1
CdsSub:
                inc     a
                or      a
                sbc     hl,de
                jr      nc,CdsSub
                add     hl,de
                ld      (ix+0),a
                inc     ix
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
CmdUp:
                call    CmdSelAt
                ld      a,(hl)
                or      a
                jp      z,CmdDone
                dec     (hl)
                dec     a
                push    af
                call    CmdTopAt
                pop     af
                cp      (hl)
                jr      nc,CmdNavDone
                ld      (hl),a
                jr      CmdNavDone
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
                ld      (hl),b
                push    bc
                call    CmdTopAt
                pop     bc
                ld      a,b
                sub     (hl)
                cp      CMDROWS
                jr      c,CmdNavDone
                inc     (hl)
CmdNavDone:
                call    CmdRefresh
                xor     a
                jp      CmdResult
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
                ld      a,(CmdSrcBk)
                ld      (NoteBackend),a
                ld      hl,CmdWork
                call    NoteLoadAs
                jr      nc,CopOk
                ld      a,(CmdWasK)
                call    StSelect
                ld      a,(WndCur)
                call    WndTouch
                call    RowsAddLive
                ld      hl,TxtCmdError
                ld      de,0
                call    DlgAlert
                ld      a,1
                ret
CopOk:
                ld      a,(CmdWasK)
                call    StSelect
                ld      a,1                     ; repaint the new notepad
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
TxtCmdRam:      defb    "RAM",0
TxtCmdDisk:     defb    "DISK",0
TxtCmdEmpty:    defb    "(EMPTY)",0
TxtCmdBlank:    defb    0
TxtCmdHelp1:    defb    "TAB PANE SHIFT+ARROWS R LIST",0
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
TxtBrowseHelp2: defb    "DELETE REMOVE R LIST",0
TxtBrowseDelete: defb   "DELETE THIS FILE?",0
