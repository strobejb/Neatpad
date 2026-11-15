#pragma once

#ifndef NEATPAD_PANGOTEXTLAYOUT_INCLUDED
#define NEATPAD_PANGOTEXTLAYOUT_INCLUDED

#include <pango/pango.h>

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

    const TextViewGtk *m_host;
    PangoLayout       *m_layout;
    int                m_length;

    // One of each per byte, and one for the end of the line
    std::vector<bool> m_cursorStop;
    std::vector<bool> m_wordStop;
    std::vector<bool> m_whiteSpace;

    // The runs' colours, a pair per byte, for drawing
    std::vector<uint32_t> m_fg;
    std::vector<uint32_t> m_bg;

    int      m_selStart;
    int      m_selEnd;
    uint32_t m_selFg;
    uint32_t m_selBg;
};

#endif
