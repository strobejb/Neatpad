// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		PangoTextLayout.cpp
//
//	PURPOSE:	A line laid out by Pango
//
#include <string.h>

#include "PangoTextLayout.h"
#include "GtkCanvas.h"
#include "TextViewGtk.h"

PangoTextLayout::PangoTextLayout(const TextViewGtk *host)
    : m_host(host), m_layout(pango_layout_new(host->Context())), m_length(0), m_lineWidth(0), m_selStart(0),
      m_selEnd(0), m_selFg(0), m_selBg(0)
{
    pango_layout_set_single_paragraph_mode(m_layout, TRUE);
}

PangoTextLayout::~PangoTextLayout()
{
    g_object_unref(m_layout);
}

// The border round a control character's label, as UspLib's: 15% of its
// digits' height down, and at least 2 pixels across
static int BorderY(int inkHeight)
{
    return inkHeight * 15 / 100 > 1 ? inkHeight * 15 / 100 : 1;
}

static int BorderX(int inkHeight)
{
    return BorderY(inkHeight) > 2 ? BorderY(inkHeight) : 2;
}

//
//	Lay out the line, and spread Pango's stops, one per character, over its
//	bytes. The runs' foregrounds become attributes; their backgrounds are drawn.
//
bool PangoTextLayout::Analyze(const tchar_t *text, int length, TextRun *runs, int tabWidth)
{
    PangoTabArray      *tabs = pango_tab_array_new(1, TRUE);
    PangoAttrList      *list = pango_attr_list_new();
    const PangoLogAttr *attrs;
    int                 count;
    int                 index = 0;

    m_length   = length;
    m_selStart = 0;
    m_selEnd   = 0;

    pango_layout_set_font_description(m_layout, m_host->Font());

    // Pango stops at a NUL, as UTF-8 never has one: a space holds its place,
    // drawn as a control character as the rest are
    if (memchr(text, 0, length))
    {
        std::string copy(text, length);

        for (char &c : copy)
        {
            if (c == 0)
                c = ' ';
        }

        pango_layout_set_text(m_layout, copy.data(), length);
    }
    else
    {
        pango_layout_set_text(m_layout, text, length);
    }

    // a tab stop every tabWidth characters: Pango repeats the one given
    pango_tab_array_set_tab(tabs, 0, PANGO_TAB_LEFT, (tabWidth > 0 ? tabWidth : 1) * m_host->CharWidth());
    pango_layout_set_tabs(m_layout, tabs);
    pango_tab_array_free(tabs);

    m_fg.assign(length, 0);
    m_bg.assign(length, 0);

    for (int i = 0; i < length; i++)
    {
        m_fg[i] = runs[i].fg;
        m_bg[i] = runs[i].bg;
    }

    // the foreground of each stretch of one colour
    for (int start = 0; start < length;)
    {
        int             end = start;
        GdkRGBA         rgba;
        PangoAttribute *attr;

        while (end < length && m_fg[end] == m_fg[start])
            end++;

        rgba = RgbaFromColour(m_fg[start]);
        attr = pango_attr_foreground_new((guint16)(rgba.red * 65535), (guint16)(rgba.green * 65535),
                                         (guint16)(rgba.blue * 65535));

        attr->start_index = start;
        attr->end_index   = end;
        pango_attr_list_insert(list, attr);
        start = end;
    }

    // the bold and italic stretches
    for (int start = 0; start < length;)
    {
        int end = start;

        while (end < length && runs[end].bold == runs[start].bold && runs[end].italic == runs[start].italic)
            end++;

        PangoAttribute *style[] = {
            runs[start].bold ? pango_attr_weight_new(PANGO_WEIGHT_BOLD) : 0,
            runs[start].italic ? pango_attr_style_new(PANGO_STYLE_ITALIC) : 0,
        };

        for (PangoAttribute *attr : style)
        {
            if (attr == 0)
                continue;

            attr->start_index = start;
            attr->end_index   = end;
            pango_attr_list_insert(list, attr);
        }

        start = end;
    }

    // each control character a shape, as wide as its label in a box
    m_controls.clear();

    for (int i = 0; i < length; i++)
    {
        PangoRectangle  room;
        PangoAttribute *shape;

        if (!runs[i].ctrl || text[i] == '\t')
            continue;

        const TextViewGtk::ControlLabel &label = m_host->Control((unsigned char)text[i]);

        m_controls.push_back({i, (unsigned char)text[i]});

        room.x      = 0;
        room.y      = -label.baseline * PANGO_SCALE;
        room.width  = (label.width + 2 * BorderX(label.inkHeight) + BorderY(label.inkHeight)) * PANGO_SCALE;
        room.height = label.height * PANGO_SCALE;

        shape              = pango_attr_shape_new(&room, &room);
        shape->start_index = i;
        shape->end_index   = i + 1;
        pango_attr_list_insert(list, shape);
    }

    pango_layout_set_attributes(m_layout, list);
    pango_attr_list_unref(list);

    m_cursorStop.assign(length + 1, false);
    m_wordStop.assign(length + 1, false);
    m_whiteSpace.assign(length + 1, false);

    attrs = pango_layout_get_log_attrs_readonly(m_layout, &count);

    // a character's stops belong to its first byte alone
    for (int c = 0; index < length && c < count; c++)
    {
        m_cursorStop[index] = attrs[c].is_cursor_position;
        m_wordStop[index]   = attrs[c].is_word_start;
        m_whiteSpace[index] = attrs[c].is_white;

        index = (int)(g_utf8_next_char(text + index) - text);
    }

    m_cursorStop[length] = true;
    m_wordStop[length]   = true;

    // where each character is, in one pass along the line
    PangoLayoutIter *iter = pango_layout_get_iter(m_layout);

    m_x.assign(length + 1, 0);
    m_width.assign(length + 1, 0);

    do
    {
        int             i   = pango_layout_iter_get_index(iter);
        PangoLayoutRun *run = pango_layout_iter_get_run_readonly(iter);
        PangoRectangle  extents;
        bool            rtl;

        if (i >= length)
            break;

        pango_layout_iter_get_char_extents(iter, &extents);
        rtl        = run && (run->item->analysis.level & 1);
        m_x[i]     = rtl ? extents.x + extents.width : extents.x;
        m_width[i] = rtl ? -extents.width : extents.width;
    } while (pango_layout_iter_next_char(iter));

    pango_layout_iter_free(iter);
    pango_layout_get_pixel_size(m_layout, &m_lineWidth, 0);
    return true;
}

