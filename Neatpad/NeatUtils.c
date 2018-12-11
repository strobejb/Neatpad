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

#define WINPOS_FILESPEC _T("%s:Neatpad.Settings")

typedef HMONITOR (WINAPI * MFR_PROC)(LPCRECT, DWORD);

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

//
//	Ensure that the specified window is on a visible monitor
//
void ForceVisibleDisplay(HWND hwnd)
{
	RECT		rect;
	HMODULE		hUser32;
	MFR_PROC	pMonitorFromRect;

	GetWindowRect(hwnd, &rect);

	hUser32 = GetModuleHandle(_T("USER32.DLL"));
	
	pMonitorFromRect = (MFR_PROC)GetProcAddress(hUser32, "MonitorFromRect");

	if(pMonitorFromRect != 0)
	{
		if(NULL == pMonitorFromRect(&rect, MONITOR_DEFAULTTONULL))
		{
			// force window onto primary display if it is not visible
			rect.left %= GetSystemMetrics(SM_CXSCREEN);
			rect.top  %= GetSystemMetrics(SM_CYSCREEN);

			SetWindowPos(hwnd, 0, rect.left, rect.top, 0, 0, SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOSIZE);
		}
	}
}

//
//	Save window-position for the specified file
//
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

	wsprintf(szStream, WINPOS_FILESPEC, szPath);

	if(!GetWindowPlacement(hwnd, &wp))
		return FALSE;

	//
	//	Get the file time-stamp. Try the stream first - if that doesn't exist 
	//	get the time from the 'base' file
	//
	if((hFile = CreateFile(szStream, GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0)) != INVALID_HANDLE_VALUE ||
	   (hFile = CreateFile(szPath,   GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0)) != INVALID_HANDLE_VALUE)
	{
		if(GetFileTime(hFile, &ctm, &atm, &wtm))
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

//
//	Restore the last window-position for the specified file
//
BOOL LoadFileData(TCHAR *szPath, HWND hwnd)
{
	WINDOWPLACEMENT wp = { sizeof(wp) };

	TCHAR		szStream[MAX_PATH];
	HANDLE		hFile;
	DWORD		len;

	if(szPath == 0 || szPath[0] == 0)
		return FALSE;

	wsprintf(szStream, WINPOS_FILESPEC, szPath);

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
		ForceVisibleDisplay(hwnd);
	}

	CloseHandle(hFile);
	return TRUE;
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