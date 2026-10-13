//
//	Neatpad - Simple Text Editor application
//
//	Minimal search bar shown above the status bar. Ctrl+H shows a replace box
//	and its buttons in it too.
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

extern HWND g_hwndSearchBar;
extern HWND g_hwndStatusbar;

#define SEARCHBAR_PADX 8
#define SEARCHBAR_PADY 8
#define SEARCHBAR_EDIT_MINWIDTH 120
#define SEARCHBAR_GAP 4
#define SEARCHBAR_ICON_SIZE 32
#define SEARCHBAR_EDIT_PADX 8
#define IDM_SEARCH_MATCHCASE 50000
#define IDM_SEARCH_WRAP 50001

static WNDPROC g_OldSearchEditProc;
static int g_nSearchBarHeight;
static int g_nSearchButtonWidth;
static int g_nSearchControlHeight;
static int g_nSearchEditWidth;
static HWND g_hwndOptionsTB;
static HWND g_hwndFindTB;
static HWND g_hwndCloseTB;
static HIMAGELIST g_hSearchImageList;
static BOOL g_fReplacing;		// the replace box and its buttons are showing

static UINT SearchBarFind(BOOL backwards);
static void SearchBarReplace(BOOL fAll);
static void ShowSearchOptionsMenu(HWND hwnd);

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

	if(g_nSearchEditWidth == 0)
	{
		GetWindowRect(GetDlgItem(hwnd, IDC_EDIT1), &rect);
		g_nSearchEditWidth = rect.right - rect.left;
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

// "Search", or "Replace", in an empty box
static void PaintSearchEditHint(HWND hwndEdit)
{
	TCHAR *hint = GetDlgCtrlID(hwndEdit) == IDC_SEARCH_REPLACEWITH ? _T("Replace") : _T("Search");
	RECT rect;
	HDC hdc;
	HFONT hOldFont;

	if(GetWindowTextLength(hwndEdit) != 0)
		return;

	SendMessage(hwndEdit, EM_GETRECT, 0, (LPARAM)&rect);

	hdc = GetDC(hwndEdit);
	hOldFont = SelectObject(hdc, (HFONT)SendMessage(hwndEdit, WM_GETFONT, 0, 0));
	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
	DrawText(hdc, hint, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	SelectObject(hdc, hOldFont);
	ReleaseDC(hwndEdit, hdc);
}

static int ControlWidth(HWND hwndCtrl)
{
	RECT rect;

	GetWindowRect(hwndCtrl, &rect);
	return rect.right - rect.left;
}

static void SearchBarLayout(HWND hwnd)
{
	RECT rc;
	HWND hwndReplace = GetDlgItem(hwnd, IDC_SEARCH_REPLACE);
	HWND hwndReplaceAll = GetDlgItem(hwnd, IDC_SEARCH_REPLACEALL);
	int width;
	int height;
	int x;
	int y;
	int controlHeight;
	int editWidth;
	int maxEditWidth;
	int replaceButtons = 0;

	GetClientRect(hwnd, &rc);
	width = rc.right - rc.left;
	height = rc.bottom - rc.top;
	x = SEARCHBAR_PADX;
	SearchBarInitMetrics(hwnd);
	controlHeight = g_nSearchControlHeight;
	y = (height - controlHeight) / 2;

	MoveWindow(g_hwndOptionsTB, x, y, g_nSearchButtonWidth, controlHeight, TRUE);
	x += g_nSearchButtonWidth + SEARCHBAR_GAP;

	// replacing, the replace box shares the room with the search box, and its buttons take some
	if(g_fReplacing)
		replaceButtons = ControlWidth(hwndReplace) + ControlWidth(hwndReplaceAll) + SEARCHBAR_GAP * 3;

	maxEditWidth = width - SEARCHBAR_PADX * 2 - g_nSearchButtonWidth * 4 - SEARCHBAR_GAP * 5 - replaceButtons;
	if(g_fReplacing)
		maxEditWidth = (maxEditWidth - SEARCHBAR_GAP) / 2;
	if(maxEditWidth < 40)
		maxEditWidth = 40;

	editWidth = g_nSearchEditWidth;
	if(editWidth > maxEditWidth)
		editWidth = maxEditWidth;
	if(editWidth < SEARCHBAR_EDIT_MINWIDTH && maxEditWidth >= SEARCHBAR_EDIT_MINWIDTH)
		editWidth = SEARCHBAR_EDIT_MINWIDTH;

	MoveWindow(GetDlgItem(hwnd, IDC_EDIT1), x, y, editWidth, controlHeight, TRUE);
	SetSearchEditPadding(GetDlgItem(hwnd, IDC_EDIT1));
	x += editWidth + SEARCHBAR_GAP;

	MoveWindow(g_hwndFindTB, x, y, g_nSearchButtonWidth * 2, controlHeight, TRUE);
	x += g_nSearchButtonWidth * 2 + SEARCHBAR_GAP;

	if(g_fReplacing)
	{
		HWND hwndWith = GetDlgItem(hwnd, IDC_SEARCH_REPLACEWITH);

		MoveWindow(hwndWith, x, y, editWidth, controlHeight, TRUE);
		SetSearchEditPadding(hwndWith);
		x += editWidth + SEARCHBAR_GAP;

		MoveWindow(hwndReplace, x, y, ControlWidth(hwndReplace), controlHeight, TRUE);
		x += ControlWidth(hwndReplace) + SEARCHBAR_GAP;

		MoveWindow(hwndReplaceAll, x, y, ControlWidth(hwndReplaceAll), controlHeight, TRUE);
	}

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
	LRESULT retval;
	LPTSTR text;
	LPTSTR clean;
	SIZE_T len;
	SIZE_T i;
	SIZE_T j;
	HGLOBAL hmem;

	switch(msg)
	{
	case WM_PAINT:
		retval = CallWindowProc(g_OldSearchEditProc, hwnd, msg, wParam, lParam);
		PaintSearchEditHint(hwnd);
		return retval;

	case WM_KEYDOWN:
		if(wParam == VK_ESCAPE)
		{
			HideSearchBar();
			return 0;
		}
		// Enter in the replace box replaces; in the search box, it finds
		if(wParam == VK_RETURN)
		{
			if(GetDlgCtrlID(hwnd) == IDC_SEARCH_REPLACEWITH)
				SearchBarReplace(FALSE);
			else
				SearchBarFind(FALSE);

			return 0;
		}
		break;

	case WM_CHAR:
		if(wParam == VK_RETURN || wParam == _T('\n'))
			return 0;
		break;

	case WM_SETFOCUS:
	case WM_KILLFOCUS:
		retval = CallWindowProc(g_OldSearchEditProc, hwnd, msg, wParam, lParam);
		InvalidateRect(hwnd, 0, TRUE);
		return retval;

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

//
//	Show or hide the replace box and its buttons
//
static void SetReplacing(HWND hwnd, BOOL fReplacing)
{
	static const UINT controls[] = { IDC_SEARCH_REPLACEWITH, IDC_SEARCH_REPLACE, IDC_SEARCH_REPLACEALL };
	size_t i;

	g_fReplacing = fReplacing;

	for(i = 0; i < ARRAYSIZE(controls); i++)
		ShowWindow(GetDlgItem(hwnd, controls[i]), fReplacing ? SW_SHOW : SW_HIDE);

	SearchBarLayout(hwnd);
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
		if(hStatusFont == 0)
			hStatusFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
		SendMessage(hwndEdit, WM_SETFONT, (WPARAM)hStatusFont, TRUE);
		SendMessage(hwndEdit, EM_SETCUEBANNER, 0, (LPARAM)_T("Search"));
		SetSearchEditPadding(hwndEdit);

		g_OldSearchEditProc = (WNDPROC)GetWindowLongPtr(hwndEdit, GWLP_WNDPROC);
		SetWindowLongPtr(hwndEdit, GWLP_WNDPROC, (LONG_PTR)SearchEditProc);

		// the replace box works as the search box does (both are edit controls,
		// so they share its window procedure), and its buttons wait for Ctrl+H
		hwndEdit = GetDlgItem(hwnd, IDC_SEARCH_REPLACEWITH);
		SendMessage(hwndEdit, WM_SETFONT, (WPARAM)hStatusFont, TRUE);
		SetWindowLongPtr(hwndEdit, GWLP_WNDPROC, (LONG_PTR)SearchEditProc);
		SendDlgItemMessage(hwnd, IDC_SEARCH_REPLACE,	WM_SETFONT, (WPARAM)hStatusFont, TRUE);
		SendDlgItemMessage(hwnd, IDC_SEARCH_REPLACEALL, WM_SETFONT, (WPARAM)hStatusFont, TRUE);

		SetReplacing(hwnd, FALSE);
		return TRUE;

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDC_SEARCH_OPTIONS:
			ShowSearchOptionsMenu(hwnd);
			return TRUE;

		case IDC_SEARCH_PREVIOUS:
			SearchBarFind(TRUE);
			return TRUE;

		case IDC_SEARCH_NEXT:
			SearchBarFind(FALSE);
			return TRUE;

		case IDC_SEARCH_REPLACE:
			SearchBarReplace(FALSE);
			return TRUE;

		case IDC_SEARCH_REPLACEALL:
			SearchBarReplace(TRUE);
			return TRUE;

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

//
//	Ctrl+F and Ctrl+H: show the search bar, without or with the replace box -
//	or hide it, if it's showing that way already
//
void ToggleSearchBar(HWND hwndParent, BOOL fReplace)
{
	if(SearchBarIsVisible() && g_fReplacing == fReplace)
	{
		HideSearchBar();
		return;
	}

	ShowSearchBar(hwndParent);
	SetReplacing(g_hwndSearchBar, fReplace);
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

static void ShowSearchOptionsMenu(HWND hwnd)
{
	HMENU hmenu;
	RECT rect;
	UINT cmd;

	hmenu = CreatePopupMenu();
	if(hmenu == 0)
		return;

	AppendMenu(hmenu, MF_STRING | (g_fSearchMatchCase ? MF_CHECKED : MF_UNCHECKED), IDM_SEARCH_MATCHCASE, _T("Match case"));
	AppendMenu(hmenu, MF_STRING | (g_fSearchWrap ? MF_CHECKED : MF_UNCHECKED), IDM_SEARCH_WRAP, _T("Wrap around"));

	GetWindowRect(g_hwndOptionsTB, &rect);
	cmd = TrackPopupMenu(hmenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, rect.left, rect.bottom, 0, hwnd, 0);
	DestroyMenu(hmenu);

	switch(cmd)
	{
	case IDM_SEARCH_MATCHCASE:
		g_fSearchMatchCase = !g_fSearchMatchCase;
		SaveRegSettings();
		break;

	case IDM_SEARCH_WRAP:
		g_fSearchWrap = !g_fSearchWrap;
		SaveRegSettings();
		break;
	}
}

static UINT SearchBarFind(BOOL backwards)
{
	HWND hwndEdit;
	TCHAR text[1024];
	TEXTVIEWFIND find;
	UINT result;

	if(g_hwndSearchBar == 0)
		return TVFR_NOTFOUND;

	hwndEdit = GetDlgItem(g_hwndSearchBar, IDC_EDIT1);
	GetWindowText(hwndEdit, text, ARRAYSIZE(text));

	if(text[0] == 0)
		return TVFR_NOTFOUND;

	find.text = text;
	find.length = lstrlen(text);
	find.backwards = backwards;
	find.wrap = g_fSearchWrap;
	find.matchcase = g_fSearchMatchCase;

	result = TextView_FindText(g_hwndTextView, &find);
	if(result == TVFR_NOTFOUND)
		MessageBeep(MB_ICONASTERISK);

	SetFocus(hwndEdit);
	SendMessage(hwndEdit, EM_SETSEL, 0, -1);
	return result;
}

UINT SearchBarFindNext(HWND hwndParent)
{
	if(!SearchBarIsVisible())
		ShowSearchBar(hwndParent);

	return SearchBarFind(FALSE);
}

UINT SearchBarFindPrevious(HWND hwndParent)
{
	if(!SearchBarIsVisible())
		ShowSearchBar(hwndParent);

	return SearchBarFind(TRUE);
}

//
//	Replace the match the search selected and select the next one, keeping the
//	focus where it is - or, with fAll, replace every match, and say how many
//
static void SearchBarReplace(BOOL fAll)
{
	HWND hwndFocus = GetFocus();
	TCHAR text[1024];
	TCHAR with[1024];
	TCHAR count[32];
	TEXTVIEWFIND find;
	size_t replaced;

	GetDlgItemText(g_hwndSearchBar, IDC_EDIT1, text, ARRAYSIZE(text));
	GetDlgItemText(g_hwndSearchBar, IDC_SEARCH_REPLACEWITH, with, ARRAYSIZE(with));

	if(text[0] == 0)
		return;

	find.text = text;
	find.length = lstrlen(text);
	find.backwards = FALSE;
	find.wrap = g_fSearchWrap;
	find.matchcase = g_fSearchMatchCase;
	find.replace = with;
	find.replacelength = lstrlen(with);

	replaced = TextView_Replace(g_hwndTextView, &find, fAll);

	if(fAll)
	{
		if(replaced == 0)
			MessageBeep(MB_ICONASTERISK);

		FormatCount(count, ARRAYSIZE(count), replaced);
		SetStatusBarText(g_hwndStatusbar, STATUS_PART_MESSAGE, 0, _T(" Replaced %s %s"), count, replaced == 1 ? _T("match") : _T("matches"));
		return;
	}

	SearchBarFind(FALSE);
	SetFocus(hwndFocus);
}
