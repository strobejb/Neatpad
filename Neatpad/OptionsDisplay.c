//
//	Neatpad
//	OptionsDisplay.c
//
//	www.catch22.net
//

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tchar.h>
#include "Neatpad.h"
#include "resource.h"

//
//	Dialogbox procedure for the FONT pane
//
BOOL CALLBACK DisplayOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	PSHNOTIFY *pshn;

	switch(msg)
	{
	case WM_INITDIALOG:

		SendDlgItemMessage(hwnd, IDC_LONGLINEMODE, CB_ADDSTRING, 0, (LPARAM)_T("No Highlighting"));
		SendDlgItemMessage(hwnd, IDC_LONGLINEMODE, CB_ADDSTRING, 0, (LPARAM)_T("Highlight long lines"));

		SendDlgItemMessage(hwnd, IDC_LONGLINEMODE, CB_SETCURSEL, g_fLongLines, 0);

		SetDlgItemInt(hwnd, IDC_LONGLINELIM, g_nLongLineLimit, FALSE);
		EnableDlgItem(hwnd, IDC_LONGLINELIM, g_fLongLines);

		CheckDlgButton(hwnd, IDC_LINENOS,   g_fLineNumbers);
		CheckDlgButton(hwnd, IDC_SELMARGIN, g_fSelMargin);

		CheckDlgButton(hwnd, IDC_HIGHLIGHTCURLINE, g_nHLCurLine);

		return TRUE;

	case WM_CLOSE:
		return TRUE;

	case WM_NOTIFY:

		pshn = (PSHNOTIFY *)lParam;
		
		if(pshn->hdr.code == PSN_APPLY)
		{
			g_fLineNumbers	 = IsDlgButtonChecked(hwnd, IDC_LINENOS);
			g_fSelMargin	 = IsDlgButtonChecked(hwnd, IDC_SELMARGIN);
			g_fLongLines	 = SendDlgItemMessage(hwnd, IDC_LONGLINEMODE, CB_GETCURSEL, 0, 0);
			g_nLongLineLimit = GetDlgItemInt(hwnd, IDC_LONGLINELIM, 0, FALSE);
			g_nHLCurLine	 = IsDlgButtonChecked(hwnd, IDC_HIGHLIGHTCURLINE);

			return TRUE;
		}

		return FALSE;

	case WM_COMMAND:

		switch(LOWORD(wParam))
		{
		case IDC_LONGLINEMODE:
			
			if(HIWORD(wParam) == CBN_SELCHANGE)
			{
				int idx = SendDlgItemMessage(hwnd, IDC_LONGLINEMODE, CB_GETCURSEL, 0, 0);
				EnableDlgItem(hwnd, IDC_LONGLINELIM, idx);
			}

			return TRUE;

		case IDCANCEL:
			return TRUE;
		}

		return FALSE;
	}

	return FALSE;
}
