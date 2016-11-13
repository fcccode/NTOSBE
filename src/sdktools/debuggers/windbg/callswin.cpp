/*++

Copyright (c) 1992-2002  Microsoft Corporation

Module Name:

    callswin.cpp

Abstract:

    This module contains the main line code for display of calls window.

Environment:

    Win32, User Mode

--*/


#include "precomp.hxx"
#pragma hdrstop

#define MIN_FRAMES 10
#define MORE_LESS 10

#define CALLS_CONTEXT_ID_BASE 0x100

#define TBB_MORE     9
#define TBB_LESS     10
#define TBB_COPY_ALL 13

// The IDs of the buttons are the bit shift of the
// corresponding flag.
TBBUTTON g_CallsTbButtons[] =
{
    TEXT_TB_BTN(0, "Args", BTNS_CHECK),
    TEXT_TB_BTN(1, "Func info", BTNS_CHECK),
    TEXT_TB_BTN(2, "Source", BTNS_CHECK),
    TEXT_TB_BTN(3, "Addrs", BTNS_CHECK),
    TEXT_TB_BTN(4, "Headings", BTNS_CHECK),
    TEXT_TB_BTN(5, "Nonvolatile regs", BTNS_CHECK),
    TEXT_TB_BTN(6, "Frame nums", BTNS_CHECK),
    TEXT_TB_BTN(7, "Arg types", BTNS_CHECK),
    SEP_TB_BTN(),
    TEXT_TB_BTN(TBB_MORE, "More", 0),
    TEXT_TB_BTN(TBB_LESS, "Less", 0),
    SEP_TB_BTN(),
    TEXT_TB_BTN(ID_SHOW_TOOLBAR, "Toolbar", 0),
    SEP_TB_BTN(),
    TEXT_TB_BTN(TBB_COPY_ALL, "Copy stack to clipboard", 0),
};

#define NUM_CALLS_MENU_BUTTONS \
    (sizeof(g_CallsTbButtons) / sizeof(g_CallsTbButtons[0]))
#define NUM_CALLS_TB_BUTTONS \
    (NUM_CALLS_MENU_BUTTONS - 4)

HMENU CALLSWIN_DATA::s_ContextMenu;

//
//
//
CALLSWIN_DATA::CALLSWIN_DATA()
    : SINGLE_CHILDWIN_DATA(1024)
{
    m_enumType = CALLS_WINDOW;
    m_Flags = 0;
    m_Frames = 20;
    m_FramesFound = 0;
    m_TextOffset = 0;
    m_WarningLine = 0xffffffff;
}

void
CALLSWIN_DATA::Validate()
{
    SINGLE_CHILDWIN_DATA::Validate();

    Assert(CALLS_WINDOW == m_enumType);
}

