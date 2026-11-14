#pragma once

#ifndef NEATPAD_TEXTVIEW_WIN32_INCLUDED
#define NEATPAD_TEXTVIEW_WIN32_INCLUDED

#include <windows.h>
#include <uxtheme.h>

#include "../TextViewHost.h"

class TextView;

//
//	TextViewWin32 - the TextView window. The window holds it, and it holds the
//	engine: it turns the window's messages into calls to the engine, and does
//	what the engine asks of its host.
//
class TextViewWin32 : public TextViewHost
{
  public:
    explicit TextViewWin32(HWND hwnd);
    ~TextViewWin32() override;

    LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

    TextView *View() const
    {
        return m_pView;
    }

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
    void MousePosition(int *x, int *y) override;
    void CaptureMouse() override;
    void ReleaseMouse() override;
    void SetCursor(Cursor cursor) override;

    void ClientSize(int *width, int *height) override;
    bool HasFocus() override;
    bool IsActive() override;
    int  CaretWidth() override;
    int  WheelScrollLines() override;

    bool ReadClipboard(const std::function<void(const tchar_t *text, size_t length)> &use) override;
    bool WriteClipboard(size_t length, const std::function<size_t(tchar_t *buffer)> &fill) override;

    intptr_t Notify(unsigned code, void *info) override;

  private:
    LONG  OnNcPaint(HRGN hrgnUpdate);
    LONG  OnContextMenu(HWND hwndParam, int x, int y);
    HMENU CreateContextMenu();

    HWND      m_hWnd;
    TextView *m_pView;
    HTHEME    m_hTheme;
    HMENU     m_hUserMenu;     // the context menu the parent gave, if any
    HCURSOR   m_hMarginCursor; // the margin's right-pointing arrow
};

#endif
