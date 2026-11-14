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
#include "../TextViewInternal.h"

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
    : m_hWnd(hwnd), m_pView(0), m_hTheme(OpenThemeData(hwnd, L"edit")), m_hUserMenu(0)
{
    m_pView = new TextView(hwnd, this);
}

TextViewWin32::~TextViewWin32()
{
    delete m_pView;
    CloseThemeData(m_hTheme);
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

intptr_t TextViewWin32::Notify(unsigned code, void *info)
{
    UINT   nCtrlId = GetWindowLong(m_hWnd, GWL_ID);
    NMHDR  nmhdr   = {m_hWnd, nCtrlId, code};
    NMHDR *nmptr   = &nmhdr;

    if (info)
    {
        nmptr  = (NMHDR *)info;
        *nmptr = nmhdr;
    }

    return SendMessage(GetParent(m_hWnd), WM_NOTIFY, (WPARAM)nCtrlId, (LPARAM)nmptr);
}
