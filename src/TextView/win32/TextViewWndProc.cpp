//
//	MODULE:		TextViewWndProc.cpp
//
//	PURPOSE:	The TextView window: its class, its window procedure, and the
//				messages that turn into calls to the engine
//
//	NOTES:		www.catch22.net
//
#define _WIN32_WINNT 0x501
#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <tchar.h>
#include <uxtheme.h>
#include <vssym32.h>

// for the EM_xxx message constants
#include <richedit.h>

#include "TextViewWin32.h"
#include "../TextView.h"
#include "../TextViewInternal.h"

#if !defined(UNICODE)
#error "Please build as Unicode only!"
#endif

#pragma comment(lib, "comctl32.lib")

//
//	A 64-bit value as a message result: exact wherever it fits (always on x64),
//	otherwise the largest positive result, so it never reads as 0 or -1
//
static LRESULT ResultFromSize(size_w value)
{
    return value > (size_w)MAXLONG_PTR ? MAXLONG_PTR : (LRESULT)value;
}

//
//	The window's own messages, and the TXM_* and EM_* messages of its API
//
LRESULT TextViewWin32::WndProc(UINT msg, WPARAM wParam, LPARAM lParam)
{
    TextView *view = m_pView;

    switch (msg)
    {
    // Draw contents of TextView whenever window needs updating
    case WM_ERASEBKGND:
        return 1;

    // Need to custom-draw the border for XP/Vista themes
    case WM_NCPAINT:
        return OnNcPaint((HRGN)wParam);

    case WM_PAINT:
        return OnPaint();

    case WM_PRINTCLIENT:
        return OnPrintClient((HDC)wParam);

    // Set a new font: the default font is always #0
    case WM_SETFONT:
        SetFont(0, (HFONT)wParam);
        view->FontsChanged();
        return 0;

    case WM_SIZE:
        return view->OnSize((UINT)wParam, LOWORD(lParam), HIWORD(lParam));

    case WM_VSCROLL:
        return view->OnVScroll(LOWORD(wParam), HIWORD(wParam));

    case WM_HSCROLL:
        return view->OnHScroll(LOWORD(wParam), HIWORD(wParam));

    // Grab the keyboard focus on a click
    case WM_MOUSEACTIVATE:
        SetFocus(m_hWnd);
        return MA_ACTIVATE;

    case WM_CONTEXTMENU:
        return OnContextMenu((HWND)wParam, (short)LOWORD(lParam), (short)HIWORD(lParam));

    // Ctrl+wheel is left to DefWindowProc, which passes it to the parent to zoom
    case WM_MOUSEWHEEL:
        if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL)
        {
            break;
        }

        return view->OnMouseWheel((short)HIWORD(wParam));

    case WM_SETFOCUS:
        return view->OnSetFocus();

    case WM_KILLFOCUS:
        return view->OnKillFocus();

    // make sure we get arrow-keys, enter, tab, etc when hosted inside a dialog
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS;

    case WM_LBUTTONDOWN:
        return view->OnLButtonDown((UINT)wParam, (short)LOWORD(lParam), (short)HIWORD(lParam));

    case WM_LBUTTONUP:
        return view->OnLButtonUp((UINT)wParam, (short)LOWORD(lParam), (short)HIWORD(lParam));

    case WM_LBUTTONDBLCLK:
        return view->OnLButtonDblClick((UINT)wParam, (short)LOWORD(lParam), (short)HIWORD(lParam));

    case WM_MOUSEMOVE:
        return view->OnMouseMove((UINT)wParam, (short)LOWORD(lParam), (short)HIWORD(lParam));

    case WM_KEYDOWN:
        return view->OnKeyDown((UINT)wParam, (UINT)lParam);

    case WM_UNDO:
    case TXM_UNDO:
    case EM_UNDO:
        return view->Undo();

    case TXM_REDO:
    case EM_REDO:
        return view->Redo();

    case TXM_CANUNDO:
    case EM_CANUNDO:
        return view->CanUndo();

    case TXM_CANREDO:
    case EM_CANREDO:
        return view->CanRedo();

    // whether the text differs from its file, as an edit control reports it
    case EM_GETMODIFY:
        return view->FileDoc()->modified();

    case WM_CHAR:
        return view->OnChar((UINT)wParam, (UINT)lParam);

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            return TRUE;
        }

        break;

    case WM_COPY:
        return view->OnCopy();

    case WM_CUT:
        return view->OnCut();

    case WM_PASTE:
        return view->OnPaste();

    case WM_CLEAR:
        return view->OnClear();

    // the Win32 EDIT control messages (TextViewEdit.cpp)
    case WM_GETTEXT:
    case WM_GETTEXTLENGTH:
    case EM_GETLINE:
    case EM_GETLINECOUNT:
    case EM_GETFIRSTVISIBLELINE:
    case EM_GETSEL:
    case EM_SETSEL:
    case EM_LINEFROMCHAR:
    case EM_LINEINDEX:
    case EM_LINELENGTH:
    case EM_POSFROMCHAR:
    case EM_CHARFROMPOS:
    case EM_REPLACESEL:
    case EM_SCROLLCARET:
    case EM_LINESCROLL:
        return view->EditMessage(msg, wParam, lParam);

    case WM_TIMER:
        return view->OnTimer((UINT_PTR)wParam);

    //
    case TXM_OPENFILE:
        return view->OpenFile((TCHAR *)lParam, -1, false);

    case TXM_SAVESTATE:
        return view->SaveState((TEXTVIEWRECOVERY *)lParam);

    case TXM_LOADSTATE:
        return view->LoadState((TEXTVIEWRECOVERY *)lParam);

    case TXM_TAILFILE:
        return view->TailFile((TCHAR *)lParam);

    case TXM_UPDATETAIL:
        return view->UpdateTail();

    case TXM_SAVEFILE:
        return view->SaveFile((TCHAR *)lParam, LOBYTE(LOWORD(wParam)), HIWORD(wParam), HIBYTE(LOWORD(wParam)));

    case TXM_CLEAR:
        return view->ClearFile();

    case TXM_SETLINESPACING:
        return view->SetLineSpacing((int)wParam, (int)lParam);

    case TXM_ADDFONT:
        return AddFont((HFONT)wParam);

    case TXM_ADDFONTFALLBACK:
        return AddFontFallback((int)wParam, (HFONT)lParam);

    case TXM_CLEARFONTFALLBACKS:
        return ClearFontFallbacks((int)wParam);

    case TXM_SETCOLOR:
        return view->SetColour((UINT)wParam, (COLORREF)lParam);

    case TXM_SETSTYLE:
        return view->SetStyle((ULONG)wParam, (ULONG)lParam);

    case TXM_SETCARETWIDTH:
        return view->SetCaretWidth((int)wParam);

    case TXM_SETIMAGELIST:
        m_hImageList = (HIMAGELIST)wParam;
        return TRUE;

    case TXM_SETLONGLINE:
        return view->SetLongLine((int)lParam);

    case TXM_SETLINEIMAGE:
        return view->SetLineImage((ULONG)wParam, (ULONG)lParam);

    case TXM_GETFORMAT:
        return view->FileDoc()->getformat();

    case TXM_GETBOM:
        return view->FileDoc()->has_bom();

    case TXM_GETLINEFORMAT:
        return view->LineFormat();

    case TXM_SETTABWIDTH:
        return view->SetTabWidth((UINT)wParam);

    case TXM_SETFILTERCONTEXT:
        return view->SetFilterContext((UINT)wParam);

    case TXM_SETINMEMORYLIMIT:
        view->FileDoc()->set_in_memory_limit(wParam);
        return 0;

    case TXM_CONVERTLINEBREAKS:
        return view->ConvertLineBreaks((UINT)wParam);

    case TXM_GETSELSIZE:
        return ResultFromSize(view->SelectionSize());

    case TXM_SETSELALL:
        return view->SelectAll();

    // -1 while the caret is past the indexed part of a UTF-8 or UTF-32 document
    case TXM_GETCURPOS:
    {
        size_w offset_chars;

        return view->CursorOffset(&offset_chars) == TDR_OK ? ResultFromSize(offset_chars) : -1;
    }

    case TXM_GETCURLINE:
    {
        size_w lineno = 0;

        view->CursorLine(&lineno);
        return ResultFromSize(lineno);
    }

    case TXM_GETCURCOL:
        return ResultFromSize(view->CursorColumn());

    // filtered, the view is read-only, but the document may not be
    case TXM_GETEDITMODE:
        return view->GetEditMode();

    case TXM_GETCURLINEKNOWN:
    {
        size_w lineno;

        return view->CursorLine(&lineno) == TDR_OK;
    }

    case TXM_GETDOCSTATS:
        return view->GetDocStats((TEXTVIEWDOCSTATS *)lParam);

    case TXM_GETCURSORINFO:
        if (lParam == 0)
        {
            return FALSE;
        }

        view->FillCursorInfo((TVNCURSORINFO *)lParam);
        return TRUE;

    case TXM_INDEXFILE:
        return view->IndexFile((BOOL)wParam);

    // the line number comes in two halves
    case TXM_GOTOLINE:
    case TXM_CANGOTOLINE:
        return view->GotoLine((size_w)(DWORD)wParam | ((size_w)(DWORD)lParam << 32), msg == TXM_GOTOLINE);

    case TXM_GETINDEXINFO:
        if (lParam == 0)
        {
            return FALSE;
        }

        view->FillIndexInfo((TVNINDEXINFO *)lParam);
        return TRUE;

    case TXM_FINDTEXT:
        return view->FindText((TEXTVIEWFIND *)lParam);

    case TXM_SHOWMATCHES:
        return view->ShowMatches((TEXTVIEWFIND *)lParam, (BOOL)wParam);

    case TXM_REPLACETEXT:
        return ResultFromSize(view->ReplaceText((TEXTVIEWFIND *)lParam, (BOOL)wParam));

    case TXM_SETEDITMODE:
        return view->SetEditMode((UINT)wParam);

    case TXM_SETCONTEXTMENU:
        m_hUserMenu = (HMENU)wParam;
        return 0;

    default:
        break;
    }

    return DefWindowProc(m_hWnd, msg, wParam, lParam);
}

