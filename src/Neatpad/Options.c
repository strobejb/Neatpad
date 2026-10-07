//
//	Neatpad
//	Options.c
//
//	www.catch22.net
//

#define STRICT

#include <windows.h>
#include <tchar.h>
#include <commctrl.h>
#include <shellapi.h>
#include "resource.h"
#include "Neatpad.h"
#include "..\TextView\TextView.h"

#pragma comment(lib, "comctl32.lib")

#define CONTEXT_CMD_LOC _T("*\\shell\\Open with Neatpad\\command")
#define CONTEXT_APP_LOC _T("*\\shell\\Open with Neatpad")

#define IMAGEFILE_XOPT _T("Software\\Microsoft\\Windows NT\\CurrentVersion")\
					   _T("\\Image File Execution Options\\Notepad.exe")



INT_PTR CALLBACK FontOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK MiscOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK DisplayOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

LONG  g_nFontSize;
BOOL  g_fFontBold;
TCHAR g_szFontName[LF_FACESIZE];
LONG  g_nFontSmoothing;

LONG  g_nPaddingAbove;
LONG  g_nPaddingBelow;
LONG  g_fPaddingFlags;
BOOL  g_fLineNumbers;
BOOL  g_fLongLines;
BOOL  g_fSelMargin;
BOOL  g_fSaveOnExit;
int	  g_nLongLineLimit;
BOOL  g_nHLCurLine;
BOOL  g_fShowLineEndings;
BOOL  g_fShowStatusbar;
BOOL  g_fAddToExplorer		= 0;
BOOL  g_fReplaceNotepad		= 0;

COLORREF g_rgbColourList[TXC_MAX_COLOURS];
COLORREF g_rgbCustColours[16];
extern COLORREF g_rgbAutoColourList[];

#define MAX_RECENT_FILES (IDM_RECENT_LAST - IDM_RECENT_FIRST + 1)

TCHAR g_szRecentFiles[MAX_RECENT_FILES][MAX_PATH];

// Get a binary buffer from the registry
BOOL GetSettingBin(HKEY hkey, TCHAR szKeyName[], PVOID pBuffer, LONG nLength)
{
	ZeroMemory(pBuffer, nLength);
	return !RegQueryValueEx(hkey, szKeyName, 0, 0, (BYTE *)pBuffer, &nLength);
}

// Get an integer value from the registry
BOOL GetSettingInt(HKEY hkey, TCHAR szKeyName[], LONG *pnReturnVal, LONG nDefault)
{
	ULONG len = sizeof(nDefault);

	*pnReturnVal = nDefault;

	return !RegQueryValueEx(hkey, szKeyName, 0, 0, (BYTE *)pnReturnVal, &len);
}

// Get a string buffer from the registry
BOOL GetSettingStr(HKEY hkey, TCHAR szKeyName[], TCHAR pszReturnStr[], DWORD nLength, TCHAR szDefault[])
{
	ULONG len = nLength * sizeof(TCHAR);

	lstrcpyn(pszReturnStr, szDefault, nLength);

	return !RegQueryValueEx(hkey, szKeyName, 0, 0, (BYTE *)pszReturnStr, &len);
}

// Write a binary value from the registry
BOOL WriteSettingBin(HKEY hkey, TCHAR szKeyName[], PVOID pData, ULONG nLength)
{
	return !RegSetValueEx(hkey, szKeyName, 0, REG_BINARY, (BYTE *)pData, nLength);
}

// Write an integer value from the registry
BOOL WriteSettingInt(HKEY hkey, TCHAR szKeyName[], LONG nValue)
{
	return !RegSetValueEx(hkey, szKeyName, 0, REG_DWORD, (BYTE *)&nValue, sizeof(nValue));
}

// Get a string buffer from the registry
BOOL WriteSettingStr(HKEY hkey, TCHAR szKeyName[], TCHAR szString[])
{
	return !RegSetValueEx(hkey, szKeyName, 0, REG_SZ, (BYTE *)szString, (lstrlen(szString) + 1) * sizeof(TCHAR));
}

