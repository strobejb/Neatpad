#ifndef NEATPAD_INCLUDED
#define NEATPAD_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif


#define APP_TITLE   _T("Neatpad")
#define REGLOC		_T("SOFTWARE\\Catch22\\Neatpad")

#define WEBSITE_STR _T("www.catch22.net")
#define WEBSITE_URL _T("https://") WEBSITE_STR
#define SYSLINK_STR _T("<A HREF=\"") WEBSITE_URL _T("\">") WEBSITE_STR _T("</A>")


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
extern BOOL		g_fShowLineEndings;
extern BOOL		g_fAddToExplorer;
extern BOOL		g_fReplaceNotepad;
extern BOOL		g_fShowStatusbar;
extern BOOL		g_fSearchMatchCase;
extern BOOL		g_fSearchWrap;
extern LONG		g_nInMemoryLimit;

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
extern LONG		g_nZoom;		// the text's zoom, in percent of the font size
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
void ApplyFonts();
void LoadRegSettings();
void SaveRegSettings();
void DeleteRuntimeFonts();
void LoadRegSysSettings();
void SaveRegSysSettings();
void AddRecentFile(TCHAR *szFileName);
BOOL OpenRecentFile(HWND hwnd, UINT nCommandId);
BOOL OpenPreviousFile(HWND hwnd);
void ClearRecentFiles();
void UpdateRecentMenu(HMENU hMenu);
BOOL SetExplorerContextMenu(BOOL fAddToMenu);
BOOL SetImageFileExecutionOptions(BOOL fReplaceWithCurrentApp);


//
//	OpenSave.c functions
//
// How to save: as the file is now, or as chosen in the Save As dialog
typedef struct
{
	UINT	nEncoding;		// NCP_*
	BOOL	fBom;			// write the encoding's byte-order mark
	UINT	nLineBreak;		// TXL_CR, TXL_LF or TXL_CRLF; 0 keeps them as they are
} SAVEOPTIONS;

BOOL DoOpenFile(HWND hwndMain, TCHAR *szFileName, TCHAR *szFileTitle);
BOOL DoSaveFile(HWND hwndMain, TCHAR *szFileName, TCHAR *szFileTitle, const SAVEOPTIONS *pOptions);
BOOL ShowOpenFileDlg(HWND hwnd, TCHAR *pstrFileName, TCHAR *pstrTitleName);
BOOL ShowSaveFileDlg(HWND hwnd, TCHAR *pstrFileName, TCHAR *pstrTitleName, SAVEOPTIONS *pOptions);
void HandleDropFiles(HWND hwnd, HDROP hDrop);
BOOL NeatpadOpenFile(HWND hwnd, TCHAR *szFile);
void ShowFileFormat(HWND hwndMain);
BOOL NeatpadSaveFile(HWND hwnd);
BOOL NeatpadSaveFileAs(HWND hwnd);
BOOL NeatpadReloadFile(HWND hwnd);
BOOL NeatpadTailFile(HWND hwnd);
BOOL NeatpadStopTail(HWND hwnd);
BOOL QuerySaveChanges(HWND hwnd);
void CheckFileChange(void);
void ShowRecoveredFile(HWND hwnd, const TCHAR *filename);

void RecoveryChanged(void);
void RecoveryTick(void);
void RecoveryReset(void);
void RecoverySaved(void);
BOOL RecoveryBeforeSave(void);
BOOL RecoveryClose(void);
void RecoveryShutdown(void);
BOOL RecoveryRestore(HWND hwnd);
void RecoveryRevert(HWND hwnd);

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
//	SearchBar.c functions
//
HWND CreateSearchBar(HWND hwndParent);
HWND ShowSearchBar(HWND hwndParent);
void ToggleSearchBar(HWND hwndParent, BOOL fReplace);
void HideSearchBar(void);
int  SearchBarHeight(void);
BOOL SearchBarIsVisible(void);
UINT SearchBarFindNext(HWND hwndParent);
UINT SearchBarFindPrevious(HWND hwndParent);

//
//	NotifyBar.c functions
//
#define NOTIFY_CHANGED	1	// another program has changed the open file
#define NOTIFY_DELETED	2	// or deleted it
#define NOTIFY_READONLY	3	// or is writing it, so it opened read-only
#define NOTIFY_TAILING	4	// the file is being followed as it grows
#define NOTIFY_RECOVERED 5
#define NOTIFY_RECOVERED_UNTITLED 6
#define NOTIFY_RECOVERY_WRITE_FAILED 7
#define NOTIFY_RECOVERY_READ_FAILED 8
#define NOTIFY_RECOVERY_ORIGINAL_CHANGED 9
#define NOTIFY_SESSION_RESTORED 10
#define NOTIFY_SESSION_RESTORED_UNTITLED 11

    HWND CreateNotifyBar(HWND hwndParent);
    void ShowNotifyBar(UINT nNotice);
    void HideNotifyBar(void);
    int  NotifyBarHeight(void);
    BOOL NotifyBarIsVisible(void);

    //
    //	Toolbars.c functions
    //
    HWND  CreateStatusBar(HWND hwndParent);
    HFONT StatusBarFont(void);
    void  SetStatusBarParts(HWND hwndSB);
    int   StatusBarMenuSelect(HWND hwnd, HWND hwndSB, WPARAM wParam, LPARAM lParam);
    void  SetStatusBarText(HWND hwndSB, UINT nPart, UINT uStyle, TCHAR *fmt, ...);
    void  SetStatusBarLink(HWND hwndSB, UINT nPart, TCHAR *text);
    void  FormatCount(TCHAR *buf, int cch, unsigned __int64 value);
    void  ShowGotoLineDlg(HWND hwndMain);
    BOOL  DrawStatusBarLink(const DRAWITEMSTRUCT *dis);
    BOOL  StatusBarLinkAt(HWND hwndSB, POINT pt);
    void  UpdateStatusBarFileInfo(void);

#define STATUS_PART_CURSOR		0
#define STATUS_PART_DOCSTATS	1
#define STATUS_PART_MESSAGE		2
#define STATUS_PART_EDITMODE	STATUS_PART_DOCSTATS
#define STATUS_PART_INDEX		3
#define STATUS_PART_ZOOM		4
#define STATUS_PART_LINEFMT		5
#define STATUS_PART_ENCODING	6

#ifdef __cplusplus
}
#endif

#endif