//
//	Need to custom-draw the non-client area when using XP/Vista themes,
//	otherwise the border looks old-style
//
LONG TextViewWin32::OnNcPaint(HRGN hrgnUpdate)
{
    HRGN hrgnClip = hrgnUpdate;

    if (m_hTheme != 0)
    {
        HDC   hdc = GetWindowDC(m_hWnd);
        RECT  rc;
        RECT  rcWindow;
        DWORD state = ETS_NORMAL;

        if (!IsWindowEnabled(m_hWnd))
        {
            state = ETS_DISABLED;
        }
        else if (GetFocus() == m_hWnd)
        {
            state = ETS_HOT;
        }
        else
        {
            state = ETS_NORMAL;
        }

        GetWindowRect(m_hWnd, &rcWindow);
        GetClientRect(m_hWnd, &rc);
        ClientToScreen(m_hWnd, (POINT *)&rc.left);
        ClientToScreen(m_hWnd, (POINT *)&rc.right);
        rc.right  = rcWindow.right - (rc.left - rcWindow.left);
        rc.bottom = rcWindow.bottom - (rc.top - rcWindow.top);

        hrgnClip = CreateRectRgn(rc.left, rc.top, rc.right, rc.bottom);

        if (hrgnUpdate != (HRGN)1)
        {
            CombineRgn(hrgnClip, hrgnClip, hrgnUpdate, RGN_AND);
        }

        OffsetRect(&rc, -rcWindow.left, -rcWindow.top);

        ExcludeClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);
        OffsetRect(&rcWindow, -rcWindow.left, -rcWindow.top);

        DrawThemeBackground(m_hTheme, hdc, 6, state, &rcWindow, NULL);

        ReleaseDC(m_hWnd, hdc);
    }

    return (LONG)DefWindowProc(m_hWnd, WM_NCPAINT, (WPARAM)hrgnClip, 0);
}