void GetRecentValueName(int index, TCHAR *szName)
{
	wsprintf(szName, _T("RecentFile%d"), index);
}

int RecentFileCount()
{
	int i;

	for(i = 0; i < MAX_RECENT_FILES; i++)
	{
		if(g_szRecentFiles[i][0] == '\0')
			break;
	}

	return i;
}

void LoadRecentFiles(HKEY hKey)
{
	int i;
	TCHAR szName[32];

	for(i = 0; i < MAX_RECENT_FILES; i++)
	{
		GetRecentValueName(i, szName);
		GetSettingStr(hKey, szName, g_szRecentFiles[i], MAX_PATH, _T(""));
	}
}

void SaveRecentFiles(HKEY hKey)
{
	int i;
	TCHAR szName[32];

	for(i = 0; i < MAX_RECENT_FILES; i++)
	{
		GetRecentValueName(i, szName);
		WriteSettingStr(hKey, szName, g_szRecentFiles[i]);
	}
}

void SaveRecentFilesNow()
{
	HKEY hKey;

	RegCreateKeyEx(HKEY_CURRENT_USER, REGLOC, 0, 0, 0, KEY_WRITE, 0, &hKey, 0);
	SaveRecentFiles(hKey);
	RegCloseKey(hKey);
}

int FindRecentFile(TCHAR *szFileName)
{
	int i;

	for(i = 0; i < MAX_RECENT_FILES; i++)
	{
		if(g_szRecentFiles[i][0] && lstrcmpi(g_szRecentFiles[i], szFileName) == 0)
			return i;
	}

	return -1;
}

void RemoveRecentFileIndex(int index)
{
	int i;

	if(index < 0 || index >= MAX_RECENT_FILES)
		return;

	for(i = index; i < MAX_RECENT_FILES - 1; i++)
		lstrcpy(g_szRecentFiles[i], g_szRecentFiles[i + 1]);

	g_szRecentFiles[MAX_RECENT_FILES - 1][0] = '\0';
}

void AddRecentFile(TCHAR *szFileName)
{
	int i;
	int oldpos;
	TCHAR szFullPath[MAX_PATH];

	if(szFileName == 0 || szFileName[0] == '\0')
		return;

	if(GetFullPathName(szFileName, MAX_PATH, szFullPath, 0) == 0)
		lstrcpyn(szFullPath, szFileName, MAX_PATH);

	oldpos = FindRecentFile(szFullPath);

	if(oldpos >= 0)
		RemoveRecentFileIndex(oldpos);

	for(i = MAX_RECENT_FILES - 1; i > 0; i--)
		lstrcpy(g_szRecentFiles[i], g_szRecentFiles[i - 1]);

	lstrcpyn(g_szRecentFiles[0], szFullPath, MAX_PATH);
	SaveRecentFilesNow();
}

void ClearRecentFiles()
{
	int i;

	for(i = 0; i < MAX_RECENT_FILES; i++)
		g_szRecentFiles[i][0] = '\0';

	SaveRecentFilesNow();
}

void RemoveRecentFile(TCHAR *szFileName)
{
	int index = FindRecentFile(szFileName);

	if(index >= 0)
	{
		RemoveRecentFileIndex(index);
		SaveRecentFilesNow();
	}
}

BOOL MenuHasCommand(HMENU hMenu, UINT nCommandId)
{
	int i;
	int count = GetMenuItemCount(hMenu);

	for(i = 0; i < count; i++)
	{
		if(GetMenuItemID(hMenu, i) == nCommandId)
			return TRUE;
	}

	return FALSE;
}

HMENU FindRecentMenu(HMENU hMenu)
{
	int i;
	int count = GetMenuItemCount(hMenu);

	for(i = 0; i < count; i++)
	{
		HMENU hSubMenu = GetSubMenu(hMenu, i);

		if(hSubMenu)
		{
			if(MenuHasCommand(hSubMenu, IDM_RECENT_CLEAR))
				return hSubMenu;

			hSubMenu = FindRecentMenu(hSubMenu);

			if(hSubMenu)
				return hSubMenu;
		}
	}

	return 0;
}