int PangoTextLayout::Length()
{
    return m_length;
}

int PangoTextLayout::Width()
{
    return m_lineWidth;
}

int PangoTextLayout::OffsetToX(int offset, bool trailing)
{
    int start;

    if (offset >= m_length)
        return Width();

    start = CharStart(offset);
    return PANGO_PIXELS(trailing ? m_x[start] + m_width[start] : m_x[start]);
}

void PangoTextLayout::XToOffset(int x, int *offset, bool *trailing)
{
    int index = 0;
    int trail = 0;

    pango_layout_xy_to_index(m_layout, x * PANGO_SCALE, 0, &index, &trail);

    *offset   = index < m_length ? index : m_length;
    *trailing = trail > 0 && index < m_length;
}

int PangoTextLayout::SnapXToOffset(int x, int *snappedX)
{
    int  offset;
    bool trailing;

    XToOffset(x, &offset, &trailing);

    if (trailing)
        offset = NextChar(offset);

    *snappedX = OffsetToX(offset, false);
    return offset;
}

bool PangoTextLayout::IsCursorStop(int offset)
{
    return offset >= m_length || (offset >= 0 && m_cursorStop[offset]);
}

bool PangoTextLayout::IsWordStop(int offset)
{
    return offset >= m_length || (offset >= 0 && m_wordStop[offset]);
}

