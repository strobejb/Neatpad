//
//	MODULE:		PangoTextLayout.cpp
//
//	PURPOSE:	A line laid out by Pango
//
#include "PangoTextLayout.h"
#include "GtkCanvas.h"
#include "TextViewGtk.h"

PangoTextLayout::PangoTextLayout(const TextViewGtk *host)
    : m_host(host), m_layout(pango_layout_new(host->Context())), m_length(0), m_selStart(0), m_selEnd(0),
      m_selFg(0), m_selBg(0)
{
    pango_layout_set_single_paragraph_mode(m_layout, TRUE);
}

PangoTextLayout::~PangoTextLayout()
{
    g_object_unref(m_layout);
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
    pango_layout_set_text(m_layout, text, length);

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
    return true;
}

int PangoTextLayout::Length()
{
    return m_length;
}

int PangoTextLayout::Width()
{
    int width = 0;

    pango_layout_get_pixel_size(m_layout, &width, 0);
    return width;
}

int PangoTextLayout::OffsetToX(int offset, bool trailing)
{
    PangoRectangle pos;

    if (offset >= m_length)
        return Width();

    pango_layout_index_to_pos(m_layout, CharStart(offset), &pos);
    return PANGO_PIXELS(trailing ? pos.x + pos.width : pos.x);
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
//	selection, in its own colour
//
void PangoTextLayout::Draw(TextCanvas &canvas, int x, int y, int lineHeight, int adjustY, int clipLeft, int clipTop,
                           int clipRight, int clipBottom)
{
    GtkSnapshot   *snapshot = static_cast<GtkCanvas &>(canvas).Snapshot();
    PangoAttrList *original = pango_layout_get_attributes(m_layout);
    GdkRGBA        black    = {0, 0, 0, 1};

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

    if (m_selStart < m_selEnd)
    {
        int     left  = OffsetToX(m_selStart, false);
        int     right = m_selEnd >= m_length ? Width() + m_host->CharWidth() : OffsetToX(m_selEnd, false);
        GdkRGBA rgba  = RgbaFromColour(m_selBg);

        gtk_snapshot_append_color(snapshot, &rgba,
                                  RectArg((float)(x + left), (float)y, (float)(right - left),
                                          (float)(lineHeight + GtkCanvas::ROW_OVERLAP)));

        if (original)
        {
            PangoAttrList  *list = pango_attr_list_copy(original);
            GdkRGBA         fg   = RgbaFromColour(m_selFg);
            PangoAttribute *attr = pango_attr_foreground_new((guint16)(fg.red * 65535), (guint16)(fg.green * 65535),
                                                             (guint16)(fg.blue * 65535));

            pango_attr_list_ref(original);
            attr->start_index = m_selStart;
            attr->end_index   = m_selEnd;
            pango_attr_list_change(list, attr);
            pango_layout_set_attributes(m_layout, list);
            pango_attr_list_unref(list);
        }
    }

    gtk_snapshot_save(snapshot);
    gtk_snapshot_translate(snapshot, PointArg((float)x, (float)(y + adjustY)));
    gtk_snapshot_append_layout(snapshot, m_layout, &black);
    gtk_snapshot_restore(snapshot);

    // the line's own colours again, without the selection's
    if (m_selStart < m_selEnd && original)
    {
        pango_layout_set_attributes(m_layout, original);
        pango_attr_list_unref(original);
    }

    gtk_snapshot_pop(snapshot);
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