void MakeRecentMenuText(int index, TCHAR *szText, int cchText)
{
	TCHAR *src = g_szRecentFiles[index];
	TCHAR *dst = szText;
	TCHAR *end = szText + cchText - 1;

	wsprintf(szText, _T("&%d "), index + 1);
	dst += lstrlen(szText);

	while(*src && dst < end)
	{
		if(*src == _T('&') && dst + 1 < end)
			*dst++ = _T('&');

		*dst++ = *src++;
	}

	*dst = '\0';
}

void UpdateRecentMenu(HMENU hMenu)
{
	int i;
	int count = RecentFileCount();
	HMENU hRecent = FindRecentMenu(hMenu);

	if(hRecent == 0)
		return;

	for(i = IDM_RECENT_FIRST; i <= IDM_RECENT_LAST; i++)
		DeleteMenu(hRecent, i, MF_BYCOMMAND);

	DeleteMenu(hRecent, IDM_RECENT_EMPTY, MF_BYCOMMAND);

	if(count == 0)
	{
		InsertMenu(hRecent, 0, MF_BYPOSITION | MF_STRING | MF_GRAYED, IDM_RECENT_EMPTY, _T("(Empty)"));
	}
	else
	{
		for(i = 0; i < count; i++)
		{
			TCHAR szText[MAX_PATH + 16];

			MakeRecentMenuText(i, szText, MAX_PATH + 16);
			InsertMenu(hRecent, i, MF_BYPOSITION | MF_STRING, IDM_RECENT_FIRST + i, szText);
		}
	}

	EnableMenuItem(hRecent, IDM_RECENT_CLEAR, MF_BYCOMMAND | (count ? MF_ENABLED : MF_GRAYED));
}

BOOL OpenRecentFile(HWND hwnd, UINT nCommandId)
{
	int index = nCommandId - IDM_RECENT_FIRST;
	TCHAR szFileName[MAX_PATH];

	if(index < 0 || index >= MAX_RECENT_FILES || g_szRecentFiles[index][0] == '\0')
		return FALSE;

	lstrcpy(szFileName, g_szRecentFiles[index]);

	if(NeatpadOpenFile(hwnd, szFileName))
		return TRUE;

	RemoveRecentFile(szFileName);
	return FALSE;
}

//
//	Add or remove Neatpad from the Explorer context-menu
//
BOOL SetExplorerContextMenu(BOOL fAddToMenu)
{
	HRESULT hr;

	if(fAddToMenu)
	{
		TCHAR szAppPath[MAX_PATH];
		TCHAR szDefaultStr[MAX_PATH];

		GetModuleFileName(0, szAppPath, MAX_PATH);

		wsprintf(szDefaultStr, _T("\"%s\" \"%%1\""), szAppPath);

		hr = RegSetValue(HKEY_CLASSES_ROOT, CONTEXT_CMD_LOC, REG_SZ, szDefaultStr, lstrlen(szDefaultStr) * sizeof(TCHAR));
	}
	else
	{
		hr = RegDeleteKey(HKEY_CLASSES_ROOT, CONTEXT_CMD_LOC);

		if(hr == ERROR_SUCCESS)
			hr = RegDeleteKey(HKEY_CLASSES_ROOT, CONTEXT_APP_LOC);
	}

	return (hr == ERROR_SUCCESS) ? TRUE : FALSE;
}

