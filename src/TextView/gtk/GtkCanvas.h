#pragma once

#ifndef NEATPAD_GTKCANVAS_INCLUDED
#define NEATPAD_GTKCANVAS_INCLUDED

#include <gtk/gtk.h>

#include "../TextCanvas.h"

// A rectangle or a point to pass to GTK, as C would pass a compound literal
struct RectArg
{
    graphene_rect_t rect;

    RectArg(float x, float y, float width, float height)
    {
        graphene_rect_init(&rect, x, y, width, height);
    }

    operator const graphene_rect_t *() const
    {
        return &rect;
    }
};

struct PointArg
{
    graphene_point_t point;

    PointArg(float x, float y)
    {
        graphene_point_init(&point, x, y);
    }

    operator const graphene_point_t *() const
    {
        return &point;
    }
};

// A colour as the engine has it, 0x00BBGGRR, for GDK
GdkRGBA RgbaFromColour(uint32_t colour);

//
//	GtkCanvas - painting onto a GtkSnapshot. A row moves the origin to its top
//	and clips to itself until it ends.
//
class GtkCanvas : public TextCanvas
{
  public:
    // How far a row's backgrounds reach into the next row (see BeginRow)
    static const int ROW_OVERLAP = 1;

    // originY: the view's top in its surface; scale: device pixels a pixel
    GtkCanvas(GtkSnapshot *snapshot, PangoContext *context, const PangoFontDescription *font, double originY,
              double scale);
    ~GtkCanvas() override;

    void BeginRow(int y, int width, int height) override;
    void EndRow() override;

    void FillRect(int x, int y, int width, int height, uint32_t colour) override;
    void FillChecked(int x, int y, int width, int height, uint32_t fg, uint32_t bg) override;

    int  MarginTextWidth(const tchar_t *text, int length) override;
    void DrawMarginText(int x, int y, const tchar_t *text, int length, uint32_t fg, uint32_t bg, int clipLeft,
                        int clipTop, int clipRight, int clipBottom) override;

    void DrawMarginImage(int image, int x, int y, int width, int height) override;

    GtkSnapshot *Snapshot() const;

  private:
    GtkSnapshot *m_snapshot;
    PangoLayout *m_marginLayout;
    int          m_rowHeight;   // the row being drawn, or 0
    double       m_originY;
    double       m_scale;

    float SnapY(int y) const;
};

#endif
