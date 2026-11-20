// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

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
#include "../../TextView/TextView.h"

#pragma comment(lib, "comctl32.lib")

#define CONTEXT_APP_LOC _T("Software\\Classes\\*\\shell\\Open with Neatpad")
#define CONTEXT_CMD_LOC CONTEXT_APP_LOC _T("\\command")

INT_PTR CALLBACK AppearanceOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK MiscOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK DisplayOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LONG  g_nFontSize;
static BOOL  g_fFontBold;
static BOOL  g_fFontItalic;
static TCHAR g_szFontName[LF_FACESIZE];
static LONG  g_nLineSpacing;
static LONG  g_nAppearanceStyle;
static LONG  g_nPalette;

BOOL  g_fLineNumbers;
BOOL  g_fLongLines;
BOOL  g_fSelMargin;
BOOL  g_fChangeIndicators;
BOOL  g_fHighlightChanges;
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

BOOL g_fHasCustomPalette;
static COLORREF g_legacyColours[TXC_MAX_COLOURS];

#define MAX_RECENT_FILES (IDM_RECENT_LAST - IDM_RECENT_FIRST + 1)

TCHAR g_szRecentFiles[MAX_RECENT_FILES][MAX_PATH];

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

//
// Keep saved per-colour settings available as Custom. The old registry
// entries are read in TXC_* order and left intact when a preset is selected.
//
static void LoadLegacyPalette(HKEY hKey)
{
    static const struct
    {
        TCHAR   *name;
        COLORREF automatic;
    } colours[] = {
        {_T("Foreground"), SYSCOL(COLOR_WINDOWTEXT)},
        {_T("Background"), SYSCOL(COLOR_WINDOW)},
        {_T("SelFG"), SYSCOL(COLOR_HIGHLIGHTTEXT)},
        {_T("SelBG"), SYSCOL(COLOR_HIGHLIGHT)},
        {_T("SelFG2"), SYSCOL(COLOR_WINDOWTEXT)},
        {_T("SelBG2"), SYSCOL(COLOR_3DFACE)},
        {_T("Margin1"), SYSCOL(COLOR_3DFACE)},
        {_T("Margin2"), SYSCOL(COLOR_3DHIGHLIGHT)},
        {_T("LinenoText"), MIXED_SYSCOL(COLOR_3DSHADOW, COLOR_3DDKSHADOW)},
        {_T("Lineno"), MIXED_SYSCOL2(COLOR_3DFACE, COLOR_WINDOW)},
        {_T("LongLineText"), SYSCOL(COLOR_WINDOWTEXT)},
        {_T("LongLine"), MIXED_SYSCOL(COLOR_3DFACE, COLOR_WINDOW)},
        {_T("CurlineText"), SYSCOL(COLOR_WINDOWTEXT)},
        {_T("Curline"), RGB(230, 240, 255)},
        {_T("SearchMatchText"), SYSCOL(COLOR_WINDOWTEXT)},
        {_T("SearchMatch"), RGB(255, 230, 128)},
        {_T("ChangedText"), RGB(190, 55, 55)},
        {_T("ChangedLine"), RGB(88, 132, 164)},
        {_T("ChangedLineBG"), MIXED_SYSCOL2(COLOR_3DFACE, COLOR_WINDOW)},
    };
    HKEY colourKey;

    C_ASSERT(ARRAYSIZE(colours) == TXC_MAX_COLOURS);
    g_fHasCustomPalette = FALSE;

    if (RegOpenKeyEx(hKey, _T("Colours"), 0, KEY_QUERY_VALUE, &colourKey) != ERROR_SUCCESS)
        return;

    for (UINT i = 0; i < ARRAYSIZE(colours); ++i)
    {
        LONG colour;

        GetSettingInt(colourKey, colours[i].name, &colour, colours[i].automatic);

        // The previous automatic outline colour predates the current default.
        if (i == TXC_CHANGEDLINE && (COLORREF)colour == RGB(202, 151, 54))
            colour = colours[i].automatic;

        g_legacyColours[i] = colour;
        if (g_legacyColours[i] != colours[i].automatic)
            g_fHasCustomPalette = TRUE;
    }

    RegCloseKey(colourKey);
}

