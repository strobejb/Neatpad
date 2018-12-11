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

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <tchar.h>
#include <commctrl.h>
#include <shellapi.h>
#include "Neatpad.h"
#include "resource.h"

BOOL ElevateToAdmin(HWND hwnd, BOOL fChecked1, BOOL fChecked2)//TCHAR *szParams)
{
	//// http://codefromthe70s.org/vistatutorial.asp
	SHELLEXECUTEINFO sei = { sizeof(sei) };
	TCHAR szFile[MAX_PATH];
	TCHAR szParams[32];
	
	BOOL  success = FALSE;

	wsprintf(szParams, _T("-uac %d %d"), fChecked1, fChecked2);

	GetModuleFileName(0, szFile, MAX_PATH);

	sei.hwnd			= hwnd;
	sei.lpVerb			= L"runas";
	sei.lpFile			= szFile;
	sei.lpParameters	= szParams;//L"oof";
	sei.fMask			= SEE_MASK_NOCLOSEPROCESS;
	sei.nShow			= SW_SHOWNORMAL;

	if(ShellExecuteEx(&sei))
	{
		WaitForSingleObject(sei.hProcess, INFINITE);
		GetExitCodeProcess(sei.hProcess, &success);
		CloseHandle(sei.hProcess);
		success = !success;
	}
	
	return success;
}

void ApplyAdminSettings(HWND hwnd)
{
	BOOL fChecked1;
	BOOL fChecked2;
	
	fChecked1 = IsDlgButtonChecked(hwnd, IDC_ADDCONTEXT);
	fChecked2 = IsDlgButtonChecked(hwnd, IDC_REPLACENOTEPAD);
	
	// do we need to elevate?
	if(g_fAddToExplorer != fChecked1 || g_fReplaceNotepad != fChecked2)
	{
		// spawn ourselves to set the HKLM registry keys
		ElevateToAdmin(hwnd, fChecked1, fChecked2);

		// the spawned process will do a SaveRegSysSettings if
		// it was successful - so do a 'Load' to refresh our own state
		LoadRegSysSettings();
	}
}

//
//	Dialogbox procedure for the FONT pane
//
BOOL CALLBACK MiscOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	PSHNOTIFY *pshn;
	HICON hShield;

	switch(msg)
	{

	case WM_INITDIALOG:

		//oof(hwnd);

		// load the 'vista shield icon'
		hShield = LoadImage(0, MAKEINTRESOURCE(106), IMAGE_ICON, 32, 32, LR_CREATEDIBSECTION);//IDI_SHIELD));

		// display the next-best-thing if not running on Vista
		if(hShield == 0)
		{
			hShield = LoadIcon(0, MAKEINTRESOURCE(IDI_INFORMATION));
		}
			
		SendDlgItemMessage(hwnd, IDC_SHIELD, STM_SETICON, (WPARAM)hShield, 0);

		CheckDlgButton(hwnd, IDC_ADDCONTEXT, g_fAddToExplorer);
		CheckDlgButton(hwnd, IDC_REPLACENOTEPAD, g_fReplaceNotepad);

		// disable 'replace notepad' option for Win9x
		EnableDlgItem(hwnd, IDC_REPLACENOTEPAD, (GetVersion() & 0x80000000) ? FALSE : TRUE);
		return TRUE;

	case WM_CLOSE:
		return TRUE;

	case WM_NOTIFY:

		pshn = (PSHNOTIFY *)lParam;

		if(pshn->hdr.code == PSN_APPLY)
		{
			ApplyAdminSettings(hwnd);
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
