// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		TextViewWin32.cpp
//
//	PURPOSE:	The TextView engine's host on Win32: redraws, scrolling, the
//				caret, timers, the clipboard and notifications, done to the window
//
#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include "TextViewWin32.h"
#include "UspTextLayout.h"
#include "Win32Canvas.h"
#include "racursor.h"
#include "../TextViewInternal.h"

#ifndef SPI_GETCARETWIDTH
#define SPI_GETCARETWIDTH 0x2006
#endif

//
//	64-bit scrollbars (www.catch22.net/tuts/win32/64bit-scrollbars): a range that
//	fits a 32-bit scrollbar is used as it is, and a larger one is scaled down to
//	0..WIN16_SCROLLBAR_MAX by a whole-number divisor
//
#define WIN16_SCROLLBAR_MAX 0x7fff
#define WIN32_SCROLLBAR_MAX 0x7fffffff

//
//	Wrapper around SetScrollInfo, performs scaling to allow massive 64bit scroll
//	ranges. The page is in the same units as the range (bytes), so it is scaled too.
//
static BOOL SetScrollInfo64(HWND hwnd, int nBar, UINT fMask, uint64_t nMax64, uint64_t nPos64, uint64_t nPage64,
                            BOOL fRedraw)
{
    SCROLLINFO si = {sizeof(si), fMask};

    // normal scroll range requires no adjustment
    if (nMax64 <= WIN32_SCROLLBAR_MAX)
    {
        si.nMin  = 0;
        si.nMax  = (int)nMax64;
        si.nPage = (UINT)nPage64;
        si.nPos  = (int)nPos64;
    }
    // scale the scroll range down into allowed bounds
    else
    {
        uint64_t unit = nMax64 / WIN16_SCROLLBAR_MAX;

        si.nMin  = 0;
        si.nMax  = WIN16_SCROLLBAR_MAX;
        si.nPage = (UINT)max(nPage64 / unit, (uint64_t)1);
        si.nPos  = (int)min(nPos64 / unit, (uint64_t)WIN16_SCROLLBAR_MAX);
    }

    return SetScrollInfo(hwnd, nBar, &si, fRedraw);
}

//
//	Wrapper around GetScrollInfo, returns 64bit scrollbar position.
//	fMask must be either SIF_POS or SIF_TRACKPOS
//
static uint64_t GetScrollPos64(HWND hwnd, int nBar, UINT fMask, uint64_t nMax64)
{
    SCROLLINFO si = {sizeof(si), fMask | SIF_PAGE};
    uint64_t   nPos32;

    if (!GetScrollInfo(hwnd, nBar, &si))
    {
        return 0;
    }

    nPos32 = (fMask & SIF_TRACKPOS) ? si.nTrackPos : si.nPos;

    // normal scroll range requires no adjustment
    if (nMax64 <= WIN32_SCROLLBAR_MAX)
    {
        return nPos32;
    }

    // special-case: the thumb at the very end shows the end of the document
    if (nPos32 >= WIN16_SCROLLBAR_MAX - si.nPage + 1)
    {
        return nMax64;
    }

    // adjust the scroll position to be relative to maximum value
    return nPos32 * (nMax64 / WIN16_SCROLLBAR_MAX);
}

TextViewWin32::TextViewWin32(HWND hwnd)
    : m_hWnd(hwnd), m_pView(0), m_hTheme(OpenThemeData(hwnd, L"edit")), m_hUserMenu(0),
      m_hMarginCursor(CreateCursor(GetModuleHandle(0), 21, 5, 32, 32, XORMask, ANDMask)), m_nNumFonts(1),
      m_hImageList(0)
{
    memset(m_fonts, 0, sizeof(m_fonts));

    // the default font, which the view measures itself in as it starts
    SetFont(0, (HFONT)GetStockObject(ANSI_FIXED_FONT));

    m_pView = new TextView(this);
}

TextViewWin32::~TextViewWin32()
{
    delete m_pView;

    for (int i = 0; i < m_nNumFonts; i++)
    {
        UspFreeFont(&m_fonts[i]);
    }

    DestroyCursor(m_hMarginCursor);
    CloseThemeData(m_hTheme);
}