//
//	Replace/Restore Notepad (with Neatpad) as the default text editor, by
//  manipulating the Image-File-Execution-Options debugger setting for NOTEPAD.EXE
//
BOOL SetImageFileExecutionOptions(BOOL fReplaceWithCurrentApp)
{
	HKEY	hKey;
	HRESULT hr;
	TCHAR	szPath[MAX_PATH];

	// create an 'ImageFileExecutionOptions' entry for the standard Notepad app
	hr = RegCreateKeyEx(HKEY_LOCAL_MACHINE, IMAGEFILE_XOPT, 0, 0, 0, KEY_WRITE, 0, &hKey, 0);

	if(hr != ERROR_SUCCESS)
		return FALSE;

	// get path of current exe
	GetModuleFileName(0, szPath+1, MAX_PATH);

	// enclose it in double-quotes
	szPath[0] = '\"';
	lstrcat(szPath, _T("\""));
	lstrcat(szPath, _T(" -ifeo"));

	// set the 'debugger' key so that whenever notepad.exe is executed, neatpad runs instead
	if(fReplaceWithCurrentApp)
		WriteSettingStr(hKey, _T("Debugger"), szPath);
	else
		RegDeleteValue(hKey, _T("Debugger"));
		
	RegCloseKey(hKey);
	return TRUE;
}

void LoadRegSettings()
{
	HKEY hKey, hColKey;

	// open registry location for reading
	RegCreateKeyEx(HKEY_CURRENT_USER, REGLOC, 0, 0, 0, KEY_READ, 0, &hKey, 0);

	GetSettingInt(hKey, _T("FontSize"),		&g_nFontSize, 10);
	GetSettingInt(hKey, _T("FontBold"),		&g_fFontBold, FALSE);
	GetSettingStr(hKey, _T("FontName"),		g_szFontName, LF_FACESIZE, _T("Courier New"));
	GetSettingInt(hKey, _T("FontSmooth"),	&g_nFontSmoothing, DEFAULT_QUALITY);

	GetSettingInt(hKey, _T("PaddingAbove"), &g_nPaddingAbove, 0);
	GetSettingInt(hKey, _T("PaddingBelow"), &g_nPaddingBelow, 1);
	GetSettingInt(hKey, _T("PaddingFlags"), &g_fPaddingFlags, COURIERNEW|LUCIDACONS);

	GetSettingInt(hKey, _T("SelMargin"),	 &g_fSelMargin, TRUE);
	GetSettingInt(hKey, _T("LineNumbers"),   &g_fLineNumbers, FALSE);
	GetSettingInt(hKey, _T("LongLines"),	 &g_fLongLines, TRUE);
	GetSettingInt(hKey, _T("LongLineLimit"), &g_nLongLineLimit, 80);
	GetSettingInt(hKey, _T("SaveOnExit"),	 &g_fSaveOnExit, TRUE);
	GetSettingInt(hKey, _T("HLCurLine"),	 &g_nHLCurLine, FALSE);
	GetSettingInt(hKey, _T("ShowLineEndings"), &g_fShowLineEndings, FALSE);

	GetSettingInt(hKey, _T("AddExplorer"),	 &g_fAddToExplorer, FALSE);
	GetSettingInt(hKey, _T("ReplaceNotepad"), &g_fReplaceNotepad, FALSE);
	GetSettingInt(hKey, _T("ShowStatusbar"), &g_fShowStatusbar, TRUE);
	LoadRecentFiles(hKey);
	
	// read the display colours
	RegCreateKeyEx(hKey, _T("Colours"), 0, 0, 0, KEY_READ, 0, &hColKey, 0);

	GetSettingInt(hColKey, _T("Foreground"),	&g_rgbColourList[TXC_FOREGROUND],		g_rgbAutoColourList[TXC_FOREGROUND]		);
	GetSettingInt(hColKey, _T("Background"),	&g_rgbColourList[TXC_BACKGROUND],		g_rgbAutoColourList[TXC_BACKGROUND]		);	
	GetSettingInt(hColKey, _T("SelFG"),			&g_rgbColourList[TXC_HIGHLIGHTTEXT],	g_rgbAutoColourList[TXC_HIGHLIGHTTEXT]	);
	GetSettingInt(hColKey, _T("SelBG"),			&g_rgbColourList[TXC_HIGHLIGHT],		g_rgbAutoColourList[TXC_HIGHLIGHT]		);
	GetSettingInt(hColKey, _T("SelFG2"),		&g_rgbColourList[TXC_HIGHLIGHTTEXT2],   g_rgbAutoColourList[TXC_HIGHLIGHTTEXT2]	);
	GetSettingInt(hColKey, _T("SelBG2"),		&g_rgbColourList[TXC_HIGHLIGHT2],		g_rgbAutoColourList[TXC_HIGHLIGHT2]		);
	GetSettingInt(hColKey, _T("Margin1"),		&g_rgbColourList[TXC_SELMARGIN1],		g_rgbAutoColourList[TXC_SELMARGIN1]		);
	GetSettingInt(hColKey, _T("Margin2"),		&g_rgbColourList[TXC_SELMARGIN2],		g_rgbAutoColourList[TXC_SELMARGIN2]		);
	GetSettingInt(hColKey, _T("LinenoText"),	&g_rgbColourList[TXC_LINENUMBERTEXT],	g_rgbAutoColourList[TXC_LINENUMBERTEXT]	);
	GetSettingInt(hColKey, _T("Lineno"),		&g_rgbColourList[TXC_LINENUMBER],		g_rgbAutoColourList[TXC_LINENUMBER]		);
	GetSettingInt(hColKey, _T("LongLineText"),	&g_rgbColourList[TXC_LONGLINETEXT],		g_rgbAutoColourList[TXC_LONGLINETEXT]	);
	GetSettingInt(hColKey, _T("LongLine"),		&g_rgbColourList[TXC_LONGLINE],			g_rgbAutoColourList[TXC_LONGLINE]		);
	GetSettingInt(hColKey, _T("CurlineText"),	&g_rgbColourList[TXC_CURRENTLINETEXT],	g_rgbAutoColourList[TXC_CURRENTLINETEXT]	);
	GetSettingInt(hColKey, _T("Curline"),		&g_rgbColourList[TXC_CURRENTLINE],		g_rgbAutoColourList[TXC_CURRENTLINE]		);
	
	GetSettingBin(hColKey, _T("Custom"),		g_rgbCustColours, sizeof(g_rgbCustColours)); 

	RegCloseKey(hColKey);
	RegCloseKey(hKey);
}