void LoadRegSettings()
{
    HKEY hKey = 0;

    RegCreateKeyEx(HKEY_CURRENT_USER, REGLOC, 0, 0, 0, KEY_READ, 0, &hKey, 0);

    GetSettingInt(hKey, _T("FontSize"), &g_nFontSize, DEFAULT_EDITOR_FONT_SIZE);
    GetSettingInt(hKey, _T("FontBold"), &g_fFontBold, FALSE);
    GetSettingInt(hKey, _T("FontItalic"), &g_fFontItalic, FALSE);
    GetSettingStr(hKey, _T("FontName"), g_szFontName, LF_FACESIZE, DEFAULT_EDITOR_FONT);

    LoadLegacyPalette(hKey);
    GetSettingInt(hKey, _T("AppearanceStyle"), &g_nAppearanceStyle, STYLE_SYSTEM);
    if (g_nAppearanceStyle < STYLE_SYSTEM || g_nAppearanceStyle > STYLE_DARK)
        g_nAppearanceStyle = STYLE_SYSTEM;

    GetSettingInt(hKey, _T("Palette"), &g_nPalette, g_fHasCustomPalette ? PALETTE_CUSTOM : PALETTE_ADWAITA);
    if (g_nPalette < PALETTE_ADWAITA || g_nPalette > (g_fHasCustomPalette ? PALETTE_CUSTOM : PALETTE_SOLARIZED))
        g_nPalette = PALETTE_ADWAITA;

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
    GetSettingInt(hKey, _T("ChangeIndicators"), &g_fChangeIndicators, TRUE);
    GetSettingInt(hKey, _T("HighlightChanges"), &g_fHighlightChanges, TRUE);
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
	
    RegCloseKey(hKey);
}

