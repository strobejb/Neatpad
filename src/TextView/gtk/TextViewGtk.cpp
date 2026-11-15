// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		TextViewGtk.cpp
//
//	PURPOSE:	The TextView engine's host on GTK
//
#include "TextViewGtk.h"
#include "GtkCanvas.h"
#include "PangoTextLayout.h"
#include "../TextViewInternal.h"

#include <math.h>
#include <vector>

TextViewGtk::TextViewGtk(GtkWidget *widget)
    : m_widget(widget), m_context(gtk_widget_create_pango_context(widget)),
      m_font(pango_font_description_from_string("Monospace 11")), m_measure(pango_layout_new(m_context)),
      m_metrics(), m_controls(), m_view(0), m_windowColour(TV_RGB(255, 255, 255)), m_textColour(TV_RGB(0, 0, 0)),
      m_caretColour(TV_RGB(0, 0, 0)), m_width(0), m_height(0), m_caretCreated(false), m_caretHidden(1), m_caretX(0),
      m_caretY(0), m_caretWidth(0), m_caretHeight(0), m_caretOn(true),
      m_blinkTimer(0), m_blinkUntil(0), m_vadjust(0), m_hadjust(0), m_settingScroll(false),
      m_painting(false), m_vpending(), m_hpending(), m_scrollIdle(0), m_modifiers(0), m_mouseX(0), m_mouseY(0)
{
    // the engine works in whole pixels: so do glyphs, so an x always finds the
    // character it was measured from
    pango_context_set_round_glyph_positions(m_context, TRUE);

    Measure();

    // the view measures itself in the font as it starts
    m_view = new TextView(this);
}

TextViewGtk::~TextViewGtk()
{
    for (std::map<unsigned, guint>::iterator i = m_timers.begin(); i != m_timers.end(); ++i)
        g_source_remove(i->second);

    m_timers.clear();
    StopBlink();
    ForgetControls();

    if (m_scrollIdle)
        g_source_remove(m_scrollIdle);

    delete m_view;

    g_object_unref(m_measure);
    pango_font_description_free(m_font);
    g_object_unref(m_context);
}

TextView *TextViewGtk::View() const
{
    return m_view;
}

PangoContext *TextViewGtk::Context() const
{
    return m_context;
}

PangoFontDescription *TextViewGtk::Font() const
{
    return m_font;
}

int TextViewGtk::CharWidth() const
{
    return m_metrics.averageWidth;
}

void TextViewGtk::SetFont(const char *description)
{
    pango_font_description_free(m_font);
    m_font = pango_font_description_from_string(description);

    Measure();
    m_view->FontsChanged();
    Invalidate();
}

const TextViewGtk::ControlLabel &TextViewGtk::Control(unsigned char code) const
{
    ControlLabel &control = m_controls[code & 31];

    if (!control.layout)
    {
        PangoRectangle ink;
        PangoRectangle logical;
        char           label[4];

        g_snprintf(label, sizeof label, "%02X", (unsigned)(code & 31));
        control.layout = pango_layout_new(m_context);
        pango_layout_set_font_description(control.layout, m_font);
        pango_layout_set_text(control.layout, label, -1);
        pango_layout_get_pixel_extents(control.layout, &ink, &logical);

        control.width     = logical.width;
        control.height    = logical.height;
        control.baseline  = PANGO_PIXELS(pango_layout_get_baseline(control.layout));
        control.inkTop    = ink.y;
        control.inkHeight = ink.height;
    }

    return control;
}

void TextViewGtk::ForgetControls() const
{
    for (ControlLabel &control : m_controls)
    {
        if (control.layout)
            g_object_unref(control.layout);

        control = ControlLabel();
    }
}

// The font's line height, ascent and character width, in pixels
void TextViewGtk::Measure()
{
    ForgetControls();

    PangoFontMetrics *metrics = pango_context_get_metrics(m_context, m_font, 0);
    int               ascent  = PANGO_PIXELS_CEIL(pango_font_metrics_get_ascent(metrics));
    int               descent = PANGO_PIXELS_CEIL(pango_font_metrics_get_descent(metrics));

    m_metrics.height       = ascent + descent;
    m_metrics.ascent       = ascent;
    m_metrics.averageWidth = PANGO_PIXELS(pango_font_metrics_get_approximate_char_width(metrics));

    if (m_metrics.averageWidth <= 0)
        m_metrics.averageWidth = 1;

    pango_font_metrics_unref(metrics);
    pango_layout_set_font_description(m_measure, m_font);
}

void TextViewGtk::Resize(int width, int height)
{
    m_width  = MAX(width - 2 * PAD_X, 0);
    m_height = MAX(height - 2 * PAD_Y, 0);
    m_view->OnSize(0, m_width, m_height);
}

