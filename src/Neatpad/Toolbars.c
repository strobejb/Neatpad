//
//	Neatpad - Simple Text Editor application 
//
//	www.catch22.net
//	Written by J Brown 2004-2006
//

#define STRICT
#define _CRT_SECURE_NO_DEPRECATE

#include <windows.h>
#include <stdarg.h>
#include <tchar.h>
#include <commctrl.h>
#include <uxtheme.h>
#include "neatpad.h"



DWORD dwStatusBarStyles = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | 
						  CCS_NODIVIDER | CCS_NOPARENTALIGN | CCS_NOMOVEY |//| CCS_TOP|
						  SBT_NOBORDERS// | SBARS_SIZEGRIP
						  ;


#define MAX_STATUS_PARTS 5

// room left after the text of each pane
#define STATUS_PANE_PADDING _T("MMM")

// width of each pane, fitted to its text (the message pane takes whatever is left)
static int g_nPaneWidth[MAX_STATUS_PARTS];

//
//	Process WM_MENUSELECT message to display menu-item hints in statusbar
//
int StatusBarMenuSelect(HWND hwnd, HWND hwndSB, WPARAM wParam, LPARAM lParam)
{
	TCHAR buf[100];
	UINT  id    = LOWORD(wParam);
	UINT  flags = HIWORD(wParam);

	if((flags == 0xffff && lParam == 0) || (flags & (MF_POPUP | MF_SEPARATOR)))
	{
		SetStatusBarText(hwndSB, STATUS_PART_MESSAGE, 0, _T(""));
		return 0;
	}

	// Display helpful text in status bar
	if(LoadString(g_hResourceModule, id, buf, sizeof(buf) / sizeof(buf[0])))
		SetStatusBarText(hwndSB, STATUS_PART_MESSAGE, 0, _T("%s"), buf);
	else
		SetStatusBarText(hwndSB, STATUS_PART_MESSAGE, 0, _T(""));

	return 0;
}

//
//	Lay out the panes: the cursor and document-statistics panes on the left, the
//	line-format and encoding panes against the right edge, and the message pane
//	in between. Must be called whenever the statusbar changes size, so call each
//	time the main-window gets a WM_SIZE
//
void SetStatusBarParts(HWND hwndSB)
{
	RECT	r;
	HWND	hwndParent = GetParent(hwndSB);
	int		parts[MAX_STATUS_PARTS];
	int		right;

	GetClientRect(hwndParent, &r);

	// a window too narrow for every pane clips the ones on the right
	right = max(r.right, g_nPaneWidth[STATUS_PART_CURSOR]   + g_nPaneWidth[STATUS_PART_DOCSTATS] +
						 g_nPaneWidth[STATUS_PART_LINEFMT]  + g_nPaneWidth[STATUS_PART_ENCODING]);

	parts[STATUS_PART_CURSOR]   = g_nPaneWidth[STATUS_PART_CURSOR];
	parts[STATUS_PART_DOCSTATS] = parts[STATUS_PART_CURSOR] + g_nPaneWidth[STATUS_PART_DOCSTATS];
	parts[STATUS_PART_ENCODING] = right;
	parts[STATUS_PART_LINEFMT]  = parts[STATUS_PART_ENCODING] - g_nPaneWidth[STATUS_PART_ENCODING];
	parts[STATUS_PART_MESSAGE]  = parts[STATUS_PART_LINEFMT]  - g_nPaneWidth[STATUS_PART_LINEFMT];

	// Tell the status bar to create the window parts.
    SendMessage(hwndSB, SB_SETPARTS, MAX_STATUS_PARTS, (LPARAM)parts);
}

//
//	The width a pane needs: its text in the status bar's own font, the pane's
//	borders, and plenty of room after the text. An empty pane takes no room.
//
static int StatusPaneWidth(HWND hwndSB, UINT nPart, TCHAR *text)
{
	int		borders[3] = { 0 };
	int		width;
	SIZE	textSize;
	SIZE	padSize;
	HDC		hdc;
	HANDLE	hOldFont;

	if(text[0] == 0)
		return 0;

	SendMessage(hwndSB, SB_GETBORDERS, 0, (LPARAM)borders);

	hdc = GetDC(hwndSB);
	hOldFont = SelectObject(hdc, (HFONT)SendMessage(hwndSB, WM_GETFONT, 0, 0));
	GetTextExtentPoint32(hdc, text, lstrlen(text), &textSize);
	GetTextExtentPoint32(hdc, STATUS_PANE_PADDING, lstrlen(STATUS_PANE_PADDING), &padSize);
	SelectObject(hdc, hOldFont);
	ReleaseDC(hwndSB, hdc);

	width = textSize.cx + padSize.cx + borders[0] * 2 + borders[2];

	// the last pane also makes room for the size grip
	if(nPart == MAX_STATUS_PARTS - 1 && (GetWindowLong(hwndSB, GWL_STYLE) & SBARS_SIZEGRIP))
		width += GetSystemMetrics(SM_CXVSCROLL);

	return width;
}

//
//	sprintf-style wrapper for setting statubar pane text. Every pane but the
//	message pane is resized to fit its new text.
//
void SetStatusBarText(HWND hwndSB, UINT nPart, UINT uStyle, TCHAR *fmt, ...)
{
	TCHAR tmpbuf[100];
	va_list argp;
	int width;

	va_start(argp, fmt);
	_vsntprintf(tmpbuf, 100, fmt, argp);
	va_end(argp);
	tmpbuf[99] = 0;

	//cannot use PostMessage, as the panel type is not set correctly
	SendMessage(hwndSB, SB_SETTEXT, (WPARAM)(nPart | uStyle), (LPARAM)tmpbuf);

	if(nPart == STATUS_PART_MESSAGE || nPart >= MAX_STATUS_PARTS)
		return;

	width = StatusPaneWidth(hwndSB, nPart, tmpbuf);

	if(width != g_nPaneWidth[nPart])
	{
		g_nPaneWidth[nPart] = width;
		SetStatusBarParts(hwndSB);
	}
}

//
//	Create Neatpad's statusbar
//
HWND CreateStatusBar (HWND hwndParent)
{
	HWND hwndSB;
	const int verticalPadding = 16;
	
	hwndSB = CreateStatusWindow(dwStatusBarStyles, _T(""), hwndParent, 2);

	//SetWindowTheme(hwndSB, L"", L"");
	
	NONCLIENTMETRICS ncm;
	ncm.cbSize = sizeof(NONCLIENTMETRICS);

	// Fetch the system's default statusbar metrics
	if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0)) {
		LOGFONT statusFontDetails = ncm.lfStatusFont;
		int statusHeight = abs(statusFontDetails.lfHeight) + verticalPadding * 2;

		SendMessage(hwndSB, SB_SETMINHEIGHT, (WPARAM)(statusHeight), 0);
		SendMessage(hwndSB, WM_SIZE, 0, 0);
	}

	SetStatusBarParts(hwndSB);

	SetStatusBarText(hwndSB, STATUS_PART_CURSOR,   0,  _T(" Ln %d, Col %d"), 1, 1);
	SetStatusBarText(hwndSB, STATUS_PART_MESSAGE,  0,  _T(""));
	SetStatusBarText(hwndSB, STATUS_PART_DOCSTATS, 0,  _T(""));
	SetStatusBarText(hwndSB, STATUS_PART_LINEFMT,  0,  _T(" CRLF"));
	SetStatusBarText(hwndSB, STATUS_PART_ENCODING, 0, _T(" ASCII"));

	return hwndSB ;
}