//
//	Painting procedure for TextView objects
//
LONG TextViewWin32::OnPaint()
{
    PAINTSTRUCT ps;

    BeginPaint(m_hWnd, &ps);

    Win32Canvas canvas(ps.hdc, MainFont(), m_hImageList);
    TextRect    rect = {(int)ps.rcPaint.left, (int)ps.rcPaint.top, (int)ps.rcPaint.right, (int)ps.rcPaint.bottom};

    m_pView->Paint(canvas, &rect);

    EndPaint(m_hWnd, &ps);

    return 0;
}

//
//	WM_PRINTCLIENT: draw the whole client area into hdc, as WM_PAINT draws it
//	when the whole window needs updating
//
LONG TextViewWin32::OnPrintClient(HDC hdc)
{
    RECT client;

    GetClientRect(m_hWnd, &client);

    Win32Canvas canvas(hdc, MainFont(), m_hImageList);
    TextRect    rect = {0, 0, (int)client.right, (int)client.bottom};

    m_pView->Paint(canvas, &rect);

    return 0;
}

//
//	Set one of the fonts
//
void TextViewWin32::SetFont(int idx, HFONT hFont)
{
    // need a DC to query font data
    HDC hdc = GetDC(m_hWnd);

    // Initialize the font for USPLIB
    UspFreeFont(&m_fonts[idx]);
    UspInitFont(&m_fonts[idx], hdc, hFont);

    ReleaseDC(m_hWnd, hdc);
}

//
//	Add a secondary font
//
LONG TextViewWin32::AddFont(HFONT hFont)
{
    int idx;

    if (m_nNumFonts >= MAX_FONTS)
    {
        return -1;
    }

    idx = m_nNumFonts++;
    SetFont(idx, hFont);
    m_pView->FontsChanged();

    return idx;
}

LONG TextViewWin32::AddFontFallback(int slot, HFONT hFont)
{
    HDC  hdc;
    LONG result;

    if (slot < 0 || slot >= m_nNumFonts)
    {
        return FALSE;
    }

    hdc    = GetDC(m_hWnd);
    result = UspAddFontFallback(&m_fonts[slot], hdc, hFont);
    ReleaseDC(m_hWnd, hdc);

    if (result)
    {
        m_pView->FontsChanged();
    }

    return result;
}

LONG TextViewWin32::ClearFontFallbacks(int slot)
{
    if (slot < 0 || slot >= m_nNumFonts)
    {
        return FALSE;
    }

    UspClearFontFallbacks(&m_fonts[slot]);
    m_pView->FontsChanged();

    return TRUE;
}

void TextViewWin32::Invalidate()
{
    ::InvalidateRect(m_hWnd, NULL, FALSE);
}

void TextViewWin32::InvalidateRect(int left, int top, int right, int bottom)
{
    RECT rect = {left, top, right, bottom};

    ::InvalidateRect(m_hWnd, &rect, FALSE);
}

void TextViewWin32::UpdateNow()
{
    UpdateWindow(m_hWnd);
}

void TextViewWin32::ScrollPixels(int dx, int dy, int clipLeft, int clipTop, int clipRight, int clipBottom)
{
    RECT clip = {clipLeft, clipTop, clipRight, clipBottom};

    ScrollWindowEx(m_hWnd, dx, dy, NULL, &clip, 0, 0, SW_INVALIDATE);
}

void TextViewWin32::SetVScroll(uint64_t max, uint64_t pos, uint64_t page)
{
    SetScrollInfo64(m_hWnd, SB_VERT, SIF_PAGE | SIF_POS | SIF_RANGE | SIF_DISABLENOSCROLL, max, pos, page, TRUE);
}

uint64_t TextViewWin32::VScrollTrackPos(uint64_t max)
{
    return GetScrollPos64(m_hWnd, SB_VERT, SIF_TRACKPOS, max);
}

void TextViewWin32::SetHScroll(int max, int pos, int page)
{
    SCROLLINFO si = {sizeof(si)};

    si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE | SIF_DISABLENOSCROLL;
    si.nPos  = pos;
    si.nPage = (UINT)page;
    si.nMin  = 0;
    si.nMax  = max;

    SetScrollInfo(m_hWnd, SB_HORZ, &si, TRUE);
}

