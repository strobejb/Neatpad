#pragma once

#ifndef NEATPAD_TEXTLAYOUT_INCLUDED
#define NEATPAD_TEXTLAYOUT_INCLUDED

#include <stdint.h>

#include "../TextDocument/TextDocument.h" // tchar_t

class TextCanvas;

//
//	TextRun - how one unit of a line is drawn. A line has one per unit.
//	Colours are 0x00BBGGRR.
//
struct TextRun
{
    uint32_t fg;
    uint32_t bg;
    int      len : 16;     // units in this run
    int      font : 7;     // the host's font, by index
    int      sel : 1;      // selected
    int      ctrl : 1;     // shown as an isolated control character
    int      eol : 1;      // part of the line break: the caret can't stop inside it
    int      reserved : 6; // must be 0
};

//
//	TextLayout - one line of text laid out, in the build's own text units.
//	The engine asks it where things are across the line, and where the caret
//	can stop. Offsets count units from the start of the line, and x is in
//	pixels from its start.
//
class TextLayout
{
  public:
    virtual ~TextLayout()
    {
    }

    // Lay out length units of text, with a run for each unit. Tab stops are
    // tabWidth columns apart.
    virtual bool Analyze(const tchar_t *text, int length, TextRun *runs, int tabWidth) = 0;

    // The units laid out, and how wide they are
    virtual int Length() = 0;
    virtual int Width()  = 0;

    // The x of an offset's leading edge, or its trailing edge
    virtual int OffsetToX(int offset, bool trailing) = 0;

    // The unit at x, and whether x is in its trailing half
    virtual void XToOffset(int x, int *offset, bool *trailing) = 0;

    // The cursor stop nearest x, and its own x
    virtual int SnapXToOffset(int x, int *snappedX) = 0;

    // Where the caret can stop, where words start, and which units are white
    // space. The end of the line is a cursor stop and a word stop. None of
    // them is ever true part way through a character, such as the second
    // half of a surrogate pair or the second byte of a UTF-8 sequence: the
    // engine steps a unit at a time and stops where these say.
    virtual bool IsCursorStop(int offset) = 0;
    virtual bool IsWordStop(int offset)   = 0;
    virtual bool IsWhiteSpace(int offset) = 0;

    // The selection, in units from start to end, and its colours. It's drawn
    // when the layout is.
    virtual void SetSelection(int start, int end)              = 0;
    virtual void SetSelectionColours(uint32_t fg, uint32_t bg) = 0;

    // Draw the line on a canvas the same platform's host made, at x, y in a
    // row lineHeight high, with the text adjustY below its top. What falls
    // outside the clip rectangle can be left undrawn.
    virtual void Draw(TextCanvas &canvas, int x, int y, int lineHeight, int adjustY, int clipLeft, int clipTop,
                      int clipRight, int clipBottom) = 0;
};

#endif
