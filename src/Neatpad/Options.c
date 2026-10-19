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
#include <shlobj.h>
#include "resource.h"
#include "Neatpad.h"
#include "..\TextView\TextView.h"

#pragma comment(lib, "comctl32.lib")

#define CONTEXT_APP_LOC _T("Software\\Classes\\*\\shell\\Open with Neatpad")
#define CONTEXT_CMD_LOC CONTEXT_APP_LOC _T("\\command")

INT_PTR CALLBACK FontOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK MiscOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK DisplayOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

LONG  g_nFontSize;
LONG  g_nLineSpacing;
BOOL  g_fFontBold;
TCHAR g_szFontName[LF_FACESIZE];

BOOL  g_fLineNumbers;
BOOL  g_fLongLines;
BOOL  g_fSelMargin;
BOOL  g_fSaveOnExit;
int	  g_nLongLineLimit;
BOOL  g_nHLCurLine;
BOOL  g_fShowLineEndings;
BOOL  g_fShowStatusbar;
BOOL  g_fSearchMatchCase;
BOOL  g_fSearchWrap;
LONG  g_nFilterContext = 2;
LONG  g_nTabWidth      = 4;
BOOL  g_fInsertSpaces  = TRUE;
BOOL  g_fAutoIndent    = TRUE;
LONG  g_nInMemoryLimit;

C_ASSERT(TXC_MAX_COLOURS == ARRAYSIZE(((OPTIONS *)0)->rgbColourList));

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

	if(!QuerySaveChanges(hwnd))
		return FALSE;

	if(NeatpadOpenFile(hwnd, szFileName))
		return TRUE;

	RemoveRecentFile(szFileName);
	return FALSE;
}

//
//	Open the most recent file other than the one being edited, so that
//	repeating it switches between the two latest files
//
BOOL OpenPreviousFile(HWND hwnd)
{
	TCHAR szCurrent[MAX_PATH] = _T("");
	int i;

	// File > New clears the title but leaves the old file name behind
	if(g_szFileTitle[0] && GetFullPathName(g_szFileName, MAX_PATH, szCurrent, 0) == 0)
		lstrcpyn(szCurrent, g_szFileName, MAX_PATH);

	for(i = 0; i < RecentFileCount(); i++)
	{
		if(lstrcmpi(g_szRecentFiles[i], szCurrent) != 0)
			return OpenRecentFile(hwnd, IDM_RECENT_FIRST + i);
	}

	return FALSE;
}

