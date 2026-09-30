//
//	Neatpad - Simple Text Editor application 
//
//	www.catch22.net
//	Written by J Brown 2004-2006
//
//	Freeware
//

#define STRICT
#define _CRT_SECURE_NO_DEPRECATE

#include <windows.h>
#include <stdarg.h>
#include <tchar.h>
#include <commctrl.h>
#include "neatpad.h"



DWORD dwStatusBarStyles = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | 
						  CCS_NODIVIDER | CCS_NOPARENTALIGN | CCS_NOMOVEY |	//| CCS_NORESIZE
						  SBT_NOBORDERS | SBARS_SIZEGRIP
						  ;


#define MAX_STATUS_PARTS 3

//
//	Process WM_MENUSELECT message to display menu-item hints in statusbar
//
int StatusBarMenuSelect(HWND hwnd, HWND hwndSB, WPARAM wParam, LPARAM lParam)
{
	UINT lookup[] = { 0, 0 };

	// Display helpful text in status bar
	MenuHelp(WM_MENUSELECT, wParam, lParam, GetMenu(hwnd), g_hResourceModule,
		hwndSB, (UINT *)lookup);

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

	parentwidth = r.right < 400 ? 400 : r.right;
	parts[0] = parentwidth - 250;
	parts[1] = parentwidth - 70;		
	parts[2] = parentwidth;//-1;

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
	
	hwndSB = CreateStatusWindow(dwStatusBarStyles, _T(""), hwndParent, 2);

	SetStatusBarParts(hwndSB);

	SetStatusBarText(hwndSB, 0, 1, _T(""));
	SetStatusBarText(hwndSB, 1, 0, _T(" Ln %d, Col %d"), 1, 1);
	SetStatusBarText(hwndSB, 2, 0, _T(" INS"));

	return hwndSB ;
}