void LoadRegSysSettings()
{
	HKEY hKey;

	RegCreateKeyEx(HKEY_CURRENT_USER, REGLOC, 0, 0, 0, KEY_READ, 0, &hKey, 0);
	GetSettingInt(hKey, _T("AddExplorer"),	 &g_fAddToExplorer, FALSE);
	GetSettingInt(hKey, _T("ReplaceNotepad"), &g_fReplaceNotepad, FALSE);
	RegCloseKey(hKey);
}

void SaveRegSysSettings()
{
	HKEY hKey;

	RegCreateKeyEx(HKEY_CURRENT_USER, REGLOC, 0, 0, 0, KEY_WRITE, 0, &hKey, 0);
	WriteSettingInt(hKey, _T("AddExplorer"),	g_fAddToExplorer);
	WriteSettingInt(hKey, _T("ReplaceNotepad"), g_fReplaceNotepad);
	RegCloseKey(hKey);
}

void SaveRegSettings()
{
	HKEY hKey, hColKey;

	// open registry location for writing
	RegCreateKeyEx(HKEY_CURRENT_USER, REGLOC, 0, 0, 0, KEY_WRITE, 0, &hKey, 0);

	WriteSettingInt(hKey, _T("FontSize"),	g_nFontSize);
	WriteSettingInt(hKey, _T("FontBold"),	g_fFontBold);
	WriteSettingStr(hKey, _T("FontName"),	g_szFontName);
	WriteSettingInt(hKey, _T("FontSmooth"),	g_nFontSmoothing);

	WriteSettingInt(hKey, _T("PaddingAbove"), g_nPaddingAbove);
	WriteSettingInt(hKey, _T("PaddingBelow"), g_nPaddingBelow);
	WriteSettingInt(hKey, _T("PaddingFlags"), g_fPaddingFlags);

	WriteSettingInt(hKey, _T("SelMargin"),	  g_fSelMargin);
	WriteSettingInt(hKey, _T("LineNumbers"),  g_fLineNumbers);
	WriteSettingInt(hKey, _T("LongLines"),	  g_fLongLines);
	WriteSettingInt(hKey, _T("SaveOnExit"),	  g_fSaveOnExit);
	WriteSettingInt(hKey, _T("LongLineLimit"),g_nLongLineLimit);
	WriteSettingInt(hKey, _T("HLCurLine"),	  g_nHLCurLine);
	WriteSettingInt(hKey, _T("ShowLineEndings"), g_fShowLineEndings);

	WriteSettingInt(hKey, _T("AddExplorer"),  g_fAddToExplorer);
	WriteSettingInt(hKey, _T("ReplaceNotepad"), g_fReplaceNotepad);
	WriteSettingInt(hKey, _T("ShowStatusbar"), g_fShowStatusbar);
	SaveRecentFiles(hKey);
	
	// write the display colours
	RegCreateKeyEx(hKey, _T("Colours"), 0, 0, 0, KEY_WRITE, 0, &hColKey, 0);

	WriteSettingInt(hColKey, _T("Foreground"),	g_rgbColourList[TXC_FOREGROUND]); 
	WriteSettingInt(hColKey, _T("Background"),	g_rgbColourList[TXC_BACKGROUND]); 
	WriteSettingInt(hColKey, _T("SelFG"),		g_rgbColourList[TXC_HIGHLIGHTTEXT]); 
	WriteSettingInt(hColKey, _T("SelBG"),		g_rgbColourList[TXC_HIGHLIGHT]); 
	WriteSettingInt(hColKey, _T("SelFG2"),		g_rgbColourList[TXC_HIGHLIGHTTEXT2]); 
	WriteSettingInt(hColKey, _T("SelBG2"),		g_rgbColourList[TXC_HIGHLIGHT2]); 

	WriteSettingInt(hColKey, _T("Margin1"),		g_rgbColourList[TXC_SELMARGIN1]); 
	WriteSettingInt(hColKey, _T("Margin2"),		g_rgbColourList[TXC_SELMARGIN2]); 
	WriteSettingInt(hColKey, _T("LinenoText"),	g_rgbColourList[TXC_LINENUMBERTEXT]); 
	WriteSettingInt(hColKey, _T("Lineno"),		g_rgbColourList[TXC_LINENUMBER]); 
	WriteSettingInt(hColKey, _T("LongLineText"),g_rgbColourList[TXC_LONGLINETEXT]); 
	WriteSettingInt(hColKey, _T("LongLine"),	g_rgbColourList[TXC_LONGLINE]); 

	WriteSettingInt(hColKey, _T("CurlineText"),	g_rgbColourList[TXC_CURRENTLINETEXT]); 
	WriteSettingInt(hColKey, _T("Curline"),		g_rgbColourList[TXC_CURRENTLINE]); 


	WriteSettingBin(hColKey, _T("Custom"),		g_rgbCustColours, sizeof(g_rgbCustColours)); 

	RegCloseKey(hColKey);
	RegCloseKey(hKey);
}