void TextViewGtk::SetPalette(uint32_t window, uint32_t text, uint32_t caret)
{
    m_windowColour = window;
    m_textColour   = text;
    m_caretColour  = caret;
    gtk_widget_queue_draw(m_widget);
}

void TextViewGtk::PaletteColours(uint32_t *window, uint32_t *text, uint32_t *caret) const
{
    *window = m_windowColour;
    *text   = m_textColour;
    *caret  = m_caretColour;
}

void TextViewGtk::EnginePoint(double x, double y, int *engineX, int *engineY) const
{
    *engineX = (int)floor(x) - PAD_X;
    *engineY = (int)floor(y) - PAD_Y;
}

//
//	The padding is the text's background. Inside it the engine paints its area,
//	row by row, and the caret goes on top.
//
void TextViewGtk::Snapshot(GtkSnapshot *snapshot)
{
    GtkNative *native  = gtk_widget_get_native(m_widget);
    double     originY = 0;
    double     scale   = 1;

    // where the view is in its surface, and the surface's scale, to keep rows
    // on device pixels
    if (native)
    {
        graphene_point_t at;
        double           surfaceX;
        double           surfaceY;

        gtk_native_get_surface_transform(native, &surfaceX, &surfaceY);

        if (gtk_widget_compute_point(m_widget, GTK_WIDGET(native), PointArg(0, PAD_Y), &at))
            originY = at.y + surfaceY;

        if (GdkSurface *surface = gtk_native_get_surface(native))
            scale = gdk_surface_get_scale(surface);
    }

    GtkCanvas canvas(snapshot, m_context, m_font, originY, scale);
    TextRect  rect       = {0, 0, m_width, m_height};
    GdkRGBA   background = RgbaFromColour(SystemColour(TVSC_WINDOW));

    gtk_snapshot_append_color(snapshot, &background,
                              RectArg(0, 0, (float)(m_width + 2 * PAD_X), (float)(m_height + 2 * PAD_Y)));

    gtk_snapshot_save(snapshot);
    gtk_snapshot_translate(snapshot, PointArg(PAD_X, PAD_Y));
    gtk_snapshot_push_clip(snapshot, RectArg(0, 0, (float)m_width, (float)m_height));

    m_painting = true;
    m_view->Paint(canvas, &rect);
    m_painting = false;

    if (m_caretCreated && m_caretHidden == 0 && m_caretOn && HasFocus())
    {
        GdkRGBA colour = RgbaFromColour(m_caretColour);

        gtk_snapshot_append_color(snapshot, &colour,
                                  RectArg((float)m_caretX, (float)m_caretY, (float)m_caretWidth,
                                                      (float)m_caretHeight));
    }

    gtk_snapshot_pop(snapshot);
    gtk_snapshot_restore(snapshot);
}

void TextViewGtk::AttachScrollbars(GtkAdjustment *vertical, GtkAdjustment *horizontal)
{
    m_vadjust = vertical;
    m_hadjust = horizontal;
}

// The user moved a scrollbar: the engine reads where it is now
void TextViewGtk::Scrolled(bool vertical)
{
    if (m_settingScroll)
        return;

    if (vertical)
        m_view->OnVScroll(TVSB_THUMBTRACK, 0);
    else
        m_view->OnHScroll(TVSB_THUMBTRACK, 0);
}

void TextViewGtk::SetModifiers(GdkModifierType state)
{
    m_modifiers = (state & GDK_SHIFT_MASK ? TVMOD_SHIFT : 0) | (state & GDK_CONTROL_MASK ? TVMOD_CTRL : 0) |
                  (state & GDK_ALT_MASK ? TVMOD_ALT : 0);
}

void TextViewGtk::SetMousePosition(int x, int y)
{
    m_mouseX = x;
    m_mouseY = y;
}

// The clipboard's text, read: the engine pastes it as if it read it itself
void TextViewGtk::Paste(const char *text)
{
    m_paste = text ? text : "";
    m_view->OnPaste();
    m_paste.clear();
}

//
//	Redraws. GTK draws the whole widget each frame, so all of them are one.
//
void TextViewGtk::Invalidate()
{
    gtk_widget_queue_draw(m_widget);
}

void TextViewGtk::InvalidateRect(int, int, int, int)
{
    gtk_widget_queue_draw(m_widget);
}

void TextViewGtk::UpdateNow()
{
}

void TextViewGtk::ScrollPixels(int, int, int, int, int, int)
{
    gtk_widget_queue_draw(m_widget);
}