//
//	The view's own context menu: what it can do now
//
HMENU TextViewWin32::CreateContextMenu()
{
    HMENU hMenu = CreatePopupMenu();

    // do we have a selection?
    UINT fSelection = m_pView->SelectionSize() > 0 ? MF_ENABLED : MF_DISABLED | MF_GRAYED;

    // is there text on the clipboard?
    UINT fClipboard = (IsClipboardFormatAvailable(CF_TEXT) || IsClipboardFormatAvailable(CF_UNICODETEXT))
                          ? MF_ENABLED
                          : MF_GRAYED | MF_DISABLED;

    UINT fCanUndo = m_pView->CanUndo() ? MF_ENABLED : MF_GRAYED | MF_DISABLED;
    UINT fCanRedo = m_pView->CanRedo() ? MF_ENABLED : MF_GRAYED | MF_DISABLED;

    AppendMenu(hMenu, MF_STRING | fCanUndo, WM_UNDO, _T("&Undo"));
    AppendMenu(hMenu, MF_STRING | fCanRedo, TXM_REDO, _T("&Redo"));
    AppendMenu(hMenu, MF_SEPARATOR, 0, 0);
    AppendMenu(hMenu, MF_STRING | fSelection, WM_CUT, _T("Cu&t"));
    AppendMenu(hMenu, MF_STRING | fSelection, WM_COPY, _T("&Copy"));
    AppendMenu(hMenu, MF_STRING | fClipboard, WM_PASTE, _T("&Paste"));
    AppendMenu(hMenu, MF_STRING | fSelection, WM_CLEAR, _T("&Delete"));
    AppendMenu(hMenu, MF_SEPARATOR, 0, 0);
    AppendMenu(hMenu, MF_STRING | MF_ENABLED, TXM_SETSELALL, _T("&Select All"));
    AppendMenu(hMenu, MF_SEPARATOR, 0, 0);
    AppendMenu(hMenu, MF_STRING | MF_ENABLED, WM_USER + 7, _T("&Right to left Reading order"));
    AppendMenu(hMenu, MF_STRING | MF_ENABLED, WM_USER + 8, _T("&Show Unicode control characters"));

    return hMenu;
}

