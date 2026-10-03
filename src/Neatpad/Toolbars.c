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
		SetStatusBarText(hwndSB, STATUS_PART_MESSAGE, 1, _T(""));
		return 0;
	}

	// Display helpful text in status bar
	if(LoadString(g_hResourceModule, id, buf, sizeof(buf) / sizeof(buf[0])))
		SetStatusBarText(hwndSB, STATUS_PART_MESSAGE, 1, _T("%s"), buf);
	else
		SetStatusBarText(hwndSB, STATUS_PART_MESSAGE, 1, _T(""));

	return 0;
}

//
//	Create each menubar pane. Must be called whenever the statusbar changes size,
//  so call each time the main-window gets a WM_SIZE
//
void SetStatusBarParts(HWND hwndSB)
{
	RECT	r;
	HWND	hwndParent = GetParent(hwndSB);
	int		parts[MAX_STATUS_PARTS];
	int		parentwidth;

	GetClientRect(hwndParent, &r);

	parentwidth = r.right < 620 ? 620 : r.right;
	parts[STATUS_PART_CURSOR]   = 270;
	parts[STATUS_PART_MESSAGE]  = parentwidth - 665;
	parts[STATUS_PART_EDITMODE] = parentwidth - 410;
	parts[STATUS_PART_LINEFMT]  = parentwidth - 220;
	parts[STATUS_PART_ENCODING] = parentwidth;

	// Tell the status bar to create the window parts. 
    SendMessage(hwndSB, SB_SETPARTS, MAX_STATUS_PARTS, (LPARAM)parts); 
}

//
//	sprintf-style wrapper for setting statubar pane text
//
void SetStatusBarText(HWND hwndSB, UINT nPart, UINT uStyle, TCHAR *fmt, ...)
{
	TCHAR tmpbuf[100];
	va_list argp;
	
	va_start(argp, fmt);
	_vsntprintf(tmpbuf, 100, fmt, argp);
	va_end(argp);

	//cannot use PostMessage, as the panel type is not set correctly
	SendMessage(hwndSB, SB_SETTEXT, (WPARAM)(nPart | uStyle), (LPARAM)tmpbuf);
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
	SetStatusBarText(hwndSB, STATUS_PART_MESSAGE,  1,  _T(""));
	//SetStatusBarText(hwndSB, STATUS_PART_EDITMODE, 0,  _T(" INS"));
	SetStatusBarText(hwndSB, STATUS_PART_LINEFMT,  0,  _T(" CRLF"));
	SetStatusBarText(hwndSB, STATUS_PART_ENCODING, 0, _T(" ASCII"));

	return hwndSB ;
}