//
//	The scrollbars. A Win32 scrollbar's range includes its page, as the engine
//	gives it: an adjustment's upper is one past it, and never less than a page.
//
// The ranges painting set, now that the frame is drawn
gboolean TextViewGtk::OnPaintedScroll(gpointer data)
{
    TextViewGtk *host = (TextViewGtk *)data;
    Range        v    = host->m_vpending;
    Range        h    = host->m_hpending;

    host->m_scrollIdle       = 0;
    host->m_vpending.pending = false;
    host->m_hpending.pending = false;

    if (v.pending)
        host->SetVScroll(v.max, v.pos, v.page);

    if (h.pending)
        host->SetHScroll((int)h.max, (int)h.pos, (int)h.page);

    return G_SOURCE_REMOVE;
}

void TextViewGtk::SetVScroll(uint64_t max, uint64_t pos, uint64_t page)
{
    if (m_vadjust == 0)
        return;

    if (m_painting)
    {
        m_vpending = {true, max, pos, page};

        if (m_scrollIdle == 0)
            m_scrollIdle = g_idle_add(OnPaintedScroll, this);

        return;
    }

    m_settingScroll = true;
    gtk_adjustment_configure(m_vadjust, (double)pos, 0, MAX((double)max + 1, (double)page), 1, (double)page,
                             (double)page);
    m_settingScroll = false;
}

uint64_t TextViewGtk::VScrollTrackPos(uint64_t max)
{
    double value = m_vadjust ? gtk_adjustment_get_value(m_vadjust) : 0;

    return value <= 0 ? 0 : value >= (double)max ? max : (uint64_t)value;
}

void TextViewGtk::SetHScroll(int max, int pos, int page)
{
    if (m_hadjust == 0)
        return;

    if (m_painting)
    {
        m_hpending = {true, (uint64_t)max, (uint64_t)pos, (uint64_t)page};

        if (m_scrollIdle == 0)
            m_scrollIdle = g_idle_add(OnPaintedScroll, this);

        return;
    }

    m_settingScroll = true;
    gtk_adjustment_configure(m_hadjust, pos, 0, MAX(max + 1, page), 1, page, page);
    m_settingScroll = false;
}

int TextViewGtk::HScrollTrackPos()
{
    return m_hadjust ? (int)gtk_adjustment_get_value(m_hadjust) : 0;
}

//
//	The caret: hidden once for each HideCaret not yet shown again, as Win32's is
//
void TextViewGtk::CreateCaret(int width, int height)
{
    m_caretCreated = true;
    m_caretHidden  = 1;
    m_caretWidth   = width;
    m_caretHeight  = height;
    RestartBlink();
}

void TextViewGtk::DestroyCaret()
{
    m_caretCreated = false;
    StopBlink();
    gtk_widget_queue_draw(m_widget);
}

void TextViewGtk::ShowCaret()
{
    if (m_caretHidden > 0)
        m_caretHidden--;

    RestartBlink();
    gtk_widget_queue_draw(m_widget);
}

void TextViewGtk::HideCaret()
{
    m_caretHidden++;
    gtk_widget_queue_draw(m_widget);
}

void TextViewGtk::SetCaretPos(int x, int y)
{
    // shown as it moves; refreshed where it is, it blinks on
    if (x != m_caretX || y != m_caretY)
        RestartBlink();

    m_caretX = x;
    m_caretY = y;
    gtk_widget_queue_draw(m_widget);
}

// Shown, and blinking from now on: GTK's settings say whether it blinks, how
// fast (a whole on-and-off), and for how many seconds
void TextViewGtk::RestartBlink()
{
    gboolean blink   = TRUE;
    int      time    = 1200;
    int      timeout = 10;

    StopBlink();
    m_caretOn = true;

    g_object_get(gtk_widget_get_settings(m_widget), "gtk-cursor-blink", &blink, "gtk-cursor-blink-time", &time,
                 "gtk-cursor-blink-timeout", &timeout, NULL);

    if (!m_caretCreated || !blink || time <= 0)
        return;

    m_blinkUntil = g_get_monotonic_time() + (gint64)timeout * G_TIME_SPAN_SECOND;
    m_blinkTimer = g_timeout_add((guint)(time / 2), OnBlink, this);
}

void TextViewGtk::StopBlink()
{
    if (m_blinkTimer)
    {
        g_source_remove(m_blinkTimer);
        m_blinkTimer = 0;
    }

    m_caretOn = true;
}