static HFONT g_hFallbackFonts[3];

static void DeleteFallbackFonts()
{
	int i;

	if(g_hwndTextView)
		TextView_ClearFontFallbacks(g_hwndTextView, 0);

	for(i = 0; i < sizeof(g_hFallbackFonts) / sizeof(g_hFallbackFonts[0]); i++)
	{
		if(g_hFallbackFonts[i])
		{
			DeleteObject(g_hFallbackFonts[i]);
			g_hFallbackFonts[i] = 0;
		}
	}
}

static void AddFallbackFont(int idx, TCHAR *faceName)
{
	g_hFallbackFonts[idx] = EasyCreateFont(g_nFontSize, g_fFontBold, g_nFontSmoothing, faceName);

	if(g_hFallbackFonts[idx])
		TextView_AddFontFallback(g_hwndTextView, 0, g_hFallbackFonts[idx]);
}

void DeleteRuntimeFonts()
{
	DeleteFallbackFonts();

	if(g_hFont)
	{
		DeleteObject(g_hFont);
		g_hFont = 0;
	}
}

void ApplyRegSettings()
{
	int i;
	HFONT oldFont = g_hFont;

	DeleteFallbackFonts();

	g_hFont = EasyCreateFont(g_nFontSize, g_fFontBold, g_nFontSmoothing, g_szFontName);

	TextView_SetLineSpacing(g_hwndTextView, g_nPaddingAbove, g_nPaddingBelow);

	TextView_SetStyleBool(g_hwndTextView, TXS_SELMARGIN,	g_fSelMargin);
	TextView_SetStyleBool(g_hwndTextView, TXS_LINENUMBERS,	g_fLineNumbers);
	TextView_SetStyleBool(g_hwndTextView, TXS_LONGLINES,	g_fLongLines);

	TextView_SetStyleBool(g_hwndTextView, TXS_HIGHLIGHTCURLINE,	g_nHLCurLine);
	TextView_SetStyleBool(g_hwndTextView, TXS_SHOWLINEENDINGS,	g_fShowLineEndings);

	TextView_SetCaretWidth(g_hwndTextView, 2);
	TextView_SetLongLine(g_hwndTextView, g_nLongLineLimit);
	
	SendMessage(g_hwndTextView, WM_SETFONT, (WPARAM)g_hFont, 0);

	if(oldFont)
		DeleteObject(oldFont);

	AddFallbackFont(0, _T("Segoe UI"));
	AddFallbackFont(1, _T("Nirmala UI"));
	AddFallbackFont(2, _T("Leelawadee UI"));

	for(i = 0; i < TXC_MAX_COLOURS; i++)
	{
		TextView_SetColor(g_hwndTextView, i, g_rgbColourList[i]);
	}

	//
	//	System-wide options require Administrator access. On Vista we
	//	need to elevate using the UAC prompt. Only do this if the settings have actually
	//	changed
	//
	//SetExplorerContextMenu(g_fAddToExplorerContextMenu);
	//SetImageFileExecutionOptions(g_fReplaceNotepad);
}

