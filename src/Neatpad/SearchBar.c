//
//	Neatpad - Simple Text Editor application
//
//	Minimal search bar shown above the status bar.
//
#define _CRT_SECURE_NO_DEPRECATE
#define _WIN32_WINNT 0x501
#define STRICT

#include <windows.h>
#include <tchar.h>
#include <commctrl.h>
#include "Neatpad.h"
#include "resource.h"

extern HWND g_hwndSearchBar;

#define SEARCHBAR_PADX 8
#define SEARCHBAR_PADY 8
#define SEARCHBAR_EDIT_MAXWIDTH 320
#define SEARCHBAR_EDIT_MINWIDTH 120
#define SEARCHBAR_GAP 4
#define SEARCHBAR_ICON_SIZE 32
#define SEARCHBAR_EDIT_PADX 8

static WNDPROC g_OldSearchEditProc;
static int g_nSearchBarHeight;
static int g_nSearchButtonWidth;
static int g_nSearchControlHeight;
static HWND g_hwndOptionsTB;
static HWND g_hwndFindTB;
static HWND g_hwndCloseTB;
static HIMAGELIST g_hSearchImageList;

enum
{
	SEARCH_ICON_MENU,
	SEARCH_ICON_PREVIOUS,
	SEARCH_ICON_NEXT,
	SEARCH_ICON_CLOSE,
	SEARCH_ICON_COUNT
};

static HIMAGELIST CreateSearchImageList(void)
{
	if(g_hSearchImageList)
		return g_hSearchImageList;

	g_hSearchImageList = ImageList_LoadImage(g_hResourceModule,
		MAKEINTRESOURCE(IDB_SEARCHBAR_BUTTONS),
		SEARCHBAR_ICON_SIZE, 0, RGB(255, 0, 255),
		IMAGE_BITMAP, LR_CREATEDIBSECTION | LR_LOADTRANSPARENT);

	return g_hSearchImageList;
}

