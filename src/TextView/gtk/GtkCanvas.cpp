// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		GtkCanvas.cpp
//
//	PURPOSE:	Painting onto a GtkSnapshot
//
#include <math.h>

#include "GtkCanvas.h"

GdkRGBA RgbaFromColour(uint32_t colour)
{
    GdkRGBA rgba;

    rgba.red   = (float)(colour & 0xff) / 255.0f;
    rgba.green = (float)((colour >> 8) & 0xff) / 255.0f;
    rgba.blue  = (float)((colour >> 16) & 0xff) / 255.0f;
    rgba.alpha = 1.0f;
    return rgba;
}

GtkCanvas::GtkCanvas(GtkSnapshot *snapshot, PangoContext *context, const PangoFontDescription *font, double originY,
                     double scale)
    : m_snapshot(snapshot), m_marginLayout(pango_layout_new(context)), m_rowHeight(0), m_originY(originY),
      m_scale(scale)
{
    pango_layout_set_font_description(m_marginLayout, font);
}

GtkCanvas::~GtkCanvas()
{
    g_object_unref(m_marginLayout);
}

GtkSnapshot *GtkCanvas::Snapshot() const
{
    return m_snapshot;
}

// The nearest y on a device pixel's edge
float GtkCanvas::SnapY(int y) const
{
    return (float)(round((m_originY + y) * m_scale) / m_scale - m_originY);
}

//
//	At a fractional scale a row can start or end part way through a device
//	pixel. Both rows would part-paint that pixel, each layer of each row
//	blending its edge again, and a faint line would show between them. So a
//	row starts and ends on device pixels, and its backgrounds reach a pixel
//	past its end for the clip to cut.
//
void GtkCanvas::BeginRow(int y, int width, int height)
{
    float top    = SnapY(y);
    float bottom = SnapY(y + height);

    m_rowHeight = height;

    gtk_snapshot_save(m_snapshot);
    gtk_snapshot_translate(m_snapshot, PointArg(0, top));
    gtk_snapshot_push_clip(m_snapshot, RectArg(0, 0, (float)width, bottom - top));
}

void GtkCanvas::EndRow()
{
    m_rowHeight = 0;

    gtk_snapshot_pop(m_snapshot);
    gtk_snapshot_restore(m_snapshot);
}

void GtkCanvas::FillRect(int x, int y, int width, int height, uint32_t colour)
{
    GdkRGBA rgba = RgbaFromColour(colour);

    if (m_rowHeight && y + height >= m_rowHeight)
        height += ROW_OVERLAP;

    gtk_snapshot_append_color(m_snapshot, &rgba,
                              RectArg((float)x, (float)y, (float)width, (float)height));
}

//
//	Checked in two colours, a pixel at a time: drawn as a pixel pattern would
//	cost a rectangle a pixel, so a fill of half of each will do
//
void GtkCanvas::FillChecked(int x, int y, int width, int height, uint32_t fg, uint32_t bg)
{
    uint32_t mixed = 0;

    for (int shift = 0; shift < 24; shift += 8)
        mixed |= ((((fg >> shift) & 0xff) + ((bg >> shift) & 0xff)) / 2) << shift;

    FillRect(x, y, width, height, mixed);
}

int GtkCanvas::MarginTextWidth(const tchar_t *text, int length)
{
    int width = 0;

    pango_layout_set_text(m_marginLayout, text, length);
    pango_layout_get_pixel_size(m_marginLayout, &width, 0);
    return width;
}

void GtkCanvas::DrawMarginText(int x, int y, const tchar_t *text, int length, uint32_t fg, uint32_t bg, int clipLeft,
                               int clipTop, int clipRight, int clipBottom)
{
    GdkRGBA rgba = RgbaFromColour(fg);
    RectArg clip((float)clipLeft, (float)clipTop, (float)(clipRight - clipLeft), (float)(clipBottom - clipTop));

    FillRect(clipLeft, clipTop, clipRight - clipLeft, clipBottom - clipTop, bg);

    pango_layout_set_text(m_marginLayout, text, length);

    gtk_snapshot_push_clip(m_snapshot, clip);
    gtk_snapshot_save(m_snapshot);
    gtk_snapshot_translate(m_snapshot, PointArg((float)x, (float)y));
    gtk_snapshot_append_layout(m_snapshot, m_marginLayout, &rgba);
    gtk_snapshot_restore(m_snapshot);
    gtk_snapshot_pop(m_snapshot);
}

// The view has no margin images on GTK yet
void GtkCanvas::DrawMarginImage(int, int, int, int, int)
{
}
