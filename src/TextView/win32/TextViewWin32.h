#pragma once

#ifndef NEATPAD_TEXTVIEW_WIN32_INCLUDED
#define NEATPAD_TEXTVIEW_WIN32_INCLUDED

#include <windows.h>

#include "../TextViewHost.h"

//
//	TextViewWin32 - the TextView engine's host on Win32: its window. For now
//	the engine is still the window's own object and owns this; the window
//	will own the engine instead once the message code moves here.
//
class TextViewWin32 : public TextViewHost
{
  public:
    explicit TextViewWin32(HWND hwnd);

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

    bool ReadClipboard(const std::function<void(const tchar_t *text, size_t length)> &use) override;
    bool WriteClipboard(size_t length, const std::function<size_t(tchar_t *buffer)> &fill) override;

    intptr_t Notify(unsigned code, void *info) override;

  private:
    HWND m_hWnd;
};

#endif