HRESULT
CALLSWIN_DATA::ReadState(void)
{
    HRESULT Status;
    ULONG FramesFound;
    ULONG TextOffset;

    Empty();
    
    //
    // Record the raw frame data first.
    //

    // Preallocate space to record the raw frames.
    if (AddData(sizeof(DEBUG_STACK_FRAME) * m_Frames) == NULL)
    {
        return E_OUTOFMEMORY;
    }

    // Allocate a separate buffer to hold the frames while
    // calling OutputStackTrace on them.  We can't just pass
    // in the state buffer pointer as resizing of the state
    // buffer may cause the data pointer to change.
    PDEBUG_STACK_FRAME RawFrames = (PDEBUG_STACK_FRAME)malloc(m_DataUsed);
    if (RawFrames == NULL)
    {
        return E_OUTOFMEMORY;
    }

    Status = g_pDbgControl->GetStackTrace(0, 0, 0, RawFrames, m_Frames,
                                          &FramesFound);
    if (Status != S_OK)
    {
        free(RawFrames);
        m_FramesFound = 0;
        m_TextOffset = 0;
        return Status;
    }
    
    TextOffset = m_DataUsed;
    
    g_OutStateBuf.SetBuffer(this);
    if ((Status = g_OutStateBuf.Start(FALSE)) != S_OK)
    {
        free(RawFrames);
        m_FramesFound = 0;
        m_TextOffset = 0;
        return Status;
    }

    // If nonvolatile registers were requested we can't use just
    // our saved frames as they require full context information.
    Status = g_pOutCapControl->
        OutputStackTrace(DEBUG_OUTCTL_THIS_CLIENT |
                         DEBUG_OUTCTL_OVERRIDE_MASK |
                         DEBUG_OUTCTL_NOT_LOGGED,
                         (m_Flags & DEBUG_STACK_NONVOLATILE_REGISTERS) ?
                         NULL : RawFrames, FramesFound, m_Flags);
    if (Status == S_OK)
    {
        Status = g_OutStateBuf.End(FALSE);
    }
    else
    {
        g_OutStateBuf.End(FALSE);
    }

    // Now that the state buffer is stable put the raw frame
    // data in.
    memcpy(m_Data, RawFrames, TextOffset);
    m_FramesFound = FramesFound;
    m_TextOffset = TextOffset;
    free(RawFrames);

    // Check and see if there's a stack trace warning and remember
    // it so we can ignore it.  We still want it in the text
    // as a reminder so we can't just remove it.
    ULONG Line = 0;
    PSTR Scan = (PSTR)m_Data + m_TextOffset;

    m_WarningLine = 0xffffffff;
    while (Scan < (PSTR)m_Data + m_DataUsed)
    {
        if (!memcmp(Scan, "WARNING:", 8))
        {
            m_WarningLine = Line;
            break;
        }

        Scan = strchr(Scan, '\n');
        if (!Scan)
        {
            break;
        }
        else
        {
            Scan++;
        }

        Line++;
    }
    
    return Status;
}

void
CALLSWIN_DATA::Copy()
{
    LRESULT Line = SendMessage(m_hwndChild, LB_GETCURSEL, 0, 0);
    Assert(Line != LB_ERR);

    LRESULT Len = SendMessage(m_hwndChild, LB_GETTEXTLEN, Line, 0);
    if (Len <= 0)
    {
        return;
    }

    Len++;
    
    PSTR Text = (PSTR)malloc(Len);

    if (!Text) 
    {
        return;
    }
    SendMessage(m_hwndChild, LB_GETTEXT, Line, (LPARAM)Text);
    Text[Len - 1] = 0;

    CopyToClipboard(Text, FALSE);

    free (Text);
    return;

}

BOOL 
CALLSWIN_DATA::CanWriteTextToFile()
{
    return TRUE;
}

HRESULT
CALLSWIN_DATA::WriteTextToFile(HANDLE File)
{
    HRESULT Status;
    BOOL Write;
    ULONG Done;
    
    if ((Status = UiLockForRead()) != S_OK)
    {
        return Status;
    }

    Write = WriteFile(File, (PSTR)m_Data + m_TextOffset,
                      m_DataUsed - m_TextOffset, &Done, NULL);

    UnlockStateBuffer(this);

    if (!Write)
    {
        return WIN32_LAST_STATUS();
    }
    if (Done < m_DataUsed - m_TextOffset)
    {
        return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
    }

    return S_OK;
}

HMENU
CALLSWIN_DATA::GetContextMenu(void)
{
    ULONG i;
    
    //
    // We only keep one menu around for all call windows
    // so apply the menu check state for this particular
    // window.
    // In reality there's only one calls window anyway,
    // but this is a good example of how to handle
    // multi-instance windows.
    //

    for (i = 0; i < NUM_CALLS_TB_BUTTONS; i++)
    {
        CheckMenuItem(s_ContextMenu, i + CALLS_CONTEXT_ID_BASE,
                      MF_BYCOMMAND | ((m_Flags & (1 << i)) ? MF_CHECKED : 0));
    }
    CheckMenuItem(s_ContextMenu, ID_SHOW_TOOLBAR + CALLS_CONTEXT_ID_BASE,
                  MF_BYCOMMAND | (m_ShowToolbar ? MF_CHECKED : 0));
    
    return s_ContextMenu;
}

