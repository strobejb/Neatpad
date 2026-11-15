//
//	MODULE:		GtkCanvas.cpp
//
//	PURPOSE:	Painting onto a GtkSnapshot
//
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

GtkCanvas::GtkCanvas(GtkSnapshot *snapshot, PangoContext *context, const PangoFontDescription *font)
    : m_snapshot(snapshot), m_marginLayout(pango_layout_new(context))
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

void GtkCanvas::BeginRow(int y, int width, int height)
{
    gtk_snapshot_save(m_snapshot);
    gtk_snapshot_translate(m_snapshot, PointArg(0, (float)y));
    gtk_snapshot_push_clip(m_snapshot, RectArg(0, 0, (float)width, (float)height));
}

void GtkCanvas::EndRow()
{
    gtk_snapshot_pop(m_snapshot);
    gtk_snapshot_restore(m_snapshot);
}

void GtkCanvas::FillRect(int x, int y, int width, int height, uint32_t colour)
{
    GdkRGBA rgba = RgbaFromColour(colour);

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