int TextViewWin32::HScrollTrackPos()
{
    SCROLLINFO si = {sizeof(si), SIF_TRACKPOS};

    GetScrollInfo(m_hWnd, SB_HORZ, &si);
    return si.nTrackPos;
}

void TextViewWin32::CreateCaret(int width, int height)
{
    ::CreateCaret(m_hWnd, (HBITMAP)NULL, width, height);
}

void TextViewWin32::DestroyCaret()
{
    ::DestroyCaret();
}

void TextViewWin32::ShowCaret()
{
    ::ShowCaret(m_hWnd);
}

void TextViewWin32::HideCaret()
{
    ::HideCaret(m_hWnd);
}

void TextViewWin32::SetCaretPos(int x, int y)
{
    ::SetCaretPos(x, y);
}

bool TextViewWin32::StartTimer(unsigned id, unsigned milliseconds)
{
    return SetTimer(m_hWnd, id, milliseconds ? milliseconds : USER_TIMER_MINIMUM, 0) != 0;
}

void TextViewWin32::StopTimer(unsigned id)
{
    KillTimer(m_hWnd, id);
}

void TextViewWin32::MousePosition(int *x, int *y)
{
    POINT pt;

    GetCursorPos(&pt);
    ScreenToClient(m_hWnd, &pt);

    *x = pt.x;
    *y = pt.y;
}

void TextViewWin32::CaptureMouse()
{
    SetCapture(m_hWnd);
}

void TextViewWin32::ReleaseMouse()
{
    ReleaseCapture();
}

void TextViewWin32::SetCursor(Cursor cursor)
{
    ::SetCursor(cursor == CursorMargin ? m_hMarginCursor : LoadCursor(0, IDC_IBEAM));
}

void TextViewWin32::ClientSize(int *width, int *height)
{
    RECT rect;

    GetClientRect(m_hWnd, &rect);

    *width  = rect.right;
    *height = rect.bottom;
}

bool TextViewWin32::HasFocus()
{
    return GetFocus() == m_hWnd;
}

//
//	Whether the view or its top-level window has the focus, so the selection
//	shows in its active colours
//
bool TextViewWin32::IsActive()
{
    if (!IsWindowEnabled(m_hWnd) || !IsWindowEnabled(GetAncestor(m_hWnd, GA_ROOT)))
    {
        return false;
    }

    HWND hwndFocus = GetFocus();

    if (hwndFocus == m_hWnd || IsChild(m_hWnd, hwndFocus))
    {
        return true;
    }

    HWND hwndRoot       = GetAncestor(m_hWnd, GA_ROOT);
    HWND hwndForeground = GetForegroundWindow();

    return hwndForeground == hwndRoot || GetAncestor(hwndForeground, GA_ROOT) == hwndRoot;
}

int TextViewWin32::CaretWidth()
{
    DWORD width = 0;

    SystemParametersInfo(SPI_GETCARETWIDTH, 0, &width, 0);
    return (int)width;
}

int TextViewWin32::WheelScrollLines()
{
    UINT lines = 0;

    SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    return (int)lines;
}

int TextViewWin32::FontCount()
{
    return m_nNumFonts;
}

void TextViewWin32::GetFontMetrics(int font, FontMetrics *metrics)
{
    // always include a font's external-leading
    metrics->height       = m_fonts[font].tm.tmHeight + m_fonts[font].tm.tmExternalLeading;
    metrics->ascent       = m_fonts[font].tm.tmAscent;
    metrics->averageWidth = m_fonts[font].tm.tmAveCharWidth;
}

int TextViewWin32::TextWidth(const tchar_t *text, int length)
{
    HDC     hdc      = GetDC(m_hWnd);
    HGDIOBJ hOldFont = SelectObject(hdc, MainFont());
    SIZE    sz       = {0, 0};

    GetTextExtentPoint32W(hdc, text, length, &sz);

    SelectObject(hdc, hOldFont);
    ReleaseDC(m_hWnd, hdc);

    return sz.cx;
}

