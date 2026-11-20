// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

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

// A TEXTVIEWFIND as the engine's TextViewFind, or 0 for none
static const TextViewFind *FindFrom(const TEXTVIEWFIND *from, TextViewFind *to)
{
    if (from == 0)
        return 0;

    to->text          = from->text;
    to->length        = from->length;
    to->backwards     = from->backwards != FALSE;
    to->wrap          = from->wrap != FALSE;
    to->matchcase     = from->matchcase != FALSE;
    to->replace       = from->replace;
    to->replacelength = from->replacelength;
    return to;
}

static BOOL DocStatsMessage(TextView *view, TEXTVIEWDOCSTATS *to)
{
    TextViewDocStats stats;

    if (to == 0 || !view->GetDocStats(&stats))
        return FALSE;

    to->byte_count       = stats.byte_count;
    to->char_count       = stats.char_count;
    to->char_count_known = stats.char_count_known;
    to->sel_byte_count   = stats.sel_byte_count;
    to->sel_char_count   = stats.sel_char_count;
    to->linebreaks_seen  = stats.linebreaks_seen;
    return TRUE;
}

//
//	Save or restore recovery state through a TEXTVIEWRECOVERY: its file name
//	goes to the engine and, after a restore, comes back
//
static BOOL RecoveryState(TextView *view, TEXTVIEWRECOVERY *state, bool save)
{
    TextViewRecovery recovery;
    size_t           length = 0;
    bool             done;

    if (state == 0 || state->directory == 0)
        return FALSE;

    while (length < MAX_PATH && state->filename[length])
        ++length;

    recovery.directory           = state->directory;
    recovery.filename.assign(state->filename, length);
    recovery.originalUnavailable = false;
    recovery.preserveSources     = state->preserveSources != FALSE;

    done = save ? view->SaveState(&recovery) : view->LoadState(&recovery);

    state->originalUnavailable = recovery.originalUnavailable;

    // a restored name always fits: recovery keeps no longer one
    if (done && !save)
    {
        for (size_t i = 0; i < recovery.filename.size(); ++i)
            state->filename[i] = recovery.filename[i];

        state->filename[recovery.filename.size()] = 0;
    }

    return done;
}

static std::string ResourceText(UINT id)
{
    HMODULE     module   = GetModuleHandle(0);
    HRSRC       resource = FindResource(module, MAKEINTRESOURCE(id), RT_RCDATA);
    DWORD       size     = resource ? SizeofResource(module, resource) : 0;
    HGLOBAL     data     = size ? LoadResource(module, resource) : 0;
    const char *text     = data ? static_cast<const char *>(LockResource(data)) : 0;

    return text ? std::string(text, size) : std::string();
}

static BOOL SetPaletteResources(TextView *view, const TEXTVIEWPALETTE *settings)
{
    if (!settings)
        return FALSE;

    std::vector<std::string> palettes;
    TextPalette              palette;

    palettes.push_back(ResourceText(settings->baseResource));
    if (settings->overlayResource)
        palettes.push_back(ResourceText(settings->overlayResource));

    for (const std::string &text : palettes)
    {
        if (PaletteName(text).empty())
            return FALSE;
    }

    ResolvePalette(palettes, settings->dark != FALSE, settings->accent, &palette);

    if (settings->legacyColours)
    {
        for (int i = 0; i < TXC_MAX_COLOURS; ++i)
            palette.colours[i] = settings->legacyColours[i];

        // Classic's emphasis uses the document's foreground, including a custom one.
        palette.syntax[(int)SyntaxKind::Strong].colour = palette.colours[TXC_FOREGROUND];
        palette.syntax[(int)SyntaxKind::Emphasis].colour = palette.colours[TXC_FOREGROUND];
    }

    view->SetPalette(palette);
    return TRUE;
}

void TextViewWin32::SetSyntaxResources(const UINT *ids, UINT count)
{
    m_syntaxDefinitions.clear();

    for (UINT i = 0; i < count; ++i)
    {
        std::string text = ResourceText(ids[i]);

        if (!text.empty())
            m_syntaxDefinitions.push_back(std::move(text));
    }

    m_pView->SetSyntax(0);
}