//
//	Add or remove Neatpad from the Explorer context-menu
//
BOOL IsExplorerContextMenuEnabled(void)
{
    HKEY  key;
    TCHAR path[MAX_PATH];
    TCHAR expected[MAX_PATH + 8];
    TCHAR command[MAX_PATH + 8];
    DWORD size = sizeof(command);
    DWORD type;
    LONG  status;

    if (RegOpenKeyEx(HKEY_CURRENT_USER, CONTEXT_CMD_LOC, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    {
        return FALSE;
    }

    status = RegQueryValueEx(key, NULL, NULL, &type, (BYTE *)command, &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ || size < sizeof(TCHAR) || size > sizeof(command))
    {
        return FALSE;
    }

    command[ARRAYSIZE(command) - 1] = 0;
    if (!GetModuleFileName(NULL, path, ARRAYSIZE(path)))
    {
        return FALSE;
    }

    wsprintf(expected, _T("\"%s\" \"%%1\""), path);
    return lstrcmpi(command, expected) == 0;
}

BOOL SetExplorerContextMenu(BOOL enabled)
{
    HKEY  key;
    TCHAR path[MAX_PATH];
    TCHAR command[MAX_PATH + 8];
    LONG  status;

    if (enabled)
    {
        if (!GetModuleFileName(NULL, path, ARRAYSIZE(path)))
        {
            return FALSE;
        }

        wsprintf(command, _T("\"%s\" \"%%1\""), path);
        status = RegCreateKeyEx(HKEY_CURRENT_USER, CONTEXT_CMD_LOC, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL);
        if (status == ERROR_SUCCESS)
        {
            status = RegSetValueEx(key, NULL, 0, REG_SZ, (const BYTE *)command, (lstrlen(command) + 1) * sizeof(TCHAR));
            RegCloseKey(key);
        }
    }
    else
    {
        status = RegDeleteKey(HKEY_CURRENT_USER, CONTEXT_CMD_LOC);
        if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND)
        {
            status = RegDeleteKey(HKEY_CURRENT_USER, CONTEXT_APP_LOC);
        }
        if (status == ERROR_FILE_NOT_FOUND)
        {
            status = ERROR_SUCCESS;
        }
    }

    if (status != ERROR_SUCCESS)
    {
        SetLastError(status);
        return FALSE;
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
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

    // Migrate the old separate values, keeping their total extra space.
    {
        LONG above, below;

        GetSettingInt(hKey, _T("PaddingAbove"), &above, 0);
        GetSettingInt(hKey, _T("PaddingBelow"), &below, 1);
        above = max(0, min(above, MAX_LINE_SPACING));
        below = max(0, min(below, MAX_LINE_SPACING));
        GetSettingInt(hKey, _T("LineSpacing"), &g_nLineSpacing, min(above + below, MAX_LINE_SPACING));
        if (g_nLineSpacing < 0 || g_nLineSpacing > MAX_LINE_SPACING)
        {
            g_nLineSpacing = 1;
        }
    }

    GetSettingInt(hKey, _T("SelMargin"),	 &g_fSelMargin, TRUE);
	GetSettingInt(hKey, _T("LineNumbers"),   &g_fLineNumbers, FALSE);
	GetSettingInt(hKey, _T("LongLines"),	 &g_fLongLines, TRUE);
	GetSettingInt(hKey, _T("LongLineLimit"), &g_nLongLineLimit, 80);
	GetSettingInt(hKey, _T("SaveOnExit"),	 &g_fSaveOnExit, TRUE);
	GetSettingInt(hKey, _T("HLCurLine"),	 &g_nHLCurLine, FALSE);
	GetSettingInt(hKey, _T("ShowLineEndings"), &g_fShowLineEndings, FALSE);
	GetSettingInt(hKey, _T("SearchMatchCase"), &g_fSearchMatchCase, FALSE);
	GetSettingInt(hKey, _T("SearchWrap"), &g_fSearchWrap, TRUE);
    GetSettingInt(hKey, _T("TabWidth"), &g_nTabWidth, 4);
    if (g_nTabWidth < 1 || g_nTabWidth > TV_MAX_TAB_WIDTH)
        g_nTabWidth = 4;
    GetSettingInt(hKey, _T("InsertSpaces"), &g_fInsertSpaces, TRUE);
    GetSettingInt(hKey, _T("AutoIndent"), &g_fAutoIndent, TRUE);

    GetSettingInt(hKey, _T("FilterContext"), &g_nFilterContext, 2);
    if (g_nFilterContext < 0 || g_nFilterContext > TV_MAX_FILTER_CONTEXT)
        g_nFilterContext = 2;

    // files under 16 MB are read into memory and released; larger ones are read
	// from disk and locked while they're open. 0 reads every file from disk.
	GetSettingInt(hKey, _T("InMemoryLimit"), &g_nInMemoryLimit, 16 * 1024 * 1024);

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
	GetSettingInt(hColKey, _T("SearchMatchText"), &g_rgbColourList[TXC_SEARCHMATCHTEXT],	g_rgbAutoColourList[TXC_SEARCHMATCHTEXT]	);
	GetSettingInt(hColKey, _T("SearchMatch"),	&g_rgbColourList[TXC_SEARCHMATCH],		g_rgbAutoColourList[TXC_SEARCHMATCH]		);
	
	GetSettingBin(hColKey, _T("Custom"),		g_rgbCustColours, sizeof(g_rgbCustColours)); 

	RegCloseKey(hColKey);
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
    WriteSettingInt(hKey, _T("LineSpacing"), g_nLineSpacing);

    WriteSettingInt(hKey, _T("SelMargin"),	  g_fSelMargin);
	WriteSettingInt(hKey, _T("LineNumbers"),  g_fLineNumbers);
	WriteSettingInt(hKey, _T("LongLines"),	  g_fLongLines);
	WriteSettingInt(hKey, _T("SaveOnExit"),	  g_fSaveOnExit);
	WriteSettingInt(hKey, _T("LongLineLimit"),g_nLongLineLimit);
	WriteSettingInt(hKey, _T("HLCurLine"),	  g_nHLCurLine);
	WriteSettingInt(hKey, _T("ShowLineEndings"), g_fShowLineEndings);
	WriteSettingInt(hKey, _T("SearchMatchCase"), g_fSearchMatchCase);
	WriteSettingInt(hKey, _T("SearchWrap"), g_fSearchWrap);
    WriteSettingInt(hKey, _T("TabWidth"), g_nTabWidth);
    WriteSettingInt(hKey, _T("InsertSpaces"), g_fInsertSpaces);
    WriteSettingInt(hKey, _T("AutoIndent"), g_fAutoIndent);
    WriteSettingInt(hKey, _T("FilterContext"), g_nFilterContext);
    WriteSettingInt(hKey, _T("InMemoryLimit"), g_nInMemoryLimit);

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
	WriteSettingInt(hColKey, _T("SearchMatchText"), g_rgbColourList[TXC_SEARCHMATCHTEXT]);
	WriteSettingInt(hColKey, _T("SearchMatch"),	g_rgbColourList[TXC_SEARCHMATCH]);


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

//
//	The text's font in the face given, at the font size zoomed to g_nZoom percent
//
static HFONT CreateTextFont(TCHAR *szFace)
{
    return CreateFont(MulDiv(PointsToLogical(g_nFontSize), g_nZoom, 100), 0, 0, 0, g_fFontBold ? FW_BOLD : 0, 0, 0, 0,
                      DEFAULT_CHARSET, 0, 0, DEFAULT_QUALITY, 0, szFace);
}

static void AddFallbackFont(int idx, TCHAR *faceName)
{
	g_hFallbackFonts[idx] = CreateTextFont(faceName);

	if(g_hFallbackFonts[idx])
		TextView_AddFontFallback(g_hwndTextView, 0, g_hFallbackFonts[idx]);
}

//
//	Give the TextView its fonts, after the font or the zoom changes
//
void ApplyFonts()
{
	HFONT oldFont = g_hFont;

	DeleteFallbackFonts();

	g_hFont = CreateTextFont(g_szFontName);
	SendMessage(g_hwndTextView, WM_SETFONT, (WPARAM)g_hFont, 0);

	if(oldFont)
		DeleteObject(oldFont);

	AddFallbackFont(0, _T("Segoe UI"));
	AddFallbackFont(1, _T("Nirmala UI"));
	AddFallbackFont(2, _T("Leelawadee UI"));
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

    // Split extra pixels evenly; an odd pixel belongs below the text.
    TextView_SetLineSpacing(g_hwndTextView, g_nLineSpacing / 2, (g_nLineSpacing + 1) / 2);

    TextView_SetStyleBool(g_hwndTextView, TXS_SELMARGIN,	g_fSelMargin);
	TextView_SetStyleBool(g_hwndTextView, TXS_LINENUMBERS,	g_fLineNumbers);
	TextView_SetStyleBool(g_hwndTextView, TXS_LONGLINES,	g_fLongLines);

	TextView_SetStyleBool(g_hwndTextView, TXS_HIGHLIGHTCURLINE,	g_nHLCurLine);
	TextView_SetStyleBool(g_hwndTextView, TXS_SHOWLINEENDINGS,	g_fShowLineEndings);

	TextView_SetCaretWidth(g_hwndTextView, 2);
	TextView_SetLongLine(g_hwndTextView, g_nLongLineLimit);
	TextView_SetInMemoryLimit(g_hwndTextView, (DWORD)g_nInMemoryLimit);
    TextView_SetFilterContext(g_hwndTextView, (UINT)g_nFilterContext);
    TextView_SetTabWidth(g_hwndTextView, (UINT)g_nTabWidth);
    TextView_SetStyleBool(g_hwndTextView, TXS_INSERTSPACES, g_fInsertSpaces);
    TextView_SetStyleBool(g_hwndTextView, TXS_AUTOINDENT, g_fAutoIndent);

    ApplyFonts();

	for(i = 0; i < TXC_MAX_COLOURS; i++)
	{
		TextView_SetColor(g_hwndTextView, i, g_rgbColourList[i]);
	}

}

void ShowOptions(HWND hwndParent)
{
    PROPSHEETHEADER psh    = {sizeof(psh)};
    PROPSHEETPAGE   psp[3] = {
        {sizeof(psp[0])},
        {sizeof(psp[1])},
        {sizeof(psp[2])},
    };

    OPTIONS options;

    options.nFontSize             = g_nFontSize;
    options.fFontBold             = g_fFontBold;
    options.nLineSpacing          = g_nLineSpacing;
    options.fLineNumbers          = g_fLineNumbers;
    options.fLongLines            = g_fLongLines;
    options.fSelMargin            = g_fSelMargin;
    options.nLongLineLimit        = g_nLongLineLimit;
    options.fHighlightCurrentLine = g_nHLCurLine;
    options.nTabWidth             = g_nTabWidth;
    options.fInsertSpaces         = g_fInsertSpaces;
    options.fAutoIndent           = g_fAutoIndent;
    options.nFilterContext        = g_nFilterContext;
    memcpy(options.szFontName, g_szFontName, sizeof(options.szFontName));
    memcpy(options.rgbColourList, g_rgbColourList, sizeof(options.rgbColourList));
    memcpy(options.rgbCustColours, g_rgbCustColours, sizeof(options.rgbCustColours));

    CoInitialize(0);

    // configure property sheet
    psh.dwFlags    = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW;
    psh.hwndParent = hwndParent;
    psh.nPages     = sizeof(psp) / sizeof(psp[0]);
    psh.ppsp       = psp;
    psh.pszCaption = _T("Options");

    // configure property sheet page(1)
    psp[0].lParam      = (LPARAM)&options;
    psp[0].dwFlags     = PSP_USETITLE;
    psp[0].hInstance   = g_hResourceModule; // GetModuleHandle(0);
    psp[0].pfnDlgProc  = FontOptionsDlgProc;
    psp[0].pszTemplate = MAKEINTRESOURCE(IDD_FONT);
    psp[0].pszTitle    = _T("Appearance");

    // configure property sheet page(2)
    psp[1].lParam      = (LPARAM)&options;
    psp[1].dwFlags     = PSP_USETITLE;
    psp[1].hInstance   = g_hResourceModule; // GetModuleHandle(0);
    psp[1].pfnDlgProc  = DisplayOptionsDlgProc;
    psp[1].pszTemplate = MAKEINTRESOURCE(IDD_DISPLAY);
    psp[1].pszTitle    = _T("Display");

    // configure property sheet page(3)
    psp[2].lParam      = (LPARAM)&options;
    psp[2].dwFlags     = PSP_USETITLE;
    psp[2].hInstance   = g_hResourceModule; // GetModuleHandle(0);
    psp[2].pfnDlgProc  = MiscOptionsDlgProc;
    psp[2].pszTemplate = MAKEINTRESOURCE(IDD_OPTIONS);
    psp[2].pszTitle    = _T("Settings");

    if (PropertySheet(&psh) > 0)
    {
        g_nFontSize      = options.nFontSize;
        g_fFontBold      = options.fFontBold;
        g_nLineSpacing   = options.nLineSpacing;
        g_fLineNumbers   = options.fLineNumbers;
        g_fLongLines     = options.fLongLines;
        g_fSelMargin     = options.fSelMargin;
        g_nLongLineLimit = options.nLongLineLimit;
        g_nHLCurLine     = options.fHighlightCurrentLine;
        g_nTabWidth      = options.nTabWidth;
        g_fInsertSpaces  = options.fInsertSpaces;
        g_fAutoIndent    = options.fAutoIndent;
        g_nFilterContext = options.nFilterContext;
        memcpy(g_szFontName, options.szFontName, sizeof(options.szFontName));
        memcpy(g_rgbColourList, options.rgbColourList, sizeof(options.rgbColourList));
        memcpy(g_rgbCustColours, options.rgbCustColours, sizeof(options.rgbCustColours));

        ApplyRegSettings();
    }

    CoUninitialize();
}
