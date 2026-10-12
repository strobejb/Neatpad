//
//	Neatpad - Simple Text Editor application
//
//	The notification bar above the text, saying that another program has
//	changed or deleted the open file. As high as the search bar, and laid out
//	the same way: the message on the left, and its buttons on the right.
//
#define _CRT_SECURE_NO_DEPRECATE
#define _WIN32_WINNT 0x501
#define STRICT

#include <windows.h>
#include <tchar.h>
#include <commctrl.h>
#include "..\TextView\TextView.h"
#include "Neatpad.h"
#include "resource.h"

extern HWND g_hwndNotifyBar;

#define NOTIFYBAR_PADX	8
#define NOTIFYBAR_GAP	4

// A light, dusty orange, to stand out from the text and the search bar
#define NOTIFYBAR_COLOUR	RGB(244, 226, 200)
#define NOTIFYBAR_LINE		RGB(222, 196, 160)		// along the bottom
#define NOTIFYBAR_TEXT		RGB(70, 50, 30)

// The search bar's close button: the fourth of its 32-pixel icons
#define CLOSE_ICON		3
#define CLOSE_ICON_SIZE	32

static HWND		  g_hwndCloseTB;
static HIMAGELIST g_hCloseImages;
static HBRUSH	  g_hBarBrush;
static int		  g_nNotifyBarHeight;
static int		  g_nTextLeft;			// where the dialog template puts the message

//
//	Put a flat toolbar button with the search bar's close icon in place of the
//	dialog's placeholder, the same size
//
static void CreateCloseButton(HWND hwnd)
{
	HWND	 hwndPlaceholder = GetDlgItem(hwnd, IDCANCEL);
	TBBUTTON button = { CLOSE_ICON, IDCANCEL, TBSTATE_ENABLED, BTNS_BUTTON, { 0 }, 0, 0 };
	RECT	 rect;
	int		 width, height;

	GetWindowRect(hwndPlaceholder, &rect);
	DestroyWindow(hwndPlaceholder);

	width  = rect.right - rect.left;
	height = rect.bottom - rect.top;

	g_hCloseImages = ImageList_LoadImage(g_hResourceModule, MAKEINTRESOURCE(IDB_SEARCHBAR_BUTTONS),
		CLOSE_ICON_SIZE, 0, RGB(255, 0, 255), IMAGE_BITMAP, LR_CREATEDIBSECTION | LR_LOADTRANSPARENT);

	g_hwndCloseTB = CreateWindowEx(0, TOOLBARCLASSNAME, 0,
		WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NOPARENTALIGN | CCS_NORESIZE | CCS_NODIVIDER,
		0, 0, width, height, hwnd, (HMENU)IDCANCEL, GetModuleHandle(0), 0);

	SendMessage(g_hwndCloseTB, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
	SendMessage(g_hwndCloseTB, TB_SETIMAGELIST, 0, (LPARAM)g_hCloseImages);
	SendMessage(g_hwndCloseTB, TB_SETBITMAPSIZE, 0, MAKELPARAM(CLOSE_ICON_SIZE, CLOSE_ICON_SIZE));
	SendMessage(g_hwndCloseTB, TB_SETBUTTONSIZE, 0, MAKELPARAM(width, height));
	SendMessage(g_hwndCloseTB, TB_ADDBUTTONS, 1, (LPARAM)&button);
}

//
//	Move a control across to x, centred down the bar and keeping its size
//
static void PlaceControl(HWND hwndCtrl, int x, int width, int barHeight)
{
	RECT rect;

	GetWindowRect(hwndCtrl, &rect);
	MoveWindow(hwndCtrl, x, (barHeight - (rect.bottom - rect.top)) / 2, width, rect.bottom - rect.top, TRUE);
}

//
//	The close button against the right edge, Reload before it, and the message
//	in the room left, from where the dialog template starts it
//
static void NotifyBarLayout(HWND hwnd)
{
	HWND hwndReload = GetDlgItem(hwnd, IDC_NOTIFY_RELOAD);
	RECT bar, close, reload;
	int  x;

	// sized as the dialog is made, before it has its controls
	if(g_hwndCloseTB == 0)
		return;

	GetClientRect(hwnd, &bar);
	GetWindowRect(g_hwndCloseTB, &close);
	GetWindowRect(hwndReload, &reload);

	x = bar.right - NOTIFYBAR_PADX - (close.right - close.left);
	PlaceControl(g_hwndCloseTB, x, close.right - close.left, bar.bottom);

	x -= NOTIFYBAR_GAP + (reload.right - reload.left);
	PlaceControl(hwndReload, x, reload.right - reload.left, bar.bottom);

	PlaceControl(GetDlgItem(hwnd, IDC_NOTIFY_TEXT), g_nTextLeft, max(0, x - NOTIFYBAR_GAP - g_nTextLeft), bar.bottom);
}

//
//	The bar's background, with a line along the bottom between it and the
//	text. The buttons ask for it too, to draw around themselves.
//
static void PaintNotifyBar(HWND hwnd, HDC hdc)
{
	RECT rc;

	GetClientRect(hwnd, &rc);
	FillRect(hdc, &rc, g_hBarBrush);

	rc.top = rc.bottom - 1;
	SetBkColor(hdc, NOTIFYBAR_LINE);
	ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rc, 0, 0, 0);
}

