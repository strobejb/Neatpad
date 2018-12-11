//
//	Neatpad - Simple Text Editor application 
//
//	www.catch22.net
//	Written by J Brown 2004
//
//	Freeware
//
#define _CRT_SECURE_NO_DEPRECATE
#define _WIN32_WINNT 0x501
#define STRICT

#include <windows.h>
#include <tchar.h>
#include <commctrl.h>
#include "Neatpad.h"
#include "..\TextView\TextView.h"
#include "resource.h"

extern HWND g_hwndSearchDlg;
extern HWND g_hwndSearchBar;

#define MAX_FIND_PANES 3
#define FINDBORDER 6

#define SWP_SIZEONLY  (SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)
#define SWP_MOVEONLY  (SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)
#define SWP_ZONLY     (SWP_NOSIZE | SWP_NOMOVE   | SWP_NOACTIVATE)
#define SWP_SHOWONLY  (SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE | SWP_SHOWWINDOW)
#define SWP_HIDEONLY  (SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE | SWP_HIDEWINDOW)


HWND g_hwndFindPane[MAX_FIND_PANES];

BOOL CALLBACK FindHexDlg(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg)
	{
	case WM_INITDIALOG:
		EnableDialogTheme(hwnd);
		return FALSE;

	case WM_COMMAND:

		switch(LOWORD(wParam))
		{
		case IDCANCEL:
			DestroyWindow(GetParent(hwnd));
			return TRUE;
		}

		if(HIWORD(wParam) == BN_CLICKED)
		{
			//g_fFindInSelection = IsDlgButtonChecked(hwnd, IDC_SCOPE_SEL);
			//g_fSearchBackwards = IsDlgButtonChecked(hwnd, IDC_SEARCHBACK);
			//g_fKeepVisible     = IsDlgButtonChecked(hwnd, IDC_KEEPVISIBLE);
			return TRUE;
		}

		return FALSE;

	case WM_CLOSE:
		DestroyWindow(GetParent(hwnd));
		return TRUE;
	}
	return FALSE;
}


void AddSearchTabs(HWND hwnd)
{
	TCITEM tcitem;
	RECT rect;
	int i;

	HWND hwndTab = GetDlgItem(hwnd, IDC_TAB1);

	tcitem.mask = TCIF_TEXT;
	tcitem.pszText = _T("Find");
	TabCtrl_InsertItem(hwndTab, 0, &tcitem);

	tcitem.mask = TCIF_TEXT;
	tcitem.pszText = _T("Replace");
	TabCtrl_InsertItem(hwndTab, 1, &tcitem);

	tcitem.mask = TCIF_TEXT;
	tcitem.pszText = _T("Goto");
	TabCtrl_InsertItem(hwndTab, 2, &tcitem);

	///tcitem.mask = TCIF_TEXT;
	///tcitem.pszText = _T("Replace");
	//TabCtrl_InsertItem(hwndTab, 3, &tcitem);

	//	GetClient
//	TabCtrl_GetItemRect(hwndTab, 0, &rect);

	//for(i = MAX_FIND_PANES-1; i >= 0; i--)
	//{
	
	g_hwndFindPane[0] = CreateDialog(g_hResourceModule, MAKEINTRESOURCE(IDD_FINDPANE), hwnd, FindHexDlg);
	g_hwndFindPane[1] = CreateDialog(g_hResourceModule, MAKEINTRESOURCE(IDD_REPLACEPANE), hwnd, FindHexDlg);
	g_hwndFindPane[2] = CreateDialog(g_hResourceModule, MAKEINTRESOURCE(IDD_GOTOPANE), hwnd, FindHexDlg);
	
	//ShowWindow(g_hwndFindPane[0], SW_SHOW);
	//}

	// work out how big tab control needs to be to hold the pane
	//for(i = 0; i < MAX_FIND_PANES; i++)
	i = 0;
	{
		GetClientRect(g_hwndFindPane[i], &rect);
		MapWindowPoints(g_hwndFindPane[i], hwnd, (POINT *)&rect, 2);
		TabCtrl_AdjustRect(hwndTab, TRUE, &rect);
		
	//	break;
	}

	// move tab control into position
	MoveWindow(hwndTab, FINDBORDER, FINDBORDER, rect.right-rect.left, rect.bottom-rect.top, FALSE);

	// adjust the find dialog size
	AdjustWindowRectEx(&rect, GetWindowLong(hwnd, GWL_STYLE), FALSE, GetWindowLong(hwnd, GWL_EXSTYLE));
	InflateRect(&rect, FINDBORDER, FINDBORDER);
	SetWindowPos(hwnd, 0, 0, 0, rect.right-rect.left, rect.bottom-rect.top-2, SWP_SIZEONLY);

	// now find out the tab control's client display area
	GetWindowRect(hwndTab, &rect);
	MapWindowPoints(0, hwnd, (POINT *)&rect, 2);
	TabCtrl_AdjustRect(hwndTab, FALSE, &rect);

	// move find pane into position
	for(i = 0; i < MAX_FIND_PANES; i++)
	{
		MoveWindow(g_hwndFindPane[i], rect.left, rect.top, rect.right-rect.left, rect.bottom-rect.top, FALSE);
	}
	
//	ShowWindow(g_hwndFindPane[0], SW_SHOW);
}