bool PangoTextLayout::IsWhiteSpace(int offset)
{
    return offset >= 0 && offset < m_length && m_whiteSpace[offset];
}

void PangoTextLayout::SetSelection(int start, int end)
{
    m_selStart = start;
    m_selEnd   = end;
}

void PangoTextLayout::SetSelectionColours(uint32_t fg, uint32_t bg)
{
    m_selFg = fg;
    m_selBg = bg;
}

//
//	The runs' backgrounds, the selection over them, then the text: in the
//	selection, in its own colour. Only what's in sight is drawn: a line can be
//	thousands of characters long.
//
void PangoTextLayout::Draw(TextCanvas &canvas, int x, int y, int lineHeight, int adjustY, int clipLeft, int clipTop,
                           int clipRight, int clipBottom)
{
    GtkSnapshot *snapshot = static_cast<GtkCanvas &>(canvas).Snapshot();

    gtk_snapshot_push_clip(snapshot,
                           RectArg((float)clipLeft, (float)clipTop, (float)(clipRight - clipLeft),
                                               (float)(clipBottom - clipTop + GtkCanvas::ROW_OVERLAP)));

    for (int start = 0; start < m_length;)
    {
        int     end = start;
        int     left;
        GdkRGBA rgba;

        while (end < m_length && m_bg[end] == m_bg[start])
            end++;

        left = OffsetToX(start, false);
        rgba = RgbaFromColour(m_bg[start]);

        gtk_snapshot_append_color(snapshot, &rgba,
                                  RectArg((float)(x + left), (float)y,
                                                      (float)(OffsetToX(end, false) - left),
                                                      (float)(lineHeight + GtkCanvas::ROW_OVERLAP)));
        start = end;
    }

    // the line's end is in the line, as a space, so it's selected with it, as
    // on Win32: nothing more is added past the end
    if (m_selStart < m_selEnd)
    {
        int     left  = OffsetToX(m_selStart, false);
        int     right = OffsetToX(m_selEnd, false);
        GdkRGBA rgba  = RgbaFromColour(m_selBg);

        gtk_snapshot_append_color(snapshot, &rgba,
                                  RectArg((float)(x + left), (float)y, (float)(right - left),
                                          (float)(lineHeight + GtkCanvas::ROW_OVERLAP)));
    }

    // the text, and the selected text in the selection's colour: the line is
    // laid out once, and its glyphs drawn either side of the selection, and
    // in it
    if (m_selStart < m_selEnd)
    {
        int     left  = x + OffsetToX(m_selStart, false);
        int     right = x + OffsetToX(m_selEnd, false);
        GdkRGBA fg    = RgbaFromColour(m_selFg);

        DrawText(snapshot, x, y + adjustY, clipLeft, left, 0);
        DrawText(snapshot, x, y + adjustY, left, right, &fg);
        DrawText(snapshot, x, y + adjustY, right, clipRight, 0);
    }
    else
    {
        DrawText(snapshot, x, y + adjustY, clipLeft, clipRight, 0);
    }

    // those in sight: a long line can hold thousands
    for (size_t i = 0; i < m_controls.size(); i++)
    {
        int left  = x + OffsetToX(m_controls[i].offset, false);
        int right = x + OffsetToX(m_controls[i].offset + 1, false);

        if (right >= clipLeft && left <= clipRight)
            DrawControl(snapshot, x, y + adjustY, (int)i);
    }

    gtk_snapshot_pop(snapshot);
}

