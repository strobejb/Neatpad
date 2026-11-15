// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

#pragma once

#ifndef NEATPAD_PANGOTEXTLAYOUT_INCLUDED
#define NEATPAD_PANGOTEXTLAYOUT_INCLUDED

#include <gtk/gtk.h>

#include <vector>

#include "../TextLayout.h"

class TextViewGtk;

//
//	PangoTextLayout - a line laid out by Pango. Text is UTF-8, as Pango holds
//	it, so offsets are Pango's byte indexes. Pango's per-character stops are
//	spread over the bytes: none is ever inside a character. The font is the
//	host's, as it is when the line is laid out.
//
class PangoTextLayout : public TextLayout
{
  public:
    explicit PangoTextLayout(const TextViewGtk *host);
    ~PangoTextLayout() override;

    bool Analyze(const tchar_t *text, int length, TextRun *runs, int tabWidth) override;

    int  Length() override;
    int  Width() override;
    int  OffsetToX(int offset, bool trailing) override;
    void XToOffset(int x, int *offset, bool *trailing) override;
    int  SnapXToOffset(int x, int *snappedX) override;

    bool IsCursorStop(int offset) override;
    bool IsWordStop(int offset) override;
    bool IsWhiteSpace(int offset) override;

    void SetSelection(int start, int end) override;
    void SetSelectionColours(uint32_t fg, uint32_t bg) override;

    void Draw(TextCanvas &canvas, int x, int y, int lineHeight, int adjustY, int clipLeft, int clipTop, int clipRight,
              int clipBottom) override;

  private:
    // Where a byte's character starts, and the next character's start
    int CharStart(int offset);
    int NextChar(int offset);

    void DrawText(GtkSnapshot *snapshot, int x, int y, int left, int right, const GdkRGBA *colour);
    void DrawControl(GtkSnapshot *snapshot, int x, int y, int index);

    const TextViewGtk *m_host;
    PangoLayout       *m_layout;
    int                m_length;

    // One of each per byte, and one for the end of the line
    std::vector<bool> m_cursorStop;
    std::vector<bool> m_wordStop;
    std::vector<bool> m_whiteSpace;

    // Each character's leading edge and width (negative right to left), in
    // Pango units, by its first byte: found once, as Pango's own lookup walks
    // the line each time, which a long line makes slow
    std::vector<int> m_x;
    std::vector<int> m_width;
    int              m_lineWidth; // in pixels

    // The runs' colours, a pair per byte, for drawing
    std::vector<uint32_t> m_fg;
    std::vector<uint32_t> m_bg;

    // Control characters (but tabs), as Win32's UspLib shows them: their code
    // in hex, in the text's colour turned round, in a rounded box. Pango keeps
    // a shape's room for each, and draws nothing there.
    struct Control
    {
        int           offset;
        unsigned char code;
    };

    std::vector<Control> m_controls;

    int      m_selStart;
    int      m_selEnd;
    uint32_t m_selFg;
    uint32_t m_selBg;
};

#endif
