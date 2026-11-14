//
//	Helper routines for Neatpad
//
//	www.catch22.net
//

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <shellapi.h>
#include <tchar.h>
#include <uxtheme.h>
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

// Remember one app position, without adding metadata to edited files.
void SaveWindowPosition(HWND hwnd)
{
    WINDOWPLACEMENT placement = {sizeof(placement)};
    HKEY            key;

    if (!GetWindowPlacement(hwnd, &placement))
    {
        return;
    }

    if (IsIconic(hwnd))
    {
        placement.showCmd = (placement.flags & WPF_RESTORETOMAXIMIZED) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    }
    placement.flags = 0;

    if (RegCreateKeyEx(HKEY_CURRENT_USER, REGLOC, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS)
    {
        RegSetValueEx(key, _T("WindowPlacement"), 0, REG_BINARY, (const BYTE *)&placement, sizeof(placement));
        RegCloseKey(key);
    }
}

int RestoreWindowPosition(HWND hwnd, int showCmd)
{
    WINDOWPLACEMENT placement;
    MONITORINFO     monitorInfo = {sizeof(monitorInfo)};
    HMONITOR        monitor;
    HKEY            key;
    DWORD           size = sizeof(placement);
    DWORD           type;
    LONG            status;
    int             savedShowCmd;

    if (RegOpenKeyEx(HKEY_CURRENT_USER, REGLOC, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    {
        return showCmd;
    }

    status = RegQueryValueEx(key, _T("WindowPlacement"), NULL, &type, (BYTE *)&placement, &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_BINARY || size != sizeof(placement) ||
        placement.length != sizeof(placement) || placement.rcNormalPosition.right <= placement.rcNormalPosition.left ||
        placement.rcNormalPosition.bottom <= placement.rcNormalPosition.top)
    {
        return showCmd;
    }

    // A disconnected monitor must not leave the restored window off screen.
    monitor = MonitorFromRect(&placement.rcNormalPosition, MONITOR_DEFAULTTONULL);
    if (!monitor)
    {
        monitor = MonitorFromRect(&placement.rcNormalPosition, MONITOR_DEFAULTTONEAREST);
        if (GetMonitorInfo(monitor, &monitorInfo))
        {
            LONG width  = min(placement.rcNormalPosition.right - placement.rcNormalPosition.left,
                              monitorInfo.rcWork.right - monitorInfo.rcWork.left);
            LONG height = min(placement.rcNormalPosition.bottom - placement.rcNormalPosition.top,
                              monitorInfo.rcWork.bottom - monitorInfo.rcWork.top);

            SetRect(&placement.rcNormalPosition, monitorInfo.rcWork.left, monitorInfo.rcWork.top,
                    monitorInfo.rcWork.left + width, monitorInfo.rcWork.top + height);
        }
    }

    savedShowCmd      = placement.showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    placement.flags   = 0;
    placement.showCmd = SW_HIDE;
    if (!SetWindowPlacement(hwnd, &placement))
    {
        return showCmd;
    }

    // Honour an explicit launch request to start minimized or maximized.
    return showCmd == SW_SHOWNORMAL || showCmd == SW_SHOWDEFAULT ? savedShowCmd : showCmd;
}

//
//	Resolve a ShellLink (i.e. c:\path\shortcut.lnk) to a real path
//	Refactored from MFC source
//
BOOL ResolveShortcut(TCHAR *pszShortcut, TCHAR *pszFilePath, int nPathLen)
{
	IShellLink * psl;
	SHFILEINFO   info = { 0 };
	IPersistFile *ppf;

	*pszFilePath = 0;   // assume failure

	// retrieve file's shell-attributes
	if((SHGetFileInfo(pszShortcut, 0, &info, sizeof(info), SHGFI_ATTRIBUTES) == 0))
		return FALSE;

	// not a shortcut?
	if(!(info.dwAttributes & SFGAO_LINK))
	{
		lstrcpyn(pszFilePath, pszShortcut, nPathLen);
		return TRUE;
	}

	// obtain the IShellLink interface
	if(FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLink, (LPVOID*)&psl)))
		return FALSE;

	if (SUCCEEDED(psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (LPVOID*)&ppf)))
	{
		if (SUCCEEDED(ppf->lpVtbl->Load(ppf, pszShortcut, STGM_READ)))
		{
			// Resolve the link, this may post UI to find the link
			if (SUCCEEDED(psl->lpVtbl->Resolve(psl, 0, SLR_NO_UI )))
			{
				psl->lpVtbl->GetPath(psl, pszFilePath, nPathLen, NULL, 0);
				ppf->lpVtbl->Release(ppf);
				psl->lpVtbl->Release(psl);
				return TRUE;
			}
		}

		ppf->lpVtbl->Release(ppf);
	}

	psl->lpVtbl->Release(psl);
	return FALSE;
}


