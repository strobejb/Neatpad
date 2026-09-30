//
//	Helper routines for Neatpad
//
//	www.catch22.net
//
#include <windows.h>
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