void
CALLSWIN_DATA::OnContextMenuSelection(UINT Item)
{
    Item -= CALLS_CONTEXT_ID_BASE;

    switch(Item)
    {
    case TBB_MORE:
        m_Frames += MORE_LESS;
        break;
    case TBB_LESS:
        if (m_Frames >= MIN_FRAMES + MORE_LESS)
        {
            m_Frames -= MORE_LESS;
        }
        break;
    case TBB_COPY_ALL:
        if (UiLockForRead() == S_OK)
        {
            CopyToClipboard((PSTR)m_Data + m_TextOffset, TRUE);
            UnlockStateBuffer(this);
        }
        break;
    case ID_SHOW_TOOLBAR:
        SetShowToolbar(!m_ShowToolbar);
        break;
    default:
        m_Flags ^= 1 << Item;
        SyncUiWithFlags(1 << Item);
        break;
    }
    if (g_Workspace != NULL)
    {
        g_Workspace->AddDirty(WSPF_DIRTY_WINDOWS);
    }
    UiRequestRead();
}

HRESULT
CALLSWIN_DATA::CodeExprAtCaret(PSTR Expr, PULONG64 Offset)
{
    HRESULT Status;
    
    ULONG Line = SelectionToFrame();
    if (Line >= m_FramesFound)
    {
        return E_INVALIDARG;
    }
    
    if ((Status = UiLockForRead()) != S_OK)
    {
        // Don't want to return any success codes here.
        return FAILED(Status) ? Status : E_FAIL;
    }
    
    PDEBUG_STACK_FRAME RawFrames = (PDEBUG_STACK_FRAME)m_Data;
    if (Expr != NULL)
    {
        sprintf(Expr, "0x%I64x", RawFrames[Line].InstructionOffset);
    }
    if (Offset != NULL)
    {
        *Offset = RawFrames[Line].InstructionOffset;
    }
    UnlockStateBuffer(this);
    return S_OK;
}

HRESULT
CALLSWIN_DATA::StackFrameAtCaret(PDEBUG_STACK_FRAME Frame)
{
    HRESULT Status;
    
    ULONG Line = SelectionToFrame();
    if (Line >= m_FramesFound)
    {
        return E_INVALIDARG;
    }
    
    if ((Status = UiLockForRead()) != S_OK)
    {
        // Don't want to return any success codes here.
        return FAILED(Status) ? Status : E_FAIL;
    }
    
    PDEBUG_STACK_FRAME RawFrames = (PDEBUG_STACK_FRAME)m_Data;
    *Frame = RawFrames[Line];
    UnlockStateBuffer(this);
    return S_OK;
}