static INT_PTR CALLBACK NotifyBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	HFONT hFont;
	RECT  rect;

	switch(msg)
	{
	case WM_INITDIALOG:
		g_hBarBrush = CreateSolidBrush(NOTIFYBAR_COLOUR);
		CreateCloseButton(hwnd);
		hFont = StatusBarFont();

		GetWindowRect(GetDlgItem(hwnd, IDC_NOTIFY_TEXT), &rect);
		MapWindowPoints(0, hwnd, (POINT *)&rect, 2);
		g_nTextLeft = rect.left;

		// the message and its button in the status bar's font, as the search bar's text is
		if(hFont)
		{
			SendDlgItemMessage(hwnd, IDC_NOTIFY_TEXT,	WM_SETFONT, (WPARAM)hFont, TRUE);
			SendDlgItemMessage(hwnd, IDC_NOTIFY_RELOAD, WM_SETFONT, (WPARAM)hFont, TRUE);
		}

		NotifyBarLayout(hwnd);
		return TRUE;

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDC_NOTIFY_RELOAD:
			HideNotifyBar();
			NeatpadReloadFile(GetParent(hwnd));
			return TRUE;

		case IDCANCEL:
			HideNotifyBar();
			return TRUE;
		}
		return FALSE;

	case WM_NOTIFY:
		if(((NMHDR *)lParam)->code == TBN_GETINFOTIP && ((NMTBGETINFOTIP *)lParam)->iItem == IDCANCEL)
		{
			NMTBGETINFOTIP *tip = (NMTBGETINFOTIP *)lParam;

			lstrcpyn(tip->pszText, _T("Close"), tip->cchTextMax);
			return TRUE;
		}
		return FALSE;

	// the message, and the corners around the button, in the bar's colour
	case WM_CTLCOLORSTATIC:
		SetBkColor((HDC)wParam, NOTIFYBAR_COLOUR);
		SetTextColor((HDC)wParam, NOTIFYBAR_TEXT);
		return (INT_PTR)g_hBarBrush;

	case WM_CTLCOLORBTN:
		return (INT_PTR)g_hBarBrush;

	// asked by the buttons too: tell them it's painted
	case WM_ERASEBKGND:
		PaintNotifyBar(hwnd, (HDC)wParam);
		SetWindowLongPtr(hwnd, DWLP_MSGRESULT, TRUE);
		return TRUE;

	case WM_SIZE:
		NotifyBarLayout(hwnd);
		return TRUE;

	case WM_DESTROY:
		ImageList_Destroy(g_hCloseImages);
		DeleteObject(g_hBarBrush);
		g_hCloseImages = 0;
		g_hBarBrush = 0;
		g_hwndNotifyBar = 0;
		return TRUE;
	}

	return FALSE;
}

HWND CreateNotifyBar(HWND hwndParent)
{
	RECT rect;

	g_hwndNotifyBar = CreateDialog(g_hResourceModule, MAKEINTRESOURCE(IDD_NOTIFYBAR), hwndParent, NotifyBarProc);

	GetWindowRect(g_hwndNotifyBar, &rect);
	g_nNotifyBarHeight = rect.bottom - rect.top;

	ShowWindow(g_hwndNotifyBar, SW_HIDE);
	return g_hwndNotifyBar;
}

//
//	Say what has happened to the open file: NOTIFY_CHANGED offers to reload it
//
void ShowNotifyBar(UINT nNotice)
{
	SetDlgItemText(g_hwndNotifyBar, IDC_NOTIFY_TEXT, nNotice == NOTIFY_CHANGED ?
		_T("This file has been changed by another program.") :
		_T("This file has been deleted or moved by another program."));

	ShowWindow(GetDlgItem(g_hwndNotifyBar, IDC_NOTIFY_RELOAD), nNotice == NOTIFY_CHANGED ? SW_SHOW : SW_HIDE);

	if(!IsWindowVisible(g_hwndNotifyBar))
	{
		ShowWindow(g_hwndNotifyBar, SW_SHOW);
		SendMessage(GetParent(g_hwndNotifyBar), WM_SIZE, 0, 0);
	}
}

void HideNotifyBar(void)
{
	if(g_hwndNotifyBar && IsWindowVisible(g_hwndNotifyBar))
	{
		ShowWindow(g_hwndNotifyBar, SW_HIDE);
		SendMessage(GetParent(g_hwndNotifyBar), WM_SIZE, 0, 0);
		SetFocus(g_hwndTextView);
	}
}

int NotifyBarHeight(void)
{
	return g_nNotifyBarHeight;
}

BOOL NotifyBarIsVisible(void)
{
	return g_hwndNotifyBar && IsWindowVisible(g_hwndNotifyBar);
}
