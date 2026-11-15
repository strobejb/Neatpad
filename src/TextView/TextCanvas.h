// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

#pragma once

#ifndef NEATPAD_TEXTCANVAS_INCLUDED
#define NEATPAD_TEXTCANVAS_INCLUDED

#include <stdint.h>

#include "../TextDocument/TextDocument.h" // tchar_t

//
//	TextCanvas - what the TextView engine draws on. The host makes one for
//	each paint, and the engine decides what to draw. Colours are 0x00BBGGRR,
//	and positions are pixels.
//
class TextCanvas
{
  public:
    virtual ~TextCanvas()
    {
    }

    // Draw one row of the view, at y down from its top. Until EndRow, drawing
    // is clipped to the row and y counts from the row's top.
    virtual void BeginRow(int y, int width, int height) = 0;
    virtual void EndRow()                               = 0;

    // A solid rectangle, and one checked in two colours pixel by pixel
    virtual void FillRect(int x, int y, int width, int height, uint32_t colour)             = 0;
    virtual void FillChecked(int x, int y, int width, int height, uint32_t fg, uint32_t bg) = 0;

    // Margin text, such as line numbers, in the view's main font: how wide it
    // is, and drawing it at x, y over a clip rectangle it fills with bg first
    virtual int  MarginTextWidth(const tchar_t *text, int length)           = 0;
    virtual void DrawMarginText(int x, int y, const tchar_t *text, int length, uint32_t fg, uint32_t bg, int clipLeft,
                                int clipTop, int clipRight, int clipBottom) = 0;

    // A margin image, from the images the view was given
    virtual void DrawMarginImage(int image, int x, int y, int width, int height) = 0;
};

#endif