void ShowOptions(HWND hwndParent)
{
	PROPSHEETHEADER psh    = {   sizeof(psh)   };
	PROPSHEETPAGE   psp[3] = {  { sizeof(psp[0]) },  
								{ sizeof(psp[1]) }, 
								{ sizeof(psp[2]) }, 
							};

	CoInitialize(0);
	
	// configure property sheet
	psh.dwFlags			= PSH_PROPSHEETPAGE;
	psh.hwndParent		= hwndParent;
	psh.nPages			= sizeof(psp) / sizeof(psp[0]);
	psh.ppsp			= psp;
	psh.pszCaption		= _T("Options");

	// configure property sheet page(1)
	psp[0].dwFlags		= PSP_USETITLE;
	psp[0].hInstance	= g_hResourceModule;//GetModuleHandle(0);
	psp[0].pfnDlgProc	= FontOptionsDlgProc;
	psp[0].pszTemplate	= MAKEINTRESOURCE(IDD_FONT);
	psp[0].pszTitle		= _T("Font");

	// configure property sheet page(2)
	psp[1].dwFlags		= PSP_USETITLE;
	psp[1].hInstance	= g_hResourceModule;//GetModuleHandle(0);
	psp[1].pfnDlgProc	= DisplayOptionsDlgProc;
	psp[1].pszTemplate	= MAKEINTRESOURCE(IDD_DISPLAY);
	psp[1].pszTitle		= _T("Display");

	// configure property sheet page(3)
	psp[2].dwFlags		= PSP_USETITLE;
	psp[2].hInstance	= g_hResourceModule;//GetModuleHandle(0);
	psp[2].pfnDlgProc	= MiscOptionsDlgProc;
	psp[2].pszTemplate	= MAKEINTRESOURCE(IDD_OPTIONS);
	psp[2].pszTitle		= _T("Settings");

	if(PropertySheet(&psh))
	{
		ApplyRegSettings();		
	}

	CoUninitialize();
}