void TextViewWin32::ChooseSyntax(const WCHAR *filename)
{
    if (m_syntaxDefinitions.empty())
        return;

    if (filename == 0 || filename[0] == 0)
    {
        m_pView->SetSyntax(0);
        return;
    }

    int         length = WideCharToMultiByte(CP_UTF8, 0, filename, -1, 0, 0, 0, 0);
    std::string name(length, '\0');

    if (length && WideCharToMultiByte(CP_UTF8, 0, filename, -1, &name[0], length, 0, 0))
    {
        std::vector<const char *> definitions;

        for (const std::string &definition : m_syntaxDefinitions)
            definitions.push_back(definition.c_str());

        m_pView->ChooseSyntax(definitions.data(), (int)definitions.size(), name.c_str());
    }
    else
    {
        m_pView->SetSyntax(0);
    }
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

    // the Win32 EDIT control messages (win32/TextViewEdit.cpp)
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

    case TXM_SETSYNTAXRESOURCES:
        SetSyntaxResources((const UINT *)lParam, (UINT)wParam);
        return 0;

    case TXM_SETSYNTAXRESOURCE:
    {
        std::string definition = ResourceText((UINT)wParam);

        return !definition.empty() && view->SetSyntax(definition.c_str());
    }

    case TXM_SETPALETTERESOURCES:
        return SetPaletteResources(view, (const TEXTVIEWPALETTE *)lParam);

    case TXM_OPENFILE:
    case TXM_TAILFILE:
    case TXM_SAVEFILE:
    {
        const TCHAR *filename = (const TCHAR *)lParam;
        bool         done;

        if (msg == TXM_OPENFILE)
            done = view->OpenFile(filename, -1, false);
        else if (msg == TXM_TAILFILE)
            done = view->TailFile(filename);
        else
            done = view->SaveFile(filename, LOBYTE(LOWORD(wParam)), HIWORD(wParam), HIBYTE(LOWORD(wParam)));

        if (done)
            ChooseSyntax(filename);

        return done;
    }

    case TXM_SAVESTATE:
        return RecoveryState(view, (TEXTVIEWRECOVERY *)lParam, true);

    case TXM_LOADSTATE:
    {
        TEXTVIEWRECOVERY *state = (TEXTVIEWRECOVERY *)lParam;

        if (!RecoveryState(view, state, false))
            return FALSE;

        ChooseSyntax(state->filename);
        return TRUE;
    }

    case TXM_UPDATETAIL:
        return view->UpdateTail();

    case TXM_CLEAR:
        ChooseSyntax(0);
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
        return DocStatsMessage(view, (TEXTVIEWDOCSTATS *)lParam);

    case TXM_GETCURSORINFO:
    {
        TextViewCursorInfo info;

        if (lParam == 0)
        {
            return FALSE;
        }

        view->FillCursorInfo(&info);
        ToWin32(info, (TVNCURSORINFO *)lParam);
        return TRUE;
    }

    case TXM_INDEXFILE:
        return view->IndexFile((BOOL)wParam);

    // the line number comes in two halves
    case TXM_GOTOLINE:
    case TXM_CANGOTOLINE:
        return view->GotoLine((size_w)(DWORD)wParam | ((size_w)(DWORD)lParam << 32), msg == TXM_GOTOLINE);

    case TXM_GETINDEXINFO:
    {
        TextViewIndexInfo info;

        if (lParam == 0)
        {
            return FALSE;
        }

        view->FillIndexInfo(&info);
        ToWin32(info, (TVNINDEXINFO *)lParam);
        return TRUE;
    }

    case TXM_FINDTEXT:
    {
        TextViewFind find;

        return view->FindText(FindFrom((TEXTVIEWFIND *)lParam, &find));
    }

    case TXM_SHOWMATCHES:
    {
        TextViewFind find;

        return view->ShowMatches(FindFrom((TEXTVIEWFIND *)lParam, &find), (BOOL)wParam);
    }

    case TXM_REPLACETEXT:
    {
        TextViewFind find;

        return ResultFromSize(view->ReplaceText(FindFrom((TEXTVIEWFIND *)lParam, &find), (BOOL)wParam));
    }

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
