// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

#pragma once

#ifndef NEATPAD_USPTEXTLAYOUT_INCLUDED
#define NEATPAD_USPTEXTLAYOUT_INCLUDED

#include <windows.h>

#include "../TextLayout.h"
#include "UspLib/usplib.h"

//
//	UspTextLayout - a line laid out by Uniscribe, through UspLib's USPDATA
//
class UspTextLayout : public TextLayout
{
  public:
    // Lines are laid out for hwnd, in fonts from the list
    UspTextLayout(HWND hwnd, USPFONT *fonts);
    ~UspTextLayout() override;

    bool Analyze(const tchar_t *text, int length, TextRun *runs, int tabWidth) override;

    int Length() override;
    int Width() override;

    int  OffsetToX(int offset, bool trailing) override;
    void XToOffset(int x, int *offset, bool *trailing) override;
    int  SnapXToOffset(int x, int *snappedX) override;

    bool IsCursorStop(int offset) override;
    bool IsWordStop(int offset) override;
    bool IsWhiteSpace(int offset) override;

    void SetSelection(int start, int end) override;
    void SetSelectionColours(uint32_t fg, uint32_t bg) override;

    // Draw on a Win32Canvas
    void Draw(TextCanvas &canvas, int x, int y, int lineHeight, int adjustY, int clipLeft, int clipTop, int clipRight,
              int clipBottom) override;

  private:
    const SCRIPT_LOGATTR *LogAttr(int offset);

    HWND     m_hWnd;
    USPFONT *m_fonts;
    USPDATA *m_uspData;
};

#endif
