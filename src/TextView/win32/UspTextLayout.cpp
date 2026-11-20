// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		UspTextLayout.cpp
//
//	PURPOSE:	A line laid out by Uniscribe, through UspLib
//
#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <vector>

#include "UspTextLayout.h"
#include "TextViewWin32.h"
#include "Win32Canvas.h"

UspTextLayout::UspTextLayout(TextViewWin32 &host) : m_host(host), m_uspData(UspAllocate())
{
}

UspTextLayout::~UspTextLayout()
{
    UspFree(m_uspData);
}

bool UspTextLayout::Analyze(const tchar_t *text, int length, TextRun *runs, int tabWidth)
{
    HDC               hdc = GetDC(m_host.m_hWnd);
    std::vector<ATTR> attr(length > 0 ? length : 1);

    // UspLib's runs: the same, by another name
    for (int i = 0; i < length; i++)
    {
        attr[i].fg       = runs[i].fg;
        attr[i].bg       = runs[i].bg;
        attr[i].len      = runs[i].len;
        attr[i].font     = m_host.StyledFont(runs[i].font, runs[i].bold, runs[i].italic, hdc);
        attr[i].sel      = runs[i].sel;
        attr[i].ctrl     = runs[i].ctrl;
        attr[i].eol      = runs[i].eol;
        attr[i].reserved = runs[i].reserved;
    }

    //
    // setup the tabs + itemization states
    //
    int            tablist[]     = {tabWidth};
    SCRIPT_TABDEF  tabdef        = {1, 0, tablist, 0};
    SCRIPT_CONTROL scriptControl = {0};
    SCRIPT_STATE   scriptState   = {0};

    BOOL analyzed = UspAnalyze(m_uspData, hdc, (WCHAR *)text, length, attr.data(), 0, m_host.m_fonts, &scriptControl,
                               &scriptState, &tabdef);

    ReleaseDC(m_host.m_hWnd, hdc);

    return analyzed != FALSE;
}

int UspTextLayout::Length()
{
    return m_uspData->stringLen;
}

int UspTextLayout::Width()
{
    SIZE size = {0, 0};

    UspGetSize(m_uspData, &size);
    return size.cx;
}

int UspTextLayout::OffsetToX(int offset, bool trailing)
{
    int x = 0;

    UspOffsetToX(m_uspData, offset, trailing ? TRUE : FALSE, &x);
    return x;
}

void UspTextLayout::XToOffset(int x, int *offset, bool *trailing)
{
    int  charPos = 0;
    BOOL fTrail  = FALSE;

    UspXToOffset(m_uspData, x, &charPos, &fTrail, 0);

    *offset   = charPos;
    *trailing = fTrail != FALSE;
}

int UspTextLayout::SnapXToOffset(int x, int *snappedX)
{
    int charPos = 0;

    *snappedX = x;
    UspSnapXToOffset(m_uspData, x, snappedX, &charPos, 0);

    return charPos;
}

//
//	The logical attributes of a unit, or 0 at or past the end of the line, and
//	for an empty line, which Uniscribe gives none
//
const SCRIPT_LOGATTR *UspTextLayout::LogAttr(int offset)
{
    const SCRIPT_LOGATTR *logAttr = UspGetLogAttr(m_uspData);

    if (logAttr == 0 || offset < 0 || offset >= m_uspData->stringLen)
    {
        return 0;
    }

    return &logAttr[offset];
}

bool UspTextLayout::IsCursorStop(int offset)
{
    const SCRIPT_LOGATTR *logAttr = LogAttr(offset);

    return logAttr == 0 || logAttr->fCharStop;
}

bool UspTextLayout::IsWordStop(int offset)
{
    const SCRIPT_LOGATTR *logAttr = LogAttr(offset);

    return logAttr == 0 || logAttr->fWordStop;
}

bool UspTextLayout::IsWhiteSpace(int offset)
{
    const SCRIPT_LOGATTR *logAttr = LogAttr(offset);

    return logAttr != 0 && logAttr->fWhiteSpace;
}

void UspTextLayout::SetSelection(int start, int end)
{
    UspApplySelection(m_uspData, start, end);
}

void UspTextLayout::SetSelectionColours(uint32_t fg, uint32_t bg)
{
    UspSetSelColor(m_uspData, fg, bg);
}

void UspTextLayout::Draw(TextCanvas &canvas, int x, int y, int lineHeight, int adjustY, int clipLeft, int clipTop,
                         int clipRight, int clipBottom)
{
    RECT bounds = {clipLeft, clipTop, clipRight, clipBottom};
    HDC  hdc    = static_cast<Win32Canvas &>(canvas).Target();

    // UspTextOut clips to the selection as it draws, and leaves a clip region
    // behind when there wasn't one before, so start and finish with none
    SelectClipRgn(hdc, NULL);
    UspTextOut(m_uspData, hdc, x, y, lineHeight, adjustY, &bounds);
    SelectClipRgn(hdc, NULL);
}