BOOL
CALLSWIN_DATA::OnCreate(void)
{
    if (s_ContextMenu == NULL)
    {
        s_ContextMenu = CreateContextMenuFromToolbarButtons
            (NUM_CALLS_MENU_BUTTONS, g_CallsTbButtons, CALLS_CONTEXT_ID_BASE);
        if (s_ContextMenu == NULL)
        {
            return FALSE;
        }
    }
    
    m_Toolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL,
                               WS_CHILD | WS_VISIBLE |
                               TBSTYLE_WRAPABLE | TBSTYLE_LIST | CCS_TOP,
                               0, 0, m_Size.cx, 0, m_Win, (HMENU)ID_TOOLBAR,
                               g_hInst, NULL);
    if (m_Toolbar == NULL)
    {
        return FALSE;
    }
    SendMessage(m_Toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessage(m_Toolbar, TB_ADDBUTTONS, NUM_CALLS_TB_BUTTONS,
                (LPARAM)&g_CallsTbButtons);
    SendMessage(m_Toolbar, TB_AUTOSIZE, 0, 0);
    
    RECT Rect;
    GetClientRect(m_Toolbar, &Rect);
    m_ToolbarHeight = Rect.bottom - Rect.top;
    m_ShowToolbar = TRUE;

    m_hwndChild = CreateWindowEx(
        WS_EX_CLIENTEDGE,                           // Extended style
        _T("LISTBOX"),                              // class name
        NULL,                                       // title
        WS_CHILD | WS_VISIBLE
        | WS_MAXIMIZE
        | WS_HSCROLL | WS_VSCROLL
        | LBS_NOTIFY | LBS_WANTKEYBOARDINPUT
        | LBS_NOINTEGRALHEIGHT,                     // style
        0,                                          // x
        m_ToolbarHeight,                            // y
        m_Size.cx,                                  // width
        m_Size.cy - m_ToolbarHeight,                // height
        m_Win,                                      // parent
        0,                                          // control id
        g_hInst,                                    // hInstance
        NULL                                        // user defined data
        );

    if (m_hwndChild != NULL)
    {
        SetFont( FONT_FIXED );
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

LRESULT
CALLSWIN_DATA::OnCommand(
    WPARAM wParam,
    LPARAM lParam
    )
{
    if (HIWORD(wParam) == LBN_DBLCLK)
    {
        ULONG64 Offset;
        if (CodeExprAtCaret(NULL, &Offset) == S_OK)
        {
            UIC_DISPLAY_CODE_DATA* DispCode =
                StartStructCommand(UIC_DISPLAY_CODE);
            if (DispCode != NULL)
            {
                DispCode->Offset = Offset;
                FinishCommand();
            }
        }

        DEBUG_STACK_FRAME StkFrame;
        if (StackFrameAtCaret(&StkFrame) == S_OK)
        {
            UIC_SET_SCOPE_DATA* SetScope =
                StartStructCommand(UIC_SET_SCOPE);
            if (SetScope != NULL)
            {
                SetScope->StackFrame = StkFrame;
                FinishCommand();
            }
        }
        return 0;
    }

    if ((HWND)lParam == m_Toolbar)
    {
        OnContextMenuSelection(LOWORD(wParam) + CALLS_CONTEXT_ID_BASE);
        return 0;
    }
    
    return 0;
}

LRESULT
CALLSWIN_DATA::OnVKeyToItem(
    WPARAM wParam,
    LPARAM lParam
    )
{
    if (LOWORD(wParam) == VK_RETURN)
    {
        ULONG64 Offset;
        if (CodeExprAtCaret(NULL, &Offset) == S_OK)
        {
            UIC_DISPLAY_CODE_DATA* DispCode =
                StartStructCommand(UIC_DISPLAY_CODE);
            if (DispCode != NULL)
            {
                DispCode->Offset = Offset;
                FinishCommand();
            }
        }
        DEBUG_STACK_FRAME StkFrame;
        if (StackFrameAtCaret(&StkFrame) == S_OK)
        {
            UIC_SET_SCOPE_DATA* SetScope =
                StartStructCommand(UIC_SET_SCOPE);
            if (SetScope != NULL)
            {
                SetScope->StackFrame = StkFrame;
                FinishCommand();
            }
        }

    }
    else if (_T('G') == LOWORD(wParam))
    {
        ULONG64 Offset;
        if (CodeExprAtCaret(NULL, &Offset) == S_OK)
        {
            PrintStringCommand(UIC_EXECUTE, "g 0x%I64x", Offset);
        }
    }
    else if (_T('R') == LOWORD(wParam))
    {
        OnUpdate(UPDATE_BUFFER);
    }
    else
    {
        // Default behavior.
        return -1;
    }

    // Keystroke processed.
    return -2;
}

void
CALLSWIN_DATA::OnUpdate(
    UpdateType Type
    )
{
    if (Type != UPDATE_BUFFER)
    {
        return;
    }
    
    LRESULT lbItem;
    int     nFrameCount;
    HRESULT Status;

    lbItem = SendMessage( m_hwndChild, LB_GETCURSEL, 0, 0 );
    
    SendMessage( m_hwndChild, WM_SETREDRAW, FALSE, 0L );
    SendMessage( m_hwndChild, LB_RESETCONTENT, 0, 0 );

    Status = UiLockForRead();
    if (Status == S_OK)
    {
        PSTR Buf = (PSTR)m_Data + m_TextOffset;
        // Ignore final terminator.
        PSTR End = (PSTR)m_Data + m_DataUsed - 1;
        ULONG Width = 0;

        nFrameCount = 0;

        while (Buf < End)
        {
            PSTR Sep = strchr(Buf, '\n');
            if (!Sep)
            {
                // Shouldn't happen, but just in case.
                break;
            }
            
            ULONG Len = (ULONG)(Sep - Buf);
            ULONG StrWidth = Len * m_Font->Metrics.tmAveCharWidth;
            *Sep = 0;
            SendMessage(m_hwndChild, LB_ADDSTRING, 0, (LPARAM)Buf);
            Buf = Sep;
            *Buf++ = '\n';
            if (StrWidth > Width)
            {
                Width = StrWidth;
            }
            nFrameCount++;
        }

        SendMessage(m_hwndChild, LB_SETHORIZONTALEXTENT, Width, 0);
        UnlockStateBuffer(this);
    }
    else
    {
        SendLockStatusMessage(m_hwndChild, LB_ADDSTRING, Status);
        nFrameCount = 1;
    }

    SendMessage( m_hwndChild, LB_SETCURSEL,
                 (lbItem > nFrameCount) ? 0 : lbItem, 0 );
    SendMessage( m_hwndChild, WM_SETREDRAW, TRUE, 0L );
}

ULONG
CALLSWIN_DATA::GetWorkspaceSize(void)
{
    return SINGLE_CHILDWIN_DATA::GetWorkspaceSize() + 2 * sizeof(ULONG);
}

PUCHAR
CALLSWIN_DATA::SetWorkspace(PUCHAR Data)
{
    Data = SINGLE_CHILDWIN_DATA::SetWorkspace(Data);
    *(PULONG)Data = m_Flags;
    Data += sizeof(ULONG);
    *(PULONG)Data = m_Frames;
    Data += sizeof(ULONG);
    return Data;
}

PUCHAR
CALLSWIN_DATA::ApplyWorkspace1(PUCHAR Data, PUCHAR End)
{
    Data = SINGLE_CHILDWIN_DATA::ApplyWorkspace1(Data, End);

    if (End - Data >= 2 * sizeof(ULONG))
    {
        m_Flags = *(PULONG)Data;
        Data += sizeof(ULONG);
        m_Frames = *(PULONG)Data;
        Data += sizeof(ULONG);

        SyncUiWithFlags(0xffffffff);
        UiRequestRead();
    }

    return Data;
}

void
CALLSWIN_DATA::SyncUiWithFlags(ULONG Changed)
{
    ULONG i;

    //
    // Set toolbar button state from flags.
    //

    for (i = 0; i < NUM_CALLS_TB_BUTTONS; i++)
    {
        if (Changed & (1 << i))
        {
            SendMessage(m_Toolbar, TB_SETSTATE, g_CallsTbButtons[i].idCommand,
                        TBSTATE_ENABLED |
                        ((m_Flags & (1 << i)) ? TBSTATE_CHECKED : 0));
        }
    }
}

ULONG
CALLSWIN_DATA::SelectionToFrame(void)
{
    LRESULT CurSel = SendMessage(m_hwndChild, LB_GETCURSEL, 0, 0);
    // Check for no-selection.
    if (CurSel < 0)
    {
        return 0xffffffff;
    }
    
    ULONG Line = (ULONG)CurSel;
    
    // If there's a WARNING line ignore it.
    if (Line == m_WarningLine)
    {
        return 0xffffffff;
    }
    else if (Line > m_WarningLine)
    {
        Line--;
    }

    // If the column headers are on ignore the header line.
    if (m_Flags & DEBUG_STACK_COLUMN_NAMES)
    {
        if (Line == 0)
        {
            return 0xffffffff;
        }
        else
        {
            Line--;
        }
    }
    
    return Line;
}