BOOL CALLBACK SearchDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	NMHDR *nmhdr;
	static BOOL fMouseDown = FALSE;

	switch(msg)
	{
	case WM_INITDIALOG:
		AddSearchTabs(hwnd);
		return FALSE;

	case WM_NOTIFY:
		nmhdr = (NMHDR *)lParam;

		if(nmhdr->code == TCN_SELCHANGE)
		{
			int i;
			int idx = TabCtrl_GetCurSel(nmhdr->hwndFrom);
			HWND hwndPanel;

			for(i = 0; i < MAX_FIND_PANES; i++)				
			{
				if(i != idx)
				{
					//DelStyle(g_hwndFindPane[i], WS_VISIBLE);
					ShowWindow(g_hwndFindPane[i], SW_HIDE);
				}
			}

			hwndPanel = g_hwndFindPane[idx];

			SetWindowPos(g_hwndFindPane[idx], HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE|SWP_NOMOVE|SWP_SHOWWINDOW);

			if(fMouseDown)
			{
				SetFocus(GetDlgItem(hwndPanel, IDC_COMBO1));
				PostMessage(hwndPanel, WM_NEXTDLGCTL, IDC_COMBO1, TRUE);
			}

			return TRUE;
		}
		else if(nmhdr->code == TCN_SELCHANGING)
		{
			fMouseDown = (GetKeyState(VK_LBUTTON) & 0x80000000) ? TRUE : FALSE;
		}
		else if(nmhdr->code == NM_RELEASEDCAPTURE)
		{
			fMouseDown = FALSE;
		}
		break;

	case WM_COMMAND:

		switch(LOWORD(wParam))
		{
		case IDCANCEL:
			DestroyWindow(hwnd);
			return TRUE;
		}

		return TRUE;

	case WM_CLOSE:
		DestroyWindow(hwnd);
		return TRUE;

	case WM_DESTROY:
		g_hwndSearchDlg = 0;
		return TRUE;
	}
	return FALSE;
}

//
//	Show the find/replace/goto dialog
//
HWND ShowFindDlg(HWND hwndParent, UINT nPage)
{
	HWND	hwndTab;
	NMHDR	nmhdr;

	//
	//	Create the dialog if it hasn't been already
	//		
	if(g_hwndSearchDlg == 0)
	{
		g_hwndSearchDlg = CreateDialog(g_hResourceModule, MAKEINTRESOURCE(IDD_FIND), hwndParent, SearchDlgProc);

			
		CenterWindow(g_hwndSearchDlg);
		ShowWindow(g_hwndSearchDlg, SW_SHOW);
	}

	SetForegroundWindow(g_hwndSearchDlg);

	// 
	//	Simulate the user clicking one of the TABs in order to
	//	set the desired page
	//
	hwndTab = GetDlgItem(g_hwndSearchDlg, IDC_TAB1);
	nmhdr.hwndFrom	= hwndTab;
	nmhdr.code		= TCN_SELCHANGE;
	nmhdr.idFrom	= IDC_TAB1;

	TabCtrl_SetCurSel(hwndTab, nPage);
	SendMessage(g_hwndSearchDlg, WM_NOTIFY, IDC_TAB1, (LPARAM)&nmhdr);

	//	Set focus to 1st control in dialog
	SetFocus(GetDlgItem(g_hwndFindPane[nPage], IDC_COMBO1));
	PostMessage(g_hwndFindPane[nPage], WM_NEXTDLGCTL, IDC_COMBO1, TRUE);

	return g_hwndSearchDlg;
}