//
//	WM_CONTEXTMENU
//
//	Respond to right-click message
//
LONG TextViewWin32::OnContextMenu(HWND hwndParam, int x, int y)
{
    if (m_hUserMenu == 0)
    {
        HMENU hMenu = CreateContextMenu();
        UINT  uCmd  = TrackPopupMenu(hMenu, TPM_RETURNCMD, x, y, 0, m_hWnd, 0);

        if (uCmd != 0)
        {
            PostMessage(m_hWnd, uCmd, 0, 0);
        }

        return 0;
    }
    else
    {
        UINT uCmd = TrackPopupMenu(m_hUserMenu, TPM_RETURNCMD, x, y, 0, m_hWnd, 0);

        if (uCmd != 0)
        {
            PostMessage(GetParent(m_hWnd), WM_COMMAND, MAKEWPARAM(uCmd, 0), (LPARAM)GetParent(m_hWnd));
        }

        return 0;
    }
}

//
//	Win32 TextView window procedure stub
//
static LRESULT WINAPI TextViewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    TextViewWin32 *ptv = (TextViewWin32 *)GetWindowLongPtr(hwnd, 0);

    switch (msg)
    {
    // First message received by any window - make a new TextView object
    // and store pointer to it in our extra-window-bytes
    case WM_NCCREATE:

        if ((ptv = new TextViewWin32(hwnd)) == 0)
        {
            return FALSE;
        }

        SetWindowLongPtr(hwnd, 0, (LONG_PTR)ptv);
        return TRUE;

    // Last message received by any window - delete the TextView object
    case WM_NCDESTROY:
        delete ptv;
        SetWindowLongPtr(hwnd, 0, 0);
        return 0;

    // Pass everything to the TextView window procedure
    default:
        if (ptv)
        {
            return ptv->WndProc(msg, wParam, lParam);
        }
        else
        {
            return 0;
        }
    }
}

//
//	Register the TextView window class
//
BOOL InitTextView()
{
    WNDCLASSEX wcx;

    // Window class for the main application parent window
    wcx.cbSize        = sizeof(wcx);
    wcx.style         = CS_DBLCLKS;
    wcx.lpfnWndProc   = TextViewWndProc;
    wcx.cbClsExtra    = 0;
    wcx.cbWndExtra    = sizeof(TextViewWin32 *);
    wcx.hInstance     = GetModuleHandle(0);
    wcx.hIcon         = 0;
    wcx.hCursor       = LoadCursor(NULL, IDC_IBEAM);
    wcx.hbrBackground = (HBRUSH)0; // NO FLICKERING FOR US!!
    wcx.lpszMenuName  = 0;
    wcx.lpszClassName = TEXTVIEW_CLASS;
    wcx.hIconSm       = 0;

    return RegisterClassEx(&wcx) ? TRUE : FALSE;
}

//
//	Create a TextView control!
//
HWND CreateTextView(HWND hwndParent)
{
    return CreateWindowEx(WS_EX_CLIENTEDGE, TEXTVIEW_CLASS, _T(""), WS_VSCROLL | WS_HSCROLL | WS_CHILD | WS_VISIBLE, 0,
                          0, 0, 0, hwndParent, 0, GetModuleHandle(0), 0);
}
