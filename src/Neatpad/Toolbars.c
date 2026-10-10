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


#define MAX_STATUS_PARTS 6

// room left after the text of each pane
#define STATUS_PANE_PADDING _T("MMM")

// width of each pane, fitted to its text (the message pane takes whatever is left)
static int g_nPaneWidth[MAX_STATUS_PARTS];
static HFONT g_hStatusFont;

// the text of each pane shown as a link (empty for the others), and its font
static TCHAR g_szLinkText[MAX_STATUS_PARTS][64];
static HFONT g_hLinkFont;

HFONT StatusBarFont(void)
{
	return g_hStatusFont;
}

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
//	indexing, line-format and encoding panes against the right edge, and the
//	message pane in between. Must be called whenever the statusbar changes size, so call each
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
						 g_nPaneWidth[STATUS_PART_INDEX]    + g_nPaneWidth[STATUS_PART_LINEFMT]  +
						 g_nPaneWidth[STATUS_PART_ENCODING]);

	parts[STATUS_PART_CURSOR]   = g_nPaneWidth[STATUS_PART_CURSOR];
	parts[STATUS_PART_DOCSTATS] = parts[STATUS_PART_CURSOR] + g_nPaneWidth[STATUS_PART_DOCSTATS];
	parts[STATUS_PART_ENCODING] = right;
	parts[STATUS_PART_LINEFMT]  = parts[STATUS_PART_ENCODING] - g_nPaneWidth[STATUS_PART_ENCODING];
	parts[STATUS_PART_INDEX]    = parts[STATUS_PART_LINEFMT]  - g_nPaneWidth[STATUS_PART_LINEFMT];
	parts[STATUS_PART_MESSAGE]  = parts[STATUS_PART_INDEX]    - g_nPaneWidth[STATUS_PART_INDEX];

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
//	Show a pane's text as a link: the parent draws it (DrawStatusBarLink), and it
//	is clicked like any pane (NM_CLICK). No text hides the pane.
//
void SetStatusBarLink(HWND hwndSB, UINT nPart, TCHAR *text)
{
	int width;

	if(nPart >= MAX_STATUS_PARTS)
		return;

	lstrcpyn(g_szLinkText[nPart], text, sizeof(g_szLinkText[nPart]) / sizeof(TCHAR));

	// an owner-drawn pane keeps a pointer to its text, for WM_DRAWITEM
	if(g_szLinkText[nPart][0])
		SendMessage(hwndSB, SB_SETTEXT, (WPARAM)(nPart | SBT_OWNERDRAW), (LPARAM)g_szLinkText[nPart]);
	else
		SendMessage(hwndSB, SB_SETTEXT, (WPARAM)nPart, (LPARAM)_T(""));

	width = StatusPaneWidth(hwndSB, nPart, g_szLinkText[nPart]);

	if(width != g_nPaneWidth[nPart])
	{
		g_nPaneWidth[nPart] = width;
		SetStatusBarParts(hwndSB);
	}
}

//
//	Draw a link pane, for the parent's WM_DRAWITEM: its text in the link colour,
//	underlined, starting where the other panes' text does
//
BOOL DrawStatusBarLink(const DRAWITEMSTRUCT *dis)
{
	TCHAR	*text  = (TCHAR *)dis->itemData;
	HFONT	 hFont = (HFONT)SendMessage(dis->hwndItem, WM_GETFONT, 0, 0);
	HANDLE	 hOldFont;
	RECT	 rect  = dis->rcItem;
	LOGFONT	 lf;
	SIZE	 space;

	if(text == 0)
		return FALSE;

	if(g_hLinkFont == 0 && hFont && GetObject(hFont, sizeof(lf), &lf))
	{
		lf.lfUnderline = TRUE;
		g_hLinkFont = CreateFontIndirect(&lf);
	}

	hOldFont = SelectObject(dis->hDC, g_hLinkFont ? g_hLinkFont : hFont);

	GetTextExtentPoint32(dis->hDC, _T(" "), 1, &space);
	rect.left += space.cx;

	SetTextColor(dis->hDC, GetSysColor(COLOR_HOTLIGHT));
	SetBkMode(dis->hDC, TRANSPARENT);
	DrawText(dis->hDC, text, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);

	SelectObject(dis->hDC, hOldFont);
	return TRUE;
}

//
//	Whether a point, in the statusbar's client coordinates, is on a link pane
//
BOOL StatusBarLinkAt(HWND hwndSB, POINT pt)
{
	RECT rect;
	UINT i;

	for(i = 0; i < MAX_STATUS_PARTS; i++)
	{
		if(g_szLinkText[i][0] && SendMessage(hwndSB, SB_GETRECT, i, (LPARAM)&rect) && PtInRect(&rect, pt))
			return TRUE;
	}

	return FALSE;
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

		if(g_hStatusFont == 0)
			g_hStatusFont = CreateFontIndirect(&statusFontDetails);

		if(g_hStatusFont)
			SendMessage(hwndSB, WM_SETFONT, (WPARAM)g_hStatusFont, TRUE);

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
