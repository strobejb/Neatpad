#pragma once

#ifndef NEATPAD_TEXTVIEW_GTK_INCLUDED
#define NEATPAD_TEXTVIEW_GTK_INCLUDED

#include <gtk/gtk.h>

#include <map>
#include <string>

#include "../TextViewHost.h"

class TextView;

//
//	TextViewGtk - the TextView engine's host on GTK. The NeatpadTextView widget
//	holds it, and it holds the engine: it does what the engine asks of its
//	host, and the widget passes the engine its input and its painting.
//
class TextViewGtk : public TextViewHost
{
  public:
    explicit TextViewGtk(GtkWidget *widget);
    ~TextViewGtk() override;

    TextView *View() const;

    // The fonts the view's layouts are in, and its character width
    PangoContext         *Context() const;
    PangoFontDescription *Font() const;
    int                   CharWidth() const;

    void SetFont(const char *description);

    // Space between the text and the edge of its area, as GNOME's editors leave
    static const int PAD_X = 12;
    static const int PAD_Y = 6;

    //
    //	From the widget: its text area's size, painting, scrollbars, input state
    //	and the clipboard's text when it has been read. The widget's points are
    //	in its own coordinates; EnginePoint gives the engine's, inside the padding.
    //
    void Resize(int width, int height);
    void EnginePoint(double x, double y, int *engineX, int *engineY) const;
    void Snapshot(GtkSnapshot *snapshot);

    void AttachScrollbars(GtkAdjustment *vertical, GtkAdjustment *horizontal);
    void Scrolled(bool vertical);

    void SetModifiers(GdkModifierType state);
    void SetMousePosition(int x, int y);
    void Paste(const char *text);

    //
    //	TextViewHost
    //
    void Invalidate() override;
    void InvalidateRect(int left, int top, int right, int bottom) override;
    void UpdateNow() override;

    void     ScrollPixels(int dx, int dy, int clipLeft, int clipTop, int clipRight, int clipBottom) override;
    void     SetVScroll(uint64_t max, uint64_t pos, uint64_t page) override;
    uint64_t VScrollTrackPos(uint64_t max) override;
    void     SetHScroll(int max, int pos, int page) override;
    int      HScrollTrackPos() override;

    void CreateCaret(int width, int height) override;
    void DestroyCaret() override;
    void ShowCaret() override;
    void HideCaret() override;
    void SetCaretPos(int x, int y) override;

    bool StartTimer(unsigned id, unsigned milliseconds) override;
    void StopTimer(unsigned id) override;

    void     MousePosition(int *x, int *y) override;
    void     CaptureMouse() override;
    void     ReleaseMouse() override;
    unsigned Modifiers() override;
    void     SetCursor(Cursor cursor) override;

    void     ClientSize(int *width, int *height) override;
    bool     HasFocus() override;
    bool     IsActive() override;
    int      CaretWidth() override;
    int      WheelScrollLines() override;
    uint32_t SystemColour(int index) override;

    int         FontCount() override;
    void        GetFontMetrics(int font, FontMetrics *metrics) override;
    int         TextWidth(const tchar_t *text, int length) override;
    bool        MarginImageSize(int *width, int *height) override;
    TextLayout *CreateLayout() override;

    bool ReadClipboard(const std::function<void(const tchar_t *text, size_t length)> &use) override;
    bool WriteClipboard(size_t length, const std::function<size_t(tchar_t *buffer)> &fill) override;

    intptr_t Notify(unsigned code, void *info) override;

  private:
    struct Timer
    {
        TextViewGtk *host;
        unsigned     id;
    };

    static gboolean OnTimer(gpointer data);
    static gboolean OnPaintedScroll(gpointer data);

    void Measure();

    GtkWidget            *m_widget;
    PangoContext         *m_context;
    PangoFontDescription *m_font;
    PangoLayout          *m_measure; // for TextWidth
    FontMetrics           m_metrics;
    TextView             *m_view;

    int m_width; // the engine's area, inside the padding
    int m_height;

    // The caret, drawn with the text: hidden while m_caretHidden isn't 0
    bool m_caretCreated;
    int  m_caretHidden;
    int  m_caretX;
    int  m_caretY;
    int  m_caretWidth;
    int  m_caretHeight;

    GtkAdjustment *m_vadjust;
    GtkAdjustment *m_hadjust;
    bool           m_settingScroll; // the view is moving the scrollbars, not the user

    // Scrollbar ranges set while painting wait until the frame is drawn: GTK
    // can't place a scrollbar part way through drawing it
    struct Range
    {
        bool     pending;
        uint64_t max;
        uint64_t pos;
        uint64_t page;
    };

    bool  m_painting;
    Range m_vpending;
    Range m_hpending;
    guint m_scrollIdle;

    std::map<unsigned, guint> m_timers;

    unsigned    m_modifiers;
    int         m_mouseX;
    int         m_mouseY;
    std::string m_paste; // the clipboard's text while a paste reads it
};

// Emit the widget's signal for a TVNOTIFY_* code (NeatpadTextView.cpp)
void NeatpadTextViewNotify(GtkWidget *widget, unsigned code);

// The widget's host, for tests (NeatpadTextView.cpp)
TextViewGtk *NeatpadTextViewHost(GtkWidget *widget);

#endif
