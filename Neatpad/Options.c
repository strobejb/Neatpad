//
//	Neatpad
//	Options.c
//
//	www.catch22.net
//
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

BOOL CALLBACK FontOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK MiscOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK DisplayOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

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
BOOL  g_fAddToExplorerContextMenu;

COLORREF g_rgbColourList[TXC_MAX_COLOURS];
COLORREF g_rgbCustColours[16];
extern COLORREF g_rgbAutoColourList[];

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

void SetExplorerContextMenu(BOOL fAddToMenu)
{
	if(fAddToMenu)
	{
		TCHAR szAppPath[MAX_PATH];
		TCHAR szDefaultStr[MAX_PATH];

		GetModuleFileName(0, szAppPath, MAX_PATH);

		wsprintf(szDefaultStr, _T("\"%s\" \"%%1\""), szAppPath);

		RegSetValue(HKEY_CLASSES_ROOT, CONTEXT_CMD_LOC, REG_SZ, szDefaultStr, lstrlen(szDefaultStr) * sizeof(TCHAR));
	}
	else
	{
		RegDeleteKey(HKEY_CLASSES_ROOT, CONTEXT_CMD_LOC);
		RegDeleteKey(HKEY_CLASSES_ROOT, CONTEXT_APP_LOC);
	}
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

	GetSettingInt(hKey, _T("AddExplorer"),	 &g_fAddToExplorerContextMenu, FALSE);
	
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

	WriteSettingInt(hKey, _T("AddExplorer"),  g_fAddToExplorerContextMenu);
	
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

void ApplyRegSettings()
{
	int i;

	if(g_hFont)
		DeleteObject(g_hFont);

	g_hFont = EasyCreateFont(g_nFontSize, g_fFontBold, g_nFontSmoothing, g_szFontName);

	TextView_SetLineSpacing(g_hwndTextView, g_nPaddingAbove, g_nPaddingBelow);

	TextView_SetStyleBool(g_hwndTextView, TXS_SELMARGIN,	g_fSelMargin);
	TextView_SetStyleBool(g_hwndTextView, TXS_LINENUMBERS,	g_fLineNumbers);
	TextView_SetStyleBool(g_hwndTextView, TXS_LONGLINES,	g_fLongLines);

	TextView_SetStyleBool(g_hwndTextView, TXS_HIGHLIGHTCURLINE,	g_nHLCurLine);

	TextView_SetCaretWidth(g_hwndTextView, 2);
	TextView_SetLongLine(g_hwndTextView, g_nLongLineLimit);
	
	SendMessage(g_hwndTextView, WM_SETFONT, (WPARAM)g_hFont, 0);

	for(i = 0; i < TXC_MAX_COLOURS; i++)
	{
		TextView_SetColor(g_hwndTextView, i, g_rgbColourList[i]);
	}

	//
	//	Add
	//
	SetExplorerContextMenu(g_fAddToExplorerContextMenu);
}

void ShowProperties(HWND hwndParent)
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
	psp[0].hInstance	= GetModuleHandle(0);
	psp[0].pfnDlgProc	= FontOptionsDlgProc;
	psp[0].pszTemplate	= MAKEINTRESOURCE(IDD_FONT);
	psp[0].pszTitle		= _T("Font");

	// configure property sheet page(2)
	psp[1].dwFlags		= PSP_USETITLE;
	psp[1].hInstance	= GetModuleHandle(0);
	psp[1].pfnDlgProc	= DisplayOptionsDlgProc;
	psp[1].pszTemplate	= MAKEINTRESOURCE(IDD_DISPLAY);
	psp[1].pszTitle		= _T("Display");

	// configure property sheet page(2)
	psp[2].dwFlags		= PSP_USETITLE;
	psp[2].hInstance	= GetModuleHandle(0);
	psp[2].pfnDlgProc	= MiscOptionsDlgProc;
	psp[2].pszTemplate	= MAKEINTRESOURCE(IDD_OPTIONS);
	psp[2].pszTitle		= _T("Settings");

	if(PropertySheet(&psh))
	{
		ApplyRegSettings();		
	}

	CoUninitialize();
}
