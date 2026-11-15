// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		Win32Canvas.cpp
//
//	PURPOSE:	A TextCanvas over a device context
//
#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>

#include "Win32Canvas.h"

Win32Canvas::Win32Canvas(HDC hdc, HFONT marginFont, HIMAGELIST images)
    : m_hdc(hdc), m_target(hdc), m_marginFont(marginFont), m_images(images), m_rowDC(0), m_rowBitmap(0),
      m_rowPrevious(0), m_rowY(0), m_rowWidth(0), m_rowHeight(0)
{
}

Win32Canvas::~Win32Canvas()
{
    if (m_rowDC)
    {
        SelectObject(m_rowDC, m_rowPrevious);
        DeleteDC(m_rowDC);
        DeleteObject(m_rowBitmap);
    }
}

void Win32Canvas::BeginRow(int y, int width, int height)
{
    // one bitmap the size of a row, for every row
    if (m_rowDC && (width != m_rowWidth || height != m_rowHeight))
    {
        SelectObject(m_rowDC, m_rowPrevious);
        DeleteDC(m_rowDC);
        DeleteObject(m_rowBitmap);

        m_rowDC = 0;
    }

    if (m_rowDC == 0)
    {
        m_rowDC       = CreateCompatibleDC(m_hdc);
        m_rowBitmap   = CreateCompatibleBitmap(m_hdc, width, height);
        m_rowPrevious = SelectObject(m_rowDC, m_rowBitmap);
        m_rowWidth    = width;
        m_rowHeight   = height;
    }

    m_rowY   = y;
    m_target = m_rowDC;
}

void Win32Canvas::EndRow()
{
    // transfer to screen
    BitBlt(m_hdc, 0, m_rowY, m_rowWidth, m_rowHeight, m_rowDC, 0, 0, SRCCOPY);
    m_target = m_hdc;
}

void Win32Canvas::FillRect(int x, int y, int width, int height, uint32_t colour)
{
    RECT     rect = {x, y, x + width, y + height};
    COLORREF old  = SetBkColor(m_target, colour);

    ExtTextOut(m_target, 0, 0, ETO_OPAQUE, &rect, 0, 0, 0);
    SetBkColor(m_target, old);
}

//
//	Paint a checkered rectangle, with each alternate
//	pixel being assigned a different colour
//
void Win32Canvas::FillChecked(int x, int y, int width, int height, uint32_t fg, uint32_t bg)
{
    static WORD wCheckPat[8] = {0xaaaa, 0x5555, 0xaaaa, 0x5555, 0xaaaa, 0x5555, 0xaaaa, 0x5555};

    HBITMAP  hbmp;
    HBRUSH   hbr, hbrold;
    COLORREF fgold, bgold;

    hbmp = CreateBitmap(8, 8, 1, 1, wCheckPat);
    hbr  = CreatePatternBrush(hbmp);

    SetBrushOrgEx(m_target, x, 0, 0);
    hbrold = (HBRUSH)SelectObject(m_target, hbr);

    fgold = SetTextColor(m_target, fg);
    bgold = SetBkColor(m_target, bg);

    PatBlt(m_target, x, y, width, height, PATCOPY);

    SetBkColor(m_target, bgold);
    SetTextColor(m_target, fgold);

    SelectObject(m_target, hbrold);
    DeleteObject(hbr);
    DeleteObject(hbmp);
}

int Win32Canvas::MarginTextWidth(const tchar_t *text, int length)
{
    HGDIOBJ old = SelectObject(m_target, m_marginFont);
    SIZE    sz  = {0, 0};

    GetTextExtentPoint32W(m_target, text, length, &sz);
    SelectObject(m_target, old);

    return sz.cx;
}

void Win32Canvas::DrawMarginText(int x, int y, const tchar_t *text, int length, uint32_t fg, uint32_t bg, int clipLeft,
                                 int clipTop, int clipRight, int clipBottom)
{
    RECT    rect = {clipLeft, clipTop, clipRight, clipBottom};
    HGDIOBJ old  = SelectObject(m_target, m_marginFont);

    SetTextColor(m_target, fg);
    SetBkColor(m_target, bg);

    ExtTextOutW(m_target, x, y, ETO_OPAQUE | ETO_CLIPPED, &rect, text, length, 0);

    SelectObject(m_target, old);
}

void Win32Canvas::DrawMarginImage(int image, int x, int y, int width, int height)
{
    ImageList_DrawEx(m_images, image, m_target, x, y, width, height, CLR_NONE, CLR_NONE, ILD_TRANSPARENT);
}
