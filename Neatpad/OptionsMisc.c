//
//	Neatpad
//	OptionsMisc.c
//
//	Use the following registry key to replace 'notepad' with 'neatpad'
//
//		"HKLM\Software\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\Notepad"
//		REG_SZ "Debugger"="C:\path\Neatpad.exe"
//
//
//	www.catch22.net
//
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include "Neatpad.h"
#include "resource.h"

//
//	Dialogbox procedure for the FONT pane
//
BOOL CALLBACK MiscOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	PSHNOTIFY *pshn;

	switch(msg)
	{
	case WM_INITDIALOG:

		CheckDlgButton(hwnd, IDC_ADDCONTEXT, g_fAddToExplorerContextMenu);
		CheckDlgButton(hwnd, IDC_REPLACENOTEPAD, g_fReplaceNotepad);

		// disable 'replace notepad' option for Win9x
		EnableDlgItem(hwnd, IDC_REPLACENOTEPAD, GetVersion() & 0x80000000 ? FALSE : TRUE);
		return TRUE;

	case WM_CLOSE:
		return TRUE;

	case WM_NOTIFY:

		pshn = (PSHNOTIFY *)lParam;
		
		if(pshn->hdr.code == PSN_APPLY)
		{
			g_fAddToExplorerContextMenu = IsDlgButtonChecked(hwnd, IDC_ADDCONTEXT);
			g_fReplaceNotepad			= IsDlgButtonChecked(hwnd, IDC_REPLACENOTEPAD);
			return TRUE;
		}

		return FALSE;

	case WM_COMMAND:

		switch(LOWORD(wParam))
		{
		case IDCANCEL:
			return TRUE;
		}

		return FALSE;
	}

	return FALSE;
}