bool TextViewWin32::MarginImageSize(int *width, int *height)
{
    *width  = 0;
    *height = 0;

    return m_hImageList && ImageList_GetIconSize(m_hImageList, width, height);
}

unsigned TextViewWin32::Modifiers()
{
    return (GetKeyState(VK_SHIFT) < 0 ? TVMOD_SHIFT : 0) | (GetKeyState(VK_CONTROL) < 0 ? TVMOD_CTRL : 0) |
           (GetKeyState(VK_MENU) < 0 ? TVMOD_ALT : 0);
}

uint32_t TextViewWin32::SystemColour(int index)
{
    return GetSysColor(index);
}

TextLayout *TextViewWin32::CreateLayout()
{
    return new UspTextLayout(m_hWnd, m_fonts);
}

bool TextViewWin32::ReadClipboard(const std::function<void(const tchar_t *text, size_t length)> &use)
{
    bool success = false;

    if (OpenClipboard(m_hWnd))
    {
        HANDLE   hMem   = GetClipboardData(CF_UNICODETEXT);
        tchar_t *szText = (tchar_t *)GlobalLock(hMem);

        if (szText)
        {
            use(szText, (size_t)lstrlenW(szText));
            GlobalUnlock(hMem);

            success = true;
        }

        CloseClipboard();
    }

    return success;
}

bool TextViewWin32::WriteClipboard(size_t length, const std::function<size_t(tchar_t *buffer)> &fill)
{
    bool success = false;

    if (OpenClipboard(m_hWnd))
    {
        HANDLE   hMem;
        tchar_t *ptr;

        if ((hMem = GlobalAlloc(GPTR, (length + 1) * sizeof(tchar_t))) != 0)
        {
            if ((ptr = (tchar_t *)GlobalLock(hMem)) != 0)
            {
                EmptyClipboard();

                ptr[fill(ptr)] = 0;

                SetClipboardData(CF_UNICODETEXT, hMem);
                success = true;

                GlobalUnlock(hMem);
            }
        }

        CloseClipboard();
    }

    return success;
}

//
//	The engine's notification as its TVN_* one, with its structure
//
intptr_t TextViewWin32::Notify(unsigned code, void *info)
{
    UINT          nCtrlId = GetWindowLong(m_hWnd, GWL_ID);
    TVNCURSORINFO cursor  = {};
    TVNINDEXINFO  index   = {};
    NMHDR         nmhdr   = {};
    NMHDR        *nmptr   = &nmhdr;

    if (code == TVNOTIFY_CURSOR_CHANGE && info)
    {
        ToWin32(*(TextViewCursorInfo *)info, &cursor);
        nmptr = &cursor.hdr;
    }
    else if (code == TVNOTIFY_INDEX_CHANGE && info)
    {
        ToWin32(*(TextViewIndexInfo *)info, &index);
        nmptr = &index.hdr;
    }

    nmptr->hwndFrom = m_hWnd;
    nmptr->idFrom   = nCtrlId;
    nmptr->code     = TVN_BASE + code;

    return SendMessage(GetParent(m_hWnd), WM_NOTIFY, (WPARAM)nCtrlId, (LPARAM)nmptr);
}

static COLORREF MixRGB(COLORREF rgbCol1, COLORREF rgbCol2)
{
    return RGB((GetRValue(rgbCol1) + GetRValue(rgbCol2)) / 2, (GetGValue(rgbCol1) + GetGValue(rgbCol2)) / 2,
               (GetBValue(rgbCol1) + GetBValue(rgbCol2)) / 2);
}

//
//	A SYSCOL colour made real, for the application (TextView.h)
//
extern "C" COLORREF RealizeColour(COLORREF col)
{
    COLORREF result = col;

    if (col & 0x80000000)
        result = GetSysColor(col & 0xff);

    if (col & 0x40000000)
        result = MixRGB(GetSysColor((col & 0xff00) >> 8), result);

    if (col & 0x20000000)
        result = MixRGB(GetSysColor((col & 0xff00) >> 8), result);

    return result;
}