static HWND CreateSearchToolbar(HWND hwndParent, UINT id, TBBUTTON *buttons, int buttonCount)
{
	HWND hwndTB = CreateWindowEx(0, TOOLBARCLASSNAME, 0,
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS |
			CCS_NOPARENTALIGN | CCS_NORESIZE | CCS_NODIVIDER,
		0, 0, 0, 0, hwndParent, (HMENU)(UINT_PTR)id, GetModuleHandle(0), 0);

	SendMessage(hwndTB, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
	SendMessage(hwndTB, TB_SETIMAGELIST, 0, (LPARAM)CreateSearchImageList());
	SendMessage(hwndTB, TB_SETBITMAPSIZE, 0, MAKELPARAM(SEARCHBAR_ICON_SIZE, SEARCHBAR_ICON_SIZE));
	SendMessage(hwndTB, TB_SETBUTTONSIZE, 0, MAKELPARAM(g_nSearchButtonWidth, g_nSearchControlHeight));
	SendMessage(hwndTB, TB_ADDBUTTONS, buttonCount, (LPARAM)buttons);
	SendMessage(hwndTB, TB_AUTOSIZE, 0, 0);

	return hwndTB;
}

static void SearchBarInitMetrics(HWND hwnd)
{
	RECT rect;

	if(g_nSearchButtonWidth == 0)
	{
		GetWindowRect(GetDlgItem(hwnd, IDC_SEARCH_OPTIONS), &rect);
		g_nSearchButtonWidth = rect.right - rect.left;
	}

	if(g_nSearchControlHeight == 0)
	{
		GetWindowRect(GetDlgItem(hwnd, IDC_EDIT1), &rect);
		g_nSearchControlHeight = rect.bottom - rect.top;
	}
}

static void SetSearchEditPadding(HWND hwndEdit)
{
	RECT rect;
	HDC hdc;
	HFONT hOldFont;
	TEXTMETRIC tm;
	int textHeight;
	int y;

	GetClientRect(hwndEdit, &rect);
	hdc = GetDC(hwndEdit);
	hOldFont = SelectObject(hdc, (HFONT)SendMessage(hwndEdit, WM_GETFONT, 0, 0));
	GetTextMetrics(hdc, &tm);
	SelectObject(hdc, hOldFont);
	ReleaseDC(hwndEdit, hdc);

	textHeight = tm.tmHeight;
	y = (rect.bottom - rect.top - textHeight) / 2;
	if(y < 0)
		y = 0;

	rect.left += SEARCHBAR_EDIT_PADX;
	rect.right -= SEARCHBAR_EDIT_PADX;
	rect.top = y;
	rect.bottom = y + textHeight + 1;

	SendMessage(hwndEdit, EM_SETRECTNP, 0, (LPARAM)&rect);
	InvalidateRect(hwndEdit, 0, TRUE);
}

static void SearchBarLayout(HWND hwnd)
{
	RECT rc;
	int width;
	int height;
	int x;
	int y;
	int controlHeight;
	int editWidth;
	int maxEditWidth;

	GetClientRect(hwnd, &rc);
	width = rc.right - rc.left;
	height = rc.bottom - rc.top;
	x = SEARCHBAR_PADX;
	SearchBarInitMetrics(hwnd);
	controlHeight = g_nSearchControlHeight;
	y = (height - controlHeight) / 2;

	MoveWindow(g_hwndOptionsTB, x, y, g_nSearchButtonWidth, controlHeight, TRUE);
	x += g_nSearchButtonWidth + SEARCHBAR_GAP;

	maxEditWidth = width - SEARCHBAR_PADX * 2 - g_nSearchButtonWidth * 4 - SEARCHBAR_GAP * 5;
	if(maxEditWidth < 40)
		maxEditWidth = 40;

	editWidth = SEARCHBAR_EDIT_MAXWIDTH;
	if(editWidth > maxEditWidth)
		editWidth = maxEditWidth;
	if(editWidth < SEARCHBAR_EDIT_MINWIDTH && maxEditWidth >= SEARCHBAR_EDIT_MINWIDTH)
		editWidth = SEARCHBAR_EDIT_MINWIDTH;

	MoveWindow(GetDlgItem(hwnd, IDC_EDIT1), x, y, editWidth, controlHeight, TRUE);
	SetSearchEditPadding(GetDlgItem(hwnd, IDC_EDIT1));
	x += editWidth + SEARCHBAR_GAP;

	MoveWindow(g_hwndFindTB, x, y, g_nSearchButtonWidth * 2, controlHeight, TRUE);

	MoveWindow(g_hwndCloseTB, width - SEARCHBAR_PADX - g_nSearchButtonWidth, y, g_nSearchButtonWidth, controlHeight, TRUE);
}

static void PaintSearchBar(HWND hwnd)
{
	PAINTSTRUCT ps;
	RECT rc;
	HDC hdc = BeginPaint(hwnd, &ps);

	GetClientRect(hwnd, &rc);
	FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));

	rc.bottom = rc.top + 1;
	FillRect(hdc, &rc, GetSysColorBrush(COLOR_3DSHADOW));

	GetClientRect(hwnd, &rc);
	rc.top = rc.bottom - 1;
	FillRect(hdc, &rc, GetSysColorBrush(COLOR_3DHILIGHT));

	EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	LPTSTR text;
	LPTSTR clean;
	SIZE_T len;
	SIZE_T i;
	SIZE_T j;
	HGLOBAL hmem;

	switch(msg)
	{
	case WM_KEYDOWN:
		if(wParam == VK_ESCAPE)
		{
			HideSearchBar();
			return 0;
		}
		break;

	case WM_CHAR:
		if(wParam == VK_RETURN || wParam == _T('\n'))
			return 0;
		break;

	case WM_PASTE:
		if(!OpenClipboard(hwnd))
			return 0;

		hmem = GetClipboardData(CF_UNICODETEXT);
		if(hmem == 0)
		{
			CloseClipboard();
			return 0;
		}

		text = GlobalLock(hmem);
		if(text == 0)
		{
			CloseClipboard();
			return 0;
		}

		len = lstrlen(text);
		clean = (LPTSTR)GlobalAlloc(GPTR, (len + 1) * sizeof(TCHAR));
		if(clean)
		{
			for(i = 0, j = 0; i < len; i++)
			{
				if(text[i] == _T('\r') || text[i] == _T('\n'))
					break;

				clean[j++] = text[i];
			}

			clean[j] = 0;
			SendMessage(hwnd, EM_REPLACESEL, TRUE, (LPARAM)clean);
			GlobalFree(clean);
		}

		GlobalUnlock(hmem);
		CloseClipboard();
		return 0;
	}

	return CallWindowProc(g_OldSearchEditProc, hwnd, msg, wParam, lParam);
}

