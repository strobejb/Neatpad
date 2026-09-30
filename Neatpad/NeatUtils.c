//
//	Helper routines for Neatpad
//
//	www.catch22.net
//
#include <windows.h>
#include <tchar.h>
#include "neatpad.h"

BOOL CheckMenuCommand(HMENU hMenu, int nCommandId, BOOL fChecked)
{
	if(fChecked)
	{
		CheckMenuItem(hMenu, nCommandId, MF_CHECKED | MF_BYCOMMAND);
		return TRUE;
	}
	else
	{
		CheckMenuItem(hMenu, nCommandId, MF_UNCHECKED | MF_BYCOMMAND);
		return FALSE;
	}
}

BOOL ToggleMenuItem(HMENU hmenu, UINT menuid)
{	
	if(MF_CHECKED & GetMenuState(hmenu, menuid, MF_BYCOMMAND))
	{
		CheckMenuItem(hmenu, menuid, MF_UNCHECKED | MF_BYCOMMAND);
		return FALSE;
	}
	else
	{
		CheckMenuItem(hmenu, menuid, MF_CHECKED | MF_BYCOMMAND);
		return TRUE;
	}
}

BOOL EnableMenuCommand(HMENU hmenu, int nCommandId, BOOL fEnable)
{
	if(fEnable)
	{
		EnableMenuItem(hmenu, nCommandId, MF_ENABLED | MF_BYCOMMAND);
		return TRUE;
	}
	else
	{
		EnableMenuItem(hmenu, nCommandId, MF_GRAYED | MF_DISABLED | MF_BYCOMMAND);
		return FALSE;
	}
}

BOOL EnableDlgItem(HWND hDlg, UINT nCommandId, BOOL fEnable)
{
	return EnableWindow(GetDlgItem(hDlg, nCommandId), fEnable);
}

BOOL SaveFileData(TCHAR *szPath, HWND hwnd)
{
	WINDOWPLACEMENT wp = { sizeof(wp) };

	TCHAR		szStream[MAX_PATH];
	HANDLE		hFile;
	DWORD		len;
	BOOL		restoretime = FALSE;
	FILETIME	ctm, atm, wtm;

	if(szPath == 0 || szPath[0] == 0)
		return FALSE;

	wsprintf(szStream, _T("%s:Neatpad.Winpos"), szPath);

	if(!GetWindowPlacement(hwnd, &wp))
		return FALSE;

	//
	//	Get the filetimes first of all
	//
	if((hFile = CreateFile(szStream, GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0)) != INVALID_HANDLE_VALUE)
	{
		GetFileTime(hFile, &ctm, &atm, &wtm);
		restoretime = TRUE;
		CloseHandle(hFile);
	}

	//
	//	Now open the stream for writing
	//
	if((hFile = CreateFile(szStream, GENERIC_WRITE, 0, 0, OPEN_ALWAYS, 0, 0)) == INVALID_HANDLE_VALUE)
		return FALSE;

	WriteFile(hFile, &wp, sizeof(wp), &len, 0);
	
	//
	//	Restore timestamp if necessary
	//
	if(restoretime == TRUE)
		SetFileTime(hFile, &ctm, &atm, &wtm);

	CloseHandle(hFile);

	return TRUE;
}

BOOL LoadFileData(TCHAR *szPath, HWND hwnd)
{
	TCHAR szStream[MAX_PATH];
	HANDLE hFile;
	WINDOWPLACEMENT wp = { sizeof(wp) };
	DWORD  len;

	if(szPath == 0 || szPath[0] == 0)
		return FALSE;

	wsprintf(szStream, _T("%s:Neatpad.Winpos"), szPath);

	//
	//	Can only set the window-position if the alternate-data-stream exists
	//
	if((hFile = CreateFile(szStream, GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0)) == INVALID_HANDLE_VALUE)
		return FALSE;

	if(!ReadFile(hFile, &wp, sizeof(wp), &len, 0))
		return FALSE;

	//
	//	Only set the window-position if we read a valid WINDOWPLACEMENT structure!!
	//
	if(len == sizeof(wp) && wp.length == sizeof(wp))
	{
		wp.flags = SW_HIDE;
		SetWindowPlacement(hwnd, &wp);
	}

	CloseHandle(hFile);
	return TRUE;
}