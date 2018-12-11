#ifndef NEATPAD_INCLUDED
#define NEATPAD_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif


#define APP_TITLE   _T("Neatpad")
#define REGLOC		_T("SOFTWARE\\Catch22\\Neatpad")

#define WEBSITE_STR _T("www.catch22.net")
#define WEBSITE_URL _T("http://") WEBSITE_STR
#define SYSLINK_STR _T("Updates available at <A HREF=\"") WEBSITE_URL _T("\">") WEBSITE_STR _T("</A>")


//
//	Global variables used for Neatpad settings
//
extern LONG		g_nFontSize;
extern BOOL		g_fFontBold;
extern TCHAR	g_szFontName[];
extern LONG		g_nFontSmoothing;
extern BOOL		g_fLineNumbers;
extern BOOL		g_fLongLines;
extern BOOL		g_fSelMargin;
extern BOOL		g_fSaveOnExit;
extern int		g_nLongLineLimit;
extern BOOL		g_nHLCurLine;
extern BOOL		g_fAddToExplorer;
extern BOOL		g_fReplaceNotepad;
extern BOOL		g_fShowStatusbar;

extern LONG		g_nPaddingAbove;
extern LONG		g_nPaddingBelow;
extern LONG		g_fPaddingFlags;
extern COLORREF g_rgbColourList[];
extern COLORREF g_rgbCustColours[];


#define COURIERNEW	1
#define LUCIDACONS	2

//	Global runtime variables
extern HWND		g_hwndTextView;
extern HWND		g_hwndMain;
extern HFONT	g_hFont;
extern TCHAR	g_szFileName[];
extern TCHAR	g_szFileTitle[];
extern BOOL		g_fFileChanged;
extern HINSTANCE g_hResourceModule;

//
//	Global functions
//
void	SetWindowFileName(HWND hwnd, TCHAR *szFileName, BOOL fModified);
HFONT	EasyCreateFont(int nPointSize, BOOL fBold, DWORD dwQuality, TCHAR *szFace);
HDC		ShowPrintDlg(HWND hwndParent);

//
//	Options.c functions
//
void ShowOptions(HWND hwndParent);
void ApplyRegSettings();
void LoadRegSettings();
void SaveRegSettings();
void LoadRegSysSettings();
void SaveRegSysSettings();
BOOL SetExplorerContextMenu(BOOL fAddToMenu);
BOOL SetImageFileExecutionOptions(BOOL fReplaceWithCurrentApp);


//
//	OpenSave.c functions
//
BOOL DoOpenFile(HWND hwndMain, TCHAR *szFileName, TCHAR *szFileTitle);
BOOL DoSaveFile(HWND hwndMain, TCHAR *szFileName, TCHAR *szFileTitle);
BOOL ShowOpenFileDlg(HWND hwnd, TCHAR *pstrFileName, TCHAR *pstrTitleName);
BOOL ShowSaveFileDlg(HWND hwnd, TCHAR *pstrFileName, TCHAR *pstrTitleName);
void HandleDropFiles(HWND hwnd, HDROP hDrop);
void NeatpadOpenFile(HWND hwnd, TCHAR *szFile);

//
//	NeatUtils.c functions
//
BOOL ResolveShortcut(TCHAR *pszShortcut, TCHAR *pszFilePath, int nPathLen);
BOOL SaveFileData(TCHAR *szPath, HWND hwnd);
BOOL LoadFileData(TCHAR *szPath, HWND hwnd);

BOOL CheckMenuCommand(HMENU hMenu, int nCommandId, BOOL fChecked);
BOOL EnableMenuCommand(HMENU hmenu, int nCommandId, BOOL fEnable);
BOOL EnableDlgItem(HWND hDlg, UINT nCommandId, BOOL fEnable);
void CenterWindow(HWND hwnd);
BOOL EnableDialogTheme(HWND hwnd);


HFONT EasyCreateFont(int nPointSize, BOOL fBold, DWORD dwQuality, TCHAR *szFace);
int	  PointsToLogical(int nPointSize);
HFONT CreateBoldFontFromHwnd(HWND hwnd);

//
//	Search.c functions
//
#define FIND_PAGE 0
#define REPLACE_PAGE 1
#define GOTO_PAGE 2
HWND ShowFindDlg(HWND hwndParent, UINT nPage);


//
//	Toolbars.c functions
//
HWND CreateStatusBar (HWND hwndParent);
void SetStatusBarParts(HWND hwndSB);
int  StatusBarMenuSelect(HWND hwnd, HWND hwndSB, WPARAM wParam, LPARAM lParam);
void SetStatusBarText(HWND hwndSB, UINT nPart, UINT uStyle, TCHAR *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif