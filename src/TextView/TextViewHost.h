#pragma once

#ifndef NEATPAD_TEXTVIEW_HOST_INCLUDED
#define NEATPAD_TEXTVIEW_HOST_INCLUDED

#include <functional>
#include <stddef.h>
#include <stdint.h>

#include "../TextDocument/TextDocument.h" // tchar_t

class TextLayout;

//
//	TextViewHost - what the TextView engine asks of the window it lives in.
//	The engine reaches its platform through this: redraws, scrolling, the
//	caret, timers, the clipboard and notifications. Positions are pixels in
//	the view, from its top-left corner.
//
class TextViewHost
{
  public:
    virtual ~TextViewHost()
    {
    }

    //
    //	Redraws. These are hints: a host may redraw more than it's asked to.
    //
    virtual void Invalidate()                                             = 0;
    virtual void InvalidateRect(int left, int top, int right, int bottom) = 0;

    // Paint whatever needs it now, before returning
    virtual void UpdateNow() = 0;

    //
    //	Scrolling
    //

    // Move what the view shows inside the clip rectangle by dx, dy pixels, and
    // redraw what that uncovers
    virtual void ScrollPixels(int dx, int dy, int clipLeft, int clipTop, int clipRight, int clipBottom) = 0;

    // The vertical scrollbar, in bytes of the document: 0 to max, with the
    // page showing from pos. Its range can be more than 32 bits.
    virtual void     SetVScroll(uint64_t max, uint64_t pos, uint64_t page) = 0;
    virtual uint64_t VScrollTrackPos(uint64_t max)                         = 0;

    // The horizontal scrollbar, in columns
    virtual void SetHScroll(int max, int pos, int page) = 0;
    virtual int  HScrollTrackPos()                      = 0;

    //
    //	The caret
    //
    virtual void CreateCaret(int width, int height) = 0;
    virtual void DestroyCaret()                     = 0;
    virtual void ShowCaret()                        = 0;
    virtual void HideCaret()                        = 0;
    virtual void SetCaretPos(int x, int y)          = 0;

    //
    //	Timers. The host calls the engine's OnTimer with the id every
    //	milliseconds; 0 milliseconds is as often as the platform allows.
    //
    virtual bool StartTimer(unsigned id, unsigned milliseconds) = 0;
    virtual void StopTimer(unsigned id)                         = 0;

    // Where the mouse is, in the view's coordinates, for scrolling while
    // a selection is dragged past the edge
    virtual void MousePosition(int *x, int *y) = 0;

    // Keep the mouse while a selection is dragged, and let it go
    virtual void CaptureMouse() = 0;
    virtual void ReleaseMouse() = 0;

    // The mouse pointer's shape: the text's I-beam, or the margin's arrow
    enum Cursor
    {
        CursorText,
        CursorMargin
    };

    virtual void SetCursor(Cursor cursor) = 0;

    //
    //	The view and its window
    //

    // The view's size, in pixels
    virtual void ClientSize(int *width, int *height) = 0;

    // Whether the view has the keyboard focus, and whether it's in the active
    // window, which shows the selection in its active colours
    virtual bool HasFocus() = 0;
    virtual bool IsActive() = 0;

    // The user's settings: the caret's width (0 for the platform's usual), and
    // how many lines one notch of the mouse wheel scrolls
    virtual int CaretWidth()       = 0;
    virtual int WheelScrollLines() = 0;

    //
    //	Fonts. The host holds them, and a layout's runs choose one by index.
    //	Font 0 is the main font, which the margin uses too.
    //
    struct FontMetrics
    {
        int height; // a line of it, with its external leading
        int ascent;
        int averageWidth;
    };

    virtual int  FontCount()                                    = 0;
    virtual void GetFontMetrics(int font, FontMetrics *metrics) = 0;

    // How wide text is in the main font
    virtual int TextWidth(const tchar_t *text, int length) = 0;

    // The size of the images the margin shows, or false if it has none
    virtual bool MarginImageSize(int *width, int *height) = 0;

    // A new layout, for a line in the host's fonts
    virtual TextLayout *CreateLayout() = 0;

    //
    //	The clipboard, as text in the build's text units, without copying it
    //	more than the platform needs to
    //

    // Call use with the clipboard's text; false if it holds none
    virtual bool ReadClipboard(const std::function<void(const tchar_t *text, size_t length)> &use) = 0;

    // Put length units of text on the clipboard: fill writes them into the
    // buffer it's given and returns how many it wrote
    virtual bool WriteClipboard(size_t length, const std::function<size_t(tchar_t *buffer)> &fill) = 0;

    //
    //	Notifications: tell whoever holds the view that code happened. info is
    //	the notification's own structure, starting with its header, or 0.
    //
    virtual intptr_t Notify(unsigned code, void *info) = 0;
};

#endif