void SaveRegSettings()
{
    HKEY hKey;

    if (RegCreateKeyEx(HKEY_CURRENT_USER, REGLOC, 0, 0, 0, KEY_WRITE, 0, &hKey, 0) != ERROR_SUCCESS)
        return;

    WriteSettingInt(hKey, _T("AppearanceStyle"), g_nAppearanceStyle);
    WriteSettingInt(hKey, _T("Palette"), g_nPalette);
    WriteSettingInt(hKey, _T("FontSize"), g_nFontSize);
    WriteSettingInt(hKey, _T("FontBold"), g_fFontBold);
    WriteSettingInt(hKey, _T("FontItalic"), g_fFontItalic);
    WriteSettingStr(hKey, _T("FontName"), g_szFontName);
    WriteSettingInt(hKey, _T("LineSpacing"), g_nLineSpacing);

    WriteSettingInt(hKey, _T("SelMargin"), g_fSelMargin);
    WriteSettingInt(hKey, _T("ChangeIndicators"), g_fChangeIndicators);
    WriteSettingInt(hKey, _T("HighlightChanges"), g_fHighlightChanges);
    WriteSettingInt(hKey, _T("LineNumbers"), g_fLineNumbers);
    WriteSettingInt(hKey, _T("LongLines"), g_fLongLines);
    WriteSettingInt(hKey, _T("SaveOnExit"), g_fSaveOnExit);
    WriteSettingInt(hKey, _T("LongLineLimit"), g_nLongLineLimit);
    WriteSettingInt(hKey, _T("HLCurLine"), g_nHLCurLine);
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
//	The given editor font at the requested zoom percentage
//
HFONT CreateEditorFont(const TCHAR *face, int points, BOOL bold, BOOL italic, int zoom)
{
    int weight = bold ? FW_BOLD : FW_NORMAL;

    return CreateFont(MulDiv(PointsToLogical(points), zoom, 100), 0, 0, 0, weight, italic, 0, 0,
                      DEFAULT_CHARSET, 0, 0, DEFAULT_QUALITY, 0, face);
}

static void AddFallbackFont(int idx, TCHAR *faceName)
{
    g_hFallbackFonts[idx] = CreateEditorFont(faceName, g_nFontSize, g_fFontBold, g_fFontItalic, g_nZoom);

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

    g_hFont = CreateEditorFont(g_szFontName, g_nFontSize, g_fFontBold, g_fFontItalic, g_nZoom);
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

static BOOL UseDarkEditorStyle(LONG style)
{
    HKEY key;
    LONG light = TRUE;

    if (style != STYLE_SYSTEM)
        return style == STYLE_DARK;

    if (RegOpenKeyEx(HKEY_CURRENT_USER, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
                     0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
    {
        GetSettingInt(key, _T("AppsUseLightTheme"), &light, TRUE);
        RegCloseKey(key);
    }

    return !light;
}

void ApplyEditorPalette(HWND view, LONG choice, LONG style)
{
    TEXTVIEWPALETTE palette = {0};

    palette.baseResource = IDR_PALETTE_ADWAITA;
    palette.accent       = GetSysColor(COLOR_HIGHLIGHT);

    switch (choice)
    {
    case PALETTE_CUSTOM:
        palette.legacyColours = g_legacyColours;
        // Custom keeps its saved colours and Classic's syntax styles.
        // Fall through to Classic.
    case PALETTE_CLASSIC:
        palette.overlayResource = IDR_PALETTE_CLASSIC;
        break;

    case PALETTE_SOLARIZED:
        palette.overlayResource = IDR_PALETTE_SOLARIZED;
        // Fall through to set the light or dark variant.
    default:
        palette.dark = UseDarkEditorStyle(style);
        break;
    }

    TextView_SetPaletteResources(view, &palette);
}

void ApplyAppearancePalette()
{
    ApplyEditorPalette(g_hwndTextView, g_nPalette, g_nAppearanceStyle);
}

void ApplyRegSettings()
{

    // Split extra pixels evenly; an odd pixel belongs below the text.
    TextView_SetLineSpacing(g_hwndTextView, g_nLineSpacing / 2, (g_nLineSpacing + 1) / 2);

    TextView_SetStyleBool(g_hwndTextView, TXS_SELMARGIN,	g_fSelMargin);
    TextView_SetStyleBool(g_hwndTextView, TXS_CHANGEMARKERS, g_fChangeIndicators);
    TextView_SetStyleBool(g_hwndTextView, TXS_HIGHLIGHTCHANGES, g_fHighlightChanges);
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
    ApplyAppearancePalette();
}

#define WM_FIT_OPTIONS_PAGE (WM_APP + 2)

void OptionsPageActivated(HWND page)
{
    // The property sheet positions the page after PSN_SETACTIVE returns.
    PostMessage(GetParent(page), WM_FIT_OPTIONS_PAGE, 0, 0);
}

static void FitOptionsPage(HWND sheet)
{
    static const int buttons[] = {IDOK, IDCANCEL};
    HWND page = PropSheet_GetCurrentPageHwnd(sheet);
    HWND tabs = PropSheet_GetTabControl(sheet);
    RECT pageRect, tabRect, tabBody, sheetRect;
    RECT padding = {0, 0, 0, 7};
    int  bottom, delta, height, top;
    HDWP positions;

    if (!page || !tabs)
        return;

    GetWindowRect(page, &pageRect);
    GetWindowRect(tabs, &tabRect);
    GetWindowRect(sheet, &sheetRect);
    GetClientRect(tabs, &tabBody);
    TabCtrl_AdjustRect(tabs, FALSE, &tabBody);
    MapWindowPoints(tabs, 0, (POINT *)&tabBody, 2);
    MapDialogRect(page, &padding);

    bottom = pageRect.top;
    for (HWND control = GetWindow(page, GW_CHILD); control; control = GetWindow(control, GW_HWNDNEXT))
    {
        RECT bounds;

        if ((GetWindowLongPtr(control, GWL_STYLE) & WS_VISIBLE) && GetWindowRect(control, &bounds))
            bottom = max(bottom, bounds.bottom);
    }
    bottom += padding.bottom;
    delta = bottom - tabBody.bottom;

    positions = BeginDeferWindowPos(2 + ARRAYSIZE(buttons));
    if (!positions)
        return;

    positions = DeferWindowPos(positions, page, 0, 0, 0, pageRect.right - pageRect.left, bottom - pageRect.top,
                               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (positions)
    {
        positions = DeferWindowPos(positions, tabs, 0, 0, 0, tabRect.right - tabRect.left,
                                   tabRect.bottom - tabRect.top + delta, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    for (UINT i = 0; positions && i < ARRAYSIZE(buttons); ++i)
    {
        HWND button = GetDlgItem(sheet, buttons[i]);
        RECT bounds;

        if (button && GetWindowRect(button, &bounds))
        {
            MapWindowPoints(0, sheet, (POINT *)&bounds, 2);
            positions = DeferWindowPos(positions, button, 0, bounds.left, bounds.top + delta, 0, 0,
                                       SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    if (!positions || !EndDeferWindowPos(positions) || !delta)
        return;

    height = sheetRect.bottom - sheetRect.top + delta;
    top    = sheetRect.top;
    if (delta > 0)
    {
        MONITORINFO monitor = {sizeof(monitor)};

        if (GetMonitorInfo(MonitorFromWindow(sheet, MONITOR_DEFAULTTONEAREST), &monitor))
            top = max(monitor.rcWork.top, min(top, monitor.rcWork.bottom - height));
    }

    SetWindowPos(sheet, 0, sheetRect.left, top, sheetRect.right - sheetRect.left, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

static LRESULT CALLBACK OptionsSheetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data)
{
    UNREFERENCED_PARAMETER(data);

    if (msg == WM_FIT_OPTIONS_PAGE)
    {
        FitOptionsPage(hwnd);
        return 0;
    }
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, OptionsSheetProc, id);

    return DefSubclassProc(hwnd, msg, wp, lp);
}

static int CALLBACK InitOptionsSheet(HWND hwnd, UINT msg, LPARAM lp)
{
    UNREFERENCED_PARAMETER(lp);

    if (msg == PSCB_INITIALIZED && SetWindowSubclass(hwnd, OptionsSheetProc, 0, 0))
        PostMessage(hwnd, WM_FIT_OPTIONS_PAGE, 0, 0);

    return 0;
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
    options.fFontItalic           = g_fFontItalic;
    options.nLineSpacing          = g_nLineSpacing;
    options.nAppearanceStyle      = g_nAppearanceStyle;
    options.nPalette               = g_nPalette;
    options.fShowStatusbar        = g_fShowStatusbar;
    options.fLineNumbers          = g_fLineNumbers;
    options.fLongLines            = g_fLongLines;
    options.fSelMargin            = g_fSelMargin;
    options.fChangeIndicators     = g_fChangeIndicators;
    options.fHighlightChanges     = g_fHighlightChanges;
    options.nLongLineLimit        = g_nLongLineLimit;
    options.fHighlightCurrentLine = g_nHLCurLine;
    options.nTabWidth             = g_nTabWidth;
    options.fInsertSpaces         = g_fInsertSpaces;
    options.fAutoIndent           = g_fAutoIndent;
    options.nFilterContext        = g_nFilterContext;
    memcpy(options.szFontName, g_szFontName, sizeof(options.szFontName));

    CoInitialize(0);

    // configure property sheet
    psh.dwFlags    = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW | PSH_USECALLBACK;
    psh.hwndParent = hwndParent;
    psh.nPages     = sizeof(psp) / sizeof(psp[0]);
    psh.ppsp       = psp;
    psh.pszCaption = _T("Options");
    psh.pfnCallback = InitOptionsSheet;

    // configure property sheet page(1)
    psp[0].lParam      = (LPARAM)&options;
    psp[0].dwFlags     = PSP_USETITLE;
    psp[0].hInstance   = g_hResourceModule; // GetModuleHandle(0);
    psp[0].pfnDlgProc  = AppearanceOptionsDlgProc;
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
        g_nFontSize         = options.nFontSize;
        g_fFontBold         = options.fFontBold;
        g_fFontItalic       = options.fFontItalic;
        g_nLineSpacing      = options.nLineSpacing;
        g_nAppearanceStyle  = options.nAppearanceStyle;
        g_nPalette          = options.nPalette;
        g_fShowStatusbar    = options.fShowStatusbar;
        g_fLineNumbers      = options.fLineNumbers;
        g_fLongLines        = options.fLongLines;
        g_fSelMargin        = options.fSelMargin;
        g_fChangeIndicators = options.fChangeIndicators;
        g_fHighlightChanges = options.fHighlightChanges;
        g_nLongLineLimit    = options.nLongLineLimit;
        g_nHLCurLine        = options.fHighlightCurrentLine;
        g_nTabWidth         = options.nTabWidth;
        g_fInsertSpaces     = options.fInsertSpaces;
        g_fAutoIndent       = options.fAutoIndent;
        g_nFilterContext    = options.nFilterContext;
        memcpy(g_szFontName, options.szFontName, sizeof(options.szFontName));
        ApplyRegSettings();
        SendMessage(hwndParent, WM_SIZE, 0, 0);
    }

    CoUninitialize();
}