//
//	The glyphs between left and right, the line's runs at a time: those out of
//	sight aren't drawn. Each in its own colour, or in colour if it's given.
//
void PangoTextLayout::DrawText(GtkSnapshot *snapshot, int x, int y, int left, int right, const GdkRGBA *colour)
{
    PangoLayoutIter *iter;

    if (left >= right)
        return;

    iter = pango_layout_get_iter(m_layout);

    gtk_snapshot_push_clip(snapshot, RectArg((float)left, (float)y - 1000, (float)(right - left), 2000));

    do
    {
        PangoLayoutRun *run = pango_layout_iter_get_run_readonly(iter);
        PangoRectangle  extents;
        GdkRGBA         rgba = {0, 0, 0, 1};
        GskRenderNode  *node;

        if (!run)
            continue;

        pango_layout_iter_get_run_extents(iter, 0, &extents);

        // runs go left to right: the rest are past the right edge
        if (x + PANGO_PIXELS(extents.x) > right)
            break;

        if (x + PANGO_PIXELS(extents.x + extents.width) < left)
            continue;

        if (colour)
        {
            rgba = *colour;
        }
        else
        {
            for (GSList *a = run->item->analysis.extra_attrs; a; a = a->next)
            {
                PangoAttribute *attr = (PangoAttribute *)a->data;

                if (attr->klass->type == PANGO_ATTR_FOREGROUND)
                {
                    PangoColor *c = &((PangoAttrColor *)attr)->color;

                    rgba = {c->red / 65535.0f, c->green / 65535.0f, c->blue / 65535.0f, 1};
                }
            }
        }

        graphene_point_t at = GRAPHENE_POINT_INIT(
            (float)x + (float)extents.x / PANGO_SCALE,
            (float)y + (float)(pango_layout_iter_get_baseline(iter) - run->y_offset) / PANGO_SCALE);

        node = gsk_text_node_new(run->item->analysis.font, run->glyphs, &rgba, &at);

        if (node)
        {
            gtk_snapshot_append_node(snapshot, node);
            gsk_render_node_unref(node);
        }
    } while (pango_layout_iter_next_run(iter));

    gtk_snapshot_pop(snapshot);
    pango_layout_iter_free(iter);
}

//
//	A control character: its label in the background's colour, in a rounded
//	box of the text's (the selection's, where it's selected), as UspLib's
//	PaintCtrlChar draws it
//
void PangoTextLayout::DrawControl(GtkSnapshot *snapshot, int x, int y, int index)
{
    const Control                   &control  = m_controls[index];
    const TextViewGtk::ControlLabel &text     = m_host->Control(control.code);
    bool                             selected = control.offset >= m_selStart && control.offset < m_selEnd;
    GdkRGBA                          box      = RgbaFromColour(selected ? m_selFg : m_fg[control.offset]);
    GdkRGBA                          label    = RgbaFromColour(selected ? m_selBg : m_bg[control.offset]);
    int                              borderY  = BorderY(text.inkHeight);
    int                              borderX  = BorderX(text.inkHeight);
    int                              left     = x + OffsetToX(control.offset, false) + borderY / 2 + 1;
    graphene_rect_t                  rect     = GRAPHENE_RECT_INIT((float)left, (float)(y + text.inkTop - borderY),
                                                                   (float)(text.width + 2 * borderX - 2),
                                                                   (float)(text.inkHeight + 2 * borderY));
    GskRoundedRect                   rounded;

    gsk_rounded_rect_init_from_rect(&rounded, &rect, (float)borderY + 1);
    gtk_snapshot_push_rounded_clip(snapshot, &rounded);
    gtk_snapshot_append_color(snapshot, &box, &rect);
    gtk_snapshot_pop(snapshot);

    gtk_snapshot_save(snapshot);
    gtk_snapshot_translate(snapshot, PointArg((float)(left + borderX - 1), (float)y));
    gtk_snapshot_append_layout(snapshot, text.layout, &label);
    gtk_snapshot_restore(snapshot);
}

int PangoTextLayout::CharStart(int offset)
{
    while (offset > 0 && offset < m_length && !m_cursorStop[offset])
        offset--;

    return offset;
}

int PangoTextLayout::NextChar(int offset)
{
    do
        offset++;
    while (offset < m_length && !m_cursorStop[offset]);

    return offset < m_length ? offset : m_length;
}