gboolean TextViewGtk::OnBlink(gpointer data)
{
    TextViewGtk *host = static_cast<TextViewGtk *>(data);

    host->m_caretOn = !host->m_caretOn;
    gtk_widget_queue_draw(host->m_widget);

    // it stops when it's shown
    if (host->m_caretOn && g_get_monotonic_time() >= host->m_blinkUntil)
    {
        host->m_blinkTimer = 0;
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

//
//	Timers, as GLib sources: 0 milliseconds is whenever the main loop is idle
//
gboolean TextViewGtk::OnTimer(gpointer data)
{
    Timer *timer = (Timer *)data;

    // the engine may stop this timer: GLib keeps it until this returns
    timer->host->m_view->OnTimer(timer->id);
    return G_SOURCE_CONTINUE;
}

bool TextViewGtk::StartTimer(unsigned id, unsigned milliseconds)
{
    Timer *timer = new Timer{this, id};
    guint  source;

    StopTimer(id);

    if (milliseconds == 0)
        source = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, OnTimer, timer, [](gpointer data) { delete (Timer *)data; });
    else
        source = g_timeout_add_full(G_PRIORITY_DEFAULT, milliseconds, OnTimer, timer,
                                    [](gpointer data) { delete (Timer *)data; });

    m_timers[id] = source;
    return source != 0;
}

void TextViewGtk::StopTimer(unsigned id)
{
    std::map<unsigned, guint>::iterator found = m_timers.find(id);

    if (found != m_timers.end())
    {
        g_source_remove(found->second);
        m_timers.erase(found);
    }
}

//
//	The mouse. GTK keeps it for the widget while a button is down anyway.
//
void TextViewGtk::MousePosition(int *x, int *y)
{
    *x = m_mouseX;
    *y = m_mouseY;
}

void TextViewGtk::CaptureMouse()
{
}

void TextViewGtk::ReleaseMouse()
{
}

unsigned TextViewGtk::Modifiers()
{
    return m_modifiers;
}

void TextViewGtk::SetCursor(Cursor cursor)
{
    gtk_widget_set_cursor_from_name(m_widget, cursor == CursorText ? "text" : "default");
}

void TextViewGtk::ClientSize(int *width, int *height)
{
    *width  = m_width;
    *height = m_height;
}

bool TextViewGtk::HasFocus()
{
    return gtk_widget_has_focus(m_widget);
}

bool TextViewGtk::IsActive()
{
    GtkRoot *root = gtk_widget_get_root(m_widget);

    return root && GTK_IS_WINDOW(root) && gtk_window_is_active(GTK_WINDOW(root));
}

int TextViewGtk::CaretWidth()
{
    return 0;
}

int TextViewGtk::WheelScrollLines()
{
    return 3;
}

//
//	The system colours the view uses, as a light theme has them. Following
//	the desktop's own theme comes with the application.
//
uint32_t TextViewGtk::SystemColour(int index)
{
    switch (index)
    {
    case TVSC_WINDOW:
        return m_windowColour;

    case TVSC_WINDOWTEXT:
        return m_textColour;

    case TVSC_HIGHLIGHT:
        return TV_RGB(0x35, 0x84, 0xe4);

    case TVSC_HIGHLIGHTTEXT:
        return TV_RGB(255, 255, 255);

    case TVSC_3DFACE:
        return TV_RGB(0xeb, 0xeb, 0xed);

    case TVSC_3DSHADOW:
        return TV_RGB(0x9a, 0x99, 0x96);

    case TVSC_3DHIGHLIGHT:
        return TV_RGB(0xfa, 0xfa, 0xfa);

    case TVSC_3DDKSHADOW:
        return TV_RGB(0x5e, 0x5c, 0x64);

    default:
        return TV_RGB(0x80, 0x80, 0x80);
    }
}

//
//	One font, for now: the main one
//
int TextViewGtk::FontCount()
{
    return 1;
}

void TextViewGtk::GetFontMetrics(int, FontMetrics *metrics)
{
    *metrics = m_metrics;
}

int TextViewGtk::TextWidth(const tchar_t *text, int length)
{
    int width = 0;

    pango_layout_set_text(m_measure, text, length);
    pango_layout_get_pixel_size(m_measure, &width, 0);
    return width;
}

bool TextViewGtk::MarginImageSize(int *width, int *height)
{
    *width  = 0;
    *height = 0;
    return false;
}

TextLayout *TextViewGtk::CreateLayout()
{
    return new PangoTextLayout(this);
}

//
//	The clipboard. GTK reads it asynchronously, so the widget reads it first
//	and Paste hands the text to the engine; writing it is immediate.
//
bool TextViewGtk::ReadClipboard(const std::function<void(const tchar_t *text, size_t length)> &use)
{
    if (m_paste.empty())
        return false;

    use(m_paste.data(), m_paste.size());
    return true;
}

bool TextViewGtk::WriteClipboard(size_t length, const std::function<size_t(tchar_t *buffer)> &fill)
{
    std::vector<tchar_t> buffer(length + 1);
    std::string          text(buffer.data(), fill(buffer.data()));

    gdk_clipboard_set_text(gtk_widget_get_clipboard(m_widget), text.c_str());
    return true;
}

intptr_t TextViewGtk::Notify(unsigned code, void *)
{
    NeatpadTextViewNotify(m_widget, code);
    return 0;
}