//
//	Convert 'points' to logical-units suitable for CreateFont
//
int PointsToLogical(int nPointSize)
{
	HDC hdc      = GetDC(0);
	int nLogSize = -MulDiv(nPointSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
	ReleaseDC(0, hdc);

	return nLogSize;
}

//
//	Simple wrapper around CreateFont
//
HFONT EasyCreateFont(int nPointSize, BOOL fBold, DWORD dwQuality, TCHAR *szFace)
{
	return CreateFont(PointsToLogical(nPointSize), 
					  0, 0, 0, 
					  fBold ? FW_BOLD : 0,
					  0,0,0,DEFAULT_CHARSET,0,0,
					  dwQuality,
					  0,
					  szFace);
}

//
//	Return a BOLD version of whatever font the 
//	specified window is using
//
HFONT CreateBoldFontFromHwnd(HWND hwnd)
{
	HFONT	hFont;
	LOGFONT logfont;

	// get the font information
	hFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
	GetObject(hFont, sizeof(logfont), &logfont);
		
	// create it with the 'bold' attribute
	logfont.lfWeight = FW_BOLD;
	return CreateFontIndirect(&logfont);
}

int RectWidth(RECT *rect)
{
	return rect->right - rect->left;
}

int RectHeight(RECT *rect)
{
	return rect->bottom - rect->top;
}

//
//	Center the specified window relative to it's parent
//	
void CenterWindow(HWND hwnd)
{
	HWND hwndParent = GetParent(hwnd);
	RECT rcChild;
	RECT rcParent;
	int  x, y;

	GetWindowRect(hwnd,			&rcChild);
	GetWindowRect(hwndParent,	&rcParent);

	x = rcParent.left + (RectWidth(&rcParent)  - RectWidth(&rcChild))  / 2;
	y = rcParent.top +  (RectHeight(&rcParent) - RectHeight(&rcChild)) / 2;

	MoveWindow(hwnd, max(0, x), max(0, y), RectWidth(&rcChild), RectHeight(&rcChild), TRUE);
}


//
//	Copied from uxtheme.h
//  If you have this new header, then delete these and
//  #include <uxtheme.h> instead!
//
/*#define ETDT_DISABLE        0x00000001
#define ETDT_ENABLE         0x00000002
#define ETDT_USETABTEXTURE  0x00000004
#define ETDT_ENABLETAB      (ETDT_ENABLE  | ETDT_USETABTEXTURE)
*/
// 

//
//	Try to call EnableThemeDialogTexture, if uxtheme.dll is present
//
BOOL EnableDialogTheme(HWND hwnd)
{
	HMODULE  hUXTheme;
	typedef  HRESULT (WINAPI * ETDTProc) (HWND, DWORD);
	ETDTProc fnEnableThemeDialogTexture;

	hUXTheme = GetModuleHandle(_T("uxtheme.dll"));

	if(hUXTheme)
	{
		fnEnableThemeDialogTexture = 
			(ETDTProc)GetProcAddress(hUXTheme, "EnableThemeDialogTexture");

		if(fnEnableThemeDialogTexture)
		{
			fnEnableThemeDialogTexture(hwnd, ETDT_ENABLETAB);
			return TRUE;
		}
		else
		{
			// Failed to locate API!
			return FALSE;
		}
	}
	else
	{
		// Not running under XP? Just fail gracefully
		return FALSE;
	}
}


//
//	WM_NCCALCSIZE handler for top-level windows. Prevents the
//  flickering observed during a 'top-left' window resize by adjusting
//  the source+dest BitBlt rectangles
//
//	hwnd - must have WS_CLIPCHILDREN turned OFF
//
/*UINT NcCalcSize(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
	ULONG ret = DefWindowProc(hwnd, WM_NCCALCSIZE, wParam, lParam);
	
	if(wParam == TRUE)
	{
		NCCALCSIZE_PARAMS *nccsp = (NCCALCSIZE_PARAMS *)lParam;
		RECT rc;
		int  sbheight = 0;
		
		GetWindowRect(g_hwndStatusbar, &rc);
		sbheight = g_fShowStatusbar ? rc.bottom-rc.top : 0;
		
		nccsp->rgrc[1] = nccsp->rgrc[0];
		nccsp->rgrc[1].right  -= GetSystemMetrics(SM_CXVSCROLL) + 50;
		nccsp->rgrc[1].bottom -= GetSystemMetrics(SM_CYHSCROLL) + sbheight + 50;
		
		return WVR_VALIDRECTS;
	}
	else
	{
		return ret;
	}
}*/