/*
void AddBlankSpace(HWND hwndTB, int width)
{
	TBBUTTON tbb = { width, 0, TBSTATE_ENABLED, TBSTYLE_SEP, 0, 0,0, 0 };
	SendMessage(hwndTB, TB_ADDBUTTONS,  1, (LPARAM) &tbb);
}


void AddButton(HWND hwndTB, UINT uCmdId, UINT uImageIdx, UINT uStyle, TCHAR *szText)
{
	//uStyle |= BTNS_SHOWTEXT;
	TBBUTTON tbb = { uImageIdx, uCmdId, TBSTATE_ENABLED, uStyle|BTNS_SHOWTEXT, 0, 0,0, (INT_PTR)szText };

	//SendMessage(hwndTB, TB_ADDSTRING, 0, (LPARAM)"Hello\0");
	
	SendMessage(hwndTB, TB_ADDBUTTONS, 1, (LPARAM) &tbb);
}

static DWORD TOOLBAR_STYLE =WS_CHILD | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | 
						 WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VISIBLE | 
						CCS_NOPARENTALIGN | 
						 CCS_NORESIZE|
						 CCS_NODIVIDER;

HWND CreateEmptyToolbar(HWND hwndParent, int nBitmapIdx, int nBitmapWidth, int nCtrlId, DWORD dwExtraStyle)
{
	HWND	   hwndTB;
	HIMAGELIST hImgList;
	
	hwndTB = CreateToolbarEx (hwndParent,
			TOOLBAR_STYLE|dwExtraStyle,
			nCtrlId, 0,
			0,
			0,
			NULL,
			0,
			0, 0, 0, 0,
			sizeof(TBBUTTON) );

	hImgList = ImageList_LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(nBitmapIdx), 
									nBitmapWidth, 16, RGB(255,0,255), 
									IMAGE_BITMAP, LR_CREATEDIBSECTION);

	SendMessage(hwndTB, TB_SETIMAGELIST, 0, (LPARAM)hImgList);
	return hwndTB;
}


LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	WNDPROC oldproc = (WNDPROC)GetWindowLong(hwnd, GWL_USERDATA);
	RECT *prect, rect;
	LRESULT retval;

	static HICON hIcon;
	HDC hdc;
	
	if(hIcon == 0)
	{
		hIcon = LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDI_ICON4), 
				IMAGE_ICON,16,16,LR_CREATEDIBSECTION);
	}

	switch(msg)
	{
	case WM_NCCALCSIZE:
		prect = (RECT *)lParam;

		retval = CallWindowProc(oldproc, hwnd, msg, wParam, lParam);
		prect->right -= 16;
		return retval; 

	case WM_NCPAINT:

		retval = CallWindowProc(oldproc, hwnd, msg, wParam, lParam);

		GetWindowRect(hwnd, &rect);
		OffsetRect(&rect, -rect.left, -rect.top);
	
		if(wParam <= 1)
			hdc = GetWindowDC(hwnd);
		else
			hdc = (HDC)wParam;
	
		rect.left = rect.right - 18;
		rect.right -= 2;
		rect.top += 2;
		rect.bottom -= 2;
		FillRect(hdc, &rect, GetSysColorBrush(COLOR_WINDOW));
		DrawIconEx(hdc, rect.left, rect.top + (rect.bottom-rect.top-16)/2, hIcon, 16, 16, 0, 0, DI_NORMAL);

		if(wParam <= 1)
			ReleaseDC(hwnd, hdc);

		return retval;

	}

	return CallWindowProc(oldproc, hwnd, msg, wParam, lParam);
}

BOOL CALLBACK SearchBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	NMHDR *nmhdr;
	HTHEME hTheme;
	RECT rect;
	static HWND hwndTB;
	HWND hwndEdit;
	WNDPROC oldproc;

	switch(msg)
	{
	case WM_INITDIALOG:
		//CreateWindowEx(0, WC_TOOLBAR, 0, WS_VISIBLE|WS_CHILD,
		InitCommonControls();

		GetWindowRect(GetDlgItem(hwnd, IDC_EDIT1), &rect);
		ScreenToClient(hwnd, (POINT *)&rect.left);
		ScreenToClient(hwnd, (POINT *)&rect.right);

		hwndTB = //CreateWindowEx(0, TOOLBARCLASSNAME,0,WS_VISIBLE|WS_CHILD|TBSTYLE_FLAT|CCS_NORESIZE|CCS_NODIVIDER,
			CreateEmptyToolbar(hwnd, IDB_BITMAP3, 18, 0, TBSTYLE_LIST);

		SendMessage(hwndTB, WM_SETFONT, (WPARAM)SendMessage(hwnd, WM_GETFONT, 0, 0), 0);

		MoveWindow(hwndTB, rect.right + 8, rect.top, 400, rect.bottom-rect.top, TRUE);

		SendMessage(hwndTB, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS|TBSTYLE_EX_DRAWDDARROWS );

		AddButton(hwndTB, 0, 0, TBSTYLE_BUTTON, _T("Find &Next"));
		AddButton(hwndTB, 0, 1, TBSTYLE_BUTTON, _T("Find &Previous"));
		AddButton(hwndTB, 0, 3, TBSTYLE_BUTTON|TBSTYLE_CHECK, _T("Highlight &all"));

		hwndEdit = GetDlgItem(hwnd, IDC_EDIT1);
		oldproc = (WNDPROC)GetWindowLong(hwndEdit, GWL_WNDPROC);
		SetWindowLong(hwndEdit, GWL_USERDATA, (LONG)oldproc);
		SetWindowLong(hwndEdit, GWL_WNDPROC, (LONG)EditSubclassProc);

		SetWindowPos(hwndEdit, 0, 0, 0, 0, 0, SWP_NOSIZE|SWP_NOMOVE|SWP_FRAMECHANGED);

		return TRUE;

	default:
		return FALSE;
	}
}

HWND ShowSearchBar(HWND hwndParent)
{
	if(g_hwndSearchBar == 0)
	{
		g_hwndSearchBar = CreateDialog(g_hResourceModule, MAKEINTRESOURCE(IDD_SEARCHBAR), hwndParent, SearchBarProc);
	}

	return g_hwndSearchBar;
}*/