static INT_PTR CALLBACK SearchBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	HWND hwndEdit;
	TBBUTTON tbbOptions[] =
	{
		{ SEARCH_ICON_MENU, IDC_SEARCH_OPTIONS, TBSTATE_ENABLED, BTNS_BUTTON, { 0 }, 0, 0 }
	};
	TBBUTTON tbbFind[] =
	{
		{ SEARCH_ICON_PREVIOUS, IDC_SEARCH_PREVIOUS, TBSTATE_ENABLED, BTNS_BUTTON, { 0 }, 0, 0 },
		{ SEARCH_ICON_NEXT, IDC_SEARCH_NEXT, TBSTATE_ENABLED, BTNS_BUTTON, { 0 }, 0, 0 }
	};
	TBBUTTON tbbClose[] =
	{
		{ SEARCH_ICON_CLOSE, IDCANCEL, TBSTATE_ENABLED, BTNS_BUTTON, { 0 }, 0, 0 }
	};
	HFONT hStatusFont;

	switch(msg)
	{
	case WM_INITDIALOG:
		EnableDialogTheme(hwnd);
		InitCommonControls();
		SearchBarInitMetrics(hwnd);

		DestroyWindow(GetDlgItem(hwnd, IDC_SEARCH_OPTIONS));
		DestroyWindow(GetDlgItem(hwnd, IDC_SEARCH_PREVIOUS));
		DestroyWindow(GetDlgItem(hwnd, IDC_SEARCH_NEXT));
		DestroyWindow(GetDlgItem(hwnd, IDCANCEL));

		g_hwndOptionsTB = CreateSearchToolbar(hwnd, IDC_SEARCH_OPTIONS, tbbOptions, 1);
		g_hwndFindTB = CreateSearchToolbar(hwnd, IDC_SEARCH_PREVIOUS, tbbFind, 2);
		g_hwndCloseTB = CreateSearchToolbar(hwnd, IDCANCEL, tbbClose, 1);

		hwndEdit = GetDlgItem(hwnd, IDC_EDIT1);
		hStatusFont = StatusBarFont();
		if(hStatusFont)
			SendMessage(hwndEdit, WM_SETFONT, (WPARAM)hStatusFont, TRUE);
		else
			SendMessage(hwndEdit, WM_SETFONT, (WPARAM)SendMessage(hwnd, WM_GETFONT, 0, 0), TRUE);
		SendMessage(hwndEdit, EM_SETCUEBANNER, 0, (LPARAM)_T("Search"));
		SetSearchEditPadding(hwndEdit);

		g_OldSearchEditProc = (WNDPROC)GetWindowLongPtr(hwndEdit, GWLP_WNDPROC);
		SetWindowLongPtr(hwndEdit, GWLP_WNDPROC, (LONG_PTR)SearchEditProc);

		SearchBarLayout(hwnd);
		return TRUE;

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDCANCEL:
			HideSearchBar();
			return TRUE;
		}
		return FALSE;

	case WM_NOTIFY:
		if(((NMHDR *)lParam)->code == TBN_GETINFOTIP)
		{
			NMTBGETINFOTIP *tip = (NMTBGETINFOTIP *)lParam;

			switch(tip->iItem)
			{
			case IDC_SEARCH_OPTIONS:
				lstrcpyn(tip->pszText, _T("Search options"), tip->cchTextMax);
				return TRUE;
			case IDC_SEARCH_PREVIOUS:
				lstrcpyn(tip->pszText, _T("Find previous"), tip->cchTextMax);
				return TRUE;
			case IDC_SEARCH_NEXT:
				lstrcpyn(tip->pszText, _T("Find next"), tip->cchTextMax);
				return TRUE;
			case IDCANCEL:
				lstrcpyn(tip->pszText, _T("Close"), tip->cchTextMax);
				return TRUE;
			}
		}
		return FALSE;

	case WM_ERASEBKGND:
		return TRUE;

	case WM_PAINT:
		PaintSearchBar(hwnd);
		return TRUE;

	case WM_SIZE:
		SearchBarLayout(hwnd);
		return TRUE;

	case WM_DESTROY:
		if(g_hSearchImageList)
		{
			ImageList_Destroy(g_hSearchImageList);
			g_hSearchImageList = 0;
		}

		g_hwndSearchBar = 0;
		return TRUE;
	}

	return FALSE;
}

HWND CreateSearchBar(HWND hwndParent)
{
	if(g_hwndSearchBar == 0)
	{
		RECT rect;

		g_hwndSearchBar = CreateDialog(g_hResourceModule, MAKEINTRESOURCE(IDD_SEARCHBAR), hwndParent, SearchBarProc);
		GetWindowRect(g_hwndSearchBar, &rect);
		g_nSearchBarHeight = rect.bottom - rect.top;

		ShowWindow(g_hwndSearchBar, SW_HIDE);
	}

	return g_hwndSearchBar;
}

HWND ShowSearchBar(HWND hwndParent)
{
	HWND hwndEdit;

	if(g_hwndSearchBar == 0)
		CreateSearchBar(hwndParent);

	ShowWindow(g_hwndSearchBar, SW_SHOW);
	SendMessage(hwndParent, WM_SIZE, 0, 0);

	hwndEdit = GetDlgItem(g_hwndSearchBar, IDC_EDIT1);
	SetFocus(hwndEdit);
	SendMessage(hwndEdit, EM_SETSEL, 0, -1);

	return g_hwndSearchBar;
}

void HideSearchBar(void)
{
	if(g_hwndSearchBar && IsWindowVisible(g_hwndSearchBar))
	{
		HWND hwndParent = GetParent(g_hwndSearchBar);

		ShowWindow(g_hwndSearchBar, SW_HIDE);
		SendMessage(hwndParent, WM_SIZE, 0, 0);
		SetFocus(g_hwndTextView);
	}
}

int SearchBarHeight(void)
{
	if(g_nSearchBarHeight == 0 && g_hwndSearchBar)
	{
		RECT rect;
		GetWindowRect(g_hwndSearchBar, &rect);
		g_nSearchBarHeight = rect.bottom - rect.top;
	}

	return g_nSearchBarHeight;
}

BOOL SearchBarIsVisible(void)
{
	return g_hwndSearchBar && IsWindowVisible(g_hwndSearchBar);
}
