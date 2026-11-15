// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

#pragma once

#ifndef NEATPAD_WIN32CANVAS_INCLUDED
#define NEATPAD_WIN32CANVAS_INCLUDED

#include <windows.h>
#include <commctrl.h>

#include "../TextCanvas.h"

//
//	Win32Canvas - a TextCanvas over a device context. Each row is drawn into a
//	memory bitmap and copied across when it's done, so it never flickers.
//
class Win32Canvas : public TextCanvas
{
  public:
    // Draw into hdc, with margin text in marginFont and margin images from images
    Win32Canvas(HDC hdc, HFONT marginFont, HIMAGELIST images);
    ~Win32Canvas() override;

    void BeginRow(int y, int width, int height) override;
    void EndRow() override;

    void FillRect(int x, int y, int width, int height, uint32_t colour) override;
    void FillChecked(int x, int y, int width, int height, uint32_t fg, uint32_t bg) override;

    int  MarginTextWidth(const tchar_t *text, int length) override;
    void DrawMarginText(int x, int y, const tchar_t *text, int length, uint32_t fg, uint32_t bg, int clipLeft,
                        int clipTop, int clipRight, int clipBottom) override;

    void DrawMarginImage(int image, int x, int y, int width, int height) override;

    // The device context being drawn into now: the row's, during a row
    HDC Target() const
    {
        return m_target;
    }

  private:
    HDC        m_hdc;
    HDC        m_target;
    HFONT      m_marginFont;
    HIMAGELIST m_images;

    // the row being drawn
    HDC     m_rowDC;
    HBITMAP m_rowBitmap;
    HGDIOBJ m_rowPrevious;
    int     m_rowY;
    int     m_rowWidth;
    int     m_rowHeight;
};

#endif
